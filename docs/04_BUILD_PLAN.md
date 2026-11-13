# 04 — Build Plan (reordered: security early)

Each phase has a **Definition of Done (DoD)**. A phase is not complete until the tutor (Claude, in chat) has reviewed the agent's report AND Piyush has passed the "explain it back" check.

| Phase | Goal | DoD |
|---|---|---|
| 0 | Repo, rules, PROGRESS log, append-only git hook | Hook rejects an edit to an old log line (demo it). |
| 1 | Sandbox v1 in C: fork/exec/rlimits/pipes/poll/wait4/JSON | Runs a C program; correct statuses for loop, sleep, memory bomb, output flood; no zombies. |
| 2 | Sandbox v2 hardening | Fork bomb, file read, network attempt all fail safely; stray-process check clean. |
| 3 | Kotlin worker + gRPC + compile pipeline (compile also sandboxed) | C and Python run end-to-end from a gRPC client (grpcurl). |
| 4 | Java gateway + WebSocket + rooms | Test WebSocket client triggers a run and receives status events. |
| 5 | React + Monaco, run UI, metrics panel (single user) | Browser run works; statuses Idle/Compiling/Running/Timed Out render. |
| 6 | Yjs collaboration | Two browsers converge; cursors visible. |
| 7 | Java/Kotlin/Node support, Docker Compose, load test, README with honest benchmarks | 50 concurrent runs without host instability; documented limits. |

## Rule of thumb
Never start phase N+1 with a red test in phase N.
