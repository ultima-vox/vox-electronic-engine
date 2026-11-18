import { useState } from 'react';
import { Search } from 'lucide-react';
import { Button, Module, Notice, Select } from '../design-system/ui';
import { downloadBlob } from '../shared/storage';
import matrix from '../../../../docs/ui/vox-music/UI_BINDING_MATRIX.csv?raw';
import { csvRows } from './csv';
import { DocTable } from './DocTable';

const [columns, ...data] = csvRows(matrix);
const productIndex = columns.indexOf('product');
const index = ['product','control','parameterID_or_controller_method','default','status','source_sha'].map(column => columns.indexOf(column));
const entries = data.map(row => ({ product: row[productIndex], terms: row.join(' ').toLowerCase(), summary: index.map(n => row[n]) }));
const pageSize = 24;

/** Interaction adapted from donor A Binding.tsx; the source is the pinned DS-00 audit, not the donor's draft rows. */
export default function BindingAudit() {
  const [product, setProduct] = useState('all');
  const [query, setQuery] = useState('');
  const [page, setPage] = useState(0);
  const shown = entries.filter(row => (product === 'all' || row.product === product) && row.terms.includes(query.trim().toLowerCase()));
  const start = page * pageSize;
  return <Module title="DS-00 binding audit" actions={<Button onClick={() => downloadBlob('UI_BINDING_MATRIX.csv', new Blob([matrix], { type: 'text/csv;charset=utf-8' }))}>Download full CSV</Button>}>
    <div className="panel-body stack">
      <Notice>Static, source-pinned native audit. <b>Partial</b> and <b>gap</b> do not indicate runtime acceptance. Search includes the full source rows; the CSV contains all {columns.length} columns.</Notice>
      <div className="inline wrap">
        <Select label="Binding product" value={product} onChange={e => { setProduct(e.target.value); setPage(0); }}><option value="all">All products</option><option value="drums">Drums</option><option value="fx">FX</option></Select>
        <label className="search-field"><Search size={14} aria-hidden="true" /><input type="search" aria-label="Search DS-00 bindings" placeholder="Search ID, control or SHA…" value={query} onChange={e => { setQuery(e.target.value); setPage(0); }} /></label>
      </div>
      <div className="inline wrap"><span className="caption" aria-live="polite">{shown.length} of {entries.length} rows{shown.length > 0 && ` · ${start + 1}–${Math.min(start + pageSize, shown.length)}`}</span><div className="spacer" />
        <Button disabled={page === 0} onClick={() => setPage(n => n - 1)}>Previous rows</Button><Button disabled={start + pageSize >= shown.length} onClick={() => setPage(n => n + 1)}>Next rows</Button>
      </div>
    </div>
    <DocTable label="DS-00 native binding rows" headings={['Product','Control','Host ID / controller','Default','Status','Source SHA']} rows={shown.slice(start, start + pageSize).map(row => row.summary)} />
  </Module>;
}
