import assert from 'node:assert/strict';
import { test } from 'node:test';
import { baseStep, createPattern, draftDefaults, draftGrid, editPattern, editSteps, getPattern, initialProject, sourceRef, styles } from './model';
import { buildMidi, frame, initialCursor, stepHits } from './sequencer';
import { moveDock, parseLayout, showEditor, workspace } from './layout';

const first = sourceRef({ slot: 0, variation: 'A' });
test('16 distinct pad identities, 4 A/B sources and 16/32/64 bounds', () => {
  const p = initialProject();
  assert.equal(p.pads.length,16); assert.equal(new Set(p.pads.map(p => p.note)).size,16);
  assert.equal(p.song.length,64); assert.equal(p.songLength,16); assert.equal(p.patterns.length,4);
  for (const pair of p.patterns) { assert.notEqual(pair.A,pair.B); assert.equal(pair.A.tracks.length,16); assert.equal(pair.B.tracks[0].length,64); }
  assert.equal(styles.length,23); assert.equal(new Set(styles).size,23);
});
test('Song COW ignores no-op, isolates first edit, and leaves other linked bars on source', () => {
  const p = initialProject(); const ref = { kind: 'bar' as const, bar: 0 };
  assert.strictEqual(editPattern(p,ref,old => editSteps(old,0,[42],s => s)),p);
  assert.strictEqual(getPattern(p,ref),getPattern(p,first));
  const edit = editPattern(p,ref,old => editSteps(old,0,[0],() => null));
  assert(edit.song[0].override); assert.equal(edit.song[0].override.tracks[0][0],null);
  assert(p.song[0].override === null); assert.strictEqual(getPattern(edit,{kind:'bar',bar:1}),getPattern(edit,first)); assert(p.patterns[0].A.tracks[0][0]);
  const updated = editPattern(edit,first,old => ({ ...old,length: 64 }));
  assert.equal(getPattern(updated,ref)?.length,16); assert.equal(getPattern(updated,{kind:'bar',bar:1})?.length,64);
  assert.equal(getPattern(updated,{kind:'bar',bar:32}),null);
});
test('Step traversal and actual note frames use bounds, probability, ratchet and solo state', () => {
  const p = initialProject();
  const pattern = createPattern();
  pattern.tracks[0][0] = {...baseStep, velocity: 107, ratchet: 2, micro: 10};
  pattern.tracks[1][0] = {...baseStep, probability: 0};
  pattern.tracks[2][0] = {...baseStep}; p.pads[2].mute = true;
  assert.deepEqual(stepHits(p,pattern,0,0,0),[{pad:0,velocity:107,offset:.1},{pad:0,velocity:107,offset:.6}]);
  p.pads[3].solo = true; assert.deepEqual(stepHits(p,pattern,0,0,0),[]); p.pads[3].solo = false;
  const right = frame(p,{mode:'song',loopBar:9},{ bar:0,step:15,pass:0 });
  assert.equal(right?.bar,9); assert.deepEqual(right?.next,{bar:0,step:0,pass:1});
  for (const songLength of [16,32,64] as const) {
    const tail = frame({...p,songLength},{mode:'song',loopBar:null},{bar:songLength-1,step:15,pass:0});
    assert.deepEqual(tail?.next,{bar:0,step:0,pass:1});
  }
  assert.equal(frame(p,{mode:'pattern',ref:first},initialCursor())?.step,0);
});
test('MIDI writes a bounded SMF with the same first-pass event velocity and an end-of-track', () => {
  const p = initialProject(); p.patterns[0].A = createPattern();
  p.patterns[0].A.tracks[0][0] = {...baseStep, velocity: 112};
  const bytes = buildMidi(p,{mode:'pattern',ref:first},128);
  const b = Buffer.from(bytes);
  assert.equal(b.toString('ascii',0,4),'MThd'); assert.equal(b.toString('ascii',14,18),'MTrk');
  assert.equal(b.readUInt32BE(18),b.length-22); assert(b.includes(Buffer.from([0,0x99,36,112])));
  assert.deepEqual([...b.subarray(-3)],[255,47,0]);
});
test('Generic draft is deterministic and cannot touch lane or step locks', () => {
  const p = initialProject().patterns[0].A; p.laneLocks[0] = true; p.stepLocks[2][12] = true;
  const next = draftGrid(p,draftDefaults);
  assert.deepEqual(next,draftGrid(p,draftDefaults));
  assert.strictEqual(next.tracks[0],p.tracks[0]); assert.strictEqual(next.tracks[2][12],p.tracks[2][12]);
  assert.notDeepEqual(next.tracks[3],p.tracks[3]);
});
test('Workspace parser rejects duplicate/unknown IDs and clamps sizes without changing musical state', () => {
  const def = workspace();
  assert.deepEqual(parseLayout({ ...def, right:['pads'] }),def);
  assert.deepEqual(parseLayout({ ...def, left:['floating'] }),def);
  const moved = moveDock(def,'pads','right');
  assert.deepEqual(moved.left,[]); assert.deepEqual(moved.right,['inspector','pads']); assert.equal(moved.active.right,'pads');
  assert.equal(moveDock(moved,'pads',null).active.right,'inspector');
  const layout = parseLayout({ ...moved,leftWidth:1,rightWidth:10000,ratio:93,primary:'song',secondary:'song',pin:{kind:'pad',pad:-1} });
  assert.equal(layout.leftWidth,180); assert.equal(layout.rightWidth,340); assert.equal(layout.ratio,70); assert.equal(layout.secondary,null); assert.equal(layout.pin,null);
  assert.deepEqual(parseLayout(JSON.parse(JSON.stringify(workspace('Composition')))),workspace('Composition'));
  assert.equal(showEditor(workspace('Composition'),'step','primary').secondary,'song');
});
