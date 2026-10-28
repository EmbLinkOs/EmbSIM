// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { differentialVoltage, VoltageComparison } from './VoltageComparison';

describe('voltage comparison', () => {
  it('subtracts solved voltages with polarity and rejects logic-only or invalid observations', () => {
    const nets = { hi: { voltage: 3.3 }, lo: { voltage: 1.1 }, logic: { logic: '1' as const }, bad: { voltage: Infinity } };
    expect(differentialVoltage(nets,'hi','lo')).toBeCloseTo(2.2);
    expect(differentialVoltage(nets,'lo','hi')).toBeCloseTo(-2.2);
    expect(differentialVoltage(nets,'hi','hi')).toBe(0);
    for (const net of ['logic','bad','missing']) expect(differentialVoltage(nets,'hi',net)).toBeNull();
    expect(differentialVoltage(nets)).toBeNull();
  });
  it('offers keyboard net selection and explicit zero-loading semantics', () => {
    const change=vi.fn(), pick=vi.fn();
    render(<VoltageComparison names={['HI','GND']} nets={{HI:{voltage:5},GND:{voltage:0}}} leads={{a:'HI',b:'GND'}} onChange={change} active={null} onPick={pick}/>);
    expect(screen.getByLabelText('Differential voltage A minus B').textContent).toBe('5 V');
    expect(screen.getByText(/zero loading/)).toBeTruthy();
    fireEvent.change(screen.getByLabelText('Voltage probe B net'),{target:{value:'HI'}});expect(change).toHaveBeenCalledWith('b','HI');
    fireEvent.click(screen.getByRole('button',{name:'Pick A'}));expect(pick).toHaveBeenCalledWith('a');
  });
});
