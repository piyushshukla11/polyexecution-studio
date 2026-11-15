package pes.gateway.service;

public enum AcquireRunResult {
    ACQUIRED,
    NOT_IN_ROOM,
    ROOM_RUN_IN_PROGRESS,
    GLOBAL_CONCURRENCY_LIMIT_REACHED,
    RATE_LIMITED
}
