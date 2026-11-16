import React, { useState, useCallback, useRef, useEffect } from 'react';
import type { OnMount } from '@monaco-editor/react';
import { SupportedLanguage, IncomingServerEvent } from './types/protocol.ts';
import { useExecutionSocket } from './hooks/useExecutionSocket.ts';
import { useCollaboration, C_STARTER, PYTHON_STARTER } from './hooks/useCollaboration.ts';
import { LanguageSelector } from './components/LanguageSelector.tsx';
import { CodeEditor } from './components/CodeEditor.tsx';
import { OutputPanel } from './components/OutputPanel.tsx';
import { MetricsPanel } from './components/MetricsPanel.tsx';
import { StatusBadge } from './components/StatusBadge.tsx';

function getInitialRoomId(): string {
  if (typeof window !== 'undefined' && window.location) {
    const params = new URLSearchParams(window.location.search);
    const roomParam = params.get('room');
    if (roomParam && /^[a-zA-Z0-9_-]{1,64}$/.test(roomParam)) {
      return roomParam;
    }
  }
  return `room-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`;
}

export const App: React.FC = () => {
  const [roomId, setRoomId] = useState<string>(getInitialRoomId);
  const [roomInput, setRoomInput] = useState<string>('');
  const [stdin, setStdin] = useState<string>('');

  const collabHandlerRef = useRef<(event: IncomingServerEvent) => void>();

  const {
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
    resetState: resetSocketState
  } = useExecutionSocket({
    roomId,
    onServerMessage: (event) => collabHandlerRef.current?.(event)
  });

  const collab = useCollaboration({
    roomId,
    joined,
    sessionId,
    assignedUser,
    sendMessage,
    initialLanguage: 'c'
  });

  useEffect(() => {
    collabHandlerRef.current = collab.processServerMessage;
  }, [collab.processServerMessage]);

  const isRunning =
    uiStatus === 'SUBMITTING' ||
    uiStatus === 'ACCEPTED' ||
    uiStatus === 'COMPILING' ||
    uiStatus === 'RUNNING';

  const handleLanguageChange = (newLang: SupportedLanguage) => {
    if (isRunning || collab.isLocked) return;
    collab.setSharedLanguage(newLang);
  };

  const handleRun = () => {
    runCode(collab.language, collab.sourceCode, stdin);
  };

  const handleReset = () => {
    if (isRunning || collab.isLocked) return;
    resetSocketState();
    const starter = collab.language === 'c' ? C_STARTER : PYTHON_STARTER;
    collab.setSharedCode(starter);
  };

  const handleJoinRoom = (e?: React.FormEvent) => {
    e?.preventDefault();
    const targetRoom = roomInput.trim();
    if (targetRoom && /^[a-zA-Z0-9_-]{1,64}$/.test(targetRoom)) {
      if (typeof window !== 'undefined' && window.location) {
        const url = new URL(window.location.href);
        url.searchParams.set('room', targetRoom);
        window.history.pushState({}, '', url.toString());
      }
      setRoomId(targetRoom);
      setRoomInput('');
    }
  };

  const handleEditorMount: OnMount = useCallback(
    (editor) => {
      return collab.bindEditor(editor);
    },
    [collab.bindEditor]
  );

  return (
    <div className="pes-app" data-testid="pes-app">
      <header className="app-header">
        <div className="header-brand">
          <h1 className="header-title">PolyExecution Studio</h1>
          <span className="header-subtitle">Linux-isolated Multi-language Execution Environment</span>
        </div>
        <div className="header-status">
          {collab.collaborators.length > 0 && (
            <div className="collaborators-list" data-testid="collaborators-list">
              {collab.collaborators.map((c) => (
                <span
                  key={c.clientId}
                  className="collaborator-pill"
                  data-testid={`collaborator-${c.name}`}
                  data-client-id={c.clientId}
                  data-is-local={c.isLocal ? 'true' : 'false'}
                  style={{ borderColor: c.color }}
                >
                  <span className="collaborator-dot" style={{ backgroundColor: c.color }} />
                  {c.name}{c.isLocal ? ' (You)' : ''}
                </span>
              ))}
            </div>
          )}

          <span
            className={`connection-indicator ${connected && joined ? 'connected' : 'disconnected'}`}
            data-testid="connection-status"
          >
            {connected && joined
              ? `Room: ${roomId}`
              : reconnectAttempt > 0
              ? `Reconnecting (${reconnectAttempt})...`
              : 'Disconnected'}
          </span>

          <form className="room-control" onSubmit={handleJoinRoom}>
            <input
              type="text"
              className="room-input"
              data-testid="room-input"
              placeholder="Join room..."
              value={roomInput}
              onChange={(e) => setRoomInput(e.target.value)}
            />
            <button
              type="submit"
              className="btn btn-secondary btn-sm"
              data-testid="join-room-button"
              disabled={!roomInput.trim()}
            >
              Join
            </button>
          </form>
        </div>
      </header>

      {connectionError && (
        <div className="alert-banner alert-warning" data-testid="connection-error-banner">
          Gateway connection issue: {connectionError}. Ensure Java gateway is running on 127.0.0.1:8080.
        </div>
      )}

      {errorMessage && (
        <div className="alert-banner alert-error" data-testid="error-banner">
          {errorMessage}
        </div>
      )}

      {collab.isLocked && (
        <div className="alert-banner alert-warning" data-testid="collab-history-full-banner">
          Room collaboration history limit reached. Editing has been locked to prevent document divergence.
        </div>
      )}

      <div className="controls-bar">
        <div className="controls-left">
          <LanguageSelector
            language={collab.language}
            onChange={handleLanguageChange}
            disabled={isRunning || collab.isLocked}
          />
        </div>

        <div className="controls-center">
          <div className="status-container">
            <span className="status-label">Execution Status:</span>
            <StatusBadge status={uiStatus} />
            <span data-testid="status-timeline" className="status-timeline">
              {stageHistory.join(' -> ')}
            </span>
          </div>
        </div>

        <div className="controls-right">
          <button
            type="button"
            className="btn btn-secondary"
            onClick={handleReset}
            disabled={isRunning || collab.isLocked}
            data-testid="reset-button"
          >
            Reset
          </button>
          <button
            type="button"
            className={`btn btn-primary ${isRunning ? 'loading' : ''}`}
            onClick={handleRun}
            disabled={isRunning || !connected || !joined}
            data-testid="run-button"
          >
            {isRunning ? (uiStatus === 'SUBMITTING' ? 'Submitting...' : 'Running...') : 'Run Code'}
          </button>
        </div>
      </div>

      <main className="workbench-layout">
        <section className="editor-section" aria-label="Editor Section">
          <div className="section-header">
            <span className="section-title">Source Editor</span>
            <span className="file-badge">{collab.language === 'c' ? 'main.c' : 'main.py'}</span>
          </div>
          <CodeEditor
            language={collab.language}
            value={collab.sourceCode}
            onChange={collab.setSharedCode}
            onMount={handleEditorMount}
            readOnly={isRunning || collab.isLocked}
          />
        </section>

        <section className="terminal-section" aria-label="Terminal Section">
          <OutputPanel
            stdout={stdout}
            stderr={stderr}
            stdin={stdin}
            onStdinChange={setStdin}
            disabled={isRunning}
          />

          <MetricsPanel metrics={metrics} />
        </section>
      </main>

      <footer className="app-footer">
        <span>Execution runs inside Linux namespaces, seccomp-bpf, and rlimits. Loopback binding only.</span>
      </footer>
    </div>
  );
};
