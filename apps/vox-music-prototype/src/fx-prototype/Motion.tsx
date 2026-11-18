import { useState } from 'react';
import { AudioLines, Route as RouteIcon, Trash2 } from 'lucide-react';
import { ModulationTable, DropZone } from '../design-system/content';
import { Knob, NumericValue } from '../design-system/controls';
import { CurveEditor, CurveView } from '../design-system/graph';
import { createCurve } from '../design-system/curves';
import { linear } from '../design-system/parameters';
import { Badge, Button, IconButton, Module, Notice, Select, Switch } from '../design-system/ui';
import { destinations, fxLimits, lfoValue, macroSources, newRoute, sourceValue, sources, syncChoices, waves } from './model';
import type { LFOSpec, Route } from './model';
import type { FXSession } from './session';

const modMime = 'application/x-vox-fx-macro';
export function MotionZone({ s }: { s: FXSession }) {
  const p = s.history.value;
  const [macro, setMacro] = useState(macroSources[0]);
  const [target, setTarget] = useState(destinations[0].id);
  function attach(source = 'LFO 1', destination = 'A.gain') {
    s.history.edit(old => {
      if (old.routes.length >= fxLimits.routes || !sources.includes(source) || !destinations.some(d => d.id === destination)) return old;
      const id = Array.from({ length: fxLimits.routes }, (_, n) => n + 1).find(n => old.routes.every(route => route.id !== n));
      return id ? { ...old, routes: [...old.routes,newRoute(id,source,destination)] } : old;
    });
  }
  function update(id: number, fn: (route: Route) => Route) { s.history.edit(old => ({ ...old, routes: old.routes.map(route => route.id === id ? fn(route) : route) })); }
  function updateLfo(index: number, patch: Partial<LFOSpec>) { s.history.edit(old => ({ ...old, lfos: old.lfos.map((lfo,i) => i === index ? { ...lfo, ...patch } : lfo) })); }
  const atLimit = p.routes.length >= fxLimits.routes;
  return <div className="stack">
    <div className="motion-grid">{p.lfos.map((lfo,i) => <Module key={i} title={`LFO ${i + 1}`} icon={<AudioLines size={14} aria-hidden="true" />} actions={<Badge>{lfo.sync ? 'SYNC TO DEMO' : 'FREE'}</Badge>}><div className="panel-body stack">
      <div className="inline wrap"><Select label={`LFO ${i + 1} wave shape`} value={lfo.shape} onChange={e => updateLfo(i, { shape: e.target.value as LFOSpec['shape'] })}>{waves.map(wave => <option key={wave}>{wave}</option>)}</Select>
        <Switch label={`Sync LFO ${i + 1}`} checked={lfo.sync} onChange={sync => updateLfo(i, { sync })} />
        {lfo.sync ? <Select label={`LFO ${i + 1} division`} value={lfo.division} onChange={e => updateLfo(i, { division: e.target.value as LFOSpec['division'] })}>{syncChoices.map(name => <option key={name}>{name}</option>)}</Select> : <Knob variant="encoder" size="small" binding={s.bind({ ...linear('rate','Rate',.01,24,i ? 1.25 : .5,.01,'Hz'), keyStep: .1 },lfo.rate,rate => updateLfo(i, { rate }))} />}</div>
      <CurveView label={`LFO ${i + 1} evaluated over two seconds`} curve={createCurve(Array.from({ length: 80 }, (_, n) => [n / 79, lfoValue(lfo, n / 79 * 2)]))} />
    </div></Module>)}</div>
    <Module title="Modulation Matrix" icon={<RouteIcon size={14} aria-hidden="true" />} actions={<Badge tone="accent">{p.routes.length} / {fxLimits.routes} ROUTES</Badge>}>
      <div className="panel-body stack"><p className="caption">Sources cannot be targets, so routes cannot create cycles. The UI evaluates normalized offsets; smoothing is a stored native target.</p>
      <ModulationTable headers={['Source', 'Target', 'Depth', 'Polarity', 'Source range', 'Smoothing', 'Readback', 'Remove']} columnCount={8} empty={!p.routes.length ? 'No routes. Add an LFO route or assign a macro.' : undefined}>
        {p.routes.map((route,i) => {
          const readback = sourceValue(p, route.source, s.preview.modInput);
          return <tr key={route.id}><td><Select hideLabel label={`Route ${i + 1} source`} value={route.source} onChange={e => update(route.id, r => ({ ...r, source: e.target.value }))}>{sources.map(source => <option key={source} disabled={source === 'Self follower' && route.source !== source} value={source}>{source}{source === 'Self follower' ? ' (not connected)' : ''}</option>)}</Select></td>
            <td><Select hideLabel label={`Route ${i + 1} target`} value={route.target} onChange={e => update(route.id, r => ({ ...r, target: e.target.value }))}>{destinations.map(d => <option key={d.id} value={d.id}>{d.label}</option>)}</Select></td>
            <td><NumericValue binding={s.bind(linear(`depth${i}`,`Route ${i + 1} depth`,-100,100,30,1,'%'),route.depth,depth => update(route.id, r => ({ ...r, depth })))} /></td>
            <td><Select hideLabel label={`Route ${i + 1} polarity`} value={route.polarity} onChange={e => update(route.id, r => ({ ...r, polarity: e.target.value as Route['polarity'] }))}><option>Bipolar</option><option>Unipolar</option></Select></td>
            <td><span className="inline"><NumericValue binding={s.bind(linear(`min${i}`,`Route ${i + 1} range minimum`,0,100,0,1,'%'),route.min,v => update(route.id, r => ({ ...r, min: Math.min(r.max, v) })))} /><span aria-hidden="true"> → </span><NumericValue binding={s.bind(linear(`max${i}`,`Route ${i + 1} range maximum`,0,100,100,1,'%'),route.max,v => update(route.id, r => ({ ...r, max: Math.max(r.min, v) })))} /></span></td>
            <td><NumericValue binding={s.bind(linear(`smoothing${i}`,`Route ${i + 1} smoothing`,0,250,12,1,'ms'),route.smoothing,smoothing => update(route.id, r => ({ ...r, smoothing })))} /></td>
            <td className="mono">{readback === null ? 'Not connected' : `${Math.round(readback * 100)}% source`}</td>
            <td><IconButton label={`Remove route ${i + 1}`} onClick={() => s.history.edit(old => ({ ...old, routes: old.routes.filter(r => r.id !== route.id) }))}><Trash2 size={13} aria-hidden="true" /></IconButton></td></tr>;
        })}
      </ModulationTable><Button onClick={() => attach()} disabled={atLimit}>Add route</Button>
      <DropZone label="Macro assignment" description="Drop a macro here, or select a macro and use Assign." kind={modMime} onDrop={value => { if (macroSources.includes(value)) attach(value, target); }}>
        <div className="inline wrap macro-drag-list">{macroSources.map(src => <Button key={src} className="sm" draggable={!atLimit} disabled={atLimit} aria-pressed={src === macro} onClick={() => setMacro(src)} onDragStart={e => { e.dataTransfer.setData(modMime,src); e.dataTransfer.effectAllowed = 'copy'; setMacro(src); }}>{src}</Button>)}</div>
        <div className="inline wrap"><Select label="Macro assignment target" value={target} hideLabel onChange={e => setTarget(e.target.value)}>{destinations.map(d => <option key={d.id} value={d.id}>{d.label}</option>)}</Select><Button onClick={() => attach(macro,target)} disabled={atLimit}>Assign {macro}</Button></div>
      </DropZone>
      </div>
    </Module>
    <Module title="Event Modulation Sources"><div className="panel-body stack"><div className="source-config"><Knob size="small" binding={s.bind(linear('snh','S&H interval',.05,8,.5,.01,'s'),p.sampleHold,sampleHold => s.history.edit(old => ({ ...old, sampleHold })))} />
      {(['attack','decay','sustain','release'] as const).map((key,i) => <Knob size="small" key={key} binding={s.bind(linear(key,key[0].toUpperCase() + key.slice(1),0,100,[12,36,65,40][i],1,'%'), p.adsr[key], n => s.history.edit(old => ({ ...old, adsr: { ...old.adsr, [key]: n } })))} />)}</div>
      <CurveEditor label="Step envelope" curve={p.step} onChange={step => s.history.edit(old => step === old.step ? old : { ...old, step })} onBegin={s.history.begin} onEnd={s.history.end} />
      <Notice>Event phase and velocity come from the UI event clock. The self-signal follower is not connected.</Notice></div></Module>
  </div>;
}
