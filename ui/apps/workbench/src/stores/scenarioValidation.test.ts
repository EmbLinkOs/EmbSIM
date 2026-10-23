// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { emptyScenario } from './verify';
import { expectationProblem, scenarioProblems } from './scenarioValidation';

const valid = () => ({ ...emptyScenario(), assertions: [{ id: 'check', at: '10ms', probe: 'led', expect: { logic: '1' } }] });
describe('scenario validation', () => {
  it('accepts valid zero-time, fractional and full-run checks', () => {
    const s = valid();
    s.assertions[0]!.at = '0ps';
    expect(scenarioProblems(s)).toEqual([]);
    expect(scenarioProblems({ ...s, assertions: [{ id: 'safe', always: true, diagnostics: { forbid: ['net.contention'] } }] })).toEqual([]);
  });
  it('rejects missing, negative, malformed and out-of-run assertion times', () => {
    for (const at of ['', '-1ms', 'tomorrow', '51ms']) {
      const s = valid();
      s.assertions[0]!.at = at;
      expect(scenarioProblems(s).length).toBeGreaterThan(0);
    }
    expect(scenarioProblems({ ...emptyScenario(), until: undefined }).length).toBeGreaterThan(0);
  });
  it('rejects duplicate IDs, reversed windows and ambiguous timing', () => {
    const s = valid();
    expect(scenarioProblems({ ...s, assertions: [...s.assertions, ...s.assertions] }).join(' ')).toContain('duplicate');
    expect(scenarioProblems({ ...s, assertions: [{ id: 'window', between: ['20ms', '10ms'], probe: 'led', expect: { min_v: 0 } }] }).join(' ')).toContain('start must not follow');
    expect(scenarioProblems({ ...s, assertions: [{ ...s.assertions[0]!, by: '20ms' }] }).join(' ')).toContain('exactly one');
  });
  it('rejects unknown or wrongly typed checks and impossible bounds', () => {
    for (const value of [{}, [], null, { typo: 1 }, { logic: 1 }, { edges: -1 }, { frequency_hz: 0 }, { min_v: 4, max_v: 2 }, { equals: Infinity }, { not_contains: 3 }]) {
      expect(expectationProblem(value)).toBeTruthy();
    }
    expect(expectationProblem({ min_v: 0, max_v: 3.3, tolerance: 0 })).toBeUndefined();
    expect(expectationProblem({ not_contains: 'Restarting now.' })).toBeUndefined();
  });
  it('rejects missing input bindings and values the scenario schema cannot store', () => {
    expect(scenarioProblems({ ...emptyScenario(), inputs: [{ at: '1ms', target: '', input: '', value: {} }] })).toHaveLength(3);
  });
});
