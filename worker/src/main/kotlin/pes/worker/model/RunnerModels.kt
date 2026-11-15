package pes.worker.model

import com.google.gson.annotations.SerializedName
import pes.execution.v1.ExecutionStatus

/**
 * Raw JSON output produced by sandbox-runner on stdout.
 * Conforms to the JSON result schema defined in docs/02_ARCHITECTURE.md.
 */
data class RawRunnerJson(
    @SerializedName("job_id")
    val jobId: String? = null,
    @SerializedName("status")
    val status: String? = null,
    @SerializedName("exit_code")
    val exitCode: Int? = null,
    @SerializedName("signal")
    val signal: Int? = null,
    @SerializedName("wall_ms")
    val wallMs: Long? = null,
    @SerializedName("cpu_ms")
    val cpuMs: Long? = null,
    @SerializedName("peak_memory_kb")
    val peakMemoryKb: Long? = null,
    @SerializedName("stdout")
    val stdout: String? = null,
    @SerializedName("stderr")
    val stderr: String? = null
)

/**
 * Strongly-typed internal result representation.
 */
data class RunnerResult(
    val jobId: String,
    val status: ExecutionStatus,
    val exitCode: Int,
    val signal: Int,
    val wallMs: Long,
    val cpuMs: Long,
    val peakMemoryKb: Long,
    val stdout: String,
    val stderr: String
) {
    companion object {
        fun fromRaw(raw: RawRunnerJson, fallbackJobId: String): RunnerResult {
            val status = parseStatus(raw.status)
            return RunnerResult(
                jobId = raw.jobId ?: fallbackJobId,
                status = status,
                exitCode = raw.exitCode ?: 0,
                signal = raw.signal ?: 0,
                wallMs = raw.wallMs ?: 0L,
                cpuMs = raw.cpuMs ?: 0L,
                peakMemoryKb = raw.peakMemoryKb ?: 0L,
                stdout = raw.stdout ?: "",
                stderr = raw.stderr ?: ""
            )
        }

        fun parseStatus(statusStr: String?): ExecutionStatus {
            return when (statusStr?.trim()?.uppercase()) {
                "OK" -> ExecutionStatus.OK
                "COMPILE_ERROR" -> ExecutionStatus.COMPILE_ERROR
                "RUNTIME_ERROR" -> ExecutionStatus.RUNTIME_ERROR
                "TIMEOUT" -> ExecutionStatus.TIMEOUT
                "MEMORY_LIMIT" -> ExecutionStatus.MEMORY_LIMIT
                "OUTPUT_LIMIT" -> ExecutionStatus.OUTPUT_LIMIT
                "SANDBOX_ERROR" -> ExecutionStatus.SANDBOX_ERROR
                else -> ExecutionStatus.SANDBOX_ERROR
            }
        }

        fun sandboxError(jobId: String, message: String): RunnerResult {
            return RunnerResult(
                jobId = jobId,
                status = ExecutionStatus.SANDBOX_ERROR,
                exitCode = 127,
                signal = 0,
                wallMs = 0L,
                cpuMs = 0L,
                peakMemoryKb = 0L,
                stdout = "",
                stderr = message
            )
        }
    }
}
