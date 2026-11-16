package pes.gateway.config;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.stereotype.Component;

import java.util.List;

@Component
@ConfigurationProperties(prefix = "gateway")
public class GatewayProperties {
    private String workerGrpcTarget = "127.0.0.1:50051";
    private List<String> allowedOrigins = List.of(
        "http://127.0.0.1:8080",
        "http://localhost:8080",
        "http://127.0.0.1:3000",
        "http://localhost:3000",
        "http://127.0.0.1:5173",
        "http://localhost:5173"
    );
    private int maxRooms = 100;
    private int maxSessionsPerRoom = 20;
    private int maxGlobalConcurrentRuns = 16;
    private long maxAggregateMemoryMb = 8192L;
    private int rateLimitRunsPerMinute = 30;
    private int maxMessageBytes = 512 * 1024; // 512 KiB
    private int maxSourceBytes = 256 * 1024;  // 256 KiB
    private int maxStdinBytes = 64 * 1024;    // 64 KiB
    private int maxCollabUpdateBytes = 64 * 1024; // 64 KiB
    private int maxAwarenessBytes = 16 * 1024;    // 16 KiB
    private int maxRoomCollabHistory = 2000;      // max 2000 updates in room memory
    private int maxRoomCollabHistoryBytes = 2 * 1024 * 1024; // 2 MiB
    private int collabRateLimitPerSec = 120;      // max 120 collab messages / sec per session
    private int collabSyncRateLimitPerMinute = 10; // max 10 collab sync requests / min per session

    public String getWorkerGrpcTarget() {
        return workerGrpcTarget;
    }

    public void setWorkerGrpcTarget(String workerGrpcTarget) {
        this.workerGrpcTarget = workerGrpcTarget;
    }

    public List<String> getAllowedOrigins() {
        return allowedOrigins;
    }

    public void setAllowedOrigins(List<String> allowedOrigins) {
        this.allowedOrigins = allowedOrigins;
    }

    public int getMaxRooms() {
        return maxRooms;
    }

    public void setMaxRooms(int maxRooms) {
        this.maxRooms = maxRooms;
    }

    public int getMaxSessionsPerRoom() {
        return maxSessionsPerRoom;
    }

    public void setMaxSessionsPerRoom(int maxSessionsPerRoom) {
        this.maxSessionsPerRoom = maxSessionsPerRoom;
    }

    public int getMaxGlobalConcurrentRuns() {
        return maxGlobalConcurrentRuns;
    }

    public void setMaxGlobalConcurrentRuns(int maxGlobalConcurrentRuns) {
        this.maxGlobalConcurrentRuns = maxGlobalConcurrentRuns;
    }

    public long getMaxAggregateMemoryMb() {
        return maxAggregateMemoryMb;
    }

    public void setMaxAggregateMemoryMb(long maxAggregateMemoryMb) {
        this.maxAggregateMemoryMb = maxAggregateMemoryMb;
    }

    public int getRateLimitRunsPerMinute() {
        return rateLimitRunsPerMinute;
    }

    public void setRateLimitRunsPerMinute(int rateLimitRunsPerMinute) {
        this.rateLimitRunsPerMinute = rateLimitRunsPerMinute;
    }

    public int getMaxMessageBytes() {
        return maxMessageBytes;
    }

    public void setMaxMessageBytes(int maxMessageBytes) {
        this.maxMessageBytes = maxMessageBytes;
    }

    public int getMaxSourceBytes() {
        return maxSourceBytes;
    }

    public void setMaxSourceBytes(int maxSourceBytes) {
        this.maxSourceBytes = maxSourceBytes;
    }

    public int getMaxStdinBytes() {
        return maxStdinBytes;
    }

    public void setMaxStdinBytes(int maxStdinBytes) {
        this.maxStdinBytes = maxStdinBytes;
    }

    public int getMaxCollabUpdateBytes() {
        return maxCollabUpdateBytes;
    }

    public void setMaxCollabUpdateBytes(int maxCollabUpdateBytes) {
        this.maxCollabUpdateBytes = maxCollabUpdateBytes;
    }

    public int getMaxAwarenessBytes() {
        return maxAwarenessBytes;
    }

    public void setMaxAwarenessBytes(int maxAwarenessBytes) {
        this.maxAwarenessBytes = maxAwarenessBytes;
    }

    public int getMaxRoomCollabHistory() {
        return maxRoomCollabHistory;
    }

    public void setMaxRoomCollabHistory(int maxRoomCollabHistory) {
        this.maxRoomCollabHistory = maxRoomCollabHistory;
    }

    public int getMaxRoomCollabHistoryBytes() {
        return maxRoomCollabHistoryBytes;
    }

    public void setMaxRoomCollabHistoryBytes(int maxRoomCollabHistoryBytes) {
        this.maxRoomCollabHistoryBytes = maxRoomCollabHistoryBytes;
    }

    public int getCollabRateLimitPerSec() {
        return collabRateLimitPerSec;
    }

    public void setCollabRateLimitPerSec(int collabRateLimitPerSec) {
        this.collabRateLimitPerSec = collabRateLimitPerSec;
    }

    public int getCollabSyncRateLimitPerMinute() {
        return collabSyncRateLimitPerMinute;
    }

    public void setCollabSyncRateLimitPerMinute(int collabSyncRateLimitPerMinute) {
        this.collabSyncRateLimitPerMinute = collabSyncRateLimitPerMinute;
    }
}
