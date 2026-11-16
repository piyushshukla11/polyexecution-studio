#!/usr/bin/env bash
set -euo pipefail

GATEWAY_PORT="${1:-8080}"
WORKER_PORT="${2:-50055}"
CONCURRENCY="${3:-50}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORKER_BIN="${REPO_ROOT}/worker/build/install/pes-worker/bin/pes-worker"
GATEWAY_JAR="${REPO_ROOT}/gateway/build/libs/gateway-0.1.0.jar"
RUNNER="${REPO_ROOT}/sandbox-daemon/sandbox-runner"
LOAD_CLIENT="${REPO_ROOT}/gateway/tests/LoadTest50Concurrent.java"

WORKER_LOG="/tmp/pes-load-worker.log"
GATEWAY_LOG="/tmp/pes-load-gateway.log"

if [ ! -x "$RUNNER" ]; then
    echo "Error: sandbox-runner not built at $RUNNER. Run 'make -C sandbox-daemon all' first."
    exit 1
fi

if [ ! -x "$WORKER_BIN" ]; then
    echo "Error: worker distribution missing at $WORKER_BIN. Run '(cd worker && gradle installDist)' first."
    exit 1
fi

if [ ! -f "$GATEWAY_JAR" ]; then
    echo "Error: gateway jar missing at $GATEWAY_JAR. Run '(cd gateway && gradle bootJar)' first."
    exit 1
fi

echo "========================================================================"
echo "PolyExecution Studio - Phase 7 50-Concurrent-Runs Load Benchmark"
echo "Host Kernel:      $(uname -s -r -v)"
echo "Available CPUs:   $(nproc)"
echo "Host Memory:      $(free -h | awk '/^Mem:/ {print $2 " total, " $7 " available"}')"
echo "Gateway Port:     $GATEWAY_PORT"
echo "Worker Port:      $WORKER_PORT"
echo "Target Load:      $CONCURRENCY concurrent executions"
echo "========================================================================"

# Track child PIDs for cleanup
WORKER_PID=""
GATEWAY_PID=""

cleanup() {
    echo -e "\n--- Tearing down load test processes ---"
    if [ -n "$GATEWAY_PID" ]; then
        kill "$GATEWAY_PID" 2>/dev/null || true
        wait "$GATEWAY_PID" 2>/dev/null || true
    fi
    if [ -n "$WORKER_PID" ]; then
        kill "$WORKER_PID" 2>/dev/null || true
        wait "$WORKER_PID" 2>/dev/null || true
    fi
    echo "Teardown complete."
}
trap cleanup EXIT

# 1. Start Kotlin Worker
echo "Starting Kotlin Worker on 127.0.0.1:$WORKER_PORT..."
"$WORKER_BIN" --port "$WORKER_PORT" --runner "$RUNNER" >"$WORKER_LOG" 2>&1 &
WORKER_PID=$!

# Wait for worker ready
for _ in {1..40}; do
    if grpcurl -plaintext "127.0.0.1:$WORKER_PORT" list >/dev/null 2>&1; then
        break
    fi
    sleep 0.2
done

if ! grpcurl -plaintext "127.0.0.1:$WORKER_PORT" list >/dev/null 2>&1; then
    echo "Error: Worker failed to start within timeout. Log:"
    cat "$WORKER_LOG"
    exit 1
fi
echo "Worker is ready on 127.0.0.1:$WORKER_PORT."

# 2. Start Java Gateway with global concurrency = 64 (above 50) and max-rooms = 200
echo "Starting Java Gateway on 127.0.0.1:$GATEWAY_PORT (maxGlobalConcurrentRuns=64)..."
java -jar "$GATEWAY_JAR" \
    --server.address=127.0.0.1 \
    --server.port="$GATEWAY_PORT" \
    --gateway.worker-grpc-target="127.0.0.1:$WORKER_PORT" \
    --gateway.max-global-concurrent-runs=64 \
    --gateway.max-rooms=200 >"$GATEWAY_LOG" 2>&1 &
GATEWAY_PID=$!

# Wait for gateway health check
for _ in {1..60}; do
    if curl -s "http://127.0.0.1:$GATEWAY_PORT/health" | grep -q '"status":"UP"'; then
        break
    fi
    sleep 0.2
done

if ! curl -s "http://127.0.0.1:$GATEWAY_PORT/health" | grep -q '"status":"UP"'; then
    echo "Error: Gateway failed to start within timeout. Log:"
    cat "$GATEWAY_LOG"
    exit 1
fi
echo "Gateway is ready on 127.0.0.1:$GATEWAY_PORT."

# 3. Check baseline cgroups and processes
echo -e "\n--- Pre-load Resource & Stray Process Check ---"
PRE_CGROUPS=$(find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null | wc -l)
PRE_RUNNERS=$(pgrep -c -x "sandbox-runner" || true)
echo "Active pes-* cgroups before test: $PRE_CGROUPS"
echo "Active sandbox-runner processes before test: $PRE_RUNNERS"

# 4. Execute the 50-client load benchmark
echo -e "\n--- Executing 50 Concurrent Runs Load Benchmark ---"
java "$LOAD_CLIENT" "ws://127.0.0.1:$GATEWAY_PORT/ws" "$CONCURRENCY"

# 5. Post-load verification: Host stability & cleanup
echo -e "\n--- Post-load Verification: Host Stability & Cleanup ---"
sleep 1
POST_CGROUPS=$(find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null | wc -l)
POST_RUNNERS=$(pgrep -c -x "sandbox-runner" || true)
ZOMBIES=$(ps -eo stat | grep -c '^Z' || true)

echo "Active pes-* cgroups after test: $POST_CGROUPS"
echo "Active sandbox-runner processes after test: $POST_RUNNERS"
echo "Zombie processes on host: $ZOMBIES"

if [ "$POST_CGROUPS" -gt "$PRE_CGROUPS" ]; then
    echo "FAIL: Leaked cgroups detected! ($POST_CGROUPS > $PRE_CGROUPS)"
    find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null || true
    exit 1
fi

if [ "$POST_RUNNERS" -gt 0 ]; then
    echo "FAIL: Lingering sandbox-runner processes detected!"
    ps aux | grep "[s]andbox-runner" || true
    exit 1
fi

echo -e "\nHost Memory after test: $(free -h | awk '/^Mem:/ {print $2 " total, " $7 " available"}')"
echo "SUCCESS: 50 concurrent runs executed without host instability; 0 stray processes; 0 orphan cgroups."
