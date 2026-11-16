package pes.gateway.tests;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.time.Duration;
import java.util.concurrent.*;

public class E2eWebSocketClient {
    private static String pollUntilCompleted(BlockingQueue<String> messages, long timeoutSeconds) throws InterruptedException {
        long deadline = System.currentTimeMillis() + timeoutSeconds * 1000;
        while (System.currentTimeMillis() < deadline) {
            long remaining = Math.max(1, deadline - System.currentTimeMillis());
            String msg = messages.poll(remaining, TimeUnit.MILLISECONDS);
            if (msg == null) {
                return null;
            }
            System.out.println("Received: " + msg);
            if (msg.contains("\"run.completed\"")) {
                return msg;
            }
        }
        return null;
    }

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
        String cCompleted = pollUntilCompleted(messages, 15);
        if (cCompleted == null || !cCompleted.contains("\"OK\"") || !cCompleted.contains("Hello from PES E2E C")) {
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
        String badCCompleted = pollUntilCompleted(messages, 15);
        if (badCCompleted == null || !badCCompleted.contains("\"COMPILE_ERROR\"")) {
            System.err.println("FAIL: Expected COMPILE_ERROR for invalid C");
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
        String pyCompleted = pollUntilCompleted(messages, 15);
        if (pyCompleted == null || !pyCompleted.contains("\"OK\"") || !pyCompleted.contains("Hello from PES E2E Python")) {
            System.err.println("FAIL: Expected OK status and stdout for Python");
            System.exit(1);
        }

        // 5. Leave room & close
        System.out.println("\n[5] Leaving room...");
        ws.sendText("{\"type\":\"room.leave\",\"roomId\":\"e2e-room\"}", true);
        String leaveResp = messages.poll(5, TimeUnit.SECONDS);
        System.out.println("Received: " + leaveResp);

        ws.sendClose(WebSocket.NORMAL_CLOSURE, "Done").get(5, TimeUnit.SECONDS);
        System.out.println("\nSUCCESS: All E2E WebSocket verification checks passed against real sandbox runner!");
    }
}
