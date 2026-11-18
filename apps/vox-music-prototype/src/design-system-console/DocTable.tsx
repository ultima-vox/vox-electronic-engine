import { Badge } from '../design-system/ui';

export function DocTable({ label, headings, rows }: { label?: string; headings: readonly string[]; rows: readonly (readonly string[])[] }) {
  return <div className="matrix-scroll"><table className="doc-table" aria-label={label}>
    <thead><tr>{headings.map(key => <th scope="col" key={key}>{key}</th>)}</tr></thead>
    <tbody>{rows.length ? rows.map((row,n) => <tr key={n}>{row.map((value,i) => <td key={i}>{['Browser','Partial','Pending'].includes(value) ? <Badge tone={value === 'Browser' ? 'good' : value === 'Partial' ? 'warning' : 'neutral'}>{value}</Badge> : value}</td>)}</tr>) : <tr><td colSpan={headings.length}>No rows match.</td></tr>}</tbody>
  </table></div>;
}
