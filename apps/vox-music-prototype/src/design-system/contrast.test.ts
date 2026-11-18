import assert from 'node:assert/strict';
import { test } from 'node:test';
import { contrastRatio } from './contrast';
import { surfaces, themes } from './tokens';

test('WCAG sRGB contrast computation is symmetric and uses all three product accents', () => {
  assert.equal(contrastRatio('#000000', '#ffffff'), 21);
  assert.equal(contrastRatio('#aaaaaa', '#aaaaaa'), 1);
  assert.equal(contrastRatio('#118899', '#3d3d3d'), contrastRatio('#3d3d3d', '#118899'));
  const panel = surfaces.find(([name]) => name === 'Panel')![2];
  for (const theme of Object.values(themes)) assert(contrastRatio(theme.accent, panel) >= 3);
});
