#!/usr/bin/env python3
"""
PolyExecution Studio — Phase 2 Preflight Capability Audit Tool
Tier C Utility (read-only system inspection)

Inspects the host environment without modifying state, creating namespaces,
writing to cgroups, or executing adversarial payloads.

Reports findings in four strictly separated categories:
  1. Directly Observed Facts
  2. What Could Not Be Verified
  3. Inferences (clearly labeled hypotheses)
  4. Phase 2 Readiness Status
"""

import argparse
import ctypes
import ctypes.util
import json
import os
import pwd
import grp
import shutil
import sys
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

# Linux clone/unshare flags (from linux/sched.h)
CLONE_NEWNS = 0x00020000    # Mount namespace
CLONE_NEWUTS = 0x04000000   # UTS namespace
CLONE_NEWIPC = 0x08000000   # IPC namespace
CLONE_NEWUSER = 0x10000000  # User namespace
CLONE_NEWPID = 0x20000000   # PID namespace
CLONE_NEWNET = 0x40000000   # Network namespace
CLONE_NEWCGROUP = 0x02000000# Cgroup namespace

# Linux prctl constants (from sys/prctl.h)
PR_SET_PDEATHSIG = 1
PR_GET_PDEATHSIG = 2
PR_SET_NO_NEW_PRIVS = 38
PR_GET_NO_NEW_PRIVS = 39


def _default_unshare_probe(flag: int) -> Tuple[int, int]:
    """
    Safely probes libc unshare(flag) inside a transient fork child.
    The parent process remains completely untouched and never changes namespace.
    Returns (return_code, errno).
    """
    try:
        r_fd, w_fd = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(r_fd)
            try:
                libc = ctypes.CDLL(None, use_errno=True)
                ret = libc.unshare(flag)
                err = ctypes.get_errno() if ret != 0 else 0
            except Exception as exc:
                ret, err = -1, -999
            msg = f"{ret},{err}".encode("ascii")
            try:
                os.write(w_fd, msg)
            finally:
                os.close(w_fd)
                os._exit(0)
        else:
            os.close(w_fd)
            data = b""
            while True:
                chunk = os.read(r_fd, 64)
                if not chunk:
                    break
                data += chunk
            os.close(r_fd)
            _, status = os.waitpid(pid, 0)
            if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
                return -1, -1
            parts = data.decode("ascii", errors="replace").split(",")
            if len(parts) == 2:
                return int(parts[0]), int(parts[1])
            return -1, -1
    except Exception:
        return -1, -1


def _default_prctl_probe(option: int) -> Tuple[int, int]:
    """Probes prctl(option) safely using libc."""
    try:
        libc = ctypes.CDLL(None, use_errno=True)
        if option == PR_GET_PDEATHSIG:
            sig = ctypes.c_int(0)
            ret = libc.prctl(option, ctypes.byref(sig), 0, 0, 0)
            err = ctypes.get_errno() if ret < 0 else 0
            return ret, err
        ret = libc.prctl(option, 0, 0, 0, 0)
        err = ctypes.get_errno() if ret < 0 else 0
        return ret, err
    except Exception:
        return -1, -1


def run_preflight(
    proc_root: str = "/proc",
    sys_root: str = "/sys",
    etc_root: str = "/etc",
    usr_root: str = "/usr",
    unshare_probe_fn: Optional[Callable[[int], Tuple[int, int]]] = None,
    prctl_probe_fn: Optional[Callable[[int], Tuple[int, int]]] = None,
    which_fn: Callable[[str], Optional[str]] = shutil.which,
    find_library_fn: Callable[[str], Optional[str]] = ctypes.util.find_library,
    current_uid: Optional[int] = None,
    current_user: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Executes a read-only Phase 2 capability preflight audit.
    All path roots and probe functions are injectable for deterministic testing.
    """
    if unshare_probe_fn is None:
        unshare_probe_fn = _default_unshare_probe
    if prctl_probe_fn is None:
        prctl_probe_fn = _default_prctl_probe
    if current_uid is None:
        current_uid = os.getuid()
    if current_user is None:
        try:
            current_user = pwd.getpwuid(current_uid).pw_name
        except Exception:
            current_user = f"uid-{current_uid}"

    observed: Dict[str, Any] = {}
    unverified: List[str] = []
    inferences: List[str] = []
    readiness: Dict[str, Dict[str, Any]] = {}

    # -------------------------------------------------------------------------
    # 1. Host Kernel and Identity
    # -------------------------------------------------------------------------
    proc_path = Path(proc_root)
    version_file = proc_path / "version"
    if version_file.is_file():
        try:
            observed["kernel_version"] = version_file.read_text().strip()
        except Exception as e:
            observed["kernel_version"] = f"unreadable ({e})"
    else:
        observed["kernel_version"] = "unknown (version file not found)"

    observed["caller_identity"] = {
        "uid": current_uid,
        "username": current_user,
    }

    # -------------------------------------------------------------------------
    # 2. Namespaces & Unshare Capabilities
    # -------------------------------------------------------------------------
    ns_dir = proc_path / "self" / "ns"
    observed_ns: Dict[str, Any] = {}
    if ns_dir.is_dir():
        for item in ["cgroup", "ipc", "mnt", "net", "pid", "time", "user", "uts"]:
            target_symlink = ns_dir / item
            if target_symlink.is_symlink() or target_symlink.exists():
                try:
                    target_val = os.readlink(str(target_symlink))
                    observed_ns[item] = {"present": True, "target": target_val}
                except Exception:
                    observed_ns[item] = {"present": True, "target": "unreadable"}
            else:
                observed_ns[item] = {"present": False, "target": None}
    else:
        observed_ns["error"] = f"{ns_dir} directory not present"
    observed["proc_self_ns"] = observed_ns

    # Check unprivileged userns sysctl
    userns_sysctl = proc_path / "sys" / "kernel" / "unprivileged_userns_clone"
    if userns_sysctl.is_file():
        try:
            observed["unprivileged_userns_clone"] = int(userns_sysctl.read_text().strip())
        except Exception as e:
            observed["unprivileged_userns_clone"] = f"error ({e})"
    else:
        observed["unprivileged_userns_clone"] = "not_present"

    # Probe unshare capability
    unshare_probe_results: Dict[str, Any] = {}
    probed_flags = {
        "CLONE_NEWUSER": CLONE_NEWUSER,
        "CLONE_NEWPID": CLONE_NEWPID,
        "CLONE_NEWNET": CLONE_NEWNET,
        "CLONE_NEWNS": CLONE_NEWNS,
        "CLONE_NEWIPC": CLONE_NEWIPC,
        "CLONE_NEWUTS": CLONE_NEWUTS,
    }
    for flag_name, flag_val in probed_flags.items():
        ret, err = unshare_probe_fn(flag_val)
        unshare_probe_results[flag_name] = {
            "return_code": ret,
            "errno": err,
            "status": "success" if ret == 0 else f"failed (errno={err})",
        }
    observed["unshare_probe"] = unshare_probe_results

    # Namespace readiness & separation of facts from inferences
    user_unshare = unshare_probe_results.get("CLONE_NEWUSER", {})
    if user_unshare.get("return_code") == 0:
        readiness["user_namespaces"] = {
            "status": "READY",
            "detail": "unshare(CLONE_NEWUSER) succeeded in probe without privileges",
        }
    else:
        err = user_unshare.get("errno")
        readiness["user_namespaces"] = {
            "status": "UNAVAILABLE",
            "detail": f"unshare(CLONE_NEWUSER) returned ret={user_unshare.get('return_code')}, errno={err}",
        }
        unverified.append(
            f"Exact cause of unshare(CLONE_NEWUSER) failure (errno={err}). "
            f"Observed errno={err} leaves open whether kernel lacks user namespace support, "
            f"an outer LSM/container seccomp filter intercepts the syscall, or root privileges are strictly required."
        )
        inferences.append(
            f"User namespace creation is unavailable in the current runtime environment. "
            f"Direct unprivileged containerization via unshare(CLONE_NEWUSER) cannot proceed without runtime changes."
        )

    for flag_name, ns_key in [
        ("CLONE_NEWPID", "pid_namespaces"),
        ("CLONE_NEWNET", "network_namespaces"),
        ("CLONE_NEWNS", "mount_namespaces"),
    ]:
        p_res = unshare_probe_results.get(flag_name, {})
        if p_res.get("return_code") == 0:
            readiness[ns_key] = {
                "status": "READY",
                "detail": f"unshare({flag_name}) succeeded in probe",
            }
        else:
            readiness[ns_key] = {
                "status": "UNAVAILABLE",
                "detail": f"unshare({flag_name}) returned errno={p_res.get('errno')}",
            }

    # -------------------------------------------------------------------------
    # 3. Cgroups v2 Mount and Delegation
    # -------------------------------------------------------------------------
    sys_path = Path(sys_root)
    cgroup_root = sys_path / "fs" / "cgroup"
    cgroup_controllers_file = cgroup_root / "cgroup.controllers"

    is_cgroup2 = cgroup_controllers_file.is_file()
    cgroup_info: Dict[str, Any] = {
        "cgroup2_mounted": is_cgroup2,
        "root_controllers": [],
        "user_slice_path": None,
        "user_slice_controllers": [],
        "user_slice_subtree_control": [],
        "user_slice_writable": False,
        "current_process_cgroup": None,
        "current_cgroup_controllers": [],
        "current_cgroup_writable": False,
    }

    if is_cgroup2:
        try:
            cgroup_info["root_controllers"] = cgroup_controllers_file.read_text().split()
        except Exception as e:
            cgroup_info["root_controllers"] = [f"unreadable: {e}"]

        # Process cgroup from /proc/self/cgroup
        self_cgroup_file = proc_path / "self" / "cgroup"
        current_rel_cgroup = ""
        if self_cgroup_file.is_file():
            try:
                for line in self_cgroup_file.read_text().splitlines():
                    if line.startswith("0::"):
                        current_rel_cgroup = line.split("0::", 1)[1].lstrip("/")
                        cgroup_info["current_process_cgroup"] = current_rel_cgroup
                        break
            except Exception as e:
                cgroup_info["current_process_cgroup"] = f"unreadable: {e}"

        # Current process cgroup node in sysfs
        if current_rel_cgroup:
            proc_cg_node = cgroup_root / current_rel_cgroup
            if proc_cg_node.is_dir():
                cg_ctrls = proc_cg_node / "cgroup.controllers"
                if cg_ctrls.is_file():
                    try:
                        cgroup_info["current_cgroup_controllers"] = cg_ctrls.read_text().split()
                    except Exception:
                        pass
                cg_procs = proc_cg_node / "cgroup.procs"
                cgroup_info["current_cgroup_writable"] = os.access(str(cg_procs), os.W_OK) if cg_procs.exists() else False

        # User delegation path
        user_service_path = cgroup_root / "user.slice" / f"user-{current_uid}.slice" / f"user@{current_uid}.service"
        if user_service_path.is_dir():
            cgroup_info["user_slice_path"] = str(user_service_path)
            u_ctrls = user_service_path / "cgroup.controllers"
            if u_ctrls.is_file():
                try:
                    cgroup_info["user_slice_controllers"] = u_ctrls.read_text().split()
                except Exception:
                    pass
            u_sub = user_service_path / "cgroup.subtree_control"
            if u_sub.is_file():
                try:
                    cgroup_info["user_slice_subtree_control"] = u_sub.read_text().split()
                except Exception:
                    pass
            u_procs = user_service_path / "cgroup.procs"
            u_sub_ctrl = user_service_path / "cgroup.subtree_control"
            cgroup_info["user_slice_writable"] = (
                os.access(str(u_procs), os.W_OK) and os.access(str(u_sub_ctrl), os.W_OK)
            )

    observed["cgroup_v2"] = cgroup_info

    # Cgroup readiness evaluation
    avail_controllers = set(cgroup_info.get("user_slice_controllers", [])) | set(cgroup_info.get("current_cgroup_controllers", []))
    is_writable = cgroup_info.get("user_slice_writable") or cgroup_info.get("current_cgroup_writable")

    if not is_cgroup2:
        readiness["cgroup_v2_pids_limit"] = {
            "status": "UNAVAILABLE",
            "detail": f"Unified cgroup v2 filesystem not mounted at {cgroup_root}",
        }
        readiness["cgroup_v2_memory_limit"] = {
            "status": "UNAVAILABLE",
            "detail": f"Unified cgroup v2 filesystem not mounted at {cgroup_root}",
        }
    elif not is_writable:
        readiness["cgroup_v2_pids_limit"] = {
            "status": "UNAVAILABLE",
            "detail": "cgroup v2 mounted but delegation directory is not writable by current user",
        }
        readiness["cgroup_v2_memory_limit"] = {
            "status": "UNAVAILABLE",
            "detail": "cgroup v2 mounted but delegation directory is not writable by current user",
        }
    else:
        if "pids" in avail_controllers:
            readiness["cgroup_v2_pids_limit"] = {
                "status": "READY",
                "detail": f"cgroup v2 pids controller delegated and writable under {cgroup_info.get('user_slice_path') or cgroup_info.get('current_process_cgroup')}",
            }
        else:
            readiness["cgroup_v2_pids_limit"] = {
                "status": "UNAVAILABLE",
                "detail": "pids controller not present in delegated controllers",
            }

        if "memory" in avail_controllers:
            readiness["cgroup_v2_memory_limit"] = {
                "status": "READY",
                "detail": f"cgroup v2 memory controller delegated and writable under {cgroup_info.get('user_slice_path') or cgroup_info.get('current_process_cgroup')}",
            }
        else:
            readiness["cgroup_v2_memory_limit"] = {
                "status": "UNAVAILABLE",
                "detail": "memory controller not present in delegated controllers",
            }

    if is_cgroup2 and is_writable:
        inferences.append(
            f"Cgroup v2 controllers ({', '.join(sorted(avail_controllers))}) are delegated to user {current_user} (UID {current_uid}). "
            f"The sandbox can manage child cgroups and enforce memory.max and pids.max without root privileges."
        )
    elif is_cgroup2 and not is_writable:
        unverified.append(
            "Cgroup v2 hierarchy is mounted on host, but user-level delegation write permissions were not observed."
        )

    # -------------------------------------------------------------------------
    # 4. Seccomp Libraries and Prctl Controls
    # -------------------------------------------------------------------------
    usr_path = Path(usr_root)
    seccomp_header = usr_path / "include" / "seccomp.h"
    seccomp_lib_name = find_library_fn("seccomp")

    lib_version_str = None
    if seccomp_lib_name:
        try:
            cdll = ctypes.CDLL(seccomp_lib_name)
            class ScVer(ctypes.Structure):
                _fields_ = [("major", ctypes.c_uint), ("minor", ctypes.c_uint), ("micro", ctypes.c_uint)]
            cdll.seccomp_version.restype = ctypes.POINTER(ScVer)
            v = cdll.seccomp_version().contents
            lib_version_str = f"{v.major}.{v.minor}.{v.micro}"
        except Exception:
            lib_version_str = "loaded (version call unverified)"

    no_new_privs_ret, no_new_privs_err = prctl_probe_fn(PR_GET_NO_NEW_PRIVS)
    pdeathsig_ret, pdeathsig_err = prctl_probe_fn(PR_GET_PDEATHSIG)

    observed["seccomp"] = {
        "header_present": seccomp_header.is_file(),
        "header_path": str(seccomp_header),
        "library_found": seccomp_lib_name,
        "library_version": lib_version_str,
        "pr_get_no_new_privs": {
            "return_code": no_new_privs_ret,
            "errno": no_new_privs_err,
        },
        "pr_get_pdeathsig": {
            "return_code": pdeathsig_ret,
            "errno": pdeathsig_err,
        },
    }

    if seccomp_header.is_file() and seccomp_lib_name:
        readiness["seccomp_bpf_filtering"] = {
            "status": "READY",
            "detail": f"libseccomp ({lib_version_str or seccomp_lib_name}) and seccomp.h are available",
        }
    else:
        missing_parts = []
        if not seccomp_header.is_file():
            missing_parts.append("seccomp.h header missing")
        if not seccomp_lib_name:
            missing_parts.append("libseccomp library not found")
        readiness["seccomp_bpf_filtering"] = {
            "status": "UNAVAILABLE",
            "detail": "; ".join(missing_parts),
        }

    # -------------------------------------------------------------------------
    # 5. Rootless ID Mapping & Filesystem Setup Prerequisites
    # -------------------------------------------------------------------------
    etc_path = Path(etc_root)
    subuid_file = etc_path / "subuid"
    subgid_file = etc_path / "subgid"

    subuid_entry = None
    if subuid_file.is_file():
        try:
            for line in subuid_file.read_text().splitlines():
                parts = line.strip().split(":")
                if len(parts) >= 3 and (parts[0] == current_user or parts[0] == str(current_uid)):
                    subuid_entry = line.strip()
                    break
        except Exception as e:
            subuid_entry = f"unreadable: {e}"

    subgid_entry = None
    if subgid_file.is_file():
        try:
            for line in subgid_file.read_text().splitlines():
                parts = line.strip().split(":")
                if len(parts) >= 3 and (parts[0] == current_user or parts[0] == str(current_uid)):
                    subgid_entry = line.strip()
                    break
        except Exception as e:
            subgid_entry = f"unreadable: {e}"

    newuidmap_bin = which_fn("newuidmap")
    newgidmap_bin = which_fn("newgidmap")
    pivot_root_bin = which_fn("pivot_root")

    # Inspect nobody user/group
    nobody_uid = None
    nobody_gid = None
    try:
        nobody_uid = pwd.getpwnam("nobody").pw_uid
    except Exception:
        pass
    try:
        nobody_gid = grp.getgrnam("nobody").gr_gid
    except Exception:
        try:
            nobody_gid = grp.getgrnam("nogroup").gr_gid
        except Exception:
            pass

    observed["rootless_prerequisites"] = {
        "subuid_entry": subuid_entry,
        "subgid_entry": subgid_entry,
        "newuidmap_binary": newuidmap_bin,
        "newgidmap_binary": newgidmap_bin,
        "pivot_root_binary": pivot_root_bin,
        "nobody_uid": nobody_uid,
        "nobody_gid": nobody_gid,
    }

    if subuid_entry and subgid_entry and newuidmap_bin and newgidmap_bin:
        readiness["rootless_id_mapping"] = {
            "status": "READY",
            "detail": f"subuid ({subuid_entry}) and newuidmap/newgidmap available",
        }
    else:
        missing = []
        if not subuid_entry:
            missing.append("missing /etc/subuid entry")
        if not subgid_entry:
            missing.append("missing /etc/subgid entry")
        if not newuidmap_bin:
            missing.append("missing newuidmap binary")
        if not newgidmap_bin:
            missing.append("missing newgidmap binary")
        readiness["rootless_id_mapping"] = {
            "status": "UNAVAILABLE",
            "detail": "; ".join(missing),
        }

    if nobody_uid is not None:
        readiness["privilege_dropping"] = {
            "status": "READY",
            "detail": f"Unprivileged nobody account available (UID {nobody_uid}, GID {nobody_gid})",
        }
    else:
        readiness["privilege_dropping"] = {
            "status": "UNAVAILABLE",
            "detail": "nobody unprivileged user account not found in system databases",
        }

    # What could not be verified regarding filesystem setup
    unverified.append(
        "Whether pivot_root(2) syscall will succeed at runtime without setting up a private mount namespace first "
        "(pivot_root requires the caller to have CAP_SYS_ADMIN in its user namespace and a private root mount)."
    )

    return {
        "observed": observed,
        "unverified": unverified,
        "inferences": inferences,
        "readiness": readiness,
    }


def format_text_report(report: Dict[str, Any]) -> str:
    """Formats the audit report into human-readable text with the required 4 sections."""
    lines: List[str] = []
    lines.append("=" * 76)
    lines.append("POLYEXECUTION STUDIO — PHASE 2 PREFLIGHT CAPABILITY AUDIT")
    lines.append("=" * 76)
    lines.append("")

    # Section 1: Directly Observed Facts
    lines.append("[SECTION 1: DIRECTLY OBSERVED FACTS]")
    obs = report.get("observed", {})
    lines.append(f"  * Kernel: {obs.get('kernel_version')}")
    identity = obs.get("caller_identity", {})
    lines.append(f"  * Execution Context: UID {identity.get('uid')} ({identity.get('username')})")

    # Namespaces
    lines.append("  * Namespace Entries (/proc/self/ns):")
    proc_ns = obs.get("proc_self_ns", {})
    if "error" in proc_ns:
        lines.append(f"    - Error: {proc_ns['error']}")
    else:
        present_ns = [k for k, v in proc_ns.items() if isinstance(v, dict) and v.get("present")]
        lines.append(f"    - Available namespace symlinks: {', '.join(present_ns) if present_ns else 'none'}")
    lines.append(f"  * unprivileged_userns_clone sysctl: {obs.get('unprivileged_userns_clone')}")

    # Unshare probe
    lines.append("  * Transient Unshare Probe Results (isolated fork child):")
    unshare_res = obs.get("unshare_probe", {})
    for flag_name, res in unshare_res.items():
        lines.append(f"    - {flag_name:<16}: ret={res.get('return_code')}, errno={res.get('errno')} ({res.get('status')})")

    # Cgroup v2
    lines.append("  * Unified Cgroups v2:")
    cg = obs.get("cgroup_v2", {})
    lines.append(f"    - cgroup2 mounted: {cg.get('cgroup2_mounted')}")
    lines.append(f"    - Root controllers: {' '.join(cg.get('root_controllers', []))}")
    lines.append(f"    - Current process cgroup: {cg.get('current_process_cgroup')}")
    lines.append(f"    - User slice path: {cg.get('user_slice_path')}")
    lines.append(f"    - Delegated controllers: {' '.join(cg.get('user_slice_controllers', []))}")
    lines.append(f"    - User slice writable: {cg.get('user_slice_writable')}")

    # Seccomp
    lines.append("  * Seccomp & Prctl:")
    sc = obs.get("seccomp", {})
    lines.append(f"    - Header: {sc.get('header_path')} (present: {sc.get('header_present')})")
    lines.append(f"    - Library: {sc.get('library_found')} (version: {sc.get('library_version')})")
    p_nnp = sc.get("pr_get_no_new_privs", {})
    lines.append(f"    - PR_GET_NO_NEW_PRIVS: ret={p_nnp.get('return_code')}, errno={p_nnp.get('errno')}")
    p_pds = sc.get("pr_get_pdeathsig", {})
    lines.append(f"    - PR_GET_PDEATHSIG: ret={p_pds.get('return_code')}, errno={p_pds.get('errno')}")

    # Rootless prerequisites
    lines.append("  * Rootless ID Mapping & Tools:")
    rt = obs.get("rootless_prerequisites", {})
    lines.append(f"    - /etc/subuid: {rt.get('subuid_entry') or 'none'}")
    lines.append(f"    - /etc/subgid: {rt.get('subgid_entry') or 'none'}")
    lines.append(f"    - newuidmap binary: {rt.get('newuidmap_binary') or 'not found'}")
    lines.append(f"    - newgidmap binary: {rt.get('newgidmap_binary') or 'not found'}")
    lines.append(f"    - pivot_root binary: {rt.get('pivot_root_binary') or 'not found'}")
    lines.append(f"    - nobody account: UID {rt.get('nobody_uid')}, GID {rt.get('nobody_gid')}")
    lines.append("")

    # Section 2: What Could Not Be Verified
    lines.append("[SECTION 2: WHAT COULD NOT BE VERIFIED]")
    unver = report.get("unverified", [])
    if unver:
        for idx, item in enumerate(unver, 1):
            lines.append(f"  {idx}. {item}")
    else:
        lines.append("  (none)")
    lines.append("")

    # Section 3: Inferences (Clearly Labeled Hypotheses)
    lines.append("[SECTION 3: INFERENCES (LABELED HYPOTHESES)]")
    infs = report.get("inferences", [])
    if infs:
        for idx, item in enumerate(infs, 1):
            lines.append(f"  {idx}. [INFERENCE] {item}")
    else:
        lines.append("  (none)")
    lines.append("")

    # Section 4: Phase 2 Readiness Status
    lines.append("[SECTION 4: PHASE 2 READINESS STATUS]")
    readiness = report.get("readiness", {})
    for key, data in readiness.items():
        st = data.get("status", "UNKNOWN")
        det = data.get("detail", "")
        tag = f"[{st}]"
        lines.append(f"  {tag:<14} {key:<26} -> {det}")
    lines.append("=" * 76)

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="PolyExecution Studio Phase 2 Read-Only Preflight Capability Audit"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output raw audit report in JSON format",
    )
    args = parser.parse_args()

    report = run_preflight()

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(format_text_report(report))

    return 0


if __name__ == "__main__":
    sys.exit(main())
