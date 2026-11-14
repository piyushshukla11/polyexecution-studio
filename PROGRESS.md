# PROGRESS.md — APPEND-ONLY LOG

## RULES (do not edit this section after LOG-0001)
1. **Append only.** Existing entries are never changed, reordered or deleted. Mistake? Add a `CORRECTION of LOG-xxxx` entry.
2. Every entry has: id, real date+time (IST), type, actor, reason, details.
3. Write a `PLAN` entry BEFORE a change, a `DONE` entry AFTER it (linking the PLAN id).
4. Get time with: `TZ=Asia/Kolkata date +"%Y-%m-%d %H:%M IST"`
5. Enforced by `scripts/check_progress_append_only.sh` (git pre-commit hook).
6. Types: PLAN | DONE | DECISION | FIX | CORRECTION | LEARNING | NOTE | BLOCKED

## ENTRY TEMPLATE
```
## [LOG-0000] YYYY-MM-DD HH:MM IST | TYPE | actor: agent|piyush|tutor
- Reason:
- Details / Change:
- Files:
- Linked: (LOG ids)
- Piyush prediction (optional):
- Tutor review: pending
```

---

## [LOG-0001] 2026-11-13 17:43 IST | DECISION | actor: tutor
- Reason: Establish the project's documentation, rules and logging system before any code is written.
- Details / Change: Created AI_RULES.md, PROGRESS.md, docs/01-06, scripts/check_progress_append_only.sh, HANDOFF_PROMPT.md. Build order changed from the original README: sandbox hardening moved from Phase 5 to Phase 2; single WebSocket channel for run requests.
- Files: all files in this pack
- Linked: none
- Tutor review: n/a

## [LOG-0002] 2026-11-13 17:49 IST | DECISION | actor: tutor
- Reason: Piyush wants a clean git history and will give the agent a fresh repo with one branch per phase; the pack needed enforceable git rules.
- Details / Change: Added AI_RULES section 10 (branching, commit format, never-commit list, no stray docs), .gitignore, scripts/check_repo_hygiene.sh and scripts/hooks/pre-commit (runs the append-only check and the hygiene check). Hook install is now a one-time 'git config core.hooksPath scripts/hooks'. HANDOFF_PROMPT.md is git-ignored. Hooks were tested in a temp repo: a PROGRESS.md edit, a staged .env and a stray NOTES.md were all rejected.
- Files: AI_RULES.md, .gitignore, README.md, scripts/*
- Linked: LOG-0001
- Tutor review: n/a

## [LOG-0003] 2026-11-13 17:57 IST | DECISION | actor: tutor
- Reason: Piyush wants a clean, professional GitHub presence: a good repo name, an emoji-free README, and visible contribution activity earned honestly.
- Details / Change: Added AI_RULES sections 11 (no emojis, README honesty, README updated per phase) and 12 (issues, one PR per phase, merge commits not squash, author email must match GitHub, no fake commits). Added .github/pull_request_template.md. Hygiene hook now rejects emojis and allows .github/ markdown. Recommended repo name: polyexecution-studio.
- Files: AI_RULES.md, scripts/check_repo_hygiene.sh, .github/pull_request_template.md
- Linked: LOG-0002
- Tutor review: n/a

## [LOG-0004] 2026-11-13 18:50 IST | PLAN | actor: agent
- Reason: Verify git hooks enforcement (append-only PROGRESS.md check and repo hygiene check) for Phase 0 Definition of Done.
- Details / Change: Test pre-commit hook behavior against invalid operations without polluting git history: (a) reject modification/deletion of old lines in PROGRESS.md, (b) reject staging .env, (c) reject staging stray markdown NOTES.md. After verifying negative cases, append a DONE log entry and prepare PR for phase/0-setup.
- Files: PROGRESS.md
- Linked: LOG-0001, LOG-0002, LOG-0003
- Piyush prediction (optional): Pre-commit hook will exit with non-zero status and abort commit on all three violations.
- Tutor review: pending

## [LOG-0005] 2026-11-13 18:51 IST | DONE | actor: agent
- Reason: Record verified git hooks enforcement and completion of Phase 0 setup.
- Details / Change: Verified all git hook constraints on phase/0-setup branch:
  1. Modifying an existing line of PROGRESS.md was blocked with 'BLOCKED: PROGRESS.md is append-only. 1 line(s) removed or changed.'.
  2. Staging a .env secret file was blocked with 'BLOCKED: junk/secret/build files staged: .env'.
  3. Staging a stray NOTES.md markdown file was blocked with 'BLOCKED: stray markdown files (allowed: README.md, AI_RULES.md, PROGRESS.md, docs/*, .github/*): NOTES.md'.
  All throwaway changes were unstaged and removed cleanly. Ready for Phase 0 PR submission.
- Files: PROGRESS.md
- Linked: LOG-0004
- Piyush prediction (optional): Pre-commit hook will exit with non-zero status and abort commit on all three violations.
- Tutor review: pending

## [LOG-0006] 2026-11-13 19:50 IST | PLAN | actor: agent
- Reason: Set up Phase 1 Step 1 scaffolding, provide Tier A conceptual instruction, provide skeleton files with TODO(piyush) markers, and add evil test fixtures.
- Details / Change:
  1. Commit this PLAN entry alone to demonstrate intent before code changes.
  2. Provide teaching guide for fork, execve (vs execvp), wait4 (and zombies), and status decoding macros.
  3. Create sandbox-daemon/Makefile (-Wall -Wextra -Werror -O2, clean target).
  4. Create sandbox-daemon/sandbox.h and sandbox-daemon/sandbox.c skeleton with clear TODO(piyush) markers for child process creation, execution, waiting, and exit status decoding (no implementation).
  5. Create sandbox-daemon/tests/evil/loop.c and sandbox-daemon/tests/evil/sleeper.c test programs.
  6. Commit changes in small steps referencing LOG id and issue #3, push branch, and submit Change Report without opening PR or advancing to Tier A implementation.
- Files: PROGRESS.md, sandbox-daemon/Makefile, sandbox-daemon/sandbox.h, sandbox-daemon/sandbox.c, sandbox-daemon/tests/evil/loop.c, sandbox-daemon/tests/evil/sleeper.c
- Linked: LOG-0005
- Piyush prediction (optional): Skeleton and test fixtures will compile cleanly and wait for Piyush to implement core Tier A process logic.
- Tutor review: pending

## [LOG-0007] 2026-11-13 20:18 IST | PLAN | actor: agent
- Reason: Establish local-only LEARNING_NOTES study directory and add Section 13 to AI_RULES.md.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before making changes.
  2. Add Section 13 to AI_RULES.md defining the local study folder workflow: excluded via .git/info/exclude, uncommitted, unstaged, and updated each turn.
  3. Create local LEARNING_NOTES/ directory with 00_tutor_notes.md, glossary.md, phase-0.md, and phase-1.md containing full teaching material, Change Reports with evidence, and questions with unfilled answer blocks.
  4. Append a DONE entry linking this PLAN (LOG-0007), commit AI_RULES.md and PROGRESS.md, and push branch.
- Files: PROGRESS.md, AI_RULES.md
- Linked: LOG-0006
- Piyush prediction (optional): LEARNING_NOTES will be ignored by git via .git/info/exclude and remain purely local.
- Tutor review: pending

## [LOG-0008] 2026-11-13 20:19 IST | DONE | actor: agent
- Reason: Record completion of AI_RULES.md Section 13 update and local LEARNING_NOTES study directory setup.
- Details / Change:
  1. Added Section 13 to AI_RULES.md formalizing the local-only study folder workflow, .git/info/exclude rule, and continuous maintenance duties.
  2. Created LEARNING_NOTES/ directory at repo root excluded by .git/info/exclude.
  3. Created 00_tutor_notes.md, glossary.md, phase-0.md, and phase-1.md populated with unabridged teaching guides, Change Reports with full evidence, empty question/answer blocks, and systems definitions.
  4. Verified git check-ignore and git status to confirm zero tracking or leakage into version control.
- Files: AI_RULES.md, PROGRESS.md
- Linked: LOG-0007
- Piyush prediction (optional): LEARNING_NOTES will be ignored by git via .git/info/exclude and remain purely local.
- Tutor review: pending

## [LOG-0009] 2026-11-13 20:39 IST | PLAN | actor: agent
- Reason: Execute combined tutor task covering Part A (governance follow-ups from LOG-0008), Part B (Step 1 test fixtures, Makefile evil target, .gitignore update, and testing guide), and Part C (phase overview).
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1.
  2. Part A: Verify LEARNING_NOTES study directory state, prove second-line defence against forced staging (-f) with check_repo_hygiene.sh, label all Phase 0 evidence blocks in phase-0.md with source tags, verify AI_RULES.md Section 13, and inspect Issue #4.
  3. Part B: Create sandbox-daemon/tests/evil/exit7.c and sandbox-daemon/tests/evil/segv.c. Update sandbox-daemon/Makefile with evil target (compiling to tests/evil/bin/ with -O0) and clean target. Update .gitignore with sandbox-daemon/sandbox-runner, sandbox-daemon/*.o, and sandbox-daemon/tests/evil/bin/. Verify build and execution behavior of exit7 (7) and segv (139). Expand LEARNING_NOTES/phase-1.md with Step 1 testing guide and 8 experiments. Add envp to glossary.md.
  4. Part C: Create LEARNING_NOTES/phase-overview.md mapping Phases 0 through 7 with definitions of done, tiers, dependencies, and rationale from docs/04_BUILD_PLAN.md. Note any architecture inconsistencies.
  5. Step 1 Definition of Done:
     - ./sandbox-runner ./tests/evil/bin/exit7 prints exit code 7
     - ./sandbox-runner ./tests/evil/bin/segv prints signal 11
     - Killing a running child with kill -9 from another terminal prints signal 9
     - No zombie processes left behind
  6. Append DONE entry (LOG-0010), commit in small steps (fixtures, Makefile/.gitignore, DONE), push branch, and submit full Change Report.
- Files: PROGRESS.md, sandbox-daemon/tests/evil/exit7.c, sandbox-daemon/tests/evil/segv.c, sandbox-daemon/Makefile, .gitignore, LEARNING_NOTES/phase-0.md, LEARNING_NOTES/phase-1.md, LEARNING_NOTES/glossary.md, LEARNING_NOTES/phase-overview.md
- Linked: LOG-0008
- Piyush prediction (optional): Fixtures will build cleanly into tests/evil/bin/ and demonstrate expected shell status codes (7 and 139) prior to sandbox-runner integration.
- Tutor review: pending

## [LOG-0010] 2026-11-13 20:45 IST | DONE | actor: agent
- Reason: Complete tutor task covering Part A (governance follow-ups), Part B (test fixtures and testing guide), and Part C (phase overview).
- Details / Change:
  1. Part A: Verified LEARNING_NOTES study directory integrity (4 files, 650 lines initially). Proved second line of defence by force-staging LEARNING_NOTES/00_tutor_notes.md and verifying both check_repo_hygiene.sh and pre-commit hook blocked commit. Labeled all Phase 0 evidence blocks in phase-0.md with source tags (4 recovered, 3 reconstructed) and added recovery limitations. Verified AI_RULES.md Section 13 and confirmed Issue #4 via gh CLI.
  2. Part B: Created sandbox-daemon/tests/evil/exit7.c and sandbox-daemon/tests/evil/segv.c. Updated sandbox-daemon/Makefile with evil target (O0 flags, tests/evil/bin/) and clean target. Updated .gitignore to exclude sandbox-daemon/*.o and sandbox-daemon/tests/evil/bin/. Verified compilation and shell exit codes: exit7 yielded 7, and segv yielded 139 (128 + signal 11). Expanded LEARNING_NOTES/phase-1.md with Step 1 Definition of Done, 3 operational tasks, and 5 blank experiment slots for Piyush. Added envp definition to glossary.md.
  3. Part C: Created LEARNING_NOTES/phase-overview.md mapping Phases 0 through 7 with verbatim Definitions of Done, ownership tiers, dependencies, and sizing rationale from docs/04_BUILD_PLAN.md. Noted architectural consistency items.
  4. Step 1 Definition of Done recorded for Piyush's upcoming Tier A implementation:
     - ./sandbox-runner ./tests/evil/bin/exit7 prints exit code 7
     - ./sandbox-runner ./tests/evil/bin/segv prints signal 11
     - Killing a running child with kill -9 from another terminal prints signal 9
     - No zombie processes left behind
- Files: PROGRESS.md, sandbox-daemon/tests/evil/exit7.c, sandbox-daemon/tests/evil/segv.c, sandbox-daemon/Makefile, .gitignore, LEARNING_NOTES/phase-0.md, LEARNING_NOTES/phase-1.md, LEARNING_NOTES/glossary.md, LEARNING_NOTES/phase-overview.md
- Linked: LOG-0009
- Piyush prediction (optional): Fixtures will build cleanly into tests/evil/bin/ and demonstrate expected shell status codes (7 and 139) prior to sandbox-runner integration.
- Tutor review: pending

## [LOG-0011] 2026-11-13 20:57 IST | PLAN | actor: agent
- Reason: Execute Tutor Instruction Follow-Up Round 2 covering governance hardening, evil binary ignore validation, Phase 0 evidence re-run, core dump check, environment assessment, issue timing, study notes audit, and Tier A integrity.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1.
  2. Item 1: Harden scripts/check_repo_hygiene.sh. Inspect script and hook wiring (1a), prove the hole with LEARNING_NOTES/_hook_test.c allowed (1b), fix script to block any staged path starting with LEARNING_NOTES/ regardless of extension using diff without --diff-filter with dedicated error message (1c), prove fix with T1, T2, T3, T4 (1d), and commit script fix alone (1e).
  3. Item 2: Build evil binaries and prove they are ignored via git status, git check-ignore, and git ls-files.
  4. Item 3: Re-run the 3 historical Phase 0 negative tests for real (R1: edit old PROGRESS.md log line, R2: staged .env secret, R3: stray NOTES.md), paste real outputs, clean up, and update phase-0.md labeling them recovered on 2026-11-13.
  5. Item 4: Check for stray core dump files; report kernel core_pattern and coredumpctl.
  6. Item 5: Produce Linux host environment report (kernel, user, umask, filesystem, namespaces, cgroups, permissions analysis).
  7. Item 6: Verify Issue #4 creation timing relative to commits referencing it.
  8. Item 7: Audit LEARNING_NOTES/phase-1.md structure, clarify experiment count, verify stdio demo location, and report final line counts.
  9. Item 8: Verify Tier A integrity in sandbox-daemon/sandbox.c and sandbox.h remains completely untouched.
  Note: CI is a known gap, deliberately deferred by the tutor until Piyush passes the Step 1 Definition of Done. Do not add CI this round.
  10. Append DONE entry (LOG-0012), commit, push branch, and submit Change Report.
- Files: PROGRESS.md, scripts/check_repo_hygiene.sh, LEARNING_NOTES/phase-0.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0010
- Piyush prediction (optional): scripts/check_repo_hygiene.sh will effectively block any forced additions under LEARNING_NOTES/ regardless of extension.
- Tutor review: pending

## [LOG-0012] 2026-11-13 21:02 IST | DONE | actor: agent
- Reason: Complete Follow-Up Round 2 covering hook hardening, evil binary ignore validation, Phase 0 evidence re-run, core dump audit, environment report, issue timing check, study notes audit, and Tier A integrity proof.
- Details / Change:
  1. Item 1: Hardened scripts/check_repo_hygiene.sh. Proved old script allowed non-markdown files under LEARNING_NOTES/ (exit 0). Updated script to block any staged path starting with LEARNING_NOTES/ without --diff-filter with dedicated instruction message. Proved fix with T1 (blocked), T2 (blocked), T3 (allowed), T4 (commit passed). Committed fix alone.
  2. Item 2: Verified evil binaries in tests/evil/bin/ are ignored by .gitignore (git status clean, git check-ignore confirmed, git ls-files clean).
  3. Item 3: Re-ran 3 historical Phase 0 negative tests for real (R1: old PROGRESS.md line edit, R2: staged .env secret, R3: stray NOTES.md). Replaced reconstructed evidence in LEARNING_NOTES/phase-0.md with real outputs labeled recovered on 2026-11-13 (new counts: 7 recovered, 0 reconstructed).
  4. Item 4: Verified zero stray core dumps in repository. Confirmed kernel core_pattern pipes to systemd-coredump and .gitignore contains core rules.
  5. Item 5: Generated Linux environment report (kernel 7.2.8, btrfs, umask 0000, unprivileged userns enabled, cgroup2fs active).
  6. Item 6: Verified Issue #4 was created at 20:17:49 IST, preceding commits 8ee9ffc (20:18:53 IST) and f8d13f7 (20:21:30 IST).
  7. Item 7: Audited LEARNING_NOTES/phase-1.md structure, clarified 4 experiments and 1 envp note, confirmed throwaway stdio test location, reported 1045 total lines.
  8. Item 8: Confirmed Tier A files sandbox-daemon/sandbox.c and sandbox.h remain completely untouched (single skeleton commit 210cc62).
  Note: CI is a known gap, deliberately deferred by the tutor until Piyush passes the Step 1 Definition of Done. Do not add CI this round.
- Files: PROGRESS.md, scripts/check_repo_hygiene.sh, LEARNING_NOTES/phase-0.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0011
- Piyush prediction (optional): scripts/check_repo_hygiene.sh will effectively block any forced additions under LEARNING_NOTES/ regardless of extension.
- Tutor review: pending

## [LOG-0013] 2026-11-13 22:31 IST | PLAN | actor: agent
- Reason: Execute Tutor Instruction Follow-Up Round 3 covering hygiene script error message refinement, deletion check verification, Phase 2 host environment checks, and study notes updates.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1.
  2. Item 1: Display git show 9204813 -- scripts/check_repo_hygiene.sh and cat -n scripts/check_repo_hygiene.sh.
  3. Item 2: Fix hygiene script error output: eliminate duplicate LEARNING_NOTES message and prevent .gitignore unstage suggestion when LEARNING_NOTES rule triggers. Show before and after output.
  4. Item 3: Prove rule via real hook using _hook_test.c (confirm blocked, log unchanged, clean status).
  5. Item 4: Verify staged file deletion is not broken by removing --diff-filter.
  6. Item 5: Run report-only environment checks for Phase 2 (cgroup delegation, nobody group, umask origins) and provide 3-5 sentence analysis.
  7. Item 6: Add Phase 2 notes section to LEARNING_NOTES/phase-overview.md (umask 0000 implications, RLIMIT_CORE = 0, cgroup delegation result).
  8. Item 7: Add note in LEARNING_NOTES documenting that core.hooksPath and .git/info/exclude are per-clone settings needing a future setup script.
  Note: Do not touch sandbox-daemon/sandbox.c or sandbox.h.
  9. Append DONE entry (LOG-0014), commit script changes, push branch, and submit Change Report.
- Files: PROGRESS.md, scripts/check_repo_hygiene.sh, LEARNING_NOTES/phase-overview.md, LEARNING_NOTES/00_tutor_notes.md
- Linked: LOG-0012
- Piyush prediction (optional): Scripts will cleanly report LEARNING_NOTES rejections without conflicting advice, and staged deletions of allowed files will succeed.
- Tutor review: pending

## [LOG-0014] 2026-11-13 22:37 IST | DONE | actor: agent
- Reason: Complete Follow-Up Round 3 covering hygiene error output refinement, deletion verification, Phase 2 host environment checks, and study notes updates.
- Details / Change:
  1. Item 1: Displayed git show 9204813 -- scripts/check_repo_hygiene.sh and cat -n scripts/check_repo_hygiene.sh.
  2. Item 2: Fixed hygiene script error output to eliminate duplicate LEARNING_NOTES message and prevent .gitignore suggestion when LEARNING_NOTES rule triggers. Demonstrated BEFORE and AFTER outputs for staged _hook_test.c. Committed fix alone.
  3. Item 3: Proved rule through real pre-commit hook with _hook_test.c (blocked with single message, commit prevented, HEAD unchanged, clean tree).
  4. Item 4: Verified staged deletions of allowed files are not blocked by the absence of --diff-filter on the LEARNING_NOTES check (tested deleting docs/01_PROJECT_OVERVIEW.md with exit 0, restored cleanly).
  5. Item 5: Ran Phase 2 environment checks: confirmed cgroup v2 delegation to user 1000 with cpu, memory, and pids controllers; determined umask 0000 source from container session manager; identified group nobody (65534) membership.
  6. Item 6: Added Phase 2 notes section to LEARNING_NOTES/phase-overview.md covering runner umask requirement, RLIMIT_CORE = 0, and cgroup delegation.
  7. Item 7: Added clone setup note in LEARNING_NOTES/00_tutor_notes.md regarding core.hooksPath and .git/info/exclude persistence.
  Note: Confirmed Tier A files sandbox-daemon/sandbox.c and sandbox.h remain untouched.
- Files: PROGRESS.md, scripts/check_repo_hygiene.sh, LEARNING_NOTES/phase-overview.md, LEARNING_NOTES/00_tutor_notes.md
- Linked: LOG-0013
- Piyush prediction (optional): Scripts will cleanly report LEARNING_NOTES rejections without conflicting advice, and staged deletions of allowed files will succeed.
- Tutor review: pending

## [LOG-0015] 2026-11-14 00:00 IST | PLAN | actor: agent
- Reason: Record Piyush's decision to change working mode for Phase 1 Step 1 (agent writes Tier A core code, tutor teaches theory, Piyush explains code back; tagged AGENT-WROTE-TIER-A) and plan the implementation of process lifecycle, execution, and status decoding in sandbox-daemon/sandbox.c.
- Details / Change:
  1. Record CORRECTION to LOG-0014: in Item 5 of LOG-0014, attributing ambient umask 0000 to the container session manager and the explanation of group nobody (65534) membership were Inferred rather than directly Observed; from now on all claims are explicitly labeled Observed, Inferred, or Unknown.
  2. Record working mode shift per Piyush's wish: Tier A sandbox core implementation moves from "Piyush writes" to "agent writes, tutor teaches, Piyush must explain it back", tagged with AGENT-WROTE-TIER-A per AI_RULES.md Section 2.
  3. Implement Step 1 process lifecycle across small commits:
     (a) Implement fork() + child _exit(3) + parent wait4() loop handling EINTR, error handling if fork() returns -1, and basic status collection.
     (b) Implement child execve() with explicit binary path, argv = { binary_path, NULL }, and minimal fixed envp = { NULL }; handle execve() failure via write(2, ...) error message and _exit(127).
     (c) Implement full status decoding: check WIFEXITED(status) and print WEXITSTATUS(status); check WIFSIGNALED(status) and print WTERMSIG(status); never print raw status integer.
  4. Build and verify with test binaries (exit7, segv, sleeper with kill -9, non-existent binary) and verify absence of zombie processes with ps.
  5. Add beginner-friendly line-by-line walkthrough of sandbox.c and system calls to LEARNING_NOTES/phase-1.md, emphasizing that Step 1 provides no isolation or resource limits.
  6. Commit each change incrementally, stage files by name only, append DONE entry (LOG-0016), and submit Change Report.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/sandbox.h, LEARNING_NOTES/phase-1.md
- Linked: LOG-0014
- Piyush prediction (optional): Step 1 execution will correctly report exit codes and signals for test binaries, handle failed execve with code 127 without crashing, and leave no zombie processes.
- Tutor review: pending

## [LOG-0016] 2026-11-14 00:07 IST | DONE | actor: agent
- Reason: Complete Phase 1 Step 1 sandbox process lifecycle implementation (fork, child execve, parent wait4, status decoding) under AGENT-WROTE-TIER-A per Piyush's decision.
- Details / Change:
  1. Implemented Phase 1 Step 1 in sandbox-daemon/sandbox.c across three small commits:
     - (a) fork + child _exit(3) baseline + parent wait4 with EINTR retry and fork == -1 guard (commit 716826d).
     - (b) child execve with explicit path, argv, minimal envp = { NULL }, and write(2, ...) + _exit(127) error reporting (commit 4a1ae01).
     - (c) full status decoding using WIFEXITED/WEXITSTATUS and WIFSIGNALED/WTERMSIG, never printing raw status (commit 33ab46c).
  2. Updated sandbox-daemon/sandbox.h with clear documentation.
  3. Added extensive beginner-friendly comments explaining every system call (fork, _exit vs exit, execve vs execvp, write vs printf, wait4 and EINTR loop, bitfield status decoding).
  4. Executed verification tests and observed real outputs:
     - ./sandbox-runner ./tests/evil/bin/exit7 produced 'exit code 7' (Observed).
     - ./sandbox-runner ./tests/evil/bin/segv produced 'signal 11' (Observed).
     - ./sandbox-runner ./tests/evil/bin/does-not-exist produced 'execve failed' and 'exit code 127' (Observed).
     - Background runner with sleeper killed by kill -9 to child PID produced 'signal 9' (Observed).
     - ps confirmed zero zombie processes (Observed).
  5. Appended detailed line-by-line beginner-friendly walkthrough of sandbox.c and complete Change Report to LEARNING_NOTES/phase-1.md. Explicitly noted that Step 1 has no isolation.
  6. Confirmed all factual statements are labeled Observed, Inferred, or Unknown.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/sandbox.h, LEARNING_NOTES/phase-1.md
- Linked: LOG-0015
- Piyush prediction (optional): Step 1 execution will correctly report exit codes and signals for test binaries, handle failed execve with code 127 without crashing, and leave no zombie processes.
- Tutor review: pending

## [LOG-0017] 2026-11-14 01:05 IST | PLAN | actor: agent
- Reason: Implement Phase 1 Step 2 sandbox core hardening (CLOEXEC error pipe with setup_error_t record, setsid process group cleanup, non-blocking pipe I/O capture, poll event loop with monotonic wall timeout and continuous child liveness probing, kill-on-cap output flood policy, and child rlimits) authorized by Piyush under AGENT-WROTE-TIER-A.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1.
  2. Implement Step 2 in small, reviewable commits:
     (a) Normalize initially closed standard descriptors (0, 1, 2) to /dev/null; implement pipe2(O_CLOEXEC) error pipe with setup_error_t struct (stage + errno) and full-read/write loops; add exit127 fixture.
     (b) Implement child setsid() session/process-group leader and parent kill(-child_pid, SIGKILL) best-effort cleanup helper; create non-blocking stdout/stderr pipes, dup2 in child, and close unused descriptors.
     (c) Implement poll-based event loop using CLOCK_MONOTONIC with continuous wait4(..., WNOHANG) child liveness probing; handle EINTR and ECHILD; enforce 2000 ms wall timeout; implement 64 KB kill-on-cap output flood policy with bounded non-blocking drain; add fixtures for stream separation, output flood, early stream close, and descendant pipe holding.
     (d) Configure in-child resource limits via setrlimit: RLIMIT_CPU (soft 1s, hard 2s), RLIMIT_AS (128 MiB), RLIMIT_FSIZE (1 MiB), RLIMIT_NOFILE (64), RLIMIT_CORE (0); add fixtures for memory bomb and file size limit.
  3. Execute focused acceptance tests, verify absence of zombie processes and orphan test descendants, and append DONE entry (LOG-0018).
- Files: PROGRESS.md, sandbox-daemon/sandbox.h, sandbox-daemon/sandbox.c, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/*, LEARNING_NOTES/phase-1.md
- Linked: LOG-0016
- Piyush prediction (optional): Step 2 will cleanly distinguish execve failure from exit code 127, capture I/O without deadlocks, terminate long sleeps and output floods without hanging, and enforce POSIX limits on child processes.
- Tutor review: pending

## [LOG-0018] 2026-11-14 01:10 IST | DONE | actor: agent
- Reason: Complete Phase 1 Step 2 sandbox hardening (error pipe with setup_error_t, setsid process group cleanup, non-blocking pipe capture, poll event loop with monotonic timeout and continuous child liveness checking, kill-on-cap output flood policy, and child rlimits) under AGENT-WROTE-TIER-A.
- Details / Change:
  1. Implemented setup error pipe (pipe2 with O_CLOEXEC) and setup_error_t (stage + errno) in sandbox-daemon/sandbox.c (commit 06c4c63). Verified missing binary returns stage=10 errno=2, and exit127 fixture returns exit code 127.
  2. Implemented setsid() process-group leader setup and kill(-pgid, SIGKILL) best-effort group cleanup (commit 2ca0e4e).
  3. Implemented non-blocking stdout/stderr pipe capture with descriptor normalization (ensuring fds 0, 1, 2 valid) and clean dup2 redirection.
  4. Implemented poll-based event loop using CLOCK_MONOTONIC with continuous wait4(..., WNOHANG) direct-child liveness probing. Verified that early-closed streams still respect wall timeout (close_stream_sleep -> TIMEOUT), and that direct-child exit reaps cleanly without hanging when a descendant keeps a pipe open (descendant_pipe -> exit code 42).
  5. Implemented 64 KiB kill-on-cap output flood policy with bounded non-blocking pipe draining (output_flood -> OUTPUT_LIMIT_EXCEEDED).
  6. Configured child rlimits before execve: RLIMIT_CPU (soft 1s, hard 2s -> loop produces signal 24 SIGXCPU), RLIMIT_AS (128 MiB -> memory_bomb produces signal 11 SIGSEGV), RLIMIT_FSIZE (1 MiB -> file_size_limit produces signal 25 SIGXFSZ), RLIMIT_CORE (0), RLIMIT_NOFILE (64).
  7. Confirmed zero zombie processes and zero orphan test processes remaining on the host.
- Files: PROGRESS.md, sandbox-daemon/sandbox.h, sandbox-daemon/sandbox.c, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/*
- Linked: LOG-0017
- Piyush prediction (optional): Step 2 will cleanly distinguish execve failure from exit code 127, capture I/O without deadlocks, terminate long sleeps and output floods without hanging, and enforce POSIX limits on child processes.
- Tutor review: pending

## [LOG-0019] 2026-11-14 01:11 IST | PLAN | actor: agent
- Reason: Implement Phase 1 Step 3 JSON execution result contract conforming to docs/02_ARCHITECTURE.md (ExecuteResponse schema: job_id, status, stdout, stderr, exit_code, signal, wall_ms, cpu_ms, peak_memory_kb) and docs/04_BUILD_PLAN.md Definition of Done.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1.
  2. Implement RFC 8259 compliant JSON string escaping in sandbox-daemon/sandbox.c for stdout, stderr, and error strings (handling control characters, quotes, backslashes, and byte values).
  3. Map internal execution results into the ExecuteResponse status enum: OK, RUNTIME_ERROR, TIMEOUT, OUTPUT_LIMIT, MEMORY_LIMIT, SANDBOX_ERROR.
  4. Implement CLI argument parsing for runner flags (--job-id, --timeout-ms, --cpu-soft, --cpu-hard, --as-mb, --fsize-mb, --max-output-kb).
  5. Format and emit single structured JSON object on stdout upon completion; ensure clean separation between child output streams and runner telemetry.
  6. Verify JSON validity and schema across all test fixtures using python3 -m json.tool.
  7. Append DONE entry (LOG-0020), update LEARNING_NOTES/phase-1.md, and push branch.
- Files: PROGRESS.md, sandbox-daemon/sandbox.h, sandbox-daemon/sandbox.c, LEARNING_NOTES/phase-1.md
- Linked: LOG-0018
- Piyush prediction (optional): Step 3 will emit strictly valid JSON across all evil fixtures, parsing correctly with JSON tools and accurately capturing stdout, stderr, resource timings, and peak memory.
- Tutor review: pending

## [LOG-0020] 2026-11-14 01:13 IST | DONE | actor: agent
- Reason: Complete Phase 1 Step 3 JSON execution result contract conforming to docs/02_ARCHITECTURE.md (ExecuteResponse schema) and docs/04_BUILD_PLAN.md Definition of Done.
- Details / Change:
  1. Implemented RFC 8259 compliant JSON string escaping for stdout, stderr, and error strings in sandbox-daemon/sandbox.c (commit 4e2fd7d).
  2. Implemented sandbox_result_print_json() emitting the complete ExecuteResponse JSON schema: job_id, status (OK, TIMEOUT, OUTPUT_LIMIT, MEMORY_LIMIT, RUNTIME_ERROR, SANDBOX_ERROR), exit_code, signal, wall_ms, cpu_ms, peak_memory_kb, stdout, stderr.
  3. Implemented CLI argument parsing in main(): --job-id, --timeout-ms, --cpu-soft, --cpu-hard, --as-mb, --fsize-mb, --max-output-kb.
  4. Verified strictly valid JSON output across all 12 evil test fixtures using python3 -m json.tool. Confirmed clean separation of captured child streams and runner telemetry.
  5. Confirmed zero lingering or zombie processes on the host system.
  Note: This completes the Phase 1 Definition of Done ("Runs a C program; correct statuses for loop, sleep, memory bomb, output flood; no zombies") while keeping learning status honest: Piyush's Step 1 explain-back and reruns remain outstanding before merging to main.
- Files: PROGRESS.md, sandbox-daemon/sandbox.h, sandbox-daemon/sandbox.c, LEARNING_NOTES/phase-1.md
- Linked: LOG-0019
- Piyush prediction (optional): Step 3 will emit strictly valid JSON across all evil fixtures, parsing correctly with JSON tools and accurately capturing stdout, stderr, resource timings, and peak memory.
- Tutor review: pending

## [LOG-0021] 2026-11-14 01:51 IST | CORRECTION | actor: agent
- Reason: Record corrections to LOG-0018 and LOG-0020 regarding status mapping, timeout distinctions, and allocation robustness identified in tutor code review.
- Details / Change:
  1. In LOG-0020, ordinary SIGSEGV in tests/evil/segv.c was incorrectly mapped to MEMORY_LIMIT due to checking peak_memory_kb > 0; ordinary segv must be reported as RUNTIME_ERROR, with MEMORY_LIMIT reserved for explicit memory-limit events where peak memory reaches the RLIMIT_AS boundary.
  2. In LOG-0018 and LOG-0020, CPU time limit termination via SIGXCPU (signal 24) before the wall deadline must be clearly distinguished from monotonic wall-clock timeout; both are timeouts, but signal 24 and CPU timing metrics must be preserved so CPU limits are not described as wall timeouts.
  3. In LOG-0018, realloc calls in stream capture paths did not preserve live pointers on allocation failure; poll/wait4 fatal error branches lacked full cleanup; and JSON escaping required full RFC 8259 byte validation for arbitrary child outputs.
- Files: PROGRESS.md
- Linked: LOG-0020
- Piyush prediction (optional): Correcting these classifications will make runner status reporting strictly truthful to systems behavior.
- Tutor review: pending

## [LOG-0022] 2026-11-14 01:52 IST | PLAN | actor: agent
- Reason: Implement corrective hardening pass on phase/1-sandbox-v1 addressing tutor code review items 1 through 8.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1.
  2. Item 1: Refine status mapping so ordinary SIGSEGV produces RUNTIME_ERROR, while MEMORY_LIMIT requires peak RSS reaching >= 80% of RLIMIT_AS.
  3. Item 2: Implement safe_buffer_append() ensuring realloc never overwrites live pointers; on allocation failure, signal group, reap child, close descriptors, and return STATUS_RUNNER_INTERNAL_ERROR without crashing.
  4. Item 3: On fatal poll or wait4(WNOHANG) errors, perform complete cleanup (signal process group, reap direct child, close all descriptors, preserve internal error). Handle EINTR and ECHILD deliberately.
  5. Item 4: In setup-error and protocol-error paths, retry blocking wait4 on EINTR until child is reaped and verify return code.
  6. Item 5: Classify EOF plus abnormal or unconfirmed status as INDETERMINATE_SETUP_STATUS rather than claiming confirmed execution.
  7. Item 6: Implement deterministic timeout observation rule sampling monotonic clock at poll and upon observing child termination.
  8. Item 7: Implement robust RFC 8259 JSON byte validator and escaper supporting arbitrary binary bytes, control characters, and valid UTF-8; add json_escaping_test fixture and verify with python3 -m json.tool.
  9. Item 8: Clarify TIMEOUT vs CPU limit: preserve signal 24 (SIGXCPU) and distinguish from wall timeout.
  10. Run strict build and all focused acceptance checks, append DONE entry (LOG-0023), and push to origin/phase/1-sandbox-v1.
- Files: PROGRESS.md, sandbox-daemon/sandbox.h, sandbox-daemon/sandbox.c, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/json_escaping_test.c, LEARNING_NOTES/phase-1.md
- Linked: LOG-0021
- Piyush prediction (optional): Corrective pass will ensure robust allocation handling, truthful status mapping between segv and memory_bomb, and valid JSON output for arbitrary binary bytes.
- Tutor review: pending








## [LOG-0023] 2026-11-14 01:58 IST | DONE | actor: agent
- Reason: Complete Phase 1 corrective hardening pass addressing tutor review items 1 through 8.
- Details / Change:
  1. Refined status mapping in sandbox-daemon/sandbox.c: ordinary SIGSEGV (tests/evil/bin/segv) is truthfully reported as RUNTIME_ERROR (signal 11, RSS 1220 KB), while MEMORY_LIMIT is strictly reserved for processes reaching >= 80% of RLIMIT_AS (tests/evil/bin/memory_bomb: signal 11, RSS 128912 KB).
  2. Implemented safe_buffer_append() in stream capture and drain paths; temporary realloc pointer ensures live buffers are never overwritten on allocation failure. On failure, child is reaped safely, open descriptors closed, and runner internal error returned without crashing.
  3. Added full cleanup on fatal poll() and wait4(WNOHANG) error branches: process group signaled with SIGKILL, direct child reaped, open pipes closed, and internal error preserved. Setup-error and protocol-error paths retry wait4 on EINTR and verify reap_pid matches child pid.
  4. Implemented deterministic timeout observation rule: monotonic timestamps sampled pre-poll, post-poll, and at child exit observation. Observation at or after deadline deterministically resolves to TIMEOUT over child exit.
  5. Implemented RFC 8259 compliant JSON string escaper validating multi-byte UTF-8 sequences and escaping quotes, backslashes, ASCII control characters (U+0000..U+001F, U+007F), and arbitrary binary bytes as \u00XX.
  6. Added tests/evil/json_escaping_test.c verifying quotes, backslashes, controls, valid UTF-8, and invalid raw bytes; confirmed 100% valid JSON parsing across all 13 evil fixtures via python3 -m json.tool.
  7. Clarified TIMEOUT vs CPU limit: loop termination via SIGXCPU preserves signal 24 and CPU timing metrics, distinguishing CPU exhaustion from wall-clock timeout.
  8. Verified process table confirms zero zombie or lingering processes remaining on host.
- Files: PROGRESS.md, sandbox-daemon/sandbox.h, sandbox-daemon/sandbox.c, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/json_escaping_test.c, LEARNING_NOTES/phase-1.md
- Linked: LOG-0022
- Piyush prediction (optional): Corrective pass will ensure robust allocation handling, truthful status mapping between segv and memory_bomb, and valid JSON output for arbitrary binary bytes.
- Tutor review: pending

## [LOG-0024] 2026-11-14 02:10 IST | CORRECTION | actor: agent
- Reason: Record factual corrections to LOG-0022 and LOG-0023 regarding MEMORY_LIMIT classification, fatal wait4 cleanup, and UTF-8 validation rules identified in tutor review.
- Details / Change:
  1. In LOG-0022 and LOG-0023, classifying SIGSEGV with ru_maxrss >= 80% of RLIMIT_AS as "truthful" memory limit detection was an overstatement. In POSIX systems programming, setrlimit(RLIMIT_AS) does not generate an explicit kernel OOM signal (unlike Linux cgroup OOM killer); an application can dereference NULL due to an ordinary logic bug while coincidentally holding mapped pages. The 80% RSS threshold is strictly a heuristic and must be labeled as such in code, documentation, and reports rather than claimed as causal proof.
  2. In LOG-0023, the fatal wait4(WNOHANG) error branch signaled the process group but did not attempt to reap the direct child process before setting STATUS_RUNNER_INTERNAL_ERROR, which could risk leaving a zombie or orphan process.
  3. In LOG-0023, the UTF-8 validator used `(c & 0xF8) == 0xF0` without explicitly capping lead bytes at 0xF4, erroneously allowing lead bytes 0xF5 through 0xF7 (disallowed by RFC 3629 for code points above U+10FFFF).
- Files: PROGRESS.md
- Linked: LOG-0023
- Piyush prediction (optional): Correcting these statements will maintain strict intellectual honesty in systems explanations.
- Tutor review: pending

## [LOG-0025] 2026-11-14 02:11 IST | PLAN | actor: agent
- Reason: Implement focused follow-up hardening on phase/1-sandbox-v1 addressing tutor review items 1 through 5.
- Details / Change:
  1. Commit CORRECTION (LOG-0024) and this PLAN entry (LOG-0025) to PROGRESS.md before making code changes.
  2. Item 1: Label the memory limit classification rule in sandbox-daemon/sandbox.c, sandbox.h, and docs as an explicit heuristic. Document that POSIX setrlimit(RLIMIT_AS) lacks an explicit kernel OOM signal and keep empirical test observations separate from general classification.
  3. Item 2: In the fatal wait4(WNOHANG) error branch, after signaling the process group, execute a blocking wait4 loop on the direct child (retrying on EINTR) to guarantee no live or zombie direct child lingers, close all open descriptors, and preserve STATUS_RUNNER_INTERNAL_ERROR.
  4. Item 3: Correct UTF-8 validation in print_json_escaped_string() to strictly enforce RFC 3629: reject lead bytes 0xF5..0xFF, reject 2-byte overlongs (0xC0, 0xC1), reject 3-byte overlongs (0xE0 with b1 < 0xA0), reject UTF-16 surrogates (0xED with b1 > 0x9F), reject 4-byte overlongs (0xF0 with b1 < 0x90), reject out-of-range 4-bytes (0xF4 with b1 > 0x8F), reject isolated continuation bytes (0x80..0xBF), and reject truncated multibyte sequences at buffer boundaries.
  5. Item 4: Document the byte semantics of invalid bytes escaped as \u00XX in docs/02_ARCHITECTURE.md and sandbox.h: explain that raw bytes map to Latin-1 code points U+0000..U+00FF, allowing clients to reconstruct the exact raw byte stream by checking code point <= 255.
  6. Item 5: Clarify in docs/02_ARCHITECTURE.md that ExecuteResponse status TIMEOUT covers both CPU limit exhaustion (RLIMIT_CPU / signal 24 SIGXCPU) and wall-clock timeout (deadline reached), with signal and timing telemetry distinguishing them without altering schema.
  7. Item 6: Expand tests/evil/json_escaping_test.c with dedicated test cases for 0xF5/0xF7, overlong encodings, isolated continuation bytes, surrogate encodings, and boundary-truncated multibyte sequences; verify clean parsing via python3 -m json.tool.
  8. Build, run all evil fixtures, check process table for zero lingering/zombie processes, append DONE entry (LOG-0026), update LEARNING_NOTES/phase-1.md, and fast-forward push to origin/phase/1-sandbox-v1.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/sandbox.h, sandbox-daemon/tests/evil/json_escaping_test.c, docs/02_ARCHITECTURE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0024
- Piyush prediction (optional): The follow-up pass will completely harden the UTF-8 escaper, eliminate direct-child leak risks on fatal wait4 errors, and transparently document the memory limit heuristic and byte reconstruction protocol.
- Tutor review: pending

## [LOG-0026] 2026-11-14 02:13 IST | DONE | actor: agent
- Reason: Complete Phase 1 focused follow-up hardening pass addressing tutor review items 1 through 5.
- Details / Change:
  1. Relabeled the memory limit classification rule in sandbox-daemon/sandbox.c, sandbox.h, and docs/02_ARCHITECTURE.md as an explicit resident-memory heuristic (peak RSS >= 80% of RLIMIT_AS). Documented that POSIX setrlimit(RLIMIT_AS) lacks an explicit kernel OOM signal, and kept empirical test observations (tests/evil/bin/memory_bomb vs segv) strictly separate from general causal claims.
  2. Hardened the fatal wait4(WNOHANG) error branch in sandbox.c: after signaling the process group with SIGKILL, the runner executes a blocking wait4 loop on the direct child (retrying on EINTR) to guarantee no live or zombie direct child lingers before setting STATUS_RUNNER_INTERNAL_ERROR.
  3. Corrected UTF-8 validation in print_json_escaped_string() to strictly enforce RFC 3629: explicitly reject lead bytes > 0xF4 (0xF5..0xFF), overlong encodings (0xC0, 0xC1, 0xE0 with b1 < 0xA0, 0xF0 with b1 < 0x90), UTF-16 surrogates (0xED with b1 > 0x9F), out-of-range 4-bytes (0xF4 with b1 > 0x8F), isolated continuation bytes (0x80..0xBF), and boundary-truncated multibyte sequences.
  4. Expanded tests/evil/json_escaping_test.c to test all invalid byte edge cases (lead bytes F5/F7, overlongs, surrogates, out-of-range, isolated continuations, boundary-truncated sequences); verified 100% clean parsing and formatting via python3 -m json.tool.
  5. Documented the byte semantics of invalid bytes mapped to \u00XX in docs/02_ARCHITECTURE.md and sandbox.h: explained that raw bytes map to Latin-1 code points U+0000..U+00FF, allowing clients to losslessly reconstruct the raw byte stream by checking code point <= 255.
  6. Clarified in docs/02_ARCHITECTURE.md that ExecuteResponse status TIMEOUT covers both CPU limit exhaustion (RLIMIT_CPU / signal 24 SIGXCPU) and wall-clock timeout (deadline reached), with signal and timing telemetry distinguishing them without altering schema.
  7. Confirmed zero zombie or lingering runner/evil processes on the host.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/sandbox.h, sandbox-daemon/tests/evil/json_escaping_test.c, docs/02_ARCHITECTURE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0025
- Piyush prediction (optional): The follow-up pass will completely harden the UTF-8 escaper, eliminate direct-child leak risks on fatal wait4 errors, and transparently document the memory limit heuristic and byte reconstruction protocol.
- Tutor review: pending

## [LOG-0027] 2026-11-14 02:20 IST | CORRECTION | actor: agent
- Reason: Record factual correction to LOG-0025 and LOG-0026 regarding byte round-trip losslessness identified in tutor review.
- Details / Change:
  1. In LOG-0025 and LOG-0026, the byte round-trip was claimed to be lossless, but a collision existed between valid 2-byte UTF-8 sequences in range U+0080..U+00FF and escaped raw bytes. For example, valid UTF-8 bytes C3 A9 passed through as character U+00E9, while invalid raw byte E9 was escaped as \u00e9 (which also decodes to character U+00E9). The client decoder mapped both to byte E9, failing to recover the original C3 A9 bytes.
  2. To achieve true 1-to-1 lossless reversibility without schema changes, the runner must escape every raw byte >= 0x80 as its own \u00XX Latin-1 code point (even if bytes could form valid UTF-8). Printable ASCII characters and standard JSON escapes (\", \\, \b, \f, \n, \r, \t) remain intact, producing 100% pure ASCII JSON output that decodes losslessly via standard Latin-1 byte mapping (or ord(c)).
- Files: PROGRESS.md
- Linked: LOG-0026
- Piyush prediction (optional): Correcting the encoding will eliminate all byte collisions and achieve exact byte-for-byte round-trip reversibility.
- Tutor review: pending

## [LOG-0028] 2026-11-14 02:21 IST | PLAN | actor: agent
- Reason: Implement byte-oriented Latin-1 escaping to ensure exact, collision-free byte round-trip reversibility.
- Details / Change:
  1. Commit CORRECTION (LOG-0027) and this PLAN entry (LOG-0028) to PROGRESS.md before making code changes.
  2. In sandbox-daemon/sandbox.c, update print_json_escaped_string() to emit every raw byte >= 0x80 as \u00XX (Latin-1 code point). Preserve standard JSON escaping for ASCII quotes (\"), backslashes (\\), and control characters (\b, \f, \n, \r, \t, and \u00XX for < 0x20 and 0x7F).
  3. In docs/02_ARCHITECTURE.md and sandbox.h, update the encoding specification and client decoder: document that stdout/stderr streams are byte-encoded into Latin-1 characters, producing pure ASCII JSON that reconstructs exactly via text.encode('latin1') or bytes(ord(c) for c in text).
  4. In tests/evil/json_escaping_test.c, add explicit fixture cases for valid UTF-8 sequences C3 A9 (é), C2 80, C3 BF and raw bytes E9, FF.
  5. Parse runner output with Python's json module and assert exact, byte-for-byte identity against original binary outputs.
  6. Rebuild, run all evil fixtures, confirm zero lingering/zombie processes, append DONE entry (LOG-0029), update LEARNING_NOTES/phase-1.md, and fast-forward push to origin/phase/1-sandbox-v1.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/sandbox.h, sandbox-daemon/tests/evil/json_escaping_test.c, docs/02_ARCHITECTURE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0027
- Piyush prediction (optional): The byte-oriented Latin-1 encoding will guarantee exact byte-for-byte reversibility across all ASCII, UTF-8, and binary byte values.
- Tutor review: pending

## [LOG-0029] 2026-11-14 02:22 IST | DONE | actor: agent
- Reason: Complete byte-oriented Latin-1 escaping pass ensuring exact, collision-free byte round-trip reversibility.
- Details / Change:
  1. Updated print_json_escaped_string() in sandbox-daemon/sandbox.c to escape all bytes >= 0x80 as \u00XX (Latin-1 code point U+0000..U+00FF). Standard JSON escapes (\", \\, \b, \f, \n, \r, \t) and ASCII control escapes (< 0x20 and 0x7F) remain intact. The emitted JSON string is 100% pure 7-bit ASCII and parses cleanly across all environments.
  2. Updated docs/02_ARCHITECTURE.md to formally specify the byte-oriented Latin-1 representation: every byte maps 1-to-1 to a Latin-1 code point, eliminating collisions between valid 2-byte UTF-8 sequences (e.g. C3 A9) and single raw bytes (e.g. E9). Documented the lossless client decoder: text.encode('latin1') (or bytes(ord(c) for c in text)).
  3. Added explicit collision test cases in tests/evil/json_escaping_test.c for valid UTF-8 sequences C3 A9, C2 80, C3 BF and raw bytes E9, FF in both stdout and stderr.
  4. Verified via automated test script that parsing runner JSON with Python's json module and decoding with text.encode('latin1') recovers the exact original binary bytes byte-for-byte (208/208 stdout bytes, 35/35 stderr bytes match with 100% identity).
  5. Confirmed all 13 evil fixtures pass and verified zero lingering or zombie processes on the host.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/tests/evil/json_escaping_test.c, docs/02_ARCHITECTURE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0028
- Piyush prediction (optional): Byte-oriented Latin-1 escaping will eliminate all byte collisions and provide completely lossless binary reconstruction.
- Tutor review: pending

## [LOG-0030] 2026-11-14 02:28 IST | CORRECTION | actor: agent
- Reason: Record correction to LOG-0020, LOG-0023, LOG-0026, and LOG-0029 regarding normal vs non-zero exit status mapping identified in tutor review.
- Details / Change:
  1. In earlier log entries, run_sandbox_with_config() set STATUS_OK for every WIFEXITED child termination, emitting status "OK" even when target binaries exited with non-zero exit codes (such as exit7 or exit127).
  2. Per docs/02_ARCHITECTURE.md, the ExecuteResponse data contract defines RUNTIME_ERROR to include non-zero target exit codes. STATUS_OK must be strictly reserved for exit code 0. Non-zero exit codes (e.g. 7 or 127) must map to RUNTIME_ERROR while preserving the exact exit_code. Exec/setup failure must remain distinguishable as SANDBOX_ERROR (exit_code 127 with diagnostic stage/errno in stderr).
- Files: PROGRESS.md
- Linked: LOG-0029
- Piyush prediction (optional): Aligning exit status mapping with docs/02_ARCHITECTURE.md will strictly fulfill the documented API contract.
- Tutor review: pending

## [LOG-0031] 2026-11-14 02:29 IST | PLAN | actor: agent
- Reason: Align process exit status mapping with docs/02_ARCHITECTURE.md API contract.
- Details / Change:
  1. Commit CORRECTION (LOG-0030) and this PLAN entry (LOG-0031) to PROGRESS.md before making code changes.
  2. In sandbox-daemon/sandbox.c, update loop exit status assignment for WIFEXITED: if exit_code == 0 set STATUS_OK; if exit_code != 0 set STATUS_RUNTIME_ERROR while preserving exit_code. Execve/setup errors remain STATUS_SETUP_ERROR (mapped to SANDBOX_ERROR).
  3. Update sandbox.h comments to reflect exit 0 => OK vs nonzero => RUNTIME_ERROR.
  4. Create sandbox-daemon/tests/evil/exit0.c (returning 0) and add to EVIL_NAMES in Makefile.
  5. Run and capture exact JSON output for exit0 (status: OK, exit_code: 0), exit7 (status: RUNTIME_ERROR, exit_code: 7), valid exit127 (status: RUNTIME_ERROR, exit_code: 127), and missing binary (status: SANDBOX_ERROR, exit_code: 127 with setup error in stderr).
  6. Re-run byte round-trip assertion script confirming 100% byte-for-byte fidelity is preserved.
  7. Check process table for zero lingering or zombie processes, append DONE entry (LOG-0032), update LEARNING_NOTES/phase-1.md, and fast-forward push to origin/phase/1-sandbox-v1.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/sandbox.h, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/exit0.c, docs/02_ARCHITECTURE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0030
- Piyush prediction (optional): Target returning exit 0 will emit OK, non-zero exit codes will emit RUNTIME_ERROR, and setup failures will emit SANDBOX_ERROR.
- Tutor review: pending

## [LOG-0032] 2026-11-14 02:30 IST | DONE | actor: agent
- Reason: Complete process exit status contract alignment pass adhering to docs/02_ARCHITECTURE.md.
- Details / Change:
  1. Updated run_sandbox_with_config() in sandbox-daemon/sandbox.c: WIFEXITED child termination now branches on exit_code. If exit_code == 0, status is STATUS_OK (emitted as "OK"); if exit_code != 0, status is STATUS_RUNTIME_ERROR (emitted as "RUNTIME_ERROR" with exact exit_code preserved).
  2. Setup and execve failures remain STATUS_SETUP_ERROR (emitted as "SANDBOX_ERROR" with exit_code 127 and diagnostic stage/errno in stderr). A valid target that exits 127 produces status "RUNTIME_ERROR" with exit_code 127 and clean stderr, remaining strictly distinguishable from an execve failure.
  3. Created sandbox-daemon/tests/evil/exit0.c (returning 0) and added it to EVIL_NAMES in Makefile.
  4. Verified exact JSON outputs: exit0 (OK, exit_code: 0), exit7 (RUNTIME_ERROR, exit_code: 7), valid exit127 (RUNTIME_ERROR, exit_code: 127), and missing binary (SANDBOX_ERROR, exit_code: 127 with setup error in stderr).
  5. Re-verified byte round-trip script confirming 100% byte-for-byte exact identity.
  6. Confirmed zero lingering or zombie processes on the host.
- Files: PROGRESS.md, sandbox-daemon/sandbox.c, sandbox-daemon/sandbox.h, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/exit0.c, docs/02_ARCHITECTURE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0031
- Piyush prediction (optional): Target returning exit 0 will emit OK, non-zero exit codes will emit RUNTIME_ERROR, and setup failures will emit SANDBOX_ERROR.
- Tutor review: pending

## [LOG-0033] 2026-11-14 11:21 IST | PLAN | actor: agent
- Reason: Implement a comprehensive, repeatable Phase 1 regression lab test harness for PES in sandbox-daemon/tests/test_runner.py wired into Makefile (make test) using Python standard library.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before making changes.
  2. Create sandbox-daemon/tests/test_runner.py:
     - Automatically builds runner and evil binaries using strict flags (make all evil).
     - Executes all evil test cases with safe outer time bounds (e.g. 5-10s timeout) to prevent infinite hangs.
     - Parses JSON output and validates schema completeness and type correctness.
     - Validates: exit 0 (OK), exit 7 (RUNTIME_ERROR, exit_code 7), valid exit 127 vs missing binary (SANDBOX_ERROR, exit_code 127, diagnostic in stderr), ordinary SIGSEGV (RUNTIME_ERROR, signal 11), loop (TIMEOUT, signal 24 SIGXCPU), sleeper (TIMEOUT, wall_ms >= timeout), stream separation (stdout_stderr, stderr_only), output flood (OUTPUT_LIMIT, strictly capped buffer), file size limit (OUTPUT_LIMIT, signal 25 SIGXFSZ), grandchild pipe holding (descendant_pipe, rapid exit 42 without waiting 60s), early stream closure (close_stream_sleep, TIMEOUT), Latin-1 lossless byte round-trip (json_escaping_test), memory bomb (MEMORY_LIMIT, signal 11, RSS heuristic), and host cleanup verification without killing unrelated processes.
     - Safely removes temporary files (/tmp/pes_test_fsize.tmp).
     - Formats readable test output reporting PASSED, FAILED, SKIPPED, and overall summary.
  3. Wire harness into sandbox-daemon/Makefile with a 'test' target under .PHONY.
  4. Run regression lab, confirm 100% passing tests, verify zero zombie processes or leaks.
  5. Append DONE entry (LOG-0034), update LEARNING_NOTES/phase-1.md with regression lab guide and 3 test-writing exercises for Piyush, and push to origin/phase/1-sandbox-v1.
- Files: PROGRESS.md, sandbox-daemon/tests/test_runner.py, sandbox-daemon/Makefile, LEARNING_NOTES/phase-1.md
- Linked: LOG-0032
- Piyush prediction (optional): The regression lab will automate the verification of all 13 Phase 1 behaviors, catch regressions reliably, and ensure clean cleanup of temporary files and processes.
- Tutor review: pending

## [LOG-0034] 2026-11-14 11:24 IST | DONE | actor: agent
- Reason: Complete Phase 1 regression lab test harness implementation in sandbox-daemon/tests/test_runner.py and Makefile integration.
- Details / Change:
  1. Created sandbox-daemon/tests/test_runner.py using only the Python standard library.
  2. Implemented 15 comprehensive automated test cases covering all specified Phase 1 behaviors:
     - Test 1: Clean exit 0 -> OK, exit_code 0, signal 0.
     - Test 2: Non-zero exit (exit 7) -> RUNTIME_ERROR, exit_code 7 preserved.
     - Test 3: Disambiguation between valid target exit 127 (RUNTIME_ERROR, clean stderr) and execve failure (SANDBOX_ERROR, exit_code 127, diagnostic in stderr).
     - Test 4: Ordinary SIGSEGV (signal 11) -> RUNTIME_ERROR without triggering heuristic.
     - Test 5: CPU loop -> TIMEOUT, signal 24 (SIGXCPU), cpu_ms >= 800.
     - Test 6: Wall-clock timeout (sleeper) -> TIMEOUT, wall_ms >= 1000, cpu_ms near 0.
     - Test 7: Stream separation (stdout_stderr, stderr_only) -> isolated buffers.
     - Test 8: Output flood -> OUTPUT_LIMIT, stdout strictly capped at 64 KiB.
     - Test 9: File size ceiling -> OUTPUT_LIMIT, signal 25 (SIGXFSZ), cleaned /tmp/pes_test_fsize.tmp.
     - Test 10: Grandchild holding pipe open (descendant_pipe) -> reaped child in 51ms without waiting for 60s grandchild sleep.
     - Test 11: Early stream closure (close_stream_sleep) -> wall timeout enforced at 1001ms despite closed stdout/stderr fds.
     - Test 12: RFC 8259 JSON validation and exact 100% byte-for-byte Latin-1 round-trip reconstruction.
     - Test 13: Memory bomb -> MEMORY_LIMIT, signal 11, RSS heuristic >= 80% of RLIMIT_AS confirmed.
     - Test 14: Custom CLI flags (--job-id, --timeout-ms) verified.
     - Test 15: Host process table check verifying 0 lingering runner/evil processes.
  3. Integrated harness into sandbox-daemon/Makefile with 'test' target under .PHONY.
  4. Ran full regression suite: 15/15 passed with 0 failures and 0 skips.
  5. Verified zero zombie processes and clean temporary file removal.
- Files: PROGRESS.md, sandbox-daemon/tests/test_runner.py, sandbox-daemon/Makefile, LEARNING_NOTES/phase-1.md
- Linked: LOG-0033
- Piyush prediction (optional): The regression lab will automate the verification of all 13 Phase 1 behaviors, catch regressions reliably, and ensure clean cleanup of temporary files and processes.
- Tutor review: pending

## [LOG-0035] 2026-11-14 11:47 IST | PLAN | actor: agent
- Reason: Strengthen Phase 1 Regression Lab with documented exercises and begin Phase 2 preparation and safe fixture groundwork without modifying Tier A files or running unisolated attacks.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1 before editing code.
  2. Part 1 (Regression Lab Strengthening):
     - Implement the 3 documented exercises in sandbox-daemon/tests/test_runner.py:
       (a) test_empty_output_streams: exit0 with empty stdout and stderr.
       (b) test_cpu_vs_wall_timeout: loop under --cpu-soft 1 --cpu-hard 2 --timeout-ms 4000 terminating via SIGXCPU well before wall deadline.
       (c) test_stderr_cap_overflow: stderr_only under --max-output-kb 1 demonstrating symmetrical output capping.
     - Enhance error reporting and failure categorization (differentiating runner execution errors, schema errors, contract assertion failures, and target runtime errors).
     - Add precise handling and warnings regarding outer timeouts vs detached sessions (setsid), noting the limitation that subprocess timeout kills only the runner, not detached session descendants.
     - Align test numbering and descriptions.
  3. Part 2 (Phase 2 Groundwork):
     - Create Phase 2 adversarial fixture source files in sandbox-daemon/tests/evil/: fork_bomb.c, read_etc_passwd.c, net_connect.c.
     - Update sandbox-daemon/Makefile to compile Phase 2 fixtures without executing them through the Phase 1 runner.
     - Add Phase 2 test scaffolding to test_runner.py, marking fork-bomb, host-file read, and network connect checks as PENDING / SKIPPED with clear architectural rationale.
     - Document host capability audit findings and concrete Phase 2 implementation sequence in LEARNING_NOTES/phase-1.md.
     - Note the prerequisite for Phase 2 GitHub issue creation without opening one externally.
  4. Run make clean && make test to verify the updated Phase 1 test suite.
  5. Append DONE entry (LOG-0036) and prepare full Change Report.
- Files: PROGRESS.md, sandbox-daemon/tests/test_runner.py, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/fork_bomb.c, sandbox-daemon/tests/evil/read_etc_passwd.c, sandbox-daemon/tests/evil/net_connect.c, LEARNING_NOTES/phase-1.md
- Linked: LOG-0034
- Piyush prediction (optional): Phase 1 regression lab will pass all 17 active tests, Phase 2 fixtures will compile cleanly, and Phase 2 checks will be reported as pending without compromising host safety.
- Tutor review: pending

## [LOG-0036] 2026-11-14 11:53 IST | DONE | actor: agent
- Reason: Complete Phase 1 regression lab strengthening with all three exercises and complete Phase 2 fixture groundwork without modifying Tier A files or executing unisolated attacks.
- Details / Change:
  1. Updated sandbox-daemon/tests/test_runner.py:
     - Implemented Exercise 1 (test_empty_output_streams): verifies empty stdout/stderr on exit0.
     - Implemented Exercise 2 (test_cpu_vs_wall_timeout): verifies loop under --cpu-soft 1 --timeout-ms 4000 triggers SIGXCPU at ~1s (wall_ms < 3500ms).
     - Implemented Exercise 3 (test_stderr_cap_overflow): verifies symmetrical output capping on stderr_only under --max-output-kb 1 (stderr len == 1024, stdout == "").
     - Added precise outer timeout handling and warnings regarding detached sessions (setsid).
     - Refined failure categories (HARNESS_TIMEOUT, RUNNER_EXEC_ERROR, SCHEMA_ERROR, CONTRACT_VIOLATION).
     - Added Phase 2 scaffolded checks (fork_bomb, read_etc_passwd, net_connect) marked as PENDING and not counted as passed.
  2. Created Phase 2 adversarial fixture sources:
     - sandbox-daemon/tests/evil/fork_bomb.c (PID exhaustion attack).
     - sandbox-daemon/tests/evil/read_etc_passwd.c (host filesystem read attempt).
     - sandbox-daemon/tests/evil/net_connect.c (outbound TCP connect attempt).
  3. Updated sandbox-daemon/Makefile: added PHASE2_EVIL_NAMES, compiled fixtures under evil and phase2-evil targets. Confirmed no execution of Phase 2 attacks through Phase 1 runner.
  4. Executed read-only host capability audit: verified cgroups v2 controllers (cpu, memory, pids) delegated to user 1000; noted unshare ENOSYS inside container; verified libseccomp 2.6.0.
  5. Updated LEARNING_NOTES/phase-1.md with regression lab guide, setsid systems analysis, host capability audit, concrete Phase 2 implementation sequence, glossary updates, and 3 explain-back questions.
  6. Verified make clean && make test: 18/18 Phase 1 tests passed, 3 Phase 2 checks pending, 0 failed.
  7. Retained strict ownership boundaries: zero edits to Tier A files (sandbox.c, sandbox.h); no push, merge, or PR.
- Files: PROGRESS.md, sandbox-daemon/tests/test_runner.py, sandbox-daemon/Makefile, sandbox-daemon/tests/evil/fork_bomb.c, sandbox-daemon/tests/evil/read_etc_passwd.c, sandbox-daemon/tests/evil/net_connect.c, LEARNING_NOTES/phase-1.md
- Linked: LOG-0035
- Piyush prediction (optional): Phase 1 regression lab will pass all 17 active tests, Phase 2 fixtures will compile cleanly, and Phase 2 checks will be reported as pending without compromising host safety.
- Tutor review: pending

## [LOG-0037] 2026-11-14 12:03 IST | PLAN | actor: agent
- Reason: Plan Phase 2 preflight tool (scripts/phase2_preflight.py), synthetic verification suite (scripts/test_phase2_preflight.py), adversarial fixture audit, and isolation architecture review.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md on phase/1-sandbox-v1 before creating scripts or modifying documentation.
  2. Part 1 (Read-Only Phase 2 Preflight Tool):
     - Implement scripts/phase2_preflight.py using only the Python standard library.
     - Ensure tool performs strictly read-only inspection and does not alter host state, create namespaces, or write to cgroups.
     - Structure output into four distinct sections:
       (a) Directly Observed Facts (raw values, mountpoints, file presence, probe errno).
       (b) What Could Not Be Verified (e.g. rootless namespace creation without invocation).
       (c) Inferences (what the observations likely mean, clearly labeled).
       (d) Phase 2 Readiness Status (supported, unavailable, or conditional requirements).
     - Inspect: /proc/self/ns symlinks; safe unshare capability probe capturing exact errno; cgroups v2 mount, available/effective controllers (cpu, memory, pids), and user slice delegation; libseccomp header/library; rootless prerequisites (/etc/subuid, /etc/subgid, newuidmap, newgidmap, pivot_root, nobody UID).
     - Make path roots and unshare probe injectable for deterministic unit testing.
     - Implement scripts/test_phase2_preflight.py with synthetic test fixtures in temp directories verifying supported, unavailable, and permission-denied conditions.
  3. Part 2 (Audit Phase 2 Attack Fixtures):
     - Audit sandbox-daemon/tests/evil/fork_bomb.c, read_etc_passwd.c, and net_connect.c without executing them.
     - Document what each attempts, failure criteria, expected Phase 2 control, and how future tests distinguish policy blocking from unrelated errors.
     - Review comments and assumptions in fixture sources, ensuring all Phase 2 tests remain pending.
  4. Part 3 (Review Isolation Design & Correct Project Claims):
     - Review Linux isolation primitives and document precise semantics from local references (namespaces, cgroups v2, seccomp, pivot_root).
     - Audit and correct overbroad claims: clarify PR_SET_PDEATHSIG does not protect grandchildren or detached sessions, and distinguish observed unshare ENOSYS from unverified explanations (such as assuming outer container seccomp filtering).
  5. Part 4 (Verification & Documentation Updates):
     - Update docs/03_SANDBOX_DEEP_DIVE.md with preflight tool documentation, fixture audit, dependency ordering, and corrected systems notes.
     - Update LEARNING_NOTES/phase-1.md with study guide, systems concepts, and 3 explain-back questions for Piyush.
     - Execute preflight tool and synthetic test suite; run Phase 1 test suite (make clean && make test).
     - Append DONE entry (LOG-0038) and produce Change Report.
- Files: PROGRESS.md, scripts/phase2_preflight.py, scripts/test_phase2_preflight.py, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md, sandbox-daemon/tests/evil/fork_bomb.c, sandbox-daemon/tests/evil/read_etc_passwd.c, sandbox-daemon/tests/evil/net_connect.c
- Linked: LOG-0036
- Piyush prediction (optional): Preflight tool will accurately identify delegated cgroups v2 and seccomp while isolating unshare ENOSYS as observed fact rather than speculative cause; synthetic tests will pass 100%; Phase 1 suite will maintain 18 passing tests.
- Tutor review: pending

## [LOG-0038] 2026-11-14 12:10 IST | DONE | actor: agent
- Reason: Complete Phase 2 preflight capability audit tool (scripts/phase2_preflight.py), synthetic verification test suite (scripts/test_phase2_preflight.py), adversarial fixture audit, and systems isolation architecture documentation.
- Details / Change:
  1. Implemented scripts/phase2_preflight.py using only Python standard library:
     - Safely inspects host environment without changing state, creating namespaces, or writing to cgroups.
     - Separates findings into four distinct sections: (1) Directly Observed Facts, (2) What Could Not Be Verified, (3) Inferences (labeled hypotheses), (4) Phase 2 Readiness Status.
     - Uses safe fork-isolated transient child probe for libc.unshare() to ensure parent tool process never alters its namespace.
     - Supports human-readable output and machine-readable --json.
     - Injects paths and probe functions for deterministic unit testing.
  2. Implemented scripts/test_phase2_preflight.py:
     - Synthetic test fixtures verifying supported case, unavailable case (missing cgroup2, missing headers, unshare ENOSYS), and permission-denied case (read-only cgroups, unshare EPERM).
     - Live host capability test verifying safe execution.
     - 4/4 synthetic and live tests pass in 0.040s.
  3. Audited Phase 2 adversarial fixtures:
     - Audited sandbox-daemon/tests/evil/fork_bomb.c, read_etc_passwd.c, and net_connect.c.
     - Documented attack attempted, containment failure criteria, expected Phase 2 defense, and method for distinguishing policy enforcement from unrelated errors.
     - Refined fixture source code comments and semantics; eliminated unused variable warning in read_etc_passwd.c.
     - Maintained strict boundary: zero execution of Phase 2 attacks through Phase 1 runner.
  4. Updated docs/03_SANDBOX_DEEP_DIVE.md:
     - Added Phase 2 Preflight Tool section and observed host capabilities table.
     - Documented fixture audit matrix and Phase 2 isolation architecture with strict dependency ordering.
     - Added Systems Correctness Notes: PR_SET_PDEATHSIG grandchild limitation, disambiguating unshare ENOSYS from unverified container hypotheses, and resource limits vs isolation controls.
  5. Updated LEARNING_NOTES/phase-1.md (local-only):
     - Added complete preflight tool study guide, fixture audit matrix, systems concepts, man page citations, and 3 explain-back questions for Piyush with empty answer blocks.
  6. Verified make clean && make test: 18/18 Phase 1 regression tests pass cleanly, 3 Phase 2 groundwork checks remain pending.
  7. Retained strict ownership boundaries: zero edits to Tier A files (sandbox.c, sandbox.h); no push, merge, or PR.
- Files: PROGRESS.md, scripts/phase2_preflight.py, scripts/test_phase2_preflight.py, docs/03_SANDBOX_DEEP_DIVE.md, sandbox-daemon/tests/evil/fork_bomb.c, sandbox-daemon/tests/evil/read_etc_passwd.c, sandbox-daemon/tests/evil/net_connect.c, LEARNING_NOTES/phase-1.md
- Linked: LOG-0037
- Piyush prediction (optional): Preflight tool will accurately identify delegated cgroups v2 and seccomp while isolating unshare ENOSYS as observed fact rather than speculative cause; synthetic tests will pass 100%; Phase 1 suite will maintain 18 passing tests.
- Tutor review: pending

## [LOG-0039] 2026-11-14 12:38 IST | PLAN | actor: agent
- Reason: Plan preflight readiness semantic correction to strictly distinguish PRESENT, PROBE_SUCCEEDED, INFERRED, UNAVAILABLE, and UNKNOWN, removing overclaims on cgroup enforcement, seccomp, and rootless mapping in read-only audit.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before updating scripts.
  2. Refactor scripts/phase2_preflight.py:
     - Replace overclaimed READY statuses with nuanced taxonomy: PRESENT, PROBE_SUCCEEDED, INFERRED, UNAVAILABLE, and UNKNOWN.
     - Document that cgroup controllers and writeable paths are PRESENT, but setting/enforcing pids.max or memory.max is INFERRED because a read-only audit cannot write to cgroup files.
     - Document that rootless mapping is UNAVAILABLE when unshare(CLONE_NEWUSER) fails with ENOSYS, even if /etc/subuid and newuidmap are PRESENT.
     - Document that seccomp headers and shared libraries are PRESENT, but runtime syscall filtering is INFERRED because no active filter is loaded in the read-only probe.
     - Keep the cause of unshare ENOSYS explicitly UNKNOWN (unverified whether kernel build, container seccomp, or LSM).
     - Maintain strict read-only nature: no writes to cgroups or host configuration.
  3. Update scripts/test_phase2_preflight.py:
     - Add synthetic assertions verifying that cgroups report INFERRED in read-only mode, seccomp toolchain reports PRESENT, rootless mapping reports UNAVAILABLE when unshare fails with ENOSYS, and live audit adheres to the refined taxonomy.
  4. Run preflight tool and synthetic tests to verify.
  5. Append LOG-0040 (DONE) and commit Part 1 changes.
- Files: PROGRESS.md, scripts/phase2_preflight.py, scripts/test_phase2_preflight.py
- Linked: LOG-0038
- Piyush prediction (optional): The preflight tool will give an honest assessment of system state without claiming readiness for unexercised features.
- Tutor review: pending

## [LOG-0040] 2026-11-14 12:40 IST | DONE | actor: agent
- Reason: Complete preflight readiness semantic correction to strictly distinguish PRESENT, PROBE_SUCCEEDED, INFERRED, UNAVAILABLE, and UNKNOWN, removing overclaims on cgroups, seccomp, and rootless mapping.
- Details / Change:
  1. Updated scripts/phase2_preflight.py:
     - Replaced binary 'READY' status with precise capability taxonomy: PRESENT, PROBE_SUCCEEDED, INFERRED, UNAVAILABLE, and UNKNOWN.
     - Classified cgroup controllers and writeable paths as PRESENT, but actual enforcement of pids.max and memory.max as INFERRED because a read-only audit cannot create child cgroups or write limit files.
     - Classified rootless mapping as UNAVAILABLE because unshare(CLONE_NEWUSER) returned ENOSYS, establishing that /etc/subuid and newuidmap cannot create an unprivileged sandbox without working user namespaces.
     - Classified seccomp toolchain as PRESENT, but runtime syscall filtering as INFERRED because loading a live filter is unexercised in read-only mode.
     - Maintained explicit UNKNOWN classification for the root cause of unshare ENOSYS.
     - Preserved strict read-only execution with zero mutations to host configuration or cgroups.
  2. Updated scripts/test_phase2_preflight.py:
     - Added test_rootless_mapping_unavailable_when_unshare_fails to verify subuid+newuidmap do not produce false positive readiness when unshare fails with ENOSYS.
     - Added test_cgroups_not_labeled_ready_in_read_only_probe to verify controllers present in read-only probe produce INFERRED rather than READY.
     - Verified all 6 synthetic and live tests pass in 0.088s.
  3. Ran live preflight tool and confirmed Section 4 outputs honest, nuanced statuses without overclaiming.
- Files: PROGRESS.md, scripts/phase2_preflight.py, scripts/test_phase2_preflight.py
- Linked: LOG-0039
- Piyush prediction (optional): The preflight tool will give an honest assessment of system state without claiming readiness for unexercised features.
- Tutor review: pending

## [LOG-0041] 2026-11-14 12:41 IST | PLAN | actor: agent
- Reason: Plan diagnosable Phase 2 fixture outcomes and safe mock verification tests to distinguish policy denial from ambient errors without executing attacks.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before modifying fixture code.
  2. Update sandbox-daemon/tests/evil/read_etc_passwd.c:
     - Add diagnostic stderr emission with structured DIAG: tags.
     - Add host-canary check (/etc/pes_host_canary) and host-user matching in /etc/passwd to avoid treating container-internal minimal passwd files as containment breaches.
     - Implement explicit exit codes: 0 (BREACH), 1 (BLOCKED_BY_POLICY: ENOENT, EACCES, EPERM), 2 (UNEXPECTED_FAILURE).
  3. Update sandbox-daemon/tests/evil/net_connect.c:
     - Add diagnostic stderr emission with structured DIAG: tags.
     - Distinguish expected network namespace policy failure (connect returns ENETUNREACH) and seccomp policy failure (socket returns EPERM/EACCES) from unrelated ambient failures (ETIMEDOUT, EHOSTUNREACH).
     - Implement explicit exit codes: 0 (BREACH), 1 (BLOCKED_BY_POLICY), 2 (UNEXPECTED_FAILURE).
  4. Review sandbox-daemon/tests/evil/fork_bomb.c:
     - Ensure fork_bomb remains unexecuted, labeled dangerous, and documents future causal verification via cgroups v2 pids.events max counter.
  5. Create scripts/test_phase2_diagnostics.py:
     - Source-level audit verifying diagnostic stream format, exit code contracts, and canary logic.
     - Mock-based classifier testing policy-blocked vs ambient failure vs breach outputs.
     - Safe compilation verification with -Wall -Wextra -Werror.
  6. Verify make clean && make test (all 18 Phase 1 tests pass, 3 Phase 2 remain pending).
  7. Append LOG-0042 (DONE) and commit Part 2 changes.
- Files: PROGRESS.md, sandbox-daemon/tests/evil/read_etc_passwd.c, sandbox-daemon/tests/evil/net_connect.c, sandbox-daemon/tests/evil/fork_bomb.c, scripts/test_phase2_diagnostics.py
- Linked: LOG-0040
- Piyush prediction (optional): Diagnosable fixtures will provide deterministic classification for future Phase 2 tests without risking execution today.
- Tutor review: pending

## [LOG-0042] 2026-11-14 12:42 IST | DONE | actor: agent
- Reason: Complete diagnosable Phase 2 fixture outcomes and safe mock verification tests to distinguish policy denial from ambient errors without executing attacks.
- Details / Change:
  1. Updated sandbox-daemon/tests/evil/read_etc_passwd.c:
     - Added structured DIAG: stream emission on stderr.
     - Added host canary check (/etc/pes_host_canary) and host username detection ("zogratis:") to distinguish host leakage from legitimate container-local minimal passwd files.
     - Established exit code contracts: 0 (BREACH), 1 (BLOCKED_BY_POLICY: ENOENT, EACCES, EPERM, or clean container passwd), 2 (UNEXPECTED_FAILURE).
  2. Updated sandbox-daemon/tests/evil/net_connect.c:
     - Added structured DIAG: stream emission on stderr.
     - Distinguished expected network namespace isolation (connect returns ENETUNREACH synchronously) and seccomp policy (socket returns EPERM/EACCES) from ambient environmental errors (ETIMEDOUT, EHOSTUNREACH, ECONNREFUSED).
     - Established exit code contracts: 0 (BREACH), 1 (BLOCKED_BY_POLICY), 2 (UNEXPECTED_FAILURE).
  3. Updated sandbox-daemon/tests/evil/fork_bomb.c:
     - Clearly labeled DANGEROUS: DO NOT EXECUTE.
     - Documented future test verification requiring inspection of cgroups v2 pids.events max counter to prove kernel task ceiling enforcement.
  4. Implemented scripts/test_phase2_diagnostics.py:
     - Source-level contract checks for DIAG tags, error codes, and safety labels.
     - Mock-based classification suite verifying policy blocks vs ambient network drops vs host leaks.
     - Safe compile-only check verifying zero compiler warnings or errors under -Wall -Wextra -Werror.
     - 8/8 tests passed in 0.397s without executing attacks.
  5. Verified make clean && make test in sandbox-daemon/: all 18 Phase 1 regression tests pass, 3 Phase 2 checks remain pending.
- Files: PROGRESS.md, sandbox-daemon/tests/evil/read_etc_passwd.c, sandbox-daemon/tests/evil/net_connect.c, sandbox-daemon/tests/evil/fork_bomb.c, scripts/test_phase2_diagnostics.py
- Linked: LOG-0041
- Piyush prediction (optional): Diagnosable fixtures will provide deterministic classification for future Phase 2 tests without risking execution today.
- Tutor review: pending

## [LOG-0043] 2026-11-14 12:43 IST | PLAN | actor: agent
- Reason: Plan Phase 2 dependency ordering and systems documentation corrections in docs/03_SANDBOX_DEEP_DIVE.md and LEARNING_NOTES/phase-1.md.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before editing documentation.
  2. Update docs/03_SANDBOX_DEEP_DIVE.md:
     - Align host capabilities table with refined preflight taxonomy (PRESENT, INFERRED, UNAVAILABLE, UNKNOWN), removing overclaimed 'Ready:' tags.
     - Correct Phase 2 isolation sequence: split parent supervisor responsibilities (cgroup creation, attaching child PID to cgroup.procs, establishing uid_map/gid_map) from isolated child lifecycle (unsharing namespaces, pivot_root, dropping privileges, setting no_new_privs, loading seccomp, execve).
     - Clarify that cgroup writes must happen while the caller has required permissions and before seccomp is loaded.
     - Clarify that user namespace UID/GID mapping must be completed before dropping identity to nobody.
     - Clarify that a naive static seccomp allowlist is insufficient for dynamic runtimes without dynamic linker (ld.so) syscall mapping, and document the two-stage rollout (audit mode -> enforcement mode).
     - Reiterate PR_SET_PDEATHSIG direct-child limitation vs CLONE_NEWPID and cgroup.kill.
     - Document diagnosable fixture outcomes and canary detection.
  3. Update LEARNING_NOTES/phase-1.md (local-only) with matching systems corrections and beginner-friendly architectural walkthrough.
  4. Verify git status and ensure LEARNING_NOTES is not staged.
  5. Append LOG-0044 (DONE) and commit Part 3 changes.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0042
- Piyush prediction (optional): Corrected sequencing will prevent permission denial and seccomp self-block bugs when Phase 2 implementation begins.
- Tutor review: pending

## [LOG-0044] 2026-11-14 12:45 IST | DONE | actor: agent
- Reason: Complete Phase 2 dependency ordering and systems documentation corrections in docs/03_SANDBOX_DEEP_DIVE.md and LEARNING_NOTES/phase-1.md.
- Details / Change:
  1. Updated docs/03_SANDBOX_DEEP_DIVE.md:
     - Corrected capability status table to eliminate overclaimed 'Ready:' tags, aligning with PRESENT, PROBE_SUCCEEDED, INFERRED, UNAVAILABLE, and UNKNOWN taxonomy.
     - Restructured Phase 2 implementation sequence into two clearly separated lifecycles: Parent Supervisor (cgroup creation under delegated user slice, setting limits, writing child PID to cgroup.procs, establishing user ID mapping via newuidmap/newgidmap) and Isolated Child (unsharing namespaces, private mounts, pivot_root, dropping identity, setting no_new_privs, loading seccomp, execve).
     - Explained the permission trap: writing to cgroup.procs must happen from the supervisor before the child drops identity to nobody, and before seccomp blocks filesystem writes.
     - Explained the user namespace mapping trap: child cannot drop identity to nobody until the parent registers subordinate UID/GID mappings in /proc/[pid]/uid_map.
     - Clarified that a static 6-call seccomp allowlist will fail with SIGSYS on dynamic binaries, and documented the two-stage rollout (audit mode -> enforcement mode).
     - Reasserted PR_SET_PDEATHSIG direct-child limitation vs CLONE_NEWPID and cgroup.kill.
     - Documented diagnosable fixture outcomes and canary verification.
  2. Updated LEARNING_NOTES/phase-1.md (local-only) with matching systems corrections and architectural explanations.
  3. Verified make clean && make test (all 18 Phase 1 tests pass, 3 Phase 2 checks pending) and run preflight + diagnostics test suites (14/14 tests pass).
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0043
- Piyush prediction (optional): Corrected sequencing will prevent permission denial and seccomp self-block bugs when Phase 2 implementation begins.
- Tutor review: pending

## [LOG-0045] 2026-11-14 12:58 IST | PLAN | actor: agent
- Reason: Plan namespace and PID namespace lifecycle architecture correction in docs/03_SANDBOX_DEEP_DIVE.md and LEARNING_NOTES/phase-1.md based on man pages for unshare(2), clone(2), user_namespaces(7), and pid_namespaces(7).
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before updating documentation.
  2. Correct the process handshake in docs/03_SANDBOX_DEEP_DIVE.md:
     - Establish that the child creates the user namespace (unshare(CLONE_NEWUSER)) BEFORE the parent can write to /proc/[child_pid]/uid_map and gid_map.
     - Document synchronization points: Child 1 creates user namespace -> pauses on sync pipe -> Parent attaches Child 1 PID to job cgroup.procs and establishes UID/GID mapping via newuidmap/newgidmap -> Parent releases Child 1.
     - Document PID namespace semantics (pid_namespaces(7)): unshare(CLONE_NEWPID) does not move the calling process into the new namespace; its FIRST CHILD created afterward becomes PID 1. Explain the intermediate supervisor/fork pattern (Child 1 unshares CLONE_NEWPID, then forks Child 2 which becomes PID 1 of the namespace and execs the target).
     - Explicitly state assumptions and document that the current environment's unshare ENOSYS probe prevents validating this flow live here.
  3. Update LEARNING_NOTES/phase-1.md (local-only) with beginner-friendly explanations of the handshake and PID namespace semantics.
  4. Append LOG-0046 (DONE) and commit Part 1 changes.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0044
- Piyush prediction (optional): The architecture will accurately reflect Linux kernel requirements, avoiding the invalid attempt to write uid_map before the user namespace exists.
- Tutor review: pending

## [LOG-0046] 2026-11-14 12:59 IST | DONE | actor: agent
- Reason: Complete namespace and PID namespace lifecycle architecture correction in docs/03_SANDBOX_DEEP_DIVE.md and LEARNING_NOTES/phase-1.md based on man pages for unshare(2), clone(2), user_namespaces(7), and pid_namespaces(7).
- Details / Change:
  1. Updated docs/03_SANDBOX_DEEP_DIVE.md:
     - Corrected user namespace lifecycle: established that /proc/[pid]/uid_map belongs to the user namespace the child process currently inhabits; the parent cannot map subordinate IDs until after the child invokes unshare(CLONE_NEWUSER).
     - Documented precise two-pipe synchronization handshake (child_to_parent_sync and parent_to_child_sync) guaranteeing Child 1 pauses while parent establishes cgroup placement and UID/GID mappings.
     - Documented PID namespace semantics from pid_namespaces(7): unshare(CLONE_NEWPID) does not move the calling process into the new namespace; subsequent children created via fork() inhabit the new namespace.
     - Documented the intermediate supervisor pattern: Child 1 calls unshare(CLONE_NEWPID) and forks Child 2; Child 2 is born as PID 1 of the new PID namespace, installs seccomp, and execs the target binary.
     - Explicitly stated architecture assumptions and reiterated that unshare ENOSYS in the current environment prevents executing this flow live.
  2. Updated LEARNING_NOTES/phase-1.md (local-only) with matching sequence explanations.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0045
- Piyush prediction (optional): The architecture will accurately reflect Linux kernel requirements, avoiding the invalid attempt to write uid_map before the user namespace exists.
- Tutor review: pending

## [LOG-0047] 2026-11-14 13:00 IST | PLAN | actor: agent
- Reason: Plan safe filesystem canary test contract and mock verification checks in sandbox-daemon/tests/evil/read_etc_passwd.c and scripts/test_phase2_diagnostics.py.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before modifying fixture code.
  2. Update sandbox-daemon/tests/evil/read_etc_passwd.c:
     - Accept dynamic unique canary file path via argv[1] or PES_CANARY_PATH env var (with fallback to /etc/pes_host_canary).
     - Accept unique canary verification token via argv[2] or PES_CANARY_TOKEN env var.
     - Distinguish canary content match (BREACH, exit 0), missing file (ENOENT -> BLOCKED_BY_POLICY, exit 1), permission/seccomp block (EACCES/EPERM -> BLOCKED_BY_POLICY, exit 1), and unrelated I/O or system failure (exit 2).
     - Document that canary files must be created and destroyed dynamically by the future test harness, never committed or created during preparation.
     - Preserve host user detection in /etc/passwd as fallback breach check.
  3. Update scripts/test_phase2_diagnostics.py:
     - Add source-level assertions checking for argv/env canary handling, token matching, and error code contracts.
     - Add mock classification tests for canary missing (ENOENT), permission denied (EACCES), unrelated I/O failure (EIO), and token leak.
     - Verify clean compilation with -Wall -Wextra -Werror without executing the fixture.
  4. Append LOG-0048 (DONE) and commit Part 2 changes.
- Files: PROGRESS.md, sandbox-daemon/tests/evil/read_etc_passwd.c, scripts/test_phase2_diagnostics.py
- Linked: LOG-0046
- Piyush prediction (optional): Dynamic canary path injection will make future filesystem containment tests deterministic without relying on hardcoded host paths.
- Tutor review: pending

## [LOG-0048] 2026-11-14 13:01 IST | DONE | actor: agent
- Reason: Complete safe filesystem canary test contract and mock verification checks in sandbox-daemon/tests/evil/read_etc_passwd.c and scripts/test_phase2_diagnostics.py.
- Details / Change:
  1. Updated sandbox-daemon/tests/evil/read_etc_passwd.c:
     - Added support for dynamic canary path via argv[1] and PES_CANARY_PATH environment variable (with fallback to /etc/pes_host_canary).
     - Added support for dynamic canary token verification via argv[2] and PES_CANARY_TOKEN environment variable.
     - Implemented deterministic outcome classification: token read match -> BREACH (exit 0); canary missing via ENOENT or access blocked via EACCES/EPERM -> BLOCKED_BY_POLICY (exit 1); container-local passwd without host user -> SAFE_LOCAL_RESOURCE (exit 1); host user leaked from passwd -> BREACH (exit 0); unrelated I/O or system errors -> UNEXPECTED_FAILURE (exit 2).
     - Documented the test contract: canary files must be generated dynamically with unique tokens and cleaned up in fixture teardown, never committed to git or created during readiness passes.
  2. Updated scripts/test_phase2_diagnostics.py:
     - Added source-level checks for argv/env canary handling, token matching, and error code contracts.
     - Added mock classification tests for canary missing (ENOENT), access denied (EACCES), token breach, and unexpected I/O errors (11/11 tests pass in 0.411s).
     - Verified clean compilation under -Wall -Wextra -Werror without running the attack.
  3. Verified make clean && make test in sandbox-daemon/ (18 Phase 1 regression tests pass, 3 Phase 2 remain pending).
- Files: PROGRESS.md, sandbox-daemon/tests/evil/read_etc_passwd.c, scripts/test_phase2_diagnostics.py
- Linked: LOG-0047
- Piyush prediction (optional): Dynamic canary path injection will make future filesystem containment tests deterministic without relying on hardcoded host paths.
- Tutor review: pending

## [LOG-0049] 2026-11-14 13:03 IST | PLAN | actor: agent
- Reason: Plan network fixture destination contract review, limits of ENETUNREACH documentation, and capability audit recheck.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before updating code and documentation.
  2. Update sandbox-daemon/tests/evil/net_connect.c:
     - Add support for configurable target IP and port via argv[1]/argv[2] or PES_NET_TARGET_IP/PES_NET_TARGET_PORT environment variables (with fallback to 93.184.216.34:80).
     - Document that hardcoding public IPs creates offline/CI false dependencies; future tests will bind a local host listener outside the sandbox.
     - Document that ECONNREFUSED proves network reachability (packets reached destination and received RST), rejecting it from isolation claims.
     - Disambiguate policy blocks (ENETUNREACH in empty netns, EPERM/EACCES in seccomp) from ambient errors (ETIMEDOUT, ECONNREFUSED, EHOSTUNREACH).
     - Maintain strict boundary: no live network connection attempts in this preparation task.
  3. Update scripts/test_phase2_diagnostics.py:
     - Add source-level checks for dynamic target IP/port support and ECONNREFUSED rejection.
     - Add mock classification tests for ECONNREFUSED (verifying it is classified as UNEXPECTED_FAILURE / REACHABILITY_LEAK rather than policy denial).
  4. Update docs/03_SANDBOX_DEEP_DIVE.md:
     - Document network fixture contracts, the semantics and limits of interpreting ENETUNREACH, and host calibration requirements.
     - Recheck and document the five capability audit categories (PRESENT, PROBE_SUCCEEDED, INFERRED, UNAVAILABLE, UNKNOWN).
  5. Run test suites and sandbox regression tests to verify.
  6. Append LOG-0050 (DONE) and commit Part 3 changes.
- Files: PROGRESS.md, sandbox-daemon/tests/evil/net_connect.c, scripts/test_phase2_diagnostics.py, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0048
- Piyush prediction (optional): Configurable network targets will allow future tests to use local listeners instead of unreliable public internet connections.
- Tutor review: pending

## [LOG-0050] 2026-11-14 13:03 IST | DONE | actor: agent
- Reason: Complete network fixture destination contract review, limits of ENETUNREACH documentation, and capability audit recheck.
- Details / Change:
  1. Updated sandbox-daemon/tests/evil/net_connect.c:
     - Added support for configurable target IP and port via argv[1]/argv[2] or PES_NET_TARGET_IP/PES_NET_TARGET_PORT environment variables (with fallback to 93.184.216.34:80).
     - Documented why hardcoding external public IPs introduces false dependencies on internet connectivity, and documented that future tests will use a local host TCP listener.
     - Documented that ECONNREFUSED is a reachability leak (proving packets reached the target host), and classified it as UNEXPECTED_FAILURE rather than policy denial.
     - Disambiguated policy blocks (ENETUNREACH, EPERM, EACCES) from ambient network failures (ETIMEDOUT, ECONNREFUSED).
  2. Updated scripts/test_phase2_diagnostics.py:
     - Added source-level checks for dynamic target IP/port parsing and ECONNREFUSED handling.
     - Added mock classification tests for ECONNREFUSED verifying it is rejected from isolation claims (12/12 tests pass in 0.481s).
  3. Updated docs/03_SANDBOX_DEEP_DIVE.md:
     - Added detailed section on Network Isolation Contracts & The Limits of Interpreting ENETUNREACH, explaining routing table dependence and host reachability calibration.
  4. Updated LEARNING_NOTES/phase-1.md (local-only) with matching systems notes and sleep/wall vs CPU timing explanations.
  5. Verified make clean && make test in sandbox-daemon/ (all 18 Phase 1 regression tests pass, 3 Phase 2 checks pending).
- Files: PROGRESS.md, sandbox-daemon/tests/evil/net_connect.c, scripts/test_phase2_diagnostics.py, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0049
- Piyush prediction (optional): Configurable network targets will allow future tests to use local listeners instead of unreliable public internet connections.
- Tutor review: pending















## [LOG-0051] 2026-11-14 13:31 IST | PLAN | actor: agent
- Reason: Plan audit of UID/GID mapping routes and setgroups compatibility contracts for Phase 2 namespace architecture.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before updating architecture docs and verification tests.
  2. Audit user_namespaces(7), setgroups(2), and newuidmap/newgidmap documentation to resolve setgroups(0, NULL) compatibility:
     - Route A (newuidmap/newgidmap helper route): Explain multi-ID mapping, elevated setuid permissions, why /proc/PID/setgroups does not need to be "deny", why setgroups(0, NULL) remains permitted in the child, and what presence of helpers does and does not prove (does not prove setuid bit, subuid/subgid ownership, lack of PR_SET_NO_NEW_PRIVS in caller, or kernel CLONE_NEWUSER support).
     - Route B (unprivileged direct-write fallback): Explain mandatory write of "deny" to /proc/PID/setgroups before writing gid_map (CVE-2014-8989), why writing "deny" permanently disables setgroups(2) in the user namespace, and why calling setgroups(0, NULL) fails with EPERM in that setup.
     - Document that neither route can execute on the current host because unshare(CLONE_NEWUSER) returns ENOSYS.
  3. Update docs/03_SANDBOX_DEEP_DIVE.md:
     - Add detailed analysis comparing Route A and Route B.
     - Clarify the child supplementary group dropping contract in Step 11 for both routes.
  4. Update scripts/test_phase2_diagnostics.py:
     - Add structured source and contract checks asserting that documentation and test contracts properly distinguish Route A vs Route B setgroups compatibility, helper existence vs capability, and ENOSYS host status.
  5. Update LEARNING_NOTES/phase-1.md (local-only, not committed) with detailed systems guide on CVE-2014-8989 and setgroups denial.
  6. Run test suites to verify.
  7. Append LOG-0052 (DONE) and commit Part 1 changes.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, scripts/test_phase2_diagnostics.py, LEARNING_NOTES/phase-1.md
- Linked: LOG-0050
- Piyush prediction (optional): Documenting the setgroups "deny" requirement will prevent EPERM bugs when falling back to unprivileged single-UID mappings.
- Tutor review: pending

## [LOG-0052] 2026-11-14 13:32 IST | DONE | actor: agent
- Reason: Complete audit of UID/GID mapping routes and setgroups compatibility contracts for Phase 2 namespace architecture.
- Details / Change:
  1. Updated docs/03_SANDBOX_DEEP_DIVE.md:
     - Clarified Step 11 in the Phase 2 isolation handshake diagram to distinguish Route A (where setgroups(0, NULL) is permitted under newgidmap) from Route B (where setgroups is denied by kernel rule).
     - Added comprehensive Section 6: "UID/GID Mapping Routes and the setgroups(2) Contract (user_namespaces(7))".
     - Documented CVE-2014-8989 rootless privilege dropping flaw and the /proc/[pid]/setgroups mechanism.
     - Documented Route A (newuidmap/newgidmap): multi-ID mapping, setuid requirement, /proc/[pid]/setgroups remaining "allow", and why setgroups(0, NULL) succeeds in the child.
     - Documented what helper presence proves (binary files exist) vs does not prove (setuid bit, subuid ranges, caller PR_SET_NO_NEW_PRIVS status, or kernel user namespace support).
     - Documented Route B (direct-write fallback): requirement to write "deny" to /proc/[pid]/setgroups before writing gid_map, permanent EPERM for setgroups(2), and the requirement for the child to skip setgroups(0, NULL).
     - Documented that unshare(CLONE_NEWUSER) returning ENOSYS prevents live execution of either route on the current host.
  2. Updated scripts/test_phase2_diagnostics.py:
     - Added test_uid_gid_mapping_and_setgroups_contract asserting structured documentation of Route A vs Route B, CVE-2014-8989, setgroups "deny" semantics, helper presence limits, and ENOSYS host status (13/13 tests pass in 0.373s).
  3. Updated LEARNING_NOTES/phase-1.md (local-only, not committed) with detailed systems guide on user namespace mapping contracts.
  4. Ran regression and diagnostic suites; all tests pass.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, scripts/test_phase2_diagnostics.py, LEARNING_NOTES/phase-1.md
- Linked: LOG-0051
- Piyush prediction (optional): Documenting the setgroups "deny" requirement will prevent EPERM bugs when falling back to unprivileged single-UID mappings.
- Tutor review: pending

## [LOG-0053] 2026-11-14 13:33 IST | PLAN | actor: agent
- Reason: Plan verification and documentation of PID namespace and procfs lifecycle ordering contracts.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before modifying architecture documentation and verification suites.
  2. Audit pid_namespaces(7), unshare(2), clone(2), and proc(5):
     - Document why unshare(CLONE_NEWPID) does not change caller PID namespace (PID immutability invariant).
     - Document that the first child created via fork() after unshare becomes PID 1 of the new PID namespace, while the helper process (Child 1) supervises and reaps it from the parent namespace.
     - Document procfs mount ordering: mounting procfs before Child 2 fork reflects the parent PID namespace (leaking host processes); mounting procfs after Child 2 fork (from within the new PID namespace) reflects only isolated processes in the container.
     - Document that when namespace PID 1 exits, the kernel immediately sends SIGKILL to all remaining processes in that PID namespace.
     - Mark host runtime behavior honestly: unshare(CLONE_NEWPID) probe returns ENOSYS on this host.
  3. Update docs/03_SANDBOX_DEEP_DIVE.md:
     - Update the handshake diagram to explicitly place procfs mounting in Child 2 (after fork) to guarantee the isolated PID view.
     - Add detailed Section 7: "PID Namespace Lifecycle, PID 1 Semantics, and Procfs Mount Ordering (pid_namespaces(7), proc(5))".
  4. Update scripts/test_phase2_diagnostics.py:
     - Add structured test asserting documentation of PID immutability, PID 1 creation and supervision, procfs mount ordering after fork, and PID 1 exit SIGKILL cascade.
  5. Update LEARNING_NOTES/phase-1.md (local-only, not committed) with matching systems concepts.
  6. Run test suites to verify.
  7. Append LOG-0054 (DONE) and commit Part 2 changes.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, scripts/test_phase2_diagnostics.py, LEARNING_NOTES/phase-1.md
- Linked: LOG-0052
- Piyush prediction (optional): Mounting procfs after Child 2 is born will guarantee that /proc contains only sandbox processes rather than host system processes.
- Tutor review: pending

## [LOG-0054] 2026-11-14 13:35 IST | DONE | actor: agent
- Reason: Complete verification and documentation of PID namespace and procfs lifecycle ordering contracts.
- Details / Change:
  1. Updated docs/03_SANDBOX_DEEP_DIVE.md:
     - Updated the Phase 2 isolation handshake diagram (Step 13) to explicitly place private procfs mounting inside Child 2 after fork, preventing host process table leaks.
     - Added comprehensive Section 7: "PID Namespace Lifecycle, PID 1 Semantics, and Procfs Mount Ordering (pid_namespaces(7), proc(5))".
     - Documented why unshare(CLONE_NEWPID) does not change caller PID namespace (immutable PID allocation invariant).
     - Documented PID 1 assignment and supervisor hierarchy (Child 1 helper supervises and reaps Child 2 from outer namespace).
     - Documented procfs mount ordering trap (mounting before fork leaks host PIDs; mounting after fork inside new PID namespace provides isolated process view).
     - Documented PID 1 termination semantics (kernel immediately sends SIGKILL to all remaining processes in the namespace).
     - Documented honest runtime status: unshare(CLONE_NEWPID) probe returns ENOSYS on this host.
  2. Updated scripts/test_phase2_diagnostics.py:
     - Added test_pid_namespace_and_procfs_lifecycle_contract verifying structured documentation of PID immutability, PID 1 supervision, procfs mount ordering, and PID 1 exit SIGKILL cascade (14/14 tests pass in 0.381s).
  3. Updated LEARNING_NOTES/phase-1.md (local-only, not committed) with matching systems concepts.
  4. Ran regression and diagnostic test suites; all tests pass.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, scripts/test_phase2_diagnostics.py, LEARNING_NOTES/phase-1.md
- Linked: LOG-0053
- Piyush prediction (optional): Mounting procfs after Child 2 is born will guarantee that /proc contains only sandbox processes rather than host system processes.
- Tutor review: pending

## [LOG-0055] 2026-11-14 13:36 IST | PLAN | actor: agent
- Reason: Plan documentation of runner argument/environment gap, safe future request interface, and capability audit distinction checks.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before updating architecture docs and verification test suites.
  2. Document the gap between current Tier A runner execve implementation and Phase 2 fixture configuration:
     - Current runner (sandbox-daemon/sandbox.c lines 313-316) passes only { binary_path, NULL } in child_argv and { NULL } in child_envp.
     - New Phase 2 fixtures (read_etc_passwd.c, net_connect.c) accept dynamic argv[1]/argv[2] and environment variables, but the runner cannot currently pass those values through.
     - Propose safe future request interface: explicit argument array (size-capped), explicit environment allowlist (no host inheritance), no shell command strings (no sh -c or system), and no user-controlled PATH search (strict execve, no execvp).
  3. Update docs/03_SANDBOX_DEEP_DIVE.md:
     - Add Section 8: "Target Arguments & Environment: Current Runner Gap & Safe Future Interface Contract".
  4. Update scripts/test_phase2_diagnostics.py:
     - Add test_runner_argument_environment_gap_contract verifying that runner source and deep-dive documentation align on the execve gap and the 4 safe interface principles.
     - Add test_capability_statuses_distinct_from_enforcement asserting that phase2_preflight.py audit categories strictly distinguish observed PRESENT files from INFERRED runtime enforcement and UNAVAILABLE features.
  5. Update LEARNING_NOTES/phase-1.md (local-only, not committed) with matching systems concepts.
  6. Run full verification suites: python3 scripts/test_phase2_diagnostics.py, python3 scripts/test_phase2_preflight.py, python3 scripts/phase2_preflight.py, and cd sandbox-daemon && make clean && make test.
  7. Append LOG-0056 (DONE) and commit Part 3 changes.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, scripts/test_phase2_diagnostics.py, LEARNING_NOTES/phase-1.md
- Linked: LOG-0054
- Piyush prediction (optional): Documenting explicit argv/env contracts now will prevent insecure shell-string patterns when implementing Phase 3 gRPC.
- Tutor review: pending

## [LOG-0056] 2026-11-14 13:38 IST | DONE | actor: agent
- Reason: Complete documentation of runner argument/environment gap, safe future request interface, and capability audit distinction checks.
- Details / Change:
  1. Updated docs/03_SANDBOX_DEEP_DIVE.md:
     - Added Section 8: "Target Arguments & Environment: Current Runner Gap & Safe Future Interface Contract".
     - Documented the current implementation gap in sandbox-daemon/sandbox.c lines 313-316 (where only { binary_path, NULL } and { NULL } are passed to execve) vs Phase 2 fixture capabilities.
     - Documented the four required interface security principles: explicit argument array, explicit environment allowlist, no shell command string (no system or sh -c), and no user-controlled PATH search (execve only, no execvp).
     - Maintained strict boundaries: sandbox.c and sandbox.h were not modified (Tier A), and no fixture options were claimed to work through sandbox-runner today.
  2. Updated scripts/test_phase2_diagnostics.py:
     - Added test_runner_argument_environment_gap_contract verifying that runner C code and deep-dive documentation align on the execve gap and the 4 safe interface principles.
     - Added test_capability_statuses_distinct_from_enforcement asserting that phase2_preflight.py audit categories strictly distinguish observed PRESENT files from INFERRED runtime enforcement and UNAVAILABLE features (16/16 tests pass in 0.424s).
  3. Updated LEARNING_NOTES/phase-1.md (local-only, not committed) with matching systems concepts.
  4. Verified all test suites: python3 scripts/test_phase2_preflight.py (6/6 pass), python3 scripts/test_phase2_diagnostics.py (16/16 pass), and cd sandbox-daemon && make clean && make test (18/18 pass).
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, scripts/test_phase2_diagnostics.py, LEARNING_NOTES/phase-1.md
- Linked: LOG-0055
- Piyush prediction (optional): Documenting explicit argv/env contracts now will prevent insecure shell-string patterns when implementing Phase 3 gRPC.
- Tutor review: pending

## [LOG-0057] 2026-11-14 13:46 IST | PLAN | actor: agent
- Reason: Plan identity and supplementary-group contract correction for Phase 2 user namespace architecture.
- Details / Change:
  1. Commit this PLAN entry alone to PROGRESS.md before updating architecture documentation.
  2. Audit credentials(7), user_namespaces(7), setgroups(2), and execve(2) regarding supplementary groups:
     - Distinguish real/effective/saved UIDs and GIDs from the supplementary group array (getgroups/setgroups).
     - Document why setgid() and setuid() do NOT clear or modify supplementary groups.
     - Document why supplementary groups survive execve(2).
     - Document Route A vs Route B supplementary group behavior:
       * Route A (newgidmap helper): Helper holds CAP_SETGID in parent namespace; /proc/PID/setgroups remains "allow"; Child 1 holds CAP_SETGID in new user namespace; Child 1 can and must call setgroups(0, NULL) before setgid/setuid; remains conditional on helper setuid permissions, subuid/subgid mapping, parent PR_SET_NO_NEW_PRIVS ordering, and kernel CLONE_NEWUSER support.
       * Route B (unprivileged direct-write): Writing "deny" to /proc/PID/setgroups permanently disables setgroups(2); process cannot clear supplementary groups; host supplementary groups (e.g. wheel, docker, sudo) survive into sandboxed binary across execve; mark Route B as UNSUPPORTED for secure multi-tenant execution.
  3. Update docs/03_SANDBOX_DEEP_DIVE.md:
     - Update Step 11 of the isolation handshake diagram.
     - Refine Section 6 to explain the supplementary group inheritance trap and mark Route B unsupported.
  4. Update LEARNING_NOTES/phase-1.md (local-only, not committed) with matching systems concepts.
  5. Run test suites to verify.
  6. Append LOG-0058 (DONE) and commit Part 1 changes.
- Files: PROGRESS.md, docs/03_SANDBOX_DEEP_DIVE.md, LEARNING_NOTES/phase-1.md
- Linked: LOG-0056
- Piyush prediction (optional): Clarifying that setgid/setuid do not clear supplementary groups will prevent subtle host privilege leaks into sandboxed processes.
- Tutor review: pending
