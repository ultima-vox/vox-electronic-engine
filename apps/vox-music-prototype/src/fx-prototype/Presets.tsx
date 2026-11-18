import { useRef, useState } from 'react';
import { ArrowDownToLine, FolderOpen, Save } from 'lucide-react';
import { PresetBrowser } from '../design-system/content';
import { Badge, Button, Field, Module, Notice, Segmented } from '../design-system/ui';
import { downloadBlob, readLocal, writeLocal } from '../shared/storage';
import { categories, demoPrograms, durationSeconds, validateProgram } from './model';
import type { Program } from './model';
import type { FXSession } from './session';

const presetsKey = 'vox.fx.programs.v1';
const favoritesKey = 'vox.fx.favorites.v1';
export type SavedProgram = { id: string; program: Program };
export function readPrograms(): SavedProgram[] {
  const v = readLocal(presetsKey);
  if (!Array.isArray(v)) return [];
  const parsed = v.slice(0,16).flatMap((item): SavedProgram[] => {
    const program = validateProgram(item?.program);
    return typeof item?.id === 'string' && /^user:[a-f\d-]{1,40}$/i.test(item.id) && program ? [{ id: item.id, program }] : [];
  });
  return parsed.filter((item,n) => parsed.findIndex(q => q.id === item.id) === n);
}
export function readFavorites(): string[] {
  const raw = readLocal(favoritesKey);
  return Array.isArray(raw) ? raw.filter(id => typeof id === 'string' && id.length < 64).slice(0,20) : [];
}
export function PresetsZone({ s, userPrograms, setUser, favorites, setFavorites, compare, variant, switchVariant, copyVariant }: { s: FXSession; userPrograms: SavedProgram[]; setUser: (value: SavedProgram[]) => void; favorites: string[]; setFavorites: (value: string[]) => void; compare: React.RefObject<Partial<Record<'A'|'B',Program>>>; variant: 'A'|'B'; switchVariant: (value: 'A'|'B') => void; copyVariant: () => void }) {
  const [collection, setCollection] = useState<'demo'|'user'>('demo');
  const [name, setName] = useState('');
  const [error, setError] = useState('');
  const input = useRef<HTMLInputElement>(null);
  const p = s.history.value;
  const items = collection === 'demo' ? demoPrograms : userPrograms;
  function store(program: Program) {
    const old = userPrograms.find(v => v.program.name === program.name);
    const id = old?.id ?? `user:${crypto.randomUUID()}`;
    const next = [...userPrograms.filter(v => v.id !== id), { id, program }].slice(-16);
    if (!writeLocal(presetsKey,next)) { setError('Local storage was unavailable. Program was not saved.'); return false; }
    setUser(next); return true;
  }
  function save() {
    const candidate = validateProgram({ ...p, name: name.trim() || p.name });
    if (!candidate) { setError('Program is outside the browser schema bounds.'); return; }
    if (store(candidate)) { setError(''); setName(''); setCollection('user'); s.say(`Saved ${candidate.name} locally.`); }
  }
  async function importProgram(file: File) {
    setError('');
    try {
      if (file.size > 350000) throw new Error('Choose a browser Program JSON smaller than 350 KB.');
      const candidate = validateProgram(JSON.parse(await file.text()));
      if (!candidate) throw new Error('Invalid Program v1. Current Program was not changed.');
      if (store(candidate)) { s.history.edit(() => candidate); s.preview.panic(); setCollection('user'); s.say(`Imported ${candidate.name}.`); }
    } catch (err) { setError(err instanceof SyntaxError ? 'JSON could not be parsed. Current Program was not changed.' : err instanceof Error ? err.message : 'Import failed.'); }
  }
  function favorite(id: string) {
    const next = favorites.includes(id) ? favorites.filter(v => v !== id) : [...favorites,id].slice(-20);
    if (!writeLocal(favoritesKey,next)) { setError('Could not save favorites locally.'); return; }
    setFavorites(next);
  }
  return <div className="fx-preset-grid">
    <Module title="Program Browser" icon={<FolderOpen size={15} aria-hidden="true" />} actions={<Badge>{items.length} PROGRAMS</Badge>}><div className="panel-body stack">
      <Segmented label="FX preset collection" value={collection} choices={[{ value: 'demo', label: 'Browser collection' },{ value: 'user', label: 'User' }]} onChange={setCollection} />
      <PresetBrowser items={items.map(item => ({ id: item.id, name: item.program.name, category: item.program.category, description: `${item.program.style} · ${durationSeconds(item.program).toFixed(2)} s`, badge: item.program.category }))} categoryOptions={categories} favorites={favorites} onFavorite={favorite} selected={items.find(item => item.program.name === p.name)?.id} onSelect={id => { const chosen = items.find(item => item.id === id); if (chosen) { s.history.edit(() => chosen.program); s.preview.panic(); s.say(`Loaded ${chosen.program.name}.`); } }} />
    </div></Module>
    <div className="stack"><Module title="Compare A / B" actions={<Badge tone="accent">{variant}</Badge>}><div className="panel-body stack">
      <Segmented label="Program comparison" value={variant} choices={[{ value: 'A', label: 'A' }, { value: 'B', label: 'B' }]} onChange={switchVariant} />
      <span className="caption">The active side follows your edits. Other side: {compare.current[variant === 'A' ? 'B' : 'A']?.name ?? 'Not set; first switch copies current'}.</span>
      <Button onClick={copyVariant}>Copy {variant} to {variant === 'A' ? 'B' : 'A'}</Button>
    </div></Module>
    <Module title="User Program" icon={<Save size={15} aria-hidden="true" />}><div className="panel-body stack">
      <Field label="Save Program as"><input value={name} placeholder={p.name} maxLength={52} onChange={e => setName(e.target.value)} /></Field>
      <div className="inline wrap"><Button variant="primary" onClick={save}><Save size={12} aria-hidden="true" /> Save Program</Button>
        <Button onClick={() => input.current?.click()}>Import JSON</Button><input ref={input} className="sr-only" tabIndex={-1} type="file" accept=".json,application/json" aria-label="Import FX Program JSON" onChange={e => { const file = e.target.files?.[0]; if (file) void importProgram(file); e.target.value = ''; }} />
        <Button onClick={() => downloadBlob('vox-fx-program.json',new Blob([JSON.stringify(p,null,2)],{ type: 'application/json' }))}><ArrowDownToLine size={12} aria-hidden="true" /> Export JSON</Button></div>
      {error && <Notice tone="error">{error}</Notice>}<p className="caption">Programs store configuration and sample references only. Native factory audio, WAV bounce and DAW transfer are not connected.</p>
    </div></Module></div>
  </div>;
}
