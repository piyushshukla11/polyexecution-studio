export type SupportedLanguage = 'c' | 'python';

export interface ResourceLimitsDto {
  cpuMs?: number;
  wallMs?: number;
  memoryMb?: number;
  maxOutputKb?: number;
}

export interface AssignedUser {
  name: string;
  color: string;
  clientId?: number;
}

export interface ClientRunMessage {
  type: 'run';
  roomId: string;
  requestId: string;
  language: SupportedLanguage;
  sourceCode: string;
  stdin?: string;
  limits?: ResourceLimitsDto;
}

export interface RoomJoinMessage {
  type: 'room.join';
  roomId: string;
}

export interface RoomLeaveMessage {
  type: 'room.leave';
  roomId: string;
}

export interface CollabSyncMessage {
  type: 'collab.sync';
}

export interface CollabUpdateMessage {
  type: 'collab.update';
  update: string; // base64
}

export interface CollabAwarenessMessage {
  type: 'collab.awareness';
  awareness: string; // base64
}

export type OutgoingMessage =
  | ClientRunMessage
  | RoomJoinMessage
  | RoomLeaveMessage
  | CollabSyncMessage
  | CollabUpdateMessage
  | CollabAwarenessMessage;

export interface RoomJoinedEvent {
  type: 'room.joined';
  roomId: string;
  sessionId: string;
  members?: string[];
  memberCount?: number;
  assignedUser?: AssignedUser;
}

export interface RoomPeerJoinedEvent {
  type: 'room.peer_joined';
  roomId: string;
  sessionId: string;
  members?: string[];
  assignedUser?: AssignedUser;
}

export interface RoomLeftEvent {
  type: 'room.left';
  roomId: string;
  sessionId: string;
  memberCount: number;
}

export interface RunAcceptedEvent {
  type: 'run.accepted';
  roomId: string;
  requestId: string;
  language: string;
}

export interface RunProgressEvent {
  type: 'run.progress';
  roomId: string;
  requestId: string;
  stage: 'COMPILING' | 'RUNNING';
}

export interface RunCompletedEvent {
  type: 'run.completed';
  roomId: string;
  requestId: string;
  status:
    | 'OK'
    | 'COMPILE_ERROR'
    | 'RUNTIME_ERROR'
    | 'TIMEOUT'
    | 'MEMORY_LIMIT'
    | 'OUTPUT_LIMIT'
    | 'SANDBOX_ERROR';
  stdout: string;
  stderr: string;
  exitCode: number;
  signal: number;
  wallMs: number;
  cpuMs: number;
  peakMemoryKb: number;
}

export interface RunRejectedEvent {
  type: 'run.rejected';
  roomId?: string;
  requestId?: string;
  reason: string;
  message: string;
}

export interface CollabSyncEvent {
  type: 'collab.sync';
  roomId: string;
  updates: string[];
  historyFull?: boolean;
}

export interface CollabUpdateEvent {
  type: 'collab.update';
  roomId: string;
  update: string;
}

export interface CollabAwarenessEvent {
  type: 'collab.awareness';
  roomId: string;
  senderSessionId: string;
  awareness: string;
  assignedUser?: AssignedUser;
}

export interface GenericErrorEvent {
  type: 'error';
  code?: string;
  message: string;
}

export type IncomingServerEvent =
  | RoomJoinedEvent
  | RoomPeerJoinedEvent
  | RoomLeftEvent
  | RunAcceptedEvent
  | RunProgressEvent
  | RunCompletedEvent
  | RunRejectedEvent
  | CollabSyncEvent
  | CollabUpdateEvent
  | CollabAwarenessEvent
  | GenericErrorEvent;

export type ExecutionUiStatus =
  | 'IDLE'
  | 'SUBMITTING'
  | 'ACCEPTED'
  | 'COMPILING'
  | 'RUNNING'
  | 'COMPLETED'
  | 'TIMED_OUT'
  | 'COMPILE_ERROR'
  | 'RUNTIME_ERROR'
  | 'ERROR';

export interface ExecutionMetrics {
  wallMs: number;
  cpuMs: number;
  peakMemoryKb: number;
  exitCode: number;
  signal: number;
  status: string;
}
