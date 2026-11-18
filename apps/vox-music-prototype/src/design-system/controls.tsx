import { useId, useRef, useState } from 'react';
import type { CSSProperties, KeyboardEvent, PointerEvent, ReactNode, RefObject } from 'react';
import { AudioLines, TriangleAlert } from 'lucide-react';
import { clamp, numberText, parsePlain, quantize, reset, toNormalized, toPlain, unit01, valueText } from './parameters';
import type { ParameterBinding } from './parameters';
import { cx } from './ui';

type GestureKind = 'dial' | 'vertical' | 'horizontal' | 'encoder';
function useGesture(binding: ParameterBinding, kind: GestureKind, track?: RefObject<HTMLElement | null>) {
  const { spec, value, onBegin, onEnd, onChange, disabled } = binding;
  const drag = useRef<{ pointer: number; x: number; y: number; position: number } | null>(null);
  const keySession = useRef(false);
  const last = useRef(value);
  const endKeys = () => { if (keySession.current) { keySession.current = false; onEnd?.(); } };
  const apply = (plain: number) => {
    const next = quantize(spec, plain);
    if (next !== last.current) { last.current = next; onChange(next); }
  };
  const positionAt = (event: PointerEvent<HTMLElement>) => {
    const rect = (track?.current ?? event.currentTarget).getBoundingClientRect();
    return unit01(kind === 'vertical' ? (rect.bottom - event.clientY - 5) / Math.max(1, rect.height - 10) : (event.clientX - rect.left - 6) / Math.max(1, rect.width - 12));
  };
  function endPointer(event: PointerEvent<HTMLElement>) {
    if (drag.current?.pointer !== event.pointerId) return;
    drag.current = null;
    onEnd?.();
  }
  function onKeyDown(event: KeyboardEvent<HTMLElement>) {
    if (disabled || event.altKey || event.ctrlKey || event.metaKey) return;
    const key = event.key;
    const dir = key === 'ArrowRight' || key === 'ArrowUp' || key === 'PageUp' ? 1 : key === 'ArrowLeft' || key === 'ArrowDown' || key === 'PageDown' ? -1 : 0;
    if (!dir && !['Home', 'End', 'Delete'].includes(key)) return;
    event.preventDefault();
    if (!keySession.current) { last.current = value; keySession.current = true; onBegin?.(); }
    const step = event.shiftKey ? spec.step : (spec.keyStep ?? spec.step) * (key.startsWith('Page') ? 10 : 1);
    let next = key === 'Home' ? spec.min : key === 'End' ? spec.max : key === 'Delete' ? spec.defaultValue : last.current + dir * step;
    if (kind === 'encoder' && dir) next = next > spec.max ? spec.min : next < spec.min ? spec.max : next;
    apply(next);
  }
  return {
    role: 'slider', tabIndex: disabled ? -1 : 0, 'aria-label': spec.label, 'aria-valuemin': spec.min, 'aria-valuemax': spec.max,
    'aria-valuenow': value, 'aria-valuetext': valueText(spec, value), 'aria-disabled': disabled || undefined,
    'aria-orientation': kind === 'vertical' ? 'vertical' as const : 'horizontal' as const,
    onPointerDown: (event: PointerEvent<HTMLElement>) => {
      if (disabled || event.button !== 0 || drag.current) return;
      endKeys(); last.current = value;
      onBegin?.();
      event.currentTarget.focus(); event.currentTarget.setPointerCapture(event.pointerId);
      // Relative dragging preserves the base at press time, including on a track.
      // A drag, arrow keys and the adjacent numeric value all edit the same model.
      drag.current = { pointer: event.pointerId, x: event.clientX, y: event.clientY, position: toNormalized(spec, value) };
    },
    onPointerMove: (event: PointerEvent<HTMLElement>) => {
      const start = drag.current;
      if (!start || start.pointer !== event.pointerId || disabled) return;
      const rect = (track?.current ?? event.currentTarget).getBoundingClientRect();
      const dx = event.clientX - start.x; const dy = start.y - event.clientY;
      const movement = kind === 'horizontal' ? dx / Math.max(30, rect.width - 12)
        : kind === 'vertical' ? dy / Math.max(24, rect.height - 10) : (dy + dx * .45) / 132;
      let next = start.position + movement * (event.shiftKey ? .14 : 1);
      if (kind === 'encoder' && (next > 1 || next < 0)) next = (next % 1 + 1) % 1;
      start.position = unit01(next);
      start.x = event.clientX; start.y = event.clientY;
      apply(toPlain(spec, start.position));
    },
    onPointerUp: (event: PointerEvent<HTMLElement>) => {
      const start = drag.current;
      // Alt-click explicitly jumps. A regular press is relative, including double-click to reset.
      if (start?.pointer === event.pointerId && event.altKey && (kind === 'horizontal' || kind === 'vertical')) apply(toPlain(spec, positionAt(event)));
      endPointer(event);
    },
    onPointerCancel: endPointer, onLostPointerCapture: endPointer,
    onDoubleClick: () => { endKeys(); reset(binding); },
    onKeyDown, onKeyUp: endKeys, onBlur: endKeys,
  };
}

export function NumericValue({ binding, className }: { binding: ParameterBinding; className?: string }) {
  const { spec, value, disabled, onBegin, onChange, onEnd } = binding;
  const [draft, setDraft] = useState<string | null>(null);
  const escaped = useRef(false);
  return <span className={cx('numeric-wrap', className)}>
    <span className="numeric-display" aria-hidden="true">{valueText(spec, value)}</span>
    <input key={spec.id} type="text" inputMode="decimal" autoComplete="off" spellCheck={false} className="numeric-input"
      aria-label={`${spec.label} value${spec.unit ? ` (${spec.unit})` : ''}`} disabled={disabled} value={draft ?? numberText(value)} onChange={event => setDraft(event.currentTarget.value)}
      onFocus={event => { setDraft(numberText(value)); event.currentTarget.select(); }}
      onBlur={event => {
        const parsed = escaped.current ? null : parsePlain(spec, event.currentTarget.value);
        const next = parsed === null ? value : quantize(spec, parsed);
        setDraft(null); escaped.current = false;
        if (next !== value) { onBegin?.(); onChange(next); onEnd?.(); }
      }} onKeyDown={event => {
        if (event.key === 'Enter' || event.key === 'Escape') {
          event.preventDefault(); escaped.current = event.key === 'Escape'; event.currentTarget.blur();
        }
      }} />
    {spec.unit && <span className="numeric-unit" aria-hidden="true">{spec.unit}</span>}
  </span>;
}

function ParameterFrame({ binding, children, className, showLabel = true }: { binding: ParameterBinding; children: ReactNode; className: string; showLabel?: boolean }) {
  const state = binding.message?.severity ?? (binding.disabled ? 'disabled' : binding.effective !== undefined ? 'modulated' : binding.automated ? 'automated' : binding.selected ? 'selected' : 'normal');
  return <div className={cx('parameter', className)} data-state={state} data-disabled={binding.disabled || undefined} data-selected={binding.selected || undefined}>
    {showLabel && <span className="parameter-label">{binding.spec.label}</span>}
    {children}
    <NumericValue binding={binding} />
    {binding.effective !== undefined && <small className="effective-value" title="Effective value; the base stays unchanged">Mod {valueText(binding.spec, binding.effective)}</small>}
    {binding.automated && <small className="auto-value"><AudioLines size={9} aria-hidden="true" /> Auto</small>}
    {binding.message && <small className={`parameter-message tone-${binding.message.severity}`}><TriangleAlert size={10} aria-hidden="true" />{binding.message.text}</small>}
  </div>;
}

function point(normalized: number, radius: number) {
  const angle = (normalized * 270 + 135) * Math.PI / 180;
  return [32 + Math.cos(angle) * radius, 32 + Math.sin(angle) * radius];
}
function arc(start: number, end: number, r: number) {
  const [x1, y1] = point(start, r); const [x2, y2] = point(end, r);
  return `M${x1} ${y1} A${r} ${r} 0 ${Math.abs(end - start) > 2 / 3 ? 1 : 0} ${end > start ? 1 : 0} ${x2} ${y2}`;
}
export function Knob({ binding, variant = 'rotary', size = 'normal', intensity = false, showLabel = true }: { binding: ParameterBinding; variant?: 'rotary' | 'bipolar' | 'encoder' | 'macro'; size?: 'normal' | 'small' | 'large' | 'micro'; intensity?: boolean; showLabel?: boolean }) {
  const id = useId();
  const gesture = useGesture(binding, variant === 'encoder' ? 'encoder' : 'dial');
  const p = toNormalized(binding.spec, binding.value);
  const center = variant === 'bipolar' ? toNormalized(binding.spec, 0) : 0;
  const mod = binding.effective === undefined ? undefined : toNormalized(binding.spec, binding.effective);
  const [mx, my] = point(p, 16.5); const [sx, sy] = point(p, 10);
  const scaleColour = intensity ? `hsl(${105 - p * 94} 66% 64%)` : undefined;
  return <ParameterFrame className={`knob parameter-${size}`} binding={binding} showLabel={showLabel}>
    <div className="knob-target" {...gesture} title={`${binding.spec.label} · drag / Shift for fine · double-click to reset`}>
      <svg viewBox="0 0 64 64" aria-hidden="true" className="knob-skin">
        <defs><linearGradient id={id} x2=".48" y2="1"><stop stopColor="#646a72" /><stop offset=".43" stopColor="#373b43" /><stop offset="1" stopColor="#222630" /></linearGradient></defs>
        {Array.from({ length: 13 }, (_, n) => {
          const [x1, y1] = point(n / 12, n % 6 === 0 ? 29 : 29.7); const [x2, y2] = point(n / 12, 31.2);
          return <line key={n} x1={x1} x2={x2} y1={y1} y2={y2} className={variant === 'bipolar' && n === 6 ? 'knob-mid' : 'knob-tick'} />;
        })}
        <path d={arc(0, 1, 25.9)} className="knob-track" />
        {Math.abs(p - center) > 0.0001 && <path d={arc(center, p, 25.9)} className="knob-value" style={{ stroke: scaleColour }} />}
        {mod !== undefined && mod !== p && <path d={arc(p, mod, 28.8)} className="knob-mod" />}
        <circle cx="32" cy="33.6" r="20.6" fill="#101116" />
        <circle cx="32" cy="32" r="20.1" fill={`url(#${id})`} stroke="#0f1218" strokeWidth=".7" className="knob-cap" />
        <circle cx="32" cy="32" r="17.7" fill="none" stroke="#a6b0bb" strokeOpacity=".12" strokeWidth=".65" />
        <line x1={sx} y1={sy} x2={mx} y2={my} className="knob-pointer" />
      </svg>
    </div>
  </ParameterFrame>;
}

function Linear({ binding, vertical, compact, showLabel }: { binding: ParameterBinding; vertical?: boolean; compact?: boolean; showLabel?: boolean }) {
  const gesture = useGesture(binding, vertical ? 'vertical' : 'horizontal');
  const style = { '--position': `${toNormalized(binding.spec, binding.value) * 100}%`, '--effective': `${binding.effective === undefined ? 0 : toNormalized(binding.spec, binding.effective) * 100}%` } as CSSProperties;
  return <ParameterFrame binding={binding} showLabel={showLabel} className={cx(vertical ? 'fader' : 'linear', compact && 'fader-compact')}>
    <div className="linear-target" {...gesture} title={`${binding.spec.label} · drag or Alt-click · double-click to reset`} style={style}>
      <span className="linear-ruler" aria-hidden="true"/><span className="linear-rail" aria-hidden="true"><i className="linear-fill"/>{binding.effective !== undefined && <i className="linear-mod"/>}<i className="linear-thumb"/></span>
    </div>
  </ParameterFrame>;
}
export const Fader = ({ binding, compact, showLabel }: { binding: ParameterBinding; compact?: boolean; showLabel?: boolean }) => <Linear binding={binding} vertical compact={compact} showLabel={showLabel} />;
export const Slider = ({ binding }: { binding: ParameterBinding }) => <Linear binding={binding} />;

export function RangeSlider({ start, end, gap = 0.001 }: { start: ParameterBinding; end: ParameterBinding; gap?: number }) {
  const track = useRef<HTMLDivElement>(null);
  const left = { ...start, onChange: (next: number) => start.onChange(Math.min(next, end.value - gap)) };
  const right = { ...end, onChange: (next: number) => end.onChange(Math.max(next, start.value + gap)) };
  const startGesture = useGesture(left, 'horizontal', track);
  const endGesture = useGesture(right, 'horizontal', track);
  const positions = { '--range-start': `${toNormalized(start.spec, start.value) * 100}%`, '--range-end': `${toNormalized(end.spec, end.value) * 100}%` } as CSSProperties;
  return <div className="range-slider">
    <div className="range-track" ref={track} style={positions}><span className="linear-rail" aria-hidden="true"><i className="range-fill" /></span>
      <span className="range-handle range-start" {...startGesture} title={`${start.spec.label} · drag or use arrow keys`}><i className="linear-thumb" /></span>
      <span className="range-handle range-end" {...endGesture} title={`${end.spec.label} · drag or use arrow keys`}><i className="linear-thumb" /></span>
    </div>
    <div className="range-values"><span className="parameter-label">{start.spec.label}</span><NumericValue binding={left} /><div className="spacer" /><span className="parameter-label">{end.spec.label}</span><NumericValue binding={right} /></div>
  </div>;
}
export const amplitudeToDb = (value: number) => value > 0 ? 20 * Math.log10(value) : -Infinity;
export const peakText = (value: number) => value > 0.001 ? `${amplitudeToDb(value).toFixed(1)} dBFS` : '−∞ dBFS';
export function Meter({ value = null, label, orientation = 'vertical', showValue = false, kind = 'peak', over = false }: { value?: number | null; label: string; orientation?: 'vertical' | 'horizontal'; showValue?: boolean; kind?: 'peak' | 'rms'; over?: boolean }) {
  const db = value === null ? -Infinity : amplitudeToDb(value);
  const count = Math.round(unit01((db + 58) / 58) * 16);
  const text = value === null ? 'Not connected' : `${peakText(value)} ${kind.toUpperCase()}`;
  return <div className={`meter meter-${orientation}`} data-active={value !== null && value > .001} data-value={value ?? undefined} data-over={over || undefined} title={`${label}: ${text}`}>
    <div className="meter-bars" role="meter" aria-label={label} aria-valuemin={-60} aria-valuemax={0} aria-valuenow={clamp(db, -60, 0)} aria-valuetext={text}>
      {Array.from({ length: 16 }, (_, i) => <i key={i} data-level={i < 11 ? 'signal' : i < 15 ? 'headroom' : 'limit'} className={i < count ? 'lit' : undefined} />)}
    </div>
    {showValue && <span className="meter-text">{text}</span>}
  </div>;
}
