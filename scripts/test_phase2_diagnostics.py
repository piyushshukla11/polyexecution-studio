#!/usr/bin/env python3
"""
Unit and source-level verification tests for Phase 2 fixture diagnostics.
Tier C Verification Suite

Validates that Phase 2 fixtures provide deterministic diagnostic outputs
without executing uncontained attacks:
  1. Source-level checks for structured DIAG output, dynamic canary injection, and errno handling
  2. Mock-based classifier distinguishing policy blocks from ambient errors and canary leaks
  3. Safe compile-only check verifying zero warnings under -Wall -Wextra -Werror
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
from phase2_preflight import run_preflight
EVIL_DIR = REPO_ROOT / "sandbox-daemon" / "tests" / "evil"


def parse_diagnostic_line(line: str) -> dict:
    """Parses a structured DIAG line emitted by Phase 2 fixtures."""
    line = line.strip()
    if not line.startswith("DIAG:"):
        return {}
    parts = line[5:].strip().split()
    fields = {}
    for part in parts:
        if "=" in part:
            k, v = part.split("=", 1)
            fields[k] = v
    return fields


def classify_fixture_outcome(exit_code: int, stdout: str, stderr: str) -> dict:
    """
    Classifies a fixture's outcome based on exit code and diagnostic stream.
    Rejects ambient network drops and container-internal passwd files from false claims.
    """
    diag_fields = {}
    for line in stderr.splitlines():
        if line.startswith("DIAG:"):
            diag_fields = parse_diagnostic_line(line)
            break

    status = diag_fields.get("STATUS", "")
    op = diag_fields.get("OP", "")
    target = diag_fields.get("TARGET", "")
    errno_str = diag_fields.get("ERRNO", "")

    if exit_code == 0:
        return {
            "verdict": "BREACH",
            "op": op,
            "target": target,
            "status": status,
            "detail": f"Target breached containment policy (stdout: {stdout.strip()!r})",
        }

    if exit_code == 1:
        if status == "BLOCKED_BY_POLICY":
            return {
                "verdict": "POLICY_DENIED",
                "op": op,
                "target": target,
                "errno": errno_str,
                "detail": f"Target was explicitly blocked by sandbox security policy ({op} on {target})",
            }
        if status == "CONTAINER_LOCAL_PASSWD_OBSERVED":
            return {
                "verdict": "SAFE_LOCAL_RESOURCE",
                "op": op,
                "target": target,
                "detail": "Target opened container-local passwd without leaking host secrets",
            }
        return {
            "verdict": "POLICY_DENIED",
            "op": op,
            "target": target,
            "detail": "Exit 1 with expected policy containment",
        }

    # Exit code 2 or other non-zero
    return {
        "verdict": "UNEXPECTED_FAILURE",
        "op": op,
        "target": target,
        "errno": errno_str,
        "detail": f"Target failed due to ambient or system error (not proven isolation policy): {status}",
    }


class TestPhase2FixtureDiagnostics(unittest.TestCase):
    """Verifies Phase 2 fixture diagnostics, contracts, and safe compilation."""

    def test_read_etc_passwd_source_contract(self) -> None:
        """Verify read_etc_passwd.c source structure, canary interface, and contracts."""
        src = (EVIL_DIR / "read_etc_passwd.c").read_text()

        # Must accept argc, argv
        self.assertIn("int main(int argc, char *argv[])", src)
        # Must support dynamic canary path via argv[1] and PES_CANARY_PATH
        self.assertIn("argv[1]", src)
        self.assertIn("PES_CANARY_PATH", src)
        # Must support dynamic canary token via argv[2] and PES_CANARY_TOKEN
        self.assertIn("argv[2]", src)
        self.assertIn("PES_CANARY_TOKEN", src)
        # Must support host user check
        self.assertIn("PES_HOST_USER", src)
        self.assertIn("zogratis:", src)
        # Must emit structured DIAG tag
        self.assertIn("DIAG:", src)
        # Must distinguish policy errnos
        self.assertIn("ENOENT", src)
        self.assertIn("EACCES", src)
        self.assertIn("EPERM", src)
        # Must return exit codes 0 (breach), 1 (policy), 2 (unexpected)
        self.assertIn("return 0;", src)
        self.assertIn("return 1;", src)
        self.assertIn("return 2;", src)

    def test_net_connect_source_contract(self) -> None:
        """Verify net_connect.c source structure, dynamic target interface, and contracts."""
        src = (EVIL_DIR / "net_connect.c").read_text()

        # Must accept argc, argv
        self.assertIn("int main(int argc, char *argv[])", src)
        # Must support dynamic target IP via argv[1] and PES_NET_TARGET_IP
        self.assertIn("argv[1]", src)
        self.assertIn("PES_NET_TARGET_IP", src)
        # Must support dynamic target port via argv[2] and PES_NET_TARGET_PORT
        self.assertIn("argv[2]", src)
        self.assertIn("PES_NET_TARGET_PORT", src)
        # Must emit structured DIAG tag
        self.assertIn("DIAG:", src)
        # Must distinguish expected network namespace behavior (ENETUNREACH)
        self.assertIn("ENETUNREACH", src)
        # Must distinguish seccomp socket blocks (EPERM, EACCES)
        self.assertIn("EPERM", src)
        self.assertIn("EACCES", src)
        # Must handle ECONNREFUSED as ambient reachability failure, not policy block
        self.assertIn("ECONNREFUSED", src)
        # Must return exit codes 0 (breach), 1 (policy), 2 (unexpected)
        self.assertIn("return 0;", src)
        self.assertIn("return 1;", src)
        self.assertIn("return 2;", src)

    def test_mock_classification_network_conn_refused_rejected(self) -> None:
        """
        Verify classifier rejects ECONNREFUSED as proof of isolation.
        ECONNREFUSED indicates packets reached the host/port (reachable!).
        """
        mock_stderr = "DIAG: OP=connect TARGET=127.0.0.1:9999 STATUS=UNEXPECTED_FAILURE ERRNO=111 (Connection refused)\n"
        res = classify_fixture_outcome(exit_code=2, stdout="", stderr=mock_stderr)
        self.assertEqual(res["verdict"], "UNEXPECTED_FAILURE")
        self.assertEqual(res["errno"], "111")
        self.assertIn("ambient or system error", res["detail"])

    def test_fork_bomb_safety_label(self) -> None:
        """Verify fork_bomb.c contains safety warnings and verification guidance."""
        src = (EVIL_DIR / "fork_bomb.c").read_text()
        self.assertIn("DANGEROUS: DO NOT EXECUTE", src)
        self.assertIn("pids.max", src)
        self.assertIn("pids.events", src)
        self.assertIn("cgroup.kill", src)

    def test_mock_classification_network_policy_blocked(self) -> None:
        """Verify classifier correctly recognizes ENETUNREACH as proven policy denial."""
        mock_stderr = "DIAG: OP=connect TARGET=93.184.216.34:80 STATUS=BLOCKED_BY_POLICY ERRNO=101 (Network is unreachable)\n"
        res = classify_fixture_outcome(exit_code=1, stdout="", stderr=mock_stderr)
        self.assertEqual(res["verdict"], "POLICY_DENIED")
        self.assertEqual(res["op"], "connect")
        self.assertEqual(res["errno"], "101")

    def test_mock_classification_network_ambient_timeout_rejected(self) -> None:
        """
        Verify classifier rejects ETIMEDOUT as proof of isolation.
        Ambient network drops must be classified as UNEXPECTED_FAILURE.
        """
        mock_stderr = "DIAG: OP=connect TARGET=93.184.216.34:80 STATUS=UNEXPECTED_FAILURE ERRNO=110 (Connection timed out)\n"
        res = classify_fixture_outcome(exit_code=2, stdout="", stderr=mock_stderr)
        self.assertEqual(res["verdict"], "UNEXPECTED_FAILURE")
        self.assertIn("ambient or system error", res["detail"])

    def test_mock_classification_filesystem_canary_breach(self) -> None:
        """Verify classifier correctly recognizes canary leak as breach."""
        mock_stdout = "HOST_CANARY_LEAKED\n"
        mock_stderr = "DIAG: OP=open TARGET=/tmp/pes_canary_test123.txt STATUS=BREACH_CANARY_READ\n"
        res = classify_fixture_outcome(exit_code=0, stdout=mock_stdout, stderr=mock_stderr)
        self.assertEqual(res["verdict"], "BREACH")
        self.assertEqual(res["target"], "/tmp/pes_canary_test123.txt")

    def test_mock_classification_filesystem_canary_blocked_enoent(self) -> None:
        """Verify classifier recognizes canary missing (ENOENT) as policy containment."""
        mock_stderr = "DIAG: OP=open TARGET=/tmp/pes_canary_test123.txt STATUS=BLOCKED_BY_POLICY ERRNO=2 (No such file or directory)\n"
        res = classify_fixture_outcome(exit_code=1, stdout="", stderr=mock_stderr)
        self.assertEqual(res["verdict"], "POLICY_DENIED")
        self.assertEqual(res["errno"], "2")

    def test_mock_classification_filesystem_canary_blocked_eacces(self) -> None:
        """Verify classifier recognizes canary access denied (EACCES) as policy containment."""
        mock_stderr = "DIAG: OP=open TARGET=/tmp/pes_canary_test123.txt STATUS=BLOCKED_BY_POLICY ERRNO=13 (Permission denied)\n"
        res = classify_fixture_outcome(exit_code=1, stdout="", stderr=mock_stderr)
        self.assertEqual(res["verdict"], "POLICY_DENIED")
        self.assertEqual(res["errno"], "13")

    def test_mock_classification_filesystem_canary_unexpected_failure(self) -> None:
        """Verify classifier marks unrelated I/O failure on canary as UNEXPECTED_FAILURE."""
        mock_stderr = "DIAG: OP=open TARGET=/tmp/pes_canary_test123.txt STATUS=UNEXPECTED_FAILURE ERRNO=5 (Input/output error)\n"
        res = classify_fixture_outcome(exit_code=2, stdout="", stderr=mock_stderr)
        self.assertEqual(res["verdict"], "UNEXPECTED_FAILURE")
        self.assertEqual(res["errno"], "5")

    def test_mock_classification_filesystem_local_passwd_safe(self) -> None:
        """
        Verify classifier correctly recognizes container-local passwd without host
        data as safe containment rather than false positive breach.
        """
        mock_stderr = "DIAG: OP=read TARGET=/etc/passwd STATUS=CONTAINER_LOCAL_PASSWD_OBSERVED\n"
        res = classify_fixture_outcome(exit_code=1, stdout="", stderr=mock_stderr)
        self.assertEqual(res["verdict"], "SAFE_LOCAL_RESOURCE")

    def test_uid_gid_mapping_and_setgroups_contract(self) -> None:
        """
        Verify that architecture documentation strictly encodes the setgroups
        and UID/GID mapping contracts (user_namespaces(7) and setgroups(2)).
        """
        doc = (REPO_ROOT / "docs" / "03_SANDBOX_DEEP_DIVE.md").read_text()

        # Must document both Route A (newuidmap/newgidmap) and Route B (direct-write)
        self.assertIn("Route A: Subordinate Helper Route", doc)
        self.assertIn("Route B: Unprivileged Direct-Write Route", doc)

        # Must document CVE-2014-8989 and setgroups denial requirement for direct-write
        self.assertIn("CVE-2014-8989", doc)
        self.assertIn("/proc/[pid]/setgroups", doc)
        self.assertIn('"deny"', doc)

        # Must document that writing "deny" permanently disables setgroups(2) -> EPERM
        self.assertIn("permanently disables `setgroups(2)`", doc)
        self.assertIn("EPERM", doc)

        # Must document that helper presence does NOT prove capability
        self.assertIn("What Helper Presence Proves vs Does Not Prove", doc)

        # Must explicitly acknowledge the host ENOSYS runtime boundary
        self.assertIn("unshare(CLONE_NEWUSER)", doc)
        self.assertIn("errno=38 (ENOSYS)", doc)

    def test_supplementary_group_and_identity_contract(self) -> None:
        """
        Verify that architecture documentation strictly encodes the supplementary
        group contract (credentials(7), setgroups(2), and execve(2)), rejects the
        setgid/setuid fallacy, and marks Route B unsupported for containment.
        """
        doc = (REPO_ROOT / "docs" / "03_SANDBOX_DEEP_DIVE.md").read_text()

        # Must distinguish process identity (real/effective/saved) from supplementary groups
        self.assertIn("Process Identity vs Supplementary Groups (`credentials(7)`)", doc)
        self.assertIn("supplementary group IDs (`getgroups(2)`)", doc)

        # Must explicitly reject the fallacy that setgid()/setuid() clear supplementary groups
        self.assertIn("The `setuid`/`setgid` Fallacy and `execve(2)` Inheritance", doc)
        self.assertIn("does **not** modify or clear the supplementary group list", doc)

        # Must document that supplementary groups survive execve(2)
        self.assertIn("`execve(2)` preserves supplementary group IDs across process execution", doc)

        # Must document the supplementary group inheritance trap for direct-write (Route B)
        self.assertIn("The Supplementary Group Inheritance Trap", doc)
        self.assertIn("Route B is **UNSUPPORTED** for secure multi-tenant sandbox execution", doc)

        # Must document that Route A is strictly conditional on helper permissions
        self.assertIn("Conditional Route A Contract", doc)
        self.assertIn("possessing setuid-root file permissions", doc)
        self.assertIn("PR_SET_NO_NEW_PRIVS", doc)

        # Must document capability drop as an open design question, not claimed complete
        self.assertIn("Capability Dropping Architecture & Open Design Questions", doc)
        self.assertIn("Capability Bounding Set", doc)
        self.assertIn("PR_CAPBSET_DROP", doc)
        self.assertIn("Required Capability Drop Design Work (Before Phase 2 Sign-off)", doc)

    def test_pid_namespace_and_procfs_lifecycle_contract(self) -> None:
        """
        Verify that architecture documentation strictly encodes the PID namespace
        lifecycle, PID 1 semantics, and procfs mount ordering (pid_namespaces(7) and proc(5)).
        """
        doc = (REPO_ROOT / "docs" / "03_SANDBOX_DEEP_DIVE.md").read_text()

        # Must document why unshare(CLONE_NEWPID) does not change caller PID namespace
        self.assertIn("Why `unshare(CLONE_NEWPID)` Does Not Change the Caller", doc)
        self.assertIn("immutable attribute assigned when the process is created", doc)

        # Must document that the first child becomes PID 1 and Child 1 supervises it
        self.assertIn("PID 1 Assignment and Supervisor Hierarchy", doc)
        self.assertIn("Child 2 is born as PID 1", doc)
        self.assertIn("acts as its supervisor", doc)

        # Must document procfs mount ordering: host leak if before, isolated if after
        self.assertIn("Procfs Mount Ordering: Host Leak vs True Process Table Isolation", doc)
        self.assertIn("Step 13: Mount Private Procfs (from inside new PID namespace)", doc)
        self.assertIn("executed by Child 2 inside new PID namespace", doc)

        # Must document that PID 1 termination causes kernel to send SIGKILL to all processes
        self.assertIn("PID 1 Exit and Guaranteed Descendant Termination", doc)
        self.assertIn("kernel immediately sends `SIGKILL` to *all* remaining processes", doc)

        # Must acknowledge the host ENOSYS runtime boundary
        self.assertIn("unshare(CLONE_NEWPID)", doc)
        self.assertIn("errno=38 (ENOSYS)", doc)

    def test_runner_argument_environment_gap_contract(self) -> None:
        """
        Verify that the runner implementation gap (only passing binary_path and empty env)
        is accurately reflected in code and architecture documentation, and that the four
        safe future interface principles are strictly specified.
        """
        sandbox_c = (REPO_ROOT / "sandbox-daemon" / "sandbox.c").read_text()
        sandbox_h = (REPO_ROOT / "sandbox-daemon" / "sandbox.h").read_text()
        doc = (REPO_ROOT / "docs" / "03_SANDBOX_DEEP_DIVE.md").read_text()

        # Check current runner C code: only binary_path in child_argv, empty child_envp
        self.assertIn("char *const child_argv[] = { (char *)binary_path, NULL };", sandbox_c)
        self.assertIn("char *const child_envp[] = { NULL };", sandbox_c)
        self.assertIn("execve(binary_path, child_argv, child_envp);", sandbox_c)

        # Check sandbox.h runner signature: only binary_path, no argv or env parameters
        self.assertIn("int run_sandbox_with_config(const char *binary_path, const sandbox_config_t *cfg, sandbox_result_t *res);", sandbox_h)

        # Check documentation of the gap and safe interface principles
        self.assertIn("Target Arguments & Environment: Current Runner Gap & Safe Future Interface Contract", doc)
        self.assertIn("Explicit Argument Array", doc)
        self.assertIn("Explicit Environment Allowlist", doc)
        self.assertIn("No Shell Command String", doc)
        self.assertIn("No User-Controlled PATH Search", doc)
        self.assertIn("execve()", doc)
        self.assertIn("execvp()", doc)

    def test_capability_statuses_distinct_from_enforcement(self) -> None:
        """
        Verify that preflight audit categories strictly keep observed filesystem/toolchain
        artifacts (PRESENT) distinct from runtime enforcement (INFERRED) and unavailable
        features (UNAVAILABLE), never claiming enforcement during read-only audit.
        """
        report = run_preflight()
        readiness = report["readiness"]

        # User namespaces must be UNAVAILABLE due to unshare probe ENOSYS
        self.assertEqual(readiness["user_namespaces"]["status"], "UNAVAILABLE")
        self.assertIn("errno=38", readiness["user_namespaces"]["detail"])

        # Toolchain / controllers may be PRESENT
        self.assertEqual(readiness["cgroup_v2_controllers"]["status"], "PRESENT")
        self.assertEqual(readiness["seccomp_bpf_toolchain"]["status"], "PRESENT")

        # Runtime enforcement MUST be INFERRED (not PRESENT or PROBE_SUCCEEDED)
        self.assertEqual(readiness["cgroup_v2_pids_enforcement"]["status"], "INFERRED")
        self.assertEqual(readiness["cgroup_v2_memory_enforcement"]["status"], "INFERRED")
        self.assertEqual(readiness["seccomp_bpf_enforcement"]["status"], "INFERRED")
        self.assertEqual(readiness["privilege_dropping"]["status"], "INFERRED")

        # Rootless mapping helpers: binaries exist, but mapping UNAVAILABLE due to ENOSYS
        self.assertEqual(readiness["rootless_id_mapping"]["status"], "UNAVAILABLE")

    def test_safe_compilation_all_phase2_fixtures(self) -> None:
        """
        Safely compiles all Phase 2 evil fixtures with -Wall -Wextra -Werror.
        Confirms zero warnings and zero errors without executing the binaries.
        """
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            fixtures = ["read_etc_passwd.c", "net_connect.c", "fork_bomb.c"]

            for fname in fixtures:
                src_path = EVIL_DIR / fname
                bin_path = tmp / fname.replace(".c", "")

                cmd = [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O2",
                    str(src_path),
                    "-o",
                    str(bin_path),
                ]
                proc = subprocess.run(cmd, capture_output=True, text=True)
                self.assertEqual(
                    proc.returncode,
                    0,
                    f"Compilation of {fname} failed: {proc.stderr}",
                )
                self.assertTrue(bin_path.is_file())


if __name__ == "__main__":
    unittest.main()
