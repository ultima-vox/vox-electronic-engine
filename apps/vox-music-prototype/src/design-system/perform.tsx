import { useRef } from 'react';
import type { CSSProperties, KeyboardEvent, MouseEvent } from 'react';
import { Check, Play, Plus } from 'lucide-react';
import { NumericValue } from './controls';
import { clamp, toNormalized, toPlain, unit01 } from './parameters';
import type { ParameterBinding } from './parameters';
import { Button, cx, prop } from './ui';

export function midiNote(note: number) {
  return ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'][note % 12] + (Math.floor(note / 12) - 2);
}
export const numberTag = (i: number) => String(i).padStart(2, '0');
export type PadProps = { index: number; name: string; shortName?: string; route?: string; note: number; colour: string; selected: boolean; muted?: boolean; solo?: boolean; velocity?: number; onAudition: () => void; onSelect: () => void; onMute?: () => void; onSolo?: () => void; list?: boolean; disabled?: boolean };
export function Pad({ index, name, shortName, route, note, selected, colour, velocity, muted, solo, list, disabled, onSelect, onAudition, onMute, onSolo }: PadProps) {
  return <article className={cx('pad', list && 'pad-list-row')} data-selected={selected} data-muted={!!muted} data-hit={velocity !== undefined} style={{ '--pad-color': colour, '--hit-energy': (velocity ?? 0) / 127 } as CSSProperties}>
    <button type="button" className="pad-face" onClick={onAudition} data-pad-audition={index} aria-description={`${name}, pad ${index + 1}, MIDI note ${note}. Plays sound without changing selection.`} disabled={disabled} title={`Audition ${name} · MIDI ${note}`}>
      <span className="sr-only">Audition </span><span className="pad-index">{numberTag(index + 1)}</span><span className="pad-led" aria-hidden="true"/><strong>{!list && shortName && <span className="pad-name-short">{shortName}</span>}<span className="pad-name-full">{name}</span></strong><small>{midiNote(note)}</small>{list && route && <small className="pad-route">{route}</small>}{list && <Play size={11} aria-hidden="true" />}
    </button>
    <div className="pad-commands">
      <button type="button" className="pad-select" onClick={onSelect} data-pad-select={index} aria-pressed={selected} title={`Select ${name} without playing`}>
        {selected ? <Check size={9} aria-hidden="true" /> : <span className="select-point" aria-hidden="true" />}{list ? (selected ? 'Selected' : 'Select') : <><span className="pad-select-text" aria-hidden="true">SEL</span><span className="sr-only">Select</span></>}<span className="sr-only"> {name}, pad {index + 1}</span>
      </button>
      {onMute && <button type="button" className="pad-ms" onClick={onMute} aria-label={`Mute ${name}`} aria-pressed={!!muted} title={`Mute ${name}`}>M</button>}
      {onSolo && <button type="button" className="pad-ms" onClick={onSolo} aria-label={`Solo ${name}`} aria-pressed={!!solo} title={`Solo ${name}`}>S</button>}
    </div>
  </article>;
}
export function PadGrid({ pads, list }: { pads: readonly PadProps[]; list?: boolean }) {
  const root = useRef<HTMLDivElement>(null);
  function key(event: KeyboardEvent<HTMLDivElement>) {
    if (!(event.target instanceof HTMLButtonElement)) return;
    const isSelect = event.target.dataset.padSelect !== undefined;
    const index = Number(event.target.dataset.padSelect ?? event.target.dataset.padAudition);
    if (!Number.isInteger(index)) return;
    const delta = event.key === 'ArrowDown' ? (list ? 1 : 4) : event.key === 'ArrowUp' ? (list ? -1 : -4) : event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
    if (!delta) return;
    event.preventDefault(); root.current?.querySelector<HTMLButtonElement>(`[data-pad-${isSelect ? 'select' : 'audition'}="${clamp(index + delta, 0, pads.length - 1)}"]`)?.focus();
  }
  return <div className={list ? 'pad-list' : 'pad-grid'} ref={root} onKeyDown={key}>{pads.map(pad => <Pad key={pad.index} {...pad} list={list} />)}</div>;
}
export function TriggerButton({ label = 'Trigger', onTrigger, disabled }: { label?: string; onTrigger: () => void; disabled?: boolean }) {
  return <Button variant="primary" onClick={onTrigger} disabled={disabled}><Play size={13} aria-hidden="true" />{label}</Button>;
}
export function Keyboard({ first = 48, count = 13, active = [], onPress, onRelease }: { first?: number; count?: number; active?: readonly number[]; onPress: (note: number) => void; onRelease?: (note: number) => void }) {
  const pressed = useRef(new Set<number>());
  const start = (note: number) => { if (!pressed.current.has(note)) { pressed.current.add(note); onPress(note); } };
  const stop = (note: number) => { if (pressed.current.delete(note)) onRelease?.(note); };
  return <div className="piano" role="group" aria-label="Trigger keyboard">
    {Array.from({ length: count }, (_, n) => {
      const note = first + n; const black = [1, 3, 6, 8, 10].includes(note % 12);
      return <button type="button" key={note} className={cx('piano-key', black && 'key-black')} aria-label={`Play ${midiNote(note)} (${note})`} data-active={active.includes(note)}
        onPointerDown={event => { if (event.button === 0) { event.currentTarget.setPointerCapture(event.pointerId); start(note); } }} onPointerUp={() => stop(note)} onLostPointerCapture={() => stop(note)}
        onKeyDown={event => { if (event.key === ' ' || event.key === 'Enter') { event.preventDefault(); start(note); } }} onKeyUp={event => { if (event.key === ' ' || event.key === 'Enter') { event.preventDefault(); stop(note); } }} onBlur={() => stop(note)}
        onClick={event => { if (event.detail === 0 && !pressed.current.has(note)) { start(note); stop(note); } }}><span>{midiNote(note)}</span></button>;
    })}
  </div>;
}
export function XYPad({ x, y }: { x: ParameterBinding; y: ParameterBinding }) {
  const dragging = useRef<number | null>(null);
  const disabled = !!(x.disabled || y.disabled);
  const xPos = toNormalized(x.spec, x.value); const yPos = toNormalized(y.spec, y.value);
  function move(event: React.PointerEvent<HTMLDivElement>) {
    const rect = event.currentTarget.getBoundingClientRect();
    const nextX = unit01((event.clientX - rect.left) / rect.width);
    const nextY = 1 - unit01((event.clientY - rect.top) / rect.height);
    x.onChange(toPlain(x.spec, event.shiftKey ? xPos + (nextX - xPos) * .1 : nextX));
    y.onChange(toPlain(y.spec, event.shiftKey ? yPos + (nextY - yPos) * .1 : nextY));
  }
  return <div className="xy-control"><div className="xy-pad" role="img" aria-label={`${x.spec.label} by ${y.spec.label}; edit exact values below`} data-disabled={disabled}
    onPointerDown={event => { if (!disabled && event.button === 0 && dragging.current === null) { dragging.current = event.pointerId; x.onBegin?.(); if (x.onBegin !== y.onBegin) y.onBegin?.(); event.currentTarget.setPointerCapture(event.pointerId); move(event); } }}
    onPointerMove={event => { if (dragging.current === event.pointerId) move(event); }}
    onLostPointerCapture={() => { dragging.current = null; x.onEnd?.(); if (x.onEnd !== y.onEnd) y.onEnd?.(); }}
    onDoubleClick={() => { if (!disabled) { x.onBegin?.(); if (x.onBegin !== y.onBegin) y.onBegin?.(); x.onChange(x.spec.defaultValue); y.onChange(y.spec.defaultValue); x.onEnd?.(); if (x.onEnd !== y.onEnd) y.onEnd?.(); } }}>
    <i className="xy-marker" style={{ left: `${xPos * 100}%`, top: `${(1 - yPos) * 100}%` }} />
  </div><div className="inline xy-fields"><span>{x.spec.label}</span><NumericValue binding={x} /><span>{y.spec.label}</span><NumericValue binding={y} /></div></div>;
}

export type StepVisual = { on: boolean; selected?: boolean; strength?: number; locked?: boolean };
export function StepGrid({ label, rows, steps, beatSize = steps / 4, barSize = steps, playhead = -1, getCell, onStep, onInspect, onRow, lane, rowActions }: {
  label: string; rows: readonly { name: string; colour: string; selected?: boolean }[]; steps: number; beatSize?: number; barSize?: number; playhead?: number;
  getCell: (row: number, step: number) => StepVisual; onStep: (row: number, step: number, event: MouseEvent<HTMLButtonElement>) => void;
  onInspect?: (row: number, step: number, extend: boolean) => void; onRow?: (row: number) => void;
  lane?: { name: string; cell: (step: number) => React.ReactNode }; rowActions?: (row: number) => React.ReactNode;
}) {
  const root = useRef<HTMLDivElement>(null);
  const cursor = useRef([0, 0]);
  function keys(event: KeyboardEvent<HTMLButtonElement>, row: number, step: number) {
    if (event.altKey || event.metaKey || event.ctrlKey) return;
    const dx = event.key === 'ArrowLeft' ? -1 : event.key === 'ArrowRight' ? 1 : 0;
    const dy = event.key === 'ArrowUp' ? -1 : event.key === 'ArrowDown' ? 1 : 0;
    if (!dx && !dy && event.key !== 'Home' && event.key !== 'End') {
      if (event.key.toLowerCase() === 'i' && onInspect) { event.preventDefault(); onInspect(row, step, event.shiftKey); }
      return;
    }
    event.preventDefault();
    const nr = clamp(row + dy, 0, rows.length - 1);
    const ns = event.key === 'Home' ? 0 : event.key === 'End' ? steps - 1 : clamp(step + dx, 0, steps - 1);
    root.current?.querySelector<HTMLButtonElement>(`[data-cell="${nr}:${ns}"]`)?.focus();
    if (event.shiftKey) onInspect?.(nr, ns, true);
  }
  return <div className="step-scroll" ref={root} aria-label={label} role="group">
    <div className="step-sheet" style={prop('--steps', steps)}>
      <div className="step-row step-ruler" aria-hidden="true"><span className="step-row-title">Instrument</span>{Array.from({ length: steps }, (_, n) => <span key={n} data-quarter={Math.floor(n / beatSize) % 2} data-beat={n % beatSize === 0} data-bar={n % barSize === 0} data-playing={playhead === n}>{numberTag(n + 1)}</span>)}</div>
      {rows.map(({ name, colour, selected }, row) => <div className="step-row" key={row} data-selected={!!selected} style={prop('--track-color', colour)}>
        <div className="step-row-title">{onRow ? <button type="button" onClick={() => onRow(row)} aria-label={`Edit ${name} lane`} aria-pressed={!!selected} className="step-track"><i aria-hidden="true"/><span>{name}</span></button> : <span className="step-track"><i aria-hidden="true"/><span>{name}</span></span>}{rowActions?.(row)}</div>
        {Array.from({ length: steps }, (_, n) => {
          const { on, strength = 1, locked, selected: cellSelected } = getCell(row, n);
          return <div className="step-cell" key={n} data-quarter={Math.floor(n / beatSize) % 2} data-beat={n % beatSize === 0} data-bar={n % barSize === 0} data-playing={playhead === n}>
            <button type="button" title={`${name} · step ${n + 1}${locked ? ' · locked for generation' : ''}`} className="step-hit" data-cell={`${row}:${n}`} data-selected={!!cellSelected} data-locked={!!locked} aria-pressed={on} aria-label={`${name}, step ${n + 1}${locked ? ', generator locked' : ''}`}
              tabIndex={Math.min(cursor.current[0], rows.length - 1) === row && Math.min(cursor.current[1], steps - 1) === n ? 0 : -1} style={prop('--strength', on ? .44 + strength * .56 : 0)} onClick={event => onStep(row, n, event)} onContextMenu={event => { if (onInspect) { event.preventDefault(); onInspect(row, n, event.shiftKey); } }}
              onFocus={event => {
                root.current?.querySelector<HTMLButtonElement>('.step-hit[tabindex="0"]')?.setAttribute('tabindex', '-1');
                cursor.current = [row, n]; event.currentTarget.tabIndex = 0;
              }} onKeyDown={event => keys(event, row, n)}><span /></button>
          </div>;
        })}
      </div>)}
      {lane && <div className="step-row step-lane"><span className="step-row-title"><i aria-hidden="true" />{lane.name}</span>{Array.from({ length: steps }, (_, n) => <div className="lane-cell" key={n} data-quarter={Math.floor(n / beatSize) % 2} data-beat={n % beatSize === 0} data-bar={n % barSize === 0}>{lane.cell(n)}</div>)}</div>}
    </div>
  </div>;
}
export function SongTimeline({ count, selected, playing = -1, details, onSelect }: { count: number; selected: number; playing?: number; details: (bar: number) => { name: string; type: string; colour?: string }; onSelect: (bar: number) => void }) {
  return <div className="arranger-grid" aria-label="Song timeline" role="group">{Array.from({ length: count }, (_, bar) => {
    const detail = details(bar);
    return <button type="button" key={bar} className="arranger-bar" aria-pressed={selected === bar} data-playing={playing === bar} aria-label={`Bar ${bar + 1}: ${detail.name}, ${detail.type}`} style={detail.colour ? prop('--bar-color', detail.colour) : {}} onClick={() => onSelect(bar)}><small>BAR {numberTag(bar + 1)}</small><strong>{detail.name}</strong><span>{detail.type}</span></button>;
  })}</div>;
}
export function AddButton({ children, onClick, disabled }: { children: React.ReactNode; onClick: () => void; disabled?: boolean }) {
  return <Button onClick={onClick} disabled={disabled}><Plus size={13} aria-hidden="true" />{children}</Button>;
}
