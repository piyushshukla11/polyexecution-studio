package pes.worker

import io.grpc.Server
import io.grpc.netty.shaded.io.grpc.netty.NettyServerBuilder
import io.grpc.protobuf.services.ProtoReflectionService
import pes.worker.pipeline.ExecutionPipeline
import pes.worker.runner.ProcessSandboxRunner
import pes.worker.runner.RunnerClient
import pes.worker.runner.FakeRunnerClient
import pes.worker.service.ExecutionServiceImpl
import java.net.InetAddress
import java.net.InetSocketAddress
import java.nio.file.Path

class WorkerServer(
    val port: Int,
    val runnerClient: RunnerClient
) {
    private val pipeline = ExecutionPipeline(runnerClient)
    private val service = ExecutionServiceImpl(pipeline)

    private val server: Server = NettyServerBuilder.forAddress(InetSocketAddress(InetAddress.getByName("127.0.0.1"), port))
        .addService(service)
        .addService(ProtoReflectionService.newInstance())
        .build()

    fun start() {
        server.start()
        println("PolyExecution Studio Worker listening on loopback port $port")
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
    var fakeMode = System.getenv("PES_WORKER_MODE")?.equals("fake", ignoreCase = true) == true
    var customRunnerBin: String? = System.getenv("PES_RUNNER_BIN")

    var i = 0
    while (i < args.size) {
        when (args[i]) {
            "--port", "-p" -> {
                if (i + 1 < args.size) port = args[++i].toInt()
            }
            "--fake-runner" -> {
                fakeMode = true
            }
            "--runner", "-r" -> {
                if (i + 1 < args.size) customRunnerBin = args[++i]
            }
        }
        i++
    }

    if (System.getenv("PES_WORKER_MODE")?.equals("simulation", ignoreCase = true) == true || args.contains("--simulation")) {
        error("Simulation mode was removed because it executed submitted code on the host. Use --fake-runner only for protocol checks.")
    }

    val runnerClient: RunnerClient = if (fakeMode) {
        println("Worker starting with deterministic FAKE runner; submitted source will not execute.")
        FakeRunnerClient()
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
