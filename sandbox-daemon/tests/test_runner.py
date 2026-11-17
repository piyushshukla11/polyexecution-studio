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
import signal
import subprocess
import sys
import tempfile
import time
import uuid
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


def find_lingering_processes(tracked_pids=None, cgroup_name=None):
    """
    Search for processes owned by current user running runner, evil binaries, or sandboxed target (/run/target).
    Also detects lingering processes via cgroup membership (any /proc/<pid>/cgroup matching cgroup_name or /pes-)
    and any explicitly tracked host PIDs that remain alive.
    Read-only inspection: strictly reports processes without killing anything.
    """
    lingering = []
    my_pid = os.getpid()
    uid = os.getuid()
    proc_root = Path("/proc")
    if not proc_root.exists():
        return lingering

    seen_pids = set()

    for p in proc_root.iterdir():
        if not p.is_dir() or not p.name.isdigit():
            continue
        try:
            pid = int(p.name)
            if pid == my_pid:
                continue
            if p.stat().st_uid != uid:
                continue

            cmdline_file = p / "cmdline"
            cmd = ""
            if cmdline_file.exists():
                try:
                    with open(cmdline_file, "rb") as f:
                        cmd = f.read().replace(b"\x00", b" ").decode("latin1", errors="replace").strip()
                except OSError:
                    pass

            # Exclude self if cmdline contains test_runner.py
            if "test_runner.py" in cmd:
                continue

            # Check cgroup membership
            in_pes_cgroup = False
            cgroup_file = p / "cgroup"
            if cgroup_file.exists():
                try:
                    cgroup_content = cgroup_file.read_text()
                    if cgroup_name and cgroup_name in cgroup_content:
                        in_pes_cgroup = True
                    elif "/pes-" in cgroup_content:
                        in_pes_cgroup = True
                except OSError:
                    pass

            # Check comm
            comm = ""
            comm_file = p / "comm"
            if comm_file.exists():
                try:
                    comm = comm_file.read_text().strip()
                except OSError:
                    pass

            is_lingering = False
            reasons = []

            if "tests/evil/bin" in cmd:
                is_lingering = True
                reasons.append("evil_bin")
            if "sandbox-runner" in cmd:
                is_lingering = True
                reasons.append("sandbox_runner")
            if "/run/target" in cmd or comm == "target":
                is_lingering = True
                reasons.append("sandboxed_target")
            if in_pes_cgroup:
                is_lingering = True
                reasons.append("pes_cgroup")

            if is_lingering:
                seen_pids.add(pid)
                desc = cmd if cmd else comm
                lingering.append((pid, f"[{','.join(reasons)}] {desc}"))
        except (OSError, PermissionError):
            continue

    # Also audit explicitly tracked host PIDs
    if tracked_pids:
        for t_pid in tracked_pids:
            if t_pid == my_pid or t_pid in seen_pids:
                continue
            try:
                os.kill(t_pid, 0)
                cmd = "unknown"
                try:
                    with open(f"/proc/{t_pid}/cmdline", "rb") as f:
                        cmd = f.read().replace(b"\x00", b" ").decode("latin1", errors="replace").strip()
                except OSError:
                    pass
                seen_pids.add(t_pid)
                lingering.append((t_pid, f"[tracked_pid_alive] {cmd}"))
            except OSError:
                pass

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
# Phase 2 Integration Checks
# ---------------------------------------------------------------------------

def find_delegated_cgroup_parent():
    """Find the delegated cgroup v2 parent directory for the current user."""
    cgroup_root = Path("/sys/fs/cgroup")
    proc_cgroup = Path("/proc/self/cgroup")
    if not proc_cgroup.exists() or not cgroup_root.exists():
        return None
    try:
        for line in proc_cgroup.read_text().splitlines():
            if line.startswith("0::"):
                rel = line[3:].strip()
                cand = Path(f"{cgroup_root}{rel}")
                while str(cand) != str(cgroup_root) and str(cand).startswith(str(cgroup_root)):
                    ctrl_file = cand / "cgroup.controllers"
                    sub_file = cand / "cgroup.subtree_control"
                    procs_file = cand / "cgroup.procs"
                    if ctrl_file.exists() and sub_file.exists() and procs_file.exists():
                        ctrl = ctrl_file.read_text()
                        sub = sub_file.read_text()
                        procs = procs_file.read_text().strip()
                        if (all(c in ctrl.split() for c in ("cpu", "memory", "pids")) and
                            all(c in sub.split() for c in ("cpu", "memory", "pids")) and
                            procs == "" and os.access(str(cand), os.W_OK | os.X_OK)):
                            return cand
                    cand = cand.parent
    except Exception:
        pass
    return None


def cgroup_kill_and_wait_populated_zero(cgroup_path, poll_timeout=2.0):
    """
    Guaranteed cgroup cleanup:
    1. Writes '1' to cgroup.kill.
    2. Polls cgroup.events for confirmed 'populated 0'.
    3. If populated 0 is NOT observed before poll_timeout:
       - Attempts to identify and SIGKILL surviving PIDs in cgroup.procs.
       - Re-polls briefly.
       - If still not populated 0: strictly ABORTS removal (does NOT call rmdir) and returns an explicit error.
    4. Only upon confirmed 'populated 0': calls os.rmdir(cgroup_path).
    5. Returns None on success, or an explicit error string describing the failure.
    """
    if not cgroup_path or not cgroup_path.exists():
        return None

    kill_file = cgroup_path / "cgroup.kill"
    if kill_file.exists():
        try:
            kill_file.write_text("1")
        except OSError as exc:
            return f"Failed to write '1' to {kill_file}: {exc}"
    else:
        procs_file = cgroup_path / "cgroup.procs"
        if procs_file.exists():
            try:
                for line in procs_file.read_text().splitlines():
                    if line.strip():
                        try:
                            os.kill(int(line.strip()), signal.SIGKILL)
                        except OSError:
                            pass
            except OSError:
                pass

    poll_start = time.time()
    populated_zero = False
    events_file = cgroup_path / "cgroup.events"

    while time.time() - poll_start < poll_timeout:
        if not cgroup_path.exists():
            return None
        if events_file.exists():
            try:
                if "populated 0" in events_file.read_text():
                    populated_zero = True
                    break
            except OSError:
                pass
        time.sleep(0.01)

    if not populated_zero and cgroup_path.exists():
        procs_file = cgroup_path / "cgroup.procs"
        if procs_file.exists():
            try:
                surviving_pids = [int(line.strip()) for line in procs_file.read_text().splitlines() if line.strip()]
                for p in surviving_pids:
                    try:
                        os.kill(p, signal.SIGKILL)
                    except OSError:
                        pass
            except OSError:
                pass
        retry_start = time.time()
        while time.time() - retry_start < 0.2:
            if events_file.exists():
                try:
                    if "populated 0" in events_file.read_text():
                        populated_zero = True
                        break
                except OSError:
                    pass
            time.sleep(0.01)

    if not populated_zero and cgroup_path.exists():
        procs_file = cgroup_path / "cgroup.procs"
        surviving = []
        if procs_file.exists():
            try:
                surviving = procs_file.read_text().split()
            except OSError:
                pass
        return (
            f"cgroup {cgroup_path} failed to reach confirmed 'populated 0' after {poll_timeout}s "
            f"(surviving PIDs: {surviving}); aborted removal to avoid leaking hidden state"
        )

    try:
        os.rmdir(cgroup_path)
    except OSError as exc:
        return f"os.rmdir({cgroup_path}) failed after confirmed populated 0: {exc}"

    return None


def run_runner_with_watchdog(cmd, outer_timeout=5.0, cgroup_sampler=None, target_binary_name=None):
    """
    Execute sandbox-runner under an independent safety watchdog with guaranteed failure cleanup.

    If an outer timeout or unhandled exception occurs:
    1. Locates any created job cgroup under the delegated parent.
    2. Writes '1' to cgroup.kill, waits for confirmed 'populated 0' before removing the cgroup via rmdir.
    3. Sends SIGKILL to the runner process group and reaps the runner process to prevent zombies.
    4. Audits the host process table for lingering test descendants via tracked PIDs, cgroup membership, and /run/target cmdlines.
    5. Returns cleanup errors explicitly; never leaves orphaned tasks or cgroups.

    Returns: (stdout, stderr, returncode, timed_out_bool, cgroup_path, cleanup_err, lingering)
    """
    delegated_parent = find_delegated_cgroup_parent()
    start_time = time.time()

    proc = subprocess.Popen(
        cmd,
        cwd=BASE_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )

    cgroup_path = None
    timed_out = False
    cleanup_err = None
    tracked_pids = set()
    tracked_pids.add(proc.pid)

    try:
        while True:
            now = time.time()
            if now - start_time > outer_timeout:
                timed_out = True
                break

            if delegated_parent and not cgroup_path:
                for cand in delegated_parent.glob(f"pes-{proc.pid}-*"):
                    cgroup_path = cand
                    break

            if cgroup_path and cgroup_path.exists():
                procs_file = cgroup_path / "cgroup.procs"
                if procs_file.exists():
                    try:
                        for line in procs_file.read_text().splitlines():
                            if line.strip():
                                tracked_pids.add(int(line.strip()))
                    except OSError:
                        pass

                if cgroup_sampler:
                    try:
                        cgroup_sampler(cgroup_path)
                    except Exception:
                        pass

            if proc.poll() is not None:
                break
            time.sleep(0.01)

        if timed_out:
            # 1. Terminate all remaining tasks in the job cgroup, waiting for confirmed populated 0
            if cgroup_path and cgroup_path.exists():
                err = cgroup_kill_and_wait_populated_zero(cgroup_path, poll_timeout=2.0)
                if err:
                    cleanup_err = err

            # 2. Terminate runner process group on host
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except OSError as exc:
                if exc.errno != errno.ESRCH:
                    err_msg = f"os.killpg({proc.pid}) failed: {exc}"
                    cleanup_err = f"{cleanup_err}; {err_msg}" if cleanup_err else err_msg

            # 3. Reap runner process to guarantee no zombie process remains
            try:
                stdout, stderr = proc.communicate(timeout=2.0)
            except Exception as exc:
                stdout, stderr = "", ""
                err_msg = f"Failed to reap runner process {proc.pid}: {exc}"
                cleanup_err = f"{cleanup_err}; {err_msg}" if cleanup_err else err_msg

            # 4. Check for surviving test descendants
            time.sleep(0.05)
            lingering = find_lingering_processes(
                tracked_pids=tracked_pids,
                cgroup_name=cgroup_path.name if cgroup_path else None,
            )
            if target_binary_name:
                lingering = [p for p in lingering if target_binary_name in p[1] or "target" in p[1] or "tracked_pid" in p[1] or "pes_cgroup" in p[1]]

            return stdout, stderr, proc.returncode, True, cgroup_path, cleanup_err, lingering

        # Runner completed within watchdog timeout: reap process
        try:
            stdout, stderr = proc.communicate(timeout=2.0)
        except Exception as exc:
            stdout, stderr = "", ""
            cleanup_err = f"Failed to reap completed runner process: {exc}"

        # Audit that the runner removed its job cgroup
        if cgroup_path and cgroup_path.exists():
            cleanup_err = f"Job cgroup directory {cgroup_path} was not removed after runner exit"
            err = cgroup_kill_and_wait_populated_zero(cgroup_path, poll_timeout=2.0)
            if err:
                cleanup_err = f"{cleanup_err}; cleanup failed: {err}"

        time.sleep(0.05)
        lingering = find_lingering_processes(
            tracked_pids=tracked_pids,
            cgroup_name=cgroup_path.name if cgroup_path else None,
        )
        if target_binary_name:
            lingering = [p for p in lingering if target_binary_name in p[1] or "target" in p[1] or "tracked_pid" in p[1] or "pes_cgroup" in p[1]]

        return stdout, stderr, proc.returncode, False, cgroup_path, cleanup_err, lingering

    except Exception as exc:
        cleanup_failures = []
        if cgroup_path and cgroup_path.exists():
            err = cgroup_kill_and_wait_populated_zero(cgroup_path, poll_timeout=2.0)
            if err:
                cleanup_failures.append(err)

        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except OSError as e:
            if e.errno != errno.ESRCH:
                cleanup_failures.append(f"killpg({proc.pid}) failed: {e}")

        try:
            proc.communicate(timeout=2.0)
        except Exception as e:
            cleanup_failures.append(f"reap runner({proc.pid}) failed: {e}")

        if cleanup_failures:
            raise RuntimeError(
                f"Exception during watchdog execution ({exc}) WITH CLEANUP FAILURES: {'; '.join(cleanup_failures)}"
            ) from exc
        raise exc


def test_phase2_fork_bomb(tc):
    """Phase 2 Check 01: Fork bomb containment via cgroups v2 pids.max (32) and PID namespace."""
    delegated_parent = find_delegated_cgroup_parent()
    if not delegated_parent:
        tc.status = "FAILED"
        tc.failure_category = "SYSTEM_ENVIRONMENT_ERROR"
        tc.detail = "No writable delegated cgroup parent with cpu, memory, and pids controllers found"
        return

    cmd = [str(RUNNER_BIN), "--timeout-ms", "1000", "tests/evil/bin/fork_bomb"]
    tc.command = cmd

    pids_max_observed = None
    max_events_observed = 0
    peak_pids_current = 0

    def sample_cgroup(cg_path):
        nonlocal pids_max_observed, max_events_observed, peak_pids_current
        try:
            if pids_max_observed is None:
                mf = cg_path / "pids.max"
                if mf.exists():
                    pids_max_observed = mf.read_text().strip()

            cf = cg_path / "pids.current"
            if cf.exists():
                cur = int(cf.read_text().strip())
                if cur > peak_pids_current:
                    peak_pids_current = cur

            ef = cg_path / "pids.events"
            if ef.exists():
                for line in ef.read_text().splitlines():
                    if line.startswith("max "):
                        ev = int(line.split()[1])
                        if ev > max_events_observed:
                            max_events_observed = ev
        except OSError:
            pass

    out, err, ret, timed_out, cgroup_path, cleanup_err, lingering = run_runner_with_watchdog(
        cmd, outer_timeout=5.0, cgroup_sampler=sample_cgroup, target_binary_name="fork_bomb"
    )
    tc.raw_stdout, tc.raw_stderr = out, err

    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Outer watchdog (5s) expired during fork_bomb containment (cleanup_err={cleanup_err}, lingering={lingering})"
        return

    if cleanup_err:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = cleanup_err
        return

    if lingering:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Detected {len(lingering)} lingering fork_bomb process(es): {lingering}"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse runner JSON output: {exc}"
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
        tc.detail = f"Expected status TIMEOUT on fork_bomb wall deadline; got status '{data['status']}'"
        return

    if pids_max_observed != "32":
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Live cgroup pids.max was {pids_max_observed!r}, expected '32'"
        return

    if max_events_observed <= 0:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Cgroup pids.events 'max' counter was {max_events_observed}; expected > 0 (no kernel fork denials registered)"
        return

    # In Linux kernel/cgroup/pids.c, pids_can_fork() atomically increments pids->counter
    # via atomic64_inc_return before checking if count > limit. When count exceeds limit (33 > 32),
    # the kernel immediately decrements counter via atomic64_dec and increments pids.events max.
    # During intense concurrent fork attempts, user-space polling of pids.current can transiently
    # observe 33 before the atomic decrement completes. We assert peak_pids_current <= 34,
    # strictly bounded well below secondary safety net RLIMIT_NPROC=64.
    if peak_pids_current > 34:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Peak cgroup pids.current was {peak_pids_current}, exceeding containment ceiling 34"
        return

    if cgroup_path and cgroup_path.exists():
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Cgroup directory {cgroup_path} was not removed after runner exit"
        return

    tc.status = "PASSED"
    tc.detail = (
        f"Cgroup pids.max=32 verified; live kernel pids.events max={max_events_observed} "
        f"(causal proof of task ceiling); peak pids.current={peak_pids_current} "
        f"(transient sample <= 34 during concurrent fork kernel probe, steady-state <= 32); "
        f"status TIMEOUT (1000ms); cgroup removed (phase2_cgroup_kill verified populated 0 prior to removal); "
        f"0 lingering processes"
    )


def test_phase2_read_etc_passwd(tc):
    """Phase 2 Check 02: Host filesystem snooping defense via mount namespace, tmpfs rootfs, and pivot_root."""
    canary_token = f"CANARY_SECRET_{uuid.uuid4().hex}"
    canary_file = tempfile.NamedTemporaryFile("w", prefix="pes_canary_", suffix=".txt", delete=False)
    canary_path = canary_file.name
    canary_file.write(f"HOST_SECRET_TOKEN={canary_token}\n")
    canary_file.close()

    cmd = [
        str(RUNNER_BIN),
        "--timeout-ms", "2000",
        "--",
        "tests/evil/bin/read_etc_passwd",
        canary_path,
        canary_token,
    ]
    tc.command = cmd

    try:
        out, err, ret, timed_out, cgroup_path, cleanup_err, lingering = run_runner_with_watchdog(
            cmd, outer_timeout=5.0, target_binary_name="read_etc_passwd"
        )
        tc.raw_stdout, tc.raw_stderr = out, err
    finally:
        if os.path.exists(canary_path):
            try:
                os.unlink(canary_path)
            except OSError:
                pass

    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner timed out under 5s outer watchdog during filesystem canary test (cleanup_err={cleanup_err}, lingering={lingering})"
        return

    if cleanup_err:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = cleanup_err
        return

    if lingering:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Detected {len(lingering)} lingering read_etc_passwd process(es): {lingering}"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse runner JSON output: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "RUNTIME_ERROR" or data["exit_code"] != 1 or data["signal"] != 0:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status RUNTIME_ERROR, exit_code 1, signal 0; got status {data['status']}, exit_code {data['exit_code']}, signal {data['signal']}"
        return

    if canary_token in data["stdout"] or canary_token in data["stderr"]:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = "CRITICAL: Host canary secret token was leaked in sandbox output"
        return

    if "HOST_CANARY_LEAKED" in data["stdout"] or "HOST_PASSWD_LEAKED" in data["stdout"]:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = "CRITICAL: Target binary reported host filesystem breach in stdout"
        return

    if f"TARGET={canary_path} STATUS=BLOCKED_BY_POLICY ERRNO=2" not in data["stderr"]:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected BLOCKED_BY_POLICY ERRNO=2 for canary path '{canary_path}'; got stderr: {data['stderr']!r}"
        return

    if "TARGET=/etc/passwd STATUS=BLOCKED_BY_POLICY ERRNO=2" not in data["stderr"]:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected BLOCKED_BY_POLICY ERRNO=2 for /etc/passwd; got stderr: {data['stderr']!r}"
        return

    if os.path.exists(canary_path):
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Temporary canary file {canary_path} was not cleaned up on host"
        return

    tc.status = "PASSED"
    tc.detail = (
        f"Host canary ({canary_path}) unobservable (ENOENT in private rootfs); "
        f"/etc/passwd inaccessible (ENOENT); exit_code 1 (BLOCKED_BY_POLICY); "
        f"secret token absent; guaranteed cleanup confirmed; 0 lingering processes"
    )


def test_phase2_net_connect(tc):
    """Phase 2 Check 03: Outbound network connect defense via seccomp socket filtering and CLONE_NEWNET."""
    try:
        host_netns = os.readlink("/proc/self/ns/net")
    except OSError as exc:
        tc.status = "FAILED"
        tc.failure_category = "SYSTEM_ENVIRONMENT_ERROR"
        tc.detail = f"Failed to read host network namespace: {exc}"
        return

    local_target_ip = "127.0.0.1"
    local_target_port = 65432

    cmd = [
        str(RUNNER_BIN),
        "--timeout-ms", "2000",
        "--",
        "tests/evil/bin/net_connect",
        local_target_ip,
        str(local_target_port),
    ]
    tc.command = cmd

    out, err, ret, timed_out, cgroup_path, cleanup_err, lingering = run_runner_with_watchdog(
        cmd, outer_timeout=5.0, target_binary_name="net_connect"
    )
    tc.raw_stdout, tc.raw_stderr = out, err

    if timed_out:
        tc.status = "FAILED"
        tc.failure_category = "HARNESS_TIMEOUT"
        tc.detail = f"Runner timed out under 5s outer watchdog during network isolation test (cleanup_err={cleanup_err}, lingering={lingering})"
        return

    if cleanup_err:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = cleanup_err
        return

    if lingering:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Detected {len(lingering)} lingering net_connect process(es): {lingering}"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = f"Failed to parse runner JSON output: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.failure_category = "SCHEMA_ERROR"
        tc.detail = msg
        return

    if data["status"] != "RUNTIME_ERROR" or data["exit_code"] != 1 or data["signal"] != 0:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected status RUNTIME_ERROR, exit_code 1, signal 0; got status {data['status']}, exit_code {data['exit_code']}, signal {data['signal']}"
        return

    if "NETWORK_CONNECTED" in data["stdout"]:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = "CRITICAL: Target binary established outbound network connection"
        return

    # Verify causal seccomp denial: socket() returned EPERM (errno 1)
    expected_diag = f"DIAG: OP=socket TARGET={local_target_ip}:{local_target_port} STATUS=BLOCKED_BY_POLICY ERRNO=1 (Operation not permitted)"
    if expected_diag not in data["stderr"]:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected seccomp socket denial in stderr; got: {data['stderr']!r}"
        return

    # Verify direct runtime proof of CLONE_NEWNET network namespace isolation
    # Target reads /proc/self/ns/net via readlink (allowed by seccomp) and emits: DIAG: OP=netns TARGET=net:[<inode>] STATUS=OBSERVED
    target_netns = None
    for line in data["stderr"].splitlines():
        if "DIAG: OP=netns TARGET=" in line and "STATUS=OBSERVED" in line:
            parts = line.split("TARGET=")
            if len(parts) > 1:
                target_netns = parts[1].split()[0]
            break

    if not target_netns or not target_netns.startswith("net:["):
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected target network namespace in stderr; got stderr: {data['stderr']!r}"
        return

    if target_netns == host_netns:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"CRITICAL: Target network namespace ({target_netns}) matched host network namespace ({host_netns}); not isolated!"
        return

    tc.status = "PASSED"
    tc.detail = (
        f"Seccomp denied socket() with EPERM (errno 1, BLOCKED_BY_POLICY) before connect; "
        f"network namespace CLONE_NEWNET verified distinct from host ({target_netns} != {host_netns}); "
        f"0 lingering processes"
    )


def test_phase2_watchdog_cleanup(tc):
    """Phase 2 Check 04: Watchdog failure-path teardown, confirmed populated 0 check, and lingering detection."""
    # 1. Test unit-level abort when populated 0 is absent
    test_dummy = tempfile.mkdtemp(prefix="pes_cgroup_dummy_")
    try:
        dummy_path = Path(test_dummy)
        err = cgroup_kill_and_wait_populated_zero(dummy_path, poll_timeout=0.05)
        if not err or "failed to reach confirmed 'populated 0'" not in err:
            tc.status = "FAILED"
            tc.failure_category = "CONTRACT_VIOLATION"
            tc.detail = f"Expected cgroup_kill_and_wait_populated_zero to abort without populated 0; got err={err!r}"
            return
        if not dummy_path.exists():
            tc.status = "FAILED"
            tc.failure_category = "CONTRACT_VIOLATION"
            tc.detail = "cgroup_kill_and_wait_populated_zero removed directory even though populated 0 was never observed!"
            return
    finally:
        try:
            os.rmdir(test_dummy)
        except OSError:
            pass

    # 2. Test live watchdog timeout cleanup on a long-running process
    delegated_parent = find_delegated_cgroup_parent()
    if not delegated_parent:
        tc.status = "FAILED"
        tc.failure_category = "SYSTEM_ENVIRONMENT_ERROR"
        tc.detail = "No writable delegated cgroup parent found"
        return

    # Run sleeper with 10s runner timeout, but enforce a 0.3s outer watchdog timeout
    cmd = [str(RUNNER_BIN), "--timeout-ms", "10000", "tests/evil/bin/sleeper"]
    tc.command = cmd

    out, err, ret, timed_out, cgroup_path, cleanup_err, lingering = run_runner_with_watchdog(
        cmd, outer_timeout=0.3, target_binary_name="sleeper"
    )
    tc.raw_stdout, tc.raw_stderr = out, err

    if not timed_out:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected watchdog outer timeout (0.3s); completed early with ret={ret}"
        return

    if cleanup_err:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Watchdog cleanup failed: {cleanup_err}"
        return

    if not cgroup_path:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = "Watchdog failed to locate the job cgroup path during execution"
        return

    if cgroup_path.exists():
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Job cgroup directory {cgroup_path} still exists on host after watchdog cleanup"
        return

    if lingering:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Detected lingering process(es) after watchdog timeout cleanup: {lingering}"
        return

    tc.status = "PASSED"
    tc.detail = (
        f"Watchdog confirmed populated 0 before rmdir; cgroup {cgroup_path.name} removed; "
        f"runner reaped; 0 lingering processes (cgroup membership, cmdline /run/target, and tracked PIDs clean); "
        f"unconfirmed populated 0 abort verified"
    )


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

def test_phase2_prctl_restricted(tc: TestCase):
    """Phase 2 Check 05: Seccomp restricted prctl filtering (PR_SET_NAME allowed, unauthorized operations killed with SIGSYS)."""
    cmd = [str(RUNNER_BIN), "--timeout-ms", "2000", "--", "tests/evil/bin/prctl_unauthorized"]
    tc.command = cmd
    stdout, stderr, ret, timed_out, _ = run_runner(
        ["--timeout-ms", "2000", "--", "tests/evil/bin/prctl_unauthorized"]
    )
    tc.raw_stdout = stdout
    tc.raw_stderr = stderr

    try:
        data = json.loads(stdout)
    except json.JSONDecodeError as exc:
        tc.status = "FAILED"
        tc.failure_category = "JSON_PARSE_ERROR"
        tc.detail = f"Failed to parse JSON output: {exc}"
        return

    # prctl_unauthorized calls PR_SET_NAME and PR_GET_NAME (allowed),
    # then calls PR_SET_DUMPABLE (denied by seccomp with SIGSYS / signal 31).
    if data.get("status") != "RUNTIME_ERROR" or data.get("signal") != 31:
        tc.status = "FAILED"
        tc.failure_category = "CONTRACT_VIOLATION"
        tc.detail = f"Expected RUNTIME_ERROR with signal 31 (SIGSYS); got status={data.get('status')}, signal={data.get('signal')}, exit_code={data.get('exit_code')}"
        return

    tc.status = "PASSED"
    tc.detail = "PR_SET_NAME and PR_GET_NAME permitted; unauthorized prctl terminated with SIGSYS (signal 31)"


PHASE2_CHECKS = [
    ("test_phase2_fork_bomb", "Fork bomb containment (Phase 2)", test_phase2_fork_bomb),
    ("test_phase2_read_etc_passwd", "Host filesystem snooping defense (Phase 2)", test_phase2_read_etc_passwd),
    ("test_phase2_net_connect", "Outbound network connect defense (Phase 2)", test_phase2_net_connect),
    ("test_phase2_watchdog_cleanup", "Watchdog failure cleanup & target detection (Phase 2)", test_phase2_watchdog_cleanup),
    ("test_phase2_prctl_restricted", "Restricted prctl thread naming filter (Phase 2)", test_phase2_prctl_restricted),
]


def main():
    print("=" * 72)
    print("PolyExecution Studio (PES) — Phase 1 & Phase 2 Integration Test Harness")
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

    print("\n--- Phase 2 Integration Checks ---")
    passed_p2_count = 0
    failed_p2_count = 0
    for idx, (func_name, desc, func) in enumerate(PHASE2_CHECKS, start=1):
        tc = TestCase(func_name, desc, is_phase2=True)
        try:
            func(tc)
        except Exception as exc:
            tc.status = "FAILED"
            tc.failure_category = "UNHANDLED_EXCEPTION"
            tc.detail = f"Unhandled exception during test execution: {exc}"

        badge = f"[{tc.status}]"
        print(f"P2-{idx:02d}: {tc.description:<55} {badge}")
        if tc.status == "PASSED":
            passed_p2_count += 1
            print(f"         Detail: {tc.detail}")
        elif tc.status == "FAILED":
            failed_p2_count += 1
            print(f"         FAILED [{tc.failure_category}]: {tc.detail}")
            if tc.command:
                print(f"         Command: {' '.join(tc.command)}")
            if tc.raw_stdout:
                truncated_out = tc.raw_stdout[:300] + ("..." if len(tc.raw_stdout) > 300 else "")
                print(f"         Raw stdout: {truncated_out!r}")
            if tc.raw_stderr:
                truncated_err = tc.raw_stderr[:300] + ("..." if len(tc.raw_stderr) > 300 else "")
                print(f"         Raw stderr: {truncated_err!r}")

    clean_temp_files()

    print("\n" + "=" * 72)
    print(f"Phase 1 Summary: {passed_count} passed, {failed_count} failed, {skipped_count} skipped (Total: {len(PHASE1_TESTS)})")
    print(f"Phase 2 Integration: {passed_p2_count} passed, {failed_p2_count} failed (Total: {len(PHASE2_CHECKS)})")
    print("=" * 72)

    if failed_count > 0 or failed_p2_count > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
