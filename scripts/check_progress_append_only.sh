#!/usr/bin/env bash
# Blocks any commit that deletes or modifies existing lines of PROGRESS.md.
set -euo pipefail
F="PROGRESS.md"
git rev-parse --verify HEAD >/dev/null 2>&1 || exit 0   # first commit: nothing to compare
deleted=$(git diff --cached --numstat -- "$F" | awk '{print $2}')
if [ -n "${deleted:-}" ] && [ "$deleted" != "0" ]; then
  echo "BLOCKED: $F is append-only. $deleted line(s) removed or changed."
  echo "Add a CORRECTION entry instead of editing history."
  exit 1
fi
exit 0
