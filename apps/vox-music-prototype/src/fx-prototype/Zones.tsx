import { useRef, useState } from 'react';
import { Activity, ArrowRight, AudioLines, CircleStop, Clipboard, Copy, Layers3, Play, SlidersHorizontal, Square, WandSparkles, Waves } from 'lucide-react';
import { Fader, Knob, Meter, NumericValue } from '../design-system/controls';
import { CurveEditor, CurveView } from '../design-system/graph';
import type { Curve } from '../design-system/curves';
import { linear } from '../design-system/parameters';
import { Keyboard, midiNote, StepGrid, TriggerButton } from '../design-system/perform';
import { Badge, Button, Module, Notice, Segmented, Select, Switch, Toggle } from '../design-system/ui';
import { categories, curveNames, divisions, durationSeconds, effectSpec, effectTypes, effectiveValue, families, familyControls, fxHit, fxLimits, generate, layerLetters, layerParams, layerSpecs, styles, syncChoices, triggerModes } from './model';
import type { EffectType, FXHit, FXLayer, FXSlot, Family, Locks, Program } from './model';
import type { FXSession } from './session';

export function EventIndicator({ s }: { s: FXSession }) {
  const p = s.history.value;
  const { event, phase, events } = s.preview;
  return <div className="event-readout stack">
    <div className="inline wrap"><Badge tone={event ? 'good' : 'neutral'} dot>{event ? 'VISUAL EVENT RUNNING' : 'VISUAL EVENT READY'}</Badge><div className="spacer" /><span className="mono caption">{events.length} / {fxLimits.events} UI events</span></div>
    <div className="event-chart"><CurveView label="Evaluated global amplitude curve, configuration preview only" curve={p.curves[16]} />{event && <i className="event-position" style={{ left: `${phase * 100}%` }} aria-hidden="true" />}</div>
    <div className="graph-ruler"><span>{event ? `${event.source} · ${midiNote(event.note)} · v${event.velocity}` : 'Event phase'}</span><span>{event ? `${((event.end - event.start) * phase).toFixed(2)} / ${(event.end - event.start).toFixed(2)} s` : `${durationSeconds(p).toFixed(2)} s configured`}</span></div>
    <Meter label="FX stereo output" orientation="horizontal" showValue />
  </div>;
}
export function PerformZone({ s, locks, setLocks }: { s: FXSession; locks: Locks; setLocks: (next: Locks) => void }) {
  const p = s.history.value;
  const change = <K extends keyof Program>(key: K, value: Program[K]) => s.history.edit(old => old[key] === value ? old : { ...old, [key]: value });
  const recipes = ['intensity','complexity','energy','stereo'] as const;
  const recipeLabels = ['Intensity','Complexity','Energy','Stereo motion'];
  return <div className="fx-perform">
    <Module title="Program Generator" icon={<WandSparkles size={15} aria-hidden="true" />} actions={<Badge tone="accent">SEEDED</Badge>}>
      <div className="panel-body stack">
        <div className="generator-fields"><Select label="Category" value={p.category} onChange={e => change('category', e.target.value as Program['category'])}>{categories.map(name => <option key={name}>{name}</option>)}</Select>
          <Select label="Style" value={p.style} onChange={e => change('style', e.target.value as Program['style'])}>{styles.map(name => <option key={name}>{name}</option>)}</Select>
          <Select label="Pitch key" value={p.key} onChange={e => change('key', +e.target.value)}>{Array.from({ length: 12 }, (_, n) => <option key={n} value={n}>{midiNote(48 + n).slice(0,-1)}</option>)}</Select></div>
        <div className="inline wrap"><span className="eyebrow">Seed</span><NumericValue binding={s.bind(linear('seed','Generation seed',1,999999,2026,1), p.seed, n => change('seed',n))} />
          <span className="eyebrow">Length</span><Segmented label="Event duration units" value={p.lengthMode} choices={[{ value: 's', label: 'Seconds' }, { value: 'bars', label: 'Bars' }]} onChange={mode => change('lengthMode', mode)} />
          <NumericValue binding={p.lengthMode === 's' ? s.bind(linear('duration','Seconds',.05,32,4,.01,'s'), p.seconds, n => change('seconds',n)) : s.bind(linear('bars','Bars',1,16,2,1,'bars'), p.bars, n => change('bars',n))} />
        </div>
        <div className="fx-recipe-dials">{recipes.map((key, i) => <Knob key={key} binding={s.bind(linear(key,recipeLabels[i],0,100,[58,48,62,66][i],1,'%'), p[key], value => change(key,value))} />)}</div>
        <div className="inline wrap fx-locks"><span className="eyebrow">Keep on generate</span>{(Object.keys(locks) as (keyof Locks)[]).map(key => <Toggle key={key} checked={locks[key]} onClick={() => setLocks({ ...locks, [key]: !locks[key] })}>{key[0].toUpperCase() + key.slice(1)}</Toggle>)}</div>
        <div className="inline wrap"><Button variant="primary" onClick={() => { s.history.edit(old => generate(old,locks)); s.say('Seeded browser Program generated. No DSP render was requested.'); }}><WandSparkles size={14} aria-hidden="true" /> Generate program</Button><span className="caption">Creates editable Program data. No audio render.</span></div>
      </div>
    </Module>
    <Module title="Event Preview" icon={<Activity size={15} aria-hidden="true" />} actions={<Badge>NO DSP</Badge>}>
      <div className="panel-body stack"><div className="fx-feature"><span className="eyebrow">Selected Program</span><h3>{p.name}</h3><span className="caption">{p.category} / {p.style}</span></div>
        <EventIndicator s={s} /><div className="inline wrap"><TriggerButton label="Preview event" onTrigger={() => s.preview.trigger(48, 100, 'Audition')} /><Button onClick={s.preview.panic} disabled={!s.preview.events.length && !s.preview.running}><Square size={12} aria-hidden="true" /> Clear events</Button></div>
      </div>
    </Module>
    <Module className="fx-macros" title="Performance Macros" icon={<SlidersHorizontal size={15} aria-hidden="true" />} actions={<Badge>08 MACROS</Badge>}><div className="panel-body fx-macro-row">{p.macros.map((value, i) => <Knob variant="macro" key={i} binding={s.bind(linear(`macro${i}`,`M${i+1} · ${['Intensity','Motion','Brightness','Space','Texture','Length','Impact','Width'][i]}`,0,100,[58,32,65,44,27,52,48,36][i],1,'%'), value, next => s.history.edit(old => old.macros[i] === next ? old : { ...old, macros: old.macros.map((n,j) => i === j ? next : n) }))} />)}</div></Module>
  </div>;
}
export function DesignZone({ s, openTimeline }: { s: FXSession; openTimeline: (index: number) => void }) {
  const p = s.history.value;
  const change = (i: number, update: (layer: FXLayer) => FXLayer) => s.history.edit(old => {
    const value = update(old.layers[i]); return value === old.layers[i] ? old : { ...old, layers: old.layers.map((layer,n) => i === n ? value : layer) };
  });
  const solo = p.layers.some(layer => layer.active && layer.solo);
  return <div className="fx-layers">{p.layers.map((layer, i) => { const familyControl = familyControls[layer.family]; return <Module title={`Layer ${layerLetters[i]}`} key={i} icon={<Layers3 size={14} aria-hidden="true" />} actions={<Badge tone={layer.active && (!solo || layer.solo) ? 'accent' : 'neutral'}>{layer.active && (!solo || layer.solo) ? 'IN PROGRAM' : 'INACTIVE'}</Badge>}>
    <div className="panel-body stack"><Select label={`Layer ${layerLetters[i]} family`} value={layer.family} onChange={e => change(i, old => ({ ...old, family: e.target.value as Family }))}>{families.map(family => <option key={family}>{family}</option>)}</Select>
      <div className="inline"><Switch label={`Enable layer ${layerLetters[i]}`} checked={layer.active} onChange={v => change(i, old => ({ ...old, active: v }))} /><Toggle checked={layer.solo} onClick={() => change(i, old => ({ ...old, solo: !old.solo }))}>Solo</Toggle></div>
      <div className="fx-layer-dials">{layerParams.map(field => <Knob key={field} size="small" variant={field === 'pan' || field === 'pitch' ? 'bipolar' : 'rotary'} binding={s.bind({ ...layerSpecs[field], id: `${i}.${field}` }, layer[field], n => change(i, old => n === old[field] ? old : { ...old, [field]: n }), { effective: effectiveValue(p, i, field, s.preview.modInput) })} />)}
        <Knob key={familyControl.key} size="small" binding={s.bind(linear(`${i}.${familyControl.key}`,familyControl.label,0,100,50,1,'%'), layer.sourceSettings[familyControl.key], n => change(i, old => n === old.sourceSettings[familyControl.key] ? old : { ...old, sourceSettings: { ...old.sourceSettings, [familyControl.key]: n } }))} /></div>
      {['Granular Cloud','Glitch / Stutter'].includes(layer.family) && <label className="ds-field"><span className="eyebrow">Sample reference only</span><input value={layer.sample} maxLength={100} placeholder="Not linked" onChange={e => change(i, old => ({ ...old, sample: e.target.value }))} /></label>}
      <div className="fx-layer-curves">{curveNames.map((name, n) => <button type="button" onClick={() => openTimeline(i * 4 + n)} key={name} aria-label={`Edit layer ${layerLetters[i]} ${name} curve`}><span>{name}</span><CurveView curve={p.curves[i * 4 + n]} label={`${name} evaluated curve`} /></button>)}</div>
    </div>
  </Module>; })}</div>;
}
export function TimelineZone({ s, index, setIndex }: { s: FXSession; index: number; setIndex: (index: number) => void }) {
  const clipboard = useRef<Curve | null>(null);
  const [, update] = useState(0);
  const p = s.history.value;
  const group = Math.floor(index / 4);
  const target = group === 4 ? 'Global' : `Layer ${layerLetters[group]}`;
  const replace = (curve: Curve) => s.history.edit(old => curve === old.curves[index] ? old : { ...old, curves: old.curves.map((prev, n) => n === index ? curve : prev) });
  return <Module title="Event Timeline" icon={<Activity size={15} aria-hidden="true" />} actions={<Badge tone="accent">20 EDITABLE CURVES</Badge>}>
    <div className="panel-body stack"><div className="inline wrap"><Select label="Timeline curve group" hideLabel value={group} onChange={e => setIndex(+e.target.value * 4)}>{[...layerLetters.map(value => `Layer ${value}`), 'Global'].map((label,n) => <option value={n} key={label}>{label}</option>)}</Select>
      <Segmented label="Time curve" value={index % 4} choices={curveNames.map((name, n) => ({ value: n, label: name }))} onChange={n => setIndex(group * 4 + n)} /><span className="caption">Normalized time · {durationSeconds(p).toFixed(2)} s at the demo clock</span><div className="spacer" />
      <Button onClick={() => { clipboard.current = p.curves[index]; update(n => n + 1); s.say('Curve copied to the session clipboard.'); }}><Copy size={13} aria-hidden="true" /> Copy curve</Button><Button disabled={!clipboard.current} onClick={() => { if (clipboard.current) replace(clipboard.current.map(n => ({ ...n }))); }}><Clipboard size={13} aria-hidden="true" /> Paste</Button></div>
      <div className="curve-directory">{['Layer A','Layer B','Layer C','Layer D','Global'].map((label, i) => <div key={label} data-active={i === group}><span>{label}</span>{curveNames.map((name, x) => <button type="button" aria-label={`${label} ${name} timeline`} aria-pressed={i * 4 + x === index} key={name} onClick={() => setIndex(i * 4 + x)}>{name}</button>)}</div>)}</div>
      <CurveEditor key={index} label={`${target} ${curveNames[index % 4]}`} duration={durationSeconds(p)} curve={p.curves[index]} onChange={replace} onBegin={s.history.begin} onEnd={s.history.end} />
      <p className="caption">Drag a node, Shift + click to select multiple, or use arrows on a focused node. Double-click empty space to add a node. Endpoints stay at 0 and 1.</p>
    </div>
  </Module>;
}
export function ProcessZone({ s }: { s: FXSession }) {
  const p = s.history.value;
  const change = (i: number, update: (slot: FXSlot) => FXSlot) => s.history.edit(old => {
    const next = update(old.slots[i]); return next === old.slots[i] ? old : { ...old, slots: old.slots.map((slot,n) => i === n ? next : slot) };
  });
  return <div className="stack"><Module title="Signal Flow" icon={<AudioLines size={15} aria-hidden="true" />} actions={<Badge>CONFIGURATION ONLY</Badge>}><div className="panel-body inline wrap fx-chain-track"><Badge tone="accent">A–D SUM</Badge>{p.slots.map((slot,i) => <span className="inline" key={i}><ArrowRight size={12} aria-hidden="true" /><Badge tone={slot.type === 'Off' || slot.bypass ? 'neutral' : 'accent'}>{String(i + 1).padStart(2,'0')} · {slot.type === 'Off' ? 'Off' : slot.bypass ? `${slot.type} (bypassed)` : slot.type}</Badge></span>)}<ArrowRight size={12} aria-hidden="true"/><Badge>MASTER</Badge></div></Module>
    <div className="fx-layers">{p.slots.map((slot,i) => <Module key={i} title={`FX Slot ${i + 1}`} actions={<Badge tone={slot.bypass || slot.type === 'Off' ? 'neutral' : 'accent'}>{slot.bypass ? 'BYPASS' : slot.type === 'Off' ? 'OFF' : 'IN ROUTE'}</Badge>}><div className="panel-body stack"><Select label={`Effect type ${i + 1}`} value={slot.type} onChange={e => { const type = e.target.value as EffectType; change(i, old => ({ ...old, type, amount: effectSpec(type).defaultValue })); }}>{effectTypes.map(type => <option key={type}>{type}</option>)}</Select>
      <Switch label={`Bypass slot ${i + 1}`} checked={slot.bypass} onChange={v => change(i, old => ({ ...old, bypass: v }))} />
      <div className="fx-layer-dials">{(['wet','amount','trim'] as const).map(key => <Knob key={key} size="small" variant={key === 'trim' ? 'bipolar' : 'rotary'} binding={s.bind(key === 'amount' ? effectSpec(slot.type) : key === 'wet' ? linear('wet','Wet',0,100,30,1,'%') : linear('trim','Trim',-24,12,0,.1,'dB'), slot[key], n => change(i, old => n === old[key] ? old : { ...old, [key]: n }), { disabled: slot.type === 'Off' })} />)}</div>
      <span className="caption">{slot.bypass || slot.type === 'Off' ? 'Dry path' : `${slot.wet}% wet · ${100 - slot.wet}% dry`} · stored in Program</span>
    </div></Module>)}</div>
    <Module title="Output & Tails" icon={<Waves size={15} aria-hidden="true" />}><div className="panel-body inline wrap"><Fader compact binding={s.bind(linear('master','Master level',0,100,80,1,'%'),p.master,n => s.history.edit(old => old.master === n ? old : { ...old, master: n }))} /><Notice>No audible FX processing or tail measurement is connected. These controls edit the serial chain configuration.</Notice></div></Module>
  </div>;
}
export function TriggerZone({ s }: { s: FXSession }) {
  const p = s.history.value;
  const [selected, select] = useState(0);
  const count = divisions[p.division] * p.patternBars;
  const idx = Math.min(count - 1, selected);
  const hit = p.triggerGrid[idx];
  const change = <K extends keyof Program>(key: K, v: Program[K]) => s.history.edit(old => old[key] === v ? old : { ...old, [key]: v });
  const setHit = (i: number, fn: (h: FXHit | null) => FXHit | null) => s.history.edit(old => {
    const value = fn(old.triggerGrid[i]); return value === old.triggerGrid[i] ? old : { ...old, triggerGrid: old.triggerGrid.map((h,n) => i === n ? value : h) };
  });
  const fields = [linear('note','Note',0,127,48,1),linear('velocity','Velocity',1,127,100,1),linear('probability','Probability',0,100,100,1,'%'),linear('ratchet','Ratchet',1,4,1,1,'×')];
  return <div className="stack">
    <Module title="Trigger Pattern" icon={<Play size={15} aria-hidden="true" />} actions={<Badge>120 BPM · LOCAL 4/4</Badge>}><div className="panel-body stack">
      <div className="inline wrap"><Select label="Trigger mode" value={p.triggerMode} onChange={e => change('triggerMode',e.target.value as Program['triggerMode'])}>{triggerModes.map(mode => <option key={mode}>{mode}</option>)}</Select>
        <Select label="Trigger pattern rate" value={p.division} onChange={e => change('division',e.target.value as Program['division'])}>{syncChoices.map(value => <option key={value}>{value}</option>)}</Select>
        <Select label="Trigger pattern bars" value={p.patternBars} onChange={e => change('patternBars',+e.target.value)}>{[1,2,3,4].map(n => <option key={n} value={n}>{n}</option>)}</Select><div className="spacer" />
        <Button variant={s.preview.running ? 'neutral' : 'primary'} onClick={s.preview.running ? s.preview.stop : s.preview.start}>{s.preview.running ? 'Stop visual pattern' : 'Play visual pattern'}</Button></div>
      <StepGrid label="FX trigger steps" rows={[{ name: 'Trigger', colour: 'var(--accent)', selected: true }]} steps={count} beatSize={divisions[p.division] / 4} barSize={divisions[p.division]} playhead={s.preview.step} getCell={(_, i) => ({ on: !!p.triggerGrid[i], selected: idx === i, strength: (p.triggerGrid[i]?.velocity ?? 0) / 127 })} onInspect={(_, i) => select(i)} onStep={(_, i, e) => { select(i); if (!e.altKey && !e.shiftKey) setHit(i, h => h ? null : { ...fxHit }); }} />
      <div className="inline wrap trigger-cell"><Badge tone="accent">STEP {idx + 1} · {hit ? midiNote(hit.note) : 'EMPTY'}</Badge>{hit ? fields.map(spec => { const key = spec.id as keyof FXHit; return <span className="inline" key={key}><span>{spec.label}</span><NumericValue binding={s.bind(spec, hit[key], n => setHit(idx, old => old && old[key] !== n ? { ...old, [key]: n } : old))} /></span>; }) : <Button onClick={() => setHit(idx, () => ({ ...fxHit }))}>Add trigger</Button>}</div>
    </div></Module>
    <Module title="Trigger Keyboard" icon={<Activity size={15} aria-hidden="true" />} actions={<Badge>MIDI I/O NOT CONNECTED</Badge>}><div className="panel-body stack"><Keyboard active={s.preview.events.map(event => event.note)} onPress={note => s.preview.trigger(note)} onRelease={note => s.preview.release(note)} /><EventIndicator s={s} /><details className="instrument-details"><summary>Active UI trigger events</summary><ul className="caption">{s.preview.events.map(event => <li key={event.id}>{event.source} · {midiNote(event.note)} ({event.note}) · velocity {event.velocity}</li>)}{!s.preview.events.length && <li>No active trigger events</li>}</ul><p className="caption">This list reports the local clock. It does not send MIDI.</p></details></div></Module>
  </div>;
}
export function AdvancedZone({ s }: { s: FXSession }) {
  const stats = [
    ['Layers', `${s.history.value.layers.length} / ${fxLimits.layers}`], ['Time curves', `${s.history.value.curves.length} / ${fxLimits.curves}`], ['Typed routes', `${s.history.value.routes.length} / ${fxLimits.routes}`],
    ['Active visual events', `${s.preview.events.length} / ${fxLimits.events}`], ['DSP grains', `Not connected · cap ${fxLimits.grains}`], ['CPU / memory', 'Not measured'], ['DSP / MIDI I/O', 'Not connected'], ['Program schema', `v${s.history.value.version} · browser JSON`],
  ];
  return <div className="stack"><Module title="Instance Diagnostics" icon={<Activity size={15} aria-hidden="true" />} actions={<Badge tone="warning">BROWSER ONLY</Badge>}><div className="panel-body stack"><dl className="fx-stats">{stats.map(([key,value]) => <div key={key}><dt>{key}</dt><dd>{value}</dd></div>)}</dl><Notice>These are Program counts and UI timers. No native DSP allocator, clip detector, CPU profiler or MIDI stream is attached.</Notice></div></Module>
    <Module title="Quality & Safety"><div className="panel-body stack"><div className="inline wrap"><Select label="Target oversampling setting" value={s.history.value.quality} onChange={e => s.history.edit(old => ({ ...old, quality: e.target.value as Program['quality'] }))}><option>Off</option><option>2×</option><option>4×</option></Select><div className="spacer"/><Button variant="danger" onClick={s.preview.panic}><CircleStop size={13} aria-hidden="true" /> Panic visual events</Button></div><p className="caption">Quality is a saved configuration only. WAV bounce and MIDI/WAV drag to a DAW are not connected.</p></div></Module>
  </div>;
}
