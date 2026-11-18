# syntax=docker/dockerfile:1
# PolyExecution Studio - Worker & Sandbox Runner Dockerfile
# Multi-stage build producing Kotlin worker with native C sandbox-runner and toolchains

# Stage 1: Build native C sandbox-runner binary
FROM ubuntu:22.04 AS c-builder
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
    uidmap \
    procps \
    curl \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# The sandbox maps only UID/GID 65534 to parent namespace ID 1.
# The outer rootless Docker mapping exposes this single parent ID to the container.
RUN printf 'root:1:1\n' > /etc/subuid && \
    printf 'root:1:1\n' > /etc/subgid

# Install Kotlin compiler into /opt/kotlinc
ARG KOTLIN_VERSION=2.1.0
RUN curl -sSL "https://github.com/JetBrains/kotlin/releases/download/v${KOTLIN_VERSION}/kotlin-compiler-${KOTLIN_VERSION}.zip" -o /tmp/kotlinc.zip && \
    unzip -q /tmp/kotlinc.zip -d /opt && \
    rm /tmp/kotlinc.zip && \
    ln -s /opt/kotlinc/bin/kotlinc /usr/bin/kotlinc && \
    ln -s /opt/java/openjdk/bin/java /usr/bin/java && \
    ln -s /opt/java/openjdk/bin/javac /usr/bin/javac

WORKDIR /app

# Copy sandbox runner and evil test binaries
COPY --from=c-builder /src/sandbox-daemon/sandbox-runner /app/sandbox-daemon/sandbox-runner
RUN chmod 755 /app/sandbox-daemon/sandbox-runner

# Copy installed worker distribution
COPY --from=worker-builder /workspace/worker/build/install/pes-worker /app/worker

# Copy worker entrypoint script for cgroup subtree placement
COPY worker/entrypoint.sh /app/worker/bin/entrypoint.sh
RUN chmod 755 /app/worker/bin/entrypoint.sh

ENV PES_RUNNER_BIN=/app/sandbox-daemon/sandbox-runner
ENV PES_WORKER_HOST=0.0.0.0
ENV PES_JAVA_BIN=/opt/java/openjdk/bin/java
ENV PES_JAVAC_BIN=/opt/java/openjdk/bin/javac

EXPOSE 50051

ENTRYPOINT ["/app/worker/bin/entrypoint.sh", "--port", "50051", "--runner", "/app/sandbox-daemon/sandbox-runner", "--host", "0.0.0.0"]
