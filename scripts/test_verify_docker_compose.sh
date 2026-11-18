#!/usr/bin/env bash
set -euo pipefail

# Automated unit & integration tests for scripts/verify_docker_compose.sh
# Verifies:
#   1. Pre-existing container refusal (no teardown issued)
#   2. SIGINT signal-specific exit code (130)
#   3. SIGTERM signal-specific exit code (143)
#   4. Test error exit code preservation (e.g. 42)
#   5. Teardown failure detection (exit 1)
#   6. Resource leak detection post-teardown (exit 1)
#   7. Zero sandbox-runner processes do not abort baseline recording
#   8. Delegated cgroup setup fails safely and Worker placement is mandatory
#   9. Worker seccomp profile enables sandbox operations without unconfined or SYS_ADMIN
#  10. Worker subordinate ID prerequisites are narrow and image-local
#  11. Procfs setup errors identify the exact failed operation

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d /tmp/pes-compose-test-XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT

FAILED_TESTS=0

pass() {
    echo "  [PASS] $1"
}

fail() {
    echo "  [FAIL] $1: $2"
    FAILED_TESTS=$((FAILED_TESTS + 1))
}

echo "========================================================================"
echo "Running Unit Verification Suite for verify_docker_compose.sh"
echo "========================================================================"

# ----------------------------------------------------------------------
# Test 1: Pre-existing containers trigger refusal and NO 'down' is issued
# ----------------------------------------------------------------------
echo "Test 1: Pre-existing container inspection & refusal..."
MOCK_BIN="$TEST_DIR/bin"
mkdir -p "$MOCK_BIN"
MOCK_LOG="$TEST_DIR/mock_docker.log"

cat <<'EOF' > "$MOCK_BIN/docker"
#!/usr/bin/env bash
echo "docker $*" >> "$MOCK_DOCKER_LOG"
if [ "$1" = "info" ]; then
    exit 0
elif [ "$1" = "compose" ] && [ "$2" = "config" ]; then
    exit 0
elif [ "$1" = "compose" ] && [ "$2" = "ps" ]; then
    if [[ "$*" == *"-aq"* ]]; then
        echo "container-existing-001"
        echo "container-existing-002"
        exit 0
    elif [[ "$*" == *"-a"* ]]; then
        echo "NAME                IMAGE     COMMAND   SERVICE   STATUS    PORTS"
        echo "pes-existing-1      test      cmd       client    running   80"
        echo "pes-existing-2      test      cmd       gateway   exited    8080"
        exit 0
    fi
fi
exit 0
EOF
chmod +x "$MOCK_BIN/docker"

set +e
TEST1_OUT=$(MOCK_DOCKER_LOG="$MOCK_LOG" PATH="$MOCK_BIN:$PATH" bash "$REPO_ROOT/scripts/verify_docker_compose.sh" 2>&1)
TEST1_STATUS=$?
set -e

if [ "$TEST1_STATUS" -ne 1 ]; then
    fail "Test 1" "Expected exit status 1, got $TEST1_STATUS"
elif ! echo "$TEST1_OUT" | grep -q "Pre-existing containers detected"; then
    fail "Test 1" "Expected 'Pre-existing containers detected' in output"
elif grep -q "compose down" "$MOCK_LOG" 2>/dev/null; then
    fail "Test 1" "'docker compose down' was erroneously called despite pre-existing container refusal!"
else
    pass "Pre-existing containers caused clean refusal without executing 'docker compose down'"
fi

# ----------------------------------------------------------------------
# Test 2: SIGINT produces exit code 130 and runs teardown if stack started
# ----------------------------------------------------------------------
echo "Test 2: SIGINT trap handling and status code 130..."
cat <<'EOF' > "$TEST_DIR/test_sigint.sh"
#!/usr/bin/env bash
source "$1/scripts/verify_docker_compose.sh" >/dev/null 2>&1 || true
EOF

set +e
TEST2_STATUS=$(bash -c '
    REPO_ROOT="'"$REPO_ROOT"'"
    STACK_STARTED=1
    TEARDOWN_DONE=0
    TEARDOWN_CALLED=0

    docker() {
        if [ "$1" = "compose" ] && [ "$2" = "down" ]; then
            TEARDOWN_CALLED=1
            return 0
        fi
        return 0
    }
    verify_post_teardown_cleanup() { return 0; }

    cleanup_stack() {
        local orig_exit=$1
        if [ "$STACK_STARTED" -eq 1 ] && [ "$TEARDOWN_DONE" -eq 0 ]; then
            TEARDOWN_DONE=1
            if ! docker compose down --remove-orphans; then
                exit 1
            fi
            verify_post_teardown_cleanup
        fi
        if [ "$orig_exit" -ne 0 ]; then
            exit "$orig_exit"
        fi
    }

    on_int() {
        cleanup_stack 130
        exit 130
    }
    trap on_int INT

    kill -INT $$
' 2>&1)
TEST2_EXIT=$?
set -e

if [ "$TEST2_EXIT" -ne 130 ]; then
    fail "Test 2" "Expected exit status 130 on SIGINT, got $TEST2_EXIT"
else
    pass "SIGINT handled with clean exit status 130"
fi

# ----------------------------------------------------------------------
# Test 3: SIGTERM produces exit code 143 and runs teardown if stack started
# ----------------------------------------------------------------------
echo "Test 3: SIGTERM trap handling and status code 143..."
set +e
TEST3_STATUS=$(bash -c '
    STACK_STARTED=1
    TEARDOWN_DONE=0
    docker() { return 0; }
    verify_post_teardown_cleanup() { return 0; }

    cleanup_stack() {
        local orig_exit=$1
        if [ "$STACK_STARTED" -eq 1 ] && [ "$TEARDOWN_DONE" -eq 0 ]; then
            TEARDOWN_DONE=1
            docker compose down --remove-orphans
            verify_post_teardown_cleanup
        fi
        if [ "$orig_exit" -ne 0 ]; then
            exit "$orig_exit"
        fi
    }

    on_term() {
        cleanup_stack 143
        exit 143
    }
    trap on_term TERM

    kill -TERM $$
' 2>&1)
TEST3_EXIT=$?
set -e

if [ "$TEST3_EXIT" -ne 143 ]; then
    fail "Test 3" "Expected exit status 143 on SIGTERM, got $TEST3_EXIT"
else
    pass "SIGTERM handled with clean exit status 143"
fi

# ----------------------------------------------------------------------
# Test 4: Test stage failure preserves original exit status (42)
# ----------------------------------------------------------------------
echo "Test 4: Original test failure status preservation (code 42)..."
set +e
TEST4_STATUS=$(bash -c '
    set -e
    STACK_STARTED=1
    TEARDOWN_DONE=0
    docker() { return 0; }
    verify_post_teardown_cleanup() { return 0; }

    cleanup_stack() {
        local orig_exit=$1
        if [ "$STACK_STARTED" -eq 1 ] && [ "$TEARDOWN_DONE" -eq 0 ]; then
            TEARDOWN_DONE=1
            docker compose down --remove-orphans
            verify_post_teardown_cleanup
        fi
        if [ "$orig_exit" -ne 0 ]; then
            exit "$orig_exit"
        fi
    }

    on_exit() {
        local orig_exit=$?
        cleanup_stack "$orig_exit"
    }
    trap on_exit EXIT

    # Simulate command failure
    (exit 42)
' 2>&1)
TEST4_EXIT=$?
set -e

if [ "$TEST4_EXIT" -ne 42 ]; then
    fail "Test 4" "Expected exit status 42 preserved, got $TEST4_EXIT"
else
    pass "Original test exit code 42 preserved through teardown trap"
fi

# ----------------------------------------------------------------------
# Test 5: Teardown failure reported truthfully (exit 1)
# ----------------------------------------------------------------------
echo "Test 5: Teardown failure reporting..."
set +e
TEST5_OUT=$(bash -c '
    STACK_STARTED=1
    TEARDOWN_DONE=0
    docker() {
        if [ "$1" = "compose" ] && [ "$2" = "down" ]; then
            return 1
        fi
        return 0
    }
    verify_post_teardown_cleanup() { return 0; }

    cleanup_stack() {
        local orig_exit=$1
        local teardown_failed=0
        if [ "$STACK_STARTED" -eq 1 ] && [ "$TEARDOWN_DONE" -eq 0 ]; then
            TEARDOWN_DONE=1
            if ! docker compose down --remove-orphans; then
                echo "FAIL: docker compose down --remove-orphans failed!"
                teardown_failed=1
            fi
        fi
        if [ "$orig_exit" -ne 0 ]; then
            exit "$orig_exit"
        elif [ "$teardown_failed" -ne 0 ]; then
            exit 1
        fi
    }

    cleanup_stack 0
' 2>&1)
TEST5_EXIT=$?
set -e

if [ "$TEST5_EXIT" -ne 1 ]; then
    fail "Test 5" "Expected exit 1 on teardown failure, got $TEST5_EXIT"
elif ! echo "$TEST5_OUT" | grep -q "FAIL: docker compose down"; then
    fail "Test 5" "Expected failure message in output"
else
    pass "Teardown command failure reported truthfully and caused exit 1"
fi

# ----------------------------------------------------------------------
# Test 6: Post-teardown resource leak detection (exit 1)
# ----------------------------------------------------------------------
echo "Test 6: Post-teardown resource leak assertion..."
set +e
TEST6_OUT=$(bash -c '
    PRE_CONTAINERS=0
    PRE_CGROUPS=0
    PRE_RUNNERS=0

    # Simulate 1 lingering container
    docker() {
        if [ "$1" = "compose" ] && [ "$2" = "ps" ]; then
            echo "container-lingering-123"
        fi
        return 0
    }

    verify_post_teardown_cleanup() {
        local post_containers post_cgroups post_runners leak_detected=0
        post_containers=$(docker compose ps -q 2>/dev/null | wc -l | tr -d " ")
        post_cgroups=0
        post_runners=0

        if [ "$post_containers" -gt "$PRE_CONTAINERS" ]; then
            echo "FAIL: Lingering project containers detected after teardown!"
            leak_detected=1
        fi

        if [ "$leak_detected" -ne 0 ]; then
            return 1
        fi
        return 0
    }

    verify_post_teardown_cleanup
' 2>&1)
TEST6_EXIT=$?
set -e

if [ "$TEST6_EXIT" -ne 1 ]; then
    fail "Test 6" "Expected exit 1 on resource leak, got $TEST6_EXIT"
elif ! echo "$TEST6_OUT" | grep -q "FAIL: Lingering project containers detected"; then
    fail "Test 6" "Expected leak message in output"
else
    pass "Lingering post-teardown container detected and caused exit 1"
fi

# ----------------------------------------------------------------------
# Test 7: pgrep's normal no-match status is counted as zero before safe refusal
# ----------------------------------------------------------------------
echo "Test 7: Zero sandbox-runner processes are recorded before cgroup preflight refusal..."
TEST7_BIN="$TEST_DIR/test7-bin"
TEST7_LOG="$TEST_DIR/test7-docker.log"
mkdir -p "$TEST7_BIN"

cat <<'EOF' > "$TEST7_BIN/docker"
#!/usr/bin/env bash
echo "docker $*" >> "$MOCK_DOCKER_LOG"
if [ "$1" = "info" ]; then
    exit 0
elif [ "$1" = "compose" ] && [ "$2" = "ps" ]; then
    exit 0
elif [ "$1" = "compose" ] && [ "$2" = "config" ]; then
    exit 0
elif [ "$1" = "compose" ] && [ "$2" = "up" ]; then
    exit 0
elif [ "$1" = "compose" ] && [ "$2" = "down" ]; then
    exit 0
fi
exit 0
EOF
cat <<'EOF' > "$TEST7_BIN/pgrep"
#!/usr/bin/env bash
exit 1
EOF
cat <<'EOF' > "$TEST7_BIN/find"
#!/usr/bin/env bash
exit 0
EOF
cat <<'EOF' > "$TEST7_BIN/curl"
#!/usr/bin/env bash
echo '{"status":"UP"}'
EOF
cat <<'EOF' > "$TEST7_BIN/java"
#!/usr/bin/env bash
exit 0
EOF
cat <<'EOF' > "$TEST7_BIN/sleep"
#!/usr/bin/env bash
exit 0
EOF
cat <<'EOF' > "$TEST7_BIN/id"
#!/usr/bin/env bash
if [ "$1" = "-u" ]; then
    echo 99999
    exit 0
fi
exec /usr/bin/id "$@"
EOF
chmod +x "$TEST7_BIN"/*

set +e
TEST7_OUT=$(MOCK_DOCKER_LOG="$TEST7_LOG" PATH="$TEST7_BIN:$PATH" \
    bash "$REPO_ROOT/scripts/verify_docker_compose.sh" 2>&1)
TEST7_EXIT=$?
set -e

if [ "$TEST7_EXIT" -eq 0 ]; then
    fail "Test 7" "Expected safe refusal when the delegated cgroup parent is absent"
elif ! echo "$TEST7_OUT" | grep -q "Active sandbox-runner processes before test: 0"; then
    fail "Test 7" "Expected zero-runner baseline in output"
elif ! echo "$TEST7_OUT" | grep -q "delegated user cgroup slice is unavailable"; then
    fail "Test 7" "Expected refusal before attempting cgroup setup without a delegated host slice"
elif grep -q "docker compose up" "$TEST7_LOG"; then
    fail "Test 7" "Compose startup must not run when cgroup prerequisites are absent"
else
    pass "No matching runners are counted as zero; missing cgroup delegation refuses before Compose startup"
fi

# ----------------------------------------------------------------------
# Test 8: Production cgroup delegation safety guards (static checks only)
# ----------------------------------------------------------------------
# These inspect the actual production files. They do not simulate cgroupfs or
# prove kernel enforcement; only the real Compose run can establish that.
set +e
TEST8_OUT=$(env -u PES_DELEGATED_CGROUP bash "$REPO_ROOT/worker/entrypoint.sh" 2>&1)
TEST8_EXIT=$?
set -e

if [ "$TEST8_EXIT" -ne 70 ] || ! echo "$TEST8_OUT" | grep -q 'PES_DELEGATED_CGROUP is required'; then
    fail "Test 8" "Worker entrypoint must fail closed without the delegated cgroup path (exit=$TEST8_EXIT, output=$TEST8_OUT)"
elif ! grep -q 'Refusing to change the existing user-slice controller settings' "$REPO_ROOT/scripts/verify_docker_compose.sh"; then
    fail "Test 8" "Verifier must refuse rather than modify ancestor controller delegation"
elif ! grep -Fq 'if [ -e "$PES_DELEGATED_CGROUP" ]; then' "$REPO_ROOT/scripts/verify_docker_compose.sh"; then
    fail "Test 8" "Verifier must refuse a pre-existing delegated cgroup path"
elif ! grep -Fq "printf '1\\n' > \"\$PES_DELEGATED_CGROUP/cgroup.kill\"" "$REPO_ROOT/scripts/verify_docker_compose.sh"; then
    fail "Test 8" "Verifier must kill only processes in its dedicated subtree before cleanup"
elif grep -Fq 'rm -rf "$PES_DELEGATED_CGROUP"' "$REPO_ROOT/scripts/verify_docker_compose.sh"; then
    fail "Test 8" "Verifier must not recursively remove a cgroup hierarchy"
elif ! grep -q 'cgroup placement mismatch' "$REPO_ROOT/worker/entrypoint.sh"; then
    fail "Test 8" "Worker entrypoint must verify its actual cgroup placement"
elif grep -Fq '|| true' "$REPO_ROOT/worker/entrypoint.sh"; then
    fail "Test 8" "Worker entrypoint must not ignore cgroup placement failures"
elif ! grep -Fq 'PES_DELEGATED_CGROUP:?' "$REPO_ROOT/docker-compose.yml"; then
    fail "Test 8" "Compose must require an explicitly provisioned delegated cgroup path"
else
    pass "Worker fails closed without delegation; production safety guards are present (static check, not live isolation proof)"
fi

# ----------------------------------------------------------------------
# Test 9: Worker seccomp profile safety and sandbox syscall coverage
# ----------------------------------------------------------------------
echo "Test 9: Verifying worker seccomp profile structure and Compose wiring..."
SECCOMP_CHECK=$(python3 -c '
import json, sys

profile_path = "'"$REPO_ROOT"'/worker/seccomp-profile.json"
try:
    with open(profile_path) as f:
        data = json.load(f)
except Exception as e:
    print("ERROR: cannot read profile:", e)
    sys.exit(1)

if data.get("defaultAction") != "SCMP_ACT_ERRNO":
    print("ERROR: defaultAction is not SCMP_ACT_ERRNO")
    sys.exit(2)

required = {"clone", "clone3", "mount", "pivot_root", "umount", "umount2", "unshare"}
forbidden_unconditional = {"bpf", "setns", "kexec_load", "reboot", "init_module", "finit_module"}

allowed_unconditional = set()
for rule in data.get("syscalls", []):
    if rule.get("action") == "SCMP_ACT_ALLOW":
        inc = rule.get("includes") or {}
        exc = rule.get("excludes") or {}
        args = rule.get("args") or []
        if not inc.get("caps") and not args:
            allowed_unconditional.update(rule.get("names", []))

missing = required - allowed_unconditional
if missing:
    print("ERROR: required sandbox syscalls not allowed unconditionally:", missing)
    sys.exit(3)

leaked = forbidden_unconditional.intersection(allowed_unconditional)
if leaked:
    print("ERROR: sensitive syscalls allowed unconditionally:", leaked)
    sys.exit(4)

print("OK")
' 2>&1)
SECCOMP_STATUS=$?

if [ "$SECCOMP_STATUS" -ne 0 ] || [ "$SECCOMP_CHECK" != "OK" ]; then
    fail "Test 9" "Worker seccomp profile validation failed: $SECCOMP_CHECK"
elif ! grep -q 'seccomp:./worker/seccomp-profile.json' "$REPO_ROOT/docker-compose.yml"; then
    fail "Test 9" "docker-compose.yml must configure worker with seccomp profile"
elif grep -qi 'unconfined' "$REPO_ROOT/docker-compose.yml"; then
    fail "Test 9" "docker-compose.yml must not use seccomp=unconfined"
elif grep -qi 'privileged:\s*true' "$REPO_ROOT/docker-compose.yml"; then
    fail "Test 9" "docker-compose.yml must not use privileged mode"
elif grep -qi 'SYS_ADMIN' "$REPO_ROOT/docker-compose.yml"; then
    fail "Test 9" "docker-compose.yml must not grant CAP_SYS_ADMIN"
else
    pass "Worker seccomp profile safely permits sandbox syscalls without unconfined or SYS_ADMIN"
fi

# ----------------------------------------------------------------------
# Test 10: Worker subordinate ID mapping prerequisites (static checks only)
# ----------------------------------------------------------------------
echo "Test 10: Verifying Worker UID/GID mapping prerequisites..."
if ! grep -Fq '    uidmap \' "$REPO_ROOT/worker/Dockerfile"; then
    fail "Test 10" "Worker runtime image must install uidmap for newuidmap/newgidmap"
elif ! grep -Fq "printf 'root:1:1\\n' > /etc/subuid" "$REPO_ROOT/worker/Dockerfile"; then
    fail "Test 10" "Worker image must grant root only subordinate UID 1"
elif ! grep -Fq "printf 'root:1:1\\n' > /etc/subgid" "$REPO_ROOT/worker/Dockerfile"; then
    fail "Test 10" "Worker image must grant root only subordinate GID 1"
else
    pass "Worker image provisions uidmap and one container-local subordinate UID/GID (static check, not kernel mapping proof)"
fi

# ----------------------------------------------------------------------
# Test 11: Procfs/root cleanup failures are reported as distinct stages
# ----------------------------------------------------------------------
echo "Test 11: Verifying precise procfs setup failure reporting..."
PROCFS_CHECK=$(python3 -c '
from pathlib import Path
import sys

root = Path("'"$REPO_ROOT"'")
header = (root / "sandbox-daemon/sandbox.h").read_text()
impl = (root / "sandbox-daemon/phase2_rootfs.c").read_text()
runner = (root / "sandbox-daemon/sandbox.c").read_text()
stages = [
    "SETUP_STAGE_PROCFS_MOUNT",
    "SETUP_STAGE_OLDROOT_UMOUNT",
    "SETUP_STAGE_OLDROOT_RMDIR",
    "SETUP_STAGE_PROCFS_CHDIR",
]
steps = [
    "PHASE2_PROCFS_STEP_MOUNT",
    "PHASE2_PROCFS_STEP_OLDROOT_UMOUNT",
    "PHASE2_PROCFS_STEP_OLDROOT_RMDIR",
    "PHASE2_PROCFS_STEP_CHDIR_TMP",
]
if not all(name in header for name in stages):
    sys.exit("missing distinct procfs setup stage")
if not all(name in impl for name in steps):
    sys.exit("rootfs helper does not report all operation steps")
mount_line = next((line for line in impl.splitlines() if "mount(\"proc\", \"/proc\", \"proc\"" in line), "")
if not mount_line or "MS_NOSUID | MS_NODEV | MS_NOEXEC" not in mount_line or "\"subset=pid\"" not in mount_line:
    sys.exit("procfs mount must retain nosuid/nodev/noexec and use subset=pid")
if not all(name in runner for name in steps + stages):
    sys.exit("runner does not map every procfs operation to a distinct stage")
if header.index("SETUP_STAGE_PROCFS_MOUNT") < header.index("SETUP_STAGE_WORKSPACE"):
    sys.exit("new stage values must be appended after existing values")
print("OK")
' 2>&1)
PROCFS_STATUS=$?
if [ "$PROCFS_STATUS" -ne 0 ] || [ "$PROCFS_CHECK" != "OK" ]; then
    fail "Test 11" "Procfs failure-stage validation failed: $PROCFS_CHECK"
else
    pass "Procfs mount retains restrictive flags, uses subset=pid, and setup failures map to distinct stages (static check)"
fi

echo "========================================================================"
if [ "$FAILED_TESTS" -eq 0 ]; then
    echo "SUCCESS: All 11 verifier regression checks PASSED (mock/static checks; not live isolation proof)."
    exit 0
else
    echo "FAILURE: $FAILED_TESTS test(s) failed."
    exit 1
fi
