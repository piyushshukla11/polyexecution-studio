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
import org.springframework.web.socket.WebSocketHttpHeaders;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.client.standard.StandardWebSocketClient;
import org.springframework.web.socket.handler.TextWebSocketHandler;
import pes.execution.v1.ExecuteRequest;
import pes.execution.v1.ExecuteResponse;
import pes.execution.v1.ExecutionProgress;
import pes.execution.v1.ExecutionServiceGrpc;
import pes.execution.v1.ExecutionStage;
import pes.execution.v1.ExecutionStatus;
import pes.execution.v1.ExecutionStreamEvent;
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

        @Override
        public void executeProgressStream(ExecuteRequest request, StreamObserver<ExecutionStreamEvent> responseObserver) {
            recordedRequests.add(request);

            if (simulateUnavailable.get()) {
                responseObserver.onError(Status.UNAVAILABLE.withDescription("Simulated worker crash").asRuntimeException());
                return;
            }

            if (request.getSourceCode().contains("compile_error")) {
                responseObserver.onNext(ExecutionStreamEvent.newBuilder()
                    .setJobId(request.getJobId())
                    .setProgress(ExecutionProgress.newBuilder()
                        .setJobId(request.getJobId())
                        .setStage(ExecutionStage.STAGE_COMPILING)
                        .build())
                    .build());

                ExecuteResponse response = ExecuteResponse.newBuilder()
                    .setJobId(request.getJobId())
                    .setStatus(ExecutionStatus.COMPILE_ERROR)
                    .setStderr("syntax error: unknown token")
                    .setExitCode(1)
                    .setWallMs(15)
                    .setCpuMs(8)
                    .setPeakMemoryKb(512)
                    .build();

                responseObserver.onNext(ExecutionStreamEvent.newBuilder()
                    .setJobId(request.getJobId())
                    .setResult(response)
                    .build());
                responseObserver.onCompleted();
                return;
            }

            if (request.getLanguage() == Language.LANGUAGE_C) {
                responseObserver.onNext(ExecutionStreamEvent.newBuilder()
                    .setJobId(request.getJobId())
                    .setProgress(ExecutionProgress.newBuilder()
                        .setJobId(request.getJobId())
                        .setStage(ExecutionStage.STAGE_COMPILING)
                        .build())
                    .build());
            }

            responseObserver.onNext(ExecutionStreamEvent.newBuilder()
                .setJobId(request.getJobId())
                .setProgress(ExecutionProgress.newBuilder()
                    .setJobId(request.getJobId())
                    .setStage(ExecutionStage.STAGE_RUNNING)
                    .build())
                .build());

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

            responseObserver.onNext(ExecutionStreamEvent.newBuilder()
                .setJobId(request.getJobId())
                .setResult(response)
                .build());
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
    @DisplayName("Unapproved origin is rejected with HTTP 403 by WebSocket handshake")
    void testUnapprovedOriginRejected() {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://evil.com");

        assertThrows(Exception.class, () -> client.execute(handler, headers, uri).get(5, TimeUnit.SECONDS));
    }

    @Test
    @DisplayName("Approved development origin is accepted by WebSocket handshake")
    void testApprovedOriginAccepted() throws Exception {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://localhost:8080");

        WebSocketSession session = client.execute(handler, headers, uri).get(5, TimeUnit.SECONDS);
        assertTrue(session.isOpen());
        session.close();
    }

    @Test
    @DisplayName("End-to-end WebSocket client joins room, triggers run, receives accepted and completed events")
    void testWebSocketClientRunSuccess() throws Exception {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        WebSocketSession session = client.execute(handler, headers, uri).get(5, TimeUnit.SECONDS);
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

        // 4. Receive run.progress event (COMPILING)
        String compilingMsg = handler.nextMessage(5000);
        assertNotNull(compilingMsg, "Expected run.progress (COMPILING) message");
        JsonNode compilingJson = objectMapper.readTree(compilingMsg);
        assertEquals("run.progress", compilingJson.get("type").asText());
        assertEquals("test-room-1", compilingJson.get("roomId").asText());
        assertEquals("req-001", compilingJson.get("requestId").asText());
        assertEquals("COMPILING", compilingJson.get("stage").asText());

        // 5. Receive run.progress event (RUNNING)
        String runningMsg = handler.nextMessage(5000);
        assertNotNull(runningMsg, "Expected run.progress (RUNNING) message");
        JsonNode runningJson = objectMapper.readTree(runningMsg);
        assertEquals("run.progress", runningJson.get("type").asText());
        assertEquals("test-room-1", runningJson.get("roomId").asText());
        assertEquals("req-001", runningJson.get("requestId").asText());
        assertEquals("RUNNING", runningJson.get("stage").asText());

        // 6. Receive run.completed event
        String completedMsg = handler.nextMessage(5000);
        assertNotNull(completedMsg, "Expected run.completed message");
        JsonNode completedJson = objectMapper.readTree(completedMsg);
        assertEquals("run.completed", completedJson.get("type").asText());
        assertEquals("test-room-1", completedJson.get("roomId").asText());
        assertEquals("req-001", completedJson.get("requestId").asText());
        assertEquals("OK", completedJson.get("status").asText());
        assertEquals("Fake output for req-001", completedJson.get("stdout").asText());
        assertEquals(0, completedJson.get("exitCode").asInt());

        // 7. Verify fake gRPC worker received typed protobuf
        assertEquals(1, fakeService.recordedRequests.size());
        ExecuteRequest req = fakeService.recordedRequests.get(0);
        assertEquals("req-001", req.getJobId());
        assertEquals(Language.LANGUAGE_C, req.getLanguage());
        assertEquals("int main() { return 0; }", req.getSourceCode());
        assertEquals("test", req.getStdin());

        session.close();
    }

    @Test
    @DisplayName("C compilation error emits COMPILING progress then COMPILE_ERROR without RUNNING progress")
    void testCCompilationErrorProgressHaltWithoutRunning() throws Exception {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        WebSocketSession session = client.execute(handler, headers, uri).get(5, TimeUnit.SECONDS);
        session.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"test-room-c-err\"}"));
        assertNotNull(handler.nextMessage(5000));

        String runPayload = """
            {
              "type": "run",
              "roomId": "test-room-c-err",
              "requestId": "req-c-err",
              "language": "c",
              "sourceCode": "compile_error syntax error"
            }
            """;
        session.sendMessage(new TextMessage(runPayload));

        // 1. run.accepted
        String acceptedMsg = handler.nextMessage(5000);
        assertNotNull(acceptedMsg);
        assertEquals("run.accepted", objectMapper.readTree(acceptedMsg).get("type").asText());

        // 2. run.progress COMPILING
        String progMsg = handler.nextMessage(5000);
        assertNotNull(progMsg);
        JsonNode progJson = objectMapper.readTree(progMsg);
        assertEquals("run.progress", progJson.get("type").asText());
        assertEquals("COMPILING", progJson.get("stage").asText());

        // 3. run.completed with COMPILE_ERROR (NEVER RUNNING)
        String completedMsg = handler.nextMessage(5000);
        assertNotNull(completedMsg);
        JsonNode completedJson = objectMapper.readTree(completedMsg);
        assertEquals("run.completed", completedJson.get("type").asText());
        assertEquals("COMPILE_ERROR", completedJson.get("status").asText());
        assertEquals(1, completedJson.get("exitCode").asInt());

        session.close();
    }

    @Test
    @DisplayName("Cross-room event isolation: second room does not receive events from first room")
    void testCrossRoomEventIsolation() throws Exception {
        StandardWebSocketClient client = new StandardWebSocketClient();
        TestWebSocketHandler handler1 = new TestWebSocketHandler();
        TestWebSocketHandler handler2 = new TestWebSocketHandler();
        URI uri = URI.create("ws://127.0.0.1:" + port + "/ws");

        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        WebSocketSession session1 = client.execute(handler1, headers, uri).get(5, TimeUnit.SECONDS);
        WebSocketSession session2 = client.execute(handler2, headers, uri).get(5, TimeUnit.SECONDS);

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

        // session 1 receives accepted, progress (RUNNING), and completed
        assertNotNull(handler1.nextMessage(5000)); // run.accepted
        assertNotNull(handler1.nextMessage(5000)); // run.progress
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

        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        WebSocketSession session = client.execute(handler, headers, uri).get(5, TimeUnit.SECONDS);
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

        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        WebSocketSession session = client.execute(handler, headers, uri).get(5, TimeUnit.SECONDS);
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

    @Test
    @DisplayName("Collab update is relayed to room peer and excluded from sender")
    void testCollabUpdateRelayedToRoomAndExcludedFromSender() throws Exception {
        StandardWebSocketClient wsClient = new StandardWebSocketClient();
        URI uri = new URI("ws://127.0.0.1:" + port + "/ws");
        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        TestWebSocketHandler handlerA = new TestWebSocketHandler();
        WebSocketSession sessionA = wsClient.execute(handlerA, headers, uri).get(5, TimeUnit.SECONDS);

        TestWebSocketHandler handlerB = new TestWebSocketHandler();
        WebSocketSession sessionB = wsClient.execute(handlerB, headers, uri).get(5, TimeUnit.SECONDS);

        // Both join same room
        sessionA.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"collab-relay-room\"}"));
        assertNotNull(handlerA.nextMessage(5000));

        sessionB.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"collab-relay-room\"}"));
        assertNotNull(handlerB.nextMessage(5000));
        // handlerA gets room.joined for sessionB
        assertNotNull(handlerA.nextMessage(5000));

        // Session A sends collab.update
        String sampleUpdate = "dGVzdC15anMtdXBkYXRl";
        sessionA.sendMessage(new TextMessage("{\"type\":\"collab.update\",\"update\":\"" + sampleUpdate + "\"}"));

        // Session B must receive collab.update
        String receivedByB = handlerB.nextMessage(5000);
        assertNotNull(receivedByB, "Session B should receive collab.update");
        JsonNode jsonB = objectMapper.readTree(receivedByB);
        assertEquals("collab.update", jsonB.get("type").asText());
        assertEquals("collab-relay-room", jsonB.get("roomId").asText());
        assertEquals(sampleUpdate, jsonB.get("update").asText());

        // Session A must NOT receive its own collab.update
        assertNull(handlerA.nextMessage(500), "Session A must not receive echoed collab.update");

        sessionA.close();
        sessionB.close();
    }

    @Test
    @DisplayName("Collab sync returns accumulated room updates to newly joined session")
    void testCollabSyncReturnsStoredUpdatesToNewSession() throws Exception {
        StandardWebSocketClient wsClient = new StandardWebSocketClient();
        URI uri = new URI("ws://127.0.0.1:" + port + "/ws");
        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        TestWebSocketHandler handlerA = new TestWebSocketHandler();
        WebSocketSession sessionA = wsClient.execute(handlerA, headers, uri).get(5, TimeUnit.SECONDS);

        // Session A joins room and submits an update
        sessionA.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"collab-sync-room\"}"));
        assertNotNull(handlerA.nextMessage(5000));

        String initialUpdate = "aW5pdGlhbC1kb2MtdXBkYXRl";
        sessionA.sendMessage(new TextMessage("{\"type\":\"collab.update\",\"update\":\"" + initialUpdate + "\"}"));

        // Session B connects later and joins same room
        TestWebSocketHandler handlerB = new TestWebSocketHandler();
        WebSocketSession sessionB = wsClient.execute(handlerB, headers, uri).get(5, TimeUnit.SECONDS);
        sessionB.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"collab-sync-room\"}"));
        assertNotNull(handlerB.nextMessage(5000));

        // Session B requests collab.sync
        sessionB.sendMessage(new TextMessage("{\"type\":\"collab.sync\"}"));

        String syncResponse = handlerB.nextMessage(5000);
        assertNotNull(syncResponse, "Session B should receive collab.sync");
        JsonNode syncJson = objectMapper.readTree(syncResponse);
        assertEquals("collab.sync", syncJson.get("type").asText());
        assertEquals("collab-sync-room", syncJson.get("roomId").asText());
        assertTrue(syncJson.has("updates"));
        assertEquals(1, syncJson.get("updates").size());
        assertEquals(initialUpdate, syncJson.get("updates").get(0).asText());

        sessionA.close();
        sessionB.close();
    }

    @Test
    @DisplayName("Collab awareness is relayed with server-verified sender session ID and excluded from sender")
    void testCollabAwarenessRelayedWithServerSenderSessionId() throws Exception {
        StandardWebSocketClient wsClient = new StandardWebSocketClient();
        URI uri = new URI("ws://127.0.0.1:" + port + "/ws");
        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        TestWebSocketHandler handlerA = new TestWebSocketHandler();
        WebSocketSession sessionA = wsClient.execute(handlerA, headers, uri).get(5, TimeUnit.SECONDS);

        TestWebSocketHandler handlerB = new TestWebSocketHandler();
        WebSocketSession sessionB = wsClient.execute(handlerB, headers, uri).get(5, TimeUnit.SECONDS);

        sessionA.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"awareness-room\"}"));
        String joinedMsgA = handlerA.nextMessage(5000);
        assertNotNull(joinedMsgA);
        String serverSessionIdA = objectMapper.readTree(joinedMsgA).get("sessionId").asText();

        sessionB.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"awareness-room\"}"));
        assertNotNull(handlerB.nextMessage(5000));
        assertNotNull(handlerA.nextMessage(5000)); // joined notification

        String sampleAwareness = "YXdhcmVuZXNzLXBheWxvYWQ=";
        sessionA.sendMessage(new TextMessage("{\"type\":\"collab.awareness\",\"awareness\":\"" + sampleAwareness + "\"}"));

        String receivedByB = handlerB.nextMessage(5000);
        assertNotNull(receivedByB, "Session B should receive collab.awareness");
        JsonNode jsonB = objectMapper.readTree(receivedByB);
        assertEquals("collab.awareness", jsonB.get("type").asText());
        assertEquals("awareness-room", jsonB.get("roomId").asText());
        assertEquals(serverSessionIdA, jsonB.get("senderSessionId").asText());
        assertEquals(sampleAwareness, jsonB.get("awareness").asText());

        // Session A must not receive its own awareness
        assertNull(handlerA.nextMessage(500));

        sessionA.close();
        sessionB.close();
    }

    @Test
    @DisplayName("Collab updates and awareness are strictly isolated across rooms")
    void testCollabIsolationAcrossRooms() throws Exception {
        StandardWebSocketClient wsClient = new StandardWebSocketClient();
        URI uri = new URI("ws://127.0.0.1:" + port + "/ws");
        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        TestWebSocketHandler handlerA = new TestWebSocketHandler();
        WebSocketSession sessionA = wsClient.execute(handlerA, headers, uri).get(5, TimeUnit.SECONDS);

        TestWebSocketHandler handlerB = new TestWebSocketHandler();
        WebSocketSession sessionB = wsClient.execute(handlerB, headers, uri).get(5, TimeUnit.SECONDS);

        sessionA.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"room-iso-1\"}"));
        assertNotNull(handlerA.nextMessage(5000));

        sessionB.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"room-iso-2\"}"));
        assertNotNull(handlerB.nextMessage(5000));

        // Session A sends update and awareness
        sessionA.sendMessage(new TextMessage("{\"type\":\"collab.update\",\"update\":\"dXBkYXRlMSI=\"}"));
        sessionA.sendMessage(new TextMessage("{\"type\":\"collab.awareness\",\"awareness\":\"YXdhcmUx\"}"));

        // Session B in room-iso-2 must receive nothing
        assertNull(handlerB.nextMessage(500), "Session B in room-iso-2 must not receive updates from room-iso-1");

        sessionA.close();
        sessionB.close();
    }

    @Test
    @DisplayName("Collab security and bounds: rejects not-in-room, oversized, and blank payloads")
    void testCollabSecurityAndBounds() throws Exception {
        StandardWebSocketClient wsClient = new StandardWebSocketClient();
        URI uri = new URI("ws://127.0.0.1:" + port + "/ws");
        WebSocketHttpHeaders headers = new WebSocketHttpHeaders();
        headers.setOrigin("http://127.0.0.1:8080");

        TestWebSocketHandler handler = new TestWebSocketHandler();
        WebSocketSession session = wsClient.execute(handler, headers, uri).get(5, TimeUnit.SECONDS);

        // 1. Send collab.update before joining room -> NOT_IN_ROOM
        session.sendMessage(new TextMessage("{\"type\":\"collab.update\",\"update\":\"dXBkYXRl\"}"));
        String errNotJoined = handler.nextMessage(5000);
        assertNotNull(errNotJoined);
        JsonNode jsonNotJoined = objectMapper.readTree(errNotJoined);
        assertEquals("error", jsonNotJoined.get("type").asText());
        assertEquals("NOT_IN_ROOM", jsonNotJoined.get("code").asText());

        // Join room
        session.sendMessage(new TextMessage("{\"type\":\"room.join\",\"roomId\":\"bounds-room\"}"));
        assertNotNull(handler.nextMessage(5000));

        // 2. Oversized collab.update (> 64 KiB)
        String oversizedUpdate = "A".repeat(65536 + 10);
        session.sendMessage(new TextMessage("{\"type\":\"collab.update\",\"update\":\"" + oversizedUpdate + "\"}"));
        String errOversized = handler.nextMessage(5000);
        assertNotNull(errOversized);
        JsonNode jsonOversized = objectMapper.readTree(errOversized);
        assertEquals("error", jsonOversized.get("type").asText());
        assertEquals("MESSAGE_TOO_LARGE", jsonOversized.get("code").asText());

        // 3. Oversized collab.awareness (> 16 KiB)
        String oversizedAwareness = "B".repeat(16384 + 10);
        session.sendMessage(new TextMessage("{\"type\":\"collab.awareness\",\"awareness\":\"" + oversizedAwareness + "\"}"));
        String errOversizedAwareness = handler.nextMessage(5000);
        assertNotNull(errOversizedAwareness);
        JsonNode jsonOversizedAwareness = objectMapper.readTree(errOversizedAwareness);
        assertEquals("error", jsonOversizedAwareness.get("type").asText());
        assertEquals("MESSAGE_TOO_LARGE", jsonOversizedAwareness.get("code").asText());

        // 4. Blank update -> BAD_REQUEST
        session.sendMessage(new TextMessage("{\"type\":\"collab.update\",\"update\":\"   \"}"));
        String errBlank = handler.nextMessage(5000);
        assertNotNull(errBlank);
        JsonNode jsonBlank = objectMapper.readTree(errBlank);
        assertEquals("error", jsonBlank.get("type").asText());
        assertEquals("BAD_REQUEST", jsonBlank.get("code").asText());

        session.close();
    }
}
