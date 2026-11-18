#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "========================================================================"
echo "PolyExecution Studio - Docker Compose Stack Verification"
echo "Host Kernel:      $(uname -s -r -v)"
echo "Available CPUs:   $(nproc)"
echo "Host Memory:      $(free -h | awk '/^Mem:/ {print $2 " total, " $7 " available"}')"
echo "Compose Version:  $(docker compose version 2>/dev/null || echo 'not installed')"
echo "========================================================================"

# Track stack state for truthful teardown
STACK_STARTED=0
TEARDOWN_DONE=0

# 1. Check Docker daemon access
echo -e "\n[Stage 1] Verifying Docker daemon accessibility..."
if ! docker info >/dev/null 2>&1; then
    echo "ERROR: Docker daemon is not accessible in this environment."
    echo "Detail: /var/run/docker.sock is absent or current user lacks docker group permissions."
    echo "To run this test, execute on a host with an active Docker daemon."
    exit 1
fi
echo "Docker daemon is accessible."

# 2. Verify Compose configuration syntax
echo -e "\n[Stage 2] Validating docker compose configuration..."
docker compose config
echo "Compose configuration syntax is valid."

# 3. Inspect project container state (refuse execution if pre-existing containers exist)
echo -e "\n[Stage 3] Inspecting project container state..."
PRE_EXISTING=$(docker compose ps -aq 2>/dev/null | wc -l | tr -d " ")
if [ "$PRE_EXISTING" -gt 0 ]; then
    echo "ERROR: Pre-existing containers detected for this Docker Compose project ($PRE_EXISTING container(s) found)."
    echo "Detail: Running or stopped project containers are already present:"
    docker compose ps -a
    echo "Refusing to proceed to prevent unintended termination or deletion of pre-existing containers."
    echo "Please stop/remove existing containers manually before running this verification script."
    exit 1
fi
echo "No pre-existing project containers found."

# 4. Check baseline host resources before stack creation
echo -e "\n[Stage 4] Recording baseline host resources..."
PRE_CGROUPS=$(find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null | wc -l | tr -d " ")
PRE_RUNNERS=$(pgrep -x "sandbox-runner" 2>/dev/null | wc -l | tr -d " ")
PRE_CONTAINERS=0
echo "Active pes-* cgroups before test: $PRE_CGROUPS"
echo "Active sandbox-runner processes before test: $PRE_RUNNERS"
echo "Baseline project containers before test: $PRE_CONTAINERS"

# Verification function to run after teardown
verify_post_teardown_cleanup() {
    local post_containers post_cgroups post_runners leak_detected=0
    post_containers=$(docker compose ps -q 2>/dev/null | wc -l | tr -d " ")
    post_cgroups=$(find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null | wc -l | tr -d " ")
    post_runners=$(pgrep -x "sandbox-runner" 2>/dev/null | wc -l | tr -d " ")

    echo -e "\n--- Post-Teardown Resource & Container Cleanup Check ---"
    echo "Remaining project containers: $post_containers (baseline: $PRE_CONTAINERS)"
    echo "Active pes-* cgroups: $post_cgroups (baseline: $PRE_CGROUPS)"
    echo "Active sandbox-runner processes: $post_runners (baseline: $PRE_RUNNERS)"

    if [ "$post_containers" -gt "$PRE_CONTAINERS" ]; then
        echo "FAIL: Lingering project containers detected after teardown!"
        docker compose ps
        leak_detected=1
    fi

    if [ "$post_cgroups" -gt "$PRE_CGROUPS" ]; then
        echo "FAIL: Leaked pes-* cgroups detected! ($post_cgroups > $PRE_CGROUPS)"
        find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null || true
        leak_detected=1
    fi

    if [ "$post_runners" -gt "$PRE_RUNNERS" ]; then
        echo "FAIL: Lingering sandbox-runner processes detected! ($post_runners > $PRE_RUNNERS)"
        ps aux | grep "[s]andbox-runner" || true
        leak_detected=1
    fi

    if [ "$leak_detected" -ne 0 ]; then
        return 1
    fi
    echo "Cleanup check passed: 0 lingering containers, 0 leaked cgroups, 0 stray runners."
    return 0
}

cleanup_stack() {
    local orig_exit=$1
    local teardown_failed=0

    if [ "$STACK_STARTED" -eq 1 ] && [ "$TEARDOWN_DONE" -eq 0 ]; then
        TEARDOWN_DONE=1
        echo -e "\n--- Tearing down Docker Compose stack (docker compose down --remove-orphans) ---"
        if ! docker compose down --remove-orphans; then
            echo "FAIL: docker compose down --remove-orphans failed!"
            teardown_failed=1
        else
            echo "Compose teardown command completed successfully."
        fi

        if ! verify_post_teardown_cleanup; then
            echo "FAIL: Post-teardown resource leak or lingering container check failed!"
            teardown_failed=1
        fi
    fi

    if [ "$orig_exit" -ne 0 ]; then
        exit "$orig_exit"
    elif [ "$teardown_failed" -ne 0 ]; then
        exit 1
    fi
}

on_int() {
    echo -e "\n[SIGINT] Interrupted by user (Ctrl+C). Initiating cleanup..."
    cleanup_stack 130
    exit 130
}

on_term() {
    echo -e "\n[SIGTERM] Terminated by signal. Initiating cleanup..."
    cleanup_stack 143
    exit 143
}

on_exit() {
    local orig_exit=$?
    cleanup_stack "$orig_exit"
}

trap on_int INT
trap on_term TERM
trap on_exit EXIT

# 5. Build and start services
echo -e "\n[Stage 5] Building and launching Docker Compose stack in background..."
STACK_STARTED=1
docker compose up --build -d

echo -e "\n[Stage 6] Inspecting container status..."
docker compose ps

# 6. Wait for Gateway health check through Client reverse proxy on 127.0.0.1:5173
echo -e "\n[Stage 7] Awaiting health check through Client reverse proxy (http://127.0.0.1:5173/health)..."
for i in {1..60}; do
    if curl -s "http://127.0.0.1:5173/health" | grep -q '"status":"UP"'; then
        echo "Services are UP and healthy through Client reverse proxy (port 5173)!"
        break
    fi
    if [ "$i" -eq 60 ]; then
        echo "Error: Timed out waiting for Gateway health via port 5173. Service logs:"
        docker compose logs
        exit 1
    fi
    sleep 1
done

# 7. Execute live smoke test suite against Client reverse proxy (port 5173)
echo -e "\n[Stage 8] Executing live WebSocket smoke suite through Client (ws://127.0.0.1:5173/ws)..."
java gateway/tests/E2eWebSocketClient.java "ws://127.0.0.1:5173/ws"

# 8. Explicit teardown and post-teardown verification
echo -e "\n[Stage 9] Tearing down Docker Compose stack and verifying cleanup..."
cleanup_stack 0

echo -e "\nSUCCESS: Docker Compose stack verified end-to-end through client reverse proxy (port 5173); all runtimes passed; teardown verified clean."
