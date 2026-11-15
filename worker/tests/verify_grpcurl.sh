#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-50055}"
MODE="${2:-simulation}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${REPO_ROOT}/worker/build/install/pes-worker/bin/pes-worker"

if [ ! -x "$BIN" ]; then
    echo "Error: pes-worker binary not found at $BIN. Run 'gradle installDist' first."
    exit 1
fi

echo "=========================================================="
echo "Starting PES Worker gRPC verification (port: $PORT, mode: $MODE)"
echo "=========================================================="

if [ "$MODE" = "simulation" ]; then
    "$BIN" --port "$PORT" --simulation > /tmp/pes-worker-verify.log 2>&1 &
else
    "$BIN" --port "$PORT" --runner "${REPO_ROOT}/sandbox-daemon/sandbox-runner" > /tmp/pes-worker-verify.log 2>&1 &
fi
SERVER_PID=$!

cleanup() {
    echo "Stopping worker process (PID: $SERVER_PID)..."
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Wait up to 10 seconds for server to listen
for i in {1..20}; do
    if grpcurl -plaintext "127.0.0.1:$PORT" list >/dev/null 2>&1; then
        echo "Worker server is ready and responding to gRPC reflection!"
        break
    fi
    sleep 0.5
done

echo ""
echo "--- 1. Service Discovery via gRPC Reflection ---"
grpcurl -plaintext "127.0.0.1:$PORT" list
echo ""
echo "--- 2. Describe ExecutionService ---"
grpcurl -plaintext "127.0.0.1:$PORT" describe pes.execution.v1.ExecutionService

if [ "$MODE" = "production" ]; then
    echo ""
    echo "--- 3. Production Mode Fail-Closed Verification against sandbox-runner ---"
    grpcurl -plaintext -d '{
        "job_id": "test-c-prod",
        "language": "LANGUAGE_C",
        "source_code": "#include <stdio.h>\nint main(){printf(\"hello\\n\");return 0;}"
    }' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute
    exit 0
fi

echo ""
echo "--- 3. C Valid Execution ---"
grpcurl -plaintext -d '{
    "job_id": "test-c-valid",
    "language": "LANGUAGE_C",
    "source_code": "#include <stdio.h>\nint main(){printf(\"PES C Execution Success!\\n\");return 0;}"
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute

echo ""
echo "--- 4. C Compile Error ---"
grpcurl -plaintext -d '{
    "job_id": "test-c-compile-err",
    "language": "LANGUAGE_C",
    "source_code": "int main() { syntax error here }"
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute

echo ""
echo "--- 5. C Runtime Error (non-zero exit) ---"
grpcurl -plaintext -d '{
    "job_id": "test-c-runtime-err",
    "language": "LANGUAGE_C",
    "source_code": "#include <stdlib.h>\nint main(){exit(7);}"
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute

echo ""
echo "--- 6. C Timeout ---"
grpcurl -plaintext -d '{
    "job_id": "test-c-timeout",
    "language": "LANGUAGE_C",
    "source_code": "int main(){while(1){}}",
    "limits": {"wall_ms": 1000, "cpu_ms": 1000}
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute

echo ""
echo "--- 7. Python Valid Execution ---"
grpcurl -plaintext -d '{
    "job_id": "test-py-valid",
    "language": "LANGUAGE_PYTHON",
    "source_code": "print(\"PES Python Execution Success!\")"
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute

echo ""
echo "--- 8. Python Runtime Error ---"
grpcurl -plaintext -d '{
    "job_id": "test-py-runtime-err",
    "language": "LANGUAGE_PYTHON",
    "source_code": "raise RuntimeError(\"Intentional python exception\")"
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute

echo ""
echo "--- 9. Python Timeout ---"
grpcurl -plaintext -d '{
    "job_id": "test-py-timeout",
    "language": "LANGUAGE_PYTHON",
    "source_code": "import time; time.sleep(10)",
    "limits": {"wall_ms": 1000, "cpu_ms": 1000}
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/Execute

echo ""
echo "--- 10. Streaming RPC (ExecuteStream) ---"
grpcurl -plaintext -d '{
    "job_id": "test-stream-valid",
    "language": "LANGUAGE_PYTHON",
    "source_code": "print(\"Streaming output event\")"
}' "127.0.0.1:$PORT" pes.execution.v1.ExecutionService/ExecuteStream

echo ""
echo "All grpcurl verification scenarios completed successfully!"
