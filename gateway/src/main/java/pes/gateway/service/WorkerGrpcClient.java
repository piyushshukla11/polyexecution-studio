package pes.gateway.service;

import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import jakarta.annotation.PreDestroy;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import pes.execution.v1.ExecuteRequest;
import pes.execution.v1.ExecuteResponse;
import pes.execution.v1.ExecutionServiceGrpc;
import pes.execution.v1.ExecutionStage;
import pes.execution.v1.ExecutionStreamEvent;
import pes.execution.v1.Language;
import pes.gateway.config.GatewayProperties;

import java.util.Iterator;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

@Service
public class WorkerGrpcClient implements WorkerClient {
    private static final Logger log = LoggerFactory.getLogger(WorkerGrpcClient.class);

    private final ManagedChannel channel;
    private final ExecutionServiceGrpc.ExecutionServiceBlockingStub blockingStub;
    private final ExecutorService executor;
    private final boolean channelOwned;

    @Autowired
    public WorkerGrpcClient(GatewayProperties properties) {
        String target = properties.getWorkerGrpcTarget();
        log.info("Initializing gRPC client to worker at {}", target);
        this.channel = ManagedChannelBuilder.forTarget(target)
            .usePlaintext()
            .build();
        this.blockingStub = ExecutionServiceGrpc.newBlockingStub(channel);
        this.executor = Executors.newVirtualThreadPerTaskExecutor();
        this.channelOwned = true;
    }

    public WorkerGrpcClient(ManagedChannel channel) {
        this.channel = channel;
        this.blockingStub = ExecutionServiceGrpc.newBlockingStub(channel);
        this.executor = Executors.newVirtualThreadPerTaskExecutor();
        this.channelOwned = false;
    }

    @Override
    public CompletableFuture<ExecuteResponse> executeAsync(ExecuteRequest request) {
        return executeWithProgressAsync(request, null);
    }

    @Override
    public CompletableFuture<ExecuteResponse> executeWithProgressAsync(
        ExecuteRequest request,
        Consumer<ExecutionStage> onProgress
    ) {
        long deadlineMs = calculateDeadlineMs(request);

        return CompletableFuture.supplyAsync(() -> {
            Iterator<ExecutionStreamEvent> events = blockingStub
                .withDeadlineAfter(deadlineMs, TimeUnit.MILLISECONDS)
                .executeProgressStream(request);

            ExecuteResponse finalResult = null;
            while (events.hasNext()) {
                ExecutionStreamEvent event = events.next();
                if (event.hasProgress() && onProgress != null) {
                    onProgress.accept(event.getProgress().getStage());
                } else if (event.hasResult()) {
                    finalResult = event.getResult();
                }
            }
            if (finalResult == null) {
                throw new IllegalStateException("Worker closed stream without returning an ExecuteResponse");
            }
            return finalResult;
        }, executor);
    }

    private long calculateDeadlineMs(ExecuteRequest request) {
        long targetWallMs = request.hasLimits() && request.getLimits().getWallMs() > 0
            ? request.getLimits().getWallMs()
            : 5000L;

        // C requires sandboxed compilation before execution.
        // Worker allows up to 8000 ms compile wall time plus 5000 ms runner setup grace.
        long compileBudgetMs = (request.getLanguage() == Language.LANGUAGE_C)
            ? 13000L
            : 0L;

        long runnerGraceMs = 5000L;
        long gatewayMarginMs = 5000L;

        return compileBudgetMs + targetWallMs + runnerGraceMs + gatewayMarginMs;
    }

    @Override
    @PreDestroy
    public void close() {
        if (channelOwned && channel != null && !channel.isShutdown()) {
            try {
                channel.shutdown().awaitTermination(5, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                channel.shutdownNow();
            }
        }
        if (executor != null && !executor.isShutdown()) {
            executor.shutdown();
        }
    }
}
