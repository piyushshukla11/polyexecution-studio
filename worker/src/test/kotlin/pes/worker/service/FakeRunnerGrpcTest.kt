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
import pes.execution.v1.ExecutionStatus
import pes.execution.v1.Language
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
            assertEquals(3, fake.calls.size, "C protocol path has compile and run calls; Python has one")
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
