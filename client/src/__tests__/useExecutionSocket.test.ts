import { renderHook, act } from '@testing-library/react';
import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { useExecutionSocket } from '../hooks/useExecutionSocket.ts';

class MockWebSocket {
  static instances: MockWebSocket[] = [];
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;

  url: string;
  readyState: number = MockWebSocket.CONNECTING;
  onopen: (() => void) | null = null;
  onmessage: ((e: { data: string }) => void) | null = null;
  onerror: (() => void) | null = null;
  onclose: (() => void) | null = null;
  sentMessages: string[] = [];

  constructor(url: string) {
    this.url = url;
    MockWebSocket.instances.push(this);
  }

  send(data: string) {
    this.sentMessages.push(data);
  }

  close() {
    this.readyState = MockWebSocket.CLOSED;
    this.onclose?.();
  }

  simulateOpen() {
    this.readyState = MockWebSocket.OPEN;
    this.onopen?.();
  }

  simulateMessage(data: object) {
    this.onmessage?.({ data: JSON.stringify(data) });
  }
}

describe('useExecutionSocket', () => {
  beforeEach(() => {
    MockWebSocket.instances = [];
    vi.stubGlobal('WebSocket', MockWebSocket);
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('connects and automatically joins room on open', () => {
    const { result } = renderHook(() => useExecutionSocket({ roomId: 'test-room' }));
    expect(result.current.connected).toBe(false);

    const ws = MockWebSocket.instances[0];
    expect(ws).toBeDefined();

    act(() => {
      ws.simulateOpen();
    });

    expect(result.current.connected).toBe(true);
    expect(ws.sentMessages.length).toBe(1);
    const joinMsg = JSON.parse(ws.sentMessages[0]);
    expect(joinMsg.type).toBe('room.join');
    expect(joinMsg.roomId).toBe('test-room');

    act(() => {
      ws.simulateMessage({
        type: 'room.joined',
        roomId: 'test-room',
        sessionId: 'sess-1',
        memberCount: 1
      });
    });

    expect(result.current.joined).toBe(true);
  });

  it('runs complete execution lifecycle: Accepted -> Compiling -> Running -> Completed', () => {
    const { result } = renderHook(() => useExecutionSocket({ roomId: 'test-room' }));
    const ws = MockWebSocket.instances[0];

    act(() => {
      ws.simulateOpen();
      ws.simulateMessage({
        type: 'room.joined',
        roomId: 'test-room',
        sessionId: 'sess-1',
        memberCount: 1
      });
    });

    // Initial state
    expect(result.current.uiStatus).toBe('IDLE');

    // Run code
    let success = false;
    act(() => {
      success = result.current.runCode('c', 'int main() { return 0; }');
    });
    expect(success).toBe(true);
    // Submitting state must be shown immediately, NOT Accepted
    expect(result.current.uiStatus).toBe('SUBMITTING');

    const runMsg = JSON.parse(ws.sentMessages[1]);
    expect(runMsg.type).toBe('run');
    const reqId = runMsg.requestId;

    // Simulate run.accepted: transitions to ACCEPTED only now
    act(() => {
      ws.simulateMessage({
        type: 'run.accepted',
        roomId: 'test-room',
        requestId: reqId,
        language: 'c'
      });
    });
    expect(result.current.uiStatus).toBe('ACCEPTED');

    // Simulate run.progress COMPILING
    act(() => {
      ws.simulateMessage({
        type: 'run.progress',
        roomId: 'test-room',
        requestId: reqId,
        stage: 'COMPILING'
      });
    });
    expect(result.current.uiStatus).toBe('COMPILING');

    // Simulate run.progress RUNNING
    act(() => {
      ws.simulateMessage({
        type: 'run.progress',
        roomId: 'test-room',
        requestId: reqId,
        stage: 'RUNNING'
      });
    });
    expect(result.current.uiStatus).toBe('RUNNING');

    // Simulate run.completed
    act(() => {
      ws.simulateMessage({
        type: 'run.completed',
        roomId: 'test-room',
        requestId: reqId,
        status: 'OK',
        stdout: 'Hello World\n',
        stderr: '',
        exitCode: 0,
        signal: 0,
        wallMs: 25,
        cpuMs: 12,
        peakMemoryKb: 1024
      });
    });

    expect(result.current.uiStatus).toBe('COMPLETED');
    expect(result.current.stageHistory).toEqual([
      'SUBMITTING',
      'ACCEPTED',
      'COMPILING',
      'RUNNING',
      'COMPLETED'
    ]);
    expect(result.current.stdout).toBe('Hello World\n');
    expect(result.current.metrics?.wallMs).toBe(25);
    expect(result.current.metrics?.exitCode).toBe(0);
  });

  it('halts on C compile error without emitting running state', () => {
    const { result } = renderHook(() => useExecutionSocket({ roomId: 'test-room' }));
    const ws = MockWebSocket.instances[0];

    act(() => {
      ws.simulateOpen();
      ws.simulateMessage({
        type: 'room.joined',
        roomId: 'test-room',
        sessionId: 'sess-1',
        memberCount: 1
      });
    });

    act(() => {
      result.current.runCode('c', 'invalid c');
    });

    const runMsg = JSON.parse(ws.sentMessages[1]);
    const reqId = runMsg.requestId;

    // Progress: COMPILING
    act(() => {
      ws.simulateMessage({
        type: 'run.progress',
        roomId: 'test-room',
        requestId: reqId,
        stage: 'COMPILING'
      });
    });
    expect(result.current.uiStatus).toBe('COMPILING');

    // Complete: COMPILE_ERROR
    act(() => {
      ws.simulateMessage({
        type: 'run.completed',
        roomId: 'test-room',
        requestId: reqId,
        status: 'COMPILE_ERROR',
        stdout: '',
        stderr: 'syntax error on line 1\n',
        exitCode: 1,
        signal: 0,
        wallMs: 15,
        cpuMs: 8,
        peakMemoryKb: 512
      });
    });

    expect(result.current.uiStatus).toBe('COMPILE_ERROR');
    expect(result.current.stageHistory).toEqual([
      'SUBMITTING',
      'COMPILING',
      'COMPILE_ERROR'
    ]);
    expect(result.current.stageHistory).not.toContain('RUNNING');
    expect(result.current.stderr).toBe('syntax error on line 1\n');
  });

  it('renders TIMED_OUT status truthfully on worker TIMEOUT response', () => {
    const { result } = renderHook(() => useExecutionSocket({ roomId: 'test-room' }));
    const ws = MockWebSocket.instances[0];

    act(() => {
      ws.simulateOpen();
      ws.simulateMessage({
        type: 'room.joined',
        roomId: 'test-room',
        sessionId: 'sess-1',
        memberCount: 1
      });
    });

    act(() => {
      result.current.runCode('c', 'while(1);');
    });

    const reqId = JSON.parse(ws.sentMessages[1]).requestId;

    act(() => {
      ws.simulateMessage({
        type: 'run.completed',
        roomId: 'test-room',
        requestId: reqId,
        status: 'TIMEOUT',
        stdout: '',
        stderr: 'Process exceeded time limit\n',
        exitCode: 124,
        signal: 9,
        wallMs: 2005,
        cpuMs: 1002,
        peakMemoryKb: 1024
      });
    });

    expect(result.current.uiStatus).toBe('TIMED_OUT');
    expect(result.current.metrics?.status).toBe('TIMEOUT');
  });

  it('reconnects with backoff and rejoins room without replaying in-flight run', () => {
    vi.useFakeTimers();
    try {
      const { result } = renderHook(() =>
        useExecutionSocket({ roomId: 'test-room-reconn', maxReconnectAttempts: 3 })
      );
      const ws1 = MockWebSocket.instances[0];

      act(() => {
        ws1.simulateOpen();
        ws1.simulateMessage({
          type: 'room.joined',
          roomId: 'test-room-reconn',
          sessionId: 'sess-1',
          memberCount: 1
        });
      });
      expect(result.current.connected).toBe(true);
      expect(result.current.joined).toBe(true);

      // Start an in-flight run
      act(() => {
        result.current.runCode('c', 'int main() {}');
      });
      expect(result.current.uiStatus).toBe('SUBMITTING');

      // Socket drops unexpectedly
      act(() => {
        ws1.close();
      });
      expect(result.current.connected).toBe(false);
      expect(result.current.joined).toBe(false);
      // In-flight run must be aborted with ERROR
      expect(result.current.uiStatus).toBe('ERROR');

      // Fast-forward backoff timer for attempt 1 (1000ms)
      act(() => {
        vi.advanceTimersByTime(1000);
      });

      // A second WebSocket instance is created for reconnect
      expect(MockWebSocket.instances.length).toBe(2);
      const ws2 = MockWebSocket.instances[1];

      // Second socket connects
      act(() => {
        ws2.simulateOpen();
      });
      expect(result.current.connected).toBe(true);

      // Automatically sent room.join to rejoin the room
      expect(ws2.sentMessages.length).toBe(1);
      const rejoinMsg = JSON.parse(ws2.sentMessages[0]);
      expect(rejoinMsg.type).toBe('room.join');
      expect(rejoinMsg.roomId).toBe('test-room-reconn');

      // CRITICAL: The old in-flight run is NOT replayed
      const runMessages = ws2.sentMessages.filter(
        (m) => JSON.parse(m).type === 'run'
      );
      expect(runMessages.length).toBe(0);
    } finally {
      vi.useRealTimers();
    }
  });
});
