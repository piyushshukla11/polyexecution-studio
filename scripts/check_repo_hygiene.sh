#!/usr/bin/env bash
# Keeps the repo clean: blocks secrets, build output, IDE junk, stray docs, huge files.
set -uo pipefail
fail=0
files=$(git diff --cached --name-only --diff-filter=AM)
[ -z "$files" ] && exit 0

junk=$(echo "$files" | grep -Ei \
  -e '(^|/)\.env(\.|$)' -e '\.(pem|key)$' \
  -e '(^|/)(node_modules|build|dist|out|target|\.gradle|\.idea|\.vscode|__pycache__)/' \
  -e '\.(class|jar|o|so|exe|log|pyc|swp)$' -e '(^|/)core(\.[0-9]+)?$' \
  -e 'sandbox-runner$' -e '\.DS_Store$|Thumbs\.db$' | grep -v '\.env\.example$' || true)
if [ -n "$junk" ]; then echo "BLOCKED: junk/secret/build files staged:"; echo "$junk"; fail=1; fi

stray=$(echo "$files" | grep -E '\.md$' | grep -vE '^(README|AI_RULES|PROGRESS)\.md$|^docs/|^\.github/' || true)
if [ -n "$stray" ]; then echo "BLOCKED: stray markdown files (allowed: README.md, AI_RULES.md, PROGRESS.md, docs/*, .github/*):"; echo "$stray"; fail=1; fi

while IFS= read -r f; do
  [ -f "$f" ] && [ "$(wc -c < "$f")" -gt 1048576 ] && { echo "BLOCKED: $f is larger than 1 MB"; fail=1; }
done <<< "$files"

# no emojis in any staged text file (code, docs, README)
while IFS= read -r f; do
  [ -f "$f" ] && LC_ALL=C.UTF-8 grep -IqP '[\x{1F000}-\x{1FAFF}\x{2600}-\x{27BF}\x{FE0F}]' "$f" 2>/dev/null && { echo "BLOCKED: emoji found in $f (project rule: no emojis)"; fail=1; }
done <<< "$files"

[ $fail -ne 0 ] && echo "Fix: unstage with 'git restore --staged <file>' and add it to .gitignore if needed."
exit $fail
