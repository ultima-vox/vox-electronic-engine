import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { csvRows } from './csv';

test('DS-00 CSV parser retains quoted data, CRLF and trailing empty cells', () => {
  assert.deepEqual(csvRows('\ufeffa,b,c\r\n1,"two, and ""three""\nfour",\r\n'), [
    ['a', 'b', 'c'], ['1', 'two, and "three"\nfour', ''],
  ]);
  assert.throws(() => csvRows('a,b\n1,"oops'), /Unclosed/);
});

test('Console source resolves all audited binding rows without dropping columns or inventing IDs', () => {
  const [head, ...rows] = csvRows(readFileSync(new URL('../../../../docs/ui/vox-music/UI_BINDING_MATRIX.csv', import.meta.url), 'utf8'));
  const index = (key: string) => head.indexOf(key);
  for (const column of ['product', 'control', 'parameterID_or_controller_method', 'default', 'status', 'source_sha']) assert(index(column) >= 0);
  assert.equal(rows.length, 285);
  assert(rows.every(row => row.length === head.length && /^[0-9a-f]{40}$/.test(row[index('source_sha')])));
  const hostRows = rows.filter(row => row[index('kind')] === 'host-param');
  assert.equal(new Set(hostRows.map(row => row[index('parameterID_or_controller_method')])).size, 193);
  assert.deepEqual(new Set(rows.map(row => row[index('status')])), new Set(['partial', 'gap']));
});
