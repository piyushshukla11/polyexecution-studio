package pes.worker.model

import com.google.gson.Gson
import org.junit.jupiter.api.Assertions.*
import org.junit.jupiter.api.Test
import pes.execution.v1.ExecutionStatus

class StatusMappingTest {

    private val gson = Gson()

    @Test
    fun testParseStatusEnumMappings() {
        assertEquals(ExecutionStatus.OK, RunnerResult.parseStatus("OK"))
        assertEquals(ExecutionStatus.OK, RunnerResult.parseStatus("ok"))
        assertEquals(ExecutionStatus.COMPILE_ERROR, RunnerResult.parseStatus("COMPILE_ERROR"))
        assertEquals(ExecutionStatus.RUNTIME_ERROR, RunnerResult.parseStatus("RUNTIME_ERROR"))
        assertEquals(ExecutionStatus.TIMEOUT, RunnerResult.parseStatus("TIMEOUT"))
        assertEquals(ExecutionStatus.MEMORY_LIMIT, RunnerResult.parseStatus("MEMORY_LIMIT"))
        assertEquals(ExecutionStatus.OUTPUT_LIMIT, RunnerResult.parseStatus("OUTPUT_LIMIT"))
        assertEquals(ExecutionStatus.SANDBOX_ERROR, RunnerResult.parseStatus("SANDBOX_ERROR"))
        // Unknown or null falls back to SANDBOX_ERROR
        assertEquals(ExecutionStatus.SANDBOX_ERROR, RunnerResult.parseStatus(null))
        assertEquals(ExecutionStatus.SANDBOX_ERROR, RunnerResult.parseStatus("SOME_UNKNOWN_STATUS"))
    }

    @Test
    fun testParseRawRunnerJson() {
        val json = """
            {
              "job_id": "test-job-42",
              "status": "OK",
              "exit_code": 0,
              "signal": 0,
              "wall_ms": 150,
              "cpu_ms": 95,
              "peak_memory_kb": 4096,
              "stdout": "Hello, World!\n",
              "stderr": ""
            }
        """.trimIndent()

        val raw = gson.fromJson(json, RawRunnerJson::class.java)
        assertNotNull(raw)
        val result = RunnerResult.fromRaw(raw, "fallback-id")

        assertEquals("test-job-42", result.jobId)
        assertEquals(ExecutionStatus.OK, result.status)
        assertEquals(0, result.exitCode)
        assertEquals(0, result.signal)
        assertEquals(150L, result.wallMs)
        assertEquals(95L, result.cpuMs)
        assertEquals(4096L, result.peakMemoryKb)
        assertEquals("Hello, World!\n", result.stdout)
        assertEquals("", result.stderr)
    }

    @Test
    fun testParseSandboxErrorJson() {
        val json = """
            {
              "job_id": "err-job",
              "status": "SANDBOX_ERROR",
              "exit_code": 127,
              "signal": 0,
              "wall_ms": 0,
              "cpu_ms": 0,
              "peak_memory_kb": 0,
              "stdout": "",
              "stderr": "setup error: stage=11 errno=95 (Operation not supported)"
            }
        """.trimIndent()

        val raw = gson.fromJson(json, RawRunnerJson::class.java)
        val result = RunnerResult.fromRaw(raw, "fallback-id")

        assertEquals("err-job", result.jobId)
        assertEquals(ExecutionStatus.SANDBOX_ERROR, result.status)
        assertEquals(127, result.exitCode)
        assertTrue(result.stderr.contains("stage=11 errno=95"))
    }
}
