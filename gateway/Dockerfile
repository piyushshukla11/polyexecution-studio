# syntax=docker/dockerfile:1
# PolyExecution Studio - Gateway Dockerfile
# Multi-stage build producing minimal runnable Spring Boot service

# Stage 1: Build JAR using Gradle 8.10.2 on Eclipse Temurin JDK 21
FROM gradle:8.10.2-jdk21-jammy AS builder
WORKDIR /workspace

# Copy protobuf specifications and gateway source
COPY proto /workspace/proto
COPY gateway /workspace/gateway

WORKDIR /workspace/gateway
RUN gradle bootJar --no-daemon

# Stage 2: Minimal JRE runtime
FROM eclipse-temurin:21-jre-jammy
WORKDIR /app

# Non-root unprivileged service user
RUN groupadd -g 1001 pes && useradd -u 1001 -g pes -s /bin/bash -m pes
USER pes:pes

COPY --from=builder --chown=pes:pes /workspace/gateway/build/libs/gateway-0.1.0.jar /app/gateway.jar

ENV SERVER_ADDRESS=0.0.0.0 \
    SERVER_PORT=8080 \
    GATEWAY_WORKER_GRPC_TARGET=worker:50051 \
    GATEWAY_MAX_GLOBAL_CONCURRENT_RUNS=64

EXPOSE 8080

ENTRYPOINT ["java", "-jar", "/app/gateway.jar"]
