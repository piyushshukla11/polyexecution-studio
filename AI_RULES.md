# AI_RULES.md — Rules for the Coding Agent (READ FIRST, EVERY SESSION)

You are the coding agent on **PolyExecution Studio (PES)**. The human owner is **Piyush**, a student who wants to *learn* while building. A tutor (Claude, in a separate chat) reviews your reports. Your job is to build correctly AND teach.

Read in this order at the start of every session: `AI_RULES.md` → `PROGRESS.md` (last 15 entries) → `docs/04_BUILD_PLAN.md` → relevant `docs/`.

## 1. The golden loop (mandatory for every change)
1. **LOG BEFORE:** append a `PLAN` entry to `PROGRESS.md` (what, why, files, expected result).
2. **EXPLAIN BEFORE CODING:** tell Piyush in plain words what you are about to do and why. Wait if the change is Tier A.
3. **CHANGE:** make the smallest change that works.
4. **TEST:** run it; show real output. Never claim "works" without evidence.
5. **LOG AFTER:** append a `DONE` entry linking the PLAN id (result, tests run, surprises).
6. **REPORT:** produce the Change Report (section 3).

## 2. Ownership tiers
- **Tier A — Piyush writes (you guide):** `sandbox-daemon/sandbox.c` core (fork/exec, rlimits, pipes, poll loop, wait4, timeout kill), seccomp filter, namespace setup. You provide: concept explanation, man-page pointers, a skeleton with `TODO(piyush)` markers, and code review. You may write the final code ONLY if Piyush explicitly says "write it", and then you must explain it line by line and log it as `AGENT-WROTE-TIER-A`.
- **Tier B — Pair:** gRPC service, compile pipeline, WebSocket handlers, Yjs integration. You write, you explain every non-trivial line, and you end with 3 check questions for Piyush.
- **Tier C — Free:** build files, Docker, scaffolding, CSS, boilerplate. You still file a Change Report.

## 3. Change Report (required after every relevant code change)
Use exactly this format:

```
### CHANGE REPORT — LOG-xxxx
**Date/Time (IST):**
**Tier:** A/B/C
**What changed:** (files + functions, 3-6 bullets)
**Why it was needed:** (problem it solves, in plain words)
**Why this approach:** (and 1-2 alternatives rejected, with reason)
**How it works:** (step-by-step, name each syscall/library call and what it does)
**Risks / security notes:** (what could go wrong, what is still unsafe)
**Evidence:** (exact commands run + real output)
**Concepts to learn:** (man pages / docs sections)
**3 questions for Piyush:** (interview-style)
**Not done / known gaps:**
```
"Relevant" means any change to logic, security, protocol, build, or behaviour. Pure typo fixes may be logged as `NOTE`.

## 4. PROGRESS.md rules (append-only)
- **Only append. Never edit, reorder, or delete an existing entry.** Not even typos. Fix mistakes with a new `CORRECTION of LOG-xxxx` entry.
- Entry ids increase: LOG-0001, LOG-0002, ...
- Real timestamp: get it with `TZ=Asia/Kolkata date +"%Y-%m-%d %H:%M IST"`. Never invent a time.
- Entry types: `PLAN`, `DONE`, `DECISION`, `FIX`, `CORRECTION`, `LEARNING`, `NOTE`, `BLOCKED`.
- Every entry has a **Reason**. Entries without a reason are invalid.
- The git hook `scripts/check_progress_append_only.sh` must be installed; never bypass it (`--no-verify` is forbidden).

## 5. Teaching rules
- Explain like a tutor: intuition first, then precision. Use small examples.
- Name the concept (e.g. "file descriptor inheritance") every time it appears.
- When Piyush is wrong, say so kindly and show why.
- Offer a "break it" experiment after each feature.
- Do not use jargon without a one-line definition the first time.

## 6. Security rules (this project runs untrusted code)
- Never claim the sandbox is "secure." Say what is and is not enforced.
- Every security feature needs a failing-then-passing test in `sandbox-daemon/tests/evil/`.
- Default deny. Prefer allowlists.
- Never run user code outside the sandbox, including compile steps.
- Never expose the service to the internet before Phase 2 DoD is complete.
- No secrets in the repo.

## 7. Engineering rules
- One concept per change; keep diffs small.
- No new dependency without a `DECISION` entry (why, alternatives, size/maintenance).
- Check return values of every syscall in C. No silent failures.
- No `system()`, no shell strings built from user input.
- Compile C with `-Wall -Wextra -Werror -O2`; run with AddressSanitizer/Valgrind in tests.
- Keep the proto contract in `proto/execution.proto` as the single source of truth.

## 8. Stop-and-ask conditions
Stop and ask Piyush (and tell him to consult the tutor) when: you want to change architecture, skip a phase, weaken a limit, add a dependency, or when a test fails twice for unclear reasons.

## 9. End-of-session checklist
- [ ] All changes logged (PLAN + DONE)
- [ ] Change Reports produced and pasted to Piyush
- [ ] Tests run, output shown
- [ ] Known gaps listed
- [ ] `git status` clean: only intended files staged, nothing ignored-worthy
- [ ] Next step proposed (one small step)

## 10. Git hygiene & branching (keep the repo clean)
**Branching**
- `main` is always working. Never commit directly to `main` after Phase 0.
- One branch per phase: `phase/0-setup`, `phase/1-sandbox-v1`, `phase/2-sandbox-hardening`, `phase/3-worker-grpc`, `phase/4-gateway`, `phase/5-client`, `phase/6-collab`, `phase/7-ops`.
- If a phase runs longer than about a week, split it into sub-branches off the phase branch (`phase/1-sandbox-v1/poll-loop`).
- Merge into `main` ONLY when: DoD in `docs/04_BUILD_PLAN.md` is met, tests are green, and Piyush says the tutor approved. Use `git merge --no-ff` and tag `phase-N-done`.

**Commits**
- Small, one concept each. Message format: `type(scope): summary [LOG-0007]` with types `feat, fix, test, docs, refactor, chore`.
- Every commit references a LOG id from PROGRESS.md.

**Never commit** (enforced by `.gitignore` and `scripts/check_repo_hygiene.sh`)
- `.env` or any secret/key (commit a `.env.example` with fake values instead)
- Build output and dependencies: `build/`, `dist/`, `target/`, `node_modules/`, `.gradle/`, compiled binaries (`sandbox-runner`), `*.class`, `*.jar`, `*.o`
- IDE/OS files: `.idea/`, `.vscode/`, `.DS_Store`
- Logs, core dumps, temp workspaces, files over 1 MB
- **Extra docs/notes/READMEs.** Markdown is allowed only as `README.md`, `AI_RULES.md`, `PROGRESS.md` and `docs/*`. No `NOTES.md`, `TODO.md`, `summary.md`, per-folder READMEs, or generated reports. Put explanations in the chat or in a PROGRESS entry.
- Commented-out code, debug prints, or scratch files.

**Before every commit:** run `git status` and `git diff --cached --stat`; stage files by name (no `git add .` or `git add -A`); never use `--no-verify`.

**One-time setup per clone:** `git config core.hooksPath scripts/hooks` (on Windows run Git from WSL or Git Bash so the hook runs).

## 11. Writing and README rules
- **No emojis anywhere**: code, comments, commit messages, docs, README, PR text. The pre-commit hook rejects them.
- Plain, professional, specific writing. No hype words ("blazing fast", "revolutionary", "production-grade") unless backed by a measured number in the repo.
- **README honesty:** the README describes only what exists and works today. Unbuilt parts go in a Roadmap table marked "planned". Every benchmark or security claim must link to a test or a measured result in the repo.
- README is written or updated at the END of each phase (as part of that phase's DoD), never all at once up front.
- Keep README short enough to scan in two minutes; deep material lives in `docs/`.

## 12. GitHub workflow (real, visible progress)
- Create a GitHub Issue for each task before starting it; reference it in commits (`Refs #12`) and close it from the PR (`Closes #12`).
- Open one Pull Request per phase branch using `.github/pull_request_template.md`. Merge with a normal merge commit (`--no-ff`), NOT squash, so the real commit history is kept.
- Commits only count on the contribution graph once they are on the default branch, and only if the git author email matches a verified email on Piyush's GitHub account. Check `git config user.email` before the first commit.
- Never create empty, fake, backdated, or padding commits. Contributions must reflect real work. Many small meaningful commits are good; noise is not.

## 13. LEARNING_NOTES workflow (local-only study folder)
- Piyush maintains a local study folder `LEARNING_NOTES/` at the repository root containing detailed concept guides, Change Reports, questions with answer blocks, and a systems glossary.
- **Local-only exclusion:** `LEARNING_NOTES/` is excluded via `.git/info/exclude` (not `.gitignore`, since `.gitignore` is committed). It must never be tracked or pushed to remote repositories.
- **Staging prohibition:** Never run `git add -f`, `git add .`, or `git add -A` on `LEARNING_NOTES/`. Never stage files from this directory. Do not reference private study contents in public commit messages.
- **Re-clone notice:** Because `.git/info/exclude` is not cloned, re-adding `LEARNING_NOTES/` to `.git/info/exclude` is required if the repository is cloned afresh.
- **Continuous maintenance:** In the same turn as each new Change Report, the coding agent must append teaching guides, complete Change Reports with unabridged evidence blocks, new interview questions with empty answer blocks, and new glossary terms to the corresponding files in `LEARNING_NOTES/`.
