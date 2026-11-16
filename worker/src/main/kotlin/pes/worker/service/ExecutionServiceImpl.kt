package pes.worker.service

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.flow
import pes.execution.v1.ExecuteRequest
import pes.execution.v1.ExecuteResponse
import pes.execution.v1.ExecutionProgress
import pes.execution.v1.ExecutionServiceGrpcKt
import pes.execution.v1.ExecutionStreamEvent
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

    override fun executeProgressStream(request: ExecuteRequest): Flow<ExecutionStreamEvent> = channelFlow {
        val response = pipeline.execute(request) { stage ->
            send(
                ExecutionStreamEvent.newBuilder()
                    .setJobId(request.jobId)
                    .setProgress(
                        ExecutionProgress.newBuilder()
                            .setJobId(request.jobId)
                            .setStage(stage)
                            .build()
                    )
                    .build()
            )
        }
        send(
            ExecutionStreamEvent.newBuilder()
                .setJobId(request.jobId)
                .setResult(response)
                .build()
        )
    }
}
