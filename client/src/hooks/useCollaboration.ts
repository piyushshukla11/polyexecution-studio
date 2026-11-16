import { useEffect, useRef, useState, useCallback } from 'react';
import * as Y from 'yjs';
import * as decoding from 'lib0/decoding';
import * as encoding from 'lib0/encoding';
import {
  Awareness,
  encodeAwarenessUpdate,
  applyAwarenessUpdate,
  removeAwarenessStates
} from 'y-protocols/awareness';
import { MonacoBinding } from 'y-monaco';
import type * as monacoEditor from 'monaco-editor';
import { uint8ArrayToBase64, base64ToUint8Array } from '../utils/codec.ts';
import {
  SupportedLanguage,
  AssignedUser,
  OutgoingMessage,
  IncomingServerEvent
} from '../types/protocol.ts';

export const C_STARTER = `#include <stdio.h>

int main(void) {
    printf("Hello from PolyExecution Studio!\\n");
    return 0;
}
`;

export const PYTHON_STARTER = `print("Hello from PolyExecution Studio!")
`;

export interface CollaboratorInfo {
  clientId: number;
  name: string;
  color: string;
  isLocal: boolean;
  sessionId?: string;
}

export interface UseCollaborationOptions {
  roomId: string;
  joined: boolean;
  sessionId: string | null;
  assignedUser: AssignedUser | null;
  sendMessage: (msg: OutgoingMessage) => boolean;
  initialLanguage?: SupportedLanguage;
  onLanguageChange?: (lang: SupportedLanguage) => void;
}

function hexToRgba(hex: string, alpha: number): string {
  let c = hex.replace('#', '');
  if (c.length === 3) {
    c = c.split('').map((x) => x + x).join('');
  }
  const num = parseInt(c, 16);
  const r = (num >> 16) & 255;
  const g = (num >> 8) & 255;
  const b = num & 255;
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function updateDynamicCollabStyles(peers: CollaboratorInfo[]): void {
  if (typeof document === 'undefined') return;

  let styleEl = document.getElementById('pes-collab-dynamic-styles') as HTMLStyleElement | null;
  if (!styleEl) {
    styleEl = document.createElement('style');
    styleEl.id = 'pes-collab-dynamic-styles';
    document.head.appendChild(styleEl);
  }

  let css = '';
  for (const peer of peers) {
    const rgba = hexToRgba(peer.color, 0.35);
    css += `
      .yRemoteSelectionHead-${peer.clientId} {
        position: absolute !important;
        border-left: 2px solid ${peer.color} !important;
        border-top: 2px solid ${peer.color} !important;
        border-bottom: 2px solid ${peer.color} !important;
        height: 100%;
        box-sizing: border-box;
        pointer-events: none;
      }
      .yRemoteSelectionHead-${peer.clientId}::after {
        content: '';
        border: 3px solid ${peer.color} !important;
        border-radius: 4px;
        left: -4px;
        top: -5px;
        position: absolute;
      }
      .yRemoteSelection-${peer.clientId} {
        background-color: ${rgba} !important;
      }
    `;
  }
  styleEl.textContent = css;
}

export function useCollaboration(options: UseCollaborationOptions) {
  const {
    roomId,
    joined,
    sessionId,
    assignedUser,
    sendMessage,
    initialLanguage = 'c',
    onLanguageChange
  } = options;

  const ydocRef = useRef<Y.Doc | null>(null);
  const awarenessRef = useRef<Awareness | null>(null);
  const bindingRef = useRef<MonacoBinding | null>(null);

  if (!ydocRef.current) {
    ydocRef.current = new Y.Doc();
  }
  if (!awarenessRef.current) {
    awarenessRef.current = new Awareness(ydocRef.current);
  }

  const ydoc = ydocRef.current;
  const awareness = awarenessRef.current;
  const ytext = ydoc.getText('monaco');
  const ymeta = ydoc.getMap('meta');

  const defaultStarter = initialLanguage === 'python' ? PYTHON_STARTER : C_STARTER;
  const [synced, setSynced] = useState<boolean>(false);
  const [isLocked, setIsLocked] = useState<boolean>(false);
  const isLockedRef = useRef<boolean>(false);
  const clientIdToSessionRef = useRef<Map<number, string>>(new Map());
  const sessionToClientIdsRef = useRef<Map<string, Set<number>>>(new Map());
  const [collaborators, setCollaborators] = useState<CollaboratorInfo[]>([]);
  const [language, setLanguage] = useState<SupportedLanguage>(initialLanguage);
  const [editorText, setEditorText] = useState<string>(() => ytext.toString() || defaultStarter);

  const onLanguageChangeRef = useRef(onLanguageChange);
  useEffect(() => {
    onLanguageChangeRef.current = onLanguageChange;
  }, [onLanguageChange]);

  // Transmit local document updates over WebSocket
  useEffect(() => {
    const handleDocUpdate = (update: Uint8Array, origin: unknown) => {
      setEditorText(ytext.toString());
      if (origin === 'sync' || origin === 'remote') {
        return;
      }
      const base64 = uint8ArrayToBase64(update);
      sendMessage({ type: 'collab.update', update: base64 });
    };

    ydoc.on('update', handleDocUpdate);
    return () => {
      ydoc.off('update', handleDocUpdate);
    };
  }, [ydoc, ytext, sendMessage]);

  // Transmit local awareness updates over WebSocket and track collaborators
  useEffect(() => {
    const handleAwarenessUpdate = (
      { added, updated, removed }: { added: number[]; updated: number[]; removed: number[] },
      origin: unknown
    ) => {
      if (origin === 'local') {
        const changed = [...added, ...updated, ...removed];
        if (changed.length > 0) {
          const update = encodeAwarenessUpdate(awareness, [awareness.clientID]);
          sendMessage({ type: 'collab.awareness', awareness: uint8ArrayToBase64(update) });
        }
      }
    };

    const handleAwarenessChange = () => {
      const peers: CollaboratorInfo[] = [];
      awareness.getStates().forEach((state, cId) => {
        if (state?.user) {
          peers.push({
            clientId: cId,
            name: state.user.name || `User-${cId}`,
            color: state.user.color || '#30bced',
            isLocal: cId === awareness.clientID,
            sessionId: state.user.sessionId
          });
        }
      });
      setCollaborators(peers);
      updateDynamicCollabStyles(peers);
    };

    awareness.on('update', handleAwarenessUpdate);
    awareness.on('change', handleAwarenessChange);
    return () => {
      awareness.off('update', handleAwarenessUpdate);
      awareness.off('change', handleAwarenessChange);
    };
  }, [awareness, sendMessage]);

  // Observe shared metadata (language)
  useEffect(() => {
    const handleMetaChange = () => {
      const sharedLang = ymeta.get('language') as SupportedLanguage | undefined;
      if (sharedLang && (sharedLang === 'c' || sharedLang === 'python')) {
        setLanguage(sharedLang);
        onLanguageChangeRef.current?.(sharedLang);
      }
    };

    ymeta.observe(handleMetaChange);
    return () => {
      ymeta.unobserve(handleMetaChange);
    };
  }, [ymeta]);

  // Set local awareness identity when assignedUser is available
  useEffect(() => {
    if (!joined) {
      setSynced(false);
      return;
    }

    if (assignedUser && sessionId) {
      const current = awareness.getLocalState()?.user;
      if (
        !current ||
        current.name !== assignedUser.name ||
        current.color !== assignedUser.color ||
        current.sessionId !== sessionId
      ) {
        awareness.setLocalStateField('user', {
          name: assignedUser.name,
          color: assignedUser.color,
          sessionId: sessionId
        });
      }
    }
  }, [joined, assignedUser?.name, assignedUser?.color, sessionId, awareness]);

  // Request room collaboration document sync once per room join
  const syncedRoomRef = useRef<string | null>(null);
  useEffect(() => {
    if (!joined) {
      syncedRoomRef.current = null;
      setSynced(false);
      isLockedRef.current = false;
      setIsLocked(false);
      clientIdToSessionRef.current.clear();
      sessionToClientIdsRef.current.clear();
      return;
    }

    if (syncedRoomRef.current !== roomId) {
      syncedRoomRef.current = roomId;
      sendMessage({ type: 'collab.sync' });
    }
  }, [joined, roomId, sendMessage]);

  // Dispatch incoming server collaboration events
  const processServerMessage = useCallback(
    (event: IncomingServerEvent) => {
      switch (event.type) {
        case 'collab.sync':
          if (event.roomId === roomId) {
            ydoc.transact(() => {
              for (const updateBase64 of event.updates) {
                try {
                  const bytes = base64ToUint8Array(updateBase64);
                  Y.applyUpdate(ydoc, bytes, 'sync');
                } catch (e) {
                  console.error('Failed to apply sync update', e);
                }
              }
            }, 'sync');

            // If room had no updates yet, initialize with default starter code
            if (event.updates.length === 0 && ytext.length === 0) {
              ydoc.transact(() => {
                const starter = initialLanguage === 'python' ? PYTHON_STARTER : C_STARTER;
                ytext.insert(0, starter);
                ymeta.set('language', initialLanguage);
              }, 'local-starter');
            }

            setEditorText(ytext.toString() || defaultStarter);
            setSynced(true);
          }
          break;

        case 'collab.update':
          if (event.roomId === roomId) {
            try {
              const bytes = base64ToUint8Array(event.update);
              Y.applyUpdate(ydoc, bytes, 'remote');
              setEditorText(ytext.toString());
            } catch (e) {
              console.error('Failed to apply remote collab update', e);
            }
          }
          break;

        case 'collab.awareness':
          if (event.roomId === roomId && event.senderSessionId) {
            try {
              const bytes = base64ToUint8Array(event.awareness);
              const decoder = decoding.createDecoder(bytes);
              const len = decoding.readVarUint(decoder);
              const validEntries: Array<{ clientID: number; clock: number; state: any }> = [];

              for (let i = 0; i < len; i++) {
                const clientID = decoding.readVarUint(decoder);
                const clock = decoding.readVarUint(decoder);
                const stateStr = decoding.readVarString(decoder);
                const state = JSON.parse(stateStr);

                // 1. Reject if remote session attempts to claim local user's clientID
                if (clientID === awareness.clientID) {
                  console.warn(
                    `[useCollaboration] Rejected awareness spoof: remote session ${event.senderSessionId} claimed local clientID ${clientID}`
                  );
                  continue;
                }

                // 2. Reject if clientID is already bound to a different session (preventing eviction/overwrite)
                const boundSessionId = clientIdToSessionRef.current.get(clientID);
                if (boundSessionId !== undefined && boundSessionId !== event.senderSessionId) {
                  console.warn(
                    `[useCollaboration] Rejected awareness collision: session ${event.senderSessionId} attempted to overwrite clientID ${clientID} owned by session ${boundSessionId}`
                  );
                  continue;
                }

                // 3. Bind clientID to session, or unbind if removed
                if (state !== null) {
                  clientIdToSessionRef.current.set(clientID, event.senderSessionId);
                  let sessionClientIds = sessionToClientIdsRef.current.get(event.senderSessionId);
                  if (!sessionClientIds) {
                    sessionClientIds = new Set();
                    sessionToClientIdsRef.current.set(event.senderSessionId, sessionClientIds);
                  }
                  sessionClientIds.add(clientID);

                  // Sanitize state metadata with server-verified identity
                  if (state.user) {
                    state.user.sessionId = event.senderSessionId;
                    if (event.assignedUser) {
                      state.user.name = event.assignedUser.name;
                      state.user.color = event.assignedUser.color;
                    }
                  }
                } else {
                  // state is null (client removal): only unbind if this session actually owned it
                  if (boundSessionId === event.senderSessionId) {
                    clientIdToSessionRef.current.delete(clientID);
                    const sessionClientIds = sessionToClientIdsRef.current.get(event.senderSessionId);
                    if (sessionClientIds) {
                      sessionClientIds.delete(clientID);
                    }
                  }
                }

                validEntries.push({ clientID, clock, state });
              }

              if (validEntries.length > 0) {
                const encoder = encoding.createEncoder();
                encoding.writeVarUint(encoder, validEntries.length);
                for (const entry of validEntries) {
                  encoding.writeVarUint(encoder, entry.clientID);
                  encoding.writeVarUint(encoder, entry.clock);
                  encoding.writeVarString(encoder, JSON.stringify(entry.state));
                }
                const sanitizedBytes = encoding.toUint8Array(encoder);
                applyAwarenessUpdate(awareness, sanitizedBytes, 'remote');
              }
            } catch (e) {
              console.error('Failed to apply remote awareness update', e);
            }
          }
          break;

        case 'room.left':
          if (event.roomId === roomId) {
            // Immediately purge departed peer's awareness / cursor state using tracked clientIDs
            const trackedClientIds = sessionToClientIdsRef.current.get(event.sessionId);
            const clientsToRemove: number[] = trackedClientIds
              ? Array.from(trackedClientIds)
              : [];

            awareness.getStates().forEach((state, cId) => {
              if (state?.user?.sessionId === event.sessionId && !clientsToRemove.includes(cId)) {
                clientsToRemove.push(cId);
              }
            });

            if (clientsToRemove.length > 0) {
              removeAwarenessStates(awareness, clientsToRemove, 'remote-left');
              for (const cId of clientsToRemove) {
                clientIdToSessionRef.current.delete(cId);
              }
            }
            sessionToClientIdsRef.current.delete(event.sessionId);
          }
          break;

        case 'room.joined':
          if (event.roomId === roomId && event.sessionId !== sessionId) {
            // New participant joined; broadcast our awareness immediately so they see our cursor
            const update = encodeAwarenessUpdate(awareness, [awareness.clientID]);
            sendMessage({ type: 'collab.awareness', awareness: uint8ArrayToBase64(update) });
          }
          break;

        case 'error':
          if (event.code === 'COLLAB_HISTORY_FULL') {
            isLockedRef.current = true;
            setIsLocked(true);
          }
          break;

        default:
          break;
      }
    },
    [roomId, sessionId, ydoc, ytext, ymeta, awareness, initialLanguage, sendMessage]
  );

  // Bind Monaco Editor to Y.Text and Awareness
  const bindEditor = useCallback(
    (editor: monacoEditor.editor.IStandaloneCodeEditor) => {
      const binding = new MonacoBinding(
        ytext,
        editor.getModel()!,
        new Set([editor]),
        awareness
      );
      bindingRef.current = binding;

      return () => {
        binding.destroy();
        bindingRef.current = null;
      };
    },
    [ytext, awareness]
  );

  // Set shared language and adjust code if still at starter code
  const setSharedLanguage = useCallback(
    (newLang: SupportedLanguage) => {
      if (isLockedRef.current) {
        return;
      }
      const starterForNewLang = newLang === 'c' ? C_STARTER : PYTHON_STARTER;
      const starterForOldLang = newLang === 'c' ? PYTHON_STARTER : C_STARTER;

      if (!synced) {
        setLanguage(newLang);
        setEditorText((prev) =>
          prev === starterForOldLang || prev.trim() === '' ? starterForNewLang : prev
        );
        return;
      }

      ydoc.transact(() => {
        ymeta.set('language', newLang);
        const currentText = ytext.toString();
        if (
          currentText === starterForOldLang ||
          currentText.trim() === ''
        ) {
          ytext.delete(0, ytext.length);
          ytext.insert(0, starterForNewLang);
        }
      }, 'local-lang');
      setLanguage(newLang);
      setEditorText(ytext.toString() || starterForNewLang);
    },
    [synced, ydoc, ytext, ymeta]
  );

  // Set shared code directly (e.g. reset or typing in mock mode)
  const setSharedCode = useCallback(
    (code: string) => {
      if (isLockedRef.current) {
        return;
      }
      if (!synced) {
        setEditorText(code);
        return;
      }
      ydoc.transact(() => {
        ytext.delete(0, ytext.length);
        ytext.insert(0, code);
      }, 'local-code-reset');
      setEditorText(code);
    },
    [synced, ydoc, ytext]
  );

  return {
    ydoc,
    ytext,
    awareness,
    synced,
    isLocked,
    collaborators,
    language,
    sourceCode: editorText,
    setSharedLanguage,
    setSharedCode,
    processServerMessage,
    bindEditor
  };
}
