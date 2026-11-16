import { useState, useEffect, useRef, useCallback } from 'react';
import {
  SupportedLanguage,
  ResourceLimitsDto,
  ClientRunMessage,
  IncomingServerEvent,
  ExecutionUiStatus,
  ExecutionMetrics,
  AssignedUser,
  OutgoingMessage
} from '../types/protocol.ts';

export interface UseExecutionSocketOptions {
  url?: string;
  roomId?: string;
  maxReconnectAttempts?: number;
  onServerMessage?: (event: IncomingServerEvent) => void;
}

export function useExecutionSocket(options?: UseExecutionSocketOptions) {
  const wsUrl =
    options?.url ||
    (typeof window !== 'undefined' &&
      (window as unknown as { __PES_WS_URL?: string }).__PES_WS_URL) ||
    'ws://127.0.0.1:8080/ws';

  const [roomId] = useState<string>(
    () => options?.roomId || `room-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`
  );

  const maxReconnectAttempts = options?.maxReconnectAttempts ?? 5;

  const [connected, setConnected] = useState<boolean>(false);
  const [joined, setJoined] = useState<boolean>(false);
  const [sessionId, setSessionId] = useState<string | null>(null);
  const [assignedUser, setAssignedUser] = useState<AssignedUser | null>(null);
  const [connectionError, setConnectionError] = useState<string | null>(null);
  const [reconnectAttempt, setReconnectAttempt] = useState<number>(0);

  const [uiStatus, setUiStatus] = useState<ExecutionUiStatus>('IDLE');
  const [stageHistory, setStageHistory] = useState<ExecutionUiStatus[]>(['IDLE']);
  const [stdout, setStdout] = useState<string>('');
  const [stderr, setStderr] = useState<string>('');
  const [metrics, setMetrics] = useState<ExecutionMetrics | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const wsRef = useRef<WebSocket | null>(null);
  const currentRequestIdRef = useRef<string | null>(null);
  const sessionIdRef = useRef<string | null>(null);
  const onServerMessageRef = useRef(options?.onServerMessage);

  useEffect(() => {
    onServerMessageRef.current = options?.onServerMessage;
  });

  useEffect(() => {
    let isMounted = true;
    let ws: WebSocket | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    let attempts = 0;

    const connect = () => {
      if (!isMounted) return;

      try {
        ws = new WebSocket(wsUrl);
        wsRef.current = ws;
      } catch (err: unknown) {
        if (isMounted) {
          setConnectionError(err instanceof Error ? err.message : 'Failed to create WebSocket');
          scheduleReconnect();
        }
        return;
      }

      ws.onopen = () => {
        if (!isMounted) return;
        setConnected(true);
        // Automatically join / rejoin room upon connection
        ws?.send(JSON.stringify({ type: 'room.join', roomId }));
      };

      ws.onmessage = (event: MessageEvent) => {
        if (!isMounted) return;
        try {
          const data = JSON.parse(event.data) as IncomingServerEvent;
          handleServerEvent(data);
          onServerMessageRef.current?.(data);
        } catch (e: unknown) {
          console.error('Failed to parse incoming WebSocket message', e);
        }
      };

      ws.onerror = () => {
        if (!isMounted) return;
        setConnectionError('WebSocket encountered a network error');
      };

      ws.onclose = () => {
        if (!isMounted) return;
        sessionIdRef.current = null;
        setConnected(false);
        setJoined(false);
        setSessionId(null);

        // Abort in-flight run without replaying
        if (currentRequestIdRef.current !== null) {
          setUiStatus('ERROR');
          setStageHistory((prev) => [...prev, 'ERROR']);
          setErrorMessage('Connection closed while execution was in progress');
          currentRequestIdRef.current = null;
        }

        scheduleReconnect();
      };
    };

    const scheduleReconnect = () => {
      if (!isMounted) return;
      if (attempts < maxReconnectAttempts) {
        attempts += 1;
        setReconnectAttempt(attempts);
        const delay = Math.min(1000 * attempts, 5000);
        reconnectTimer = setTimeout(() => {
          if (isMounted) {
            connect();
          }
        }, delay);
      } else {
        setConnectionError('Connection lost; maximum reconnect attempts exceeded');
      }
    };

    const handleServerEvent = (event: IncomingServerEvent) => {
      switch (event.type) {
        case 'room.joined':
          if (event.roomId === roomId) {
            // Only adopt session identity if not yet established or matching current session
            if (!sessionIdRef.current || sessionIdRef.current === event.sessionId) {
              sessionIdRef.current = event.sessionId;
              setJoined(true);
              setSessionId(event.sessionId);
              if (event.assignedUser) {
                setAssignedUser(event.assignedUser);
              }
              attempts = 0;
              setReconnectAttempt(0);
              setConnectionError(null);
            }
          }
          break;

        case 'room.peer_joined':
          // Peer join notifications do not alter local session identity or assigned user
          break;

        case 'run.accepted':
          if (event.requestId === currentRequestIdRef.current) {
            setUiStatus('ACCEPTED');
            setStageHistory((prev) => [...prev, 'ACCEPTED']);
          }
          break;

        case 'run.progress':
          if (event.requestId === currentRequestIdRef.current) {
            if (event.stage === 'COMPILING') {
              setUiStatus('COMPILING');
              setStageHistory((prev) => [...prev, 'COMPILING']);
            } else if (event.stage === 'RUNNING') {
              setUiStatus('RUNNING');
              setStageHistory((prev) => [...prev, 'RUNNING']);
            }
          }
          break;

        case 'run.completed':
          if (event.requestId === currentRequestIdRef.current) {
            setStdout(event.stdout);
            setStderr(event.stderr);
            setMetrics({
              wallMs: event.wallMs,
              cpuMs: event.cpuMs,
              peakMemoryKb: event.peakMemoryKb,
              exitCode: event.exitCode,
              signal: event.signal,
              status: event.status
            });

            let finalStatus: ExecutionUiStatus = 'COMPLETED';
            if (event.status === 'OK') {
              finalStatus = 'COMPLETED';
            } else if (event.status === 'TIMEOUT') {
              finalStatus = 'TIMED_OUT';
            } else if (event.status === 'COMPILE_ERROR') {
              finalStatus = 'COMPILE_ERROR';
            } else if (event.status === 'RUNTIME_ERROR') {
              finalStatus = 'RUNTIME_ERROR';
            } else {
              finalStatus = 'ERROR';
              setErrorMessage(`Execution failed with status: ${event.status}`);
            }

            setUiStatus(finalStatus);
            setStageHistory((prev) => [...prev, finalStatus]);
            currentRequestIdRef.current = null;
          }
          break;

        case 'run.rejected':
          if (event.requestId === currentRequestIdRef.current) {
            setUiStatus('ERROR');
            setStageHistory((prev) => [...prev, 'ERROR']);
            setErrorMessage(`${event.reason}: ${event.message}`);
            currentRequestIdRef.current = null;
          }
          break;

        case 'error':
          setErrorMessage(event.message);
          break;

        default:
          break;
      }
    };

    connect();

    return () => {
      isMounted = false;
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
      }
      if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        ws.close();
      }
    };
  }, [wsUrl, roomId, maxReconnectAttempts]);

  const runCode = useCallback(
    (
      language: SupportedLanguage,
      sourceCode: string,
      stdin?: string,
      limits?: ResourceLimitsDto
    ): boolean => {
      if (!wsRef.current || wsRef.current.readyState !== WebSocket.OPEN || !joined) {
        setErrorMessage('Cannot execute: WebSocket is not connected and joined to room');
        return false;
      }

      const requestId = `req-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`;
      currentRequestIdRef.current = requestId;

      // Reset state for new run: transition to SUBMITTING (never ACCEPTED until gateway responds)
      setStdout('');
      setStderr('');
      setMetrics(null);
      setErrorMessage(null);
      setUiStatus('SUBMITTING');
      setStageHistory(['SUBMITTING']);

      const message: ClientRunMessage = {
        type: 'run',
        roomId,
        requestId,
        language,
        sourceCode,
        stdin: stdin || '',
        limits
      };

      wsRef.current.send(JSON.stringify(message));
      return true;
    },
    [roomId, joined]
  );

  const sendMessage = useCallback((msg: OutgoingMessage): boolean => {
    if (!wsRef.current || wsRef.current.readyState !== WebSocket.OPEN) {
      return false;
    }
    try {
      wsRef.current.send(JSON.stringify(msg));
      return true;
    } catch (e) {
      console.error('Failed to send WebSocket message', e);
      return false;
    }
  }, []);

  const resetState = useCallback(() => {
    setUiStatus('IDLE');
    setStageHistory(['IDLE']);
    setStdout('');
    setStderr('');
    setMetrics(null);
    setErrorMessage(null);
    currentRequestIdRef.current = null;
  }, []);

  return {
    roomId,
    connected,
    joined,
    sessionId,
    assignedUser,
    connectionError,
    reconnectAttempt,
    uiStatus,
    stageHistory,
    stdout,
    stderr,
    metrics,
    errorMessage,
    runCode,
    sendMessage,
    resetState
  };
}
