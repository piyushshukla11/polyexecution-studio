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
    private final AtomicBoolean collabHistoryFull = new AtomicBoolean(false);
    private final AtomicInteger nextCollabClientId = new AtomicInteger(1);
    private final Map<String, Integer> sessionCollabClientIds = new ConcurrentHashMap<>();
    private final Map<String, ConcurrentLinkedQueue<Long>> sessionCollabUpdateTimestamps = new ConcurrentHashMap<>();
    private final Map<String, ConcurrentLinkedQueue<Long>> sessionCollabAwarenessTimestamps = new ConcurrentHashMap<>();
    private final Map<String, ConcurrentLinkedQueue<Long>> sessionSyncTimestamps = new ConcurrentHashMap<>();
    private volatile String activeRequestId = null;
    private volatile long activeRunMemoryMb = 0L;

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
        sessionCollabClientIds.computeIfAbsent(sessionId, s -> nextCollabClientId.getAndIncrement());
        return sessionIds.add(sessionId);
    }

    public boolean removeSession(String sessionId) {
        sessionCollabUpdateTimestamps.remove(sessionId);
        sessionCollabAwarenessTimestamps.remove(sessionId);
        sessionSyncTimestamps.remove(sessionId);
        sessionCollabClientIds.remove(sessionId);
        return sessionIds.remove(sessionId);
    }

    public Integer getSessionCollabClientId(String sessionId) {
        return sessionCollabClientIds.get(sessionId);
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

    public synchronized boolean tryAcquireRun(String requestId, long memoryMb) {
        if (requestId == null || requestId.isBlank()) {
            return false;
        }
        if (runActive.compareAndSet(false, true)) {
            this.activeRequestId = requestId;
            this.activeRunMemoryMb = memoryMb > 0 ? memoryMb : 64L;
            return true;
        }
        return false;
    }

    public synchronized boolean tryAcquireRun(String requestId) {
        return tryAcquireRun(requestId, 64L);
    }

    public synchronized long releaseRunWithMemory(String requestId) {
        if (requestId != null && Objects.equals(this.activeRequestId, requestId)) {
            this.activeRequestId = null;
            long released = this.activeRunMemoryMb;
            this.activeRunMemoryMb = 0L;
            this.runActive.set(false);
            return released > 0 ? released : 64L;
        }
        return 0L;
    }

    public synchronized boolean releaseRun(String requestId) {
        return releaseRunWithMemory(requestId) > 0;
    }

    public boolean isRunActive() {
        return runActive.get();
    }

    public String getActiveRequestId() {
        return activeRequestId;
    }

    public long getActiveRunMemoryMb() {
        return activeRunMemoryMb;
    }

    public synchronized boolean addCollabUpdate(String updateBase64, int maxHistoryCount, int maxHistoryBytes) {
        if (updateBase64 == null || updateBase64.isBlank() || collabHistoryFull.get()) {
            return false;
        }
        int updateLength = updateBase64.length();
        if (collabByteCount.get() + updateLength > maxHistoryBytes) {
            collabHistoryFull.set(true);
            return false;
        }
        collabUpdates.add(updateBase64);
        collabByteCount.addAndGet(updateLength);
        if (collabUpdates.size() >= maxHistoryCount || collabByteCount.get() >= maxHistoryBytes) {
            collabHistoryFull.set(true);
        }
        return true;
    }

    public boolean isCollabHistoryFull() {
        return collabHistoryFull.get();
    }

    public List<String> getCollabUpdates() {
        return new ArrayList<>(collabUpdates);
    }

    public boolean checkCollabSyncRateLimit(String sessionId, int maxPerMinute, long nowMs) {
        long windowStart = nowMs - 60_000L;
        ConcurrentLinkedQueue<Long> queue = sessionSyncTimestamps.computeIfAbsent(
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
        if (queue.size() < maxPerMinute) {
            queue.offer(nowMs);
            return true;
        }
        return false;
    }

    public boolean checkCollabUpdateRateLimit(String sessionId, int maxPerSec, long nowMs) {
        return checkRateLimitQueue(sessionCollabUpdateTimestamps, sessionId, maxPerSec, nowMs, 1000L);
    }

    public boolean checkCollabAwarenessRateLimit(String sessionId, int maxPerSec, long nowMs) {
        return checkRateLimitQueue(sessionCollabAwarenessTimestamps, sessionId, maxPerSec, nowMs, 1000L);
    }

    public boolean checkCollabRateLimit(String sessionId, int maxPerSec, long nowMs) {
        return checkCollabUpdateRateLimit(sessionId, maxPerSec, nowMs);
    }

    private boolean checkRateLimitQueue(Map<String, ConcurrentLinkedQueue<Long>> map, String sessionId, int maxCount, long nowMs, long windowMs) {
        long windowStart = nowMs - windowMs;
        ConcurrentLinkedQueue<Long> queue = map.computeIfAbsent(
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
        if (queue.size() < maxCount) {
            queue.offer(nowMs);
            return true;
        }
        return false;
    }
}
