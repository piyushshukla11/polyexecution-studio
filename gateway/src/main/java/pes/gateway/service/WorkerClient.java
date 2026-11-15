package pes.gateway.service;

import pes.execution.v1.ExecuteRequest;
import pes.execution.v1.ExecuteResponse;

import java.util.concurrent.CompletableFuture;

public interface WorkerClient {
    CompletableFuture<ExecuteResponse> executeAsync(ExecuteRequest request);
    void close();
}
