package pes.worker.runner

import com.google.gson.Gson
import com.google.gson.JsonSyntaxException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.ResourceLimits
import pes.worker.model.RawRunnerJson
import pes.worker.model.RunnerResult
import java.io.File
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.TimeUnit

interface RunnerClient {
    suspend fun run(
        jobId: String,
        binaryPath: String,
        args: List<String> = emptyList(),
        limits: ResourceLimits,
        stdin: String = ""
    ): RunnerResult
}

/**
 * Production runner adapter: invokes sandbox-daemon/sandbox-runner via ProcessBuilder
 * using typed argument vectors (never shell strings).
 */
class ProcessSandboxRunner(
    val runnerBinary: Path = resolveDefaultRunnerBinary()
) : RunnerClient {

    private val gson = Gson()

    override suspend fun run(
        jobId: String,
        binaryPath: String,
        args: List<String>,
        limits: ResourceLimits,
        stdin: String
    ): RunnerResult = withContext(Dispatchers.IO) {
        if (!Files.exists(runnerBinary)) {
            return@withContext RunnerResult.sandboxError(
                jobId,
                "sandbox-runner binary not found at $runnerBinary"
            )
        }

        val wallMs = if (limits.wallMs > 0) limits.wallMs else 2000L
        val cpuMs = if (limits.cpuMs > 0) limits.cpuMs else 1000L
        val memoryMb = if (limits.memoryMb > 0) limits.memoryMb else 128L
        val maxOutputKb = if (limits.maxOutputKb > 0) limits.maxOutputKb else 64L

        val cpuSoftSec = (cpuMs / 1000L).coerceAtLeast(1L)
        val cpuHardSec = (cpuSoftSec + 1L).coerceAtLeast(2L)

        val cmd = mutableListOf<String>()
        cmd.add(runnerBinary.toAbsolutePath().normalize().toString())
        cmd.add("--job-id")
        cmd.add(jobId)
        cmd.add("--timeout-ms")
        cmd.add(wallMs.toString())
        cmd.add("--cpu-soft")
        cmd.add(cpuSoftSec.toString())
        cmd.add("--cpu-hard")
        cmd.add(cpuHardSec.toString())
        cmd.add("--as-mb")
        cmd.add(memoryMb.toString())
        cmd.add("--max-output-kb")
        cmd.add(maxOutputKb.toString())
        cmd.add("--")
        cmd.add(binaryPath)
        cmd.addAll(args)

        val processBuilder = ProcessBuilder(cmd)
        val process: Process
        try {
            process = processBuilder.start()
        } catch (e: Exception) {
            return@withContext RunnerResult.sandboxError(
                jobId,
                "failed to spawn sandbox-runner: ${e.message}"
            )
        }

        try {
            if (stdin.isNotEmpty()) {
                process.outputStream.bufferedWriter().use { writer ->
                    writer.write(stdin)
                    writer.flush()
                }
            } else {
                process.outputStream.close()
            }

            val stdoutFuture = process.inputStream.readAllBytesAsync()
            val stderrFuture = process.errorStream.readAllBytesAsync()

            val processTimeoutMs = wallMs + 5000L
            val finished = process.waitFor(processTimeoutMs, TimeUnit.MILLISECONDS)
            if (!finished) {
                process.destroyForcibly()
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

            val stdoutBytes = stdoutFuture.join()
            val stderrBytes = stderrFuture.join()
            val stdoutText = String(stdoutBytes, Charsets.UTF_8).trim()
            val stderrText = String(stderrBytes, Charsets.UTF_8).trim()

            if (stdoutText.isNotEmpty()) {
                try {
                    val rawJson = gson.fromJson(stdoutText, RawRunnerJson::class.java)
                    if (rawJson != null && rawJson.status != null) {
                        return@withContext RunnerResult.fromRaw(rawJson, jobId)
                    }
                } catch (e: JsonSyntaxException) {
                    // stdout was not valid runner JSON
                }
            }

            // Fallback when JSON stdout is missing or invalid
            val combinedError = buildString {
                if (stderrText.isNotEmpty()) append(stderrText)
                if (stdoutText.isNotEmpty()) {
                    if (isNotEmpty()) append("\n")
                    append("runner output: ").append(stdoutText)
                }
                if (isEmpty()) append("sandbox-runner exited with code ${process.exitValue()} without JSON result")
            }

            RunnerResult.sandboxError(jobId, combinedError)
        } catch (e: Exception) {
            RunnerResult.sandboxError(jobId, "exception during sandbox execution: ${e.message}")
        }
    }

    private fun InputStream.readAllBytesAsync(): java.util.concurrent.CompletableFuture<ByteArray> {
        return java.util.concurrent.CompletableFuture.supplyAsync {
            this.use { it.readBytes() }
        }
    }

    companion object {
        fun resolveDefaultRunnerBinary(): Path {
            val envPath = System.getenv("PES_RUNNER_BIN")
            if (!envPath.isNullOrBlank()) {
                return Path.of(envPath)
            }
            val candidates = listOf(
                Path.of("sandbox-daemon/sandbox-runner"),
                Path.of("../sandbox-daemon/sandbox-runner"),
                Path.of("../../sandbox-daemon/sandbox-runner")
            )
            for (candidate in candidates) {
                if (Files.isExecutable(candidate)) {
                    return candidate.toAbsolutePath().normalize()
                }
            }
            return Path.of("sandbox-daemon/sandbox-runner").toAbsolutePath().normalize()
        }
    }
}

/**
 * Simulation runner: executes host compilers and runtimes directly for test environments
 * where host kernel isolation features (cgroups, namespaces) are not available.
 */
class SimulationSandboxRunner : RunnerClient {
    override suspend fun run(
        jobId: String,
        binaryPath: String,
        args: List<String>,
        limits: ResourceLimits,
        stdin: String
    ): RunnerResult = withContext(Dispatchers.IO) {
        val wallTimeoutMs = if (limits.wallMs > 0) limits.wallMs else 2000L
        val startTime = System.currentTimeMillis()

        val cmd = mutableListOf<String>()
        cmd.add(binaryPath)
        cmd.addAll(args)

        val processBuilder = ProcessBuilder(cmd)
        val process: Process
        try {
            process = processBuilder.start()
        } catch (e: Exception) {
            return@withContext RunnerResult.sandboxError(
                jobId,
                "simulation failed to start process '$binaryPath': ${e.message}"
            )
        }

        try {
            if (stdin.isNotEmpty()) {
                process.outputStream.bufferedWriter().use { writer ->
                    writer.write(stdin)
                    writer.flush()
                }
            } else {
                process.outputStream.close()
            }

            val stdoutFuture = java.util.concurrent.CompletableFuture.supplyAsync {
                process.inputStream.use { it.readBytes() }
            }
            val stderrFuture = java.util.concurrent.CompletableFuture.supplyAsync {
                process.errorStream.use { it.readBytes() }
            }

            val finished = process.waitFor(wallTimeoutMs, TimeUnit.MILLISECONDS)
            val elapsedMs = System.currentTimeMillis() - startTime

            if (!finished) {
                process.destroyForcibly()
                return@withContext RunnerResult(
                    jobId = jobId,
                    status = ExecutionStatus.TIMEOUT,
                    exitCode = 127,
                    signal = 9,
                    wallMs = elapsedMs,
                    cpuMs = elapsedMs,
                    peakMemoryKb = 1024L,
                    stdout = "",
                    stderr = "simulation execution timed out after ${wallTimeoutMs}ms"
                )
            }

            val stdoutBytes = stdoutFuture.join()
            val stderrBytes = stderrFuture.join()
            val exitCode = process.exitValue()
            val stdout = String(stdoutBytes, Charsets.UTF_8)
            val stderr = String(stderrBytes, Charsets.UTF_8)

            val status = if (exitCode == 0) {
                ExecutionStatus.OK
            } else {
                ExecutionStatus.RUNTIME_ERROR
            }

            RunnerResult(
                jobId = jobId,
                status = status,
                exitCode = exitCode,
                signal = 0,
                wallMs = elapsedMs,
                cpuMs = elapsedMs,
                peakMemoryKb = 4096L,
                stdout = stdout,
                stderr = stderr
            )
        } catch (e: Exception) {
            RunnerResult.sandboxError(jobId, "simulation error: ${e.message}")
        }
    }
}
