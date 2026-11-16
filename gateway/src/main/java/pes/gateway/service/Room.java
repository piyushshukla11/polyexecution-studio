package pes.gateway.service;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class Room {
    private final String roomId;
    private final Set<String> sessionIds = ConcurrentHashMap.newKeySet();
    private final AtomicBoolean runActive = new AtomicBoolean(false);
    private final ConcurrentLinkedQueue<Long> runTimestamps = new ConcurrentLinkedQueue<>();
    private final List<String> collabUpdates = new CopyOnWriteArrayList<>();
    private final AtomicInteger collabByteCount = new AtomicInteger(0);
    private final Map<String, ConcurrentLinkedQueue<Long>> sessionCollabTimestamps = new ConcurrentHashMap<>();
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
        sessionCollabTimestamps.remove(sessionId);
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

    public synchronized boolean addCollabUpdate(String updateBase64, int maxHistoryCount, int maxHistoryBytes) {
        if (updateBase64 == null || updateBase64.isBlank()) {
            return false;
        }
        int updateLength = updateBase64.length();
        if (collabUpdates.size() >= maxHistoryCount || collabByteCount.get() + updateLength > maxHistoryBytes) {
            return false;
        }
        collabUpdates.add(updateBase64);
        collabByteCount.addAndGet(updateLength);
        return true;
    }

    public List<String> getCollabUpdates() {
        return new ArrayList<>(collabUpdates);
    }

    public boolean checkCollabRateLimit(String sessionId, int maxPerSec, long nowMs) {
        long windowStart = nowMs - 1000L;
        ConcurrentLinkedQueue<Long> queue = sessionCollabTimestamps.computeIfAbsent(
            sessionId, k -> new ConcurrentLinkedQueue<>()
        );
        while (!queue.isEmpty()) {
            Long first = queue.peek();
            if (first != null && first < windowStart) {
                queue.poll();
            } else {
                break;
            }
        }
        if (queue.size() < maxPerSec) {
            queue.offer(nowMs);
            return true;
        }
        return false;
    }
}
