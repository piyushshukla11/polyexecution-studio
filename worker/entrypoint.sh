#!/usr/bin/env bash
set -euo pipefail

# PolyExecution Studio - Worker Container Entrypoint
# Moves the Worker daemon into a dedicated child cgroup under the delegated subtree
# so that the parent cgroup remains empty and can delegate controllers to per-run sandboxes.

if [ -z "${PES_DELEGATED_CGROUP:-}" ]; then
    echo "ERROR: PES_DELEGATED_CGROUP is required; refusing to start Worker without cgroup placement." >&2
    exit 70
fi

case "$PES_DELEGATED_CGROUP" in
    /sys/fs/cgroup/user.slice/user-*/user@*.service/pes-worker-sandbox) ;;
    *)
        echo "ERROR: PES_DELEGATED_CGROUP is outside the expected dedicated user-slice subtree." >&2
        exit 70
        ;;
esac

worker_procs="${PES_DELEGATED_CGROUP}/worker/cgroup.procs"
if [ ! -w "$worker_procs" ]; then
    echo "ERROR: Worker child cgroup is absent or not writable: $worker_procs" >&2
    exit 70
fi
if ! printf '%s\n' "$$" > "$worker_procs"; then
    echo "ERROR: could not move Worker into its delegated child cgroup." >&2
    exit 70
fi

expected_cgroup="${PES_DELEGATED_CGROUP#/sys/fs/cgroup}/worker"
actual_cgroup=$(awk -F: '$1 == "0" { print $3 }' /proc/self/cgroup)
if [ "$actual_cgroup" != "$expected_cgroup" ]; then
    echo "ERROR: Worker cgroup placement mismatch (expected '$expected_cgroup', got '$actual_cgroup')." >&2
    exit 70
fi

exec /app/worker/bin/pes-worker "$@"
