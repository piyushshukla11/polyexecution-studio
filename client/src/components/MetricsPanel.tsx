import React from 'react';
import { ExecutionMetrics } from '../types/protocol.ts';

interface MetricsPanelProps {
  metrics: ExecutionMetrics | null;
}

export const MetricsPanel: React.FC<MetricsPanelProps> = ({ metrics }) => {
  return (
    <div className="metrics-panel" data-testid="metrics-panel">
      <div className="metrics-header">
        <span className="metrics-title">Execution Telemetry</span>
        <span className="metrics-origin">Sandbox rusage</span>
      </div>

      {metrics ? (
        <div className="metrics-grid">
          <div className="metric-card" data-testid="metric-wall-time">
            <span className="metric-label">Wall Time</span>
            <span className="metric-value">{metrics.wallMs} ms</span>
          </div>

          <div className="metric-card" data-testid="metric-cpu-time">
            <span className="metric-label">CPU Time</span>
            <span className="metric-value">{metrics.cpuMs} ms</span>
          </div>

          <div className="metric-card" data-testid="metric-peak-memory">
            <span className="metric-label">Peak Memory</span>
            <span className="metric-value">
              {metrics.peakMemoryKb} KB ({(metrics.peakMemoryKb / 1024).toFixed(1)} MB)
            </span>
          </div>

          <div className="metric-card" data-testid="metric-exit-code">
            <span className="metric-label">Exit Code</span>
            <span className={`metric-value ${metrics.exitCode === 0 ? 'success' : 'warning'}`}>
              {metrics.exitCode}
            </span>
          </div>

          <div className="metric-card" data-testid="metric-signal">
            <span className="metric-label">Signal</span>
            <span className="metric-value">{metrics.signal}</span>
          </div>

          <div className="metric-card" data-testid="metric-worker-status">
            <span className="metric-label">Worker Status</span>
            <span className="metric-value status-tag">{metrics.status}</span>
          </div>
        </div>
      ) : (
        <div className="metrics-empty" data-testid="metrics-empty">
          No execution telemetry available yet. Press Run to execute.
        </div>
      )}
    </div>
  );
};
