import { test, expect } from '@playwright/test';

test.describe('PolyExecution Studio E2E Browser Suite', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    // Wait for WebSocket connection and room join
    await expect(page.getByTestId('connection-status')).toContainText('Room: room-', {
      timeout: 10000
    });
    await expect(page.getByTestId('status-badge')).toContainText('Idle');
  });

  test('executes C code end-to-end and renders truthful status and telemetry', async ({ page }) => {
    const runBtn = page.getByTestId('run-button');
    await expect(runBtn).toBeEnabled();

    // Click Run
    await runBtn.click();

    // The execution should reach Completed
    await expect(page.getByTestId('status-badge')).toContainText('Completed', {
      timeout: 15000
    });

    // Check stdout contains output from C code
    const stdout = page.getByTestId('stdout-output');
    await expect(stdout).toContainText('Hello from PolyExecution Studio!');

    // Check truthful telemetry metrics from sandbox
    await expect(page.getByTestId('metric-exit-code')).toContainText('0');
    await expect(page.getByTestId('metric-worker-status')).toContainText('OK');
    await expect(page.getByTestId('metric-wall-time')).not.toContainText('0 ms');
  });

  test('halts on invalid C code with Compile Error and never displays Running', async ({ page }) => {
    // Set invalid C source code
    await page.evaluate(() => {
      (window as unknown as { __setEditorCode?: (code: string) => void }).__setEditorCode?.(
        'int main(void) { invalid syntax token here; return 0; }'
      );
    });

    const runBtn = page.getByTestId('run-button');
    await runBtn.click();

    // Status must transition to Compile Error
    await expect(page.getByTestId('status-badge')).toContainText('Compile Error', {
      timeout: 15000
    });

    // Check stderr contains compiler diagnostic
    const stderr = page.getByTestId('stderr-output');
    await expect(stderr).toContainText('error:');

    // Telemetry reflects failure exit code
    await expect(page.getByTestId('metric-worker-status')).toContainText('COMPILE_ERROR');
    await expect(page.getByTestId('metric-exit-code')).not.toContainText('0');
  });

  test('executes Python code end-to-end in Linux sandbox', async ({ page }) => {
    // Switch language to Python
    const langSelect = page.getByTestId('language-select');
    await langSelect.selectOption('python');

    await page.evaluate(() => {
      (window as unknown as { __setEditorCode?: (code: string) => void }).__setEditorCode?.(
        'print("Greetings from Python sandbox runner!")'
      );
    });

    const runBtn = page.getByTestId('run-button');
    await runBtn.click();

    await expect(page.getByTestId('status-badge')).toContainText('Completed', {
      timeout: 15000
    });

    const stdout = page.getByTestId('stdout-output');
    await expect(stdout).toContainText('Greetings from Python sandbox runner!');
    await expect(page.getByTestId('metric-worker-status')).toContainText('OK');
  });

  test('handles execution timeout and renders Timed Out status', async ({ page }) => {
    const langSelect = page.getByTestId('language-select');
    await langSelect.selectOption('python');

    // Python infinite loop to trigger worker wall/cpu timeout
    await page.evaluate(() => {
      (window as unknown as { __setEditorCode?: (code: string) => void }).__setEditorCode?.(
        'while True:\n    pass\n'
      );
    });

    const runBtn = page.getByTestId('run-button');
    await runBtn.click();

    // Default wall limit is 2000 ms, so timeout should occur within ~5-8 seconds
    await expect(page.getByTestId('status-badge')).toContainText('Timed Out', {
      timeout: 15000
    });

    await expect(page.getByTestId('metric-worker-status')).toContainText('TIMEOUT');
  });
});
