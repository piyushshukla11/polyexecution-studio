package pes.worker.runner

import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.Test
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.ResourceLimits
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class ProcessSandboxRunnerTest {
    private val okJson = """{"job_id":"stdin-test","status":"OK","exit_code":0,"signal":0,"wall_ms":1,"cpu_ms":1,"peak_memory_kb":1,"stdout":"","stderr":""}"""

    @Test
    fun productionCommandKeepsRequestValuesAsSeparateArguments() {
        val runner = ProcessSandboxRunner(Path.of("/tmp/pes-runner-test"))
        val workspace = WorkspaceHandoff(Path.of("/tmp/pes-job-test"), "source.c", "target")
        val command = runner.buildCommand(
            jobId = "job-1",
            binaryPath = "/usr/bin/gcc",
            args = listOf("-o", "/workspace/target", "value;still-one-arg"),
            limits = ResourceLimits.newBuilder().setCpuMs(1000).setWallMs(2000).build(),
            workspace = workspace
        )

        assertEquals("value;still-one-arg", command.last())
        assertTrue(command.containsAll(listOf("--workspace", "/tmp/pes-job-test", "--source-file", "source.c", "--artifact-out", "target", "--")))
        assertFalse(command.any { it == "sh" || it == "-c" })
    }

    @Test
    fun missingProductionRunnerFailsClosedWithoutFallback() = runBlocking {
        val missing = Path.of("/tmp/pes-missing-runner-${System.nanoTime()}")
        val result = ProcessSandboxRunner(missing).run(
            jobId = "fail-closed",
            binaryPath = "/usr/bin/python3",
            args = listOf("/workspace/source.py"),
            limits = ResourceLimits.getDefaultInstance(),
            workspace = WorkspaceHandoff(Path.of("/tmp/pes-job-does-not-exist"), "source.py")
        )
        assertEquals(ExecutionStatus.SANDBOX_ERROR, result.status)
        assertTrue(result.stderr.contains("not executable"))
        assertFalse(Files.exists(Path.of("/tmp/pes-job-does-not-exist")))
    }

    @Test
    fun adapterForwardsInputAndExplicitlyClosesEmptyInput() = runBlocking {
        val runnerPath = executablePlaceholder()
        try {
            val inputOutput = ByteArrayOutputStream()
            val inputProcess = FakeProcess(okJson, inputOutput, waitResult = true)
            val inputRunner = ProcessSandboxRunner(runnerPath) { inputProcess }
            val inputResult = inputRunner.run(
                "stdin-test", "/usr/bin/cat", emptyList(), ResourceLimits.getDefaultInstance(), "typed input\n"
            )
            assertEquals(ExecutionStatus.OK, inputResult.status)
            assertEquals("typed input\n", inputOutput.toString(Charsets.UTF_8))
            assertTrue(inputProcess.stdinClosed)

            val emptyOutput = ByteArrayOutputStream()
            val emptyProcess = FakeProcess(okJson, emptyOutput, waitResult = true)
            val emptyRunner = ProcessSandboxRunner(runnerPath) { emptyProcess }
            assertEquals(
                ExecutionStatus.OK,
                emptyRunner.run("stdin-test", "/usr/bin/cat", emptyList(), ResourceLimits.getDefaultInstance(), "").status
            )
            assertEquals(0, emptyOutput.size())
            assertTrue(emptyProcess.stdinClosed)
        } finally {
            Files.deleteIfExists(runnerPath)
        }
    }

    @Test
    fun oversizedInputIsRejectedAndNonReadingTargetRespectsDeadline() = runBlocking {
        val runnerPath = executablePlaceholder()
        try {
            val started = AtomicInteger()
            val oversized = ProcessSandboxRunner(runnerPath) {
                started.incrementAndGet()
                FakeProcess(okJson, ByteArrayOutputStream(), waitResult = true)
            }.run(
                "oversized", "/usr/bin/cat", emptyList(), ResourceLimits.getDefaultInstance(),
                "x".repeat(ProcessSandboxRunner.MAX_STDIN_BYTES + 1)
            )
            assertEquals(ExecutionStatus.SANDBOX_ERROR, oversized.status)
            assertEquals(0, started.get())

            val blockingProcess = FakeProcess(okJson, BlockingOutputStream(), waitResult = false)
            val timeoutResult = ProcessSandboxRunner(runnerPath) { blockingProcess }.run(
                "blocked-input", "/usr/bin/non-reader", emptyList(),
                ResourceLimits.newBuilder().setWallMs(1).build(), "x".repeat(ProcessSandboxRunner.MAX_STDIN_BYTES)
            )
            assertEquals(ExecutionStatus.TIMEOUT, timeoutResult.status)
            assertTrue(blockingProcess.stdinClosed)
        } finally {
            Files.deleteIfExists(runnerPath)
        }
    }

    private fun executablePlaceholder(): Path {
        val path = Files.createTempFile("pes-runner-placeholder-", ".bin")
        assertTrue(path.toFile().setExecutable(true, true))
        return path
    }

    private class FakeProcess(
        response: String,
        private val capturedStdin: OutputStream,
        private val waitResult: Boolean
    ) : Process() {
        private val stdout = ByteArrayInputStream(response.toByteArray(Charsets.UTF_8))
        private val stderr = ByteArrayInputStream(byteArrayOf())
        @Volatile var stdinClosed = false
            private set

        override fun getOutputStream(): OutputStream = object : OutputStream() {
            override fun write(b: Int) = capturedStdin.write(b)
            override fun write(b: ByteArray, off: Int, len: Int) = capturedStdin.write(b, off, len)
            override fun flush() = capturedStdin.flush()
            override fun close() {
                stdinClosed = true
                capturedStdin.close()
            }
        }

        override fun getInputStream() = stdout
        override fun getErrorStream() = stderr
        override fun waitFor(): Int = 0
        override fun waitFor(timeout: Long, unit: TimeUnit): Boolean = waitResult
        override fun exitValue(): Int = if (waitResult) 0 else throw IllegalThreadStateException()
        override fun destroy() { stdinClosed = true }
        override fun destroyForcibly(): Process { destroy(); return this }
        override fun isAlive(): Boolean = !waitResult
    }

    private class BlockingOutputStream : OutputStream() {
        @Volatile private var closed = false
        override fun write(b: Int) {
            synchronized(this) {
                while (!closed) (this as java.lang.Object).wait()
            }
            throw java.io.IOException("stream closed")
        }
        override fun write(b: ByteArray, off: Int, len: Int) {
            if (len > 0) write(b[off].toInt())
        }
        override fun close() {
            synchronized(this) {
                closed = true
                (this as java.lang.Object).notifyAll()
            }
        }
    }
}
