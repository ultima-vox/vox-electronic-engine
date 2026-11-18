import { useLayoutEffect, useRef, useState } from 'react';
import type { CSSProperties } from 'react';
import { ArrowUpRight, AudioLines, CircleHelp, Copy, Headphones, LayoutGrid, List, Route, WandSparkles } from 'lucide-react';
import { Fader, Knob, Meter, NumericValue, peakText, Slider } from '../design-system/controls';
import { linear, quantize } from '../design-system/parameters';
import { midiNote, numberTag, PadGrid } from '../design-system/perform';
import { Badge, Button, Field, Notice, Segmented, Select, Switch, Toggle } from '../design-system/ui';
import { baseStep, createKit, draftDefaults, draftGrid, editPattern, editSteps, generatorAmounts, getPattern, initialProject, kitNames, outputName, padColor, patternName, stepSpecs, styles } from './model';
import type { Context, DraftSettings, DrumProject, PadParameter, Step } from './model';
import type { Session } from './session';
import { delayDivisions, delaySeconds } from './delay';
import type { DelayDivision } from './delay';
import type { MixerMode } from './layout';

export const padViews = [{ value: 'grid' as const, label: <><LayoutGrid size={11} aria-hidden="true" /> Grid</> }, { value: 'list' as const, label: <><List size={11} aria-hidden="true" /> List</> }];
const shortNames: Record<string, string> = { 'Closed Hat': 'Cl. Hat', 'Open Hat': 'Op. Hat', 'Low Tom': 'L. Tom', 'Mid Tom': 'M. Tom', 'High Tom': 'H. Tom', Tambourine: 'Tamb.' };
export const quick = ['level', 'pan', 'tune', 'decay'] as const;
export function PadRack({ session: s, view, setView }: { session: Session; view: 'grid' | 'list'; setView: (view: 'grid' | 'list') => void }) {
  const pad = s.history.value.pads[s.selectedPad];
  const hit = s.preview.lastHit;
  return <div className="pad-rack panel-body scroll-body">
    <div className="pad-toolbar inline"><span className="eyebrow">16 voices</span><div className="spacer" /><Segmented label="Pad rack view" value={view} choices={padViews} onChange={setView} /></div>
    <PadGrid list={view === 'list'} pads={s.history.value.pads.map((p, i) => ({
      index: i, name: p.name, shortName: shortNames[p.name], route: outputName(p.out), note: p.note, colour: padColor(i), selected: i === s.selectedPad, muted: p.mute, solo: p.solo,
      onMute: () => s.setPad(i, old => ({ ...old, mute: !old.mute })), onSolo: () => s.setPad(i, old => ({ ...old, solo: !old.solo })),
      onSelect: () => s.selectPad(i), onAudition: () => void s.preview.audition(i), velocity: s.preview.pulses[i],
      disabled: p.source === 'Sample' && !p.players[p.player].sample,
    }))} />
    <div className="rack-readback"><div className="inline"><span className="eyebrow">Pad peak</span><div className="spacer"/><span className="mono caption">{pad.name} · {outputName(pad.out)}</span></div>
      <Meter value={s.preview.levels.pads[s.selectedPad]} label={`${pad.name} local post-fader peak`} orientation="horizontal" showValue />
      <div className="rack-velocity inline"><AudioLines size={12} aria-hidden="true" /><span>{hit ? `Last hit · Pad ${numberTag(hit.pad + 1)} / velocity ${hit.velocity}` : 'Last hit · —'}</span></div>
    </div>
    <div className="rack-help"><CircleHelp size={12} aria-hidden="true" /><span>Play the rubber face. <b>SEL</b> edits without sounding.</span></div>
  </div>;
}
export function MuteSolo({ session: s, pad, context, compact }: { session: Session; pad: number; context: string; compact?: boolean }) {
  const value = s.history.value.pads[pad];
  return <div className="mute-solo inline">
    <Toggle className="sm" checked={value.mute} aria-label={`${context} mute ${value.name}`} onClick={() => s.setPad(pad, old => ({ ...old, mute: !old.mute }))}>{compact ? 'M' : 'Mute'}</Toggle>
    <Toggle className="sm" checked={value.solo} aria-label={`${context} solo ${value.name}`} onClick={() => s.setPad(pad, old => ({ ...old, solo: !old.solo }))}>{compact ? 'S' : 'Solo'}</Toggle>
  </div>;
}
export function Inspector({ session: s, pin, unpin }: { session: Session; pin: Context | null; unpin: () => void }) {
  const context = pin ?? s.context;
  const p = context.kind !== 'song' ? s.history.value.pads[context.pad] : null;
  const isPinned = !!pin;
  const heading = (index: number, title: string, subtitle: string) => <div className="inspector-identity inline" style={{ '--track-color': context.kind === 'song' ? 'var(--accent)' : padColor(index) } as CSSProperties}>
    <span className="object-index">{numberTag(index + 1)}</span><div className="object-label"><h3>{title}</h3><span className="caption">{subtitle}</span></div>
  </div>;
  const previewMissing = (text: string) => <Notice tone="warning">{text} {isPinned && <button type="button" className="plain-link" onClick={unpin}>Unpin</button>}</Notice>;
  return <div className="panel-body inspector scroll-body" data-pinned={isPinned}>
    {context.kind === 'pad' && p && <>
      {heading(context.pad, p.name, `${p.source === 'Synth' ? 'Synth voice' : p.source} · ${midiNote(p.note)} / ${p.note}`)}
      <div className="inspector-controls"><div className="eyebrow inspector-caption"><span>Quick controls</span><i aria-hidden="true" /></div>
        <div className="dial-grid">{quick.map(key => <Knob key={`${context.pad}:${key}`} binding={s.bind(context.pad, key)} variant={key === 'tune' || key === 'pan' ? 'bipolar' : 'rotary'} />)}</div></div>
      <div className="inspector-route inline"><Route size={12} aria-hidden="true" /><span>{outputName(p.out)} output</span><div className="spacer" /><MuteSolo session={s} pad={context.pad} context="Inspector" compact /></div>
      {p.source !== 'Synth' && !p.players[p.player].sample && <Notice tone="warning">Player {p.player} is empty. {p.source === 'Layer' ? 'Synth layer only.' : 'No playable sample.'}</Notice>}
      <Button className="open-sound" onClick={() => { s.selectPad(context.pad); s.open('sound'); }}>Open Sound Editor <ArrowUpRight size={13} aria-hidden="true" /></Button>
    </>}
    {context.kind === 'steps' && p && (() => {
      const pattern = getPattern(s.history.value, context.target);
      const spec = context.target.kind === 'source' ? patternName(context.target) : `Bar ${context.target.bar + 1}`;
      if (!pattern || context.indices.some(i => i >= pattern.length)) return previewMissing('The step target is no longer in the active range.');
      const fields = (Object.keys(stepSpecs) as (keyof Step)[]);
      const cells = context.indices.map(i => pattern.tracks[context.pad][i]).filter((cell): cell is Step => !!cell);
      const update = (fn: (cell: Step | null) => Step | null) => s.history.edit(old => editPattern(old, context.target, p => editSteps(p, context.pad, context.indices, fn)));
      return <>{heading(context.pad, `${p.name} · ${context.indices.length === 1 ? `Step ${context.indices[0] + 1}` : `${context.indices.length} steps`}`, `${spec} · ${cells.length} active ${cells.length === 1 ? 'hit' : 'hits'}`)}
        <div className="inspector-controls"><div className="eyebrow inspector-caption"><span>Step values</span><i aria-hidden="true" /></div>
          {cells.length ? <div className="dial-grid">{fields.map(key => <Knob key={`${context.pad}:${key}`} size="small" intensity={key === 'velocity' || key === 'probability'} variant={key === 'micro' ? 'bipolar' : 'rotary'} binding={{
            spec: stepSpecs[key], value: cells[0][key], onBegin: s.history.begin, onEnd: s.history.end, onChange: n => update(cell => cell && n !== cell[key] ? { ...cell, [key]: n } : cell),
          }} />)}</div> : <p className="caption">No hits selected. Add hits to edit their parameters.</p>}
          {fields.some(key => cells.some(cell => cell[key] !== cells[0][key])) && <p className="caption">Mixed values. Editing applies to every selected hit.</p>}
        </div>
        {cells.length < context.indices.length && <Button onClick={() => update(cell => cell ?? { ...baseStep })}>Add hits to selection</Button>}
        {!!cells.length && <Button onClick={() => update(() => null)}>Clear selected hits</Button>}
      </>;
    })()}
    {context.kind === 'song' && (context.bar >= s.history.value.songLength ? previewMissing('This bar is outside the current song length.') : (() => {
      const bar = s.history.value.song[context.bar];
      return <>{heading(context.bar, `Bar ${numberTag(context.bar + 1)}`, `${patternName(bar.source)} · ${bar.override ? 'Independent' : 'Linked'}`)}<Notice>{bar.override ? 'This bar has its own pattern.' : 'Follows the source until its first independent edit.'}</Notice><Button className="open-sound" onClick={() => { s.selectBar(context.bar); s.open('song'); }}>Open Song Arranger <ArrowUpRight size={13} aria-hidden="true" /></Button></>;
    })())}
    <div className="inspector-follow inline"><i className="status-dot" aria-hidden="true" /><span>{isPinned ? 'Pinned to this object' : 'Following selection'}</span></div>
  </div>;
}
export type GeneratorConfig = { style: typeof styles[number]; draft: DraftSettings };
export const generatorConfig: GeneratorConfig = { style: styles[0], draft: draftDefaults };
export function GeneratorPanel({ session: s, config, change }: { session: Session; config: GeneratorConfig; change: (next: GeneratorConfig) => void }) {
  const p = getPattern(s.history.value, s.activeRef);
  const locks = p ? p.laneLocks.filter(Boolean).length + p.stepLocks.reduce((n, lane) => n + lane.filter(Boolean).length, 0) : 0;
  return <div className="panel-body scroll-body stack generator-panel">
    <div className="generator-inputs"><Select label="Musical style" value={config.style} onChange={e => change({ ...config, style: e.target.value as GeneratorConfig['style'] })}>{styles.map(style => <option key={style}>{style}</option>)}</Select>
    <Field label="Seed"><input type="number" min={1} max={999999} value={config.draft.seed} onChange={e => change({ ...config, draft: { ...config.draft, seed: Math.min(999999, Math.max(1, +e.target.value || 1)) } })} /></Field></div>
    <Button disabled={!p} variant="primary" onClick={() => { s.editActive(old => draftGrid(old, config.draft)); s.say('Generic test groove applied. Undo restores the previous grid.'); }}><WandSparkles size={13} aria-hidden="true" /> Apply test groove</Button>
    <div className="dial-grid">{generatorAmounts.map(amount => <Knob key={amount} size="small" binding={{ spec: linear(amount,amount,0,100,draftDefaults[amount],1,'%'), value: config.draft[amount], onChange: n => change({ ...config, draft: { ...config.draft, [amount]: n } }) }} />)}</div>
    <span className="caption">{locks} generator locks · {s.activeRef.kind === 'source' ? patternName(s.activeRef) : `Bar ${s.activeRef.bar + 1}`} · reversible</span>
    <Notice>Genre engine not connected. The 23 styles are reference names; the test groove uses amounts and locks only.</Notice>
  </div>;
}
export type CatalogGroup = 'kits'|'pads'|'patterns'|'songs';
export type LibraryItem = { name: string; group: CatalogGroup; project: DrumProject };
export function LibraryPanel({ session: s, saved, save }: { session: Session; saved: LibraryItem[]; save: (item: LibraryItem) => void }) {
  const [group, setGroup] = useState<CatalogGroup>('kits');
  const [user, setUser] = useState(false);
  const [name, setName] = useState('');
  const essentials = ['Motion / A', 'Afterbeat / A', 'Offset / B'];
  const names = user ? saved.filter(x => x.group === group).map(x => x.name) : group === 'kits' ? [...kitNames] : group === 'songs' ? ['Motion · 16 bars'] : group === 'pads' ? ['Reset selected synth voice'] : essentials;
  function load(label: string, index: number) {
    const demo = initialProject(); const pad = s.selectedPad;
    const from = saved.find(item => item.group === group && item.name === label)?.project;
    s.history.edit(old => {
      if (group === 'kits') return { ...old, kit: label, pads: user && from ? from.pads : createKit(label), kitLevel: user && from ? from.kitLevel : demo.kitLevel };
      if (group === 'pads') return { ...old, pads: old.pads.map((p, i) => i === pad ? (user && from ? from.pads[pad] : demo.pads[pad]) : p) };
      if (group === 'patterns') return editPattern(old, s.activeRef, () => user && from ? from.patterns[0].A : demo.patterns[index][index === 2 ? 'B' : 'A']);
      return { ...old, song: user && from ? from.song : demo.song, songLength: user && from ? from.songLength : demo.songLength, patterns: user && from ? from.patterns : demo.patterns };
    });
    s.say(`Loaded ${label}. Undo reverses the load.`);
  }
  function saveCurrent() {
    const label = name.trim().slice(0, 30); if (!label) return;
    const project = s.history.value;
    const active = getPattern(project, s.activeRef);
    save({ name: label, group, project: group === 'patterns' && active ? { ...project, patterns: [{ ...project.patterns[0], A: active }, ...project.patterns.slice(1)] } : project });
    setName(''); s.say(`Saved ${label} for this session.`);
  }
  return <div className="panel-body scroll-body stack">
    <Select label="Library content" value={group} onChange={e => setGroup(e.target.value as CatalogGroup)}><option value="kits">Kits</option><option value="pads">Pads</option><option value="patterns">Patterns</option><option value="songs">Songs</option></Select>
    <Segmented label="Library collection" value={user ? 'user' : 'demo'} choices={[{ value: 'demo', label: 'Browser kit' }, { value: 'user', label: 'Session' }]} onChange={value => setUser(value === 'user')} />
    <div className="library-list">{names.map((label, i) => <Button key={label} onClick={() => load(label, i)}>{label} <ArrowUpRight size={12} aria-hidden="true" /></Button>)}{names.length === 0 && <span className="caption">Nothing saved in this category.</span>}</div>
    {user && <><Field label="Snapshot name"><input value={name} maxLength={30} onChange={e => setName(e.target.value)} /></Field><Button disabled={!name.trim()} onClick={saveCurrent}>Save current</Button></>}
    <Notice>Local browser snapshots, not native factory presets. Session saves do not survive reload.</Notice>
  </div>;
}
export function Outputs({ value, onChange, label = 'Pad output', hideLabel }: { value: number; onChange: (out: number) => void; label?: string; hideLabel?: boolean }) {
  return <Select label={label} value={value} hideLabel={hideLabel} onChange={e => onChange(+e.target.value)}>{Array.from({ length: 9 }, (_, out) => <option key={out} value={out}>{outputName(out)}</option>)}</Select>;
}
export function RoutingPanel({ session: s }: { session: Session }) {
  const i = s.selectedPad; const p = s.history.value.pads[i];
  return <div className="panel-body scroll-body stack"><Select label="Route pad" value={i} onChange={e => s.selectPad(+e.target.value)}>{s.history.value.pads.map((value, pad) => <option value={pad} key={pad}>{numberTag(pad + 1)} · {value.name}</option>)}</Select>
    <div className="inline"><span className="eyebrow">MIDI note</span><NumericValue binding={{ spec: linear('note','MIDI note',0,127,36 + i,1), value: p.note, onChange: value => s.setPad(i, old => ({ ...old, note: value })) }} /><span className="caption">{midiNote(p.note)}</span></div>
    <Outputs value={p.out} onChange={out => s.setPad(i, old => ({ ...old, out }))} />
    <Notice>Main and Out 2–9 are distinct browser buses monitored on the same device. Native outputs and Host MIDI are not connected.</Notice>
    <div className="bus-list">{s.preview.levels.buses.map((value, out) => <div className="bus-status" key={out}><span>{outputName(out)}</span><Meter value={value} label={`${outputName(out)} local bus peak`} orientation="horizontal" /></div>)}</div>
  </div>;
}
export function DiagnosticsPanel({ session: s }: { session: Session }) {
  const [rms, setRms] = useState(false);
  const spec = linear('poly','Preview polyphony',8,64,48,1,'voices');
  const { levels } = s.preview;
  return <div className="panel-body scroll-body stack"><Badge tone="accent" dot>WEB AUDIO · {s.preview.state.toUpperCase()}</Badge>
    <div className="inline"><span className="eyebrow">Active browser voices</span><strong className="mono">{levels.voices}</strong></div>
    <Switch checked={rms} onChange={setRms} label="Show Main RMS" />
    <Meter value={rms ? levels.rms : Math.max(...levels.main)} label={rms ? 'Main pre-device RMS' : 'Main pre-device peak'} kind={rms ? 'rms' : 'peak'} orientation="horizontal" showValue />
    <Knob size="small" binding={{ spec, value: s.history.value.polyphony, onBegin: s.history.begin, onEnd: s.history.end, onChange: n => s.history.edit(old => ({ ...old, polyphony: quantize(spec, n) })) }} />
    <Button onClick={s.preview.clearOver} disabled={!levels.over}>Clear Main OVER latch</Button><Button variant="danger" onClick={s.preview.panic}>Panic</Button>
    <Notice>OVER means Main reached ≥0 dBFS in a browser snapshot. It is not confirmed device or DAW clipping.</Notice>
    <Button onClick={async () => { try { await navigator.clipboard.writeText(`VOX Drums browser preview\nAudio: ${s.preview.state}\nActive preview voices: ${levels.voices}\nMain pre-device: ${peakText(Math.max(...levels.main))}\nNative host MIDI: not connected`); s.say('Browser diagnostics copied.'); } catch { s.say('Browser clipboard permission was unavailable.'); } }}><Copy size={13} aria-hidden="true" /> Copy diagnostics</Button>
  </div>;
}
const feedbackSpec = { ...linear('demo.delay.feedback','Delay Feedback',0,.85,.28,.01), format: (value: number) => `${Math.round(value * 100)}%` };
const kitLevelSpec = { ...linear('browser-kit-level','Kit Level',0,1,.78,.001), keyStep: .025, format: (n: number) => `${Math.round(n * 100)}%` };
export function Mixer({ session: s, mode, setMode, bank, setBank, height, bpm }: { session: Session; mode: MixerMode; setMode: (mode: MixerMode) => void; bank: number; setBank: (bank: number) => void; height: number; bpm: number }) {
  const data = s.history.value; const y = s.preview.levels;
  const expanded = mode === 'expanded';
  const bankRef = useRef<HTMLDivElement>(null);
  useLayoutEffect(() => {
    if (!expanded || !bankRef.current) return;
    const target = bankRef.current.querySelector<HTMLElement>(`[data-channel="${bank * 8 + 1}"]`);
    if (target) bankRef.current.scrollLeft += target.getBoundingClientRect().left - bankRef.current.getBoundingClientRect().left - 4;
  }, [expanded, bank]);
  const visible = expanded ? data.pads.map((_, i) => i) : Array.from({ length: 8 }, (_, i) => bank * 8 + i);
  const mixChoices = [{ value: 'collapsed' as const, label: 'Collapsed' }, { value: 'compact' as const, label: 'Compact' }, { value: 'expanded' as const, label: 'Expanded' }];
  const outputs = Math.max(...y.main);
  return <section className="ds-panel mixer" aria-label="Mixer Dock" data-mixer={mode} style={{ '--mix-height': `${height}px` } as CSSProperties}>
    <header className="panel-header mixer-header"><AudioLines size={15} aria-hidden="true" /><h2>Mixer</h2><span className="mixer-meta eyebrow">16 channels</span><div className="spacer" />
      {mode !== 'collapsed' && <Segmented label="Mixer bank" value={bank} choices={[{ value: 0, label: '1–8' }, { value: 1, label: '9–16' }]} onChange={setBank} />}
      {mode === 'collapsed' && <div className="collapsed-peak inline"><span className="eyebrow">Main</span><Meter value={outputs} label="Main pre-device peak" orientation="horizontal" showValue /></div>}
      <Segmented value={mode} choices={mixChoices} onChange={setMode} label="Mixer mode" />
    </header>
    {mode !== 'collapsed' && <div className="mixer-body">
      <div className="channel-bank" ref={bankRef} data-expanded={expanded}><div className="channel-rows" style={{ '--channel-count': visible.length } as CSSProperties}>{visible.map(i => {
        const p = data.pads[i]; const name = (key: PadParameter) => s.bind(i, key, `Mixer ${p.name}`);
        return <article key={i} className="channel-strip" data-selected={s.selectedPad === i} data-channel={i + 1} style={{ '--track-color': padColor(i) } as CSSProperties}>
          <button type="button" className="channel-title" onClick={() => s.selectPad(i)} aria-pressed={s.selectedPad === i} title={`Select ${p.name} without playing`}><i className="channel-dot" aria-hidden="true" /><span className="sr-only">Select </span><span>{p.name}</span><small>{numberTag(i + 1)}</small></button>
          <div className="channel-controls"><div className="fader-and-peak"><Fader binding={name('level')} compact={!expanded} showLabel={false} /><Meter value={y.pads[i]} label={`${p.name} mixer peak`} /></div>
            <div className="channel-rotary"><div><Knob variant="bipolar" size={expanded ? 'small' : 'micro'} binding={name('pan')} showLabel={false} />{expanded && <span>Pan</span>}</div>{expanded && <><div><Knob size="small" binding={name('send1')} intensity showLabel={false} /><span>Delay Send</span></div><div><Knob size="small" binding={name('send2')} intensity showLabel={false} /><span>Verb Send</span></div></>}</div>
          </div>
          <div className="channel-actions"><MuteSolo pad={i} session={s} context="Mixer" compact />{expanded && <Outputs label={`${p.name} mix output`} hideLabel value={p.out} onChange={out => s.setPad(i, old => ({ ...old, out }))} />}</div>
        </article>;
      })}</div></div>
      <div className="mixer-monitor"><div className="monitor-head inline"><Headphones size={13} aria-hidden="true" /><span>Main</span><div className="spacer"/><span className="eyebrow">L / R</span></div>
        <div className="monitor-core"><div className="monitor-fader"><Fader binding={{ spec: kitLevelSpec, value: data.kitLevel, onBegin: s.history.begin, onEnd: s.history.end, onChange: n => s.history.edit(old => n === old.kitLevel ? old : { ...old, kitLevel: n }) }} compact={!expanded} showLabel={false} /><div><strong>Kit Level</strong><span>All outputs</span></div></div>
          <div className="main-meters"><div className="pair"><Meter value={y.main[0]} label="Main left pre-device peak" over={y.over} /><Meter value={y.main[1]} label="Main right pre-device peak" over={y.over} /></div><span className="mono caption">{peakText(outputs)}</span></div>
        </div>
        {y.over && <Button className="sm over-status" onClick={s.preview.clearOver} title="Reset Main pre-device OVER; not proof of clipping">OVER ≥0 dBFS · Reset</Button>}
        {expanded && <div className="return-list"><span className="eyebrow">FX returns · to Main</span>{(['delayReturn','reverbReturn'] as const).map((key, n) => <div className="return-row" key={key}><Knob binding={{ spec: { ...linear(key,n ? 'Reverb Return' : 'Delay Return',0,1,n ? .52 : .55,.001), format: x => `${Math.round(x * 100)}%` }, value: data[key], onBegin: s.history.begin, onEnd: s.history.end, onChange: value => s.history.edit(old => value === old[key] ? old : { ...old, [key]: value }) }} size="micro" showLabel={false} /><div className="return-signal"><span>{n ? 'Reverb Return' : 'Delay Return'}</span><Meter value={y.returns[n]} label={n ? 'Reverb local return peak' : 'Delay local return peak'} orientation="horizontal" /></div></div>)}
          <details className="instrument-details"><summary>Delay settings</summary><div className="stack">
            <Select label="Delay division" value={data.delayDivision} onChange={e => { const delayDivision = e.target.value as DelayDivision; s.history.edit(old => old.delayDivision === delayDivision ? old : { ...old, delayDivision }); }}>
              {Object.keys(delayDivisions).map(value => <option key={value}>{value}</option>)}
            </Select>
            <span className="caption">{Math.round(delaySeconds(data.delayDivision, bpm) * 1000)} ms · local tempo</span>
            <Slider binding={{ spec: feedbackSpec, value: data.delayFeedback, onBegin: s.history.begin, onEnd: s.history.end, onChange: delayFeedback => s.history.edit(old => old.delayFeedback === delayFeedback ? old : { ...old, delayFeedback }) }} />
          </div></details>
        </div>}
      </div>
    </div>}
  </section>;
}
