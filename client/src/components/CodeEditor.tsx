import React from 'react';
import Editor, { OnMount } from '@monaco-editor/react';
import { SupportedLanguage } from '../types/protocol.ts';

interface CodeEditorProps {
  language: SupportedLanguage;
  value?: string;
  defaultValue?: string;
  onChange?: (val: string) => void;
  onMount?: OnMount;
  readOnly?: boolean;
}

export const CodeEditor: React.FC<CodeEditorProps> = ({
  language,
  value,
  defaultValue,
  onChange,
  onMount,
  readOnly
}) => {
  const monacoLanguage =
    language === 'c'
      ? 'c'
      : language === 'python'
      ? 'python'
      : language === 'java'
      ? 'java'
      : language === 'kotlin'
      ? 'kotlin'
      : 'javascript';
  const isBound = !!onMount;

  return (
    <div className="editor-wrapper" data-testid="code-editor-container">
      <Editor
        height="380px"
        language={monacoLanguage}
        value={isBound ? undefined : value}
        defaultValue={defaultValue || value}
        onChange={isBound ? undefined : (newVal) => onChange?.(newVal || '')}
        onMount={onMount}
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
