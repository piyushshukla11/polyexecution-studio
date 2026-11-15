import React from 'react';
import { ExecutionUiStatus } from '../types/protocol.ts';

interface StatusBadgeProps {
  status: ExecutionUiStatus;
}

export const StatusBadge: React.FC<StatusBadgeProps> = ({ status }) => {
  const getBadgeConfig = (): { label: string; className: string } => {
    switch (status) {
      case 'IDLE':
        return { label: 'Idle', className: 'status-idle' };
      case 'ACCEPTED':
        return { label: 'Accepted', className: 'status-accepted' };
      case 'COMPILING':
        return { label: 'Compiling', className: 'status-compiling' };
      case 'RUNNING':
        return { label: 'Running', className: 'status-running' };
      case 'COMPLETED':
        return { label: 'Completed', className: 'status-completed' };
      case 'TIMED_OUT':
        return { label: 'Timed Out', className: 'status-timeout' };
      case 'COMPILE_ERROR':
        return { label: 'Compile Error', className: 'status-error' };
      case 'RUNTIME_ERROR':
        return { label: 'Runtime Error', className: 'status-error' };
      case 'ERROR':
        return { label: 'Error', className: 'status-error' };
      default:
        return { label: 'Unknown', className: 'status-idle' };
    }
  };

  const { label, className } = getBadgeConfig();

  return (
    <span
      data-testid="status-badge"
      className={`status-badge ${className}`}
    >
      <span className="status-dot"></span>
      {label}
    </span>
  );
};
