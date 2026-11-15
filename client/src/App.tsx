import React, { useState } from 'react';
import { SupportedLanguage } from './types/protocol.ts';
import { useExecutionSocket } from './hooks/useExecutionSocket.ts';
import { LanguageSelector } from './components/LanguageSelector.tsx';
import { CodeEditor } from './components/CodeEditor.tsx';
import { OutputPanel } from './components/OutputPanel.tsx';
import { MetricsPanel } from './components/MetricsPanel.tsx';
import { StatusBadge } from './components/StatusBadge.tsx';

const C_STARTER = `#include <stdio.h>

int main(void) {
    printf("Hello from PolyExecution Studio!\\n");
    return 0;
}
`;

const PYTHON_STARTER = `print("Hello from PolyExecution Studio!")
`;

export const App: React.FC = () => {
  const [language, setLanguage] = useState<SupportedLanguage>('c');
  const [sourceCode, setSourceCode] = useState<string>(C_STARTER);
  const [stdin, setStdin] = useState<string>('');

  const {
    roomId,
    connected,
    joined,
    connectionError,
    reconnectAttempt,
    uiStatus,
    stageHistory,
    stdout,
    stderr,
    metrics,
    errorMessage,
    runCode,
    resetState
  } = useExecutionSocket();

  const isRunning =
    uiStatus === 'SUBMITTING' ||
    uiStatus === 'ACCEPTED' ||
    uiStatus === 'COMPILING' ||
    uiStatus === 'RUNNING';

  const handleLanguageChange = (newLang: SupportedLanguage) => {
    if (newLang === language) return;

    // If code is either starter code, update to the other language's starter code
    if (sourceCode === C_STARTER || sourceCode === PYTHON_STARTER || sourceCode.trim() === '') {
      setSourceCode(newLang === 'c' ? C_STARTER : PYTHON_STARTER);
    }
    setLanguage(newLang);
  };

  const handleRun = () => {
    runCode(language, sourceCode, stdin);
  };

  const handleReset = () => {
    resetState();
  };

  return (
    <div className="pes-app" data-testid="pes-app">
      <header className="app-header">
        <div className="header-brand">
          <h1 className="header-title">PolyExecution Studio</h1>
          <span className="header-subtitle">Linux-isolated Multi-language Execution Environment</span>
        </div>
        <div className="header-status">
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

      <div className="controls-bar">
        <div className="controls-left">
          <LanguageSelector
            language={language}
            onChange={handleLanguageChange}
            disabled={isRunning}
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
            disabled={isRunning}
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
            <span className="file-badge">{language === 'c' ? 'main.c' : 'main.py'}</span>
          </div>
          <CodeEditor
            language={language}
            value={sourceCode}
            onChange={setSourceCode}
            readOnly={isRunning}
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
