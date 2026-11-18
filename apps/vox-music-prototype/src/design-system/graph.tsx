import { useRef, useState } from 'react';
import { clamp, linear, unit01 } from './parameters';
import { NumericValue } from './controls';
import { createCurve, evaluate, insertNode, moveNodes, shapes } from './curves';
import type { Curve, CurveShape } from './curves';
import { Button, Select, Toggle } from './ui';

export const chartW = 640;
export const chartH = 168;
function Grid({ center = false }: { center?: boolean }) {
  return <>{[0, 1, 2, 3, 4, 5, 6, 7, 8].map(n => <line key={`x${n}`} className="graph-grid" x1={chartW * n / 8} x2={chartW * n / 8} y1="0" y2={chartH} />)}{[0, 1, 2, 3, 4].map(n => <line key={`y${n}`} className={center && n === 2 ? 'graph-origin' : 'graph-grid'} x1="0" x2={chartW} y1={chartH * n / 4} y2={chartH * n / 4} />)}</>;
}
const inset = 10;
const point = (x: number, y: number, zoom = 1, offset = 0) => `${(inset + (x - offset) * zoom * (chartW - inset * 2)).toFixed(2)} ${(inset + (1 - y) * (chartH - inset * 2)).toFixed(2)}`;
function curvePath(curve: Curve, zoom = 1, offset = 0) {
  const times = [...Array.from({ length: 129 }, (_, n) => n / 128), ...curve.flatMap(n => [n.x, Math.max(0, n.x - .000001)])].sort((a, b) => a - b);
  return times.map((t, n) => `${n ? 'L' : 'M'}${point(t, evaluate(curve, t), zoom, offset)}`).join(' ');
}
export function CurveView({ curve, label, className = '' }: { curve: Curve; label: string; className?: string }) {
  return <svg viewBox={`0 0 ${chartW} ${chartH}`} preserveAspectRatio="none" className={`graph ${className}`} role="img" aria-label={label}><Grid /><path className="graph-trace" d={curvePath(curve)} /></svg>;
}
export function Envelope({ attack = .05, decay = .4, label = 'Envelope model' }: { attack?: number; decay?: number; label?: string }) {
  return <CurveView label={label} curve={createCurve([[0, 0], [clamp(attack, .005, .5), 1], [clamp(attack + decay, attack + .005, .999), .001], [1, 0]])} />;
}
export function FilterResponse({ cutoff, resonance = .707 }: { cutoff: number; resonance?: number }) {
  const curve = Array.from({ length: 81 }, (_, id) => {
    const x = id / 80; const w = 20 * 1000 ** x / cutoff;
    const magnitude = -10 * Math.log10((1 - w * w) ** 2 + (w / resonance) ** 2);
    return { id, x, y: unit01((magnitude + 48) / 54), shape: 'linear' as const };
  });
  return <CurveView curve={curve} label={`Low-pass response model, ${cutoff} Hz cutoff`} />;
}
export function Waveform({ peaks, start = 0, end = 1, onTrim, onBegin, onEnd }: { peaks: readonly [number, number][]; start?: number; end?: number; onTrim?: (which: 'start' | 'end', position: number) => void; onBegin?: () => void; onEnd?: () => void }) {
  const rect = useRef<SVGSVGElement>(null);
  const drag = useRef<'start' | 'end' | null>(null);
  const startX = start * chartW; const endX = end * chartW;
  return <svg ref={rect} className="graph waveform" viewBox={`0 0 ${chartW} ${chartH}`} preserveAspectRatio="none" role={onTrim ? 'group' : 'img'} aria-label="Decoded sample waveform">
    <Grid center />{peaks.map(([a, b], i) => <line className="wave-peak" key={i} x1={chartW * i / peaks.length} x2={chartW * i / peaks.length} y1={chartH / 2 * (1 - a)} y2={chartH / 2 * (1 - b)} />)}
    <rect className="trim-mask" x={0} width={startX} height={chartH} pointerEvents="none" /><rect className="trim-mask" x={endX} width={chartW - endX} height={chartH} pointerEvents="none" />
    {(['start', 'end'] as const).map(which => {
      const value = which === 'start' ? start : end; const x = value * chartW;
      return <g key={which} className="trim-handle" role={onTrim ? 'slider' : undefined} aria-label={`Sample ${which} handle`} aria-valuemin={0} aria-valuemax={1} aria-valuenow={value} tabIndex={onTrim ? 0 : undefined}
        onPointerDown={event => { if (onTrim && event.button === 0) { drag.current = which; onBegin?.(); event.currentTarget.setPointerCapture(event.pointerId); event.currentTarget.focus(); } }} onLostPointerCapture={() => { if (drag.current) { drag.current = null; onEnd?.(); } }}
        onPointerMove={event => { if (drag.current === which && rect.current) { const box = rect.current.getBoundingClientRect(); const p = unit01((event.clientX - box.left) / box.width); onTrim?.(which, event.shiftKey ? value + (p - value) * .12 : p); } }}
        onKeyDown={event => { if (onTrim && ['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) { event.preventDefault(); onBegin?.(); onTrim(which, event.key === 'Home' ? 0 : event.key === 'End' ? 1 : value + (event.key === 'ArrowRight' ? 1 : -1) * (event.shiftKey ? .001 : .01)); onEnd?.(); } }}>
        <rect x={clamp(x - 9, 0, chartW - 18)} width={18} height={chartH} fill="transparent" /><line x1={x} x2={x} y1={0} y2={chartH} /><text x={clamp(x + 6, 4, chartW - 22)} y={20}>{which === 'start' ? 'IN' : 'OUT'}</text></g>;
    })}
  </svg>;
}
export function CurveEditor({ curve, label, onChange, onBegin, onEnd, duration }: { curve: Curve; label: string; onChange: (curve: Curve) => void; onBegin?: () => void; onEnd?: () => void; duration?: number }) {
  const [ids, setIds] = useState<number[]>([0]); const [snap, setSnap] = useState(false);
  const [zoom, setZoom] = useState(1); const [offset, setOffset] = useState(0);
  const svg = useRef<SVGSVGElement>(null);
  const drag = useRef<{ ids: number[]; start: [number, number]; before: Curve } | null>(null);
  const primary = curve.find(node => ids.includes(node.id)) ?? curve[0];
  const picked = new Set(ids.filter(id => curve.some(node => node.id === id)));
  if (!picked.size) picked.add(primary.id);
  function read(clientX: number, clientY: number): [number, number] {
    const box = svg.current?.getBoundingClientRect();
    if (!box) return [0, 0];
    return [((clientX - box.left) / box.width * chartW - inset) / (chartW - inset * 2) / zoom + offset, 1 - ((clientY - box.top) / box.height * chartH - inset) / (chartH - inset * 2)];
  }
  function add() {
    const pairs = curve.slice(1).map((node, i) => [curve[i], node]);
    const [a, b] = pairs.sort((p, q) => (q[1].x - q[0].x) - (p[1].x - p[0].x))[0];
    const x = (a.x + b.x) / 2;
    onChange(insertNode(curve, x, evaluate(curve, x)));
  }
  function remove() { const next = curve.filter((node, i) => i === 0 || i === curve.length - 1 || !picked.has(node.id)); if (next.length < curve.length) onChange(next); }
  return <div className="curve-editor">
    <div className="inline wrap curve-toolbar"><Select label="Curve node" hideLabel value={primary.id} onChange={e => setIds([Number(e.target.value)])}>{curve.map((node, i) => <option key={node.id} value={node.id}>Node {i + 1} · {Math.round(node.x * 100)}%</option>)}</Select>
      <Select label="Segment shape" hideLabel value={primary.shape} onChange={e => onChange(curve.map(node => picked.has(node.id) ? { ...node, shape: e.target.value as CurveShape } : node))}>{shapes.map(shape => <option key={shape}>{shape}</option>)}</Select>
      <Toggle checked={snap} onClick={() => setSnap(!snap)}>Snap 1/16</Toggle><Select label="Timeline zoom" hideLabel value={zoom} onChange={e => { setZoom(+e.target.value); setOffset(0); }}><option value={1}>1×</option><option value={2}>2×</option><option value={4}>4×</option></Select>
      <div className="spacer" /><Button onClick={add} disabled={curve.length >= 16}>+ Node</Button><Button onClick={remove} disabled={!curve.some((node, i) => i > 0 && i < curve.length - 1 && picked.has(node.id))}>Remove</Button>
    </div>
    <svg ref={svg} className="graph editable-curve" viewBox={`0 0 ${chartW} ${chartH}`} role="group" aria-label={`${label} curve editor`} preserveAspectRatio="none" onDoubleClick={event => {
      if ((event.target as Element).closest('.curve-node')) return;
      const [x, y] = read(event.clientX, event.clientY); onChange(insertNode(curve, snap ? Math.round(x * 16) / 16 : x, y));
    }}><Grid /><path className="graph-trace" d={curvePath(curve, zoom, offset)} pointerEvents="none" />
      {curve.map((node, i) => {
        if (node.x < offset - .001 || node.x > offset + 1 / zoom + .001) return null;
        const [cx, cy] = point(node.x, node.y, zoom, offset).split(' ');
        return <g role="button" tabIndex={0} aria-label={`Node ${i + 1}, time ${Math.round(node.x * 100)}%, value ${Math.round(node.y * 100)}%`} aria-pressed={picked.has(node.id)} key={node.id} className="curve-node" data-picked={picked.has(node.id)}
          onPointerDown={event => {
            if (event.button !== 0) return;
            const next = event.shiftKey ? (picked.has(node.id) ? [...picked].filter(id => id !== node.id) : [...picked, node.id]) : picked.has(node.id) ? [...picked] : [node.id];
            setIds(next.length ? next : [node.id]);
            drag.current = { ids: next, start: read(event.clientX, event.clientY), before: curve }; onBegin?.();
            event.currentTarget.setPointerCapture(event.pointerId); event.currentTarget.focus();
          }} onPointerMove={event => {
            if (drag.current && event.currentTarget.hasPointerCapture(event.pointerId)) {
              const [x, y] = read(event.clientX, event.clientY); const { ids, start, before } = drag.current;
              onChange(moveNodes(before, ids, x - start[0], y - start[1], snap));
            }
          }} onLostPointerCapture={() => { if (drag.current) { drag.current = null; onEnd?.(); } }} onKeyDown={event => {
            if (event.key === ' ' || event.key === 'Enter') { event.preventDefault(); setIds(event.shiftKey ? [...new Set([...picked, node.id])] : [node.id]); return; }
            const dx = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
            const dy = event.key === 'ArrowUp' ? 1 : event.key === 'ArrowDown' ? -1 : 0;
            if (!dx && !dy) return;
            event.preventDefault(); const amount = event.shiftKey ? .005 : .02;
            onChange(moveNodes(curve, picked.has(node.id) ? [...picked] : [node.id], dx * (snap ? .0625 : amount), dy * amount, snap));
          }}><circle cx={cx} cy={cy} r="13" fill="transparent" /><circle cx={cx} cy={cy} r="5" className="node-dot" /></g>;
      })}
    </svg>
    <div className="graph-ruler"><span>{Math.round(offset * 100)}%</span><span>{duration ? `${(offset * duration).toFixed(2)} – ${((offset + 1 / zoom) * duration).toFixed(2)} s` : 'Normalized time'}</span><span>{Math.round((offset + 1 / zoom) * 100)}%</span></div>
    {zoom > 1 && <label className="timeline-pan">Timeline view start <input type="range" min={0} max={1 - 1 / zoom} step={.005} value={offset} onChange={e => setOffset(+e.target.value)} /></label>}
    <div className="inline curve-fields"><span>Time</span><NumericValue binding={{ spec: linear('node-x', 'Node time', 0, 1, 0, .001), value: primary.x, disabled: primary.x === 0 || primary.x === 1, onChange: next => onChange(moveNodes(curve, [primary.id], next - primary.x, 0, snap)) }} /><span>Value</span><NumericValue binding={{ spec: linear('node-y', 'Node value', 0, 1, .5, .001), value: primary.y, onChange: next => onChange(moveNodes(curve, [primary.id], 0, next - primary.y)) }} /><span className="muted">{curve.length}/16 nodes</span></div>
  </div>;
}
