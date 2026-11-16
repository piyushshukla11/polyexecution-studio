import { render, screen, fireEvent, act } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { App } from '../App.tsx';

// Mock Monaco Editor for JSDOM unit test
vi.mock('@monaco-editor/react', () => ({
  default: ({
    value,
    defaultValue,
    onChange,
    language
  }: {
    value?: string;
    defaultValue?: string;
    onChange?: (val: string) => void;
    language: string;
  }) => (
    <div data-testid="mock-monaco-editor" data-language={language}>
      <textarea
        data-testid="mock-editor-textarea"
        value={value !== undefined ? value : defaultValue ?? ''}
        onChange={(e) => onChange?.(e.target.value)}
      />
    </div>
  )
}));

class MockWebSocket {
  static instances: MockWebSocket[] = [];
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;

  url: string;
  readyState: number = MockWebSocket.OPEN;
  onopen: (() => void) | null = null;
  onmessage: ((e: { data: string }) => void) | null = null;
  onerror: (() => void) | null = null;
  onclose: (() => void) | null = null;
  sentMessages: string[] = [];

  constructor(url: string) {
    this.url = url;
    MockWebSocket.instances.push(this);
    setTimeout(() => this.onopen?.(), 0);
  }

  send(data: string) {
    this.sentMessages.push(data);
  }

  close() {
    this.readyState = MockWebSocket.CLOSED;
    this.onclose?.();
  }
}

describe('App Component', () => {
  beforeEach(() => {
    MockWebSocket.instances = [];
    vi.stubGlobal('WebSocket', MockWebSocket);
  });

  it('renders application header and initial Idle status', () => {
    render(<App />);

    expect(screen.getByText('PolyExecution Studio')).toBeInTheDocument();
    expect(screen.getByTestId('status-badge')).toHaveTextContent('Idle');
    expect(screen.getByTestId('run-button')).toBeInTheDocument();
    expect(screen.getByTestId('reset-button')).toBeInTheDocument();
  });

  it('switches between C and Python starters when language changes', () => {
    render(<App />);

    const langSelect = screen.getByTestId('language-select') as HTMLSelectElement;
    expect(langSelect.value).toBe('c');

    const textarea = screen.getByTestId('mock-editor-textarea') as HTMLTextAreaElement;
    expect(textarea.value).toContain('#include <stdio.h>');

    fireEvent.change(langSelect, { target: { value: 'python' } });
    expect(langSelect.value).toBe('python');
    expect(textarea.value).toContain('print("Hello from PolyExecution Studio!")');
  });

  it('switches tabs between output and stdin', () => {
    render(<App />);

    const stdinTab = screen.getByTestId('tab-stdin');
    fireEvent.click(stdinTab);

    const stdinInput = screen.getByTestId('stdin-input');
    expect(stdinInput).toBeInTheDocument();
    fireEvent.change(stdinInput, { target: { value: 'custom input data' } });
    expect(stdinInput).toHaveValue('custom input data');

    const outputTab = screen.getByTestId('tab-output');
    fireEvent.click(outputTab);
    expect(screen.getByTestId('stdout-output')).toBeInTheDocument();
  });

  it('disables LanguageSelector, Reset button, and displays warning banner when COLLAB_HISTORY_FULL error is received', () => {
    render(<App />);

    const ws = MockWebSocket.instances[0];
    expect(ws).toBeDefined();

    act(() => {
      ws.onopen?.();
      ws.onmessage?.({
        data: JSON.stringify({
          type: 'error',
          code: 'COLLAB_HISTORY_FULL',
          message: 'Room collaboration history limit reached. Editing has been locked.'
        })
      });
    });

    expect(screen.getByTestId('collab-history-full-banner')).toBeInTheDocument();
    expect(screen.getByTestId('reset-button')).toBeDisabled();
    expect(screen.getByTestId('language-select')).toBeDisabled();
  });
});
