import { useRef, useState } from 'react';
import type { CSSProperties } from 'react';
import { ArrowDownToLine, ArrowUpRight, AudioWaveform, ChevronRight, Clipboard, Copy, FileAudio, Lock, LockOpen, Music2, Pencil, Volume2 } from 'lucide-react';
import { Knob, RangeSlider, Slider } from '../design-system/controls';
import { Envelope, FilterResponse, Waveform } from '../design-system/graph';
import { linear, quantize, toNormalized, toPlain } from '../design-system/parameters';
import { midiNote, numberTag, SongTimeline, StepGrid } from '../design-system/perform';
import { Badge, Button, IconButton, Notice, Segmented, Select, Toggle } from '../design-system/ui';
import { downloadBlob } from '../shared/storage';
import { baseStep, createPattern, editBar, editSteps, getPattern, lengths, outputName, padColor, patternName, patternRefEqual, stepSpecs } from './model';
import type { Length, Pattern, PatternKey, Sample, Source, Step, Variation } from './model';
import { patternTools, replacePattern, transformPattern } from './pattern-tools';
import { donorSamples } from './donor-samples';
import { canNormalize, donorSample, samplePeakMultiplier } from './samples';
import { buildMidi } from './sequencer';
import type { Session } from './session';

export const lengthChoices = lengths.map(value => ({ value, label: `${value}` }));
export const variations = [{ value: 'A' as const, label: 'A' }, { value: 'B' as const, label: 'B' }];
export const paramFields = ['velocity','probability','ratchet','micro'] as const;
export const paramChoices = paramFields.map(value => ({ value, label: stepSpecs[value].label }));
export function PatternSelector({ value, onChange, label = 'Pattern slot' }: { value: number; onChange: (value: number) => void; label?: string }) {
  return <Select label={label} hideLabel value={value} onChange={event => onChange(+event.target.value)}>{['Motion', 'Afterbeat', 'Offset', 'Empty'].map((name, i) => <option key={name} value={i}>P{numberTag(i + 1)} · {name}</option>)}</Select>;
}
export function StepEditor({ session: s, patternKey, setPatternKey, bpm, clipboard }: { session: Session; patternKey: PatternKey; setPatternKey: (key: PatternKey) => void; bpm: number; clipboard: React.MutableRefObject<Pattern | null> }) {
  const [field, setField] = useState<keyof Step>('velocity');
  const [showLocks, setShowLocks] = useState(false);
  const [, refreshClipboard] = useState(0);
  const laneDrag = useRef<{ pointer: number; y: number; start: number; live: boolean } | null>(null);
  const laneClickConsumed = useRef(false);
  const p = getPattern(s.history.value, s.activeRef);
  if (!p) return <div className="panel-body"><Notice>The pattern is unavailable. Select a source or a Song bar.</Notice></div>;
  const pad = s.selectedPad;
  const selected = s.context.kind === 'steps' && patternRefEqual(s.context.target, s.activeRef) ? s.context : null;
  const bar = s.mode === 'song' ? s.history.value.song[s.selectedBar] : null;
  const tag = s.activeRef.kind === 'source' ? patternName(s.activeRef) : `BAR ${numberTag(s.activeRef.bar + 1)}`;
  const playing = s.preview.position;
  const playhead = playing && (s.mode === 'pattern' && playing.mode === 'pattern' || s.mode === 'song' && playing.bar === s.selectedBar) ? playing.step : -1;
  return <div className="step-editor editor-content">
    <div className="editor-toolbar inline wrap">
      {bar ? <Badge tone="accent">{tag} · {s.editMode === 'source' ? 'SOURCE' : bar.override ? 'INDEPENDENT' : 'LINKED UNTIL EDIT'}</Badge> : <PatternSelector value={patternKey.slot} onChange={slot => setPatternKey({ ...patternKey, slot })} />}
      {!bar && <Segmented label="Pattern variation" value={patternKey.variation} choices={variations} onChange={variation => setPatternKey({ ...patternKey, variation })} />}
      <div className="spacer" />
      <span className="eyebrow">Steps</span><Segmented label="Steps per bar" value={p.length} choices={lengthChoices} onChange={length => s.editActive(old => old.length === length ? old : { ...old, length })} />
      <IconButton label="Copy pattern" onClick={() => { clipboard.current = p; refreshClipboard(i => i + 1); s.say('Pattern copied to the session clipboard.'); }}><Copy size={13} /></IconButton>
      <IconButton label="Paste pattern" disabled={!clipboard.current} onClick={() => { const copy = clipboard.current; if (copy) s.editActive(old => replacePattern(old, copy)); }}><Clipboard size={13} /></IconButton>
      <Select label="Pattern tools" hideLabel title="Edit the current source or Song bar" value="" onChange={e => {
        const tool = patternTools.find(item => item.value === e.target.value);
        if (tool) s.editActive(old => transformPattern(old, tool.value, pad));
      }}><option value="">Pattern tools…</option>{patternTools.map(tool => <option key={tool.value} value={tool.value}>{tool.label}</option>)}</Select>
      <IconButton label="Download pattern MIDI (browser file)" onClick={() => downloadBlob('vox-pattern.mid', new Blob([buildMidi(s.history.value, { mode: 'pattern', ref: s.activeRef }, bpm)], { type: 'audio/midi' }))}><ArrowDownToLine size={14} /></IconButton>
    </div>
    <div className="sequence-inner">
      <StepGrid label="16-track step sequencer" rows={s.history.value.pads.map((item, row) => ({ name: item.name, colour: padColor(row), selected: row === pad }))} steps={p.length} playhead={playhead}
        getCell={(row, step) => ({ on: !!p.tracks[row][step], strength: (p.tracks[row][step]?.velocity ?? 0) / 127, selected: selected?.pad === row && selected.indices.includes(step), locked: p.stepLocks[row][step] })}
        onRow={s.selectPad} onInspect={(row, step, extend) => s.selectSteps(row, step, extend)}
        onStep={(row, step, event) => {
          if (showLocks) { s.editActive(old => ({ ...old, stepLocks: old.stepLocks.map((lane, i) => i === row ? lane.map((value, x) => x === step ? !value : value) : lane) })); return; }
          s.selectSteps(row, step, event.shiftKey, event.ctrlKey || event.metaKey);
          if (!event.shiftKey && !event.altKey && !event.ctrlKey && !event.metaKey) s.editActive(old => editSteps(old, row, [step], hit => hit ? null : { ...baseStep }));
        }} rowActions={showLocks ? row => <button type="button" className="lane-lock" aria-label={`Lock ${s.history.value.pads[row].name} lane for generator`} aria-pressed={p.laneLocks[row]} onClick={() => s.editActive(old => ({ ...old, laneLocks: old.laneLocks.map((value, i) => i === row ? !value : value) }))}>{p.laneLocks[row] ? <Lock size={10} /> : <LockOpen size={10} />}</button> : undefined}
        lane={{ name: stepSpecs[field].label, cell: step => {
          const hit = p.tracks[pad][step]; const val = hit?.[field]; const n = val === undefined ? 0 : Math.max(.07, toNormalized(stepSpecs[field], val));
          return <button type="button" aria-label={`${stepSpecs[field].label}, step ${step + 1}${val === undefined ? ', no hit' : `: ${val}`}`} aria-pressed={selected?.pad === pad && selected.indices.includes(step)} title={val === undefined ? 'No hit' : String(val)}
            onPointerDown={event => {
              laneClickConsumed.current = false;
              if (!hit || event.button !== 0 || event.ctrlKey || event.metaKey) return;
              laneDrag.current = { pointer: event.pointerId, y: event.clientY, start: toNormalized(stepSpecs[field], hit[field]), live: false };
              event.currentTarget.setPointerCapture(event.pointerId);
            }} onPointerMove={event => {
              const drag = laneDrag.current;
              if (!drag || drag.pointer !== event.pointerId) return;
              const delta = drag.y - event.clientY;
              if (!drag.live && Math.abs(delta) < 3) return;
              if (!drag.live) { drag.live = true; s.history.begin(); s.selectSteps(pad, step, false); }
              const next = quantize(stepSpecs[field], toPlain(stepSpecs[field], drag.start + delta / (event.shiftKey ? 200 : 40)));
              s.editActive(old => editSteps(old,pad,[step],cell => cell && cell[field] !== next ? { ...cell, [field]: next } : cell));
            }} onLostPointerCapture={() => { if (laneDrag.current?.live) { laneClickConsumed.current = true; s.history.end(); } laneDrag.current = null; }}
            onClick={event => { if (!laneClickConsumed.current || event.detail === 0) s.selectSteps(pad, step, event.shiftKey, event.ctrlKey || event.metaKey); laneClickConsumed.current = false; }}><i style={{ height: `${n * 100}%`, background: hit ? field === 'velocity' ? `hsl(${116 - n * 98} 59% 64%)` : undefined : 'transparent' }} /></button>;
        } }} />
    </div>
    <div className="sequence-footer inline wrap">
      <span className="eyebrow">Lane</span><Select label="Parameter lane" hideLabel value={field} onChange={event => setField(event.target.value as keyof Step)}>{paramChoices.map(option => <option key={option.value} value={option.value}>{option.label}</option>)}</Select>
      <div className="spacer" /><span className="caption step-help">{showLocks ? 'Click a lane or step to protect it' : 'Drag lane values · Shift + click to select a range'}</span>
      <Toggle className="sm" checked={showLocks} onClick={() => setShowLocks(!showLocks)} title="Edit generator lane and step locks"><Lock size={11} aria-hidden="true" /> Locks</Toggle>
    </div>
  </div>;
}

export function SongEditor({ session: s, bpm, enterSong }: { session: Session; bpm: number; enterSong: () => void }) {
  const data = s.history.value;
  const bar = data.song[s.selectedBar];
  const changeLength = (length: Length) => s.history.edit(old => old.songLength === length ? old : { ...old, songLength: length });
  function changeSource(next: PatternKey) {
    s.history.edit(old => editBar(old, s.selectedBar, value => value.source.slot === next.slot && value.source.variation === next.variation ? value : { ...value, source: next }));
  }
  return <div className="arranger-editor editor-content">
    <div className="editor-toolbar inline wrap"><span className="eyebrow">Song length</span><Segmented label="Song length in bars" value={data.songLength} choices={lengthChoices} onChange={changeLength} />
      <Toggle checked={s.loopBar} onClick={() => s.setLoopBar(!s.loopBar)}>Loop selected bar</Toggle><div className="spacer" />
      <IconButton label="Download whole song MIDI (browser file)" onClick={() => downloadBlob('vox-song.mid', new Blob([buildMidi(data, { mode: 'song', loopBar: null }, bpm)], { type: 'audio/midi' }))}><ArrowDownToLine size={14} /></IconButton>
    </div>
    <div className="arranger-target inline wrap"><Badge tone={bar.override ? 'good' : 'accent'}>BAR {numberTag(s.selectedBar + 1)} · {bar.override ? 'UNLINKED' : 'LINKED'}</Badge>
      <Segmented label="Song editing target" value={s.editMode} choices={[{ value: 'source', label: 'Edit Source Pattern' }, { value: 'bar', label: 'Edit This Bar' }]} onChange={mode => { s.setEditMode(mode); enterSong(); }} />
      <PatternSelector value={bar.source.slot} label="Song bar source slot" onChange={slot => changeSource({ ...bar.source, slot })} /><Segmented label="Song source variation" value={bar.source.variation} choices={variations} onChange={variation => changeSource({ ...bar.source, variation })} />
      {bar.override && <Button onClick={() => s.history.edit(old => editBar(old, s.selectedBar, value => ({ ...value, override: null })))}>Relink to source</Button>}
    </div>
    <div className="arranger-body">
      <SongTimeline count={data.songLength} selected={s.selectedBar} playing={s.preview.position?.bar ?? -1} details={i => { const item = data.song[i]; return { name: patternName(item.source), type: item.override ? 'Unique' : 'Linked', colour: ['#f3bd72','#8ed3bc','#a7b8ef','#c7a6e8'][item.source.slot] }; }} onSelect={i => { enterSong(); s.selectBar(i); }} />
    </div>
    <div className="sequence-footer inline wrap"><span className="caption">{s.editMode === 'source' ? 'Edits update every linked bar using this source.' : 'This bar gets its own copy on the first actual pattern edit.'}</span><div className="spacer" /><Button className="sm" onClick={() => { enterSong(); s.open('step'); }}><Pencil size={11} aria-hidden="true" /> Open steps</Button></div>
  </div>;
}

export function SoundEditor({ session: s }: { session: Session }) {
  const p = s.history.value.pads[s.selectedPad]; const i = s.selectedPad;
  return <div className="panel-body flow-editor scroll-body stack">
    <div className="editor-identity inline" style={{ '--track-color': padColor(i) } as CSSProperties}><span className="object-index">{numberTag(i + 1)}</span><div><h3>{p.name}</h3><span className="caption">{midiNote(p.note)} / {p.note} · Local audio path</span></div><div className="spacer" /><Button onClick={() => void s.preview.audition(i)}><Volume2 size={13} aria-hidden="true" /> Audition</Button></div>
    <div className="signal-flow">
      <div className="signal-module stack"><span className="eyebrow">01 / Source</span><Select label="Pad source" value={p.source} onChange={event => s.setPad(i, old => ({ ...old, source: event.target.value as Source }))}><option>Synth</option><option>Sample</option><option>Layer</option></Select><Button onClick={() => s.open('sample')}>Sample Editor <ArrowUpRight size={12} aria-hidden="true" /></Button>{p.source !== 'Synth' && !p.players[p.player].sample && <span className="caption tone-warning">Player {p.player} is empty.</span>}</div>
      <ChevronRight className="signal-arrow" size={14} aria-hidden="true" />
      <div className="signal-module stack"><span className="eyebrow">02 / Shape</span><div className="dial-grid"><Knob binding={s.bind(i, 'tune')} variant="bipolar"/><Knob binding={s.bind(i, 'decay')} /></div><Envelope decay={p.decay / (p.decay + .3)} label="Synth decay model, not a live waveform" /><span className="graph-note">Envelope model</span></div>
      <ChevronRight className="signal-arrow" size={14} aria-hidden="true" />
      <div className="signal-module stack"><span className="eyebrow">03 / Filter</span><div className="dial-grid"><Knob binding={s.bind(i, 'cutoff')} /><Knob binding={s.bind(i, 'drive')} intensity /></div><FilterResponse cutoff={p.cutoff} /><span className="graph-note">Filter model</span></div>
      <ChevronRight className="signal-arrow" size={14} aria-hidden="true" />
      <div className="signal-module stack"><span className="eyebrow">04 / Output</span><div className="dial-grid"><Knob binding={s.bind(i, 'level')} /><Knob binding={s.bind(i, 'pan')} variant="bipolar" /></div><span className="caption">{outputName(p.out)} · browser monitor</span></div>
    </div>
    <details className="instrument-details"><summary>Advanced voice settings</summary><div className="inline wrap"><Select label="Choke group" value={p.choke} onChange={e => s.setPad(i, old => ({ ...old, choke: +e.target.value }))}>{[0,1,2,3,4].map(group => <option key={group} value={group}>{group ? `Group ${group}` : 'Off'}</option>)}</Select><span className="caption">A hit releases earlier voices in its choke group. Sample trims and direction belong to the selected player.</span></div></details>
  </div>;
}

export function SampleEditor({ session: s }: { session: Session }) {
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [drag, setDrag] = useState(false);
  const input = useRef<HTMLInputElement>(null);
  const p = s.history.value.pads[s.selectedPad]; const i = s.selectedPad; const player = p.players[p.player]; const v = p.player;
  function loadSample(sample: Sample) {
    s.setPad(i, old => ({ ...old, source: old.source === 'Synth' ? 'Layer' : old.source, players: { ...old.players, [v]: { ...old.players[v], sample, start: 0, end: 1 } } }));
    s.say(`Loaded ${sample.name} into ${p.name} / Player ${v}.`);
  }
  async function importFile(file: File) {
    if (busy) return;
    setBusy(true); setError('');
    try {
      loadSample(await s.preview.decode(file));
    } catch (err) { setError(err instanceof Error && err.message ? err.message : 'Audio decode failed or format unsupported.'); }
    finally { setBusy(false); }
  }
  function trim(which: 'start' | 'end', value: number) {
    s.setPad(i, old => {
      const before = old.players[v]; const next = Math.round((which === 'start' ? Math.min(before.end - .005, Math.max(0, value)) : Math.max(before.start + .005, Math.min(1, value))) * 1000) / 1000;
      return next === before[which] ? old : { ...old, players: { ...old.players, [v]: { ...before, [which]: next } } };
    });
  }
  const onBegin = s.history.begin; const onEnd = s.history.end;
  const scale = samplePeakMultiplier(player.sample?.peak ?? 0, player.normalize);
  const peaks = player.sample?.peaks;
  const waveform = peaks && scale !== 1 ? peaks.map(([lo, hi]): [number, number] => [lo * scale, hi * scale]) : peaks;
  function togglePlayback(key: 'reverse' | 'normalize' | 'loop') {
    s.setPad(i, old => ({ ...old, players: { ...old.players, [v]: { ...old.players[v], [key]: !old.players[v][key] } } }));
  }
  return <div className="panel-body sample-editor scroll-body stack">
    <div className="inline wrap"><span className="eyebrow">{p.name} · Player</span><Segmented label="Sample player" value={v} choices={variations} onChange={(player: Variation) => s.setPad(i, old => ({ ...old, player }))} /><div className="spacer" />
      <Button onClick={() => input.current?.click()} disabled={busy}><FileAudio size={13} aria-hidden="true" /> {busy ? 'Decoding…' : 'Import audio'}</Button>
      <input ref={input} aria-label="Import pad sample" className="sr-only" tabIndex={-1} type="file" accept="audio/*,.wav,.aif,.aiff,.flac,.mp3,.ogg" onChange={e => { const file = e.target.files?.[0]; if (file) void importFile(file); e.target.value = ''; }} />
      <Button onClick={() => void s.preview.audition(i)} disabled={!player.sample}><Volume2 size={13} aria-hidden="true" /> Audition pad</Button>
    </div>
    <Select label="Procedural demo sample" value={player.sample?.donorId ?? ''} disabled={busy} onChange={e => {
      const item = donorSamples.find(demo => demo.id === e.target.value);
      if (!item) return;
      setError('');
      try { const next = donorSample(item.id, item.name); if (next) loadSample(next); }
      catch (err) { setError(err instanceof Error ? err.message : 'The demo sample could not be prepared.'); }
    }}><option value="" disabled>Choose a browser demo tone…</option>{donorSamples.map(item => <option key={item.id} value={item.id}>{item.name}</option>)}</Select>
    <div className="sample-drop" data-drag={drag} onDragOver={e => { if (e.dataTransfer.types.includes('Files')) { e.preventDefault(); setDrag(true); } }} onDragLeave={e => { if (!e.currentTarget.contains(e.relatedTarget as Node | null)) setDrag(false); }} onDrop={e => { e.preventDefault(); setDrag(false); const file = e.dataTransfer.files[0]; if (file) void importFile(file); }}>
      {waveform ? <Waveform peaks={waveform} start={player.start} end={player.end} onTrim={trim} onBegin={onBegin} onEnd={onEnd} /> : <div className="empty-wave"><AudioWaveform size={27} aria-hidden="true" /><strong>Player {v} is empty</strong><span>Choose a demo tone, drop a local file or use Import audio.</span></div>}
    </div>
    {error && <Notice tone="error">{error}</Notice>}
    {player.sample && <><div className="inline wrap"><Badge>{(player.sample.buffer.duration).toFixed(2)} s</Badge>{player.sample.donorId && <Badge>BROWSER DEMO</Badge>}<span className="sample-filename">{player.sample.name}</span><div className="spacer" />
      <Toggle checked={player.reverse} onClick={() => togglePlayback('reverse')}>Reverse</Toggle>
      <Toggle checked={player.normalize && canNormalize(player.sample.peak)} disabled={!canNormalize(player.sample.peak)} title="Scale the full source peak to unity before trim and player gain" onClick={() => togglePlayback('normalize')}>Normalize peak</Toggle>
      <Toggle checked={player.loop} title="Repeat the trimmed region for a bounded 600 ms preview hit" onClick={() => togglePlayback('loop')}>Loop · 600 ms</Toggle>
      <Button onClick={() => s.setPad(i, old => ({ ...old, players: { ...old.players, [v]: { ...old.players[v], sample: null } } }))}>Remove sample</Button></div>
      <RangeSlider gap={.005} start={{ spec: linear('sample-start','Sample start',0,1,0,.001), value: player.start, onChange: n => trim('start', n), onBegin, onEnd }} end={{ spec: linear('sample-end','Sample end',0,1,1,.001), value: player.end, onChange: n => trim('end', n), onBegin, onEnd }} />
      <Slider binding={{ spec: linear('sample-gain','Sample gain',0,1,1,.001), value: player.gain, onChange: value => s.setPad(i, old => ({ ...old, players: { ...old.players, [v]: { ...old.players[v], gain: value } } })), onBegin, onEnd }} /></>}
    <p className="caption"><Music2 size={11} aria-hidden="true" /> Samples stay in this browser session. Export to a DAW by dropping here is not supported.</p>
  </div>;
}
export { createPattern };
