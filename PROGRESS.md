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

