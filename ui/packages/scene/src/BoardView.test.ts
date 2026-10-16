// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { applyState } from './BoardView';

function svg(inner: string): Element {
  const host = document.createElement('div');
  host.innerHTML = `<svg xmlns="http://www.w3.org/2000/svg">${inner}</svg>`;
  return host.firstElementChild!;
}

describe('applyState', () => {
  it('formats scaled artwork text without changing the underlying measurement', () => {
    const root = svg('<text id="reading"/>');
    applyState(root, { output: 'i', selector: '#reading', attr: 'text', format: '{} mA', precision: 2, scale: { from: [0, 0.05], to: [0, 50] } }, 0.009512345);
    expect(root.querySelector('#reading')!.textContent).toBe('9.51 mA');
  });
  it('toggles a class by truth', () => {
    const root = svg('<path id="a"/>');
    applyState(root, { output: 'seg_a', selector: '#a', class: 'on' }, true);
    expect(root.querySelector('#a')!.classList.contains('on')).toBe(true);
    applyState(root, { output: 'seg_a', selector: '#a', class: 'on' }, false);
    expect(root.querySelector('#a')!.classList.contains('on')).toBe(false);
  });

  it('formats a scaled number into a transform', () => {
    const root = svg('<g id="horn"/>');
    applyState(root, { output: 'angle', selector: '#horn', attr: 'transform', format: 'rotate({} 30 30)', scale: { from: [0, 180], to: [-90, 90] } }, 90);
    expect(root.querySelector('#horn')!.getAttribute('transform')).toBe('rotate(0 30 30)');
  });

  it('puts a string into an attribute or the text', () => {
    const root = svg('<rect id="body"/><text id="l1"/>');
    applyState(root, { output: 'color', selector: '#body', attr: 'fill', format: '{}' }, '#ff0000');
    expect(root.querySelector('#body')!.getAttribute('fill')).toBe('#ff0000');
    applyState(root, { output: 'line1', selector: '#l1', attr: 'text', format: '{}' }, 'Hello');
    expect(root.querySelector('#l1')!.textContent).toBe('Hello');
  });

  it('looks values up in a map, falling back to the other rules', () => {
    const root = svg('<rect id="m"/>');
    const st = { output: 'mode', selector: '#m', attr: 'fill', map: { forward: '#2ecc71', reverse: '#e74c3c' } };
    applyState(root, st, 'reverse');
    expect(root.querySelector('#m')!.getAttribute('fill')).toBe('#e74c3c');
  });

  it('keeps the old rules: scaled opacity and true/false strings', () => {
    const root = svg('<ellipse id="g"/>');
    applyState(root, { output: 'luminance', selector: '#g', attr: 'opacity', scale: { from: [0, 1], to: [0, 1] } }, 0.5);
    expect(root.querySelector('#g')!.getAttribute('opacity')).toBe('0.500');
    applyState(root, { output: 'lit', selector: '#g', attr: 'opacity', true: '1', false: '0' }, false);
    expect(root.querySelector('#g')!.getAttribute('opacity')).toBe('0');
  });
});
