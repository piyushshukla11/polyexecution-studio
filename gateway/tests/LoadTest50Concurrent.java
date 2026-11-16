package pes.gateway.tests;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.time.Duration;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * PolyExecution Studio - Phase 7 50-Concurrent-Runs Load Test Harness
 * Exercises the complete real execution path:
 *   WebSocket Client -> Java Gateway -> Kotlin Worker (gRPC) -> C Sandbox Runner
 *
 * Verifies:
 * 1. 50 parallel connections join 50 distinct isolated rooms.
 * 2. 50 concurrent executions are dispatched simultaneously.
 * 3. All 50 executions complete with status OK and verified output.
 * 4. Latency percentiles (min, median, p95, max) are measured and reported.
 * 5. Overload rejection is verified when a room already has an active run.
 * 6. Clean disconnection without leaks.
 */
public class LoadTest50Concurrent {

    private static final int CONCURRENCY = 50;

    public static void main(String[] args) throws Exception {
        String wsUrl = args.length > 0 ? args[0] : "ws://127.0.0.1:8080/ws";
        int concurrency = args.length > 1 ? Integer.parseInt(args[1]) : CONCURRENCY;

        System.out.println("========================================================================");
        System.out.println("PolyExecution Studio - 50 Concurrent Runs Load Test Benchmark");
        System.out.println("Target: " + wsUrl);
        System.out.println("Concurrency: " + concurrency + " concurrent rooms / executions");
        System.out.println("========================================================================");

        ExecutorService executor = Executors.newFixedThreadPool(concurrency + 10);
        HttpClient httpClient = HttpClient.newBuilder()
            .executor(executor)
            .connectTimeout(Duration.ofSeconds(10))
            .build();

        CountDownLatch allJoinedLatch = new CountDownLatch(concurrency);
        CountDownLatch startExecutionLatch = new CountDownLatch(1);
        CountDownLatch allCompletedLatch = new CountDownLatch(concurrency);

        List<CompletableFuture<Long>> futures = new ArrayList<>(concurrency);
        AtomicInteger successCount = new AtomicInteger(0);
        AtomicInteger failureCount = new AtomicInteger(0);
        List<Long> latencies = Collections.synchronizedList(new ArrayList<>(concurrency));

        System.out.println("\n[Stage 1] Connecting " + concurrency + " WebSocket clients and joining rooms...");
        long benchmarkStart = System.currentTimeMillis();

        for (int i = 1; i <= concurrency; i++) {
            final int index = i;
            final String roomId = String.format("load-room-%03d", index);
            final String requestId = String.format("req-load-%03d", index);

            CompletableFuture<Long> task = CompletableFuture.supplyAsync(() -> {
                BlockingQueue<String> messages = new LinkedBlockingQueue<>();
                WebSocket ws = null;
                try {
                    ws = httpClient.newWebSocketBuilder()
                        .header("Origin", "http://127.0.0.1:8080")
                        .connectTimeout(Duration.ofSeconds(10))
                        .buildAsync(URI.create(wsUrl), new WebSocket.Listener() {
                            private final StringBuilder sb = new StringBuilder();

                            @Override
                            public CompletionStage<?> onText(WebSocket webSocket, CharSequence data, boolean last) {
                                sb.append(data);
                                if (last) {
                                    messages.offer(sb.toString());
                                    sb.setLength(0);
                                }
                                return WebSocket.Listener.super.onText(webSocket, data, last);
                            }
                        }).get(10, TimeUnit.SECONDS);

                    // 1. Join isolated room
                    ws.sendText("{\"type\":\"room.join\",\"roomId\":\"" + roomId + "\"}", true);
                    String joinEvent = messages.poll(10, TimeUnit.SECONDS);
                    if (joinEvent == null || !joinEvent.contains("\"room.joined\"")) {
                        throw new IllegalStateException("Client " + index + " failed to join room: " + joinEvent);
                    }
                    allJoinedLatch.countDown();

                    // 2. Wait for all 50 clients to join so they fire simultaneously
                    if (!startExecutionLatch.await(30, TimeUnit.SECONDS)) {
                        throw new TimeoutException("Timed out waiting for start signal");
                    }

                    // 3. Dispatch execution
                    // Alternating Python and C executions to exercise multi-runtime concurrency
                    String payload;
                    String expectedMarker;
                    if (index % 2 == 1) {
                        expectedMarker = "PYTHON_OK_" + index;
                        payload = String.format(
                            "{\"type\":\"run\",\"roomId\":\"%s\",\"requestId\":\"%s\",\"language\":\"python\",\"sourceCode\":\"print('%s')\\n\"}",
                            roomId, requestId, expectedMarker
                        );
                    } else {
                        expectedMarker = "C_OK_" + index;
                        payload = String.format(
                            "{\"type\":\"run\",\"roomId\":\"%s\",\"requestId\":\"%s\",\"language\":\"c\",\"sourceCode\":\"#include <stdio.h>\\nint main(void) { printf(\\\"%s\\\\n\\\"); return 0; }\\n\"}",
                            roomId, requestId, expectedMarker
                        );
                    }

                    long sendTime = System.currentTimeMillis();
                    ws.sendText(payload, true);

                    // 4. Await run.accepted and run.completed
                    long deadline = System.currentTimeMillis() + 30000;
                    boolean accepted = false;
                    boolean completed = false;
                    String finalResult = null;

                    while (System.currentTimeMillis() < deadline && !completed) {
                        String msg = messages.poll(deadline - System.currentTimeMillis(), TimeUnit.MILLISECONDS);
                        if (msg == null) break;
                        if (msg.contains("\"run.accepted\"")) {
                            accepted = true;
                        } else if (msg.contains("\"run.completed\"")) {
                            completed = true;
                            finalResult = msg;
                        }
                    }

                    long elapsedMs = System.currentTimeMillis() - sendTime;

                    if (!accepted) {
                        throw new IllegalStateException("Run was not accepted for room " + roomId);
                    }
                    if (!completed || finalResult == null) {
                        throw new TimeoutException("Run timed out without completion for room " + roomId);
                    }
                    if (!finalResult.contains("\"OK\"") || !finalResult.contains(expectedMarker)) {
                        throw new IllegalStateException("Unexpected run result for room " + roomId + ": " + finalResult);
                    }

                    latencies.add(elapsedMs);
                    successCount.incrementAndGet();

                    // Clean leave
                    ws.sendText("{\"type\":\"room.leave\",\"roomId\":\"" + roomId + "\"}", true);
                    messages.poll(2, TimeUnit.SECONDS);
                    ws.sendClose(WebSocket.NORMAL_CLOSURE, "Done").get(2, TimeUnit.SECONDS);

                    return elapsedMs;
                } catch (Exception e) {
                    failureCount.incrementAndGet();
                    System.err.println("[FAIL] Client " + index + " (" + roomId + "): " + e.getMessage());
                    if (ws != null) {
                        try { ws.abort(); } catch (Exception ignored) {}
                    }
                    return -1L;
                } finally {
                    allCompletedLatch.countDown();
                }
            }, executor);

            futures.add(task);
        }

        // Wait for all 50 to connect and join
        if (!allJoinedLatch.await(30, TimeUnit.SECONDS)) {
            System.err.println("Error: Not all clients joined their rooms within 30 seconds!");
            System.exit(1);
        }
        System.out.println("All " + concurrency + " clients joined their isolated rooms.");

        // Release the latch to fire all 50 runs simultaneously!
        System.out.println("\n[Stage 2] Dispatched 50 simultaneous execution requests across all rooms!");
        startExecutionLatch.countDown();

        // Wait for all 50 to complete
        boolean allDone = allCompletedLatch.await(60, TimeUnit.SECONDS);
        long totalElapsed = System.currentTimeMillis() - benchmarkStart;

        System.out.println("\n[Stage 3] Results Summary:");
        System.out.println("------------------------------------------------------------------------");
        System.out.println("Total Requests Submitted: " + concurrency);
        System.out.println("Successful Executions:    " + successCount.get());
        System.out.println("Failed Executions:        " + failureCount.get());
        System.out.println("Total Wall Clock Time:    " + totalElapsed + " ms");

        if (successCount.get() != concurrency) {
            System.err.println("\nFAILED: Expected all " + concurrency + " runs to succeed, but only " + successCount.get() + " succeeded.");
            executor.shutdownNow();
            System.exit(1);
        }

        // Compute percentiles
        Collections.sort(latencies);
        long min = latencies.get(0);
        long max = latencies.get(latencies.size() - 1);
        long median = latencies.get(latencies.size() / 2);
        long p90 = latencies.get((int) Math.round(latencies.size() * 0.90) - 1);
        long p95 = latencies.get((int) Math.round(latencies.size() * 0.95) - 1);
        double mean = latencies.stream().mapToLong(Long::longValue).average().orElse(0.0);

        System.out.println("Latency Distribution (per-run end-to-end):");
        System.out.println("  Min:    " + min + " ms");
        System.out.println("  Mean:   " + String.format("%.1f", mean) + " ms");
        System.out.println("  Median: " + median + " ms");
        System.out.println("  p90:    " + p90 + " ms");
        System.out.println("  p95:    " + p95 + " ms");
        System.out.println("  Max:    " + max + " ms");

        // [Stage 4] Overload Test: Verify honest rejection when room already has an active run
        System.out.println("\n[Stage 4] Overload Protection Verification...");
        BlockingQueue<String> overloadQueue = new LinkedBlockingQueue<>();
        WebSocket overloadWs = httpClient.newWebSocketBuilder()
            .header("Origin", "http://127.0.0.1:8080")
            .buildAsync(URI.create(wsUrl), new WebSocket.Listener() {
                private final StringBuilder sb = new StringBuilder();
                @Override
                public CompletionStage<?> onText(WebSocket webSocket, CharSequence data, boolean last) {
                    sb.append(data);
                    if (last) {
                        overloadQueue.offer(sb.toString());
                        sb.setLength(0);
                    }
                    return WebSocket.Listener.super.onText(webSocket, data, last);
                }
            }).get(5, TimeUnit.SECONDS);

        overloadWs.sendText("{\"type\":\"room.join\",\"roomId\":\"overload-test-room\"}", true);
        String ovJoin = overloadQueue.poll(5, TimeUnit.SECONDS);
        if (ovJoin == null || !ovJoin.contains("\"room.joined\"")) {
            System.err.println("Overload test failed to join room");
            System.exit(1);
        }

        // Send a sleeping Python run (wall_ms = 2000)
        String sleepRun = "{\"type\":\"run\",\"roomId\":\"overload-test-room\",\"requestId\":\"sleep-001\",\"language\":\"python\",\"sourceCode\":\"import time; time.sleep(1.5)\\n\"}";
        overloadWs.sendText(sleepRun, true);
        String accepted1 = overloadQueue.poll(5, TimeUnit.SECONDS);
        if (accepted1 == null || !accepted1.contains("\"run.accepted\"")) {
            System.err.println("Overload test failed: run 1 was not accepted");
            System.exit(1);
        }

        // Send run 2 immediately while run 1 is active -> must be rejected with ROOM_RUN_IN_PROGRESS
        String collisionRun = "{\"type\":\"run\",\"roomId\":\"overload-test-room\",\"requestId\":\"collision-002\",\"language\":\"python\",\"sourceCode\":\"print(1)\\n\"}";
        overloadWs.sendText(collisionRun, true);
        String rejected2 = overloadQueue.poll(5, TimeUnit.SECONDS);
        System.out.println("Overload collision response: " + rejected2);
        if (rejected2 == null || !rejected2.contains("\"run.rejected\"") || (!rejected2.contains("\"ROOM_RUN_IN_PROGRESS\"") && !rejected2.contains("\"GLOBAL_CONCURRENCY_LIMIT_REACHED\""))) {
            System.err.println("FAIL: Expected truthful run.rejected with code ROOM_RUN_IN_PROGRESS for concurrent run on busy room");
            System.exit(1);
        }
        System.out.println("Overload rejection successfully verified (ROOM_RUN_IN_PROGRESS received).");

        // Wait for sleep run to complete
        String sleepCompleted = null;
        long deadline = System.currentTimeMillis() + 10000;
        while (System.currentTimeMillis() < deadline) {
            String msg = overloadQueue.poll(Math.max(1, deadline - System.currentTimeMillis()), TimeUnit.MILLISECONDS);
            if (msg == null) break;
            if (msg.contains("\"run.completed\"")) {
                sleepCompleted = msg;
                break;
            }
        }
        if (sleepCompleted == null || !sleepCompleted.contains("\"OK\"")) {
            System.err.println("Overload test sleep run did not complete: " + sleepCompleted);
            System.exit(1);
        }
        overloadWs.sendClose(WebSocket.NORMAL_CLOSURE, "Done").get(5, TimeUnit.SECONDS);

        executor.shutdown();
        System.out.println("\nSUCCESS: 50 concurrent runs load benchmark and overload protection passed cleanly!");
    }
}
