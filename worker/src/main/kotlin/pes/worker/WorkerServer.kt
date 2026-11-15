package pes.worker

import io.grpc.Server
import io.grpc.ServerBuilder
import io.grpc.protobuf.services.ProtoReflectionService
import pes.worker.pipeline.ExecutionPipeline
import pes.worker.runner.ProcessSandboxRunner
import pes.worker.runner.RunnerClient
import pes.worker.runner.SimulationSandboxRunner
import pes.worker.service.ExecutionServiceImpl
import java.nio.file.Path

class WorkerServer(
    val port: Int,
    val runnerClient: RunnerClient
) {
    private val pipeline = ExecutionPipeline(runnerClient)
    private val service = ExecutionServiceImpl(pipeline)

    private val server: Server = ServerBuilder.forPort(port)
        .addService(service)
        .addService(ProtoReflectionService.newInstance())
        .build()

    fun start() {
        server.start()
        println("PolyExecution Studio Worker listening on port $port")
        Runtime.getRuntime().addShutdownHook(Thread {
            println("Shutting down worker gRPC server...")
            this@WorkerServer.stop()
            println("Worker server shut down cleanly.")
        })
    }

    fun stop() {
        server.shutdown()
    }

    fun blockUntilShutdown() {
        server.awaitTermination()
    }
}

fun main(args: Array<String>) {
    var port = System.getenv("PORT")?.toIntOrNull() ?: 50051
    var simulationMode = System.getenv("PES_WORKER_MODE")?.equals("simulation", ignoreCase = true) == true
    var customRunnerBin: String? = System.getenv("PES_RUNNER_BIN")

    var i = 0
    while (i < args.size) {
        when (args[i]) {
            "--port", "-p" -> {
                if (i + 1 < args.size) port = args[++i].toInt()
            }
            "--simulation", "-s" -> {
                simulationMode = true
            }
            "--runner", "-r" -> {
                if (i + 1 < args.size) customRunnerBin = args[++i]
            }
        }
        i++
    }

    val runnerClient: RunnerClient = if (simulationMode) {
        println("Worker starting in SIMULATION mode (host processes directly).")
        SimulationSandboxRunner()
    } else {
        val runnerPath = if (!customRunnerBin.isNullOrBlank()) {
            Path.of(customRunnerBin)
        } else {
            ProcessSandboxRunner.resolveDefaultRunnerBinary()
        }
        println("Worker starting in PRODUCTION mode using runner: $runnerPath")
        ProcessSandboxRunner(runnerPath)
    }

    val server = WorkerServer(port, runnerClient)
    server.start()
    server.blockUntilShutdown()
}
