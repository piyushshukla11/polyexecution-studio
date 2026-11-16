import { render, screen, fireEvent } from '@testing-library/react';
import { describe, it, expect, vi } from 'vitest';
import { LanguageSelector } from '../components/LanguageSelector.tsx';

describe('LanguageSelector', () => {
  it('renders select with current language value', () => {
    const onChange = vi.fn();
    render(<LanguageSelector language="c" onChange={onChange} />);

    const select = screen.getByTestId('language-select') as HTMLSelectElement;
    expect(select.value).toBe('c');
  });

  it('calls onChange when user selects a different language', () => {
    const onChange = vi.fn();
    render(<LanguageSelector language="c" onChange={onChange} />);

    const select = screen.getByTestId('language-select');
    fireEvent.change(select, { target: { value: 'python' } });

    expect(onChange).toHaveBeenCalledWith('python');
  });

  it('disables dropdown when disabled prop is true', () => {
    const onChange = vi.fn();
    render(<LanguageSelector language="c" onChange={onChange} disabled={true} />);

    const select = screen.getByTestId('language-select');
    expect(select).toBeDisabled();
  });
});
