import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createPattern, editPattern, initialProject, lengths } from './model';
import { donorSamples, renderDonorSample, SR } from './donor-samples';
import { replacePattern, transformPattern } from './pattern-tools';
import { delaySeconds } from './delay';
import { canNormalize, samplePeakMultiplier } from './samples';

test('All 24 donor recipes render distinct, deterministic, finite procedural PCM', () => {
  assert.equal(donorSamples.length, 24);
  assert.equal(new Set(donorSamples.map(x => x.id)).size, 24);
  const seen = new Set<string>();
  for (const { id } of donorSamples) {
    const pcm = renderDonorSample(id)!;
    assert(pcm.length > SR / 10 && pcm.length <= SR * 2);
    let peak = 0;
    for (const v of pcm) { assert(Number.isFinite(v)); peak = Math.max(peak, Math.abs(v)); }
    assert(peak > .5 && peak <= 1);
    assert(pcm.at(-1) === 0);
    assert.deepEqual(renderDonorSample(id), pcm);
    seen.add(`${pcm.length}:${pcm[5]}:${pcm[13]}`);
  }
  assert.equal(seen.size, 24);
  assert.equal(renderDonorSample('0:9'), null);
});

test('Normalize adjusts full source peak before playback gain; silent and tiny files stay bounded', () => {
  assert.equal(canNormalize(0), false);
  assert.equal(samplePeakMultiplier(0, true), 1);
  assert.equal(samplePeakMultiplier(1e-30, true), 1);
  assert.equal(samplePeakMultiplier(.25, false), 1);
  assert.equal(samplePeakMultiplier(.25, true) * .25, 1);
});

test('Pattern transforms use visible length, full hit data and fixed generator lock positions', () => {
  const hit = { velocity: 92, probability: 47, ratchet: 3, micro: -4.1 };
  for (const length of lengths) {
    const p = { ...createPattern(), length };
    p.tracks[0][0] = hit;
    p.tracks[0][63] = length < 64 ? hit : null;
    p.stepLocks[0][0] = true;
    p.laneLocks[0] = true;
    assert.equal(transformPattern(p, 'left', 0).tracks[0][length - 1], hit);
    const right = transformPattern(p, 'right', 0);
    assert.equal(right.tracks[0][1], hit);
    assert.equal(right.tracks[0][0], null);
    assert.equal(right.tracks[0][63], length < 64 ? hit : null);
    assert.equal(right.tracks[1], p.tracks[1]);
    assert.equal(right.stepLocks, p.stepLocks);
    assert.equal(right.laneLocks, p.laneLocks);
    assert.equal(transformPattern(p, 'reverse', 0).tracks[0][length - 1], hit);
    assert.equal(transformPattern(p, 'double', 0).tracks[0][length / 2], hit);
    const invert = transformPattern(p, 'invert', 1);
    assert.equal(invert.tracks[0], p.tracks[0]);
    assert.equal(invert.tracks[1].slice(0,length).filter(Boolean).length, length);
    assert.equal(replacePattern(p, structuredClone(p)), p);
  }
});

test('A no-op transformation or identical paste does not create a Song override', () => {
  const initial = initialProject();
  const project = { ...initial, song: initial.song.map((bar, i) => i === 0 ? { ...bar, source: { slot: 3, variation: 'A' as const } } : bar) };
  const target = { kind: 'bar' as const, bar: 0 };
  assert.equal(editPattern(project, target, old => transformPattern(old, 'reverse', 0)), project);
  assert.equal(editPattern(project, target, old => replacePattern(old, structuredClone(old))), project);
  const edited = editPattern(project, target, old => transformPattern(old, 'invert', 0));
  assert(edited.song[0].override);
  assert.equal(edited.patterns, project.patterns);
  assert.equal(edited.song[1], project.song[1]);
});

test('Donor delay divisions map to seconds at local tempo in the supported range', () => {
  assert.equal(delaySeconds('1/16', 120), .125);
  assert.equal(delaySeconds('1/8', 120), .25);
  assert.equal(delaySeconds('1/8 dotted', 120), .375);
  assert.equal(delaySeconds('1/4', 120), .5);
  assert.equal(delaySeconds('1/4', 50), 1.2);
  assert.equal(delaySeconds('1/4', 220), 60 / 220);
});
