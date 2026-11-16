import React from 'react';
import { SupportedLanguage } from '../types/protocol.ts';

interface LanguageSelectorProps {
  language: SupportedLanguage;
  onChange: (lang: SupportedLanguage) => void;
  disabled?: boolean;
}

export const LanguageSelector: React.FC<LanguageSelectorProps> = ({
  language,
  onChange,
  disabled
}) => {
  return (
    <div className="language-selector-container">
      <label htmlFor="language-select" className="control-label">
        Language:
      </label>
      <select
        id="language-select"
        data-testid="language-select"
        value={language}
        onChange={(e) => onChange(e.target.value as SupportedLanguage)}
        disabled={disabled}
        className="language-select"
      >
        <option value="c">C (GCC / Linux Sandbox)</option>
        <option value="python">Python 3 (Linux Sandbox)</option>
      </select>
    </div>
  );
};
