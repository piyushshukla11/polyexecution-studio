export type SupportedLanguage = 'c' | 'python';

export interface ResourceLimitsDto {
  cpuMs?: number;
  wallMs?: number;
  memoryMb?: number;
  maxOutputKb?: number;
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

export type OutgoingMessage = ClientRunMessage | RoomJoinMessage | RoomLeaveMessage;

export interface RoomJoinedEvent {
  type: 'room.joined';
  roomId: string;
  sessionId: string;
  memberCount: number;
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

export interface GenericErrorEvent {
  type: 'error';
  message: string;
}

export type IncomingServerEvent =
  | RoomJoinedEvent
  | RoomLeftEvent
  | RunAcceptedEvent
  | RunProgressEvent
  | RunCompletedEvent
  | RunRejectedEvent
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
