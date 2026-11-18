import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createCurve, evaluate, insertNode, moveNodes } from '../design-system/curves';
import { defaultProgram, demoPrograms, durationSeconds, effectiveValue, fxLimits, generate, validateProgram } from './model';
import { pushEvent, releaseEvent } from './trigger';
import type { UIEvent } from './trigger';

test('Program shape and generated recipes are valid across the demo fixtures', () => {
  const p = defaultProgram();
  assert.equal(p.layers.length,4); assert.equal(p.curves.length,20); assert.equal(p.slots.length,4); assert.equal(p.macros.length,8);
  for (const fixture of demoPrograms) assert(validateProgram(JSON.parse(JSON.stringify(fixture.program))),fixture.program.name);
  const locks = {layers:true,curves:true,routing:true};
  const next = generate(p,locks);
  assert.equal(next.layers,p.layers); assert.equal(next.curves,p.curves); assert.equal(next.routes,p.routes);
  assert.deepEqual(generate(p,{layers:false,curves:false,routing:false}),generate(p,{layers:false,curves:false,routing:false}));
  assert.equal(durationSeconds({...p,lengthMode:'bars',bars:16}),32);
});
test('Malformed Program schema rejects overflow, duplicated nodes, cycles and unsupported values', () => {
  const p = defaultProgram();
  assert.equal(validateProgram({...p,version:2}),null);
  assert.equal(validateProgram({...p,seconds:Infinity}),null);
  assert.equal(validateProgram({...p,curves:p.curves.slice(0,19)}),null);
  assert.equal(validateProgram({...p,curves:[[...p.curves[0],p.curves[0][0]],...p.curves.slice(1)]}),null);
  assert.equal(validateProgram({...p,routes:Array.from({length:65},(_,i)=>({...p.routes[0],id:i+1}))}),null);
  assert.equal(validateProgram({...p,routes:[{...p.routes[0],target:'LFO 1'}]}),null);
  assert.equal(validateProgram({...p,layers:[{...p.layers[0],cutoff:-17},...p.layers.slice(1)]}),null);
});
test('Stable endpoints, curve interpolation, node limit and modulation remain non-destructive', () => {
  const c = createCurve([[0,0],[.5,.5],[1,1]]); c[0].shape = 'hold';
  assert.equal(evaluate(c,.39),0); assert.equal(evaluate(c,.5),.5);
  const moved = moveNodes(c,[0,1],.25,.1,true);
  assert.equal(moved[0].x,0); assert.equal(moved[1].x,.75); assert.equal(c[1].x,.5);
  assert.equal(insertNode(c,.5001,.7),c);
  const p = defaultProgram(); const before = p.layers[0].cutoff;
  const effective = effectiveValue(p,0,'cutoff',{time:1,phase:.5,velocity:100});
  assert.equal(p.layers[0].cutoff,before); assert.notEqual(effective,before);
});
test('Bounded visual trigger policies: overlap, retrigger, ignore and gate release', () => {
  const p = defaultProgram();
  let events: UIEvent[] = [];
  for (let i=1;i<=12;i++) events = pushEvent(events,p,.001*i,i,48,100,'Pattern');
  assert.equal(events.length,fxLimits.events); assert.equal(events[0].id,5);
  const ignored = pushEvent(events,{...p,triggerMode:'Ignore'},.5,40,48,100,'Keyboard');
  assert.deepEqual(ignored,events);
  const retrigger = pushEvent(events,{...p,triggerMode:'Retrigger'},.5,41,48,100,'Keyboard');
  assert.equal(retrigger.length,1); assert.equal(retrigger[0].id,41);
  const gated = pushEvent([],{...p,triggerMode:'Gate'},0,42,48,100,'Keyboard');
  assert.equal(releaseEvent(gated,48,'Pattern').length,1); assert.equal(releaseEvent(gated,48,'Keyboard').length,0);
});
