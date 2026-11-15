#!/usr/bin/env bash
set -euo pipefail

GATEWAY_PORT="${1:-8080}"
WORKER_PORT="${2:-50051}"
CLIENT_PORT="${3:-5173}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORKER_BIN="${REPO_ROOT}/worker/build/install/pes-worker/bin/pes-worker"
GATEWAY_JAR="${REPO_ROOT}/gateway/build/libs/gateway-0.1.0.jar"
RUNNER="${REPO_ROOT}/sandbox-daemon/sandbox-runner"

WORKER_LOG="/tmp/pes-browser-worker.log"
GATEWAY_LOG="/tmp/pes-browser-gateway.log"
CLIENT_LOG="/tmp/pes-browser-client.log"

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

if [ ! -x "/usr/bin/chromium" ]; then
    echo "Error: /usr/bin/chromium not found."
    exit 1
fi

echo "=== PolyExecution Studio - Phase 5 Browser E2E Verification ==="
echo "Host Kernel: $(uname -s -r -v)"
echo "Chromium:    $(/usr/bin/chromium --version)"
echo "Gateway Port: $GATEWAY_PORT"
echo "Worker Port:  $WORKER_PORT"
echo "Client Port:  $CLIENT_PORT"

cleanup() {
    echo "Tearing down Phase 5 test processes..."
    if [ -n "${CLIENT_PID:-}" ]; then
        kill "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi
    if [ -n "${GATEWAY_PID:-}" ]; then
        kill "$GATEWAY_PID" 2>/dev/null || true
        wait "$GATEWAY_PID" 2>/dev/null || true
    fi
    if [ -n "${WORKER_PID:-}" ]; then
        kill "$WORKER_PID" 2>/dev/null || true
        wait "$WORKER_PID" 2>/dev/null || true
    fi
    echo "Teardown complete."
}
trap cleanup EXIT

# 1. Start Worker
echo "Starting Kotlin Worker on 127.0.0.1:$WORKER_PORT..."
"$WORKER_BIN" --port "$WORKER_PORT" --runner "$RUNNER" >"$WORKER_LOG" 2>&1 &
WORKER_PID=$!

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

# 2. Start Gateway
echo "Starting Java Gateway on 127.0.0.1:$GATEWAY_PORT..."
java -jar "$GATEWAY_JAR" \
    --server.address=127.0.0.1 \
    --server.port="$GATEWAY_PORT" \
    --gateway.worker-grpc-target="127.0.0.1:$WORKER_PORT" >"$GATEWAY_LOG" 2>&1 &
GATEWAY_PID=$!

for _ in {1..50}; do
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

# 3. Start Frontend Client (Vite Preview)
echo "Starting Vite preview on 127.0.0.1:$CLIENT_PORT..."
(cd "${REPO_ROOT}/client" && npx vite preview --host 127.0.0.1 --port "$CLIENT_PORT" --strictPort) >"$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!

for _ in {1..30}; do
    if curl -s "http://127.0.0.1:$CLIENT_PORT" | grep -q "PolyExecution Studio"; then
        break
    fi
    sleep 0.2
done

if ! curl -s "http://127.0.0.1:$CLIENT_PORT" | grep -q "PolyExecution Studio"; then
    echo "Error: Frontend preview failed to start within timeout. Log:"
    cat "$CLIENT_LOG"
    exit 1
fi
echo "Frontend preview is ready on 127.0.0.1:$CLIENT_PORT."

# 4. Run Playwright E2E Browser Suite
echo "Running Playwright browser test suite..."
(cd "${REPO_ROOT}/client" && npx playwright test)

echo "=== All Phase 5 Browser E2E verification checks completed successfully ==="
