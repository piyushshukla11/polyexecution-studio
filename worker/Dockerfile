# syntax=docker/dockerfile:1
# PolyExecution Studio - Worker & Sandbox Runner Dockerfile
# Multi-stage build producing Kotlin worker with native C sandbox-runner and toolchains

# Stage 1: Build native C sandbox-runner binary
FROM ubuntu:24.04 AS c-builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libseccomp-dev \
    make \
    gcc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY sandbox-daemon /src/sandbox-daemon
RUN make -C /src/sandbox-daemon clean all

# Stage 2: Build Kotlin Worker distribution
FROM gradle:8.10.2-jdk21-jammy AS worker-builder
WORKDIR /workspace
COPY proto /workspace/proto
COPY worker /workspace/worker
WORKDIR /workspace/worker
RUN gradle installDist --no-daemon

# Stage 3: Runtime image
FROM eclipse-temurin:21-jdk-jammy
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    libc6-dev \
    python3 \
    nodejs \
    libseccomp2 \
    procps \
    curl \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Install Kotlin compiler into /opt/kotlinc
ARG KOTLIN_VERSION=2.1.0
RUN curl -sSL "https://github.com/JetBrains/kotlin/releases/download/v${KOTLIN_VERSION}/kotlin-compiler-${KOTLIN_VERSION}.zip" -o /tmp/kotlinc.zip && \
    unzip -q /tmp/kotlinc.zip -d /opt && \
    rm /tmp/kotlinc.zip && \
    ln -s /opt/kotlinc/bin/kotlinc /usr/bin/kotlinc

WORKDIR /app

# Copy sandbox runner and evil test binaries
COPY --from=c-builder /src/sandbox-daemon/sandbox-runner /app/sandbox-daemon/sandbox-runner
RUN chmod 755 /app/sandbox-daemon/sandbox-runner

# Copy installed worker distribution
COPY --from=worker-builder /workspace/worker/build/install/pes-worker /app/worker

ENV PES_RUNNER_BIN=/app/sandbox-daemon/sandbox-runner
ENV PES_WORKER_HOST=0.0.0.0

EXPOSE 50051

ENTRYPOINT ["/app/worker/bin/pes-worker", "--port", "50051", "--runner", "/app/sandbox-daemon/sandbox-runner", "--host", "0.0.0.0"]
