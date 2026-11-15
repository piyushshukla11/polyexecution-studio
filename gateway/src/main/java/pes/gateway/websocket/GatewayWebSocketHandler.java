package pes.gateway.websocket;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.grpc.Status;
import io.grpc.StatusRuntimeException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;
import pes.execution.v1.ExecuteRequest;
import pes.execution.v1.ExecuteResponse;
import pes.gateway.config.GatewayProperties;
import pes.gateway.protocol.*;
import pes.gateway.service.*;

import java.util.concurrent.CompletionException;

@Component
public class GatewayWebSocketHandler extends TextWebSocketHandler {
    private static final Logger log = LoggerFactory.getLogger(GatewayWebSocketHandler.class);

    private final ObjectMapper objectMapper;
    private final RoomManager roomManager;
    private final RequestValidator validator;
    private final WorkerClient workerClient;
    private final GatewayProperties properties;

    public GatewayWebSocketHandler(
        ObjectMapper objectMapper,
        RoomManager roomManager,
        RequestValidator validator,
        WorkerClient workerClient,
        GatewayProperties properties
    ) {
        this.objectMapper = objectMapper;
        this.roomManager = roomManager;
        this.validator = validator;
        this.workerClient = workerClient;
        this.properties = properties;
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) {
        log.info("WebSocket connected: sessionId={}", session.getId());
        roomManager.registerSession(session);
    }

    @Override
    protected void handleTextMessage(WebSocketSession session, TextMessage message) {
        String payload = message.getPayload();
        if (payload.length() > properties.getMaxMessageBytes()) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("MESSAGE_TOO_LARGE", "Message payload exceeds allowed limit"));
            return;
        }

        JsonNode root;
        try {
            root = objectMapper.readTree(payload);
        } catch (Exception e) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("MALFORMED_JSON", "Invalid JSON format: " + e.getMessage()));
            return;
        }

        JsonNode typeNode = root.get("type");
        if (typeNode == null || !typeNode.isTextual() || typeNode.asText().isBlank()) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("INVALID_MESSAGE_TYPE", "Message 'type' field is required"));
            return;
        }

        String type = typeNode.asText().trim();
        switch (type) {
            case "room.join" -> handleRoomJoin(session, root);
            case "room.leave" -> handleRoomLeave(session, root);
            case "run" -> handleRun(session, root);
            default -> roomManager.sendToSession(session.getId(),
                new ErrorEvent("UNKNOWN_MESSAGE_TYPE", "Unknown message type: " + type));
        }
    }

    private void handleRoomJoin(WebSocketSession session, JsonNode root) {
        RoomJoinMessage joinMsg;
        try {
            joinMsg = objectMapper.treeToValue(root, RoomJoinMessage.class);
        } catch (Exception e) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("BAD_REQUEST", "Failed to parse room.join message"));
            return;
        }

        String roomId = joinMsg.getRoomId();
        if (!validator.isValidId(roomId)) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("INVALID_ROOM_ID", "Room ID must match ^[a-zA-Z0-9_-]{1,64}$"));
            return;
        }

        RoomManager.JoinResult result = roomManager.joinRoom(roomId, session.getId());
        if (!result.success()) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent(result.reason(), result.message()));
            return;
        }

        RoomJoinedEvent joinedEvent = new RoomJoinedEvent(roomId, session.getId(), result.members());
        roomManager.broadcastToRoom(roomId, joinedEvent);
    }

    private void handleRoomLeave(WebSocketSession session, JsonNode root) {
        RoomLeaveMessage leaveMsg;
        try {
            leaveMsg = objectMapper.treeToValue(root, RoomLeaveMessage.class);
        } catch (Exception e) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("BAD_REQUEST", "Failed to parse room.leave message"));
            return;
        }

        String roomId = leaveMsg.getRoomId();
        if (roomId == null || roomId.isBlank()) {
            roomId = roomManager.getRoomForSession(session.getId());
        }

        if (roomId == null || !roomManager.isSessionInRoom(roomId, session.getId())) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("NOT_IN_ROOM", "Session is not a member of room " + (roomId != null ? roomId : "")));
            return;
        }

        roomManager.leaveRoom(roomId, session.getId());
        roomManager.broadcastToRoom(roomId, new RoomLeftEvent(roomId, session.getId()));
    }

    private void handleRun(WebSocketSession session, JsonNode root) {
        RunMessage runMsg;
        try {
            runMsg = objectMapper.treeToValue(root, RunMessage.class);
        } catch (Exception e) {
            roomManager.sendToSession(session.getId(),
                new ErrorEvent("BAD_REQUEST", "Failed to parse run message: " + e.getMessage()));
            return;
        }

        ValidationResult valResult = validator.validateRunRequest(runMsg);
        if (!valResult.valid()) {
            String roomId = runMsg.getRoomId();
            String reqId = runMsg.getRequestId() != null ? runMsg.getRequestId() : "unknown";
            RunRejectedEvent rejected = new RunRejectedEvent(roomId, reqId, valResult.reason(), valResult.message());
            if (roomId != null && roomManager.isSessionInRoom(roomId, session.getId())) {
                roomManager.broadcastToRoom(roomId, rejected);
            } else {
                roomManager.sendToSession(session.getId(), rejected);
            }
            return;
        }

        String roomId = runMsg.getRoomId();
        String requestId = runMsg.getRequestId();

        if (!roomManager.isSessionInRoom(roomId, session.getId())) {
            roomManager.sendToSession(session.getId(),
                new RunRejectedEvent(roomId, requestId, "NOT_IN_ROOM", "Session must join room before submitting run"));
            return;
        }

        AcquireRunResult acquireResult = roomManager.tryAcquireRun(roomId, session.getId(), requestId);
        if (acquireResult != AcquireRunResult.ACQUIRED) {
            String message = switch (acquireResult) {
                case ROOM_RUN_IN_PROGRESS -> "Another execution is currently in progress for room " + roomId;
                case GLOBAL_CONCURRENCY_LIMIT_REACHED -> "Global execution concurrency limit reached; try again shortly";
                case RATE_LIMITED -> "Execution rate limit exceeded for room " + roomId;
                case NOT_IN_ROOM -> "Session is not a member of room " + roomId;
                default -> "Execution request rejected: " + acquireResult.name();
            };
            roomManager.broadcastToRoom(roomId,
                new RunRejectedEvent(roomId, requestId, acquireResult.name(), message));
            return;
        }

        // Emit truthful run.accepted event
        roomManager.broadcastToRoom(roomId,
            new RunAcceptedEvent(roomId, requestId, runMsg.getLanguage().toLowerCase()));

        ExecuteRequest protoRequest = ExecuteRequest.newBuilder()
            .setJobId(requestId)
            .setLanguage(valResult.protoLanguage())
            .setSourceCode(runMsg.getSourceCode())
            .setStdin(runMsg.getStdin() != null ? runMsg.getStdin() : "")
            .setLimits(valResult.protoLimits())
            .build();

        workerClient.executeAsync(protoRequest)
            .whenComplete((response, throwable) -> {
                try {
                    if (throwable != null) {
                        handleExecutionError(roomId, requestId, throwable);
                    } else {
                        handleExecutionSuccess(roomId, requestId, response);
                    }
                } finally {
                    roomManager.releaseRun(roomId, requestId);
                }
            });
    }

    private void handleExecutionSuccess(String roomId, String requestId, ExecuteResponse response) {
        RunCompletedEvent completedEvent = new RunCompletedEvent(
            roomId,
            requestId,
            response.getStatus().name(),
            response.getStdout(),
            response.getStderr(),
            response.getExitCode(),
            response.getSignal(),
            response.getWallMs(),
            response.getCpuMs(),
            response.getPeakMemoryKb()
        );
        roomManager.broadcastToRoom(roomId, completedEvent);
    }

    private void handleExecutionError(String roomId, String requestId, Throwable throwable) {
        Throwable cause = (throwable instanceof CompletionException && throwable.getCause() != null)
            ? throwable.getCause()
            : throwable;

        String status = "SANDBOX_ERROR";
        String errorMsg = "Execution failed: " + cause.getMessage();

        if (cause instanceof StatusRuntimeException sre) {
            Status.Code code = sre.getStatus().getCode();
            if (code == Status.Code.UNAVAILABLE) {
                errorMsg = "Worker execution service is unavailable";
            } else if (code == Status.Code.DEADLINE_EXCEEDED) {
                status = "TIMEOUT";
                errorMsg = "Execution deadline exceeded by gateway";
            }
        }

        RunCompletedEvent completedEvent = new RunCompletedEvent(
            roomId,
            requestId,
            status,
            "",
            errorMsg,
            -1,
            0,
            0L,
            0L,
            0L
        );
        roomManager.broadcastToRoom(roomId, completedEvent);
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        log.info("WebSocket disconnected: sessionId={}, status={}", session.getId(), status);
        String roomId = roomManager.handleDisconnect(session.getId());
        if (roomId != null) {
            roomManager.broadcastToRoom(roomId, new RoomLeftEvent(roomId, session.getId()));
        }
    }

    @Override
    public void handleTransportError(WebSocketSession session, Throwable exception) {
        log.warn("WebSocket transport error for session {}: {}", session.getId(), exception.getMessage());
    }
}
