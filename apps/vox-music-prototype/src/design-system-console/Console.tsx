import { lazy, Suspense, useEffect, useId, useState } from 'react';
import type { CSSProperties, ReactNode } from 'react';
import { Activity, ArrowRight, ArrowUpRight, BookOpen, CircleDot, CodeXml, Grid3X3, Layers3, ListChecks, Palette, Play, SlidersHorizontal, Workflow } from 'lucide-react';
import { contrastRatio } from '../design-system/contrast';
import { PresetBrowser } from '../design-system/content';
import { Fader, Knob, Meter, RangeSlider, Slider } from '../design-system/controls';
import { CurveEditor, Envelope, FilterResponse, Waveform } from '../design-system/graph';
import { createCurve } from '../design-system/curves';
import { linear } from '../design-system/parameters';
import type { ParameterBinding } from '../design-system/parameters';
import { Keyboard, PadGrid, SongTimeline, TriggerButton, XYPad } from '../design-system/perform';
import { padColours, space, surfaces, themes } from '../design-system/tokens';
import type { Theme } from '../design-system/tokens';
import { Badge, Button, Module, Notice, Segmented, Select, Splitter, Switch, TabPanel, Tabs, Toggle } from '../design-system/ui';
import { bindings, drumsMatrix, fxMatrix, registry, sections } from './data';
import type { Section } from './data';
import { DocTable } from './DocTable';
import './console.css';

const BindingAudit = lazy(() => import('./BindingAudit'));

const icons = [BookOpen, Palette, SlidersHorizontal, ListChecks, Grid3X3, Activity, CodeXml, Workflow];
const themeChoices = (Object.keys(themes) as Theme[]).map(value => ({ value, label: themes[value].name }));
function Overview({ open }: { open: (section: Section) => void }) {
  return <div className="stack overview">
    <div className="console-hero"><div className="stack"><span className="eyebrow">VOX MUSIC / DESIGN LANGUAGE</span><h2>Controls for making music.</h2><p>One tactile visual language for three product accents. Explore the real browser controls, trace their requirements, and see what still needs a native binding.</p><div className="inline wrap"><Button variant="primary" onClick={() => open('Showcase')}>Explore controls <ArrowRight size={14} aria-hidden="true" /></Button><Button onClick={() => open('Tokens')}>View tokens</Button></div></div>
      <div className="hero-instrument" aria-hidden="true"><div className="hero-cap"><i /></div><div className="hero-slots"><i /><i /><i /><i /></div><div className="hero-engraving">VOX / 01<span>CONTROL SURFACE</span></div></div>
    </div>
    <div className="overview-stats">{[['09','Component families'],['03','Product accents'],['02','Instrument prototypes']].map(([n,label]) => <div className="ds-panel" key={n}><strong>{n}</strong><span className="eyebrow">{label}</span></div>)}</div>
    <div className="overview-links">{([['Drums','Rhythm workstation','Independent select / audition, docking, Song and one Mixer.'],['FX','Event design','Four layers, twenty curves, modulation and a four-stage chain.'],['Binding','From web to JUCE','Map typed controls to host parameters and immutable structural state.']] as const).map(([key,title,text]) => <button className="ds-panel overview-link" type="button" key={key} onClick={() => open(key)}><span className="eyebrow">{key}</span><strong>{title} <ArrowUpRight size={14} aria-hidden="true" /></strong><p>{text}</p></button>)}</div>
    <Notice>The initial browser build preceded the donor upload. This reference uses reviewed A/B deltas and the subsequent DS-00 audit. Browser checks do not establish native behavior or owner visual acceptance.</Notice>
  </div>;
}
const palette = Object.fromEntries(surfaces.map(([name, , hex]) => [name, hex]));
const contrastPairs = [
  ['Text', 'Panel', 4.5], ['Secondary', 'Raised', 4.5], ['Muted', 'Raised', 4.5],
  ['Product accent', 'Panel', 3], ['Warning', 'Panel', 4.5], ['Error', 'Raised', 4.5], ['Border', 'Panel', 3],
] as const;
function TokenContrast({ theme }: { theme: Theme }) {
  const colors: Record<string, string> = { ...palette, 'Product accent': themes[theme].accent };
  const rows = contrastPairs.map(([fg, bg, min]) => {
    const actual = contrastRatio(colors[fg], colors[bg]);
    return [fg, bg, `${actual.toFixed(2)}:1`, `${min}:1`, actual >= min ? 'Meets target' : 'Below target'];
  });
  return <Module title="Token contrast"><p className="caption panel-body">Calculated for opaque browser tokens (A). Borders can be decorative; this does not check focus, opacity, gradients or the rendered UI.</p><DocTable label="Browser token contrast" headings={['Foreground','Background','Ratio','Target','Result']} rows={rows} /></Module>;
}
function Tokens({ theme }: { theme: Theme }) {
  const t = themes[theme];
  return <div className="stack"><Module title="Semantic color" actions={<Badge tone="accent">{t.name} ACCENT</Badge>}><div className="panel-body swatches">{[...surfaces,['Product accent','--accent',t.accent]].map(([name,id,hex]) => <article key={id} className="token-swatch"><span style={{ backgroundColor: hex }} /><strong>{name}</strong><span>{id}</span><code>{hex}</code></article>)}</div></Module>
    <Module title="Typography & spacing"><div className="panel-body stack"><span className="type-display">VOX Music / 31</span><span>Inter Variable · Control labels and body</span><span className="mono">IBM Plex Mono · 01:16 · −3.5 dBFS</span><div className="inline wrap spacing-strip">{space.map(n => <span key={n}><i style={{ width: n, height: n }} />{n}</span>)}</div><p className="caption">Spacing 4–32 px follows the existing native token rhythm. Product accent changes only semantic foreground / border / fill roles, not signal meaning.</p></div></Module><TokenContrast theme={theme} /></div>;
}
const showcaseGroups = ['Parameters','Performance','Graphs','Composition'] as const;
function Showcase() {
  const [group, setGroup] = useState<typeof showcaseGroups[number]>('Parameters');
  const [state, setState] = useState('Normal');
  const [params, set] = useState<Record<string, number>>({});
  const [picked, setPad] = useState(0);
  const [trigger, setTrigger] = useState<number | null>(null);
  const [note, setNote] = useState(48);
  const [bar, setBar] = useState(0);
  const [width, setWidth] = useState(45);
  const [flag, setFlag] = useState(false);
  const [preset, setPreset] = useState('');
  const [favorites, setFavorites] = useState<string[]>([]);
  const [curve, setCurve] = useState(() => createCurve([[0,0],[.24,.89],[.65,.42],[1,0]]));
  const id = useId();
  const bind = (key: string, label: string, min: number, max: number, def: number, step: number, unit?: string): ParameterBinding => ({ spec: { ...linear(key,label,min,max,def,step,unit), keyStep: step * 5 }, value: params[key] ?? def, onChange: value => set(prev => ({ ...prev, [key]: value })) });
  const main = bind('cutoff','Cutoff',20,20000,1200,1,'Hz');
  const fixture = (binding: ParameterBinding): ParameterBinding => ({ ...binding, disabled: state === 'Disabled', selected: state === 'Selected', effective: state === 'Modulated' ? Math.min(binding.spec.max, binding.value + (binding.spec.max - binding.spec.min) * .18) : undefined, automated: state === 'Automated', message: state === 'Warning' || state === 'Error' ? { severity: state === 'Warning' ? 'warning' : 'error', text: state === 'Warning' ? 'Check range' : 'Link unavailable' } : undefined });
  useEffect(() => { if (trigger === null) return; const t = setTimeout(() => setTrigger(null),300); return () => clearTimeout(t); }, [trigger]);
  return <div className="stack"><Tabs label="Component family showcase" id={id} value={group} choices={showcaseGroups.map(value => ({ value,label:value }))} onChange={setGroup} />
    <TabPanel id={id} value={group} className="stack">
      {group === 'Parameters' && <><Module title="Unified parameter binding" actions={<Badge>REACT REFERENCE</Badge>}><div className="panel-body stack">
        <div className="inline wrap"><Select label="Fixture state" hideLabel value={state} onChange={e => setState(e.target.value)}>{['Normal','Selected','Disabled','Automated','Modulated','Warning','Error'].map(name => <option key={name}>{name}</option>)}</Select><span className="caption">Hover, press or Tab to see live states. Drag with Shift for fine values. Double-click resets.</span></div>
        <div className="show-controls"><Knob binding={fixture({ ...main, spec: { ...main.spec, scale: 'log' } })} /><Knob variant="bipolar" binding={fixture(bind('pan','Pan',-100,100,0,1,'%'))} /><Knob variant="encoder" binding={fixture(bind('browse','Encoder',0,15,7,1))} /><Knob variant="macro" binding={fixture(bind('macro','Macro',0,100,48,1,'%'))} /><Fader compact binding={fixture(bind('level','Level',-48,6,-6,.1,'dB'))} /></div>
        <Slider binding={fixture(bind('mix','Blend',0,100,36,.1,'%'))} /><RangeSlider gap={.5} start={fixture(bind('in','In point',0,100,12,.5,'%'))} end={fixture(bind('out','Out point',0,100,87,.5,'%'))} />
      </div></Module><Module title="Actions & switches"><div className="panel-body inline wrap"><Button variant="primary" onClick={() => setFlag(!flag)}><Play size={12} aria-hidden="true"/> Run fixture</Button><Toggle checked={flag} onClick={() => setFlag(!flag)}>Selected</Toggle><Switch label="Enable fixture" checked={flag} onChange={setFlag} /><Button disabled>Disabled</Button><Badge tone={flag ? 'good' : 'neutral'} dot>{flag ? 'ENABLED' : 'OFF'}</Badge></div></Module></>}
      {group === 'Performance' && <div className="show-perform"><Module title="Pad Matrix" actions={<Badge>NO AUDIO FIXTURE</Badge>}><div className="panel-body stack"><PadGrid pads={Array.from({length:16},(_,i) => ({ index:i,name:`Pad ${i+1}`,note:36+i,colour:Object.values(padColours)[i % 5],selected:i===picked,velocity:trigger===i ? 100 : undefined,onSelect:() => setPad(i),onAudition:() => setTrigger(i) }))} /><p className="caption">Selected pad {picked+1}. The trigger pulse is a velocity fixture, not an audio level.</p></div></Module><div className="stack"><Module title="XY Pad"><div className="panel-body"><XYPad x={bind('x','X',0,100,35,.1,'%')} y={bind('y','Y',0,100,68,.1,'%')} /></div></Module><Module title="Trigger Keyboard"><div className="panel-body stack"><Keyboard onPress={setNote} active={[note]} /><TriggerButton label="Trigger note 48" onTrigger={() => setNote(48)} /></div></Module></div></div>}
      {group === 'Graphs' && <><Module title="Timeline Curve"><div className="panel-body"><CurveEditor label="Reference" curve={curve} onChange={setCurve} /></div></Module>
        <div className="show-perform"><Module title="Envelope and filter models"><div className="panel-body stack"><Envelope /><FilterResponse cutoff={1800} /></div></Module><Module title="Sample / meter examples"><div className="panel-body stack"><Waveform peaks={Array.from({length:160},(_,i) => {const a=(1-i/160)**1.6*(.55+.45*Math.cos(i/9)); return [-a,a];})} start={.1} end={.84} /><span className="caption">Synthetic waveform fixture. Not a decoded sample.</span><Meter value={.32} label="Peak fixture, not signal" orientation="horizontal" showValue /><Meter label="Unconnected signal" orientation="horizontal" showValue /></div></Module></div></>}
      {group === 'Composition' && <><Module title="Song strip"><div className="panel-body"><SongTimeline count={16} selected={bar} onSelect={setBar} details={i => ({name:i<8?'P01 · A':'P02 · B',type:i===3?'Unique':'Linked'})} /></div></Module><Module title="Splitter and preset browser"><div className="panel-body show-split" style={{'--left':`${width}%`} as CSSProperties}>
        <PresetBrowser items={[{id:'1',name:'Carbon',category:'Kits',description:'Example kit record'},{id:'2',name:'Motion',category:'Patterns',description:'Example pattern record'}]} selected={preset} favorites={favorites} onFavorite={id => setFavorites(prev => prev.includes(id) ? prev.filter(p=>p!==id) : [...prev,id])} onSelect={setPreset} />
        <Splitter value={width} min={32} max={65} onChange={setWidth} percent direction="vertical" label="Resize showcase split" /><div className="panel-body"><p className="caption">Drag or focus the splitter and use arrows. Search and favorites change the local fixture.</p></div></div></Module></>}
    </TabPanel>
  </div>;
}
const intros: Record<Section,[string,string]> = { Overview:['Design System Console','Reference controls, product themes and evidence.'], Tokens:['Token Library','Graphite materials, semantic colors and measurable geometry.'], Showcase:['Interactive Showcase','The same controlled components used by VOX Drums and VOX FX.'], Registry:['Component Registry','Browser status by family. Native status is not inherited.'], Drums:['Drums Requirement Matrix','Authoritative workstation contract #127 and hardware reference #135.'], FX:['FX Requirement Matrix','Standalone effect generator contract and browser scope.'], Binding:['Parameter Binding','One base value, one gesture lifecycle; read-only telemetry.'], Plan:['Delivery Plan','Browser review first. Native development and acceptance remain gated.'] };
export function Console({ theme, onTheme }: { theme: Theme; onTheme: (next: Theme) => void }) {
  const [section, setSection] = useState<Section>('Overview');
  const [title, caption] = intros[section];
  let body: ReactNode;
  if (section === 'Overview') body = <Overview open={setSection} />;
  else if (section === 'Tokens') body = <Tokens theme={theme} />;
  else if (section === 'Showcase') body = <Showcase />;
  else if (section === 'Registry') body = <Module title="Browser component inventory"><DocTable headings={['Family','Components','Source module','Browser status']} rows={registry} /></Module>;
  else if (section === 'Drums') body = <Module title="Contract traceability" actions={<a href="https://github.com/ultima-vox/vox-electronic-engine/issues/127" target="_blank" rel="noreferrer">Issue #127 <ArrowUpRight size={12} /></a>}><DocTable headings={['Contract','Requirement','Scope','Remaining evidence']} rows={drumsMatrix} /></Module>;
  else if (section === 'FX') body = <Module title="Contract traceability" actions={<a href="https://github.com/ultima-vox/vox-electronic-engine/blob/294a128d47ba027a2c9dbceb776fd1f5e3622d4a/docs/ultima-vox-music/04_VOX_FX_SPEC.md" target="_blank" rel="noreferrer">FX spec <ArrowUpRight size={12} /></a>}><DocTable headings={['Contract','Requirement','Scope','Remaining evidence']} rows={fxMatrix} /></Module>;
  else if (section === 'Binding') body = <div className="stack"><Module title="Binding seams"><DocTable headings={['Control','Browser model','Native seam','Rule']} rows={bindings} /></Module><Suspense fallback={<Notice>Loading the DS-00 binding audit…</Notice>}><BindingAudit /></Suspense><Notice>onBegin → onChange (plain value) → onEnd; only change the base parameter. Modulation is an independently evaluated overlay. A JUCE parameter attachment must own the host gesture and receive host writeback on the message thread.</Notice></div>;
  else body = <Module title="Review gates"><div className="panel-body plan-list">{[['01','Browser reference','Source audit, shared controls, instrument workspaces and reproducible browser checks.'],['02','Owner visual approval','Repeat the visual protocol in the handoff and approve the refined hardware treatment. No C++ UI before approval.'],['03','Native bindings','Port the exact controls to libs/vox-ui, attach APVTS/structural state, validate snapshots and realtime constraints.'],['04','Release evidence','Windows CI, validator, DPI, Cubase and REAPER, audible review and measured routing / export.']].map(([n,heading,text]) => <div className="inline" key={n}><Badge tone="accent">{n}</Badge><div><strong>{heading}</strong><p className="caption">{text}</p></div></div>)}</div></Module>;
  return <main id="console-main" className="console instrument-page" aria-labelledby="console-title">
    <aside className="ds-panel console-sidebar"><header className="panel-header"><Layers3 size={15} aria-hidden="true" /><h2>Component Library</h2></header><nav aria-label="Design system sections">{sections.map((value,i) => { const Icon = icons[i]; return <button type="button" key={value} aria-current={section === value ? 'page' : undefined} onClick={() => setSection(value)}><Icon size={15} aria-hidden="true" />{value}{section === value && <CircleDot size={10} className="nav-indicator" aria-hidden="true" />}</button>; })}</nav>
      <div className="console-theme stack"><span className="eyebrow">Product theme</span><Segmented label="Product accent theme" value={theme} choices={themeChoices} onChange={onTheme} /></div></aside>
    <div className="console-content"><header className="console-heading"><p className="eyebrow">VOX MUSIC / COMPONENT REFERENCE</p><h1 id="console-title">{title}</h1><p>{caption}</p></header>{body}</div>
  </main>;
}
