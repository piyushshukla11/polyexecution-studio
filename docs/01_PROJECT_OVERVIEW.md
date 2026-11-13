# 01 — What Are We Actually Building?

## One-sentence version
PES is a website where several people edit the same code together, press **Run**, and the code is compiled and executed on a server inside a locked box, with output and metrics sent back live.

## The two hard problems hiding inside it
1. **Shared state:** two people type at once and both must end up with the same text (collaboration, CRDT/Yjs).
2. **Untrusted code execution:** strangers' code runs on YOUR machine. It may loop forever, eat all RAM, fork-bomb, read your files, or open network connections. The sandbox exists so that none of that hurts the host.

Everything else (React, gRPC, Spring) is plumbing that carries source code to the sandbox and results back.

## Mental model: a restaurant
- **Client (React/Monaco):** the customer writing an order.
- **Gateway (Java):** the front-desk. Knows who is in which room, forwards orders.
- **Worker (Kotlin):** the kitchen manager. Queues jobs, writes files, calls compilers.
- **Sandbox (C):** a locked, monitored cooking cell. The dish (user program) cooks inside with strict limits on time, memory, and what it can touch. If it misbehaves, the cell kills it.

## What "success" means for this project
- You can run a hostile program (fork bomb, memory bomb, infinite loop, file read, network call) and PES survives and reports cleanly.
- You can explain every layer, from `fork()` to the WebSocket frame, without notes.
- Two browsers edit one document and converge.

## Languages supported (target)
C, Java, Kotlin, Python, JavaScript (Node). Start with **C only**, then Python, then the JVM languages (they are the hardest to sandbox because the JVM reserves huge virtual memory and spawns threads).

## Scope guard (what we will NOT do early)
- No user accounts, billing, or databases until execution is solid.
- No collaboration until Phase 6.
- No Kubernetes. Docker Compose at the end is enough.
