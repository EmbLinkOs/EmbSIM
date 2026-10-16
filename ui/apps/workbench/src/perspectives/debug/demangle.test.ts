// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { demangle } from './demangle';

describe('demangle', () => {
  it.each([
    ['_ZL13eeprom_statusv', 'eeprom_status()'],
    ['_Z11random_readhi', 'random_read(unsigned char, int)'],
    ['_ZN7TwoWire5writeEh', 'TwoWire::write(unsigned char)'],
    ['_ZN7TwoWire5writeEPKhj', 'TwoWire::write(unsigned char const*, unsigned int)'],
    ['_ZN5Print7printlnEPK19__FlashStringHelper', 'Print::println(__FlashStringHelper const*)'],
    ['_ZN6StringC2EPKc', 'String::String(char const*)'],
    ['_ZN6StringD1Ev', 'String::~String()'],
    ['_ZNK6String6equalsERKS_', 'String::equals(String const&) const'],
    ['_ZN5Print5printEmi', 'Print::print(unsigned long, int)'],
    ['_ZN14HardwareSerial17_tx_udr_empty_irqEv', 'HardwareSerial::_tx_udr_empty_irq()'],
    ['_ZNSt6vectorIiSaIiEE9push_backERKi', 'std::vector<int, std::allocator<int> >::push_back(int const&)'],
    ['_Z3sumPKiS0_', 'sum(int const*, int const*)'],
    ['_ZN12_GLOBAL__N_14tickEv', '(anonymous namespace)::tick()'],
    ['_ZL4loopv.constprop.0', 'loop() [clone .constprop.0]'],
  ])('%s', (mangled, want) => {
    expect(demangle(mangled)).toBe(want);
  });

  it('leaves C names and what it cannot read as they are', () => {
    expect(demangle('main')).toBe('main');
    expect(demangle('__vector_18')).toBe('__vector_18');
    expect(demangle('_ZplRK1AS1_')).toBe('_ZplRK1AS1_'); // an operator: outside the subset
  });
});
