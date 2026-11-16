import { renderHook, act } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import * as Y from 'yjs';
import { encodeAwarenessUpdate } from 'y-protocols/awareness';
import { useCollaboration, C_STARTER } from '../hooks/useCollaboration.ts';
import { uint8ArrayToBase64 } from '../utils/codec.ts';
import { OutgoingMessage, CollabSyncEvent, CollabUpdateEvent, CollabAwarenessEvent, RoomLeftEvent } from '../types/protocol.ts';

describe('useCollaboration hook', () => {
  let sentMessages: OutgoingMessage[];
  let sendMessageMock: ReturnType<typeof vi.fn<(msg: OutgoingMessage) => boolean>>;

  beforeEach(() => {
    sentMessages = [];
    sendMessageMock = vi.fn((msg: OutgoingMessage) => {
      sentMessages.push(msg);
      return true;
    });
  });

  it('initializes with default C starter code and sends collab.sync on join', () => {
    const { result } = renderHook(() =>
      useCollaboration({
        roomId: 'room-collab-test',
        joined: true,
        sessionId: 'sess-1',
        assignedUser: { name: 'User-alpha', color: '#30bced' },
        sendMessage: sendMessageMock,
        initialLanguage: 'c'
      })
    );

    expect(result.current.language).toBe('c');
    expect(result.current.sourceCode).toContain('#include <stdio.h>');

    // Check that collab.sync was requested
    expect(sentMessages.some((m) => m.type === 'collab.sync')).toBe(true);
  });

  it('applies collab.sync with empty updates initializing starter code', () => {
    const { result } = renderHook(() =>
      useCollaboration({
        roomId: 'room-collab-test',
        joined: true,
        sessionId: 'sess-1',
        assignedUser: { name: 'User-alpha', color: '#30bced' },
        sendMessage: sendMessageMock,
        initialLanguage: 'c'
      })
    );

    act(() => {
      const syncEvent: CollabSyncEvent = {
        type: 'collab.sync',
        roomId: 'room-collab-test',
        updates: []
      };
      result.current.processServerMessage(syncEvent);
    });

    expect(result.current.synced).toBe(true);
    expect(result.current.ytext.toString()).toBe(C_STARTER);

    // Initial starter code in empty room emits collab.update to backend
    expect(sentMessages.some((m) => m.type === 'collab.update')).toBe(true);
  });

  it('applies remote collab.update and updates editorText', () => {
    const { result } = renderHook(() =>
      useCollaboration({
        roomId: 'room-collab-test',
        joined: true,
        sessionId: 'sess-1',
        assignedUser: { name: 'User-alpha', color: '#30bced' },
        sendMessage: sendMessageMock
      })
    );

    // Create remote Y.Doc and generate update
    const remoteDoc = new Y.Doc();
    const remoteText = remoteDoc.getText('monaco');
    remoteText.insert(0, 'int main() { return 42; }');
    const update = Y.encodeStateAsUpdate(remoteDoc);
    const updateBase64 = uint8ArrayToBase64(update);

    act(() => {
      const updateEvent: CollabUpdateEvent = {
        type: 'collab.update',
        roomId: 'room-collab-test',
        update: updateBase64
      };
      result.current.processServerMessage(updateEvent);
    });

    expect(result.current.ytext.toString()).toBe('int main() { return 42; }');
  });

  it('converges concurrent edits from two independent Y.Doc instances', () => {
    const doc1 = new Y.Doc();
    const text1 = doc1.getText('monaco');
    text1.insert(0, 'ABCDEF');

    // Sync doc2 with doc1
    const doc2 = new Y.Doc();
    const text2 = doc2.getText('monaco');
    Y.applyUpdate(doc2, Y.encodeStateAsUpdate(doc1));
    expect(text2.toString()).toBe('ABCDEF');

    // Concurrent edits:
    // doc1 inserts at start: '123'
    // doc2 inserts at end: 'XYZ'
    text1.insert(0, '123');
    text2.insert(6, 'XYZ');

    // Cross-apply updates
    const update1 = Y.encodeStateAsUpdate(doc1);
    const update2 = Y.encodeStateAsUpdate(doc2);

    Y.applyUpdate(doc1, update2);
    Y.applyUpdate(doc2, update1);

    // CRDT mathematical convergence guarantee
    expect(text1.toString()).toBe(text2.toString());
    expect(text1.toString()).toBe('123ABCDEFXYZ');
  });

  it('cleans up departed peer awareness upon receiving room.left', () => {
    const { result } = renderHook(() =>
      useCollaboration({
        roomId: 'room-collab-test',
        joined: true,
        sessionId: 'sess-local',
        assignedUser: { name: 'Local-User', color: '#30bced' },
        sendMessage: sendMessageMock
      })
    );

    // Simulate remote peer joining with awareness
    const remoteDoc = new Y.Doc();
    const remoteAwareness = new (result.current.awareness.constructor as any)(remoteDoc);
    remoteAwareness.setLocalStateField('user', {
      name: 'Peer-Bob',
      color: '#e06c75',
      sessionId: 'sess-remote-bob'
    });
    const awarenessUpdate = encodeAwarenessUpdate(remoteAwareness, [remoteAwareness.clientID]);

    act(() => {
      const awarenessEvent: CollabAwarenessEvent = {
        type: 'collab.awareness',
        roomId: 'room-collab-test',
        senderSessionId: 'sess-remote-bob',
        awareness: uint8ArrayToBase64(awarenessUpdate)
      };
      result.current.processServerMessage(awarenessEvent);
    });

    expect(result.current.collaborators.some((c) => c.name === 'Peer-Bob')).toBe(true);

    // Now Peer Bob leaves the room
    act(() => {
      const leftEvent: RoomLeftEvent = {
        type: 'room.left',
        roomId: 'room-collab-test',
        sessionId: 'sess-remote-bob',
        memberCount: 1
      };
      result.current.processServerMessage(leftEvent);
    });

    // Remote peer presence removed; no stale cursor
    expect(result.current.collaborators.some((c) => c.name === 'Peer-Bob')).toBe(false);
  });

  it('binds awareness identity to server-assigned user and prevents identity spoofing', () => {
    const { result } = renderHook(() =>
      useCollaboration({
        roomId: 'room-collab-test',
        joined: true,
        sessionId: 'sess-local',
        assignedUser: { name: 'Local-User', color: '#30bced' },
        sendMessage: sendMessageMock
      })
    );

    // Impostor tries to claim 'Alice' with sessionId 'sess-alice'
    const impostorDoc = new Y.Doc();
    const impostorAwareness = new (result.current.awareness.constructor as any)(impostorDoc);
    impostorAwareness.setLocalStateField('user', {
      name: 'Impostor-Alice',
      color: '#ff0000',
      sessionId: 'sess-alice'
    });
    const update = encodeAwarenessUpdate(impostorAwareness, [impostorAwareness.clientID]);

    act(() => {
      const awarenessEvent: CollabAwarenessEvent = {
        type: 'collab.awareness',
        roomId: 'room-collab-test',
        senderSessionId: 'sess-real-bob', // Server verified sender
        assignedUser: { name: 'User-Real-Bob', color: '#e06c75' },
        awareness: uint8ArrayToBase64(update)
      };
      result.current.processServerMessage(awarenessEvent);
    });

    // Client forces server-assigned user metadata and server-verified sessionId
    const bob = result.current.collaborators.find((c) => c.sessionId === 'sess-real-bob');
    expect(bob).toBeDefined();
    expect(bob?.name).toBe('User-Real-Bob');
    expect(bob?.color).toBe('#e06c75');

    // Impostor Alice does NOT exist in collaborators list
    expect(result.current.collaborators.some((c) => c.sessionId === 'sess-alice')).toBe(false);
  });

  it('locks editing when COLLAB_HISTORY_FULL error is received', () => {
    const { result } = renderHook(() =>
      useCollaboration({
        roomId: 'room-collab-test',
        joined: true,
        sessionId: 'sess-local',
        assignedUser: { name: 'Local-User', color: '#30bced' },
        sendMessage: sendMessageMock
      })
    );

    expect(result.current.isLocked).toBe(false);

    act(() => {
      result.current.processServerMessage({
        type: 'error',
        code: 'COLLAB_HISTORY_FULL',
        message: 'Room collaboration history limit reached. Editing has been locked.'
      });
    });

    expect(result.current.isLocked).toBe(true);
  });

  it('guards setSharedCode and setSharedLanguage when locked on COLLAB_HISTORY_FULL', () => {
    const { result } = renderHook(() =>
      useCollaboration({
        roomId: 'room-collab-test',
        joined: true,
        sessionId: 'sess-local',
        assignedUser: { name: 'Local-User', color: '#30bced' },
        sendMessage: sendMessageMock
      })
    );

    // Initial sync
    act(() => {
      result.current.processServerMessage({
        type: 'collab.sync',
        roomId: 'room-collab-test',
        updates: []
      });
    });

    const initialText = result.current.ytext.toString();
    expect(result.current.language).toBe('c');

    // Trigger COLLAB_HISTORY_FULL
    act(() => {
      result.current.processServerMessage({
        type: 'error',
        code: 'COLLAB_HISTORY_FULL',
        message: 'Room collaboration history limit reached. Editing has been locked.'
      });
    });

    expect(result.current.isLocked).toBe(true);

    // Clear sendMessageMock calls from initial sync
    sendMessageMock.mockClear();

    // Attempt setSharedCode while locked
    act(() => {
      result.current.setSharedCode('int main() { return 42; }');
    });

    // Document must not change, and no outgoing update sent
    expect(result.current.ytext.toString()).toBe(initialText);
    expect(sendMessageMock).not.toHaveBeenCalled();

    // Attempt setSharedLanguage while locked
    act(() => {
      result.current.setSharedLanguage('python');
    });

    // Language and doc must not change, and no outgoing update sent
    expect(result.current.language).toBe('c');
    expect(result.current.ytext.toString()).toBe(initialText);
    expect(sendMessageMock).not.toHaveBeenCalled();
  });
});
