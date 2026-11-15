package pes.gateway.service;

import pes.execution.v1.ExecuteRequest;
import pes.execution.v1.ExecuteResponse;
import pes.execution.v1.ExecutionStage;

import java.util.concurrent.CompletableFuture;
import java.util.function.Consumer;

public interface WorkerClient {
    CompletableFuture<ExecuteResponse> executeAsync(ExecuteRequest request);
    CompletableFuture<ExecuteResponse> executeWithProgressAsync(
        ExecuteRequest request,
        Consumer<ExecutionStage> onProgress
    );
    void close();
}
