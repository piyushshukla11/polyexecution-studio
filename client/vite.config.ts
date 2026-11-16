/// <reference types="vitest" />
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: [
      {
        find: /^monaco-editor\/esm\/vs\/(.*)$/,
        replacement: path.resolve(__dirname, 'node_modules/monaco-editor/esm/vs/$1')
      }
    ]
  },
  server: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true
  },
  test: {
    globals: true,
    environment: 'jsdom',
    setupFiles: ['./src/setupTests.ts'],
    include: ['src/**/*.{test,spec}.{ts,tsx}'],
    server: {
      deps: {
        inline: ['y-monaco']
      }
    }
  }
});
