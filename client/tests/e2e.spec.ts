import { test, expect, Page } from '@playwright/test';

async function setMonacoContent(page: Page, text: string) {
  // Focus Monaco's standard accessible editor textbox and edit directly through Playwright
  const editorTextbox = page.getByRole('textbox', { name: 'Editor content' });
  await expect(editorTextbox).toBeAttached({ timeout: 10000 });
  await editorTextbox.focus();
  await page.keyboard.press('ControlOrMeta+A');
  await page.keyboard.press('Backspace');
  await page.keyboard.insertText(text);
  await page.waitForTimeout(150);
}

test.describe('PolyExecution Studio E2E Browser Suite', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    // Wait for WebSocket connection and room join
    await expect(page.getByTestId('connection-status')).toContainText('Room: room-', {
      timeout: 10000
    });
    await expect(page.getByTestId('status-badge')).toContainText('Idle');
    // Ensure Monaco editor has initialized in the DOM
    await expect(page.locator('.monaco-editor')).toBeVisible({ timeout: 10000 });
  });

  test('executes C code end-to-end and asserts Submitting -> Accepted -> Compiling -> Running -> Completed sequence', async ({
    page
  }) => {
    const runBtn = page.getByTestId('run-button');
    await expect(runBtn).toBeEnabled();

    // Click Run
    await runBtn.click();

    // Assert the exact real progress sequence emitted by worker milestones
    const timeline = page.getByTestId('status-timeline');
    await expect(timeline).toHaveText(
      'SUBMITTING -> ACCEPTED -> COMPILING -> RUNNING -> COMPLETED',
      { timeout: 15000 }
    );

    // Final status badge
    await expect(page.getByTestId('status-badge')).toContainText('Completed');

    // Check stdout contains output from C code
    const stdout = page.getByTestId('stdout-output');
    await expect(stdout).toContainText('Hello from PolyExecution Studio!');

    // Check truthful telemetry metrics from sandbox
    await expect(page.getByTestId('metric-exit-code')).toContainText('0');
    await expect(page.getByTestId('metric-worker-status')).toContainText('OK');
    await expect(page.getByTestId('metric-wall-time')).not.toHaveText(/^Wall Time\s*0 ms$/);
  });

  test('halts on invalid C code with Compile Error and asserts Running is never reached', async ({
    page
  }) => {
    // Edit Monaco editor directly through Playwright user input
    await setMonacoContent(
      page,
      'int main(void) { invalid syntax token here; return 0; }'
    );

    const runBtn = page.getByTestId('run-button');
    await runBtn.click();

    // Real progress sequence must halt at COMPILE_ERROR without ever reaching RUNNING
    const timeline = page.getByTestId('status-timeline');
    await expect(timeline).toHaveText(
      'SUBMITTING -> ACCEPTED -> COMPILING -> COMPILE_ERROR',
      { timeout: 15000 }
    );
    await expect(timeline).not.toContainText('RUNNING');

    // Status badge reflects Compile Error
    await expect(page.getByTestId('status-badge')).toContainText('Compile Error');

    // Stderr contains compiler diagnostic from GCC
    const stderr = page.getByTestId('stderr-output');
    await expect(stderr).toContainText('error:');

    // Telemetry reflects failure exit code and COMPILE_ERROR status
    await expect(page.getByTestId('metric-worker-status')).toContainText('COMPILE_ERROR');
    await expect(page.getByTestId('metric-exit-code')).not.toContainText('0');
  });

  test('executes Python code asserting Submitting -> Accepted -> Running -> Completed without Compiling', async ({
    page
  }) => {
    // Switch language to Python
    const langSelect = page.getByTestId('language-select');
    await langSelect.selectOption('python');

    // Edit Monaco editor directly with Python code
    await setMonacoContent(
      page,
      'print("Greetings from Python sandbox runner!")\n'
    );

    const runBtn = page.getByTestId('run-button');
    await runBtn.click();

    // Python pipeline has no compilation stage: Submitting -> Accepted -> Running -> Completed
    const timeline = page.getByTestId('status-timeline');
    await expect(timeline).toHaveText(
      'SUBMITTING -> ACCEPTED -> RUNNING -> COMPLETED',
      { timeout: 15000 }
    );
    await expect(timeline).not.toContainText('COMPILING');

    await expect(page.getByTestId('status-badge')).toContainText('Completed');

    const stdout = page.getByTestId('stdout-output');
    await expect(stdout).toContainText('Greetings from Python sandbox runner!');
    await expect(page.getByTestId('metric-worker-status')).toContainText('OK');
  });

  test('handles execution timeout and renders Submitting -> Accepted -> Running -> Timed Out', async ({
    page
  }) => {
    const langSelect = page.getByTestId('language-select');
    await langSelect.selectOption('python');

    // Python infinite loop to trigger worker wall/cpu timeout
    await setMonacoContent(page, 'while True:\n    pass\n');

    const runBtn = page.getByTestId('run-button');
    await runBtn.click();

    // Default wall limit is 2000 ms, timeout milestone reaches TIMED_OUT
    const timeline = page.getByTestId('status-timeline');
    await expect(timeline).toHaveText(
      'SUBMITTING -> ACCEPTED -> RUNNING -> TIMED_OUT',
      { timeout: 15000 }
    );

    await expect(page.getByTestId('status-badge')).toContainText('Timed Out');
    await expect(page.getByTestId('metric-worker-status')).toContainText('TIMEOUT');
  });
});
