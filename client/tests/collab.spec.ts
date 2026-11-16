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
    // In Page 1, position cursor on the editor and select text
    const editor1 = page1.getByRole('textbox', { name: 'Editor content' });
    await editor1.focus();
    await page1.keyboard.press('ControlOrMeta+Home');
    await page1.keyboard.press('ArrowDown');
    // Select characters to trigger both head and selection decorations
    await page1.keyboard.press('Shift+ArrowRight');
    await page1.keyboard.press('Shift+ArrowRight');
    await page1.keyboard.press('Shift+ArrowRight');

    // Page 2 must observe remote cursor head and selection decorations
    await expect(page2.locator('.yRemoteSelectionHead').first()).toBeAttached({ timeout: 10000 });
    await expect(page2.locator('.yRemoteSelection').first()).toBeAttached({ timeout: 10000 });

    // Collapse selection in page 1
    await page1.keyboard.press('ArrowRight');

    // 4. Concurrent Edits from Page 1 and Page 2
    // Page 1 prepends a top comment
    await editor1.focus();
    await page1.keyboard.press('ControlOrMeta+Home');
    await page1.keyboard.insertText('// Top comment from Peer 1\n');
    await page1.waitForTimeout(200);

    // Page 2 appends a bottom comment
    const editor2 = page2.getByRole('textbox', { name: 'Editor content' });
    await editor2.focus();
    await page2.keyboard.press('ControlOrMeta+End');
    await page2.keyboard.insertText('// Bottom comment from Peer 2\n');
    await page2.waitForTimeout(200);

    // Both editors must converge to the same text including both lines
    await expect(page1.locator('.monaco-editor .view-lines')).toContainText(
      'Top comment from Peer 1',
      { timeout: 10000 }
    );
    await expect(page1.locator('.monaco-editor .view-lines')).toContainText(
      'Bottom comment from Peer 2',
      { timeout: 10000 }
    );
    await expect(page2.locator('.monaco-editor .view-lines')).toContainText(
      'Top comment from Peer 1',
      { timeout: 10000 }
    );
    await expect(page2.locator('.monaco-editor .view-lines')).toContainText(
      'Bottom comment from Peer 2',
      { timeout: 10000 }
    );

    // Verify exact document convergence between page1 and page2
    const lines1 = await page1.locator('.monaco-editor .view-lines').innerText();
    const lines2 = await page2.locator('.monaco-editor .view-lines').innerText();
    expect(lines1.replace(/\s+/g, ' ').trim()).toBe(lines2.replace(/\s+/g, ' ').trim());

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
