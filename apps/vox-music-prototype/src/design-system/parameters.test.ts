import assert from 'node:assert/strict';
import { test } from 'node:test';
import { linear, parsePlain, quantize, reset, toNormalized, toPlain, valueText } from './parameters';

test('Plain and normalized domains round trip for linear/bipolar and logarithmic controls', () => {
  const linearSpec = linear('pan','Pan',-1,1,0,.001);
  const logSpec = { ...linear('decay','Decay',.01,10,.38,.001,'s'), scale:'log' as const };
  for (const spec of [linearSpec,logSpec]) for (const n of [0,.001,.16,.5,.98,1]) assert(Math.abs(toNormalized(spec,toPlain(spec,n))-n) < 1e-9);
  assert.equal(toNormalized(linearSpec,0),.5); assert.equal(valueText(logSpec,.38),'0.38 s');
  assert.equal(quantize(logSpec,400),10); assert.equal(quantize(logSpec,NaN),.38);
});
test('Reset uses actual donor/default recipe value as one gesture, never an assumed center', () => {
  const spec = linear('level','Level',0,1,.85,.001);
  const writes: (string|number)[] = [];
  const binding = {spec,value:.33,onBegin:()=>writes.push('begin'),onEnd:()=>writes.push('end'),onChange:(v:number)=>writes.push(v)};
  reset(binding); reset({...binding, disabled:true}); reset({...binding,value:.85});
  assert.deepEqual(writes,['begin',.85,'end']);
});


test('Unit entry accepts compatible suffixes without inferring a different domain', () => {
  const hertz = linear('fc', 'Cutoff', 20, 20000, 1200, 1, 'Hz');
  const seconds = linear('decay', 'Decay', .01, 10, .4, .001, 's');
  assert.equal(parsePlain(hertz, '2,5 kHz'), 2500);
  assert.equal(parsePlain(hertz, '480 Hz'), 480);
  assert.equal(parsePlain(seconds, '125ms'), .125);
  assert.equal(parsePlain(seconds, '.3 s'), .3);
  assert.equal(parsePlain(seconds, ' -0,125 '), -.125);
  assert.equal(parsePlain(linear('p', 'Probability', 0, 100, 100, 1, '%'), '50%'), 50);
  for (const invalid of ['', '---', '3 ms', '2Hz junk', '2.5,5', '2e4', 'Infinity']) assert.equal(parsePlain(hertz, invalid), null);
  assert.equal(parsePlain({ ...hertz, unit: undefined }, '50%'), null);
});
