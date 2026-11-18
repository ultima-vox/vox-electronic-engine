import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { test } from 'node:test';

const allowed: Record<string, string[]> = {
  'design-system': ['design-system'],
  'design-system-console': ['design-system-console', 'design-system', 'shared'],
  'drums-prototype': ['drums-prototype', 'design-system', 'shared'],
  'fx-prototype': ['fx-prototype', 'design-system', 'shared'],
  shared: ['shared'],
};
test('Runtime layers stay independent of other products, engines and test fixtures', () => {
  for (const [layer, imports] of Object.entries(allowed)) {
    const root = new URL(`../${layer}/`, import.meta.url);
    for (const file of readdirSync(root).filter(name => /\.(tsx?|css)$/.test(name) && !name.endsWith('.test.ts'))) {
      const text = readFileSync(new URL(file, root),'utf8');
      for (const m of text.matchAll(/(?:\bfrom\s+|\bimport\s*\(?\s*)['"](\.\.\/[^'"]+)['"]/g)) {
        // Only the Console may bundle this single documentation asset; it is not runtime native code.
        if (layer === 'design-system-console' && file === 'BindingAudit.tsx' && m[1] === '../../../../docs/ui/vox-music/UI_BINDING_MATRIX.csv?raw') continue;
        const target = m[1].split('/')[1];
        assert(imports.includes(target),`${layer}/${file} cannot import ${m[1]}`);
      }
    }
  }
});
