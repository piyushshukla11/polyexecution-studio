package pes.gateway.service;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicBoolean;

public class Room {
    private final String roomId;
    private final Set<String> sessionIds = ConcurrentHashMap.newKeySet();
    private final AtomicBoolean runActive = new AtomicBoolean(false);
    private final ConcurrentLinkedQueue<Long> runTimestamps = new ConcurrentLinkedQueue<>();
    private volatile String activeRequestId = null;

    public Room(String roomId) {
        this.roomId = roomId;
    }

    public String getRoomId() {
        return roomId;
    }

    public Set<String> getSessionIds() {
        return sessionIds;
    }

    public List<String> getMembers() {
        return new ArrayList<>(sessionIds);
    }

    public boolean addSession(String sessionId) {
        return sessionIds.add(sessionId);
    }

    public boolean removeSession(String sessionId) {
        return sessionIds.remove(sessionId);
    }

    public boolean isEmpty() {
        return sessionIds.isEmpty();
    }

    public boolean hasSession(String sessionId) {
        return sessionIds.contains(sessionId);
    }

    public boolean checkRateLimit(int maxRunsPerMinute, long nowMs) {
        long windowStart = nowMs - 60_000L;
        while (!runTimestamps.isEmpty()) {
            Long first = runTimestamps.peek();
            if (first != null && first < windowStart) {
                runTimestamps.poll();
            } else {
                break;
            }
        }
        return runTimestamps.size() < maxRunsPerMinute;
    }

    public void recordRun(long nowMs) {
        runTimestamps.offer(nowMs);
    }

    public synchronized boolean tryAcquireRun(String requestId) {
        if (requestId == null || requestId.isBlank()) {
            return false;
        }
        if (runActive.compareAndSet(false, true)) {
            this.activeRequestId = requestId;
            return true;
        }
        return false;
    }

    public synchronized boolean releaseRun(String requestId) {
        if (requestId != null && Objects.equals(this.activeRequestId, requestId)) {
            this.activeRequestId = null;
            this.runActive.set(false);
            return true;
        }
        return false;
    }

    public boolean isRunActive() {
        return runActive.get();
    }

    public String getActiveRequestId() {
        return activeRequestId;
    }
}
