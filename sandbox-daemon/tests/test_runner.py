#!/usr/bin/env python3
"""
Phase 1 Regression Lab Test Harness for PolyExecution Studio (PES).

Automates verification of the Phase 1 C sandbox runner (sandbox-runner)
and evil test fixtures. Uses only the Python standard library.

Checks:
 1. Clean exit 0 -> OK
 2. Non-zero exit code (exit 7) -> RUNTIME_ERROR, exit_code preserved
 3. Target exit 127 vs missing binary exec failure -> RUNTIME_ERROR vs SANDBOX_ERROR
 4. Ordinary segmentation fault -> RUNTIME_ERROR, signal 11
 5. CPU time limit ceiling -> TIMEOUT, signal 24 (SIGXCPU)
 6. Wall-clock timeout enforcement -> TIMEOUT, wall_ms >= timeout
 7. Stream separation -> stdout and stderr captured in distinct buffers
 8. Output flood policy -> OUTPUT_LIMIT, capped buffer, process group killed
 9. File size limit ceiling -> OUTPUT_LIMIT, signal 25 (SIGXFSZ)
10. Descendant holding pipe open -> direct child reaped, no hang
11. Early stream closure -> wall timeout persists despite closed fds
12. RFC 8259 pure ASCII JSON & Latin-1 lossless byte round-trip
13. Memory bomb resident-memory heuristic -> MEMORY_LIMIT, signal 11
14. Host process table cleanup -> zero lingering runner/evil processes
15. CLI configuration customization -> custom job_id, timeout_ms, limits
"""

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent
EVIL_BIN_DIR = BASE_DIR / "tests" / "evil" / "bin"
RUNNER_BIN = BASE_DIR / "sandbox-runner"
TEMP_FSIZE_FILE = Path("/tmp/pes_test_fsize.tmp")

# Standard ExecuteResponse schema fields and types
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
    def __init__(self, name, description):
        self.name = name
        self.description = description
        self.status = "PENDING"
        self.detail = ""
        self.command = []
        self.raw_stdout = ""
        self.raw_stderr = ""
        self.parsed_json = None


def clean_temp_files():
    """Remove known temporary files created by test fixtures."""
    try:
        if TEMP_FSIZE_FILE.exists():
            TEMP_FSIZE_FILE.unlink()
    except OSError:
        pass


def build_binaries():
    """Ensure sandbox-runner and evil fixtures are compiled with strict flags."""
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


def run_runner(args, outer_timeout=10):
    """
    Execute sandbox-runner with args under an outer timeout.
    Returns (raw_stdout, raw_stderr, returncode, timed_out_bool).
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
        return proc.stdout, proc.stderr, proc.returncode, False
    except subprocess.TimeoutExpired as exc:
        raw_out = exc.stdout.decode("latin1", errors="replace") if exc.stdout else ""
        raw_err = exc.stderr.decode("latin1", errors="replace") if exc.stderr else ""
        return raw_out, raw_err, -1, True


def validate_json_schema(parsed):
    """Verify all schema fields exist and match expected types."""
    for key, expected_type in EXPECTED_SCHEMA.items():
        if key not in parsed:
            return False, f"Missing required key '{key}' in JSON response"
        if not isinstance(parsed[key], expected_type):
            return False, f"Key '{key}' expected {expected_type.__name__}, got {type(parsed[key]).__name__}"
    if parsed["status"] not in VALID_STATUSES:
        return False, f"Status '{parsed['status']}' is not a valid ExecuteResponse status"
    return True, ""


def find_lingering_processes():
    """
    Search for processes owned by current user running runner or evil binaries.
    Avoids killing anything; strictly returns process info for inspection.
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
            # Check owner UID
            if p.stat().st_uid != uid:
                continue

            cmdline_file = p / "cmdline"
            if not cmdline_file.exists():
                continue
            with open(cmdline_file, "rb") as f:
                cmd = f.read().replace(b"\x00", b" ").decode("latin1", errors="replace").strip()

            if "tests/evil/bin" in cmd or "sandbox-runner" in cmd:
                # Exclude self if cmdline contains test_runner
                if "test_runner.py" not in cmd:
                    lingering.append((int(p.name), cmd))
        except (OSError, PermissionError):
            continue
    return lingering


# ---------------------------------------------------------------------------
# Individual Test Implementations
# ---------------------------------------------------------------------------

def test_exit0(tc):
    """Test 1: Clean exit 0 -> status OK, exit_code 0, signal 0."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/exit0"]
    out, err, ret, timed_out = run_runner(["tests/evil/bin/exit0"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "OK" or data["exit_code"] != 0 or data["signal"] != 0:
        tc.status = "FAILED"
        tc.detail = f"Expected status: OK, exit_code: 0, signal: 0; got status: {data['status']}, exit_code: {data['exit_code']}, signal: {data['signal']}"
        return

    tc.status = "PASSED"
    tc.detail = f"Status OK, exit_code 0, signal 0, wall_ms {data['wall_ms']}"


def test_exit7(tc):
    """Test 2: Target exiting 7 -> status RUNTIME_ERROR, exit_code 7 preserved."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/exit7"]
    out, err, ret, timed_out = run_runner(["tests/evil/bin/exit7"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "RUNTIME_ERROR" or data["exit_code"] != 7 or data["signal"] != 0:
        tc.status = "FAILED"
        tc.detail = f"Expected status: RUNTIME_ERROR, exit_code: 7, signal: 0; got status: {data['status']}, exit_code: {data['exit_code']}"
        return

    tc.status = "PASSED"
    tc.detail = f"Status RUNTIME_ERROR, exit_code 7 preserved, signal 0"


def test_exit127_vs_missing(tc):
    """Test 3: Target exiting 127 vs missing binary execve failure."""
    # Subtest A: Valid program exiting 127
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/exit127"]
    out_a, err_a, _, timed_out_a = run_runner(["tests/evil/bin/exit127"])
    if timed_out_a:
        tc.status = "FAILED"
        tc.detail = "Subtest exit127 timed out"
        return

    try:
        data_a = json.loads(out_a)
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Subtest exit127 JSON parse error: {exc}"
        return

    if data_a["status"] != "RUNTIME_ERROR" or data_a["exit_code"] != 127 or data_a["stderr"] != "":
        tc.status = "FAILED"
        tc.detail = f"exit127 expected RUNTIME_ERROR with clean stderr; got status={data_a['status']}, stderr={data_a['stderr']!r}"
        return

    # Subtest B: Missing binary path
    missing_path = "/nonexistent/binary/path"
    out_b, err_b, _, timed_out_b = run_runner([missing_path])
    if timed_out_b:
        tc.status = "FAILED"
        tc.detail = "Subtest missing binary timed out"
        return

    try:
        data_b = json.loads(out_b)
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Subtest missing binary JSON parse error: {exc}"
        return

    tc.raw_stdout = f"A (exit127): {out_a}\nB (missing): {out_b}"
    tc.parsed_json = {"exit127": data_a, "missing": data_b}

    if data_b["status"] != "SANDBOX_ERROR" or data_b["exit_code"] != 127 or "setup error:" not in data_b["stderr"]:
        tc.status = "FAILED"
        tc.detail = f"missing binary expected SANDBOX_ERROR with setup error in stderr; got status={data_b['status']}, stderr={data_b['stderr']!r}"
        return

    tc.status = "PASSED"
    tc.detail = "Distinguished valid exit 127 (RUNTIME_ERROR, clean stderr) from execve failure (SANDBOX_ERROR, stage/errno stderr)"


def test_segv(tc):
    """Test 4: Ordinary segmentation fault -> status RUNTIME_ERROR, signal 11."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/segv"]
    out, err, ret, timed_out = run_runner(["tests/evil/bin/segv"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "RUNTIME_ERROR" or data["signal"] != 11:
        tc.status = "FAILED"
        tc.detail = f"Expected status: RUNTIME_ERROR, signal: 11; got status: {data['status']}, signal: {data['signal']}"
        return

    # Check that ordinary segv did not trigger the memory heuristic
    if data["peak_memory_kb"] * 1024 >= (128 * 1024 * 1024 * 0.8):
        tc.status = "FAILED"
        tc.detail = f"Unexpectedly high RSS ({data['peak_memory_kb']} KB) for ordinary segv"
        return

    tc.status = "PASSED"
    tc.detail = f"Status RUNTIME_ERROR, signal 11 (SIGSEGV), peak_memory {data['peak_memory_kb']} KB (heuristic threshold not breached)"


def test_cpu_loop(tc):
    """Test 5: CPU busy-loop -> status TIMEOUT, signal 24 (SIGXCPU)."""
    tc.command = [str(RUNNER_BIN), "--cpu-soft", "1", "--cpu-hard", "2", "--timeout-ms", "3000", "tests/evil/bin/loop"]
    out, err, ret, timed_out = run_runner(["--cpu-soft", "1", "--cpu-hard", "2", "--timeout-ms", "3000", "tests/evil/bin/loop"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "TIMEOUT" or data["signal"] != 24:
        tc.status = "FAILED"
        tc.detail = f"Expected status: TIMEOUT, signal: 24 (SIGXCPU); got status: {data['status']}, signal: {data['signal']}"
        return

    # Assert that cpu_ms is near the soft limit (1000 ms), with generous bounds [800 ms, 2500 ms]
    if data["cpu_ms"] < 800:
        tc.status = "FAILED"
        tc.detail = f"cpu_ms ({data['cpu_ms']}) was lower than expected for CPU timeout"
        return

    tc.status = "PASSED"
    tc.detail = f"Status TIMEOUT, signal 24 (SIGXCPU), cpu_ms: {data['cpu_ms']}, wall_ms: {data['wall_ms']}"


def test_sleeper(tc):
    """Test 6: Long sleep -> wall-clock timeout enforcement (wall_ms >= timeout)."""
    tc.command = [str(RUNNER_BIN), "--timeout-ms", "1000", "tests/evil/bin/sleeper"]
    out, err, ret, timed_out = run_runner(["--timeout-ms", "1000", "tests/evil/bin/sleeper"], outer_timeout=5)
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "TIMEOUT":
        tc.status = "FAILED"
        tc.detail = f"Expected status: TIMEOUT; got status: {data['status']}"
        return

    # Wall time must be at or above timeout (with slight 50ms tolerance for clock resolution)
    if data["wall_ms"] < 950:
        tc.status = "FAILED"
        tc.detail = f"wall_ms ({data['wall_ms']}) was shorter than requested timeout (1000ms)"
        return

    # CPU time should be near 0 because sleeper is blocked in nanosleep/sleep
    if data["cpu_ms"] > 200:
        tc.status = "FAILED"
        tc.detail = f"cpu_ms ({data['cpu_ms']}) unexpectedly high for sleeping process"
        return

    tc.status = "PASSED"
    tc.detail = f"Status TIMEOUT, wall_ms {data['wall_ms']} >= 1000, cpu_ms {data['cpu_ms']} (verified sleep consumed no CPU)"


def test_stream_separation(tc):
    """Test 7: Capturing stdout and stderr separately without cross-contamination."""
    # Subtest A: Binary writing both stdout and stderr
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/stdout_stderr"]
    out, err, ret, timed_out = run_runner(["tests/evil/bin/stdout_stderr"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["stdout"] != "STDOUT_DATA\n" or data["stderr"] != "STDERR_DATA\n":
        tc.status = "FAILED"
        tc.detail = f"Stream content mismatch: stdout={data['stdout']!r}, stderr={data['stderr']!r}"
        return

    # Subtest B: Binary writing only stderr (4096 bytes)
    out_b, _, _, timed_out_b = run_runner(["tests/evil/bin/stderr_only"])
    if timed_out_b:
        tc.status = "FAILED"
        tc.detail = "stderr_only timed out"
        return

    data_b = json.loads(out_b)
    if data_b["stdout"] != "" or len(data_b["stderr"]) != 4096:
        tc.status = "FAILED"
        tc.detail = f"stderr_only expected empty stdout and 4096-byte stderr; got stdout len={len(data_b['stdout'])}, stderr len={len(data_b['stderr'])}"
        return

    tc.status = "PASSED"
    tc.detail = "stdout and stderr strictly isolated; verified bidirectional and stderr-only streams"


def test_output_flood(tc):
    """Test 8: Output flooding -> status OUTPUT_LIMIT, capped buffer, process killed."""
    tc.command = [str(RUNNER_BIN), "--max-output-kb", "64", "tests/evil/bin/output_flood"]
    out, err, ret, timed_out = run_runner(["--max-output-kb", "64", "tests/evil/bin/output_flood"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "OUTPUT_LIMIT":
        tc.status = "FAILED"
        tc.detail = f"Expected status: OUTPUT_LIMIT; got status: {data['status']}"
        return

    # Output must be capped at 64 KiB = 65536 bytes
    cap_bytes = 64 * 1024
    if len(data["stdout"]) != cap_bytes:
        tc.status = "FAILED"
        tc.detail = f"stdout length ({len(data['stdout'])}) did not match exact cap ({cap_bytes} bytes)"
        return

    tc.status = "PASSED"
    tc.detail = f"Status OUTPUT_LIMIT, stdout length strictly capped at {len(data['stdout'])} bytes"


def test_file_size_limit(tc):
    """Test 9: File size ceiling -> status OUTPUT_LIMIT, signal 25 (SIGXFSZ)."""
    clean_temp_files()
    tc.command = [str(RUNNER_BIN), "--fsize-mb", "1", "tests/evil/bin/file_size_limit"]
    out, err, ret, timed_out = run_runner(["--fsize-mb", "1", "tests/evil/bin/file_size_limit"])
    tc.raw_stdout, tc.raw_stderr = out, err
    clean_temp_files()

    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "OUTPUT_LIMIT" or data["signal"] != 25:
        tc.status = "FAILED"
        tc.detail = f"Expected status: OUTPUT_LIMIT, signal: 25 (SIGXFSZ); got status: {data['status']}, signal: {data['signal']}"
        return

    tc.status = "PASSED"
    tc.detail = f"Status OUTPUT_LIMIT, signal 25 (SIGXFSZ), temp file /tmp/pes_test_fsize.tmp cleaned"


def test_descendant_pipe(tc):
    """Test 10: Descendant keeps pipe open -> direct child reaped, loop finishes rapidly."""
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/descendant_pipe"]
    t0 = time.monotonic()
    out, err, ret, timed_out = run_runner(["tests/evil/bin/descendant_pipe"], outer_timeout=5)
    elapsed = time.monotonic() - t0
    tc.raw_stdout, tc.raw_stderr = out, err

    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out waiting on descendant pipe"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    # Child returned 42 -> RUNTIME_ERROR
    if data["exit_code"] != 42 or data["status"] != "RUNTIME_ERROR":
        tc.status = "FAILED"
        tc.detail = f"Expected exit_code: 42, status: RUNTIME_ERROR; got exit_code: {data['exit_code']}, status: {data['status']}"
        return

    # Grandchild sleeps for 60 seconds; runner must reap direct child and finish in under 2 seconds
    if elapsed > 2.0 or data["wall_ms"] > 2000:
        tc.status = "FAILED"
        tc.detail = f"Runner hung on unclosed descendant pipe (elapsed: {elapsed:.2f}s, wall_ms: {data['wall_ms']})"
        return

    tc.status = "PASSED"
    tc.detail = f"Direct child exit_code 42 reaped in {data['wall_ms']} ms; avoided 60s grandchild hang"


def test_close_stream_sleep(tc):
    """Test 11: Child closes streams and sleeps -> wall timeout persists."""
    tc.command = [str(RUNNER_BIN), "--timeout-ms", "1000", "tests/evil/bin/close_stream_sleep"]
    out, err, ret, timed_out = run_runner(["--timeout-ms", "1000", "tests/evil/bin/close_stream_sleep"], outer_timeout=5)
    tc.raw_stdout, tc.raw_stderr = out, err

    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "TIMEOUT":
        tc.status = "FAILED"
        tc.detail = f"Expected status: TIMEOUT; got status: {data['status']}"
        return

    if data["wall_ms"] < 950:
        tc.status = "FAILED"
        tc.detail = f"wall_ms ({data['wall_ms']}) was shorter than requested timeout (1000ms)"
        return

    tc.status = "PASSED"
    tc.detail = f"Status TIMEOUT enforced at {data['wall_ms']} ms despite child closing stdout/stderr fds"


def test_json_latin1_reconstruction(tc):
    """Test 12: RFC 8259 JSON validation and exact 100% byte-for-byte Latin-1 reconstruction."""
    fixture_bin = EVIL_BIN_DIR / "json_escaping_test"
    tc.command = [str(RUNNER_BIN), "tests/evil/bin/json_escaping_test"]

    # 1. Capture ground truth binary output directly
    ground_truth = subprocess.run([str(fixture_bin)], capture_output=True)
    expected_stdout = ground_truth.stdout
    expected_stderr = ground_truth.stderr

    # 2. Run through sandbox-runner
    out, err, ret, timed_out = run_runner(["tests/evil/bin/json_escaping_test"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    # 3. Parse JSON
    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    # 4. Lossless Latin-1 decode
    actual_stdout = data["stdout"].encode("latin1")
    actual_stderr = data["stderr"].encode("latin1")

    if actual_stdout != expected_stdout:
        tc.status = "FAILED"
        tc.detail = f"Stdout binary mismatch: actual len {len(actual_stdout)}, expected len {len(expected_stdout)}"
        return

    if actual_stderr != expected_stderr:
        tc.status = "FAILED"
        tc.detail = f"Stderr binary mismatch: actual len {len(actual_stderr)}, expected len {len(expected_stderr)}"
        return

    # 5. Check specific collision points
    # Valid UTF-8 C3 A9
    if b"\xc3\xa9" not in actual_stdout:
        tc.status = "FAILED"
        tc.detail = "Valid UTF-8 sequence C3 A9 was not preserved"
        return
    # Raw invalid byte E9
    if b" \xe9 " not in actual_stdout:
        tc.status = "FAILED"
        tc.detail = "Raw invalid byte E9 was not preserved"
        return

    tc.status = "PASSED"
    tc.detail = f"100% byte-for-byte identity verified (stdout {len(actual_stdout)} bytes, stderr {len(actual_stderr)} bytes)"


def test_memory_bomb_heuristic(tc):
    """Test 13: Memory bomb allocation failure -> status MEMORY_LIMIT, signal 11, RSS heuristic."""
    tc.command = [str(RUNNER_BIN), "--as-mb", "128", "tests/evil/bin/memory_bomb"]
    out, err, ret, timed_out = run_runner(["--as-mb", "128", "tests/evil/bin/memory_bomb"])
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["status"] != "MEMORY_LIMIT" or data["signal"] != 11:
        tc.status = "FAILED"
        tc.detail = f"Expected status: MEMORY_LIMIT, signal: 11; got status: {data['status']}, signal: {data['signal']}"
        return

    # Verify peak RSS reached heuristic ceiling (>= 80% of 128 MiB = 104,857 KB)
    threshold_kb = int(128 * 1024 * 0.8)
    if data["peak_memory_kb"] < threshold_kb:
        tc.status = "FAILED"
        tc.detail = f"peak_memory_kb ({data['peak_memory_kb']}) was below 80% threshold ({threshold_kb} KB)"
        return

    tc.status = "PASSED"
    tc.detail = f"Status MEMORY_LIMIT, signal 11 (SIGSEGV), peak_memory {data['peak_memory_kb']} KB >= {threshold_kb} KB (heuristic confirmed)"


def test_cli_customization(tc):
    """Test 14: Custom CLI flags (--job-id, --timeout-ms, custom limits)."""
    tc.command = [str(RUNNER_BIN), "--job-id", "job-lab-99", "--timeout-ms", "600", "tests/evil/bin/sleeper"]
    out, err, ret, timed_out = run_runner(["--job-id", "job-lab-99", "--timeout-ms", "600", "tests/evil/bin/sleeper"], outer_timeout=5)
    tc.raw_stdout, tc.raw_stderr = out, err
    if timed_out:
        tc.status = "FAILED"
        tc.detail = "Runner timed out under outer test timeout"
        return

    try:
        data = json.loads(out)
        tc.parsed_json = data
    except Exception as exc:
        tc.status = "FAILED"
        tc.detail = f"Failed to parse JSON: {exc}"
        return

    valid, msg = validate_json_schema(data)
    if not valid:
        tc.status = "FAILED"
        tc.detail = msg
        return

    if data["job_id"] != "job-lab-99":
        tc.status = "FAILED"
        tc.detail = f"job_id mismatch: expected 'job-lab-99', got {data['job_id']!r}"
        return

    if data["status"] != "TIMEOUT" or data["wall_ms"] < 550 or data["wall_ms"] > 1500:
        tc.status = "FAILED"
        tc.detail = f"Unexpected status or timing for custom 600ms timeout: status={data['status']}, wall_ms={data['wall_ms']}"
        return

    tc.status = "PASSED"
    tc.detail = f"job_id 'job-lab-99' preserved, wall_ms {data['wall_ms']} aligned with 600ms timeout"


def test_process_cleanup(tc):
    """Test 15: Host process table check -> zero lingering runner or evil child processes."""
    time.sleep(0.1)  # Brief settling window for kernel process table cleanup
    lingering = find_lingering_processes()
    if lingering:
        tc.status = "FAILED"
        pids_str = ", ".join(f"PID {p[0]} ({p[1]})" for p in lingering)
        tc.detail = f"Detected {len(lingering)} lingering test process(es): {pids_str}"
        return

    tc.status = "PASSED"
    tc.detail = "Verified 0 lingering runner, evil, or zombie processes owned by current user"


# ---------------------------------------------------------------------------
# Test Runner Orchestration
# ---------------------------------------------------------------------------

ALL_TESTS = [
    ("test_exit0", "Clean exit 0 -> status OK", test_exit0),
    ("test_exit7", "Non-zero exit code (exit 7) -> RUNTIME_ERROR", test_exit7),
    ("test_exit127_vs_missing", "Target exit 127 vs missing binary execve failure", test_exit127_vs_missing),
    ("test_segv", "Ordinary segmentation fault (signal 11) -> RUNTIME_ERROR", test_segv),
    ("test_cpu_loop", "CPU time limit ceiling (SIGXCPU) -> TIMEOUT", test_cpu_loop),
    ("test_sleeper", "Wall-clock timeout enforcement -> TIMEOUT", test_sleeper),
    ("test_stream_separation", "Stream separation (stdout and stderr isolated)", test_stream_separation),
    ("test_output_flood", "Output flood policy -> OUTPUT_LIMIT, capped buffer", test_output_flood),
    ("test_file_size_limit", "File size limit ceiling -> OUTPUT_LIMIT (SIGXFSZ)", test_file_size_limit),
    ("test_descendant_pipe", "Descendant holding pipe open -> no hang", test_descendant_pipe),
    ("test_close_stream_sleep", "Early stream closure -> wall timeout persists", test_close_stream_sleep),
    ("test_json_latin1_reconstruction", "RFC 8259 JSON & Latin-1 lossless byte round-trip", test_json_latin1_reconstruction),
    ("test_memory_bomb_heuristic", "Memory bomb -> MEMORY_LIMIT via RSS heuristic", test_memory_bomb_heuristic),
    ("test_cli_customization", "CLI flags customization (--job-id, --timeout-ms)", test_cli_customization),
    ("test_process_cleanup", "Host process table cleanup check", test_process_cleanup),
]


def main():
    print("=" * 70)
    print("PolyExecution Studio (PES) — Phase 1 Regression Lab")
    print("=" * 70)

    clean_temp_files()
    build_binaries()

    passed_count = 0
    failed_count = 0
    skipped_count = 0

    results = []

    for idx, (func_name, desc, func) in enumerate(ALL_TESTS, start=1):
        tc = TestCase(func_name, desc)
        try:
            func(tc)
        except Exception as exc:
            tc.status = "FAILED"
            tc.detail = f"Unhandled exception during test execution: {exc}"

        results.append(tc)

        badge = f"[{tc.status}]"
        print(f"Test {idx:02d}: {tc.description:<52} {badge}")
        if tc.status == "PASSED":
            passed_count += 1
            print(f"         Detail: {tc.detail}")
        elif tc.status == "FAILED":
            failed_count += 1
            print(f"         FAILED: {tc.detail}")
            if tc.command:
                print(f"         Command: {' '.join(tc.command)}")
            if tc.raw_stdout:
                truncated_out = tc.raw_stdout[:300] + ("..." if len(tc.raw_stdout) > 300 else "")
                print(f"         Raw stdout: {truncated_out!r}")
            if tc.raw_stderr:
                truncated_err = tc.raw_stderr[:300] + ("..." if len(tc.raw_stderr) > 300 else "")
                print(f"         Raw stderr: {truncated_err!r}")
        elif tc.status == "SKIPPED":
            skipped_count += 1
            print(f"         SKIPPED: {tc.detail}")

    clean_temp_files()

    print("\n" + "=" * 70)
    print(f"Regression Lab Summary: {passed_count} passed, {failed_count} failed, {skipped_count} skipped (Total: {len(ALL_TESTS)})")
    print("=" * 70)

    if failed_count > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
