#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-50055}"
MODE="${2:-fake}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${REPO_ROOT}/worker/build/install/pes-worker/bin/pes-worker"
LOG="/tmp/pes-worker-verify.log"

if [ ! -x "$BIN" ]; then
    echo "Error: worker distribution missing at $BIN. Run 'gradle installDist' first."
    exit 1
fi

case "$MODE" in
    fake)
        echo "Starting LOOPBACK-ONLY fake-runner gRPC protocol checks on port $PORT"
        "$BIN" --port "$PORT" --fake-runner >"$LOG" 2>&1 &
        ;;
    production)
        echo "Starting LOOPBACK-ONLY production-runner fail-closed check on port $PORT"
        "$BIN" --port "$PORT" --runner "${REPO_ROOT}/sandbox-daemon/sandbox-runner" >"$LOG" 2>&1 &
        ;;
    production-runtime)
        echo "Starting LOOPBACK-ONLY real production sandbox checks on port $PORT"
        "$BIN" --port "$PORT" --runner "${REPO_ROOT}/sandbox-daemon/sandbox-runner" >"$LOG" 2>&1 &
        ;;
    *)
        echo "Usage: $0 [port] [fake|production|production-runtime]"
        exit 2
        ;;
esac
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

for _ in {1..20}; do
    if grpcurl -plaintext "127.0.0.1:$PORT" list >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done

echo "--- Loopback service discovery ---"
grpcurl -plaintext "127.0.0.1:$PORT" list
grpcurl -plaintext "127.0.0.1:$PORT" describe pes.execution.v1.ExecutionService

if [ "$MODE" = "fake" ]; then
    echo "--- FAKE RUNNER protocol check: C request/response only; no compilation or execution ---"
    c_response="$(grpcurl -plaintext -d '{
        "job_id": "fake-c-protocol",
        "language": "LANGUAGE_C",
        "source_code": "int main(void) { return 0; }"
    }' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute)"
    printf '%s\n' "$c_response"
    grep -q 'FAKE_RUNNER: protocol response only' <<<"$c_response"

    echo "--- FAKE RUNNER protocol check: Python request/response only; no interpretation ---"
    py_response="$(grpcurl -plaintext -d '{
        "job_id": "fake-python-protocol",
        "language": "LANGUAGE_PYTHON",
        "source_code": "print(1)"
    }' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute)"
    printf '%s\n' "$py_response"
    grep -q 'FAKE_RUNNER: protocol response only' <<<"$py_response"

    echo "--- FAKE RUNNER server-streaming RPC contract: one final response ---"
    stream_response="$(grpcurl -plaintext -d '{
        "job_id": "fake-stream-protocol",
        "language": "LANGUAGE_PYTHON",
        "source_code": "print(1)"
    }' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/ExecuteStream)"
    printf '%s\n' "$stream_response"
    grep -q 'FAKE_RUNNER: protocol response only' <<<"$stream_response"
    echo "Fake-runner protocol checks completed. These are not execution evidence."
    exit 0
fi

if [ "$MODE" = "production-runtime" ]; then
    echo "--- REAL SANDBOX C compile/run with bounded stdin ---"
    c_response="$(grpcurl -plaintext -d '{
        "job_id": "production-c-stdin",
        "language": "LANGUAGE_C",
        "source_code": "#include <stdio.h>\nint main(void) { char line[128]; if (!fgets(line, sizeof(line), stdin)) return 3; fputs(line, stdout); return 0; }",
        "stdin": "sandboxed c input\n"
    }' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute)"
    printf '%s\n' "$c_response"
    grep -q '"status": "OK"' <<<"$c_response"
    grep -q 'sandboxed c input' <<<"$c_response"

    echo "--- REAL SANDBOX Python run with bounded stdin ---"
    py_response="$(grpcurl -plaintext -d '{
        "job_id": "production-python-stdin",
        "language": "LANGUAGE_PYTHON",
        "source_code": "print(input())",
        "stdin": "sandboxed python input\n"
    }' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute)"
    printf '%s\n' "$py_response"
    grep -q '"status": "OK"' <<<"$py_response"
    grep -q 'sandboxed python input' <<<"$py_response"

    echo "--- REAL SANDBOX compiler error stops before C execution ---"
    compile_error="$(grpcurl -plaintext -d '{
        "job_id": "production-c-compile-error",
        "language": "LANGUAGE_C",
        "source_code": "int main(void) { syntax error }"
    }' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute)"
    printf '%s\n' "$compile_error"
    grep -q '"status": "COMPILE_ERROR"' <<<"$compile_error"
    echo "Real sandbox C compile/run and Python checks passed. Preserve responses and runner logs as runtime evidence."
    exit 0
fi

echo "--- PRODUCTION sandbox setup check: expected to fail closed if kernel setup is unavailable ---"
prod_response="$(grpcurl -plaintext -d '{
    "job_id": "production-fail-closed",
    "language": "LANGUAGE_C",
    "source_code": "int main(void) { return 0; }"
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute)"
printf '%s\n' "$prod_response"
grep -q 'SANDBOX_ERROR' <<<"$prod_response"
grep -q 'setup error:' <<<"$prod_response"
echo "Observed production fail-closed setup error. No C/Python execution evidence was produced."
