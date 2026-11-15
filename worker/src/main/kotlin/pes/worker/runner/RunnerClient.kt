package pes.worker.runner

import com.google.gson.Gson
import com.google.gson.JsonSyntaxException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.ResourceLimits
import pes.worker.model.RawRunnerJson
import pes.worker.model.RunnerResult
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermission
import java.nio.file.attribute.PosixFilePermissions
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

data class WorkspaceHandoff(
    val directory: Path,
    val sourceName: String,
    val artifactName: String? = null
)

interface RunnerClient {
    suspend fun run(
        jobId: String,
        binaryPath: String,
        args: List<String> = emptyList(),
        limits: ResourceLimits,
        stdin: String = "",
        workspace: WorkspaceHandoff? = null
    ): RunnerResult
}

/** Production adapter. Request values remain individual argv entries; no shell is involved. */
class ProcessSandboxRunner(
    val runnerBinary: Path = resolveDefaultRunnerBinary(),
    private val processStarter: (List<String>) -> Process = { ProcessBuilder(it).start() }
) : RunnerClient {

    private val gson = Gson()

    internal fun buildCommand(
        jobId: String,
        binaryPath: String,
        args: List<String>,
        limits: ResourceLimits,
        workspace: WorkspaceHandoff?
    ): List<String> {
        val wallMs = if (limits.wallMs > 0) limits.wallMs else 2000L
        val cpuMs = if (limits.cpuMs > 0) limits.cpuMs else 1000L
        val memoryMb = if (limits.memoryMb > 0) limits.memoryMb else 128L
        val maxOutputKb = if (limits.maxOutputKb > 0) limits.maxOutputKb else 64L
        val cpuSoftSec = (cpuMs / 1000L).coerceAtLeast(1L)
        val cpuHardSec = (cpuSoftSec + 1L).coerceAtLeast(2L)

        return buildList {
            add(runnerBinary.toAbsolutePath().normalize().toString())
            addAll(listOf("--job-id", jobId, "--timeout-ms", wallMs.toString()))
            addAll(listOf("--cpu-soft", cpuSoftSec.toString(), "--cpu-hard", cpuHardSec.toString()))
            addAll(listOf("--as-mb", memoryMb.toString(), "--max-output-kb", maxOutputKb.toString()))
            if (workspace != null) {
                addAll(listOf("--workspace", workspace.directory.toAbsolutePath().normalize().toString()))
                addAll(listOf("--source-file", workspace.sourceName))
                workspace.artifactName?.let { addAll(listOf("--artifact-out", it)) }
            }
            add("--")
            add(binaryPath)
            addAll(args)
        }
    }

    override suspend fun run(
        jobId: String,
        binaryPath: String,
        args: List<String>,
        limits: ResourceLimits,
        stdin: String,
        workspace: WorkspaceHandoff?
    ): RunnerResult = withContext(Dispatchers.IO) {
        if (!Files.isExecutable(runnerBinary)) {
            return@withContext RunnerResult.sandboxError(
                jobId,
                "sandbox-runner is not executable at $runnerBinary"
            )
        }
        val stdinBytes = stdin.toByteArray(Charsets.UTF_8)
        if (stdinBytes.size > MAX_STDIN_BYTES) {
            return@withContext RunnerResult.sandboxError(jobId, "stdin exceeds maximum of $MAX_STDIN_BYTES bytes")
        }

        val command = buildCommand(jobId, binaryPath, args, limits, workspace)
        val process = try {
            processStarter(command)
        } catch (e: Exception) {
            return@withContext RunnerResult.sandboxError(jobId, "failed to spawn sandbox-runner: ${e.message}")
        }

        val wallMs = if (limits.wallMs > 0) limits.wallMs else 2000L
        val processTimeoutMs = wallMs + RUNNER_SETUP_GRACE_MS
        val deadlineNanos = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(processTimeoutMs)

        // Start all drains before feeding stdin, so a runner that reports an early error cannot fill a pipe.
        val stdoutFuture = process.inputStream.readAllBytesAsync()
        val stderrFuture = process.errorStream.readAllBytesAsync()
        val stdinFuture = CompletableFuture.runAsync {
            process.outputStream.use { output ->
                output.write(stdinBytes)
                output.flush()
            }
        }

        val finished = try {
            process.waitFor(processTimeoutMs, TimeUnit.MILLISECONDS)
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
            process.destroyForcibly()
            closeProcessPipes(process)
            stdinFuture.cancel(true)
            stdoutFuture.cancel(true)
            stderrFuture.cancel(true)
            return@withContext RunnerResult.sandboxError(jobId, "interrupted while waiting for sandbox-runner")
        }
        if (!finished) {
            process.destroyForcibly()
            try { process.waitFor(1, TimeUnit.SECONDS) } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
            }
            closeProcessPipes(process)
            stdinFuture.cancel(true)
            stdoutFuture.cancel(true)
            stderrFuture.cancel(true)
            return@withContext RunnerResult(
                jobId = jobId,
                status = ExecutionStatus.TIMEOUT,
                exitCode = 127,
                signal = 9,
                wallMs = processTimeoutMs,
                cpuMs = 0L,
                peakMemoryKb = 0L,
                stdout = "",
                stderr = "sandbox-runner process timed out after ${processTimeoutMs}ms"
            )
        }

        try {
            fun remainingMs(): Long = TimeUnit.NANOSECONDS.toMillis(
                (deadlineNanos - System.nanoTime()).coerceAtLeast(0L)
            ).coerceAtLeast(1L)
            stdinFuture.get(remainingMs(), TimeUnit.MILLISECONDS)
            val stdoutBytes = stdoutFuture.get(remainingMs(), TimeUnit.MILLISECONDS)
            val stderrBytes = stderrFuture.get(remainingMs(), TimeUnit.MILLISECONDS)
            val stdoutText = String(stdoutBytes, Charsets.UTF_8).trim()
            val stderrText = String(stderrBytes, Charsets.UTF_8).trim()

            if (stdoutText.isNotEmpty()) {
                try {
                    val rawJson = gson.fromJson(stdoutText, RawRunnerJson::class.java)
                    if (rawJson != null && rawJson.status != null) {
                        return@withContext RunnerResult.fromRaw(rawJson, jobId)
                    }
                } catch (_: JsonSyntaxException) {
                    // An invalid runner response is a setup failure below.
                }
            }

            val combinedError = buildString {
                if (stderrText.isNotEmpty()) append(stderrText)
                if (stdoutText.isNotEmpty()) {
                    if (isNotEmpty()) append('\n')
                    append("runner output: ").append(stdoutText)
                }
                if (isEmpty()) append("sandbox-runner exited with code ${process.exitValue()} without JSON result")
            }
            RunnerResult.sandboxError(jobId, combinedError)
        } catch (e: Exception) {
            RunnerResult.sandboxError(jobId, "failed to collect sandbox-runner result: ${e.message}")
        }
    }

    private fun InputStream.readAllBytesAsync(): CompletableFuture<ByteArray> = CompletableFuture.supplyAsync {
        use { it.readBytes() }
    }

    private fun closeProcessPipes(process: Process) {
        try { process.inputStream.close() } catch (_: Exception) { }
        try { process.errorStream.close() } catch (_: Exception) { }
        try { process.outputStream.close() } catch (_: Exception) { }
    }

    companion object {
        const val MAX_STDIN_BYTES = 64 * 1024
        private const val RUNNER_SETUP_GRACE_MS = 5000L

        fun resolveDefaultRunnerBinary(): Path {
            val envPath = System.getenv("PES_RUNNER_BIN")
            if (!envPath.isNullOrBlank()) return Path.of(envPath)
            val candidates = listOf(
                Path.of("sandbox-daemon/sandbox-runner"),
                Path.of("../sandbox-daemon/sandbox-runner"),
                Path.of("../../sandbox-daemon/sandbox-runner")
            )
            return candidates.firstOrNull { Files.isExecutable(it) }?.toAbsolutePath()?.normalize()
                ?: Path.of("sandbox-daemon/sandbox-runner").toAbsolutePath().normalize()
        }
    }
}

/** Deterministic protocol fake. It writes only a canned artifact and never starts a process. */
open class FakeRunnerClient(
    private val cannedResults: List<RunnerResult> = emptyList()
) : RunnerClient {
    data class Call(
        val jobId: String,
        val binaryPath: String,
        val args: List<String>,
        val stdin: String,
        val workspace: WorkspaceHandoff?
    )

    val calls = mutableListOf<Call>()
    private var nextResult = 0

    override suspend fun run(
        jobId: String,
        binaryPath: String,
        args: List<String>,
        limits: ResourceLimits,
        stdin: String,
        workspace: WorkspaceHandoff?
    ): RunnerResult {
        calls.add(Call(jobId, binaryPath, args.toList(), stdin, workspace))
        val result = cannedResults.getOrNull(nextResult++) ?: if (binaryPath == "/usr/bin/gcc") {
            RunnerResult(
                jobId = jobId,
                status = ExecutionStatus.OK,
                exitCode = 0,
                signal = 0,
                wallMs = 0,
                cpuMs = 0,
                peakMemoryKb = 0,
                stdout = "",
                stderr = "FAKE_RUNNER: compilation was not performed"
            )
        } else {
            RunnerResult(
                jobId = jobId,
                status = ExecutionStatus.OK,
                exitCode = 0,
                signal = 0,
                wallMs = 0,
                cpuMs = 0,
                peakMemoryKb = 0,
                stdout = "FAKE_RUNNER: protocol response only; submitted source was not run\n",
                stderr = ""
            )
        }
        if (result.status == ExecutionStatus.OK && workspace?.artifactName != null) {
            val artifact = workspace.directory.resolve(workspace.artifactName)
            Files.writeString(artifact, "FAKE_RUNNER_ARTIFACT", Charsets.UTF_8)
            Files.setPosixFilePermissions(
                artifact,
                PosixFilePermissions.fromString("rw-------")
            )
        }
        return result
    }
}
