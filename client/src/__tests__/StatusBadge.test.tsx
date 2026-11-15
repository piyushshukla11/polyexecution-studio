import { render, screen } from '@testing-library/react';
import { describe, it, expect } from 'vitest';
import { StatusBadge } from '../components/StatusBadge.tsx';

describe('StatusBadge', () => {
  it('renders Idle status badge', () => {
    render(<StatusBadge status="IDLE" />);
    const badge = screen.getByTestId('status-badge');
    expect(badge).toHaveTextContent('Idle');
    expect(badge).toHaveClass('status-idle');
  });

  it('renders Submitting status badge', () => {
    render(<StatusBadge status="SUBMITTING" />);
    const badge = screen.getByTestId('status-badge');
    expect(badge).toHaveTextContent('Submitting');
    expect(badge).toHaveClass('status-submitting');
  });

  it('renders Compiling status badge', () => {
    render(<StatusBadge status="COMPILING" />);
    const badge = screen.getByTestId('status-badge');
    expect(badge).toHaveTextContent('Compiling');
    expect(badge).toHaveClass('status-compiling');
  });

  it('renders Running status badge', () => {
    render(<StatusBadge status="RUNNING" />);
    const badge = screen.getByTestId('status-badge');
    expect(badge).toHaveTextContent('Running');
    expect(badge).toHaveClass('status-running');
  });

  it('renders Timed Out status badge', () => {
    render(<StatusBadge status="TIMED_OUT" />);
    const badge = screen.getByTestId('status-badge');
    expect(badge).toHaveTextContent('Timed Out');
    expect(badge).toHaveClass('status-timeout');
  });

  it('renders Completed status badge', () => {
    render(<StatusBadge status="COMPLETED" />);
    const badge = screen.getByTestId('status-badge');
    expect(badge).toHaveTextContent('Completed');
    expect(badge).toHaveClass('status-completed');
  });

  it('renders Compile Error status badge', () => {
    render(<StatusBadge status="COMPILE_ERROR" />);
    const badge = screen.getByTestId('status-badge');
    expect(badge).toHaveTextContent('Compile Error');
    expect(badge).toHaveClass('status-error');
  });
});
