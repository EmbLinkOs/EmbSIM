// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { SerialTerminal } from './SerialTerminal';

describe('SerialTerminal: the port on the host (ADR-031)', () => {
  const base = { target: 'B1', targets: ['B1'], lines: [] };

  it('offers to open the port, and says why it cannot', () => {
    const onHostPort = vi.fn();
    render(<SerialTerminal {...base} onHostPort={onHostPort} hostPortError="the demo session runs in the browser" />);
    const host = screen.getByLabelText('Port on the host');
    fireEvent.click(screen.getByRole('button', { name: 'Open on this computer' }));
    expect(onHostPort).toHaveBeenCalledWith(true);
    expect(host.textContent).toContain('runs in the browser');
  });

  it('shows the path, whether a program has it, and a baud mismatch', () => {
    const onHostPort = vi.fn();
    const { rerender } = render(
      <SerialTerminal {...base} onHostPort={onHostPort} hostPort={{ path: '/dev/pts/3', connected: false, baud: 0, sketchBaud: 9600 }} />,
    );
    const host = screen.getByLabelText('Port on the host');
    expect(host.textContent).toContain('/dev/pts/3');
    expect(host.textContent).toContain('no program has it open');
    rerender(<SerialTerminal {...base} onHostPort={onHostPort} hostPort={{ path: '/dev/pts/3', connected: true, baud: 9600, sketchBaud: 9615 }} />);
    expect(host.textContent).toContain('open at 9600 baud');
    expect(screen.queryByRole('status')).toBeNull();
    rerender(<SerialTerminal {...base} onHostPort={onHostPort} hostPort={{ path: '/dev/pts/3', connected: true, baud: 115200, sketchBaud: 9615 }} />);
    expect(screen.getByRole('status').textContent).toContain('the sketch runs at 9615');
    fireEvent.click(screen.getByRole('button', { name: 'Remove' }));
    expect(onHostPort).toHaveBeenCalledWith(false);
  });

  it('has no host strip without a handler or a port', () => {
    render(<SerialTerminal {...base} target={null} targets={[]} onHostPort={() => undefined} />);
    expect(screen.queryByLabelText('Port on the host')).toBeNull();
  });
});
