package pes.worker.pipeline

import pes.execution.v1.ExecuteRequest
import pes.execution.v1.ExecuteResponse
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.Language
import pes.execution.v1.ResourceLimits
import pes.worker.model.RunnerResult
import pes.worker.runner.RunnerClient
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes

sealed class ValidationResult {
    data class Valid(val normalizedLimits: ResourceLimits) : ValidationResult()
    data class Invalid(val reason: String) : ValidationResult()
}

class ExecutionPipeline(
    val runnerClient: RunnerClient
) {
    companion object {
        private val JOB_ID_REGEX = Regex("^[a-zA-Z0-9_-]{1,64}$")
        const val MAX_SOURCE_CODE_BYTES = 262144 // 256 KiB
        const val DEFAULT_CPU_MS = 1000L
        const val MAX_CPU_MS = 10000L
        const val DEFAULT_WALL_MS = 2000L
        const val MAX_WALL_MS = 15000L
        const val DEFAULT_MEMORY_MB = 128L
        const val MAX_MEMORY_MB = 512L
        const val DEFAULT_MAX_OUTPUT_KB = 64L
        const val MAX_OUTPUT_KB = 1024L
    }

    fun validateRequest(request: ExecuteRequest): ValidationResult {
        if (request.jobId.isNullOrBlank()) {
            return ValidationResult.Invalid("job_id must not be empty")
        }
        if (!JOB_ID_REGEX.matches(request.jobId)) {
            return ValidationResult.Invalid("job_id '${request.jobId}' is invalid: must match ^[a-zA-Z0-9_-]{1,64}$")
        }
        if (request.language == Language.LANGUAGE_UNSPECIFIED || request.language == Language.UNRECOGNIZED) {
            return ValidationResult.Invalid("language must be specified (C or PYTHON)")
        }
        if (request.sourceCode.isNullOrBlank()) {
            return ValidationResult.Invalid("source_code must not be empty")
        }
        if (request.sourceCode.toByteArray(Charsets.UTF_8).size > MAX_SOURCE_CODE_BYTES) {
            return ValidationResult.Invalid("source_code exceeds maximum allowed size of 256 KiB")
        }

        val limits = request.limits
        val cpuMs = if (limits.cpuMs <= 0) DEFAULT_CPU_MS else limits.cpuMs.coerceAtMost(MAX_CPU_MS)
        val wallMs = if (limits.wallMs <= 0) DEFAULT_WALL_MS else limits.wallMs.coerceAtMost(MAX_WALL_MS)
        val memoryMb = if (limits.memoryMb <= 0) DEFAULT_MEMORY_MB else limits.memoryMb.coerceAtMost(MAX_MEMORY_MB)
        val maxOutputKb = if (limits.maxOutputKb <= 0) DEFAULT_MAX_OUTPUT_KB else limits.maxOutputKb.coerceAtMost(MAX_OUTPUT_KB)

        val normalized = ResourceLimits.newBuilder()
            .setCpuMs(cpuMs)
            .setWallMs(wallMs)
            .setMemoryMb(memoryMb)
            .setMaxOutputKb(maxOutputKb)
            .build()

        return ValidationResult.Valid(normalized)
    }

    suspend fun execute(request: ExecuteRequest): ExecuteResponse {
        val validation = validateRequest(request)
        if (validation is ValidationResult.Invalid) {
            return ExecuteResponse.newBuilder()
                .setJobId(if (request.jobId.isNullOrBlank()) "unknown" else request.jobId)
                .setStatus(ExecutionStatus.SANDBOX_ERROR)
                .setStderr("validation error: ${validation.reason}")
                .setExitCode(127)
                .build()
        }

        val normalizedLimits = (validation as ValidationResult.Valid).normalizedLimits
        var workspace: Path? = null

        try {
            workspace = Files.createTempDirectory("pes-job-${request.jobId}-")

            return when (request.language) {
                Language.LANGUAGE_C -> executeC(request, normalizedLimits, workspace)
                Language.LANGUAGE_PYTHON -> executePython(request, normalizedLimits, workspace)
                else -> ExecuteResponse.newBuilder()
                    .setJobId(request.jobId)
                    .setStatus(ExecutionStatus.SANDBOX_ERROR)
                    .setStderr("unsupported language: ${request.language}")
                    .setExitCode(127)
                    .build()
            }
        } catch (e: Exception) {
            return ExecuteResponse.newBuilder()
                .setJobId(request.jobId)
                .setStatus(ExecutionStatus.SANDBOX_ERROR)
                .setStderr("pipeline error: ${e.message}")
                .setExitCode(127)
                .build()
        } finally {
            if (workspace != null) {
                deleteWorkspaceQuietly(workspace)
            }
        }
    }

    private suspend fun executeC(
        request: ExecuteRequest,
        limits: ResourceLimits,
        workspace: Path
    ): ExecuteResponse {
        val sourceFile = workspace.resolve("source.c")
        val targetBinary = workspace.resolve("target")
        Files.writeString(sourceFile, request.sourceCode, Charsets.UTF_8)

        // 1. Sandboxed Compilation Step
        val compileLimits = ResourceLimits.newBuilder()
            .setCpuMs(5000L)
            .setWallMs(8000L)
            .setMemoryMb(512L)
            .setMaxOutputKb(64L)
            .build()

        val compileArgs = listOf(
            "-O2",
            "-o", targetBinary.toAbsolutePath().toString(),
            sourceFile.toAbsolutePath().toString()
        )

        val compileResult = runnerClient.run(
            jobId = "${request.jobId}-compile",
            binaryPath = "/usr/bin/gcc",
            args = compileArgs,
            limits = compileLimits,
            stdin = ""
        )

        if (compileResult.status == ExecutionStatus.SANDBOX_ERROR) {
            return buildResponse(request.jobId, compileResult)
        }

        if (compileResult.status != ExecutionStatus.OK || compileResult.exitCode != 0 || !Files.exists(targetBinary)) {
            val errOutput = if (compileResult.stderr.isNotEmpty()) {
                compileResult.stderr
            } else if (compileResult.stdout.isNotEmpty()) {
                compileResult.stdout
            } else {
                "compilation failed with exit code ${compileResult.exitCode}"
            }

            return ExecuteResponse.newBuilder()
                .setJobId(request.jobId)
                .setStatus(ExecutionStatus.COMPILE_ERROR)
                .setStderr(errOutput)
                .setExitCode(if (compileResult.exitCode != 0) compileResult.exitCode else 1)
                .setSignal(compileResult.signal)
                .setWallMs(compileResult.wallMs)
                .setCpuMs(compileResult.cpuMs)
                .setPeakMemoryKb(compileResult.peakMemoryKb)
                .build()
        }

        // 2. Sandboxed Execution Step
        val execResult = runnerClient.run(
            jobId = request.jobId,
            binaryPath = targetBinary.toAbsolutePath().toString(),
            args = emptyList(),
            limits = limits,
            stdin = request.stdin
        )

        return buildResponse(request.jobId, execResult)
    }

    private suspend fun executePython(
        request: ExecuteRequest,
        limits: ResourceLimits,
        workspace: Path
    ): ExecuteResponse {
        val sourceFile = workspace.resolve("source.py")
        Files.writeString(sourceFile, request.sourceCode, Charsets.UTF_8)

        val execResult = runnerClient.run(
            jobId = request.jobId,
            binaryPath = "/usr/bin/python3",
            args = listOf(sourceFile.toAbsolutePath().toString()),
            limits = limits,
            stdin = request.stdin
        )

        return buildResponse(request.jobId, execResult)
    }

    private fun buildResponse(jobId: String, result: RunnerResult): ExecuteResponse {
        return ExecuteResponse.newBuilder()
            .setJobId(jobId)
            .setStatus(result.status)
            .setStdout(result.stdout)
            .setStderr(result.stderr)
            .setExitCode(result.exitCode)
            .setSignal(result.signal)
            .setWallMs(result.wallMs)
            .setCpuMs(result.cpuMs)
            .setPeakMemoryKb(result.peakMemoryKb)
            .build()
    }

    private fun deleteWorkspaceQuietly(path: Path) {
        try {
            if (!Files.exists(path)) return
            Files.walkFileTree(path, object : SimpleFileVisitor<Path>() {
                override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                    Files.deleteIfExists(file)
                    return FileVisitResult.CONTINUE
                }

                override fun postVisitDirectory(dir: Path, exc: IOException?): FileVisitResult {
                    Files.deleteIfExists(dir)
                    return FileVisitResult.CONTINUE
                }
            })
        } catch (_: Exception) {
            // best-effort cleanup
        }
    }
}
