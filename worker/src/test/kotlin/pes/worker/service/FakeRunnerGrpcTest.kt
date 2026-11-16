package pes.worker.service

import io.grpc.inprocess.InProcessChannelBuilder
import io.grpc.inprocess.InProcessServerBuilder
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import pes.execution.v1.ExecuteRequest
import pes.execution.v1.ExecutionServiceGrpcKt
import pes.execution.v1.ExecutionStage
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.Language
import pes.worker.model.RunnerResult
import pes.worker.pipeline.ExecutionPipeline
import pes.worker.runner.FakeRunnerClient
import java.util.UUID

class FakeRunnerGrpcTest {
    @Test
    fun unaryAndServerStreamingCallsReturnProtocolOnlyFakeResponses() = runBlocking {
        val fake = FakeRunnerClient()
        val serviceName = "pes-fake-test-${UUID.randomUUID()}"
        val server = InProcessServerBuilder.forName(serviceName)
            .directExecutor()
            .addService(ExecutionServiceImpl(ExecutionPipeline(fake)))
            .build()
            .start()
        val channel = InProcessChannelBuilder.forName(serviceName).directExecutor().build()
        try {
            val stub = ExecutionServiceGrpcKt.ExecutionServiceCoroutineStub(channel)
            val cResponse = stub.execute(request("grpc-c-fake", Language.LANGUAGE_C, "int main(void) { return 0; }"))
            assertEquals(ExecutionStatus.OK, cResponse.status)
            assertTrue(cResponse.stdout.startsWith("FAKE_RUNNER: protocol response only"))

            val streamResponses = stub.executeStream(
                request("grpc-python-fake", Language.LANGUAGE_PYTHON, "print('should not run')")
            ).toList()
            assertEquals(1, streamResponses.size, "ExecuteStream currently sends one final response")
            assertEquals(ExecutionStatus.OK, streamResponses.single().status)
            assertTrue(streamResponses.single().stdout.startsWith("FAKE_RUNNER: protocol response only"))
            assertEquals(4, fake.calls.size, "C protocol path has compile and run calls; Python has compile and run calls")
        } finally {
            channel.shutdownNow()
            server.shutdownNow()
        }
    }

    @Test
    fun executeProgressStreamEmitsCompilingAndRunningForC() = runBlocking {
        val fake = FakeRunnerClient()
        val serviceName = "pes-fake-progress-${UUID.randomUUID()}"
        val server = InProcessServerBuilder.forName(serviceName)
            .directExecutor()
            .addService(ExecutionServiceImpl(ExecutionPipeline(fake)))
            .build()
            .start()
        val channel = InProcessChannelBuilder.forName(serviceName).directExecutor().build()
        try {
            val stub = ExecutionServiceGrpcKt.ExecutionServiceCoroutineStub(channel)
            val events = stub.executeProgressStream(
                request("grpc-c-progress", Language.LANGUAGE_C, "int main(void) { return 0; }")
            ).toList()

            assertEquals(3, events.size, "Expected 2 progress events and 1 result event")
            assertTrue(events[0].hasProgress())
            assertEquals(ExecutionStage.STAGE_COMPILING, events[0].progress.stage)
            assertTrue(events[1].hasProgress())
            assertEquals(ExecutionStage.STAGE_RUNNING, events[1].progress.stage)
            assertTrue(events[2].hasResult())
            assertEquals(ExecutionStatus.OK, events[2].result.status)
        } finally {
            channel.shutdownNow()
            server.shutdownNow()
        }
    }

    @Test
    fun executeProgressStreamHaltsOnCompileErrorWithoutRunningEvent() = runBlocking {
        val compileFailResult = RunnerResult(
            jobId = "grpc-c-fail-compile",
            status = ExecutionStatus.OK,
            exitCode = 1,
            signal = 0,
            wallMs = 10,
            cpuMs = 5,
            peakMemoryKb = 1024,
            stdout = "",
            stderr = "syntax error"
        )
        val fake = FakeRunnerClient(listOf(compileFailResult))
        val serviceName = "pes-fake-progress-fail-${UUID.randomUUID()}"
        val server = InProcessServerBuilder.forName(serviceName)
            .directExecutor()
            .addService(ExecutionServiceImpl(ExecutionPipeline(fake)))
            .build()
            .start()
        val channel = InProcessChannelBuilder.forName(serviceName).directExecutor().build()
        try {
            val stub = ExecutionServiceGrpcKt.ExecutionServiceCoroutineStub(channel)
            val events = stub.executeProgressStream(
                request("grpc-c-fail", Language.LANGUAGE_C, "invalid c syntax")
            ).toList()

            assertEquals(2, events.size, "Expected 1 compile progress event and 1 result event")
            assertTrue(events[0].hasProgress())
            assertEquals(ExecutionStage.STAGE_COMPILING, events[0].progress.stage)
            assertTrue(events[1].hasResult())
            assertEquals(ExecutionStatus.COMPILE_ERROR, events[1].result.status)
        } finally {
            channel.shutdownNow()
            server.shutdownNow()
        }
    }

    @Test
    fun executeProgressStreamEmitsRunningForPython() = runBlocking {
        val fake = FakeRunnerClient()
        val serviceName = "pes-fake-progress-py-${UUID.randomUUID()}"
        val server = InProcessServerBuilder.forName(serviceName)
            .directExecutor()
            .addService(ExecutionServiceImpl(ExecutionPipeline(fake)))
            .build()
            .start()
        val channel = InProcessChannelBuilder.forName(serviceName).directExecutor().build()
        try {
            val stub = ExecutionServiceGrpcKt.ExecutionServiceCoroutineStub(channel)
            val events = stub.executeProgressStream(
                request("grpc-py-progress", Language.LANGUAGE_PYTHON, "print('hello')")
            ).toList()

            assertEquals(3, events.size, "Expected 2 progress events (compiling, running) and 1 result event")
            assertTrue(events[0].hasProgress())
            assertEquals(ExecutionStage.STAGE_COMPILING, events[0].progress.stage)
            assertTrue(events[1].hasProgress())
            assertEquals(ExecutionStage.STAGE_RUNNING, events[1].progress.stage)
            assertTrue(events[2].hasResult())
            assertEquals(ExecutionStatus.OK, events[2].result.status)
        } finally {
            channel.shutdownNow()
            server.shutdownNow()
        }
    }

    private fun request(jobId: String, language: Language, source: String): ExecuteRequest =
        ExecuteRequest.newBuilder()
            .setJobId(jobId)
            .setLanguage(language)
            .setSourceCode(source)
            .build()
}
