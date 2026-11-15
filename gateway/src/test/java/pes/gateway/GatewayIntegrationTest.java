package pes.gateway;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.grpc.ManagedChannel;
import io.grpc.Server;
import io.grpc.Status;
import io.grpc.inprocess.InProcessChannelBuilder;
import io.grpc.inprocess.InProcessServerBuilder;
import io.grpc.stub.StreamObserver;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Primary;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.client.standard.StandardWebSocketClient;
import org.springframework.web.socket.handler.TextWebSocketHandler;
import pes.execution.v1.ExecuteRequest;
import pes.execution.v1.ExecuteResponse;
import pes.execution.v1.ExecutionServiceGrpc;
import pes.execution.v1.ExecutionStatus;
import pes.execution.v1.Language;
import pes.gateway.service.WorkerClient;
import pes.gateway.service.WorkerGrpcClient;

import java.io.IOException;
import java.net.URI;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.jupiter.api.Assertions.*;

@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class GatewayIntegrationTest {

    @LocalServerPort
    private int port;

    @Autowired
    private FakeExecutionService fakeService;

    private final ObjectMapper objectMapper = new ObjectMapper();

    private static class FakeExecutionService extends ExecutionServiceGrpc.ExecutionServiceImplBase {
        private final List<ExecuteRequest> recordedRequests = new ArrayList<>();
        private final AtomicBoolean simulateUnavailable = new AtomicBoolean(false);

        @Override
        public void execute(ExecuteRequest request, StreamObserver<ExecuteResponse> responseObserver) {
            recordedRequests.add(request);

            if (simulateUnavailable.get()) {
                responseObserver.onError(Status.UNAVAILABLE.withDescription("Simulated worker crash").asRuntimeException());
                return;
            }

            ExecuteResponse response = ExecuteResponse.newBuilder()
                .setJobId(request.getJobId())
                .setStatus(ExecutionStatus.OK)
                .setStdout("Fake output for " + request.getJobId())
                .setStderr("")
                .setExitCode(0)
                .setSignal(0)
                .setWallMs(42)
                .setCpuMs(24)
                .setPeakMemoryKb(1024)
                .build();

            responseObserver.onNext(response);
            responseObserver.onCompleted();
        }

        public void reset() {
            recordedRequests.clear();
            simulateUnavailable.set(false);
        }
    }

    @TestConfiguration
    static class TestConfig {
        @Bean
        public FakeExecutionService fakeExecutionService() {
            return new FakeExecutionService();
        }

        @Bean
        @Primary
        public WorkerClient testWorkerClient(FakeExecutionService fakeService) throws IOException {
            String serverName = InProcessServerBuilder.generateName();
            Server server = InProcessServerBuilder.forName(serverName)
                .directExecutor()
                .addService(fakeService)
                .build()
                .start();

            ManagedChannel channel = InProcessChannelBuilder.forName(serverName)
                .directExecutor()
                .build();

            return new WorkerGrpcClient(channel);
        }
    }

    private static class TestWebSocketHandler extends TextWebSocketHandler {
        private final BlockingQueue<String> messages = new LinkedBlockingQueue<>();

        @Override
        protected void handleTextMessage(WebSocketSession session, TextMessage message) {
            messages.offer(message.getPayload());
        }

        public String nextMessage(long timeoutMs) throws InterruptedException {
            return messages.poll(timeoutMs, TimeUnit.MILLISECONDS);
        }
    }

    @BeforeEach
    void setUp() {
        fakeService.reset();
    }

    @Test
    @DisplayName("End-to-end WebSocket client joins room, triggers run, receives accepted and completed events")
    void testWebSocketClientRunSuccess() throws Exception {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketSession session = client.execute(handler, null, uri).get(5, TimeUnit.SECONDS);
        assertTrue(session.isOpen());

        // 1. Join room
        session.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"test-room-1\"}"));
        String joinedMsg = handler.nextMessage(5000);
        assertNotNull(joinedMsg, "Expected room.joined message");
        JsonNode joinedJson = objectMapper.readTree(joinedMsg);
        assertEquals("room.joined", joinedJson.get("type").asText());
        assertEquals("test-room-1", joinedJson.get("roomId").asText());

        // 2. Submit C run
        String runPayload = """
            {
              "type": "run",
              "roomId": "test-room-1",
              "requestId": "req-001",
              "language": "c",
              "sourceCode": "int main() { return 0; }",
              "stdin": "test"
            }
            """;
        session.sendMessage(new TextMessage(runPayload));

        // 3. Receive run.accepted event
        String acceptedMsg = handler.nextMessage(5000);
        assertNotNull(acceptedMsg, "Expected run.accepted message");
        JsonNode acceptedJson = objectMapper.readTree(acceptedMsg);
        assertEquals("run.accepted", acceptedJson.get("type").asText());
        assertEquals("test-room-1", acceptedJson.get("roomId").asText());
        assertEquals("req-001", acceptedJson.get("requestId").asText());
        assertEquals("c", acceptedJson.get("language").asText());

        // 4. Receive run.completed event
        String completedMsg = handler.nextMessage(5000);
        assertNotNull(completedMsg, "Expected run.completed message");
        JsonNode completedJson = objectMapper.readTree(completedMsg);
        assertEquals("run.completed", completedJson.get("type").asText());
        assertEquals("test-room-1", completedJson.get("roomId").asText());
        assertEquals("req-001", completedJson.get("requestId").asText());
        assertEquals("OK", completedJson.get("status").asText());
        assertEquals("Fake output for req-001", completedJson.get("stdout").asText());
        assertEquals(0, completedJson.get("exitCode").asInt());

        // 5. Verify fake gRPC worker received typed protobuf
        assertEquals(1, fakeService.recordedRequests.size());
        ExecuteRequest req = fakeService.recordedRequests.get(0);
        assertEquals("req-001", req.getJobId());
        assertEquals(Language.LANGUAGE_C, req.getLanguage());
        assertEquals("int main() { return 0; }", req.getSourceCode());
        assertEquals("test", req.getStdin());

        session.close();
    }

    @Test
    @DisplayName("Cross-room event isolation: second room does not receive events from first room")
    void testCrossRoomEventIsolation() throws Exception {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler1 = new TestWebSocketHandler();
        TestWebSocketHandler handler2 = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketSession session1 = client.execute(handler1, null, uri).get(5, TimeUnit.SECONDS);
        WebSocketSession session2 = client.execute(handler2, null, uri).get(5, TimeUnit.SECONDS);

        session1.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"room-iso-1\"}"));
        assertNotNull(handler1.nextMessage(5000));

        session2.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"room-iso-2\"}"));
        assertNotNull(handler2.nextMessage(5000));

        // Submit run in room-iso-1
        String runPayload = """
            {
              "type": "run",
              "roomId": "room-iso-1",
              "requestId": "req-iso",
              "language": "python",
              "sourceCode": "print('iso')"
            }
            """;
        session1.sendMessage(new TextMessage(runPayload));

        // session 1 receives accepted & completed
        assertNotNull(handler1.nextMessage(5000)); // run.accepted
        assertNotNull(handler1.nextMessage(5000)); // run.completed

        // session 2 in room-iso-2 must receive NOTHING
        String leaked = handler2.nextMessage(1000);
        assertNull(leaked, "Room 2 must not receive events from Room 1");

        session1.close();
        session2.close();
    }

    @Test
    @DisplayName("Invalid input is rejected with run.rejected without calling gRPC worker")
    void testInvalidInputRejectedWithoutCallingWorker() throws Exception {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketSession session = client.execute(handler, null, uri).get(5, TimeUnit.SECONDS);
        session.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"room-invalid-test\"}"));
        assertNotNull(handler.nextMessage(5000));

        // Invalid language
        String badRun = """
            {
              "type": "run",
              "roomId": "room-invalid-test",
              "requestId": "req-bad",
              "language": "ruby",
              "sourceCode": "puts 'hi'"
            }
            """;
        session.sendMessage(new TextMessage(badRun));

        String rejectedMsg = handler.nextMessage(5000);
        assertNotNull(rejectedMsg);
        JsonNode rejectedJson = objectMapper.readTree(rejectedMsg);
        assertEquals("run.rejected", rejectedJson.get("type").asText());
        assertEquals("UNSUPPORTED_LANGUAGE", rejectedJson.get("reason").asText());

        // Ensure fake worker was never called
        assertEquals(0, fakeService.recordedRequests.size());

        session.close();
    }

    @Test
    @DisplayName("Worker unavailable reports SANDBOX_ERROR without false success")
    void testWorkerUnavailableReportedFailClosed() throws Exception {
        fakeService.simulateUnavailable.set(true);

        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketSession session = client.execute(handler, null, uri).get(5, TimeUnit.SECONDS);
        session.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"room-unavail-test\"}"));
        assertNotNull(handler.nextMessage(5000));

        String runPayload = """
            {
              "type": "run",
              "roomId": "room-unavail-test",
              "requestId": "req-unavail",
              "language": "c",
              "sourceCode": "int main() { return 0; }"
            }
            """;
        session.sendMessage(new TextMessage(runPayload));

        // run.accepted
        String acceptedMsg = handler.nextMessage(5000);
        assertNotNull(acceptedMsg);
        assertEquals("run.accepted", objectMapper.readTree(acceptedMsg).get("type").asText());

        // run.completed with status SANDBOX_ERROR
        String completedMsg = handler.nextMessage(5000);
        assertNotNull(completedMsg);
        JsonNode completedJson = objectMapper.readTree(completedMsg);
        assertEquals("run.completed", completedJson.get("type").asText());
        assertEquals("SANDBOX_ERROR", completedJson.get("status").asText());
        assertTrue(completedJson.get("stderr").asText().contains("Worker execution service is unavailable"));
        assertEquals(-1, completedJson.get("exitCode").asInt());

        session.close();
    }
}
