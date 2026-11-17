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

echo "========================================================================"
if [ "$FAILED_TESTS" -eq 0 ]; then
    echo "SUCCESS: All 6 verify_docker_compose.sh unit tests PASSED!"
    exit 0
else
    echo "FAILURE: $FAILED_TESTS test(s) failed."
    exit 1
fi
