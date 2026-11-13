# 06 — Learning Rules (so AI makes you stronger, not weaker)

1. **Predict before you run.** Write one line in PROGRESS.md: "I expect X." Then run. Wrong predictions are where learning happens.
2. **Explain-back.** After each AI change, explain it in your own words to the tutor (Claude chat). If you can't, it isn't done.
3. **Rewrite-from-memory test.** For core code (sandbox), close the file and rewrite the key function roughly. Compare.
4. **Break it on purpose.** Remove one line (e.g. close of the pipe end) and observe the failure. Understanding = knowing what breaks.
5. **Small steps.** One concept per change. No 500-line AI dumps.
6. **Read the man pages.** For every syscall: `man 2 fork`, `man 2 setrlimit`, `man 2 wait4`. The agent must cite which page applies.
7. **Core zones are yours.** Tier A code (see AI_RULES.md) is typed by you with the agent as reviewer.
8. **Keep a "Why not?" list.** For each decision, one rejected alternative and why (feeds interview answers).
9. **Weekly mock interview.** Ask the tutor for 5 random questions from `05_INTERVIEW_PREP.md`.
10. **Never trust AI on security.** Every security claim must be demonstrated by a failing-then-passing evil test.
11. **Debug without AI first for 15 minutes.** Then ask for hints, not solutions.
12. **Glossary habit.** New term → one line in a personal glossary.
13. **Commit small, commit often.** Each commit message references a LOG id.
14. **Teach it.** Explain a phase to a friend or write a short blog note; teaching exposes gaps.
15. **Be honest in the log.** If you didn't understand something, write "NOT UNDERSTOOD" and revisit.
