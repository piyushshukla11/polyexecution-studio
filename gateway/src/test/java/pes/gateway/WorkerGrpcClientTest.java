package pes.gateway;

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
import pes.execution.v1.*;
import pes.gateway.service.WorkerGrpcClient;

import java.io.IOException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;

import static org.junit.jupiter.api.Assertions.*;

class WorkerGrpcClientTest {

    private String serverName;
    private Server server;
    private ManagedChannel channel;
    private WorkerGrpcClient client;
    private volatile boolean returnError = false;
    private volatile long simulateDelayMs = 0L;

    private final ExecutionServiceGrpc.ExecutionServiceImplBase serviceImpl =
        new ExecutionServiceGrpc.ExecutionServiceImplBase() {
            @Override
            public void execute(ExecuteRequest request, StreamObserver<ExecuteResponse> responseObserver) {
                if (simulateDelayMs > 0) {
                    try {
                        Thread.sleep(simulateDelayMs);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                }
                if (returnError) {
                    responseObserver.onError(Status.UNAVAILABLE.withDescription("Service down").asRuntimeException());
                    return;
                }
                ExecuteResponse response = ExecuteResponse.newBuilder()
                    .setJobId(request.getJobId())
                    .setStatus(ExecutionStatus.OK)
                    .setStdout("output: " + request.getSourceCode())
                    .setStderr("")
                    .setExitCode(0)
                    .setSignal(0)
                    .setWallMs(10)
                    .setCpuMs(5)
                    .setPeakMemoryKb(512)
                    .build();
                responseObserver.onNext(response);
                responseObserver.onCompleted();
            }
        };

    @BeforeEach
    void setUp() throws IOException {
        serverName = InProcessServerBuilder.generateName();
        server = InProcessServerBuilder.forName(serverName)
            .directExecutor()
            .addService(serviceImpl)
            .build()
            .start();

        channel = InProcessChannelBuilder.forName(serverName)
            .directExecutor()
            .build();

        client = new WorkerGrpcClient(channel);
        returnError = false;
        simulateDelayMs = 0L;
    }

    @AfterEach
    void tearDown() {
        if (client != null) {
            client.close();
        }
        if (channel != null && !channel.isShutdown()) {
            channel.shutdownNow();
        }
        if (server != null && !server.isShutdown()) {
            server.shutdownNow();
        }
    }

    @Test
    @DisplayName("WorkerGrpcClient sends request and returns ExecuteResponse")
    void testExecuteAsyncSuccess() throws Exception {
        ExecuteRequest request = ExecuteRequest.newBuilder()
            .setJobId("test-job-1")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("printf")
            .setLimits(ResourceLimits.newBuilder().setWallMs(1000).build())
            .build();

        CompletableFuture<ExecuteResponse> future = client.executeAsync(request);
        ExecuteResponse response = future.get(5, TimeUnit.SECONDS);

        assertNotNull(response);
        assertEquals("test-job-1", response.getJobId());
        assertEquals(ExecutionStatus.OK, response.getStatus());
        assertEquals("output: printf", response.getStdout());
        assertEquals(0, response.getExitCode());
    }

    @Test
    @DisplayName("WorkerGrpcClient propagates gRPC failure through CompletableFuture")
    void testExecuteAsyncFailure() {
        returnError = true;

        ExecuteRequest request = ExecuteRequest.newBuilder()
            .setJobId("test-job-err")
            .setLanguage(Language.LANGUAGE_PYTHON)
            .setSourceCode("print(1)")
            .build();

        CompletableFuture<ExecuteResponse> future = client.executeAsync(request);
        ExecutionException ex = assertThrows(ExecutionException.class, () -> future.get(5, TimeUnit.SECONDS));
        assertTrue(ex.getCause() instanceof io.grpc.StatusRuntimeException);
        io.grpc.StatusRuntimeException sre = (io.grpc.StatusRuntimeException) ex.getCause();
        assertEquals(Status.Code.UNAVAILABLE, sre.getStatus().getCode());
    }

    @Test
    @DisplayName("C request with simulated 11s compile delay succeeds under new 24s deadline but would fail under old 10s deadline")
    void testExecuteAsyncCCompileBudget() throws Exception {
        // Simulate a slow GCC compilation and execution that takes 11 seconds (11000 ms).
        // Under the old formula: max(10000ms, wallMs + 5000ms) = max(10000ms, 6000ms) = 10000ms (10.0s).
        // An 11s delay exceeds the old 10s deadline and would abort with DEADLINE_EXCEEDED.
        // Under the new formula with C compile budget (13s):
        // deadline = 13000ms + 1000ms (wall) + 5000ms (runner grace) + 5000ms (gateway margin) = 24000ms (24.0s).
        // The 11s delay completes cleanly within the 24s deadline.
        simulateDelayMs = 11000L;

        ExecuteRequest request = ExecuteRequest.newBuilder()
            .setJobId("test-c-compile-budget")
            .setLanguage(Language.LANGUAGE_C)
            .setSourceCode("int main(){return 0;}")
            .setLimits(ResourceLimits.newBuilder().setWallMs(1000).build())
            .build();

        CompletableFuture<ExecuteResponse> future = client.executeAsync(request);
        ExecuteResponse response = future.get(15, TimeUnit.SECONDS);

        assertNotNull(response);
        assertEquals("test-c-compile-budget", response.getJobId());
        assertEquals(ExecutionStatus.OK, response.getStatus());
    }
}
