#!/usr/bin/env python3
"""
Unit and synthetic regression tests for scripts/phase2_preflight.py
Tier C Verification Suite

Tests preflight capability audit logic against:
  1. Synthetic Supported Case (mock probes succeed, verifies PRESENT vs INFERRED)
  2. Synthetic Unavailable Case (missing cgroups v2, missing headers, unshare ENOSYS)
  3. Synthetic Permission-Denied Case (cgroups unwritable, probe returns EPERM)
  4. Rootless Mapping Semantic Case (subuid present + unshare ENOSYS -> UNAVAILABLE)
  5. Cgroup Read-Only Limitation Case (controllers present -> INFERRED enforcement)
  6. Live Host Inspection (verifies live probe runs safely without state changes)
"""

import os
import sys
import tempfile
import unittest
from pathlib import Path

# Add project root to path so we can import phase2_preflight
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from phase2_preflight import run_preflight, format_text_report


class TestPhase2Preflight(unittest.TestCase):
    """Synthetic test suite for Phase 2 capability preflight auditor."""

    def test_synthetic_supported_case(self) -> None:
        """
        Verify that when all filesystem and system primitives are available,
        the preflight tool reports PROBE_SUCCEEDED for probes, PRESENT for observed
        structures, and INFERRED for unexercised runtime enforcement.
        """
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            proc_root = tmp / "proc"
            sys_root = tmp / "sys"
            etc_root = tmp / "etc"
            usr_root = tmp / "usr"

            # Create mock proc structure
            (proc_root / "self" / "ns").mkdir(parents=True)
            for ns in ["cgroup", "ipc", "mnt", "net", "pid", "time", "user", "uts"]:
                (proc_root / "self" / "ns" / ns).symlink_to(f"{ns}:[4026531834]")
            (proc_root / "version").write_text("Linux version 6.6.0-mock-supported\n")
            (proc_root / "self" / "cgroup").write_text("0::/user.slice/user-1000.slice/user@1000.service\n")
            (proc_root / "sys" / "kernel").mkdir(parents=True)
            (proc_root / "sys" / "kernel" / "unprivileged_userns_clone").write_text("1\n")

            # Create mock sys cgroup v2 structure
            cg_user = sys_root / "fs" / "cgroup" / "user.slice" / "user-1000.slice" / "user@1000.service"
            cg_user.mkdir(parents=True)
            (sys_root / "fs" / "cgroup" / "cgroup.controllers").write_text("cpu memory pids\n")
            (cg_user / "cgroup.controllers").write_text("cpu memory pids\n")
            (cg_user / "cgroup.subtree_control").write_text("cpu memory pids\n")
            (cg_user / "cgroup.procs").write_text("1234\n")

            # Create mock etc structure
            etc_root.mkdir(parents=True)
            (etc_root / "subuid").write_text("mockuser:100000:65536\n")
            (etc_root / "subgid").write_text("mockuser:100000:65536\n")

            # Create mock usr structure
            (usr_root / "include").mkdir(parents=True)
            (usr_root / "include" / "seccomp.h").write_text("#define SECCOMP_MODE_FILTER 2\n")

            # Mock probes
            def mock_unshare(flag: int):
                return 0, 0  # Success

            def mock_prctl(opt: int):
                return 0, 0  # Success

            def mock_which(cmd: str):
                return f"/mock/bin/{cmd}"

            def mock_find_library(name: str):
                return "libseccomp.so.2"

            report = run_preflight(
                proc_root=str(proc_root),
                sys_root=str(sys_root),
                etc_root=str(etc_root),
                usr_root=str(usr_root),
                unshare_probe_fn=mock_unshare,
                prctl_probe_fn=mock_prctl,
                which_fn=mock_which,
                find_library_fn=mock_find_library,
                current_uid=1000,
                current_user="mockuser",
            )

            readiness = report["readiness"]
            self.assertEqual(readiness["user_namespaces"]["status"], "PROBE_SUCCEEDED")
            self.assertEqual(readiness["pid_namespaces"]["status"], "PROBE_SUCCEEDED")
            self.assertEqual(readiness["network_namespaces"]["status"], "PROBE_SUCCEEDED")
            self.assertEqual(readiness["mount_namespaces"]["status"], "PROBE_SUCCEEDED")

            # Cgroup controllers are PRESENT, but enforcement is INFERRED (read-only probe)
            self.assertEqual(readiness["cgroup_v2_controllers"]["status"], "PRESENT")
            self.assertEqual(readiness["cgroup_v2_pids_enforcement"]["status"], "INFERRED")
            self.assertEqual(readiness["cgroup_v2_memory_enforcement"]["status"], "INFERRED")

            # Seccomp toolchain is PRESENT, enforcement is INFERRED
            self.assertEqual(readiness["seccomp_bpf_toolchain"]["status"], "PRESENT")
            self.assertEqual(readiness["seccomp_bpf_enforcement"]["status"], "INFERRED")

            # Rootless mapping succeeds when unshare probe succeeds
            self.assertEqual(readiness["rootless_id_mapping"]["status"], "PROBE_SUCCEEDED")

            obs = report["observed"]
            self.assertEqual(obs["caller_identity"]["uid"], 1000)
            self.assertEqual(obs["caller_identity"]["username"], "mockuser")
            self.assertTrue(obs["cgroup_v2"]["cgroup2_mounted"])
            self.assertTrue(obs["cgroup_v2"]["user_slice_writable"])

            text_report = format_text_report(report)
            self.assertIn("[SECTION 1: DIRECTLY OBSERVED FACTS]", text_report)
            self.assertIn("[SECTION 4: PHASE 2 CAPABILITY READINESS STATUS]", text_report)
            self.assertIn("[PRESENT]", text_report)
            self.assertIn("[INFERRED]", text_report)

    def test_synthetic_unavailable_case(self) -> None:
        """
        Verify that missing cgroup2 mount, missing seccomp headers, and unshare
        ENOSYS are cleanly reported as UNAVAILABLE without false positive causes.
        """
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            proc_root = tmp / "proc"
            sys_root = tmp / "sys"
            etc_root = tmp / "etc"
            usr_root = tmp / "usr"

            proc_root.mkdir()
            (proc_root / "version").write_text("Linux version 6.6.0-mock-unavailable\n")
            sys_root.mkdir()
            etc_root.mkdir()
            usr_root.mkdir()

            def mock_unshare(flag: int):
                return -1, 38  # ENOSYS (Function not implemented)

            def mock_prctl(opt: int):
                return -1, 38

            def mock_which(cmd: str):
                return None

            def mock_find_library(name: str):
                return None

            report = run_preflight(
                proc_root=str(proc_root),
                sys_root=str(sys_root),
                etc_root=str(etc_root),
                usr_root=str(usr_root),
                unshare_probe_fn=mock_unshare,
                prctl_probe_fn=mock_prctl,
                which_fn=mock_which,
                find_library_fn=mock_find_library,
                current_uid=1001,
                current_user="testuser",
            )

            readiness = report["readiness"]
            self.assertEqual(readiness["user_namespaces"]["status"], "UNAVAILABLE")
            self.assertIn("errno=38", readiness["user_namespaces"]["detail"])
            self.assertEqual(readiness["pid_namespaces"]["status"], "UNAVAILABLE")
            self.assertEqual(readiness["mount_namespaces"]["status"], "UNAVAILABLE")
            self.assertEqual(readiness["network_namespaces"]["status"], "UNAVAILABLE")
            self.assertEqual(readiness["cgroup_v2_controllers"]["status"], "UNAVAILABLE")
            self.assertEqual(readiness["cgroup_v2_pids_enforcement"]["status"], "UNAVAILABLE")
            self.assertEqual(readiness["cgroup_v2_memory_enforcement"]["status"], "UNAVAILABLE")
            self.assertEqual(readiness["seccomp_bpf_toolchain"]["status"], "UNAVAILABLE")
            self.assertEqual(readiness["rootless_id_mapping"]["status"], "UNAVAILABLE")

            # Check separation of unverified claims from observed facts
            unverified = " ".join(report["unverified"])
            self.assertIn("Observed errno=38 leaves open", unverified)

    def test_synthetic_permission_denied_case(self) -> None:
        """
        Verify that permission-denied conditions (probe EPERM, read-only cgroups)
        are reported cleanly without crashing.
        """
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            proc_root = tmp / "proc"
            sys_root = tmp / "sys"
            etc_root = tmp / "etc"
            usr_root = tmp / "usr"

            # Create mock sys cgroup v2 structure with read-only files
            cg_user = sys_root / "fs" / "cgroup" / "user.slice" / "user-1000.slice" / "user@1000.service"
            cg_user.mkdir(parents=True)
            (sys_root / "fs" / "cgroup" / "cgroup.controllers").write_text("cpu memory pids\n")
            (cg_user / "cgroup.controllers").write_text("cpu memory pids\n")

            procs_file = cg_user / "cgroup.procs"
            procs_file.write_text("100\n")
            os.chmod(str(procs_file), 0o444)  # Read-only

            sub_file = cg_user / "cgroup.subtree_control"
            sub_file.write_text("cpu memory pids\n")
            os.chmod(str(sub_file), 0o444)  # Read-only

            proc_root.mkdir()
            (proc_root / "version").write_text("Linux mock-perm-denied\n")
            etc_root.mkdir()
            usr_root.mkdir()

            def mock_unshare(flag: int):
                return -1, 1  # EPERM (Operation not permitted)

            report = run_preflight(
                proc_root=str(proc_root),
                sys_root=str(sys_root),
                etc_root=str(etc_root),
                usr_root=str(usr_root),
                unshare_probe_fn=mock_unshare,
                which_fn=lambda cmd: None,
                find_library_fn=lambda name: None,
                current_uid=1000,
                current_user="mockuser",
            )

            readiness = report["readiness"]
            self.assertEqual(readiness["user_namespaces"]["status"], "UNAVAILABLE")
            self.assertIn("errno=1", readiness["user_namespaces"]["detail"])
            # cgroup should report unavailable because files are not writable
            self.assertEqual(readiness["cgroup_v2_pids_enforcement"]["status"], "UNAVAILABLE")
            self.assertIn("not writable", readiness["cgroup_v2_pids_enforcement"]["detail"])

    def test_rootless_mapping_unavailable_when_unshare_fails(self) -> None:
        """
        Verify that rootless mapping is NOT labeled ready or supported merely because
        /etc/subuid, /etc/subgid, newuidmap, and newgidmap exist, if unshare(CLONE_NEWUSER)
        returns ENOSYS.
        """
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            proc_root = tmp / "proc"
            sys_root = tmp / "sys"
            etc_root = tmp / "etc"
            usr_root = tmp / "usr"

            proc_root.mkdir()
            sys_root.mkdir()
            etc_root.mkdir()
            usr_root.mkdir()

            (etc_root / "subuid").write_text("mockuser:100000:65536\n")
            (etc_root / "subgid").write_text("mockuser:100000:65536\n")

            # Mock unshare returning ENOSYS
            def mock_unshare(flag: int):
                return -1, 38

            # Mock helper binaries present
            def mock_which(cmd: str):
                if cmd in ["newuidmap", "newgidmap"]:
                    return f"/usr/bin/{cmd}"
                return None

            report = run_preflight(
                proc_root=str(proc_root),
                sys_root=str(sys_root),
                etc_root=str(etc_root),
                usr_root=str(usr_root),
                unshare_probe_fn=mock_unshare,
                which_fn=mock_which,
                current_uid=1000,
                current_user="mockuser",
            )

            readiness = report["readiness"]
            self.assertEqual(readiness["rootless_id_mapping"]["status"], "UNAVAILABLE")
            self.assertIn("unshare(CLONE_NEWUSER) returned errno=38", readiness["rootless_id_mapping"]["detail"])

    def test_cgroups_not_labeled_ready_in_read_only_probe(self) -> None:
        """
        Verify that cgroup enforcement is labeled INFERRED rather than READY
        when controllers and writable paths exist, because read-only probe does not
        write to cgroups or test kernel enforcement.
        """
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            sys_root = tmp / "sys"
            proc_root = tmp / "proc"
            etc_root = tmp / "etc"
            usr_root = tmp / "usr"

            proc_root.mkdir()
            etc_root.mkdir()
            usr_root.mkdir()

            cg_user = sys_root / "fs" / "cgroup" / "user.slice" / "user-1000.slice" / "user@1000.service"
            cg_user.mkdir(parents=True)
            (sys_root / "fs" / "cgroup" / "cgroup.controllers").write_text("cpu memory pids\n")
            (cg_user / "cgroup.controllers").write_text("cpu memory pids\n")
            (cg_user / "cgroup.subtree_control").write_text("cpu memory pids\n")
            (cg_user / "cgroup.procs").write_text("100\n")

            report = run_preflight(
                proc_root=str(proc_root),
                sys_root=str(sys_root),
                etc_root=str(etc_root),
                usr_root=str(usr_root),
                current_uid=1000,
                current_user="mockuser",
            )

            readiness = report["readiness"]
            self.assertNotEqual(readiness["cgroup_v2_pids_enforcement"]["status"], "READY")
            self.assertEqual(readiness["cgroup_v2_pids_enforcement"]["status"], "INFERRED")
            self.assertIn("NOT verified", readiness["cgroup_v2_pids_enforcement"]["detail"])

    def test_live_host_inspection(self) -> None:
        """
        Verify that running against the live host executes cleanly, returns all four
        sections, and adheres to the refined taxonomy without mutating state.
        """
        report = run_preflight()
        self.assertIn("observed", report)
        self.assertIn("unverified", report)
        self.assertIn("inferences", report)
        self.assertIn("readiness", report)

        obs = report["observed"]
        self.assertIn("kernel_version", obs)
        self.assertIn("caller_identity", obs)
        self.assertIn("proc_self_ns", obs)
        self.assertIn("unshare_probe", obs)
        self.assertIn("cgroup_v2", obs)
        self.assertIn("seccomp", obs)
        self.assertIn("rootless_prerequisites", obs)

        # Host observations in our environment
        self.assertTrue(obs["cgroup_v2"]["cgroup2_mounted"])
        self.assertTrue(obs["seccomp"]["header_present"])

        readiness = report["readiness"]
        # Enforce that cgroups and seccomp are NOT overclaimed as READY
        self.assertNotIn("READY", [v["status"] for v in readiness.values()])


if __name__ == "__main__":
    unittest.main()
