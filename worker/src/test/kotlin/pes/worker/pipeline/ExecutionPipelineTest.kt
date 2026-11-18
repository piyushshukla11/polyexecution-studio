package pes.worker.pipeline

import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.Test
import pes.execution.v1.ExecuteRequest
import pes.execution.v1.ExecutionStage
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.Language
import pes.execution.v1.ResourceLimits
import pes.worker.model.RunnerResult
import pes.worker.runner.FakeRunnerClient
import pes.worker.runner.WorkspaceHandoff
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.attribute.PosixFilePermission

class ExecutionPipelineTest {

    @Test
    fun validationRejectsUnsafeRequestsBeforeCallingRunner() = runBlocking {
        val fake = FakeRunnerClient()
        val pipeline = ExecutionPipeline(fake)
        val invalidRequests = listOf(
            ExecuteRequest.newBuilder().setLanguage(Language.LANGUAGE_C).setSourceCode("int main(){}").build(),
            ExecuteRequest.newBuilder().setJobId("job;rm -rf /").setLanguage(Language.LANGUAGE_C)
                .setSourceCode("int main(){}").build(),
            ExecuteRequest.newBuilder().setJobId("job-1").setLanguage(Language.LANGUAGE_UNSPECIFIED)
                .setSourceCode("int main(){}").build(),
            ExecuteRequest.newBuilder().setJobId("job-1").setLanguage(Language.LANGUAGE_C)
                .setSourceCode("").build(),
            ExecuteRequest.newBuilder().setJobId("job-1").setLanguage(Language.LANGUAGE_C)
                .setSourceCode("int main(){}").setStdin("x".repeat(ExecutionPipeline.MAX_STDIN_BYTES + 1)).build()
        )

        invalidRequests.forEach { assertEquals(ExecutionStatus.SANDBOX_ERROR, pipeline.execute(it).status) }
        assertTrue(fake.calls.isEmpty())
    }

    @Test
    fun limitsAreNormalizedToDefaultsAndCaps() {
        val pipeline = ExecutionPipeline(FakeRunnerClient())
        val defaults = ExecuteRequest.newBuilder().setJobId("default-job").setLanguage(Language.LANGUAGE_C)
            .setSourceCode("int main() { return 0; }")
            .setLimits(ResourceLimits.newBuilder().setCpuMs(0).setWallMs(-1).build()).build()
        val defaultLimits = (pipeline.validateRequest(defaults) as ValidationResult.Valid).normalizedLimits
        assertEquals(ExecutionPipeline.DEFAULT_CPU_MS, defaultLimits.cpuMs)
        assertEquals(ExecutionPipeline.DEFAULT_WALL_MS, defaultLimits.wallMs)
        assertEquals(ExecutionPipeline.DEFAULT_MEMORY_MB, defaultLimits.memoryMb)
        assertEquals(ExecutionPipeline.DEFAULT_MAX_OUTPUT_KB, defaultLimits.maxOutputKb)

        val capped = defaults.toBuilder().setJobId("capped-job")
            .setLimits(ResourceLimits.newBuilder().setCpuMs(Long.MAX_VALUE).setWallMs(Long.MAX_VALUE)
                .setMemoryMb(Long.MAX_VALUE).setMaxOutputKb(Long.MAX_VALUE)).build()
        val cappedLimits = (pipeline.validateRequest(capped) as ValidationResult.Valid).normalizedLimits
        assertEquals(ExecutionPipeline.MAX_CPU_MS, cappedLimits.cpuMs)
        assertEquals(ExecutionPipeline.MAX_WALL_MS, cappedLimits.wallMs)
        assertEquals(ExecutionPipeline.MAX_MEMORY_MB, cappedLimits.memoryMb)
        assertEquals(ExecutionPipeline.MAX_OUTPUT_KB, cappedLimits.maxOutputKb)
    }

    @Test
    fun compileFailureStopsBeforeExecution() = runBlocking {
        val compileFailure = RunnerResult(
            jobId = "compile-error-compile", status = ExecutionStatus.RUNTIME_ERROR, exitCode = 1, signal = 0,
            wallMs = 22, cpuMs = 10, peakMemoryKb = 128, stdout = "", stderr = "compiler syntax diagnostic"
        )
        val fake = FakeRunnerClient(listOf(compileFailure))
        val response = ExecutionPipeline(fake).execute(cRequest("compile-error", "bad C"))

        assertEquals(ExecutionStatus.COMPILE_ERROR, response.status)
        assertTrue(response.stderr.contains("compiler syntax diagnostic"))
        assertEquals(1, fake.calls.size)
        assertEquals("/usr/bin/gcc", fake.calls.single().binaryPath)
        assertEquals("source.c", fake.calls.single().workspace?.sourceName)
        assertEquals("target", fake.calls.single().workspace?.artifactName)
        assertWorkspaceRemoved(fake.calls.single().workspace)
    }

    @Test
    fun compileSetupFailureIsNotMisreportedAsCompileError() = runBlocking {
        val setupFailure = RunnerResult.sandboxError("setup-job-compile", "sandbox setup unavailable")
        val fake = FakeRunnerClient(listOf(setupFailure))
        val response = ExecutionPipeline(fake).execute(cRequest("setup-job", "int main(void) { return 0; }"))

        assertEquals(ExecutionStatus.SANDBOX_ERROR, response.status)
        assertTrue(response.stderr.contains("sandbox setup unavailable"))
        assertEquals(1, fake.calls.size)
        assertWorkspaceRemoved(fake.calls.single().workspace)
    }

    @Test
    fun compileTimeoutKeepsTimeoutStatusAndStopsBeforeExecution() = runBlocking {
        val timeout = RunnerResult(
            jobId = "compile-timeout-compile", status = ExecutionStatus.TIMEOUT, exitCode = 124, signal = 9,
            wallMs = 8000, cpuMs = 5000, peakMemoryKb = 128, stdout = "", stderr = "compiler deadline"
        )
        val fake = FakeRunnerClient(listOf(timeout))
        val response = ExecutionPipeline(fake).execute(cRequest("compile-timeout", "int main(void) { return 0; }"))

        assertEquals(ExecutionStatus.TIMEOUT, response.status)
        assertEquals("compiler deadline", response.stderr)
        assertEquals(1, fake.calls.size)
        assertWorkspaceRemoved(fake.calls.single().workspace)
    }

    @Test
    fun cDispatchUsesSandboxPathsAndOnlyFakeReturnsCannedOutput() = runBlocking {
        val fake = FakeRunnerClient()
        val response = ExecutionPipeline(fake).execute(cRequest("c-good", "int main(void) { return 0; }", "input\n"))

        assertEquals(ExecutionStatus.OK, response.status)
        assertTrue(response.stdout.startsWith("FAKE_RUNNER:"))
        assertEquals(2, fake.calls.size)
        assertEquals(listOf("-O2", "-o", "/workspace/target", "/workspace/source.c"), fake.calls[0].args)
        assertEquals("input\n", fake.calls[1].stdin)
        assertNull(fake.calls[1].workspace)
        assertWorkspaceRemoved(fake.calls[0].workspace)
    }

    @Test
    fun pythonDispatchCopiesSourceIntoSandboxAndForwardsStdin() = runBlocking {
        val fake = FakeRunnerClient()
        val response = ExecutionPipeline(fake).execute(pyRequest("py-good", "print(input())", "phone input\n"))

        assertEquals(ExecutionStatus.OK, response.status)
        assertTrue(response.stdout.startsWith("FAKE_RUNNER:"))
        assertEquals(2, fake.calls.size)
        assertEquals("/usr/bin/python3", fake.calls[0].binaryPath)
        assertEquals(listOf("-m", "py_compile", "/workspace/source.py"), fake.calls[0].args)
        assertEquals("/usr/bin/python3", fake.calls[1].binaryPath)
        assertEquals(listOf("/workspace/source.py"), fake.calls[1].args)
        assertEquals("phone input\n", fake.calls[1].stdin)
        assertEquals("source.py", fake.calls[0].workspace?.sourceName)
        assertNull(fake.calls[0].workspace?.artifactName)
        assertWorkspaceRemoved(fake.calls[0].workspace)
    }

    @Test
    fun emptyStdinIsForwardedAsEmptyAndArtifactIsBoundedAndPrivate() = runBlocking {
        val fake = InspectingFakeRunner()
        val response = ExecutionPipeline(fake).execute(cRequest("empty-stdin", "int main(void) { return 0; }"))

        assertEquals(ExecutionStatus.OK, response.status)
        assertEquals(listOf("", ""), fake.calls.map { it.stdin })
        assertTrue(fake.workspaceWasPrivate)
        assertTrue(fake.artifactWasPrivate)
        assertWorkspaceRemoved(fake.calls.first().workspace)
    }

    private class InspectingFakeRunner : FakeRunnerClient() {
        var workspaceWasPrivate = false
        var artifactWasPrivate = false

        override suspend fun run(
            jobId: String,
            binaryPath: String,
            args: List<String>,
            limits: ResourceLimits,
            stdin: String,
            workspace: WorkspaceHandoff?
        ): RunnerResult {
            if (workspace != null) {
                val dirMode = Files.getAttribute(workspace.directory, "unix:mode", LinkOption.NOFOLLOW_LINKS) as Int
                val source = workspace.directory.resolve(workspace.sourceName)
                val sourceMode = Files.getAttribute(source, "unix:mode", LinkOption.NOFOLLOW_LINKS) as Int
                workspaceWasPrivate = dirMode and 0x3f == 0 && sourceMode and 0x3f == 0
            }
            val result = super.run(jobId, binaryPath, args, limits, stdin, workspace)
            if (workspace?.artifactName != null) {
                val artifact = workspace.directory.resolve(workspace.artifactName)
                val mode = Files.getAttribute(artifact, "unix:mode", LinkOption.NOFOLLOW_LINKS) as Int
                artifactWasPrivate = Files.size(artifact) in 1..ExecutionPipeline.MAX_ARTIFACT_BYTES.toLong() &&
                    mode and 0x3f == 0 && Files.getAttribute(artifact, "posix:permissions").toString().contains("OWNER_READ")
            }
            return result
        }
    }

    private fun cRequest(jobId: String, source: String, stdin: String = ""): ExecuteRequest =
        ExecuteRequest.newBuilder().setJobId(jobId).setLanguage(Language.LANGUAGE_C)
            .setSourceCode(source).setStdin(stdin).build()

    private fun pyRequest(jobId: String, source: String, stdin: String): ExecuteRequest =
        ExecuteRequest.newBuilder().setJobId(jobId).setLanguage(Language.LANGUAGE_PYTHON)
            .setSourceCode(source).setStdin(stdin).build()

    @Test
    fun javaRequestExecutesViaJavaBinary() = runBlocking {
        val compileOk = RunnerResult(
            jobId = "java-job-compile", status = ExecutionStatus.OK, exitCode = 0, signal = 0,
            wallMs = 40, cpuMs = 30, peakMemoryKb = 40000, stdout = "", stderr = ""
        )
        val runOk = RunnerResult(
            jobId = "java-job", status = ExecutionStatus.OK, exitCode = 0, signal = 0,
            wallMs = 120, cpuMs = 45, peakMemoryKb = 32000, stdout = "hello java", stderr = ""
        )
        val fake = FakeRunnerClient(listOf(compileOk, runOk))
        val req = ExecuteRequest.newBuilder()
            .setJobId("java-job")
            .setLanguage(Language.LANGUAGE_JAVA)
            .setSourceCode("public class Main { public static void main(String[] args) { System.out.println(\"hello java\"); } }")
            .build()
        val stages = mutableListOf<ExecutionStage>()
        val response = ExecutionPipeline(fake).execute(req) { stages.add(it) }
        assertEquals(ExecutionStatus.OK, response.status)
        assertEquals("hello java", response.stdout)
        assertEquals(2, fake.calls.size)
        assertEquals("Main.java", fake.calls[0].workspace?.sourceName)
        assertEquals(listOf(ExecutionStage.STAGE_COMPILING, ExecutionStage.STAGE_RUNNING), stages)
        assertWorkspaceRemoved(fake.calls.first().workspace)
    }

    @Test
    fun javaCompileFailureStopsBeforeExecution() = runBlocking {
        val compileFail = RunnerResult(
            jobId = "java-job-compile", status = ExecutionStatus.RUNTIME_ERROR, exitCode = 1, signal = 0,
            wallMs = 40, cpuMs = 30, peakMemoryKb = 40000, stdout = "", stderr = "Main.java:2: error: ';' expected"
        )
        val fake = FakeRunnerClient(listOf(compileFail))
        val req = ExecuteRequest.newBuilder()
            .setJobId("java-job")
            .setLanguage(Language.LANGUAGE_JAVA)
            .setSourceCode("public class Main { broken }")
            .build()
        val stages = mutableListOf<ExecutionStage>()
        val response = ExecutionPipeline(fake).execute(req) { stages.add(it) }
        assertEquals(ExecutionStatus.COMPILE_ERROR, response.status)
        assertTrue(response.stderr.contains("error: ';' expected"))
        assertEquals(1, fake.calls.size)
        assertEquals(listOf(ExecutionStage.STAGE_COMPILING), stages)
        assertFalse(stages.contains(ExecutionStage.STAGE_RUNNING))
        assertWorkspaceRemoved(fake.calls.single().workspace)
    }

    @Test
    fun pythonSyntaxFailureStopsBeforeExecution() = runBlocking {
        val compileFail = RunnerResult(
            jobId = "py-job-compile", status = ExecutionStatus.RUNTIME_ERROR, exitCode = 1, signal = 0,
            wallMs = 20, cpuMs = 15, peakMemoryKb = 15000, stdout = "", stderr = "SyntaxError: invalid syntax"
        )
        val fake = FakeRunnerClient(listOf(compileFail))
        val req = ExecuteRequest.newBuilder()
            .setJobId("py-job")
            .setLanguage(Language.LANGUAGE_PYTHON)
            .setSourceCode("def broken(:")
            .build()
        val stages = mutableListOf<ExecutionStage>()
        val response = ExecutionPipeline(fake).execute(req) { stages.add(it) }
        assertEquals(ExecutionStatus.COMPILE_ERROR, response.status)
        assertTrue(response.stderr.contains("SyntaxError: invalid syntax"))
        assertEquals(1, fake.calls.size)
        assertEquals(listOf(ExecutionStage.STAGE_COMPILING), stages)
        assertFalse(stages.contains(ExecutionStage.STAGE_RUNNING))
        assertWorkspaceRemoved(fake.calls.single().workspace)
    }

    @Test
    fun nodeRequestExecutesViaNodeBinary() = runBlocking {
        val checkOk = RunnerResult(
            jobId = "node-job-compile", status = ExecutionStatus.OK, exitCode = 0, signal = 0,
            wallMs = 30, cpuMs = 20, peakMemoryKb = 25000, stdout = "", stderr = ""
        )
        val runOk = RunnerResult(
            jobId = "node-job", status = ExecutionStatus.OK, exitCode = 0, signal = 0,
            wallMs = 80, cpuMs = 25, peakMemoryKb = 28000, stdout = "hello node", stderr = ""
        )
        val fake = FakeRunnerClient(listOf(checkOk, runOk))
        val req = ExecuteRequest.newBuilder()
            .setJobId("node-job")
            .setLanguage(Language.LANGUAGE_NODE)
            .setSourceCode("console.log('hello node')")
            .build()
        val stages = mutableListOf<ExecutionStage>()
        val response = ExecutionPipeline(fake).execute(req) { stages.add(it) }
        assertEquals(ExecutionStatus.OK, response.status)
        assertEquals("hello node", response.stdout)
        assertEquals(2, fake.calls.size)
        assertEquals("source.js", fake.calls[0].workspace?.sourceName)
        assertEquals(listOf(ExecutionStage.STAGE_COMPILING, ExecutionStage.STAGE_RUNNING), stages)
        assertWorkspaceRemoved(fake.calls.first().workspace)
    }

    @Test
    fun nodeSyntaxFailureStopsBeforeExecution() = runBlocking {
        val checkFail = RunnerResult(
            jobId = "node-job-compile", status = ExecutionStatus.RUNTIME_ERROR, exitCode = 1, signal = 0,
            wallMs = 30, cpuMs = 20, peakMemoryKb = 25000, stdout = "", stderr = "SyntaxError: Unexpected token"
        )
        val fake = FakeRunnerClient(listOf(checkFail))
        val req = ExecuteRequest.newBuilder()
            .setJobId("node-job")
            .setLanguage(Language.LANGUAGE_NODE)
            .setSourceCode("function broken( {")
            .build()
        val stages = mutableListOf<ExecutionStage>()
        val response = ExecutionPipeline(fake).execute(req) { stages.add(it) }
        assertEquals(ExecutionStatus.COMPILE_ERROR, response.status)
        assertTrue(response.stderr.contains("SyntaxError: Unexpected token"))
        assertEquals(1, fake.calls.size)
        assertEquals(listOf(ExecutionStage.STAGE_COMPILING), stages)
        assertFalse(stages.contains(ExecutionStage.STAGE_RUNNING))
        assertWorkspaceRemoved(fake.calls.single().workspace)
    }

    @Test
    fun kotlinRequestExecutesViaKotlincBinary() = runBlocking {
        val compileOk = RunnerResult(
            jobId = "kt-job-compile", status = ExecutionStatus.OK, exitCode = 0, signal = 0,
            wallMs = 100, cpuMs = 80, peakMemoryKb = 50000, stdout = "", stderr = ""
        )
        val runOk = RunnerResult(
            jobId = "kt-job", status = ExecutionStatus.OK, exitCode = 0, signal = 0,
            wallMs = 150, cpuMs = 50, peakMemoryKb = 35000, stdout = "hello kotlin", stderr = ""
        )
        val fake = FakeRunnerClient(listOf(compileOk, runOk))
        val req = ExecuteRequest.newBuilder()
            .setJobId("kt-job")
            .setLanguage(Language.LANGUAGE_KOTLIN)
            .setSourceCode("println(\"hello kotlin\")")
            .build()
        val stages = mutableListOf<ExecutionStage>()
        val response = ExecutionPipeline(fake).execute(req) { stages.add(it) }
        assertEquals(ExecutionStatus.OK, response.status)
        assertEquals("hello kotlin", response.stdout)
        assertEquals(2, fake.calls.size)
        assertEquals("source.kt", fake.calls[0].workspace?.sourceName)
        assertEquals(listOf(ExecutionStage.STAGE_COMPILING, ExecutionStage.STAGE_RUNNING), stages)
        assertWorkspaceRemoved(fake.calls.first().workspace)
    }

    @Test
    fun kotlinCompileFailureStopsBeforeExecution() = runBlocking {
        val compileFail = RunnerResult(
            jobId = "kt-job-compile", status = ExecutionStatus.RUNTIME_ERROR, exitCode = 1, signal = 0,
            wallMs = 100, cpuMs = 80, peakMemoryKb = 50000, stdout = "", stderr = "source.kt:1:1: error: unresolved reference"
        )
        val fake = FakeRunnerClient(listOf(compileFail))
        val req = ExecuteRequest.newBuilder()
            .setJobId("kt-job")
            .setLanguage(Language.LANGUAGE_KOTLIN)
            .setSourceCode("broken()")
            .build()
        val stages = mutableListOf<ExecutionStage>()
        val response = ExecutionPipeline(fake).execute(req) { stages.add(it) }
        assertEquals(ExecutionStatus.COMPILE_ERROR, response.status)
        assertTrue(response.stderr.contains("error: unresolved reference"))
        assertEquals(1, fake.calls.size)
        assertEquals(listOf(ExecutionStage.STAGE_COMPILING), stages)
        assertFalse(stages.contains(ExecutionStage.STAGE_RUNNING))
        assertWorkspaceRemoved(fake.calls.single().workspace)
    }

    private fun assertWorkspaceRemoved(workspace: WorkspaceHandoff?) {
        assertNotNull(workspace)
        assertFalse(Files.exists(workspace!!.directory, LinkOption.NOFOLLOW_LINKS))
    }
}
