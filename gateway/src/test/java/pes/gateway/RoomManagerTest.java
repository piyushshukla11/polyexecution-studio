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
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.*;
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
        properties.setMaxRooms(20);
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
        properties.setMaxRooms(3);
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
        assertTrue(roomManager.releaseRun("room-1", "req-1"));
        assertEquals(0, roomManager.getGlobalActiveRuns());

        AcquireRunResult r3 = roomManager.tryAcquireRun("room-1", "s1", "req-3");
        assertEquals(AcquireRunResult.ACQUIRED, r3);
        assertEquals(1, roomManager.getGlobalActiveRuns());

        assertTrue(roomManager.releaseRun("room-1", "req-3"));
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

        assertTrue(roomManager.releaseRun("room-1", "req-1"));
        assertEquals(1, roomManager.getGlobalActiveRuns());

        // Now room-3 can acquire
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-3", "s3", "req-3"));

        assertTrue(roomManager.releaseRun("room-2", "req-2"));
        assertTrue(roomManager.releaseRun("room-3", "req-3"));
    }

    @Test
    @DisplayName("Rate limiting rejects runs exceeding window threshold")
    void testRateLimiting() {
        properties.setRateLimitRunsPerMinute(2);

        roomManager.joinRoom("room-1", "s1");

        // 1st run
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-1", "s1", "r1"));
        assertTrue(roomManager.releaseRun("room-1", "r1"));

        // 2nd run
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-1", "s1", "r2"));
        assertTrue(roomManager.releaseRun("room-1", "r2"));

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

    @Test
    @DisplayName("Concurrent acquisitions across multiple threads strictly enforce global concurrency limit")
    void testConcurrentAcquisitionsAtomic() throws Exception {
        int limit = 3;
        properties.setMaxGlobalConcurrentRuns(limit);
        properties.setMaxRooms(20);

        int totalThreads = 10;
        ExecutorService executor = Executors.newFixedThreadPool(totalThreads);
        CountDownLatch startLatch = new CountDownLatch(1);
        List<Future<AcquireRunResult>> futures = new ArrayList<>();

        for (int i = 0; i < totalThreads; i++) {
            String roomId = "conc-room-" + i;
            String sessionId = "conc-session-" + i;
            String requestId = "conc-req-" + i;
            roomManager.joinRoom(roomId, sessionId);

            futures.add(executor.submit(() -> {
                startLatch.await();
                return roomManager.tryAcquireRun(roomId, sessionId, requestId);
            }));
        }

        startLatch.countDown();

        int acquiredCount = 0;
        int rejectedCount = 0;
        for (Future<AcquireRunResult> f : futures) {
            AcquireRunResult res = f.get(5, TimeUnit.SECONDS);
            if (res == AcquireRunResult.ACQUIRED) {
                acquiredCount++;
            } else if (res == AcquireRunResult.GLOBAL_CONCURRENCY_LIMIT_REACHED) {
                rejectedCount++;
            }
        }

        executor.shutdown();
        assertEquals(limit, acquiredCount, "Exactly maxGlobalConcurrentRuns must be acquired");
        assertEquals(totalThreads - limit, rejectedCount, "Remainder must be rejected with GLOBAL_CONCURRENCY_LIMIT_REACHED");
        assertEquals(limit, roomManager.getGlobalActiveRuns());
    }

    @Test
    @DisplayName("Last member disconnect during active run preserves room and prevents duplicate run on rejoin")
    void testLastMemberDisconnectWhileRunActive() {
        roomManager.joinRoom("room-disconnect-test", "s1");
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-disconnect-test", "s1", "req-active"));
        assertEquals(1, roomManager.getGlobalActiveRuns());

        // Last member disconnects while run is in progress
        String leftRoom = roomManager.handleDisconnect("s1");
        assertEquals("room-disconnect-test", leftRoom);

        // Room must NOT be pruned while run is active, and globalActiveRuns must NOT decrement
        assertEquals(1, roomManager.getRoomCount(), "Room must remain retained while run is in flight");
        assertEquals(1, roomManager.getGlobalActiveRuns(), "Global active slot must not be decremented on disconnect");

        // A new session s2 joins the same room while run is still executing
        RoomManager.JoinResult joinResult = roomManager.joinRoom("room-disconnect-test", "s2");
        assertTrue(joinResult.success());

        // New session cannot start another run in the same room while previous is active
        assertEquals(AcquireRunResult.ROOM_RUN_IN_PROGRESS,
            roomManager.tryAcquireRun("room-disconnect-test", "s2", "req-new"));

        // Previous run completes
        assertTrue(roomManager.releaseRun("room-disconnect-test", "req-active"));
        assertEquals(0, roomManager.getGlobalActiveRuns());

        // Now s2 can start a run
        assertEquals(AcquireRunResult.ACQUIRED,
            roomManager.tryAcquireRun("room-disconnect-test", "s2", "req-new"));
        assertEquals(1, roomManager.getGlobalActiveRuns());

        assertTrue(roomManager.releaseRun("room-disconnect-test", "req-new"));
        assertEquals(0, roomManager.getGlobalActiveRuns());

        // Now s2 leaves, room is empty and not active -> pruned
        roomManager.leaveRoom("room-disconnect-test", "s2");
        assertEquals(0, roomManager.getRoomCount());
    }

    @Test
    @DisplayName("Stale releaseRun call with mismatched requestId is rejected and does not clear active run")
    void testStaleReleaseRunRejected() {
        roomManager.joinRoom("room-stale-test", "s1");
        assertEquals(AcquireRunResult.ACQUIRED, roomManager.tryAcquireRun("room-stale-test", "s1", "req-valid"));
        assertEquals(1, roomManager.getGlobalActiveRuns());

        // Call releaseRun with wrong/stale requestId
        assertFalse(roomManager.releaseRun("room-stale-test", "req-stale-wrong"));
        assertEquals(1, roomManager.getGlobalActiveRuns(), "Global slot must not decrement on stale release");

        // Another run in same room is still rejected
        assertEquals(AcquireRunResult.ROOM_RUN_IN_PROGRESS,
            roomManager.tryAcquireRun("room-stale-test", "s1", "req-second"));

        // Valid release succeeds
        assertTrue(roomManager.releaseRun("room-stale-test", "req-valid"));
        assertEquals(0, roomManager.getGlobalActiveRuns());
    }

    @Test
    @DisplayName("Collab updates are stored and bounded by max history count and byte limit")
    void testCollabUpdateStorageAndBounds() {
        properties.setMaxRoomCollabHistory(3);
        properties.setMaxRoomCollabHistoryBytes(50);

        roomManager.joinRoom("collab-room", "s1");

        // Add 2 valid updates
        assertTrue(roomManager.addCollabUpdate("collab-room", "update-1"));
        assertTrue(roomManager.addCollabUpdate("collab-room", "update-2"));
        assertEquals(List.of("update-1", "update-2"), roomManager.getCollabUpdates("collab-room"));

        // Third update reaches count limit (3)
        assertTrue(roomManager.addCollabUpdate("collab-room", "update-3"));
        assertEquals(3, roomManager.getCollabUpdates("collab-room").size());

        // Fourth update rejected due to count limit
        assertFalse(roomManager.addCollabUpdate("collab-room", "update-4"));

        // Non-existent room update returns false
        assertFalse(roomManager.addCollabUpdate("nonexistent-room", "update-5"));
    }

    @Test
    @DisplayName("Collab rate limit throttles messages exceeding per-second budget")
    void testCollabRateLimitEnforcement() {
        properties.setCollabRateLimitPerSec(2);
        roomManager.joinRoom("rate-room", "s1");

        long now = 10000L;
        assertTrue(roomManager.checkCollabRateLimit("rate-room", "s1", now));
        assertTrue(roomManager.checkCollabRateLimit("rate-room", "s1", now));
        // Third message in same second rejected
        assertFalse(roomManager.checkCollabRateLimit("rate-room", "s1", now));

        // In next second, limit resets
        assertTrue(roomManager.checkCollabRateLimit("rate-room", "s1", now + 1001L));
    }

    @Test
    @DisplayName("broadcastToRoomExcept delivers message to peers and excludes sender")
    void testBroadcastToRoomExcept() throws IOException {
        properties.setMaxSessionsPerRoom(10);

        WebSocketSession session1 = mock(WebSocketSession.class);
        when(session1.getId()).thenReturn("s1");
        when(session1.isOpen()).thenReturn(true);

        WebSocketSession session2 = mock(WebSocketSession.class);
        when(session2.getId()).thenReturn("s2");
        when(session2.isOpen()).thenReturn(true);

        WebSocketSession session3 = mock(WebSocketSession.class);
        when(session3.getId()).thenReturn("s3");
        when(session3.isOpen()).thenReturn(true);

        roomManager.registerSession(session1);
        roomManager.registerSession(session2);
        roomManager.registerSession(session3);

        roomManager.joinRoom("broadcast-room", "s1");
        roomManager.joinRoom("broadcast-room", "s2");
        roomManager.joinRoom("broadcast-room", "s3");

        ErrorEvent testEvent = new ErrorEvent("TEST", "hello");
        roomManager.broadcastToRoomExcept("broadcast-room", "s2", testEvent);

        verify(session1, times(1)).sendMessage(any(TextMessage.class));
        verify(session2, never()).sendMessage(any(TextMessage.class));
        verify(session3, times(1)).sendMessage(any(TextMessage.class));
    }

    @Test
    @DisplayName("Session joining room receives assignedUser with deterministic name and color")
    void testAssignedUserGeneratedOnJoin() {
        RoomManager.JoinResult res1 = roomManager.joinRoom("user-room", "session-alpha");
        assertTrue(res1.success());
        assertNotNull(res1.assignedUser());
        assertTrue(res1.assignedUser().getName().startsWith("User-"));
        assertTrue(res1.assignedUser().getColor().startsWith("#"));
    }
}
