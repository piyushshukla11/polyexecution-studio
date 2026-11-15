import React from 'react';
import Editor from '@monaco-editor/react';
import { SupportedLanguage } from '../types/protocol.ts';

interface CodeEditorProps {
  language: SupportedLanguage;
  value: string;
  onChange: (val: string) => void;
  readOnly?: boolean;
}

export const CodeEditor: React.FC<CodeEditorProps> = ({
  language,
  value,
  onChange,
  readOnly
}) => {
  const monacoLanguage = language === 'c' ? 'c' : 'python';

  React.useEffect(() => {
    (window as unknown as { __setEditorCode?: (val: string) => void }).__setEditorCode = (val: string) => {
      onChange(val);
    };
    return () => {
      delete (window as unknown as { __setEditorCode?: (val: string) => void }).__setEditorCode;
    };
  }, [onChange]);

  return (
    <div className="editor-wrapper" data-testid="code-editor-container">
      {/* Fallback hidden textarea accessible for headless tests */}
      <textarea
        data-testid="code-textarea"
        value={value}
        onChange={(e) => onChange(e.target.value)}
        readOnly={readOnly}
        style={{ display: 'none' }}
        aria-label="Code source fallback"
      />
      <Editor
        height="380px"
        language={monacoLanguage}
        value={value}
        onChange={(newVal) => onChange(newVal || '')}
        theme="vs-dark"
        options={{
          readOnly: !!readOnly,
          minimap: { enabled: false },
          fontSize: 13,
          lineNumbers: 'on',
          scrollBeyondLastLine: false,
          automaticLayout: true,
          tabSize: 4
        }}
        loading={<div className="editor-loading" data-testid="editor-loading">Loading Monaco Editor...</div>}
      />
    </div>
  );
};
