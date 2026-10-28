// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { useState } from 'react';
import type { CircuitBlock, Project } from '@embsim/protocol';
import { setBus } from './hierarchyTools';

export function SheetBuses({
  sheet,
  writable,
  edit,
  trace,
  fail,
}: {
  sheet: Project | CircuitBlock;
  writable: boolean;
  edit(label: string, fn: (s: Project | CircuitBlock) => void): Promise<void>;
  trace(net: string): void;
  fail(e: unknown): void;
}) {
  const [old, setOld] = useState<string | null>(null),
    [name, setName] = useState(''),
    [members, setMembers] = useState('');
  return (
    <section aria-label="Sheet buses">
      <h3>Buses</h3>
      <p className="es-muted">
        Ordered groups of existing nets; first member is bit 0. A bus never joins its members electrically.
      </p>
      {(sheet.layout?.buses ?? []).map((b) => (
        <div className="wb-hierarchy__port" key={b.name}>
          <strong>{b.name}</strong>
          <div className="wb-hierarchy__bus-members">
            {b.nets.map((n, i) => (
              <button
                key={`${i}:${n}`}
                onClick={() => trace(n)}
                aria-label={`Trace bus ${b.name} bit ${i}, ${n}`}
              >
                {i}: {n}
              </button>
            ))}
          </div>
          <div className="es-row">
            <Button
              size="sm"
              disabled={!writable}
              onClick={() => {
                setOld(b.name);
                setName(b.name);
                setMembers(b.nets.join('\n'));
              }}
            >
              Edit bus {b.name}
            </Button>
            <Button
              size="sm"
              variant="ghost"
              disabled={!writable}
              onClick={() =>
                void edit('Remove sheet bus', (s) => {
                  s.layout = {
                    ...s.layout,
                    buses: s.layout?.buses?.filter((v) => v.name !== b.name),
                  };
                }).catch(fail)
              }
            >
              Remove bus {b.name}
            </Button>
          </div>
        </div>
      ))}
      <form
        onSubmit={(e) => {
          e.preventDefault();
          void edit('Edit sheet bus', (s) =>
            setBus(
              s,
              old,
              name,
              members
                .split(/[\n,]+/)
                .map((s) => s.trim())
                .filter(Boolean),
            ),
          )
            .then(() => {
              setOld(null);
              setName('');
              setMembers('');
            })
            .catch(fail);
        }}
      >
        <label>
          Bus name
          <input
            disabled={!writable}
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder="DATA"
          />
        </label>
        <label>
          Member nets, bit 0 first
          <textarea
            aria-label="Bus member nets"
            disabled={!writable}
            value={members}
            onChange={(e) => setMembers(e.target.value)}
            placeholder="D0, D1, D2, D3"
          />
        </label>
        <Button size="sm" type="submit" disabled={!writable || !name}>
          {old ? 'Save bus' : 'Add bus'}
        </Button>
        {old && (
          <Button
            size="sm"
            variant="ghost"
            onClick={() => {
              setOld(null);
              setName('');
              setMembers('');
            }}
          >
            Cancel bus edit
          </Button>
        )}
      </form>
    </section>
  );
}
