package pes.worker.service

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import pes.execution.v1.ExecuteRequest
import pes.execution.v1.ExecuteResponse
import pes.execution.v1.ExecutionServiceGrpcKt
import pes.worker.pipeline.ExecutionPipeline

class ExecutionServiceImpl(
    private val pipeline: ExecutionPipeline
) : ExecutionServiceGrpcKt.ExecutionServiceCoroutineImplBase() {

    override suspend fun execute(request: ExecuteRequest): ExecuteResponse {
        return pipeline.execute(request)
    }

    override fun executeStream(request: ExecuteRequest): Flow<ExecuteResponse> = flow {
        val response = pipeline.execute(request)
        emit(response)
    }
}
