import { useEffect, useId, useRef, useState } from 'react';
import { Activity, AudioLines, Binary, FolderOpen, Layers3, Redo2, SlidersHorizontal, Undo2, Waves, Zap } from 'lucide-react';
import { Badge, IconButton, TabPanel, Tabs } from '../design-system/ui';
import type { ParameterBinding } from '../design-system/parameters';
import { useHistory } from '../shared/history';
import { defaultProgram } from './model';
import type { Locks, Program } from './model';
import { MotionZone } from './Motion';
import { PresetsZone, readFavorites, readPrograms } from './Presets';
import { AdvancedZone, DesignZone, PerformZone, ProcessZone, TimelineZone, TriggerZone } from './Zones';
import { useFXPreview } from './preview';
import type { FXSession } from './session';
import './fx.css';

const areas = [
  { value: 'perform', label: 'Perform', Icon: SlidersHorizontal }, { value: 'design', label: 'Design', Icon: Layers3 },
  { value: 'timeline', label: 'Timeline', Icon: Activity }, { value: 'motion', label: 'Motion', Icon: Waves },
  { value: 'process', label: 'Process', Icon: AudioLines }, { value: 'trigger', label: 'Trigger', Icon: Zap },
  { value: 'presets', label: 'Presets', Icon: FolderOpen }, { value: 'advanced', label: 'Advanced', Icon: Binary },
] as const;
const fxChoices = areas.map(({ value, label, Icon }) => ({ value, label: <><Icon size={13} aria-hidden="true" />{label}</> }));
type Area = typeof areas[number]['value'];
export function FxPrototype({ active }: { active: boolean }) {
  const history = useHistory(defaultProgram);
  const preview = useFXPreview(history.current, active);
  const [area, setArea] = useState<Area>('perform');
  const [curveIndex, setCurveIndex] = useState(0);
  const [locks, setLocks] = useState<Locks>({ layers: false, curves: false, routing: false });
  const [status, say] = useState('');
  const [userPrograms, setUser] = useState(readPrograms);
  const [favorites, setFavorites] = useState(readFavorites);
  const [variant, setVariant] = useState<'A'|'B'>('A');
  const compare = useRef<Partial<Record<'A'|'B',Program>>>({});
  const tabId = useId();
  useEffect(() => { if (!status) return; const t = setTimeout(() => say(''),5000); return () => clearTimeout(t); }, [status]);
  useEffect(() => {
    if (!active) return;
    const handle = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLElement && e.target.closest('input,textarea,[contenteditable]')) return;
      if (e.key.toLowerCase() === 'z' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); if (e.shiftKey) history.redo(); else history.undo(); }
    };
    window.addEventListener('keydown',handle); return () => window.removeEventListener('keydown',handle);
  }, [active,history.undo,history.redo]);
  const s: FXSession = { history, preview, say, bind: (spec: ParameterBinding['spec'], value, apply, extra) => ({ spec, value, onChange: apply, onBegin: history.begin, onEnd: history.end, ...extra }) };
  function switchVariant(next: 'A'|'B') {
    if (next === variant) return;
    compare.current[variant] = history.current.current;
    history.edit(old => compare.current[next] ?? old);
    setVariant(next); preview.panic();
  }
  const root = useRef<HTMLDivElement>(null);
  return <main className="fx instrument-page" id="fx-main" aria-labelledby="fx-title">
    <header className="product-intro">
      <div className="product-id"><div className="product-icon"><AudioLines size={25} strokeWidth={1.5} aria-hidden="true" /></div><div><p className="eyebrow">INSTRUMENT 02 <span>/</span> EVENT DESIGN</p><h1 id="fx-title">VOX FX</h1></div></div>
      <div className="instrument-actions inline wrap"><div className="fx-program-label"><span className="eyebrow">Program</span><strong>{history.value.name}</strong></div>
        <Badge dot>DSP NOT CONNECTED</Badge><div className="instrument-history inline"><IconButton label="Undo Program edit" disabled={!history.canUndo} onClick={history.undo}><Undo2 size={16} /></IconButton><IconButton label="Redo Program edit" disabled={!history.canRedo} onClick={history.redo}><Redo2 size={16} /></IconButton></div></div>
    </header>
    <Tabs label="FX work areas" className="fx-nav" id={tabId} choices={fxChoices} value={area} onChange={next => { setArea(next); if (root.current) root.current.scrollTop = 0; }} />
    <div className="fx-scroll" ref={root}><TabPanel className="fx-content" id={tabId} value={area}>
      {area === 'perform' && <PerformZone s={s} locks={locks} setLocks={setLocks} />}
      {area === 'design' && <DesignZone s={s} openTimeline={i => { setCurveIndex(i); setArea('timeline'); }} />}
      {area === 'timeline' && <TimelineZone s={s} index={curveIndex} setIndex={setCurveIndex} />}
      {area === 'motion' && <MotionZone s={s} />}
      {area === 'process' && <ProcessZone s={s} />}
      {area === 'trigger' && <TriggerZone s={s} />}
      {area === 'presets' && <PresetsZone s={s} userPrograms={userPrograms} setUser={setUser} favorites={favorites} setFavorites={setFavorites} compare={compare} variant={variant} switchVariant={switchVariant} copyVariant={() => { compare.current[variant === 'A' ? 'B' : 'A'] = history.current.current; say(`Copied ${variant} to ${variant === 'A' ? 'B' : 'A'}.`); }} />}
      {area === 'advanced' && <AdvancedZone s={s} />}
    </TabPanel></div>
    <footer className="instrument-foot fx-foot"><span className="inline"><Activity size={13} aria-hidden="true" /> Program & event preview · no audio DSP</span><span className="live-notice" role="status">{status}</span><span className="foot-right">Program v1 · browser</span></footer>
  </main>;
}
