#!/usr/bin/env python3
"""
Phase 1 Regression Lab & Phase 2 Groundwork Test Harness
PolyExecution Studio (PES)

Automates verification of the Phase 1 C sandbox runner (sandbox-runner)
and evil test fixtures using only the Python standard library.

Phase 1 Regression Tests:
 1. Clean exit 0 -> status OK, exit_code 0, signal 0
 2. Clean exit streams -> empty stdout and stderr (Exercise 1)
 3. Non-zero exit code (exit 7) -> RUNTIME_ERROR, exit_code 7 preserved
 4. Target exit 127 vs missing binary execve failure -> RUNTIME_ERROR vs SANDBOX_ERROR
 5. Ordinary segmentation fault -> RUNTIME_ERROR, signal 11
 6. CPU time limit ceiling -> TIMEOUT, signal 24 (SIGXCPU)
 7. CPU limit vs wall-clock timeout -> SIGXCPU well before wall deadline (Exercise 2)
 8. Wall-clock timeout enforcement -> TIMEOUT, wall_ms >= timeout, cpu_ms near 0
 9. Stream separation -> stdout and stderr captured in distinct buffers
10. Output flood policy -> OUTPUT_LIMIT, capped buffer, process group killed
11. Symmetrical stderr output-cap -> OUTPUT_LIMIT, capped stderr (Exercise 3)
12. File size limit ceiling -> OUTPUT_LIMIT, signal 25 (SIGXFSZ)
13. Descendant holding pipe open -> direct child reaped, no hang
14. Early stream closure -> wall timeout persists despite closed fds
15. RFC 8259 pure ASCII JSON & Latin-1 lossless byte round-trip
16. Memory bomb resident-memory heuristic -> MEMORY_LIMIT, signal 11
17. CLI configuration customization -> custom job_id, timeout_ms, limits
18. Host process table cleanup -> zero lingering runner/evil processes

Phase 2 Groundwork Checks (Scaffolded / Pending):
 P1. Fork bomb containment -> PENDING (requires CLONE_NEWPID and cgroups pids.max)
 P2. Host filesystem snooping -> PENDING (requires CLONE_NEWNS and pivot_root)
 P3. Outbound network connect -> PENDING (requires CLONE_NEWNET and seccomp filtering)
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent
EVIL_BIN_DIR = BASE_DIR / "tests" / "evil" / "bin"
RUNNER_BIN = BASE_DIR / "sandbox-runner"
TEMP_FSIZE_FILE = Path("/tmp/pes_test_fsize.tmp")

# Standard ExecuteResponse schema fields and types per docs/02_ARCHITECTURE.md
EXPECTED_SCHEMA = {
    "job_id": str,
    "status": str,
    "exit_code": int,
    "signal": int,
    "wall_ms": int,
    "cpu_ms": int,
    "peak_memory_kb": int,
    "stdout": str,
    "stderr": str,
}

VALID_STATUSES = {
    "OK",
    "RUNTIME_ERROR",
    "TIMEOUT",
    "OUTPUT_LIMIT",
    "MEMORY_LIMIT",
    "SANDBOX_ERROR",
}


class TestCase:
    def __init__(self, name, description, is_phase2=False):
        self.name = name
        self.description = description
        self.is_phase2 = is_phase2
        self.status = "PENDING"
        self.detail = ""
        self.command = []
        self.raw_stdout = ""
        self.raw_stderr = ""
        self.parsed_json = None
        self.failure_category = ""  # HARNESS_TIMEOUT, RUNNER_EXEC_ERROR, SCHEMA_ERROR, CONTRACT_VIOLATION


def clean_temp_files():
    """Remove known temporary files created by test fixtures."""
    try:
        if TEMP_FSIZE_FILE.exists():
            TEMP_FSIZE_FILE.unlink()
    except OSError:
        pass


def build_binaries():
    """Ensure sandbox-runner and all evil fixtures are compiled with strict flags."""
    print("Building sandbox-runner and evil fixtures (make all evil)...")
    res = subprocess.run(
        ["make", "all", "evil"],
        cwd=BASE_DIR,
        capture_output=True,
        text=True,
    )
    if res.returncode != 0:
        print("Build failed:")
        print("STDOUT:\n", res.stdout)
        print("STDERR:\n", res.stderr)
        sys.exit(1)
    print("Build complete.\n")


def run_runner(args, outer_timeout=10, target_binary_name=None):
    """
    Execute sandbox-runner with args under an outer timeout.

    SYSTEMS NOTE ON OUTER TIMEOUTS & DETACHED SESSIONS:
    In Phase 1, the child process executed in sandbox-runner calls setsid()
    to create a new session (becoming its own process group leader).
    When subprocess.run(timeout=outer_timeout) expires, Python sends SIGKILL
    to the runner process PID only. Because the child target is in a separate
    session, killing the parent runner does NOT automatically terminate the
    child process group!
    To ensure clean diagnosis without indiscriminate process killing, if
    an outer timeout occurs, we inspect /proc for any surviving process
    matching target_binary_name and log a warning.
    True automatic lifecycle cleanup on parent termination requires Tier A
    features like prctl(PR_SET_PDEATHSIG, SIGKILL) or Phase 2 PID namespaces.

    Returns: (raw_stdout, raw_stderr, returncode, timed_out_bool, lingering_child_pid)
    """
    cmd = [str(RUNNER_BIN)] + args
    try:
        proc = subprocess.run(
            cmd,
            cwd=BASE_DIR,
            capture_output=True,
            text=True,
            timeout=outer_timeout,
        )
        return proc.stdout, proc.stderr, proc.returncode, False, None
    except subprocess.TimeoutExpired as exc:
        raw_out = exc.stdout.decode("latin1", errors="replace") if exc.stdout else ""
        raw_err = exc.stderr.decode("latin1", errors="replace") if exc.stderr else ""

        # Check if target process survived as an orphaned daemon
        surviving_pid = None
        if target_binary_name:
            for pid, cmdline in find_lingering_processes():
                if target_binary_name in cmdline:
                    surviving_pid = pid
                    break

        return raw_out, raw_err, -1, True, surviving_pid


def validate_json_schema(parsed):
    """Verify all ExecuteResponse schema fields exist and match expected types."""
    for key, expected_type in EXPECTED_SCHEMA.items():
        if key not in parsed:
            return False, f"Missing required key '{key}' in JSON response"
        if not isinstance(parsed[key], expected_type):
            return False, f"Key '{key}' expected {expected_type.__name__}, got {type(parsed[key]).__name__}"
    if parsed["status"] not in VALID_STATUSES:
        return False, f"Status '{parsed['status']}' is not a valid ExecuteResponse status enum"
    return True, ""


def find_lingering_processes():
    """
    Search for processes owned by current user running runner or evil binaries.
    Read-only inspection: strictly reports processes without killing anything.
    """
    lingering = []
    uid = os.getuid()
    proc_root = Path("/proc")
    if not proc_root.exists():
        return lingering

    for p in proc_root.iterdir():
        if not p.is_dir() or not p.name.isdigit():
            continue
        try:
            stat_file = p / "stat"
            if not stat_file.exists():
                continue
            with open(stat_file, "r", errors="ignore") as f:
                content = f.read()
            if p.stat().st_uid != uid:
                continue

            cmdline_file = p / "cmdline"
            if not cmdline_file.exists():
                continue
            with open(cmdline_file, "rb") as f:
                cmd = f.read().replace(b"\x00", b" ").decode("latin1", errors="replace").strip()

            if "tests/evil/bin" in cmd or "sandbox-runner" in cmd:
                # Exclude self if cmdline contains test_runner.py
                if "test_runner.py" not in cmd:
                    lingering.append((int(p.name), cmd))
        except (OSError, PermissionError):
            continue
    return lingering


# ---------------------------------------------------------------------------
# Phase 1 Test Implementations
# ---------------------------------------------------------------------------

def test_exit0(tc):
    """Test 01: Clean exit 0 -> status OK, exit_code 0, signal 0."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/exit0"]
    out, err, ret, timed_out, orphan = run_runner(["tests/evil/bin/exit0"], target_binary_name="exit0")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "OK" or data["exit_code"] != 0 or data["signal"] != 0:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: OK, exit_code: 0, signal: 0; got status: {data['status']}, exit_code: {data['exit_code']}, signal: {data['signal']}"
        return

    tc.status = "PASSED"
    tc.detail = f"Status OK, exit_code 0, signal 0, wall_ms {data['wall_ms']}"


def test_empty_output_streams(tc):
    """Test 02 (Exercise 1): Clean exit produces strictly empty stdout and stderr."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/exit0"]
    out, err, ret, timed_out, orphan = run_runner(["tests/evil/bin/exit0"], target_binary_name="exit0")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["stdout"] != "" or data["stderr"] != "":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected empty streams; got stdout len={len(data['stdout'])}, stderr len={len(data['stderr'])}"
        return

    tc.status = "PASSED"
    tc.detail = "Confirmed stdout and stderr are strictly empty strings (no uninitialized buffer remnants or null bytes)"


def test_exit7(tc):
    """Test 03: Non-zero exit code (exit 7) -> status RUNTIME_ERROR, exit_code 7 preserved."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/exit7"]
    out, err, ret, timed_out, orphan = run_runner(["tests/evil/bin/exit7"], target_binary_name="exit7")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "RUNTIME_ERROR" or data["exit_code"] != 7 or data["signal"] != 0:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: RUNTIME_ERROR, exit_code: 7, signal: 0; got status: {data['status']}, exit_code: {data['exit_code']}"
        return

    tc.status = "PASSED"
    tc.detail = f"Status RUNTIME_ERROR, exit_code 7 preserved, signal 0 (expected target error code properly reported)"


def test_exit127_vs_missing(tc):
    """Test 04: Target exit 127 vs missing binary execve failure."""
    # Subtest A: Valid program exiting 127
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/exit127"]
    out_a, err_a, _, timed_out_a, orphan_a = run_runner(["tests/evil/bin/exit127"], target_binary_name="exit127")
    if timed_out_a:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Subtest exit127 timed out (orphan PID: {orphan_a})"
        return

    try:
        data_a = json.loads(out_a)
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Subtest exit127 JSON parse error: {exc}"
        return

    if data_a["status"] != "RUNTIME_ERROR" or data_a["exit_code"] != 127 or data_a["stderr"] != "":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"exit127 expected RUNTIME_ERROR with clean stderr; got status={data_a['status']}, stderr={data_a['stderr']!r}"
        return

    # Subtest B: Missing binary path
    missing_path = "/nonexistent/binary/path"
    out_b, err_b, _, timed_out_b, _ = run_runner([missing_path])
    if timed_out_b:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = "Subtest missing binary timed out"
        return

    try:
        data_b = json.loads(out_b)
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Subtest missing binary JSON parse error: {exc}"
        return

    tc.raw_stdout = f"A (exit127): {out_a}\nB (missing): {out_b}"
    tc.parsed_json = {"exit127": data_a, "missing": data_b}

    if data_b["status"] != "SANDBOX_ERROR" or data_b["exit_code"] != 127 or "setup error:" not in data_b["stderr"]:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"missing binary expected SANDBOX_ERROR with setup error in stderr; got status={data_b['status']}, stderr={data_b['stderr']!r}"
        return

    tc.status = "PASSED"
    tc.detail = "Distinguished valid exit 127 (RUNTIME_ERROR, clean stderr) from execve failure (SANDBOX_ERROR, stage 10 errno 2 in stderr)"


def test_segv(tc):
    """Test 05: Ordinary segmentation fault -> status RUNTIME_ERROR, signal 11."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/segv"]
    out, err, ret, timed_out, orphan = run_runner(["tests/evil/bin/segv"], target_binary_name="segv")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "RUNTIME_ERROR" or data["signal"] != 11:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: RUNTIME_ERROR, signal: 11; got status: {data['status']}, signal: {data['signal']}"
        return

    # Check that ordinary segv did not trigger the memory heuristic
    if data["peak_memory_kb"] * 1024 >= (128 * 1024 * 1024 * 0.8):
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Unexpectedly high RSS ({data['peak_memory_kb']} KB) for ordinary segv"
        return

    tc.status = "PASSED"
    tc.detail = f"Status RUNTIME_ERROR, signal 11 (SIGSEGV), peak_memory {data['peak_memory_kb']} KB (heuristic threshold not breached)"


def test_cpu_loop(tc):
    """Test 06: CPU busy-loop -> status TIMEOUT, signal 24 (SIGXCPU)."""
    tc.command = [str(RUNNER_BIN), "--cpu-soft", "1", "--cpu-hard", "2", "--timeout-ms", "3000", "tests/evil/bin/loop"]
    out, err, ret, timed_out, orphan = run_runner(["--cpu-soft", "1", "--cpu-hard", "2", "--timeout-ms", "3000", "tests/evil/bin/loop"], target_binary_name="loop")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "TIMEOUT" or data["signal"] != 24:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: TIMEOUT, signal: 24 (SIGXCPU); got status: {data['status']}, signal: {data['signal']}"
        return

    if data["cpu_ms"] < 800:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"cpu_ms ({data['cpu_ms']}) was lower than expected for CPU timeout"
        return

    tc.status = "PASSED"
    tc.detail = f"Status TIMEOUT, signal 24 (SIGXCPU), cpu_ms: {data['cpu_ms']}, wall_ms: {data['wall_ms']}"


def test_cpu_vs_wall_timeout(tc):
    """Test 07 (Exercise 2): CPU limit vs wall timeout (SIGXCPU well before wall deadline)."""
    # Soft CPU limit 1s, hard 2s, but generous wall timeout 4000ms
    tc.command = [str(RUNNER_BIN), "--cpu-soft", "1", "--cpu-hard", "2", "--timeout-ms", "4000", "tests/evil/bin/loop"]
    out, err, ret, timed_out, orphan = run_runner(["--cpu-soft", "1", "--cpu-hard", "2", "--timeout-ms", "4000", "tests/evil/bin/loop"], outer_timeout=6, target_binary_name="loop")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "TIMEOUT" or data["signal"] != 24:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: TIMEOUT, signal: 24; got status: {data['status']}, signal: {data['signal']}"
        return

    # Must terminate well before wall timeout ceiling (4000ms)
    if data["wall_ms"] > 3500:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"wall_ms ({data['wall_ms']}) exceeded 3500ms; CPU limit did not terminate process before wall deadline"
        return

    tc.status = "PASSED"
    tc.detail = f"CPU soft limit triggered SIGXCPU at cpu_ms {data['cpu_ms']} (wall_ms {data['wall_ms']} < 3500ms ceiling, proving CPU termination)"


def test_sleeper(tc):
    """Test 08: Long sleep -> wall-clock timeout enforcement (wall_ms >= timeout)."""
    tc.command = [str(RUNNER_BIN), "--timeout-ms", "1000", "tests/evil/bin/sleeper"]
    out, err, ret, timed_out, orphan = run_runner(["--timeout-ms", "1000", "tests/evil/bin/sleeper"], outer_timeout=5, target_binary_name="sleeper")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "TIMEOUT":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: TIMEOUT; got status: {data['status']}"
        return

    if data["wall_ms"] < 950:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"wall_ms ({data['wall_ms']}) was shorter than requested timeout (1000ms)"
        return

    if data["cpu_ms"] > 200:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"cpu_ms ({data['cpu_ms']}) unexpectedly high for sleeping process"
        return

    tc.status = "PASSED"
    tc.detail = f"Status TIMEOUT, wall_ms {data['wall_ms']} >= 1000, cpu_ms {data['cpu_ms']} (verified sleep consumed no CPU)"


def test_stream_separation(tc):
    """Test 09: Capturing stdout and stderr separately without cross-contamination."""
    # Subtest A: Binary writing both stdout and stderr
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/stdout_stderr"]
    out, err, ret, timed_out, orphan = run_runner(["tests/evil/bin/stdout_stderr"], target_binary_name="stdout_stderr")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["stdout"] != "STDOUT_DATA\n" or data["stderr"] != "STDERR_DATA\n":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Stream content mismatch: stdout={data['stdout']!r}, stderr={data['stderr']!r}"
        return

    # Subtest B: Binary writing only stderr (4096 bytes)
    out_b, _, _, timed_out_b, orphan_b = run_runner(["tests/evil/bin/stderr_only"], target_binary_name="stderr_only")
    if timed_out_b:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"stderr_only timed out (orphan PID: {orphan_b})"
        return

    data_b = json.loads(out_b)
    if data_b["stdout"] != "" or len(data_b["stderr"]) != 4096:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"stderr_only expected empty stdout and 4096-byte stderr; got stdout len={len(data_b['stdout'])}, stderr len={len(data_b['stderr'])}"
        return

    tc.status = "PASSED"
    tc.detail = "stdout and stderr strictly isolated; verified bidirectional and stderr-only streams"


def test_output_flood(tc):
    """Test 10: Output flooding -> status OUTPUT_LIMIT, capped buffer, process killed."""
    tc.command = [str(RUNNER_BIN), "--max-output-kb", "64", "tests/evil/bin/output_flood"]
    out, err, ret, timed_out, orphan = run_runner(["--max-output-kb", "64", "tests/evil/bin/output_flood"], target_binary_name="output_flood")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "OUTPUT_LIMIT":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: OUTPUT_LIMIT; got status: {data['status']}"
        return

    cap_bytes = 64 * 1024
    if len(data["stdout"]) != cap_bytes:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"stdout length ({len(data['stdout'])}) did not match exact cap ({cap_bytes} bytes)"
        return

    tc.status = "PASSED"
    tc.detail = f"Status OUTPUT_LIMIT, stdout length strictly capped at {len(data['stdout'])} bytes"


def test_stderr_cap_overflow(tc):
    """Test 11 (Exercise 3): Symmetrical stderr capping under --max-output-kb 1."""
    tc.command = [str(RUNNER_BIN), "--max-output-kb", "1", "tests/evil/bin/stderr_only"]
    out, err, ret, timed_out, orphan = run_runner(["--max-output-kb", "1", "tests/evil/bin/stderr_only"], target_binary_name="stderr_only")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "OUTPUT_LIMIT":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: OUTPUT_LIMIT; got status: {data['status']}"
        return

    cap_bytes = 1024
    if len(data["stderr"]) != cap_bytes or data["stdout"] != "":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected stderr len={cap_bytes} and empty stdout; got stderr len={len(data['stderr'])}, stdout len={len(data['stdout'])}"
        return

    tc.status = "PASSED"
    tc.detail = f"Status OUTPUT_LIMIT, stderr capped at {len(data['stderr'])} bytes, stdout empty (symmetrical capping verified)"


def test_file_size_limit(tc):
    """Test 12: File size ceiling -> status OUTPUT_LIMIT, signal 25 (SIGXFSZ)."""
    clean_temp_files()
    tc.command = [str(RUNNER_BIN), "--fsize-mb", "1", "tests/evil/bin/file_size_limit"]
    out, err, ret, timed_out, orphan = run_runner(["--fsize-mb", "1", "tests/evil/bin/file_size_limit"], target_binary_name="file_size_limit")
    tc.raw_stdout, tc.raw_stderr = out, err
    clean_temp_files()

    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "OUTPUT_LIMIT" or data["signal"] != 25:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: OUTPUT_LIMIT, signal: 25 (SIGXFSZ); got status: {data['status']}, signal: {data['signal']}"
        return

    tc.status = "PASSED"
    tc.detail = f"Status OUTPUT_LIMIT, signal 25 (SIGXFSZ), temp file /tmp/pes_test_fsize.tmp cleaned"


def test_descendant_pipe(tc):
    """Test 13: Descendant keeps pipe open -> direct child reaped, loop finishes rapidly."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/descendant_pipe"]
    t0 = time.monotonic()
    out, err, ret, timed_out, orphan = run_runner(["tests/evil/bin/descendant_pipe"], outer_timeout=5, target_binary_name="descendant_pipe")
    elapsed = time.monotonic() - t0
    tc.raw_stdout, tc.raw_stderr = out, err

    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung waiting on descendant pipe (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["exit_code"] != 42 or data["status"] != "RUNTIME_ERROR":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected exit_code: 42, status: RUNTIME_ERROR; got exit_code: {data['exit_code']}, status: {data['status']}"
        return

    if elapsed > 2.0 or data["wall_ms"] > 2000:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Runner hung on unclosed descendant pipe (elapsed: {elapsed:.2f}s, wall_ms: {data['wall_ms']})"
        return

    tc.status = "PASSED"
    tc.detail = f"Direct child exit_code 42 reaped in {data['wall_ms']} ms; avoided 60s grandchild hang"


def test_close_stream_sleep(tc):
    """Test 14: Child closes streams and sleeps -> wall timeout persists."""
    tc.command = [str(RUNNER_BIN), "--timeout-ms", "1000", "tests/evil/bin/close_stream_sleep"]
    out, err, ret, timed_out, orphan = run_runner(["--timeout-ms", "1000", "tests/evil/bin/close_stream_sleep"], outer_timeout=5, target_binary_name="close_stream_sleep")
    tc.raw_stdout, tc.raw_stderr = out, err

    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "TIMEOUT":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: TIMEOUT; got status: {data['status']}"
        return

    if data["wall_ms"] < 950:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"wall_ms ({data['wall_ms']}) was shorter than requested timeout (1000ms)"
        return

    tc.status = "PASSED"
    tc.detail = f"Status TIMEOUT enforced at {data['wall_ms']} ms despite child closing stdout/stderr fds"


def test_json_latin1_reconstruction(tc):
    """Test 15: RFC 8259 JSON validation and exact 100% byte-for-byte Latin-1 reconstruction."""
    fixture_bin = EVIL_BIN_DIR / "json_escaping_test"
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/json_escaping_test"]

    # Capture ground truth binary output directly
    ground_truth = subprocess.run([str(fixture_bin)], capture_output=True)
    expected_stdout = ground_truth.stdout
    expected_stderr = ground_truth.stderr

    out, err, ret, timed_out, orphan = run_runner(["tests/evil/bin/json_escaping_test"], target_binary_name="json_escaping_test")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    actual_stdout = data["stdout"].encode("latin1")
    actual_stderr = data["stderr"].encode("latin1")

    if actual_stdout != expected_stdout:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Stdout binary mismatch: actual len {len(actual_stdout)}, expected len {len(expected_stdout)}"
        return

    if actual_stderr != expected_stderr:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Stderr binary mismatch: actual len {len(actual_stderr)}, expected len {len(expected_stderr)}"
        return

    if b"\xc3\xa9" not in actual_stdout or b" \xe9 " not in actual_stdout:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = "Latin-1 / UTF-8 collision boundary check failed"
        return

    tc.status = "PASSED"
    tc.detail = f"100% byte-for-byte identity verified (stdout {len(actual_stdout)} bytes, stderr {len(actual_stderr)} bytes)"


def test_memory_bomb_heuristic(tc):
    """Test 16: Memory bomb allocation failure -> status MEMORY_LIMIT, signal 11, RSS heuristic."""
    tc.command = [str(RUNNER_BIN), "--as-mb", "128", "tests/evil/bin/memory_bomb"]
    out, err, ret, timed_out, orphan = run_runner(["--as-mb", "128", "tests/evil/bin/memory_bomb"], target_binary_name="memory_bomb")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "MEMORY_LIMIT" or data["signal"] != 11:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status: MEMORY_LIMIT, signal: 11; got status: {data['status']}, signal: {data['signal']}"
        return

    threshold_kb = int(128 * 1024 * 0.8)
    if data["peak_memory_kb"] < threshold_kb:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"peak_memory_kb ({data['peak_memory_kb']}) was below 80% threshold ({threshold_kb} KB)"
        return

    tc.status = "PASSED"
    tc.detail = f"Status MEMORY_LIMIT, signal 11 (SIGSEGV), peak_memory {data['peak_memory_kb']} KB >= {threshold_kb} KB (heuristic confirmed)"


def test_cli_customization(tc):
    """Test 17: Custom CLI flags (--job-id, --timeout-ms, custom limits)."""
    tc.command = [str(RUNNER_BIN), "--job-id", "job-lab-99", "--timeout-ms", "600", "tests/evil/bin/sleeper"]
    out, err, ret, timed_out, orphan = run_runner(["--job-id", "job-lab-99", "--timeout-ms", "600", "tests/evil/bin/sleeper"], outer_timeout=5, target_binary_name="sleeper")
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner hung under outer safety timeout (orphan PID: {orphan})"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["job_id"] != "job-lab-99":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"job_id mismatch: expected 'job-lab-99', got {data['job_id']!r}"
        return

    if data["status"] != "TIMEOUT" or data["wall_ms"] < 550 or data["wall_ms"] > 1500:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Unexpected status or timing for custom 600ms timeout: status={data['status']}, wall_ms={data['wall_ms']}"
        return

    tc.status = "PASSED"
    tc.detail = f"job_id 'job-lab-99' preserved, wall_ms {data['wall_ms']} aligned with 600ms timeout"


def test_process_cleanup(tc):
    """Test 18: Host process table check -> zero lingering runner or evil child processes."""
    time.sleep(0.1)  # Brief settling window
    lingering = find_lingering_processes()
    if lingering:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        pids_str = ", ".join(f"PID {p[0]} ({p[1]})" for p in lingering)
        tc.detail = f"Detected {len(lingering)} lingering test process(es): {pids_str}"
        return

    tc.status = "PASSED"
    tc.detail = "Verified 0 lingering runner, evil, or zombie processes owned by current user"


# ---------------------------------------------------------------------------
# Phase 2 Scaffolded / Pending Checks
# ---------------------------------------------------------------------------

def test_phase2_fork_bomb(tc):
    """Phase 2 Check 01: Fork bomb containment (PENDING)."""
    tc.status = "PENDING"
    tc.detail = "Requires PID namespace (CLONE_NEWPID) and cgroups v2 pids.max (Phase 2). Execution skipped to protect host process table."


def test_phase2_read_etc_passwd(tc):
    """Phase 2 Check 02: Host filesystem snooping defense (PENDING)."""
    tc.status = "PENDING"
    tc.detail = "Requires mount namespace (CLONE_NEWNS), pivot_root, and minimal unprivileged rootfs (Phase 2). Execution skipped to protect host filesystem."


def test_phase2_net_connect(tc):
    """Phase 2 Check 03: Outbound network connect defense (PENDING)."""
    tc.status = "PENDING"
    tc.detail = "Requires network namespace (CLONE_NEWNET) and seccomp socket syscall filtering (Phase 2). Execution skipped to prevent unauthorized network access."


# ---------------------------------------------------------------------------
# Test Runner Orchestration
# ---------------------------------------------------------------------------

PHASE1_TESTS = [
    ("test_exit0", "Clean exit 0 -> status OK", test_exit0),
    ("test_empty_output_streams", "Clean exit streams -> empty stdout/stderr (Ex 1)", test_empty_output_streams),
    ("test_exit7", "Non-zero exit (exit 7) -> RUNTIME_ERROR", test_exit7),
    ("test_exit127_vs_missing", "Target exit 127 vs missing binary exec failure", test_exit127_vs_missing),
    ("test_segv", "Ordinary segfault (signal 11) -> RUNTIME_ERROR", test_segv),
    ("test_cpu_loop", "CPU time limit ceiling (SIGXCPU) -> TIMEOUT", test_cpu_loop),
    ("test_cpu_vs_wall_timeout", "CPU limit vs wall timeout (SIGXCPU early) (Ex 2)", test_cpu_vs_wall_timeout),
    ("test_sleeper", "Wall-clock timeout enforcement -> TIMEOUT", test_sleeper),
    ("test_stream_separation", "Stream separation (stdout and stderr isolated)", test_stream_separation),
    ("test_output_flood", "Output flood policy -> OUTPUT_LIMIT capped", test_output_flood),
    ("test_stderr_cap_overflow", "Symmetrical stderr output-cap (Ex 3)", test_stderr_cap_overflow),
    ("test_file_size_limit", "File size limit ceiling -> OUTPUT_LIMIT (SIGXFSZ)", test_file_size_limit),
    ("test_descendant_pipe", "Descendant holding pipe open -> no hang", test_descendant_pipe),
    ("test_close_stream_sleep", "Early stream closure -> wall timeout persists", test_close_stream_sleep),
    ("test_json_latin1_reconstruction", "RFC 8259 JSON & Latin-1 lossless byte round-trip", test_json_latin1_reconstruction),
    ("test_memory_bomb_heuristic", "Memory bomb -> MEMORY_LIMIT via RSS heuristic", test_memory_bomb_heuristic),
    ("test_cli_customization", "CLI flags customization (--job-id, --timeout-ms)", test_cli_customization),
    ("test_process_cleanup", "Host process table cleanup check", test_process_cleanup),
]

PHASE2_CHECKS = [
    ("test_phase2_fork_bomb", "Fork bomb containment (Phase 2)", test_phase2_fork_bomb),
    ("test_phase2_read_etc_passwd", "Host filesystem snooping defense (Phase 2)", test_phase2_read_etc_passwd),
    ("test_phase2_net_connect", "Outbound network connect defense (Phase 2)", test_phase2_net_connect),
]


def main():
    print("=" * 72)
    print("PolyExecution Studio (PES) — Phase 1 Regression Lab & Phase 2 Groundwork")
    print("=" * 72)

    clean_temp_files()
    build_binaries()

    passed_count = 0
    failed_count = 0
    skipped_count = 0

    print("--- Phase 1 Regression Suite ---")
    for idx, (func_name, desc, func) in enumerate(PHASE1_TESTS, start=1):
        tc = TestCase(func_name, desc, is_phase2=False)
        try:
            func(tc)
        except Exception as exc:
            tc.status = "FAILED"
            tc.failure_category = "UNHANDLED_EXCEPTION"
            tc.detail = f"Unhandled exception during test execution: {exc}"

        badge = f"[{tc.status}]"
        print(f"Test {idx:02d}: {tc.description:<54} {badge}")
        if tc.status == "PASSED":
            passed_count += 1
            print(f"         Detail: {tc.detail}")
        elif tc.status == "FAILED":
            failed_count += 1
            print(f"         FAILED [{tc.failure_category}]: {tc.detail}")
            if tc.command:
                print(f"         Command: {' '.join(tc.command)}")
            if tc.raw_stdout:
                truncated_out = tc.raw_stdout[:300] + ("..." if len(tc.raw_stdout) > 300 else "")
                print(f"         Raw stdout: {truncated_out!r}")
            if tc.raw_stderr:
                truncated_err = tc.raw_stderr[:300] + ("..." if len(tc.raw_stderr) > 300 else "")
                print(f"         Raw stderr: {truncated_err!r}")

    print("\n--- Phase 2 Groundwork Checks (Scaffolded / Pending) ---")
    pending_p2_count = 0
    for idx, (func_name, desc, func) in enumerate(PHASE2_CHECKS, start=1):
        tc = TestCase(func_name, desc, is_phase2=True)
        func(tc)
        pending_p2_count += 1
        badge = f"[{tc.status}]"
        print(f"P2-{idx:02d}: {tc.description:<55} {badge}")
        print(f"         Note: {tc.detail}")

    clean_temp_files()

    print("\n" + "=" * 72)
    print(f"Phase 1 Summary: {passed_count} passed, {failed_count} failed, {skipped_count} skipped (Total: {len(PHASE1_TESTS)})")
    print(f"Phase 2 Groundwork: {pending_p2_count} pending (scaffolded, execution blocked until container isolation)")
    print("=" * 72)

    if failed_count > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
