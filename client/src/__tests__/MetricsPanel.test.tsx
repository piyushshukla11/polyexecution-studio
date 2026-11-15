import { render, screen } from '@testing-library/react';
import { describe, it, expect } from 'vitest';
import { MetricsPanel } from '../components/MetricsPanel.tsx';

describe('MetricsPanel', () => {
  it('renders placeholder when no metrics are available', () => {
    render(<MetricsPanel metrics={null} />);
    expect(screen.getByTestId('metrics-empty')).toHaveTextContent(
      'No execution telemetry available yet. Press Run to execute.'
    );
  });

  it('renders truthful worker telemetry when metrics are provided', () => {
    render(
      <MetricsPanel
        metrics={{
          wallMs: 42,
          cpuMs: 18,
          peakMemoryKb: 2048,
          exitCode: 0,
          signal: 0,
          status: 'OK'
        }}
      />
    );

    expect(screen.getByTestId('metric-wall-time')).toHaveTextContent('42 ms');
    expect(screen.getByTestId('metric-cpu-time')).toHaveTextContent('18 ms');
    expect(screen.getByTestId('metric-peak-memory')).toHaveTextContent('2048 KB (2.0 MB)');
    expect(screen.getByTestId('metric-exit-code')).toHaveTextContent('0');
    expect(screen.getByTestId('metric-signal')).toHaveTextContent('0');
    expect(screen.getByTestId('metric-worker-status')).toHaveTextContent('OK');
  });
});
