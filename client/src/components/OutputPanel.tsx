import React, { useState } from 'react';

interface OutputPanelProps {
  stdout: string;
  stderr: string;
  stdin: string;
  onStdinChange: (val: string) => void;
  disabled?: boolean;
}

export const OutputPanel: React.FC<OutputPanelProps> = ({
  stdout,
  stderr,
  stdin,
  onStdinChange,
  disabled
}) => {
  const [activeTab, setActiveTab] = useState<'output' | 'stdin'>('output');

  return (
    <div className="output-panel" data-testid="output-panel">
      <div className="output-tabs">
        <button
          type="button"
          className={`output-tab ${activeTab === 'output' ? 'active' : ''}`}
          onClick={() => setActiveTab('output')}
          data-testid="tab-output"
        >
          Output (stdout / stderr)
        </button>
        <button
          type="button"
          className={`output-tab ${activeTab === 'stdin' ? 'active' : ''}`}
          onClick={() => setActiveTab('stdin')}
          data-testid="tab-stdin"
        >
          Standard Input (stdin)
        </button>
      </div>

      <div className="tab-content">
        {activeTab === 'stdin' ? (
          <div className="stdin-container">
            <label htmlFor="stdin-input" className="section-label">
              Program Stdin (sent to sandbox execution):
            </label>
            <textarea
              id="stdin-input"
              data-testid="stdin-input"
              value={stdin}
              onChange={(e) => onStdinChange(e.target.value)}
              placeholder="Enter optional stdin here..."
              disabled={disabled}
              className="stdin-textarea"
              rows={6}
            />
          </div>
        ) : (
          <div className="streams-container">
            <div className="stream-block">
              <div className="stream-header">
                <span className="stream-title">Standard Output (stdout)</span>
                <span className="stream-bytes">{new Blob([stdout]).size} bytes</span>
              </div>
              <pre
                data-testid="stdout-output"
                className={`stream-content stdout ${!stdout ? 'empty' : ''}`}
              >
                {stdout || '(No stdout)'}
              </pre>
            </div>

            <div className="stream-block">
              <div className="stream-header">
                <span className="stream-title stream-stderr-title">Standard Error (stderr)</span>
                <span className="stream-bytes">{new Blob([stderr]).size} bytes</span>
              </div>
              <pre
                data-testid="stderr-output"
                className={`stream-content stderr ${!stderr ? 'empty' : ''}`}
              >
                {stderr || '(No stderr)'}
              </pre>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
