package pes.gateway;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import pes.gateway.config.GatewayProperties;
import pes.gateway.protocol.ErrorEvent;
import pes.gateway.service.AcquireRunResult;
import pes.gateway.service.RoomManager;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class RoomManagerTest {

    private RoomManager roomManager;
    private GatewayProperties properties;
    private ObjectMapper objectMapper;

    @BeforeEach
    void setUp() {
        properties = new GatewayProperties();
        properties.setMaxRooms(3);
        properties.setMaxSessionsPerRoom(2);
        properties.setMaxGlobalConcurrentRuns(2);
        properties.setRateLimitRunsPerMinute(5);

        objectMapper = new ObjectMapper();
        roomManager = new RoomManager(properties, objectMapper);
    }

    @Test
    @DisplayName("Session joins room successfully and receives member list")
    void testJoinRoomAddsMember() {
        RoomManager.JoinResult res1 = roomManager.joinRoom("room-1", "session-1");
        assertTrue(res1.success());
        assertEquals(List.of("session-1"), res1.members());
        assertTrue(roomManager.isSessionInRoom("room-1", "session-1"));

        RoomManager.JoinResult res2 = roomManager.joinRoom("room-1", "session-2");
        assertTrue(res2.success());
        assertEquals(2, res2.members().size());
        assertTrue(res2.members().contains("session-1"));
        assertTrue(res2.members().contains("session-2"));
    }

    @Test
    @DisplayName("Session joining another room automatically leaves old room")
    void testJoinDifferentRoomLeavesOldRoom() {
        roomManager.joinRoom("room-1", "session-1");
        assertTrue(roomManager.isSessionInRoom("room-1", "session-1"));
        assertEquals(1, roomManager.getRoomCount());

        roomManager.joinRoom("room-2", "session-1");
        assertFalse(roomManager.isSessionInRoom("room-1", "session-1"));
        assertTrue(roomManager.isSessionInRoom("room-2", "session-1"));
        // room-1 was left empty, so it should be cleaned up
        assertEquals(1, roomManager.getRoomCount());
    }

    @Test
    @DisplayName("Room rejects additional sessions when maxSessionsPerRoom is reached")
    void testMaxSessionsPerRoom() {
        roomManager.joinRoom("room-1", "s1");
        roomManager.joinRoom("room-1", "s2");

        RoomManager.JoinResult res3 = roomManager.joinRoom("room-1", "s3");
        assertFalse(res3.success());
        assertEquals("ROOM_FULL", res3.reason());
    }

    @Test
    @DisplayName("Manager rejects new room when maxRooms is reached")
    void testMaxRoomsLimit() {
        roomManager.joinRoom("room-1", "s1");
        roomManager.joinRoom("room-2", "s2");
        roomManager.joinRoom("room-3", "s3");

        RoomManager.JoinResult res4 = roomManager.joinRoom("room-4", "s4");
        assertFalse(res4.success());
        assertEquals("MAX_ROOMS_REACHED", res4.reason());
    }

    @Test
    @DisplayName("Leaving room cleans up room when empty")
    void testLeaveRoomCleansUpEmptyRoom() {
        roomManager.joinRoom("room-1", "s1");
        assertEquals(1, roomManager.getRoomCount());

        roomManager.leaveRoom("room-1", "s1");
        assertEquals(0, roomManager.getRoomCount());
        assertFalse(roomManager.isSessionInRoom("room-1", "s1"));
    }

    @Test
    @DisplayName("Disconnect cleans up session membership and empty room")
    void testDisconnectCleansUp() {
        roomManager.joinRoom("room-1", "s1");
        assertEquals("room-1", roomManager.getRoomForSession("s1"));

        String leftRoom = roomManager.handleDisconnect("s1");
        assertEquals("room-1", leftRoom);
        assertEquals(0, roomManager.getRoomCount());
        assertNull(roomManager.getRoomForSession("s1"));
    }

    @Test
    @DisplayName("Single active run per room policy: second run rejected with ROOM_RUN_IN_PROGRESS")
    void testOneActiveRunPerRoom() {
        roomManager.joinRoom("room-1", "s1");

        AcquireRunResult r1 = roomManager.tryAcquireRun("room-1", "s1", "req-1");
        assertEquals(AcquireRunResult.ACQUIRED, r1);
        assertEquals(1, roomManager.getGlobalActiveRuns());

        // Second run in same room is rejected
        AcquireRunResult r2 = roomManager.tryAcquireRun("room-1", "s1", "req-2");
        assertEquals(AcquireRunResult.ROOM_RUN_IN_PROGRESS, r2);

        // Release run allows next run
        roomManager.releaseRun("room-1");
        assertEquals(0, roomManager.getGlobalActiveRuns());

        AcquireRunResult r3 = roomManager.tryAcquireRun("room-1", "s1", "req-3");
        assertEquals(AcquireRunResult.ACQUIRED, r3);
        assertEquals(1, roomManager.getGlobalActiveRuns());

        roomManager.releaseRun("room-1");
    }

    @Test
    @DisplayName("Global concurrency limit rejects runs across different rooms")
    void testGlobalConcurrencyLimit() {
        properties.setMaxGlobalConcurrentRuns(2);

        roomManager.joinRoom("room-1", "s1");
        roomManager.joinRoom("room-2", "s2");
        roomManager.joinRoom("room-3", "s3");

        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-1", "s1", "req-1"));
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-2", "s2", "req-2"));
        assertEquals(2, roomManager.getGlobalActiveRuns());

        // Third run in a third room rejected due to global concurrency limit
        assertEquals(AcquireRunResult.GLOBAL_CONCURRENCY_LIMIT_REACHED,
            roomManager.tryAcquireRun("room-3", "s3", "req-3"));

        roomManager.releaseRun("room-1");
        assertEquals(1, roomManager.getGlobalActiveRuns());

        // Now room-3 can acquire
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-3", "s3", "req-3"));

        roomManager.releaseRun("room-2");
        roomManager.releaseRun("room-3");
    }

    @Test
    @DisplayName("Rate limiting rejects runs exceeding window threshold")
    void testRateLimiting() {
        properties.setRateLimitRunsPerMinute(2);

        roomManager.joinRoom("room-1", "s1");

        // 1st run
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-1", "s1", "r1"));
        roomManager.releaseRun("room-1");

        // 2nd run
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-1", "s1", "r2"));
        roomManager.releaseRun("room-1");

        // 3rd run within same minute -> RATE_LIMITED
        assertEquals(AcquireRunResult.RATE_LIMITED, roomManager.tryAcquireRun("room-1", "s1", "r3"));
    }

    @Test
    @DisplayName("Broadcasting delivers exclusively to sessions within the target room")
    void testCrossRoomEventIsolation() throws IOException {
        WebSocketSession s1 = mock(WebSocketSession.class);
        when(s1.getId()).thenReturn("s1");
        when(s1.isOpen()).thenReturn(true);

        WebSocketSession s2 = mock(WebSocketSession.class);
        when(s2.getId()).thenReturn("s2");
        when(s2.isOpen()).thenReturn(true);

        roomManager.registerSession(s1);
        roomManager.registerSession(s2);

        roomManager.joinRoom("room-1", "s1");
        roomManager.joinRoom("room-2", "s2");

        ErrorEvent event = new ErrorEvent("TEST_CODE", "Test message for room 1");
        roomManager.broadcastToRoom("room-1", event);

        verify(s1, times(1)).sendMessage(any(TextMessage.class));
        verify(s2, never()).sendMessage(any(TextMessage.class));
    }
}
