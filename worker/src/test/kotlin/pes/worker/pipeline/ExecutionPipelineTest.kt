package pes.worker.pipeline

import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.Test
import pes.execution.v1.ExecuteRequest
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.Language
import pes.execution.v1.ResourceLimits
import pes.worker.model.RunnerResult
import pes.worker.runner.RunnerClient
import java.nio.file.Files
import java.nio.file.Path

class ExecutionPipelineTest {

    private class MockRunnerClient : RunnerClient {
        val calls = mutableListOf<CallRecord>()
        var compileResult: RunnerResult? = null
        var execResult: RunnerResult? = null
        var lastCreatedWorkspace: Path? = null

        data class CallRecord(
            val jobId: String,
            val binaryPath: String,
            val args: List<String>,
            val limits: ResourceLimits,
            val stdin: String
        )

        override suspend fun run(
            jobId: String,
            binaryPath: String,
            args: List<String>,
            limits: ResourceLimits,
            stdin: String
        ): RunnerResult {
            calls.add(CallRecord(jobId, binaryPath, args, limits, stdin))

            if (binaryPath == "/usr/bin/gcc") {
                // If compiling, optionally simulate creating target binary
                val targetIndex = args.indexOf("-o")
                if (targetIndex >= 0 && targetIndex + 1 < args.size) {
                    val targetPath = Path.of(args[targetIndex + 1])
                    lastCreatedWorkspace = targetPath.parent
                    if (compileResult?.status == ExecutionStatus.OK) {
                        Files.writeString(targetPath, "fake-binary")
                    }
                }
                return compileResult ?: RunnerResult(
                    jobId = jobId,
                    status = ExecutionStatus.OK,
                    exitCode = 0,
                    signal = 0,
                    wallMs = 120L,
                    cpuMs = 80L,
                    peakMemoryKb = 10240L,
                    stdout = "",
                    stderr = ""
                )
            }

            return execResult ?: RunnerResult(
                jobId = jobId,
                status = ExecutionStatus.OK,
                exitCode = 0,
                signal = 0,
                wallMs = 50L,
                cpuMs = 30L,
                peakMemoryKb = 2048L,
                stdout = "test-output",
                stderr = ""
            )
        }
    }

    @Test
    fun testValidationRejectsInvalidRequests() = runBlocking {
        val mockRunner = MockRunnerClient()
        val pipeline = ExecutionPipeline(mockRunner)

        // Empty jobId
        val res1 = pipeline.execute(ExecuteRequest.newBuilder()
            .setJobId("")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("int main(){}")
            .build())
        assertEquals(ExecutionStatus.SANDBOX_ERROR, res1.status)
        assertTrue(res1.stderr.contains("job_id must not be empty"))

        // Malicious / illegal characters in jobId
        val res2 = pipeline.execute(ExecuteRequest.newBuilder()
            .setJobId("job;rm -rf /")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("int main(){}")
            .build())
        assertEquals(ExecutionStatus.SANDBOX_ERROR, res2.status)
        assertTrue(res2.stderr.contains("is invalid: must match"))

        // Unspecified language
        val res3 = pipeline.execute(ExecuteRequest.newBuilder()
            .setJobId("job-123")
            .setLanguage(Language.LANGUAGE_UNSPECIFIED)
            .setSourceCode("int main(){}")
            .build())
        assertEquals(ExecutionStatus.SANDBOX_ERROR, res3.status)
        assertTrue(res3.stderr.contains("language must be specified"))

        // Empty source code
        val res4 = pipeline.execute(ExecuteRequest.newBuilder()
            .setJobId("job-123")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("")
            .build())
        assertEquals(ExecutionStatus.SANDBOX_ERROR, res4.status)
        assertTrue(res4.stderr.contains("source_code must not be empty"))

        // Ensure runner was never called on validation errors
        assertEquals(0, mockRunner.calls.size)
    }

    @Test
    fun testLimitNormalizationAppliesDefaultsAndBounds() {
        val mockRunner = MockRunnerClient()
        val pipeline = ExecutionPipeline(mockRunner)

        // Request with zero / negative limits -> should get defaults
        val req1 = ExecuteRequest.newBuilder()
            .setJobId("job-defaults")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("int main(){return 0;}")
            .setLimits(ResourceLimits.newBuilder().setCpuMs(0).setWallMs(-5).build())
            .build()

        val val1 = pipeline.validateRequest(req1)
        assertTrue(val1 is ValidationResult.Valid)
        val norm1 = (val1 as ValidationResult.Valid).normalizedLimits
        assertEquals(ExecutionPipeline.DEFAULT_CPU_MS, norm1.cpuMs)
        assertEquals(ExecutionPipeline.DEFAULT_WALL_MS, norm1.wallMs)
        assertEquals(ExecutionPipeline.DEFAULT_MEMORY_MB, norm1.memoryMb)
        assertEquals(ExecutionPipeline.DEFAULT_MAX_OUTPUT_KB, norm1.maxOutputKb)

        // Request exceeding maximum caps -> clamped to max
        val req2 = ExecuteRequest.newBuilder()
            .setJobId("job-caps")
            .setLanguage(Language.LANGUAGE_PYTHON)
            .setSourceCode("print('hi')")
            .setLimits(ResourceLimits.newBuilder()
                .setCpuMs(999999)
                .setWallMs(999999)
                .setMemoryMb(999999)
                .setMaxOutputKb(999999)
                .build())
            .build()

        val val2 = pipeline.validateRequest(req2)
        assertTrue(val2 is ValidationResult.Valid)
        val norm2 = (val2 as ValidationResult.Valid).normalizedLimits
        assertEquals(ExecutionPipeline.MAX_CPU_MS, norm2.cpuMs)
        assertEquals(ExecutionPipeline.MAX_WALL_MS, norm2.wallMs)
        assertEquals(ExecutionPipeline.MAX_MEMORY_MB, norm2.memoryMb)
        assertEquals(ExecutionPipeline.MAX_OUTPUT_KB, norm2.maxOutputKb)
    }

    @Test
    fun testCCompileFailureHaltsPipelineAndReportsCompileError() = runBlocking {
        val mockRunner = MockRunnerClient()
        mockRunner.compileResult = RunnerResult(
            jobId = "job-c-err-compile",
            status = ExecutionStatus.RUNTIME_ERROR,
            exitCode = 1,
            signal = 0,
            wallMs = 45L,
            cpuMs = 30L,
            peakMemoryKb = 5120L,
            stdout = "",
            stderr = "source.c:1:1: error: expected identifier or '(' before '{' token"
        )

        val pipeline = ExecutionPipeline(mockRunner)
        val req = ExecuteRequest.newBuilder()
            .setJobId("job-c-err")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("{ invalid syntax")
            .build()

        val resp = pipeline.execute(req)

        assertEquals("job-c-err", resp.jobId)
        assertEquals(ExecutionStatus.COMPILE_ERROR, resp.status)
        assertEquals(1, resp.exitCode)
        assertTrue(resp.stderr.contains("error: expected identifier"))
        // Execution step must NOT be invoked when compilation fails
        assertEquals(1, mockRunner.calls.size)
        assertEquals("/usr/bin/gcc", mockRunner.calls[0].binaryPath)
        // Workspace directory must be deleted
        assertNotNull(mockRunner.lastCreatedWorkspace)
        assertFalse(Files.exists(mockRunner.lastCreatedWorkspace!!))
    }

    @Test
    fun testCSuccessfulCompileAndExecution() = runBlocking {
        val mockRunner = MockRunnerClient()
        mockRunner.compileResult = RunnerResult(
            jobId = "job-c-ok-compile",
            status = ExecutionStatus.OK,
            exitCode = 0,
            signal = 0,
            wallMs = 80L,
            cpuMs = 50L,
            peakMemoryKb = 12000L,
            stdout = "",
            stderr = ""
        )
        mockRunner.execResult = RunnerResult(
            jobId = "job-c-ok",
            status = ExecutionStatus.OK,
            exitCode = 0,
            signal = 0,
            wallMs = 15L,
            cpuMs = 10L,
            peakMemoryKb = 2048L,
            stdout = "hello from C\n",
            stderr = ""
        )

        val pipeline = ExecutionPipeline(mockRunner)
        val req = ExecuteRequest.newBuilder()
            .setJobId("job-c-ok")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("#include <stdio.h>\nint main(){printf(\"hello from C\\n\");return 0;}")
            .build()

        val resp = pipeline.execute(req)

        assertEquals("job-c-ok", resp.jobId)
        assertEquals(ExecutionStatus.OK, resp.status)
        assertEquals(0, resp.exitCode)
        assertEquals("hello from C\n", resp.stdout)
        assertEquals(2, mockRunner.calls.size)
        assertEquals("/usr/bin/gcc", mockRunner.calls[0].binaryPath)
        assertTrue(mockRunner.calls[1].binaryPath.endsWith("target"))

        // Workspace directory must be cleaned up
        assertNotNull(mockRunner.lastCreatedWorkspace)
        assertFalse(Files.exists(mockRunner.lastCreatedWorkspace!!))
    }

    @Test
    fun testPythonDirectExecution() = runBlocking {
        val mockRunner = MockRunnerClient()
        mockRunner.execResult = RunnerResult(
            jobId = "job-py-ok",
            status = ExecutionStatus.OK,
            exitCode = 0,
            signal = 0,
            wallMs = 25L,
            cpuMs = 20L,
            peakMemoryKb = 8192L,
            stdout = "hello from python\n",
            stderr = ""
        )

        val pipeline = ExecutionPipeline(mockRunner)
        val req = ExecuteRequest.newBuilder()
            .setJobId("job-py-ok")
            .setLanguage(Language.LANGUAGE_PYTHON)
            .setSourceCode("print('hello from python')")
            .build()

        val resp = pipeline.execute(req)

        assertEquals("job-py-ok", resp.jobId)
        assertEquals(ExecutionStatus.OK, resp.status)
        assertEquals("hello from python\n", resp.stdout)
        // Python does NOT have a compilation step
        assertEquals(1, mockRunner.calls.size)
        assertEquals("/usr/bin/python3", mockRunner.calls[0].binaryPath)
        assertTrue(mockRunner.calls[0].args[0].endsWith("source.py"))
    }
}
