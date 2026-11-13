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
