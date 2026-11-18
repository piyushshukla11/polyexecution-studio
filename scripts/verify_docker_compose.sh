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

# 3. Check baseline cgroups and processes
echo -e "\n[Stage 3] Checking baseline host resources..."
PRE_CGROUPS=$(find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null | wc -l)
PRE_RUNNERS=$(pgrep -c -x "sandbox-runner" || true)
echo "Active pes-* cgroups before test: $PRE_CGROUPS"
echo "Active sandbox-runner processes before test: $PRE_RUNNERS"

# Cleanup trap to guarantee docker compose down on exit
cleanup() {
    echo -e "\n--- Tearing down Docker Compose stack ---"
    docker compose down --remove-orphans || true
    echo "Compose teardown complete."
}
trap cleanup EXIT

# 4. Build and start services
echo -e "\n[Stage 4] Building and launching Docker Compose stack in background..."
docker compose up --build -d

echo -e "\n[Stage 5] Inspecting container status..."
docker compose ps

# 5. Wait for Gateway health check through Client reverse proxy on 127.0.0.1:5173
echo -e "\n[Stage 6] Awaiting health check through Client reverse proxy (http://127.0.0.1:5173/health)..."
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

# 6. Execute live smoke test suite against Client reverse proxy (port 5173)
echo -e "\n[Stage 7] Executing live WebSocket smoke suite through Client (ws://127.0.0.1:5173/ws)..."
java gateway/tests/E2eWebSocketClient.java "ws://127.0.0.1:5173/ws"

# 7. Post-run verification
echo -e "\n[Stage 8] Post-run Host Stability & Cleanup Check..."
POST_CGROUPS=$(find /sys/fs/cgroup -maxdepth 2 -name "pes-*" 2>/dev/null | wc -l)
POST_RUNNERS=$(pgrep -c -x "sandbox-runner" || true)

echo "Active pes-* cgroups after test: $POST_CGROUPS"
echo "Active sandbox-runner processes after test: $POST_RUNNERS"

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

echo -e "\nSUCCESS: Docker Compose stack verified through client reverse proxy (port 5173); all runtimes passed; 0 leaked cgroups; 0 stray processes."
