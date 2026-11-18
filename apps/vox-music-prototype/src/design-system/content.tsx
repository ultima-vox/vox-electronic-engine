import { useState } from 'react';
import type { ReactNode } from 'react';
import { Search, Star } from 'lucide-react';
import { Badge, Button, cx, IconButton, Select, Toggle } from './ui';

export type BrowserItem = { id: string; name: string; category: string; description?: string; badge?: string };
export function PresetBrowser({ items, favorites, onFavorite, onSelect, selected, categoryOptions, action = 'Load' }: { items: BrowserItem[]; favorites?: readonly string[]; onFavorite?: (id: string) => void; onSelect: (id: string) => void; selected?: string; categoryOptions?: readonly string[]; action?: string }) {
  const [query, setQuery] = useState('');
  const [category, setCategory] = useState('all');
  const [only, setOnly] = useState(false);
  const categories = categoryOptions ?? [...new Set(items.map(item => item.category))];
  const q = query.trim().toLocaleLowerCase();
  const seen = items.filter(item => (!only || favorites?.includes(item.id)) && (category === 'all' || item.category === category) && `${item.name} ${item.category} ${item.description ?? ''}`.toLocaleLowerCase().includes(q));
  return <div className="shared-browser stack">
    <div className="inline wrap"><label className="search-field"><Search size={14} aria-hidden="true" /><input type="search" aria-label="Search presets" placeholder="Search presets…" value={query} onChange={e => setQuery(e.target.value)} /></label>
      <Select label="Filter by category" value={category} hideLabel onChange={e => setCategory(e.target.value)}><option value="all">All categories</option>{categories.map(name => <option key={name}>{name}</option>)}</Select>
      {onFavorite && <Toggle checked={only} onClick={() => setOnly(!only)}><Star size={12} aria-hidden="true" /> Favorites</Toggle>}
    </div>
    <div className="browser-results">
      {seen.map(item => <article className={cx('browser-row', item.id === selected && 'is-selected')} key={item.id}><div><strong>{item.name}</strong>{item.description && <p className="caption">{item.description}</p>}</div><div className="spacer"/>{item.badge && <Badge>{item.badge}</Badge>}
        {onFavorite && <IconButton aria-pressed={favorites?.includes(item.id)} label={`Favorite ${item.name}`} onClick={() => onFavorite(item.id)}><Star size={13} fill={favorites?.includes(item.id) ? 'currentColor' : 'none'} aria-hidden="true" /></IconButton>}
        <Button aria-label={`${action} ${item.name}`} onClick={() => onSelect(item.id)}>{action}</Button>
      </article>)}
      {!seen.length && <p className="caption browser-empty">No presets match these filters.</p>}
    </div>
  </div>;
}
export function DropZone({ label, description, kind, onDrop, children }: { label: string; description: string; kind: string; onDrop: (data: string) => void; children?: ReactNode }) {
  const [over, setOver] = useState(false);
  return <div className="ds-dropzone" data-over={over} onDragOver={event => {
    if (event.dataTransfer.types.includes(kind)) { event.preventDefault(); event.dataTransfer.dropEffect = 'copy'; setOver(true); }
  }} onDragLeave={event => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setOver(false); }} onDrop={event => {
    if (!event.dataTransfer.types.includes(kind)) return; event.preventDefault(); setOver(false); onDrop(event.dataTransfer.getData(kind));
  }}><strong>{label}</strong><span className="caption">{description}</span>{children}</div>;
}
export function ModulationTable({ headers, children, columnCount, empty }: { headers: ReactNode[]; children: ReactNode; columnCount: number; empty?: string }) {
  return <div className="matrix-scroll"><table className="mod-matrix"><thead><tr>{headers.map((value, i) => <th scope="col" key={i}>{value}</th>)}</tr></thead><tbody>{empty ? <tr><td colSpan={columnCount}>{empty}</td></tr> : children}</tbody></table></div>;
}
