// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { ComponentSymbol } from './ComponentSymbol';
import { applyState } from './artworkState';
import type { InputEntry, SymbolDescription } from '@embsim/protocol';

const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 20"><style>.lamp{fill:#111}</style><rect id="lamp" class="lamp" x="20" y="1" width="20" height="18" opacity="0"/><text id="value" x="25" y="15">—</text></svg>';
const symbol: SymbolDescription = { svg:'symbol.svg',width:60,height:20,pins:{},states:[{output:'luminance',selector:'#lamp',attr:'opacity'},{output:'color',selector:'#lamp',attr:'fill',format:'{}'},{output:'text',selector:'#value',attr:'text',format:'{}'}],hotspots:[{input:'pressed',selector:'#lamp',gesture:'hold'}] };
function input(): InputEntry { return { target:'SW',input:'pressed',type:'boolean',value:false }; }

describe('live symbol states and physical controls', () => {
  it('renders model observations and clears stale values when observations are replaced', () => {
    const { container,rerender } = render(<svg><ComponentSymbol id="D" svg={svg} symbol={symbol} layout={{x:0,y:0}} outputs={{luminance:.4,color:'#ff453a',text:'HELLO'}} /></svg>);
    expect(container.querySelector('#lamp')?.getAttribute('opacity')).toBe('0.400');
    expect((container.querySelector('#lamp') as SVGElement).style.fill).toBe('#ff453a');
    expect(container.querySelector('#value')?.textContent).toBe('HELLO');
    rerender(<svg><ComponentSymbol id="D" svg={svg} symbol={symbol} layout={{x:0,y:0}} outputs={{}} /></svg>);
    expect(container.querySelector('#lamp')?.getAttribute('opacity')).toBe('0');
    expect(container.querySelector('#value')?.textContent).toBe('—');
  });
  it('shares symbols without leaking instance CSS or changing a neighbor state', () => {
    const { container } = render(<svg><ComponentSymbol id="A" svg={svg} symbol={symbol} layout={{x:0,y:0}} outputs={{color:'#ff0000',luminance:1}}/><ComponentSymbol id="B" svg={svg} symbol={symbol} layout={{x:100,y:0}} outputs={{color:'#00ff00',luminance:.5}}/></svg>);
    expect(container.querySelector('[data-board-item="symbol-A"] style')?.textContent).toContain('[data-board-item="symbol-A"]');
    expect((container.querySelector('[data-board-item="symbol-B"] #lamp') as SVGElement).style.fill).toBe('#00ff00');
  });
  it('rejects executable attributes while allowing safe display paths and text', () => {
    const { container } = render(<svg><rect id="target"/></svg>);const root=container.querySelector('svg')!;
    applyState(root,{output:'x',selector:'#target',attr:'onclick',format:'{}'},'alert(1)');
    applyState(root,{output:'x',selector:'#target',attr:'fill',format:'{}'},'url(https://example.com/track)');
    expect(() => applyState(root,{output:'x',selector:'[',attr:'fill'},true)).not.toThrow();
    expect(() => applyState(root,{output:'x',selector:'#target',class:'invalid class'},true)).not.toThrow();
    expect(root.querySelector('#target')?.getAttribute('onclick')).toBeNull();
    expect(root.querySelector('#target')?.getAttribute('fill')).toBeNull();
  });
  it('releases a held control on blur/unmount and lets transport shortcuts propagate', () => {
    if (!('getBBox' in SVGElement.prototype)) Object.defineProperty(SVGElement.prototype, 'getBBox', {configurable:true,value:() => ({x:0,y:0,width:1,height:1})});
    const box = vi.spyOn(SVGElement.prototype, 'getBBox' as never).mockImplementation(() => ({x:20,y:1,width:20,height:18}) as never);
    const send=vi.fn(), shortcuts=vi.fn();
    const { unmount } = render(<svg onKeyDown={shortcuts}><ComponentSymbol id="SW" svg={svg} symbol={symbol} layout={{x:0,y:0}} interactive inputs={[input()]} onInput={send}/></svg>);
    const button=screen.getByRole('button',{name:'SW pressed'});
    fireEvent.keyDown(button,{key:' '});
    expect(send).toHaveBeenLastCalledWith('SW','pressed',true);
    fireEvent.keyDown(button,{key:'F7'});expect(shortcuts).toHaveBeenCalled();
    fireEvent.blur(button);expect(send).toHaveBeenLastCalledWith('SW','pressed',false);
    fireEvent.keyDown(button,{key:' '});unmount();expect(send).toHaveBeenLastCalledWith('SW','pressed',false);box.mockRestore();
  });
});
