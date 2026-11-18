package pes.gateway.tests;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.time.Duration;
import java.util.concurrent.*;

public class E2eWebSocketClient {
    private static RunExecutionResult pollUntilCompleted(BlockingQueue<String> messages, long timeoutSeconds) throws InterruptedException {
        long deadline = System.currentTimeMillis() + timeoutSeconds * 1000;
        java.util.List<String> progressStages = new java.util.ArrayList<>();
        String completedMsg = null;
        while (System.currentTimeMillis() < deadline) {
            long remaining = Math.max(1, deadline - System.currentTimeMillis());
            String msg = messages.poll(remaining, TimeUnit.MILLISECONDS);
            if (msg == null) {
                break;
            }
            System.out.println("Received: " + msg);
            if (msg.contains("\"run.progress\"")) {
                progressStages.add(msg);
            }
            if (msg.contains("\"run.completed\"")) {
                completedMsg = msg;
                break;
            }
        }
        return new RunExecutionResult(completedMsg, progressStages);
    }

    private record RunExecutionResult(String completedEvent, java.util.List<String> progressEvents) {}

    public static void main(String[] args) throws Exception {
        String wsUrl = args.length > 0 ? args[0] : "ws://127.0.0.1:8080/ws";
        System.out.println("Connecting to " + wsUrl);

        BlockingQueue<String> messages = new LinkedBlockingQueue<>();
        CompletableFuture<WebSocket> wsFuture = HttpClient.newHttpClient().newWebSocketBuilder()
            .header("Origin", "http://127.0.0.1:8080")
            .connectTimeout(Duration.ofSeconds(5))
            .buildAsync(URI.create(wsUrl), new WebSocket.Listener() {
                private final StringBuilder sb = new StringBuilder();

                @Override
                public CompletionStage<?> onText(WebSocket webSocket, CharSequence data, boolean last) {
                    sb.append(data);
                    if (last) {
                        String full = sb.toString();
                        sb.setLength(0);
                        messages.offer(full);
                    }
                    return WebSocket.Listener.super.onText(webSocket, data, last);
                }
            });

        WebSocket ws = wsFuture.get(5, TimeUnit.SECONDS);
        System.out.println("Connected to WebSocket server.");

        // 1. Join room
        System.out.println("\n[1] Joining room 'e2e-room'...");
        ws.sendText("{\"type\":\"room.join\",\"roomId\":\"e2e-room\"}", true);
        String joinResp = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + joinResp);
        if (joinResp == null || !joinResp.contains("\"room.joined\"")) {
            System.err.println("FAIL: Expected room.joined");
            System.exit(1);
        }

        // 2. Submit valid C run
        System.out.println("\n[2] Submitting valid C execution...");
        String cRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-c-001",
              "language": "c",
              "sourceCode": "#include <stdio.h>\\nint main(void) { printf(\\\"Hello from PES E2E C\\\\n\\\"); return 0; }\\n"
            }
            """;
        ws.sendText(cRun, true);
        String cAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + cAccepted);
        if (cAccepted == null || !cAccepted.contains("\"run.accepted\"")) {
            System.err.println("FAIL: Expected run.accepted for C");
            System.exit(1);
        }
        RunExecutionResult cResult = pollUntilCompleted(messages, 15);
        if (cResult.completedEvent() == null || !cResult.completedEvent().contains("\"OK\"") || !cResult.completedEvent().contains("Hello from PES E2E C")) {
            System.err.println("FAIL: Expected OK status and stdout from real sandbox");
            System.exit(1);
        }

        // 3. Submit invalid C run (compile error)
        System.out.println("\n[3] Submitting invalid C execution (compile error)...");
        String badCRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-c-bad",
              "language": "c",
              "sourceCode": "int main(void) { syntax error }\\n"
            }
            """;
        ws.sendText(badCRun, true);
        String badCAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + badCAccepted);
        RunExecutionResult badCResult = pollUntilCompleted(messages, 15);
        if (badCResult.completedEvent() == null || !badCResult.completedEvent().contains("\"COMPILE_ERROR\"")) {
            System.err.println("FAIL: Expected COMPILE_ERROR for invalid C");
            System.exit(1);
        }
        boolean badCSawRunning = badCResult.progressEvents().stream().anyMatch(e -> e.contains("\"stage\":\"RUNNING\""));
        if (badCSawRunning) {
            System.err.println("FAIL: Invalid C must NOT emit RUNNING milestone");
            System.exit(1);
        }

        // 4. Submit Python run
        System.out.println("\n[4] Submitting Python execution...");
        String pyRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-py-001",
              "language": "python",
              "sourceCode": "print(\\\"Hello from PES E2E Python\\\")\\n"
            }
            """;
        ws.sendText(pyRun, true);
        String pyAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + pyAccepted);
        RunExecutionResult pyResult = pollUntilCompleted(messages, 15);
        if (pyResult.completedEvent() == null || !pyResult.completedEvent().contains("\"OK\"") || !pyResult.completedEvent().contains("Hello from PES E2E Python")) {
            System.err.println("FAIL: Expected OK status and stdout for Python");
            System.exit(1);
        }

        // 5. Submit invalid Python run (syntax error -> COMPILE_ERROR, no RUNNING)
        System.out.println("\n[5] Submitting invalid Python execution (syntax error)...");
        String badPyRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-py-bad",
              "language": "python",
              "sourceCode": "def broken(:\\n    pass\\n"
            }
            """;
        ws.sendText(badPyRun, true);
        String badPyAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + badPyAccepted);
        RunExecutionResult badPyResult = pollUntilCompleted(messages, 15);
        if (badPyResult.completedEvent() == null || !badPyResult.completedEvent().contains("\"COMPILE_ERROR\"")) {
            System.err.println("FAIL: Expected COMPILE_ERROR for invalid Python syntax");
            System.exit(1);
        }
        boolean badPySawRunning = badPyResult.progressEvents().stream().anyMatch(e -> e.contains("\"stage\":\"RUNNING\""));
        if (badPySawRunning) {
            System.err.println("FAIL: Invalid Python must NOT emit RUNNING milestone");
            System.exit(1);
        }

        // 6. Submit Java run
        System.out.println("\n[6] Submitting valid Java execution...");
        String javaRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-java-001",
              "language": "java",
              "sourceCode": "public class Main { public static void main(String[] args) { System.out.println(\\\"Hello from PES E2E Java\\\"); } }\\n"
            }
            """;
        ws.sendText(javaRun, true);
        String javaAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + javaAccepted);
        RunExecutionResult javaResult = pollUntilCompleted(messages, 20);
        if (javaResult.completedEvent() == null || !javaResult.completedEvent().contains("\"OK\"") || !javaResult.completedEvent().contains("Hello from PES E2E Java")) {
            System.err.println("FAIL: Expected OK status and stdout for Java");
            System.exit(1);
        }

        // 7. Submit invalid Java run (syntax error -> COMPILE_ERROR, no RUNNING)
        System.out.println("\n[7] Submitting invalid Java execution (compile error)...");
        String badJavaRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-java-bad",
              "language": "java",
              "sourceCode": "public class Main { public static void main(String[] args) { broken code without semicolon } }\\n"
            }
            """;
        ws.sendText(badJavaRun, true);
        String badJavaAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + badJavaAccepted);
        RunExecutionResult badJavaResult = pollUntilCompleted(messages, 20);
        if (badJavaResult.completedEvent() == null || !badJavaResult.completedEvent().contains("\"COMPILE_ERROR\"")) {
            System.err.println("FAIL: Expected COMPILE_ERROR for invalid Java");
            System.exit(1);
        }
        boolean badJavaSawRunning = badJavaResult.progressEvents().stream().anyMatch(e -> e.contains("\"stage\":\"RUNNING\""));
        if (badJavaSawRunning) {
            System.err.println("FAIL: Invalid Java must NOT emit RUNNING milestone");
            System.exit(1);
        }

        // 8. Submit Kotlin run
        System.out.println("\n[8] Submitting valid Kotlin execution...");
        String ktRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-kt-001",
              "language": "kotlin",
              "sourceCode": "fun main() { println(\\\"Hello from PES E2E Kotlin\\\"); }\\n"
            }
            """;
        ws.sendText(ktRun, true);
        String ktAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + ktAccepted);
        RunExecutionResult ktResult = pollUntilCompleted(messages, 45);
        if (ktResult.completedEvent() == null || !ktResult.completedEvent().contains("\"OK\"") || !ktResult.completedEvent().contains("Hello from PES E2E Kotlin")) {
            System.err.println("FAIL: Expected OK status and stdout for Kotlin");
            System.exit(1);
        }

        // 9. Submit invalid Kotlin run (compile error -> COMPILE_ERROR, no RUNNING)
        System.out.println("\n[9] Submitting invalid Kotlin execution (compile error)...");
        String badKtRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-kt-bad",
              "language": "kotlin",
              "sourceCode": "fun main() { broken code }\\n"
            }
            """;
        ws.sendText(badKtRun, true);
        String badKtAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + badKtAccepted);
        RunExecutionResult badKtResult = pollUntilCompleted(messages, 45);
        if (badKtResult.completedEvent() == null || !badKtResult.completedEvent().contains("\"COMPILE_ERROR\"")) {
            System.err.println("FAIL: Expected COMPILE_ERROR for invalid Kotlin");
            System.exit(1);
        }
        boolean badKtSawRunning = badKtResult.progressEvents().stream().anyMatch(e -> e.contains("\"stage\":\"RUNNING\""));
        if (badKtSawRunning) {
            System.err.println("FAIL: Invalid Kotlin must NOT emit RUNNING milestone");
            System.exit(1);
        }

        // 10. Submit valid Node run
        System.out.println("\n[10] Submitting valid Node execution...");
        String nodeRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-node-001",
              "language": "node",
              "sourceCode": "console.log('Hello from PES E2E Node');\\n"
            }
            """;
        ws.sendText(nodeRun, true);
        String nodeAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + nodeAccepted);
        RunExecutionResult nodeResult = pollUntilCompleted(messages, 15);
        if (nodeResult.completedEvent() == null || !nodeResult.completedEvent().contains("\"OK\"") || !nodeResult.completedEvent().contains("Hello from PES E2E Node")) {
            System.err.println("FAIL: Expected OK status and stdout for Node");
            System.exit(1);
        }

        // 11. Submit invalid Node run (syntax error -> COMPILE_ERROR, no RUNNING)
        System.out.println("\n[11] Submitting invalid Node execution (syntax error)...");
        String badNodeRun = """
            {
              "type": "run",
              "roomId": "e2e-room",
              "requestId": "e2e-node-bad",
              "language": "node",
              "sourceCode": "function broken( {\\n"
            }
            """;
        ws.sendText(badNodeRun, true);
        String badNodeAccepted = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + badNodeAccepted);
        RunExecutionResult badNodeResult = pollUntilCompleted(messages, 15);
        if (badNodeResult.completedEvent() == null || !badNodeResult.completedEvent().contains("\"COMPILE_ERROR\"")) {
            System.err.println("FAIL: Expected COMPILE_ERROR for invalid Node");
            System.exit(1);
        }
        boolean badNodeSawRunning = badNodeResult.progressEvents().stream().anyMatch(e -> e.contains("\"stage\":\"RUNNING\""));
        if (badNodeSawRunning) {
            System.err.println("FAIL: Invalid Node must NOT emit RUNNING milestone");
            System.exit(1);
        }

        // 12. Leave room & close
        System.out.println("\n[12] Leaving room...");
        ws.sendText("{\"type\":\"room.leave\",\"roomId\":\"e2e-room\"}", true);
        String leaveResp = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + leaveResp);

        ws.sendClose(WebSocket.NORMAL_CLOSURE, "Done").get(5, TimeUnit.SECONDS);
        System.out.println("\nSUCCESS: All E2E WebSocket verification checks passed against real sandbox runner!");
    }
}
