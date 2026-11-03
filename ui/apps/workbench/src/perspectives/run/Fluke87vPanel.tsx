// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A Fluke 87V multimeter on the bench (ADR-033): its display (6000 counts,
// four updates a second, OL), its rotary switch, its jacks and which leads
// they take, and its fuses. The switch is the part's `function` input, as a
// person turns it; the leads are the part's pins, wired in the schematic or
// on the breadboard. A blown fuse stays blown until it is replaced.

import { Button, useToast } from '@embsim/design';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { useSession } from '../../stores/session';
import { t as tr } from '../../i18n';

export const FLUKE87V = 'emblink/fluke_87v';

/** the rotary switch, clockwise from OFF, as printed on the meter */
export const POSITIONS: { value: string; label: string; jack: string }[] = [
  { value: 'off', label: 'OFF', jack: '' },
  { value: 'vac', label: 'V~', jack: 'V' },
  { value: 'vdc', label: 'V⎓', jack: 'V' },
  { value: 'mvdc', label: 'mV⎓', jack: 'V' },
  { value: 'ohm', label: 'Ω', jack: 'V' },
  { value: 'diode', label: '⟶|', jack: 'V' },
  { value: 'ma_dc', label: 'mA⎓', jack: 'mA µA' },
  { value: 'ma_ac', label: 'mA~', jack: 'mA µA' },
  { value: 'a_dc', label: 'A⎓', jack: 'A' },
  { value: 'a_ac', label: 'A~', jack: 'A' },
  { value: 'ua_dc', label: 'µA⎓', jack: 'mA µA' },
  { value: 'ua_ac', label: 'µA~', jack: 'mA µA' },
];

const UNIT_LABEL: Record<string, string> = { ohm: 'Ω', kohm: 'kΩ', Mohm: 'MΩ', uA: 'µA' };

export function Fluke87vPanel({ part }: { part: string }) {
  const outputs = useSession((s) => s.outputs);
  const { toast } = useToast();
  const position = (outputs[`${part}.position`] as string | undefined) ?? 'vdc';
  const display = (outputs[`${part}.display`] as string | undefined) ?? '';
  const unit = (outputs[`${part}.unit`] as string | undefined) ?? '';
  const fuseMa = (outputs[`${part}.fuse_ma`] as string | undefined) ?? 'ok';
  const fuseA = (outputs[`${part}.fuse_a`] as string | undefined) ?? 'ok';
  const pos = POSITIONS.find((p) => p.value === position) ?? POSITIONS[2]!;
  const ac = position.endsWith('ac');

  const send = async (input: string, value: unknown) => {
    try {
      await getClient().input.set({ target: part, input, value });
    } catch (e) {
      toast({ kind: 'error', title: tr('inst.dmmRefused'), description: errorMessage(e) });
    }
  };
  const replace = async () => {
    await send('replace_fuse', true);
    await send('replace_fuse', false);
  };

  return (
    <section className="wb-dmm" aria-label={`${part} multimeter`}>
      <div className="wb-dmm__lcd" role="status" aria-live="polite" aria-label={tr('inst.dmmDisplay')}>
        <span className="wb-dmm__mode">{position === 'off' ? '' : ac ? 'AC' : position === 'ohm' || position === 'diode' ? '' : 'DC'}</span>
        <span className="wb-dmm__reading" data-testid="dmm-reading">{position === 'off' ? '' : display || '----'}</span>
        <span className="wb-dmm__unit">{UNIT_LABEL[unit] ?? unit}</span>
      </div>
      <div className="wb-dmm__switch" role="radiogroup" aria-label={tr('inst.dmmSwitch')}>
        {POSITIONS.map((p) => (
          <button
            key={p.value}
            type="button"
            role="radio"
            aria-checked={p.value === position}
            className={`wb-dmm__pos${p.value === position ? ' is-on' : ''}`}
            onClick={() => void send('function', p.value)}
          >
            {p.label}
          </button>
        ))}
      </div>
      <div className="wb-dmm__jacks">
        <span className={`wb-dmm__jack${pos.jack === 'A' ? ' is-used' : ''}`}>A <small>{tr('inst.dmmFuse', { rating: '11 A' })}: {fuseA === 'ok' ? tr('inst.dmmFuseOk') : tr('inst.dmmFuseBlown')}</small></span>
        <span className={`wb-dmm__jack${pos.jack === 'mA µA' ? ' is-used' : ''}`}>mA µA <small>{tr('inst.dmmFuse', { rating: '440 mA' })}: {fuseMa === 'ok' ? tr('inst.dmmFuseOk') : tr('inst.dmmFuseBlown')}</small></span>
        <span className="wb-dmm__jack is-used">COM</span>
        <span className={`wb-dmm__jack${pos.jack === 'V' ? ' is-used' : ''}`}>V Ω ⟶|</span>
      </div>
      <p className="wb-dmm__hint">{pos.jack ? tr('inst.dmmLeads', { jack: pos.jack }) : tr('inst.dmmOff')}</p>
      {(fuseMa !== 'ok' || fuseA !== 'ok') && (
        <Button size="sm" variant="danger" onClick={() => void replace()}>
          {tr('inst.dmmReplace')}
        </Button>
      )}
    </section>
  );
}
