import { useEffect, useId, useLayoutEffect, useRef, useState } from 'react';
import type { CSSProperties, DragEvent, ReactNode } from 'react';
import { Activity, AudioLines, ChevronDown, CircleStop, Columns2, Grip, Grid3X3, LayoutDashboard, Maximize2, Minimize2, PanelTop, Pin, PinOff, Play, Plus, Redo2, RotateCcw, Rows2, Square, Undo2, X } from 'lucide-react';
import { Button, IconButton, Notice, Segmented, Select, Splitter } from '../design-system/ui';
import { numberTag } from '../design-system/perform';
import { useHistory } from '../shared/history';
import { readLocal, writeLocal } from '../shared/storage';
import { SampleEditor, SongEditor, SoundEditor, StepEditor } from './editors';
import { bounds, dockNames, docks, editorNames, editors, moveDock, parseLayout, presetNames, showEditor, workspace } from './layout';
import type { Dock, Editor, Layout, PresetName, Zone } from './layout';
import { createKit, drumSpec, editPad, editPattern, initialProject, kitNames, patternRefEqual, sourceRef } from './model';
import type { Context, Pattern, PatternKey, PatternRef } from './model';
import { DiagnosticsPanel, GeneratorPanel, generatorConfig, Inspector, LibraryPanel, Mixer, PadRack, RoutingPanel } from './panels';
import type { LibraryItem } from './panels';
import { usePreview } from './preview';
import type { EditMode, Session } from './session';
import type { Target } from './sequencer';
import './drums.css';

const currentKey = 'vox.drums.workspace.v1';
const layoutsKey = 'vox.drums.presets.v1';
const dockMime = 'application/x-vox-dock';
const caps = { left: 'Left', right: 'Right' } as const;
const builtIn = (name: string): name is PresetName => (presetNames as readonly string[]).includes(name);
function readPresets(): Layout[] {
  const raw = readLocal(layoutsKey);
  if (!Array.isArray(raw)) return [];
  const checked = raw.slice(0, 12).map(parseLayout).filter(item => !builtIn(item.preset));
  return checked.filter((item, index) => checked.findIndex(x => x.preset === item.preset) === index);
}
function useSize() {
  const ref = useRef<HTMLDivElement>(null);
  const [size, set] = useState({ width: 0, height: 0 });
  useLayoutEffect(() => {
    if (!ref.current) return;
    const target = ref.current;
    const update = () => {
      const { width, height } = target.getBoundingClientRect();
      set(old => old.width === Math.round(width) && old.height === Math.round(height) ? old : { width: Math.round(width), height: Math.round(height) });
    };
    const resize = new ResizeObserver(update); resize.observe(target); update();
    return () => resize.disconnect();
  }, []);
  return { ref, ...size };
}
const modes = [{ value: 'pattern' as const, label: 'Pattern' }, { value: 'song' as const, label: 'Song' }];

export function DrumsPrototype({ active }: { active: boolean }) {
  const history = useHistory(initialProject);
  const [layout, setLayout] = useState(() => parseLayout(readLocal(currentKey)));
  const [saved, setSaved] = useState(readPresets);
  const [workspaceName, setWorkspaceName] = useState('');
  const [layoutPersisted, setLayoutPersisted] = useState(true);
  const [kitLibrary, setKitLibrary] = useState<LibraryItem[]>([]);
  const [generator, setGenerator] = useState(generatorConfig);
  const [selectedPad, setSelectedPad] = useState(0);
  const [rawBar, setBar] = useState(0);
  const [context, setContext] = useState<Context>({ kind: 'pad', pad: 0 });
  const [key, setKey] = useState<PatternKey>({ slot: 0, variation: 'A' });
  const [mode, setMode] = useState<'pattern'|'song'>('pattern');
  const [editMode, setEditing] = useState<EditMode>('source');
  const [loopBar, setLoopBar] = useState(false);
  const [bpm, setBpm] = useState(128);
  const [tempoText, setTempoText] = useState('128');
  const [shortView, setShortView] = useState(() => matchMedia('(max-width: 1100px) and (max-height: 730px)').matches);
  useEffect(() => { const m = matchMedia('(max-width: 1100px) and (max-height: 730px)'); const sync = () => setShortView(m.matches); m.addEventListener('change', sync); return () => m.removeEventListener('change', sync); }, []);
  const [bank, setBank] = useState(0);
  const [focusPane, setFocusPane] = useState<'primary'|'secondary'>('primary');
  const [maximized, setMaximized] = useState<'primary'|'secondary'|null>(null);
  const [moving, setMoving] = useState<Dock | null>(null);
  const [over, setOver] = useState<Zone | null>(null);
  const [status, say] = useState('');
  const clipboard = useRef<Pattern | null>(null);
  const selectionAnchor = useRef<{ pad: number; ref: PatternRef; step: number } | null>(null);
  const layoutMenu = useRef<HTMLDetailsElement>(null);
  const panelMenu = useRef<HTMLDetailsElement>(null);
  const rootId = useId();
  const central = useSize();
  const room = useSize();
  const selectedBar = Math.min(rawBar, history.value.songLength - 1);
  const songBar = history.value.song[selectedBar];
  const activeRef = mode === 'pattern' ? sourceRef(key) : editMode === 'source' ? sourceRef(songBar.source) : { kind: 'bar' as const, bar: selectedBar };
  const target: Target = mode === 'pattern' ? { mode: 'pattern', ref: sourceRef(key) } : { mode: 'song', loopBar: loopBar ? selectedBar : null };
  const preview = usePreview(history.current, history.value, target, bpm);
  const previewRef = useRef(preview); previewRef.current = preview;
  useEffect(() => { if (!active) previewRef.current.panic(); }, [active]);
  useEffect(() => { setLayoutPersisted(writeLocal(currentKey, layout)); }, [layout]);
  useEffect(() => { if (status) { const t = setTimeout(() => say(''), 4300); return () => clearTimeout(t); } }, [status]);
  useEffect(() => {
    if (!active) return;
    const handle = (event: KeyboardEvent) => {
      if (event.key === 'Escape') { layoutMenu.current?.removeAttribute('open'); panelMenu.current?.removeAttribute('open'); return; }
      if (!(event.target instanceof HTMLElement) || event.target.closest('input,textarea,[contenteditable]') || event.key.toLowerCase() !== 'z' || !(event.ctrlKey || event.metaKey)) return;
      event.preventDefault(); if (event.shiftKey) history.redo(); else history.undo();
    };
    window.addEventListener('keydown', handle); return () => window.removeEventListener('keydown', handle);
  }, [active, history.undo, history.redo]);
  function selectPad(pad: number) { setSelectedPad(pad); setContext({ kind: 'pad', pad }); selectionAnchor.current = null; }
  function selectBar(bar: number) { setBar(bar); setContext({ kind: 'song', bar }); selectionAnchor.current = null; }
  function selectSteps(pad: number, step: number, extend: boolean, toggle = false) {
    setSelectedPad(pad);
    const anchor = selectionAnchor.current;
    const keepAnchor = anchor && anchor.pad === pad && patternRefEqual(anchor.ref, activeRef);
    let indices = [step];
    if (extend && keepAnchor) indices = Array.from({ length: Math.abs(step - anchor.step) + 1 }, (_, n) => Math.min(step, anchor.step) + n);
    else if (toggle && context.kind === 'steps' && context.pad === pad && patternRefEqual(context.target, activeRef)) {
      if (context.indices.includes(step)) indices = context.indices.length > 1 ? context.indices.filter(n => n !== step) : [step];
      else indices = [...context.indices, step].sort((a,b) => a-b);
    }
    if (!extend || !keepAnchor) selectionAnchor.current = { pad, ref: activeRef, step };
    setContext({ kind: 'steps', pad, target: activeRef, indices });
  }
  function setDrumMode(next: 'pattern'|'song', focus = true) {
    if (mode !== next) { preview.stop(); setMode(next); if (next === 'song') setContext({ kind: 'song', bar: selectedBar }); else setContext({ kind: 'pad', pad: selectedPad }); }
    if (focus) open(next === 'song' ? 'song' : 'step');
  }
  function open(editor: Editor) {
    if (layout.secondary === editor) { setFocusPane('secondary'); if (maximized !== null) setMaximized('secondary'); return; }
    setFocusPane('primary'); if (maximized !== null) setMaximized('primary');
    setLayout(old => showEditor(old, editor, 'primary'));
  }
  const session: Session = {
    history, preview, selectedPad, selectedBar, context, selectPad, selectSteps, selectBar,
    activeRef, setEditMode: setEditing, editMode, mode, loopBar, setLoopBar, open, say,
    setPad: (pad, fn) => history.edit(old => editPad(old, pad, fn)),
    editActive: fn => history.edit(old => editPattern(old, activeRef, fn)),
    bind: (pad, field, prefix) => {
      const spec = drumSpec(pad, field);
      return { spec: prefix ? { ...spec, label: `${prefix} ${spec.label}` } : spec, value: history.value.pads[pad][field], onBegin: history.begin, onEnd: history.end, onChange: value => history.edit(old => editPad(old, pad, item => item[field] === value ? item : { ...item, [field]: value })) };
    },
  };
  function place(dock: Dock, zone: Zone | null) {
    history.end(); setLayout(old => moveDock(old, dock, zone)); setMoving(null); setOver(null);
    if (zone) say(`${dockNames[dock]} docked ${zone}.`); else say(`${dockNames[dock]} hidden. Restore it under Panels.`);
  }
  function restore(next: Layout) {
    history.end(); setLayout(parseLayout(next)); setFocusPane('primary'); setMaximized(null); setMoving(null); setOver(null);
    layoutMenu.current?.removeAttribute('open'); say(`${next.preset} workspace loaded.`);
  }
  function saveLayout() {
    const name = workspaceName.trim().slice(0, 34);
    if (!name || builtIn(name)) { say('Choose a name different from the built-in layouts.'); return; }
    const next = { ...layout, preset: name };
    const all = [...saved.filter(p => p.preset !== name), next].slice(-12);
    if (!writeLocal(layoutsKey, all)) { say('Local storage is unavailable. Workspace was not saved.'); return; }
    setSaved(all); setWorkspaceName(''); setLayout(next); layoutMenu.current?.removeAttribute('open'); say(`Workspace saved as ${name}.`);
  }
  const actualLayout = {
    left: Math.min(layout.leftWidth, room.width && room.width < 1130 ? 212 : room.width && room.width < 1350 ? 247 : 340),
    right: Math.min(layout.rightWidth, room.width && room.width < 1130 ? 187 : room.width && room.width < 1350 ? 216 : 340),
  };
  const sideVisible = (zone: Zone) => layout[zone].length > 0 || moving !== null;
  const fits = !!layout.secondary && (layout.split === 'side' ? central.width >= 706 : central.height >= 388);
  const showing: 'primary'|'secondary' = maximized === 'secondary' && layout.secondary ? 'secondary' : maximized ? 'primary' : focusPane === 'secondary' && layout.secondary ? 'secondary' : 'primary';
  const splitVisible = fits && maximized === null;
  const span = layout.split === 'side' ? central.width : central.height;
  const minimum = Math.min(48, Math.max(bounds.ratio[0], Math.ceil((layout.split === 'side' ? 302 : 150) * 100 / Math.max(span, 1))));
  const visibleRatio = Math.max(minimum, Math.min(100 - minimum, layout.ratio));
  function side(zone: Zone) {
    const dock = layout.active[zone];
    const canDrop = (event: DragEvent) => moving && event.dataTransfer.types.includes(dockMime);
    const drop = (event: DragEvent) => {
      if (!canDrop(event) || !moving || event.dataTransfer.getData(dockMime) !== moving) return;
      event.preventDefault(); event.stopPropagation(); place(moving, zone);
    };
    let contents: ReactNode = null;
    if (dock === 'pads') contents = <PadRack session={session} view={layout.padView} setView={padView => setLayout(old => ({ ...old, padView }))} />;
    else if (dock === 'inspector') contents = <Inspector session={session} pin={layout.pin} unpin={() => setLayout(old => ({ ...old, pin: null }))} />;
    else if (dock === 'generator') contents = <GeneratorPanel session={session} config={generator} change={setGenerator} />;
    else if (dock === 'library') contents = <LibraryPanel session={session} saved={kitLibrary} save={item => setKitLibrary(old => [...old.filter(p => p.name !== item.name || p.group !== item.group), item].slice(-16))} />;
    else if (dock === 'routing') contents = <RoutingPanel session={session} />;
    else if (dock === 'settings') contents = <DiagnosticsPanel session={session} />;
    const other = zone === 'left' ? 'right' : 'left';
    return <aside className={`dock-zone dock-${zone}`} aria-label={`${caps[zone]} dock`} data-zone={zone} hidden={!sideVisible(zone)}
      onDragOver={event => { if (canDrop(event)) { event.preventDefault(); event.dataTransfer.dropEffect = 'move'; setOver(zone); } }}
      onDragLeave={event => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setOver(null); }} onDrop={drop}>
      {dock && <section className="ds-panel dock-panel" aria-labelledby={`${rootId}-${dock}`} data-dock={dock}>
        <header className="panel-header dock-head">
          <IconButton className="dock-grip" label={`Move ${dockNames[dock]} to ${other} dock`}
            draggable onClick={() => place(dock, other)} onDragStart={event => { event.dataTransfer.setData(dockMime, dock); event.dataTransfer.effectAllowed = 'move'; setMoving(dock); }} onDragEnd={() => { setMoving(null); setOver(null); }}><Grip size={15} aria-hidden="true" /></IconButton>
          <h2 id={`${rootId}-${dock}`}>{dockNames[dock]}</h2><div className="spacer" />
          {dock === 'inspector' && <Button variant="ghost" className="pin-button" aria-pressed={!!layout.pin} aria-label={layout.pin ? 'Unpin Inspector' : 'Pin Inspector'} onClick={() => setLayout(old => ({ ...old, pin: old.pin ? null : context }))}>{layout.pin ? <PinOff size={12} aria-hidden="true" /> : <Pin size={12} aria-hidden="true" />} <span>{layout.pin ? 'Unpin' : 'Pin'}</span></Button>}
          <IconButton className="hide-dock" label={`Hide ${dockNames[dock]}`} onClick={() => place(dock, null)}><X size={13} aria-hidden="true" /></IconButton>
        </header>
        {layout[zone].length > 1 && <div className="dock-tabs"><Segmented label={`${caps[zone]} dock panels`} value={dock} choices={layout[zone].map(value => ({ value, label: dockNames[value] }))} onChange={next => setLayout(old => ({ ...old, active: { ...old.active, [zone]: next } }))} /></div>}
        {contents}
      </section>}
      {moving && <div className="dock-drop" data-dock-preview={over === zone} aria-hidden="true"><PanelTop size={22} /><strong>Dock {caps[zone]}</strong><small>{dock ? 'Add as a dock tab' : 'Place in empty zone'}</small></div>}
    </aside>;
  }
  function pane(editor: Editor, slot: 'primary'|'secondary') {
    const names = editors.map(value => ({ value, label: editorNames[value] }));
    const isPrimary = slot === 'primary';
    const hasSecond = layout.secondary !== null;
    let body: ReactNode;
    if (editor === 'step') body = <StepEditor session={session} patternKey={key} setPatternKey={next => { setKey(next); setContext({ kind: 'pad', pad: selectedPad }); }} bpm={bpm} clipboard={clipboard} />;
    else if (editor === 'song') body = <SongEditor session={session} bpm={bpm} enterSong={() => { if (mode !== 'song') { preview.stop(); setMode('song'); } }} />;
    else if (editor === 'sound') body = <SoundEditor session={session} />;
    else body = <SampleEditor session={session} />;
    return <section className="ds-panel center-panel" aria-label={editorNames[editor]} key={slot} hidden={!splitVisible && showing !== slot}>
      <header className="panel-header editor-header"><Grid3X3 size={15} aria-hidden="true" />
        <h2 className="sr-only">{editorNames[editor]}</h2>
        <Select label={isPrimary ? 'Primary editor' : 'Secondary editor'} className="editor-picker" hideLabel value={editor} onChange={event => setLayout(old => showEditor(old, event.target.value as Editor, slot))}>{names.filter(item => isPrimary || item.value !== layout.primary).map(item => <option value={item.value} key={item.value}>{item.label}</option>)}</Select>
        <div className="spacer" />
        {isPrimary && !hasSecond && <IconButton label="Add second editor" onClick={() => { setMaximized(null); setLayout(old => ({ ...old, secondary: old.primary === 'song' ? 'step' : 'song' })); }}><Plus size={15} aria-hidden="true" /></IconButton>}
        {hasSecond && isPrimary && <IconButton label={layout.split === 'side' ? 'Switch to stacked editors' : 'Switch to side-by-side editors'} onClick={() => setLayout(old => ({ ...old, split: old.split === 'side' ? 'stack' : 'side' }))}>{layout.split === 'side' ? <Rows2 size={14} aria-hidden="true" /> : <Columns2 size={14} aria-hidden="true" />}</IconButton>}
        <IconButton label={maximized === slot ? `Restore ${editorNames[editor]}` : `Maximize ${editorNames[editor]}`} onClick={() => setMaximized(maximized === slot ? null : slot)}>{maximized === slot ? <Minimize2 size={14} aria-hidden="true" /> : <Maximize2 size={14} aria-hidden="true" />}</IconButton>
        {!isPrimary && <IconButton label="Close second editor" onClick={() => { setLayout(old => ({ ...old, secondary: null })); setFocusPane('primary'); setMaximized(null); }}><X size={14} aria-hidden="true" /></IconButton>}
      </header>{body}
    </section>;
  }
  const mixerMode = layout.mixerAuto && shortView && layout.mixer === 'compact' ? 'collapsed' : layout.mixer;
  const bodyStyle = { '--left-width': `${sideVisible('left') && maximized === null ? actualLayout.left : 0}px`, '--right-width': `${sideVisible('right') && maximized === null ? actualLayout.right : 0}px`, '--left-gap': sideVisible('left') && maximized === null ? '12px' : '0px', '--right-gap': sideVisible('right') && maximized === null ? '12px' : '0px' } as CSSProperties;
  return <main className="drums instrument-page" id="drums-main" aria-labelledby="drums-title">
    <header className="product-intro">
      <div className="product-id"><div className="product-icon"><Grid3X3 size={24} strokeWidth={1.5} aria-hidden="true" /></div><div><p className="eyebrow">INSTRUMENT 01 <span>/</span> RHYTHM</p><h1 id="drums-title">VOX Drums</h1></div></div>
      <div className="instrument-actions inline wrap">
        <Select label="Kit" value={history.value.kit} onChange={e => { const kit = e.target.value; const custom = kitLibrary.find(p => p.group === 'kits' && p.name === kit); history.edit(old => ({ ...old, kit, pads: custom?.project.pads ?? createKit(kit), kitLevel: custom?.project.kitLevel ?? .78 })); }}>
          {[...new Set([...kitNames, ...kitLibrary.filter(x => x.group === 'kits').map(x => x.name), history.value.kit])].map(kit => <option key={kit}>{kit}</option>)}
        </Select>
        <details className="disclosure workspace-menu" ref={layoutMenu}><summary><span className="eyebrow">Workspace</span><span className="summary-value"><LayoutDashboard size={12} aria-hidden="true" /> {layout.preset} <ChevronDown size={12} aria-hidden="true" /></span></summary>
          <div className="ds-panel menu-sheet stack"><span className="eyebrow">Workspace layouts</span>{presetNames.map(name => <Button key={name} aria-pressed={layout.preset === name} onClick={() => restore(workspace(name))}>{name}</Button>)}
            {saved.length > 0 && <><div className="divider" /><span className="eyebrow">Saved locally</span>{saved.map(item => <Button key={item.preset} aria-pressed={layout.preset === item.preset} onClick={() => restore(item)}>{item.preset}</Button>)}</>}
            <div className="divider" /><label className="ds-field"><span className="eyebrow">Save workspace as</span><input maxLength={34} value={workspaceName} onChange={e => setWorkspaceName(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') saveLayout(); }} placeholder="My layout" /></label>
            <Button onClick={saveLayout} disabled={!workspaceName.trim()}>Save layout</Button><Button onClick={() => restore(workspace())}><RotateCcw size={12} aria-hidden="true" /> Reset Workspace</Button>
          </div>
        </details>
        <details className="disclosure panel-menu" ref={panelMenu}><summary><span className="summary-value"><Columns2 size={13} aria-hidden="true" /> Panels <ChevronDown size={12} aria-hidden="true" /></span></summary>
          <div className="ds-panel menu-sheet stack"><span className="eyebrow">Visible panels</span>{docks.map(dock => {
            const side = layout.left.includes(dock) ? 'left' : layout.right.includes(dock) ? 'right' : null;
            return <label className="menu-check" key={dock}><input type="checkbox" checked={!!side} onChange={e => place(dock, e.target.checked ? dock === 'pads' || dock === 'library' ? 'left' : 'right' : null)} /><span>{dockNames[dock]}</span>{side && <small>{side}</small>}</label>;
          })}<div className="divider" /><span className="caption">Drag a panel grip or click it to move to the opposite dock.</span></div>
        </details>
        <div className="instrument-history inline"><IconButton label="Undo musical edit" disabled={!history.canUndo} onClick={history.undo}><Undo2 size={16} /></IconButton><IconButton label="Redo musical edit" disabled={!history.canRedo} onClick={history.redo}><Redo2 size={16} /></IconButton></div>
        <Button variant="danger" onClick={preview.panic}><CircleStop size={13} aria-hidden="true" /> Panic</Button>
      </div>
    </header>
    <div className="drum-transport">
      <Segmented label="Drums mode" value={mode} choices={modes} onChange={setDrumMode} />
      <span className="transport-divider" />
      <Button className="play-preview" variant={preview.running ? 'neutral' : 'primary'} onClick={preview.running ? preview.stop : () => void preview.start()}>{preview.running ? <Square size={12} fill="currentColor" aria-hidden="true" /> : <Play size={13} fill="currentColor" aria-hidden="true" />}{preview.running ? 'Stop preview' : 'Play preview'}</Button>
      <div className="transport-clock"><span className="eyebrow">Demo clock</span><label><input type="number" min={50} max={220} value={tempoText} onChange={e => setTempoText(e.target.value)} onBlur={() => { const next = tempoText.trim() && Number.isFinite(+tempoText) ? Math.min(220, Math.max(50, Math.round(+tempoText))) : bpm; setBpm(next); setTempoText(String(next)); }} onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur(); }} aria-label="Browser preview tempo BPM" /> <span>BPM</span></label></div>
      <div className="transport-position" aria-label="Browser preview position"><span className="eyebrow">Position</span><span className="mono">{numberTag((preview.position?.bar ?? 0) + 1)}<b>:</b>{numberTag((preview.position?.step ?? 0) + 1)}</span></div>
      <div className="spacer" />
      <span className="host-status"><span className="status-dot" aria-hidden="true" /> Host / MIDI <b>Not connected</b></span>
    </div>
    {preview.state === 'error' && <Notice tone="error">Browser audio: {preview.error}</Notice>}
    <div className="work-area" ref={room.ref} style={bodyStyle} data-maximized={maximized !== null}>
      {side('left')}
      <Splitter className="side-divider side-left" direction="vertical" label="Resize left dock" value={actualLayout.left} min={bounds.width[0]} max={bounds.width[1]} onChange={leftWidth => setLayout(old => ({ ...old, leftWidth }))} />
      <div className="center-region" ref={central.ref}>
        {layout.secondary && !fits && maximized === null && <Segmented className="fallback-editors" label="Visible center editor" value={showing} onChange={setFocusPane} choices={[{ value: 'primary', label: editorNames[layout.primary] }, { value: 'secondary', label: editorNames[layout.secondary] }]} />}
        <div className="center-grid" data-split={splitVisible ? layout.split : undefined} style={{ '--ratio': `${visibleRatio}%` } as CSSProperties}>
          {pane(layout.primary, 'primary')}
          {splitVisible && <Splitter label="Resize editor split" direction={layout.split === 'side' ? 'vertical' : 'horizontal'} value={visibleRatio} min={minimum} max={100 - minimum} onChange={ratio => setLayout(old => ({ ...old, ratio }))} percent />}
          {layout.secondary && pane(layout.secondary, 'secondary')}
        </div>
      </div>
      <Splitter className="side-divider side-right" direction="vertical" label="Resize right dock" value={actualLayout.right} min={bounds.width[0]} max={bounds.width[1]} onChange={rightWidth => setLayout(old => ({ ...old, rightWidth }))} invert />
      {side('right')}
    </div>
    <div className="mixer-wrap" data-mode={mixerMode}>
      {mixerMode === 'expanded' && <Splitter label="Resize mixer height" direction="horizontal" value={layout.mixHeight} min={bounds.height[0]} max={bounds.height[1]} onChange={mixHeight => setLayout(old => ({ ...old, mixHeight }))} invert />}
      <Mixer bpm={bpm} session={session} mode={mixerMode} setMode={mixer => setLayout(old => ({ ...old, mixer, mixerAuto: false }))} bank={bank} setBank={setBank} height={layout.mixHeight} />
    </div>
    <footer className="instrument-foot"><span className="inline"><Activity size={13} aria-hidden="true" /><span>Web Audio preview</span><i aria-hidden="true" /> <span>{preview.running ? 'Playing locally' : preview.state === 'running' ? 'Audio ready' : 'Ready to play'}</span></span>
      <span className="live-notice" role="status">{status}</span><span className="foot-right"><AudioLines size={12} aria-hidden="true" /> {layoutPersisted ? 'Workspace saved locally' : 'Storage unavailable'} <span className="foot-hint">· Shift + drag for fine adjustment</span></span>
    </footer>
  </main>;
}
