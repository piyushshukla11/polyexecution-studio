import { useState, useEffect, useRef, useCallback } from 'react';
import {
  SupportedLanguage,
  ResourceLimitsDto,
  ClientRunMessage,
  IncomingServerEvent,
  ExecutionUiStatus,
  ExecutionMetrics
} from '../types/protocol.ts';

export interface UseExecutionSocketOptions {
  url?: string;
  roomId?: string;
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

  const [connected, setConnected] = useState<boolean>(false);
  const [joined, setJoined] = useState<boolean>(false);
  const [connectionError, setConnectionError] = useState<string | null>(null);

  const [uiStatus, setUiStatus] = useState<ExecutionUiStatus>('IDLE');
  const [stdout, setStdout] = useState<string>('');
  const [stderr, setStderr] = useState<string>('');
  const [metrics, setMetrics] = useState<ExecutionMetrics | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const wsRef = useRef<WebSocket | null>(null);
  const currentRequestIdRef = useRef<string | null>(null);

  useEffect(() => {
    let isMounted = true;
    let ws: WebSocket;

    try {
      ws = new WebSocket(wsUrl);
      wsRef.current = ws;
    } catch (err: unknown) {
      if (isMounted) {
        setConnectionError(err instanceof Error ? err.message : 'Failed to create WebSocket');
      }
      return;
    }

    ws.onopen = () => {
      if (!isMounted) return;
      setConnected(true);
      setConnectionError(null);
      // Join room upon connection
      ws.send(JSON.stringify({ type: 'room.join', roomId }));
    };

    ws.onmessage = (event: MessageEvent) => {
      if (!isMounted) return;
      try {
        const data = JSON.parse(event.data) as IncomingServerEvent;
        handleServerEvent(data);
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
      setConnected(false);
      setJoined(false);
      if (currentRequestIdRef.current !== null) {
        setUiStatus('ERROR');
        setErrorMessage('Connection closed while execution was in progress');
        currentRequestIdRef.current = null;
      }
    };

    const handleServerEvent = (event: IncomingServerEvent) => {
      switch (event.type) {
        case 'room.joined':
          if (event.roomId === roomId) {
            setJoined(true);
          }
          break;

        case 'run.accepted':
          if (event.requestId === currentRequestIdRef.current) {
            setUiStatus('ACCEPTED');
          }
          break;

        case 'run.progress':
          if (event.requestId === currentRequestIdRef.current) {
            if (event.stage === 'COMPILING') {
              setUiStatus('COMPILING');
            } else if (event.stage === 'RUNNING') {
              setUiStatus('RUNNING');
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

            if (event.status === 'OK') {
              setUiStatus('COMPLETED');
            } else if (event.status === 'TIMEOUT') {
              setUiStatus('TIMED_OUT');
            } else if (event.status === 'COMPILE_ERROR') {
              setUiStatus('COMPILE_ERROR');
            } else if (event.status === 'RUNTIME_ERROR') {
              setUiStatus('RUNTIME_ERROR');
            } else {
              setUiStatus('ERROR');
              setErrorMessage(`Execution failed with status: ${event.status}`);
            }
            currentRequestIdRef.current = null;
          }
          break;

        case 'run.rejected':
          if (event.requestId === currentRequestIdRef.current) {
            setUiStatus('ERROR');
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

    return () => {
      isMounted = false;
      if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
        ws.close();
      }
    };
  }, [wsUrl, roomId]);

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

      // Reset state for new run
      setStdout('');
      setStderr('');
      setMetrics(null);
      setErrorMessage(null);
      setUiStatus('ACCEPTED');

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

  const resetState = useCallback(() => {
    setUiStatus('IDLE');
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
    connectionError,
    uiStatus,
    stdout,
    stderr,
    metrics,
    errorMessage,
    runCode,
    resetState
  };
}
