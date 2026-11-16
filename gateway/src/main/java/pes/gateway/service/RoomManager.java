package pes.gateway.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.ConcurrentWebSocketSessionDecorator;
import pes.gateway.config.GatewayProperties;
import pes.gateway.protocol.AssignedUserDto;
import pes.gateway.protocol.ServerEvent;

import java.io.IOException;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

@Component
public class RoomManager {
    private static final Logger log = LoggerFactory.getLogger(RoomManager.class);

    private final GatewayProperties properties;
    private final ObjectMapper objectMapper;

    private final Map<String, Room> rooms = new ConcurrentHashMap<>();
    private final Map<String, String> sessionToRoom = new ConcurrentHashMap<>();
    private final Map<String, WebSocketSession> activeSessions = new ConcurrentHashMap<>();

    private final AtomicInteger globalActiveRuns = new AtomicInteger(0);

    private static final String[] PALETTE = {
        "#30bced", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#be5046", "#d19a66", "#c678dd"
    };

    private AssignedUserDto createAssignedUser(String sessionId) {
        int colorIdx = Math.abs(sessionId.hashCode()) % PALETTE.length;
        String shortId = sessionId.length() > 6 ? sessionId.substring(0, 6) : sessionId;
        return new AssignedUserDto("User-" + shortId, PALETTE[colorIdx]);
    }

    public RoomManager(GatewayProperties properties, ObjectMapper objectMapper) {
        this.properties = properties;
        this.objectMapper = objectMapper;
    }

    public void registerSession(WebSocketSession session) {
        WebSocketSession safeSession = new ConcurrentWebSocketSessionDecorator(
            session,
            5000,
            properties.getMaxMessageBytes()
        );
        activeSessions.put(session.getId(), safeSession);
    }

    public void unregisterSession(String sessionId) {
        activeSessions.remove(sessionId);
    }

    public synchronized JoinResult joinRoom(String roomId, String sessionId) {
        // If session was already in another room, leave it first
        String existingRoomId = sessionToRoom.get(sessionId);
        if (existingRoomId != null && !existingRoomId.equals(roomId)) {
            leaveRoom(existingRoomId, sessionId);
        }

        Room room = rooms.get(roomId);
        if (room == null) {
            if (rooms.size() >= properties.getMaxRooms()) {
                return JoinResult.error("MAX_ROOMS_REACHED", "Maximum number of active rooms reached");
            }
            room = new Room(roomId);
            rooms.put(roomId, room);
        }

        if (room.getSessionIds().size() >= properties.getMaxSessionsPerRoom() && !room.hasSession(sessionId)) {
            return JoinResult.error("ROOM_FULL", "Room " + roomId + " has reached maximum capacity");
        }

        room.addSession(sessionId);
        sessionToRoom.put(sessionId, roomId);
        AssignedUserDto assignedUser = createAssignedUser(sessionId);

        return JoinResult.success(room.getMembers(), assignedUser);
    }

    public synchronized String leaveRoom(String roomId, String sessionId) {
        Room room = rooms.get(roomId);
        if (room != null) {
            room.removeSession(sessionId);
            if (room.isEmpty() && !room.isRunActive()) {
                rooms.remove(roomId);
            }
        }
        sessionToRoom.remove(sessionId);
        return roomId;
    }

    public synchronized String handleDisconnect(String sessionId) {
        unregisterSession(sessionId);
        String roomId = sessionToRoom.remove(sessionId);
        if (roomId != null) {
            Room room = rooms.get(roomId);
            if (room != null) {
                room.removeSession(sessionId);
                if (room.isEmpty() && !room.isRunActive()) {
                    rooms.remove(roomId);
                }
            }
        }
        return roomId;
    }

    public synchronized AcquireRunResult tryAcquireRun(String roomId, String sessionId, String requestId) {
        Room room = rooms.get(roomId);
        if (room == null || !room.hasSession(sessionId)) {
            return AcquireRunResult.NOT_IN_ROOM;
        }

        long now = System.currentTimeMillis();
        if (!room.checkRateLimit(properties.getRateLimitRunsPerMinute(), now)) {
            return AcquireRunResult.RATE_LIMITED;
        }

        if (globalActiveRuns.get() >= properties.getMaxGlobalConcurrentRuns()) {
            return AcquireRunResult.GLOBAL_CONCURRENCY_LIMIT_REACHED;
        }

        if (!room.tryAcquireRun(requestId)) {
            return AcquireRunResult.ROOM_RUN_IN_PROGRESS;
        }

        globalActiveRuns.incrementAndGet();
        room.recordRun(now);
        return AcquireRunResult.ACQUIRED;
    }

    public synchronized boolean releaseRun(String roomId, String requestId) {
        Room room = rooms.get(roomId);
        if (room != null && room.releaseRun(requestId)) {
            globalActiveRuns.decrementAndGet();
            if (room.isEmpty()) {
                rooms.remove(roomId);
            }
            return true;
        }
        return false;
    }

    public boolean isSessionInRoom(String roomId, String sessionId) {
        Room room = rooms.get(roomId);
        return room != null && room.hasSession(sessionId);
    }

    public String getRoomForSession(String sessionId) {
        return sessionToRoom.get(sessionId);
    }

    public List<String> getRoomMembers(String roomId) {
        Room room = rooms.get(roomId);
        return (room != null) ? room.getMembers() : Collections.emptyList();
    }

    public int getRoomCount() {
        return rooms.size();
    }

    public int getGlobalActiveRuns() {
        return globalActiveRuns.get();
    }

    public void broadcastToRoom(String roomId, ServerEvent event) {
        Room room = rooms.get(roomId);
        if (room == null) {
            return;
        }

        String jsonPayload;
        try {
            jsonPayload = objectMapper.writeValueAsString(event);
        } catch (Exception e) {
            log.error("Failed to serialize server event: {}", event.getType(), e);
            return;
        }

        TextMessage message = new TextMessage(jsonPayload);
        for (String sessionId : room.getSessionIds()) {
            WebSocketSession session = activeSessions.get(sessionId);
            if (session != null && session.isOpen()) {
                try {
                    session.sendMessage(message);
                } catch (IOException e) {
                    log.warn("Failed to send event to session {}: {}", sessionId, e.getMessage());
                }
            }
        }
    }

    public void broadcastToRoomExcept(String roomId, String excludedSessionId, ServerEvent event) {
        Room room = rooms.get(roomId);
        if (room == null) {
            return;
        }

        String jsonPayload;
        try {
            jsonPayload = objectMapper.writeValueAsString(event);
        } catch (Exception e) {
            log.error("Failed to serialize server event: {}", event.getType(), e);
            return;
        }

        TextMessage message = new TextMessage(jsonPayload);
        for (String sessionId : room.getSessionIds()) {
            if (sessionId.equals(excludedSessionId)) {
                continue;
            }
            WebSocketSession session = activeSessions.get(sessionId);
            if (session != null && session.isOpen()) {
                try {
                    session.sendMessage(message);
                } catch (IOException e) {
                    log.warn("Failed to send event to session {}: {}", sessionId, e.getMessage());
                }
            }
        }
    }

    public synchronized boolean addCollabUpdate(String roomId, String updateBase64) {
        Room room = rooms.get(roomId);
        if (room == null) {
            return false;
        }
        return room.addCollabUpdate(updateBase64, properties.getMaxRoomCollabHistory(), properties.getMaxRoomCollabHistoryBytes());
    }

    public boolean isCollabHistoryFull(String roomId) {
        Room room = rooms.get(roomId);
        return room != null && room.isCollabHistoryFull();
    }

    public List<String> getCollabUpdates(String roomId) {
        Room room = rooms.get(roomId);
        return (room != null) ? room.getCollabUpdates() : Collections.emptyList();
    }

    public boolean checkCollabUpdateRateLimit(String roomId, String sessionId, long nowMs) {
        Room room = rooms.get(roomId);
        return room != null && room.checkCollabUpdateRateLimit(sessionId, properties.getCollabRateLimitPerSec(), nowMs);
    }

    public boolean checkCollabAwarenessRateLimit(String roomId, String sessionId, long nowMs) {
        Room room = rooms.get(roomId);
        return room != null && room.checkCollabAwarenessRateLimit(sessionId, properties.getCollabRateLimitPerSec(), nowMs);
    }

    public boolean checkCollabRateLimit(String roomId, String sessionId, long nowMs) {
        return checkCollabUpdateRateLimit(roomId, sessionId, nowMs);
    }

    public boolean checkCollabSyncRateLimit(String roomId, String sessionId) {
        Room room = rooms.get(roomId);
        return room != null && room.checkCollabSyncRateLimit(sessionId, properties.getCollabSyncRateLimitPerMinute(), System.currentTimeMillis());
    }

    public AssignedUserDto getAssignedUser(String sessionId) {
        return createAssignedUser(sessionId);
    }

    public void sendToSession(String sessionId, ServerEvent event) {
        WebSocketSession session = activeSessions.get(sessionId);
        if (session == null || !session.isOpen()) {
            return;
        }

        try {
            String jsonPayload = objectMapper.writeValueAsString(event);
            session.sendMessage(new TextMessage(jsonPayload));
        } catch (Exception e) {
            log.warn("Failed to send event to session {}: {}", sessionId, e.getMessage());
        }
    }

    public record JoinResult(boolean success, String reason, String message, List<String> members, AssignedUserDto assignedUser) {
        public static JoinResult success(List<String> members, AssignedUserDto assignedUser) {
            return new JoinResult(true, null, null, members, assignedUser);
        }

        public static JoinResult error(String reason, String message) {
            return new JoinResult(false, reason, message, Collections.emptyList(), null);
        }
    }
}
