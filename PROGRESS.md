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
