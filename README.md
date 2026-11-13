# PolyExecution Studio (PES) — Documentation Pack

Start here:
1. `AI_RULES.md` — rules the coding agent must follow (give it to the agent first)
2. `PROGRESS.md` — append-only log
3. `docs/01_PROJECT_OVERVIEW.md` … `docs/06_LEARNING_RULES.md`

Setup: new repo → put these files in the root → `git init` (or clone) → `git config core.hooksPath scripts/hooks` → first commit on `main`, then work on `phase/N-name` branches.
`HANDOFF_PROMPT.md` is for pasting into a new Claude chat; it is git-ignored and not part of the repo.
