import { test, expect, Page } from '@playwright/test';

async function setMonacoContent(page: Page, text: string) {
  const editorTextbox = page.getByRole('textbox', { name: 'Editor content' });
  await expect(editorTextbox).toBeAttached({ timeout: 10000 });
  await editorTextbox.focus();
  await page.keyboard.press('ControlOrMeta+A');
  await page.keyboard.press('Backspace');
  await page.keyboard.insertText(text);
  await page.waitForTimeout(150);
}

test.describe('PolyExecution Studio - Phase 6 Real-time Collaboration Suite', () => {
  test('Two independent browser pages converge on edits, display cursors, isolate rooms, and clean up on disconnect', async ({
    browser
  }) => {
    const roomId = `collab-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`;
    const isolatedRoomId = `iso-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`;

    // 1. Create two independent browser contexts (simulating two distinct users)
    const context1 = await browser.newContext();
    const context2 = await browser.newContext();
    const page1 = await context1.newPage();
    const page2 = await context2.newPage();

    // Navigate both pages to the same collaborative room
    await page1.goto(`/?room=${roomId}`);
    await page2.goto(`/?room=${roomId}`);

    // Wait for WebSocket connection and room join on both pages
    await expect(page1.getByTestId('connection-status')).toContainText(`Room: ${roomId}`, {
      timeout: 10000
    });
    await expect(page2.getByTestId('connection-status')).toContainText(`Room: ${roomId}`, {
      timeout: 10000
    });

    // Ensure Monaco editor initialized in both pages
    await expect(page1.locator('.monaco-editor')).toBeVisible({ timeout: 10000 });
    await expect(page2.locator('.monaco-editor')).toBeVisible({ timeout: 10000 });

    // Verify collaborator indicator in UI header reflects presence
    await expect(page1.getByTestId('collaborators-list')).toBeVisible({ timeout: 10000 });
    await expect(page2.getByTestId('collaborators-list')).toBeVisible({ timeout: 10000 });

    // 2. Sequential / Initial Edit from Page 1 converges to Page 2
    const cCodeSnippet = '#include <stdio.h>\n\nint main(void) {\n    printf("Collab OK\\n");\n    return 0;\n}\n';
    await setMonacoContent(page1, cCodeSnippet);

    // Page 2 must receive the updates and converge to the identical code
    await expect(page2.locator('.monaco-editor .view-lines')).toContainText('Collab OK', {
      timeout: 10000
    });
    await expect(page2.locator('.monaco-editor .view-lines')).toContainText('int main(void)', {
      timeout: 10000
    });

    // 3. Remote Cursor & Selection Visibility
    // In Page 1, select printable text on line 1 ("#include")
    const editor1 = page1.getByRole('textbox', { name: 'Editor content' });
    await editor1.focus();
    await page1.keyboard.press('ControlOrMeta+Home');
    // Select characters to trigger both head and selection decorations with visible bounding box
    await page1.keyboard.press('Shift+ArrowRight');
    await page1.keyboard.press('Shift+ArrowRight');
    await page1.keyboard.press('Shift+ArrowRight');
    await page1.keyboard.press('Shift+ArrowRight');
    await page1.keyboard.press('Shift+ArrowRight');

    // Page 2 must observe remote cursor head and selection decorations are visible
    await expect(page2.locator('.yRemoteSelectionHead').first()).toBeVisible({ timeout: 10000 });
    await expect(page2.locator('.yRemoteSelection').first()).toBeVisible({ timeout: 10000 });

    // Collapse selection in page 1
    await page1.keyboard.press('ArrowRight');

    // 4. Truly Concurrent Overlapping Edits from Page 1 and Page 2
    // Both independent browser pages submit edits simultaneously through Promise.all
    // before waiting for remote convergence
    const editor2 = page2.getByRole('textbox', { name: 'Editor content' });
    await Promise.all([
      (async () => {
        await editor1.focus();
        await page1.keyboard.press('ControlOrMeta+Home');
        await page1.keyboard.insertText('// Concurrent header from Peer 1\n');
      })(),
      (async () => {
        await editor2.focus();
        await page2.keyboard.press('ControlOrMeta+End');
        await page2.keyboard.insertText('// Concurrent footer from Peer 2\n');
      })()
    ]);

    // Both editors must converge to the same text including both lines
    await expect(page1.locator('.monaco-editor .view-lines')).toContainText(
      '// Concurrent header from Peer 1',
      { timeout: 10000 }
    );
    await expect(page1.locator('.monaco-editor .view-lines')).toContainText(
      '// Concurrent footer from Peer 2',
      { timeout: 10000 }
    );
    await expect(page2.locator('.monaco-editor .view-lines')).toContainText(
      '// Concurrent header from Peer 1',
      { timeout: 10000 }
    );
    await expect(page2.locator('.monaco-editor .view-lines')).toContainText(
      '// Concurrent footer from Peer 2',
      { timeout: 10000 }
    );

    // Verify exact document convergence between page1 and page2 without normalizing whitespace away
    const getEditorText = async (page: Page): Promise<string> => {
      return page.evaluate(() => {
        const monaco = (window as any).monaco;
        const model = monaco?.editor?.getModels()?.[0];
        if (model) {
          return model.getValue();
        }
        // Fallback DOM extraction sorted by line position
        const lines = Array.from(document.querySelectorAll('.monaco-editor .view-lines .view-line'));
        lines.sort((a, b) => {
          const topA = parseFloat((a as HTMLElement).style.top || '0');
          const topB = parseFloat((b as HTMLElement).style.top || '0');
          return topA - topB;
        });
        return lines.map((l) => (l as HTMLElement).innerText.replace(/\u00a0/g, ' ')).join('\n');
      });
    };

    const lines1 = await getEditorText(page1);
    const lines2 = await getEditorText(page2);
    expect(lines1).toBe(lines2);
    expect(lines1).toContain('// Concurrent header from Peer 1');
    expect(lines1).toContain('// Concurrent footer from Peer 2');

    // 5. Room Isolation: Third browser joins a completely separate room
    const context3 = await browser.newContext();
    const page3 = await context3.newPage();
    await page3.goto(`/?room=${isolatedRoomId}`);

    await expect(page3.getByTestId('connection-status')).toContainText(`Room: ${isolatedRoomId}`, {
      timeout: 10000
    });
    await expect(page3.locator('.monaco-editor')).toBeVisible({ timeout: 10000 });

    // Isolated room must NOT receive edits or awareness from roomId
    await expect(page3.locator('.monaco-editor .view-lines')).not.toContainText('Collab OK');
    await expect(page3.locator('.monaco-editor .view-lines')).not.toContainText('Concurrent line from Peer 2');
    await expect(page3.locator('.yRemoteSelectionHead')).toHaveCount(0);

    // 6. Disconnect / Leave cleans up peer awareness without stale cursors
    // Close Page 2
    await page2.close();
    await context2.close();

    // Page 1 receives room.left and purges remote cursor decoration
    await expect(page1.locator('.yRemoteSelectionHead')).toHaveCount(0, { timeout: 10000 });
    await expect(page1.locator('.yRemoteSelection')).toHaveCount(0, { timeout: 10000 });

    // Document in Page 1 remains intact
    await expect(page1.locator('.monaco-editor .view-lines')).toContainText('Collab OK');

    // 7. Collaborative Execution: Run code from Page 1 and verify execution completes
    const runBtn = page1.getByTestId('run-button');
    await expect(runBtn).toBeEnabled();
    await runBtn.click();

    // Verify truthful status timeline progression
    const timeline = page1.getByTestId('status-timeline');
    await expect(timeline).toHaveText(
      'SUBMITTING -> ACCEPTED -> COMPILING -> RUNNING -> COMPLETED',
      { timeout: 15000 }
    );
    await expect(page1.getByTestId('status-badge')).toContainText('Completed');
    await expect(page1.getByTestId('stdout-output')).toContainText('Collab OK');

    // Cleanup remaining contexts
    await page1.close();
    await page3.close();
    await context1.close();
    await context3.close();
  });
});
