package pes.worker.pipeline

import pes.execution.v1.ExecuteRequest
import pes.execution.v1.ExecuteResponse
import pes.execution.v1.ExecutionStage
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.Language
import pes.execution.v1.ResourceLimits
import pes.worker.model.RunnerResult
import pes.worker.runner.ProcessSandboxRunner
import pes.worker.runner.RunnerClient
import pes.worker.runner.WorkspaceHandoff
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.OpenOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.StandardOpenOption
import java.nio.file.attribute.BasicFileAttributes
import java.nio.file.attribute.PosixFilePermission
import java.nio.file.attribute.PosixFilePermissions

sealed class ValidationResult {
    data class Valid(val normalizedLimits: ResourceLimits) : ValidationResult()
    data class Invalid(val reason: String) : ValidationResult()
}

class ExecutionPipeline(val runnerClient: RunnerClient) {
    companion object {
        private val JOB_ID_REGEX = Regex("^[a-zA-Z0-9_-]{1,64}$")
        private val PRIVATE_DIR_PERMISSIONS = PosixFilePermissions.asFileAttribute(
            setOf(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE, PosixFilePermission.OWNER_EXECUTE)
        )
        private val PRIVATE_FILE_PERMISSIONS = PosixFilePermissions.asFileAttribute(
            setOf(PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE)
        )
        const val MAX_SOURCE_CODE_BYTES = 262144
        const val MAX_STDIN_BYTES = ProcessSandboxRunner.MAX_STDIN_BYTES
        const val MAX_ARTIFACT_BYTES = 1024 * 1024
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
        if (request.jobId.isBlank()) return ValidationResult.Invalid("job_id must not be empty")
        if (!JOB_ID_REGEX.matches(request.jobId)) {
            return ValidationResult.Invalid("job_id '${request.jobId}' is invalid: must match ^[a-zA-Z0-9_-]{1,64}$")
        }
        if (request.language == Language.LANGUAGE_UNSPECIFIED || request.language == Language.UNRECOGNIZED) {
            return ValidationResult.Invalid("language must be specified (C or PYTHON)")
        }
        if (request.sourceCode.isBlank()) return ValidationResult.Invalid("source_code must not be empty")
        if (request.sourceCode.toByteArray(Charsets.UTF_8).size > MAX_SOURCE_CODE_BYTES) {
            return ValidationResult.Invalid("source_code exceeds maximum allowed size of 256 KiB")
        }
        if (request.stdin.toByteArray(Charsets.UTF_8).size > MAX_STDIN_BYTES) {
            return ValidationResult.Invalid("stdin exceeds maximum allowed size of $MAX_STDIN_BYTES bytes")
        }

        val limits = request.limits
        val normalized = ResourceLimits.newBuilder()
            .setCpuMs(if (limits.cpuMs <= 0) DEFAULT_CPU_MS else limits.cpuMs.coerceAtMost(MAX_CPU_MS))
            .setWallMs(if (limits.wallMs <= 0) DEFAULT_WALL_MS else limits.wallMs.coerceAtMost(MAX_WALL_MS))
            .setMemoryMb(if (limits.memoryMb <= 0) DEFAULT_MEMORY_MB else limits.memoryMb.coerceAtMost(MAX_MEMORY_MB))
            .setMaxOutputKb(if (limits.maxOutputKb <= 0) DEFAULT_MAX_OUTPUT_KB else limits.maxOutputKb.coerceAtMost(MAX_OUTPUT_KB))
            .build()
        return ValidationResult.Valid(normalized)
    }

    suspend fun execute(
        request: ExecuteRequest,
        onProgress: (suspend (ExecutionStage) -> Unit)? = null
    ): ExecuteResponse {
        val validation = validateRequest(request)
        if (validation is ValidationResult.Invalid) return validationFailure(request, validation.reason)
        val normalizedLimits = (validation as ValidationResult.Valid).normalizedLimits

        var workspace: Path? = null
        var response: ExecuteResponse? = null
        var cleanupFailure: Exception? = null
        try {
            workspace = Files.createTempDirectory(Path.of("/tmp"), "pes-job-${request.jobId}-", PRIVATE_DIR_PERMISSIONS)
            response = when (request.language) {
                Language.LANGUAGE_C -> executeC(request, normalizedLimits, workspace, onProgress)
                Language.LANGUAGE_PYTHON -> executePython(request, normalizedLimits, workspace, onProgress)
                else -> validationFailure(request, "unsupported language: ${request.language}")
            }
        } catch (e: Exception) {
            response = validationFailure(request, "pipeline setup failed: ${e.message}")
        } finally {
            if (workspace != null) {
                try {
                    deleteWorkspace(workspace)
                } catch (e: Exception) {
                    cleanupFailure = e
                }
            }
        }
        if (cleanupFailure != null) {
            return validationFailure(request, "workspace cleanup failed: ${cleanupFailure?.message}")
        }
        return response ?: validationFailure(request, "pipeline produced no response")
    }

    private suspend fun executeC(
        request: ExecuteRequest,
        limits: ResourceLimits,
        workspace: Path,
        onProgress: (suspend (ExecutionStage) -> Unit)? = null
    ): ExecuteResponse {
        val sourceFile = workspace.resolve("source.c")
        val targetBinary = workspace.resolve("target")
        writePrivateSource(sourceFile, request.sourceCode)

        val compileLimits = ResourceLimits.newBuilder()
            .setCpuMs(5000L)
            .setWallMs(8000L)
            .setMemoryMb(512L)
            .setMaxOutputKb(64L)
            .build()
        onProgress?.invoke(ExecutionStage.STAGE_COMPILING)
        val compileResult = runnerClient.run(
            jobId = "${request.jobId}-compile",
            binaryPath = "/usr/bin/gcc",
            args = listOf("-O2", "-o", "/workspace/target", "/workspace/source.c"),
            limits = compileLimits,
            workspace = WorkspaceHandoff(workspace, "source.c", "target")
        )

        if (compileResult.status == ExecutionStatus.SANDBOX_ERROR) return buildResponse(request.jobId, compileResult)
        if (compileResult.status == ExecutionStatus.TIMEOUT ||
            compileResult.status == ExecutionStatus.MEMORY_LIMIT ||
            compileResult.status == ExecutionStatus.OUTPUT_LIMIT
        ) return buildResponse(request.jobId, compileResult)
        if (compileResult.status != ExecutionStatus.OK || compileResult.exitCode != 0) {
            val compilerOutput = compileResult.stderr.ifEmpty { compileResult.stdout }
                .ifEmpty { "compilation failed with exit code ${compileResult.exitCode}" }
            return ExecuteResponse.newBuilder()
                .setJobId(request.jobId)
                .setStatus(ExecutionStatus.COMPILE_ERROR)
                .setStderr(compilerOutput)
                .setExitCode(if (compileResult.exitCode != 0) compileResult.exitCode else 1)
                .setSignal(compileResult.signal)
                .setWallMs(compileResult.wallMs)
                .setCpuMs(compileResult.cpuMs)
                .setPeakMemoryKb(compileResult.peakMemoryKb)
                .build()
        }
        if (!isBoundedRegularArtifact(targetBinary)) {
            return validationFailure(request, "sandbox reported compile success but artifact handoff was missing or unsafe")
        }

        onProgress?.invoke(ExecutionStage.STAGE_RUNNING)
        val runResult = runnerClient.run(
            jobId = request.jobId,
            binaryPath = targetBinary.toAbsolutePath().normalize().toString(),
            args = emptyList(),
            limits = limits,
            stdin = request.stdin
        )
        return buildResponse(request.jobId, runResult)
    }

    private suspend fun executePython(
        request: ExecuteRequest,
        limits: ResourceLimits,
        workspace: Path,
        onProgress: (suspend (ExecutionStage) -> Unit)? = null
    ): ExecuteResponse {
        val sourceFile = workspace.resolve("source.py")
        writePrivateSource(sourceFile, request.sourceCode)
        onProgress?.invoke(ExecutionStage.STAGE_RUNNING)
        val runResult = runnerClient.run(
            jobId = request.jobId,
            binaryPath = "/usr/bin/python3",
            args = listOf("/workspace/source.py"),
            limits = limits,
            stdin = request.stdin,
            workspace = WorkspaceHandoff(workspace, "source.py")
        )
        return buildResponse(request.jobId, runResult)
    }

    private fun writePrivateSource(path: Path, source: String) {
        val bytes = source.toByteArray(Charsets.UTF_8)
        if (bytes.isEmpty() || bytes.size > MAX_SOURCE_CODE_BYTES) throw IOException("source size is outside the accepted range")
        val options: Set<OpenOption> = setOf(StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)
        FileChannel.open(path, options, PRIVATE_FILE_PERMISSIONS).use { channel ->
            val buffer = ByteBuffer.wrap(bytes)
            while (buffer.hasRemaining()) channel.write(buffer)
            channel.force(true)
        }
        val attrs = Files.readAttributes(path, BasicFileAttributes::class.java, LinkOption.NOFOLLOW_LINKS)
        if (!attrs.isRegularFile || attrs.isSymbolicLink || Files.size(path) != bytes.size.toLong()) {
            throw IOException("source file failed post-write validation")
        }
    }

    private fun isBoundedRegularArtifact(path: Path): Boolean {
        if (!Files.exists(path, LinkOption.NOFOLLOW_LINKS) || Files.isSymbolicLink(path)) return false
        val attrs = Files.readAttributes(path, BasicFileAttributes::class.java, LinkOption.NOFOLLOW_LINKS)
        if (!attrs.isRegularFile || attrs.size() !in 1..MAX_ARTIFACT_BYTES.toLong()) return false
        val mode = Files.getAttribute(path, "unix:mode", LinkOption.NOFOLLOW_LINKS) as Int
        val permissions = Files.getAttribute(path, "posix:permissions", LinkOption.NOFOLLOW_LINKS)
        return (mode and 0x1ff) == 0x180 && permissions == setOf(
            PosixFilePermission.OWNER_READ,
            PosixFilePermission.OWNER_WRITE
        )
    }

    private fun validationFailure(request: ExecuteRequest, message: String): ExecuteResponse =
        ExecuteResponse.newBuilder()
            .setJobId(request.jobId.ifBlank { "unknown" })
            .setStatus(ExecutionStatus.SANDBOX_ERROR)
            .setStderr(message)
            .setExitCode(127)
            .build()

    private fun buildResponse(jobId: String, result: RunnerResult): ExecuteResponse =
        ExecuteResponse.newBuilder()
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

    private fun deleteWorkspace(path: Path) {
        val normalized = path.toAbsolutePath().normalize()
        if (normalized.parent != Path.of("/tmp") || !normalized.fileName.toString().startsWith("pes-job-")) {
            throw IOException("refusing to clean a workspace outside /tmp/pes-job-*")
        }
        if (!Files.exists(normalized, LinkOption.NOFOLLOW_LINKS)) return
        val attrs = Files.readAttributes(normalized, BasicFileAttributes::class.java, LinkOption.NOFOLLOW_LINKS)
        if (!attrs.isDirectory || attrs.isSymbolicLink) throw IOException("workspace root is not a real directory")
        Files.walkFileTree(normalized, object : SimpleFileVisitor<Path>() {
            override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                Files.delete(file)
                return FileVisitResult.CONTINUE
            }

            override fun postVisitDirectory(dir: Path, exc: IOException?): FileVisitResult {
                if (exc != null) throw exc
                Files.delete(dir)
                return FileVisitResult.CONTINUE
            }
        })
    }
}
