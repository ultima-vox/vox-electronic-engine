import { useEffect, useRef, useState } from 'react';
import type { MutableRefObject } from 'react';
import { PreviewEngine, idleLevels } from './engine';
import type { DrumProject } from './model';
import { frame, initialCursor, stepHits } from './sequencer';
import type { Target } from './sequencer';

type Position = { bar: number | null; step: number; mode: Target['mode'] };
type Timed = { time: number; pad: number; velocity: number };
export function usePreview(project: MutableRefObject<DrumProject>, changeToken: DrumProject, target: Target, bpm: number) {
  const engine = useRef<PreviewEngine | null>(null);
  const refs = useRef({ target, bpm }); refs.current = { target, bpm };
  const clock = useRef({ cursor: initialCursor(), nextAt: 0 });
  const scheduled = useRef<{ time: number; position: Position }[]>([]);
  const pulseQueue = useRef<Timed[]>([]);
  const hits = useRef<Timed[]>([]);
  const serial = useRef(0);
  const playing = useRef(false);
  const [state, setState] = useState<'ready'|'running'|'error'>('ready');
  const [error, setError] = useState('');
  const [running, setRunning] = useState(false);
  const [position, setPosition] = useState<Position | null>(null);
  const [levels, setLevels] = useState(idleLevels);
  const [pulses, setPulses] = useState<Record<number, number>>({});
  const [lastHit, setLastHit] = useState<{ pad: number; velocity: number } | null>(null);
  const getEngine = () => engine.current ??= new PreviewEngine();
  const report = (err: unknown) => { setState('error'); setError(err instanceof Error ? err.message : 'Web Audio could not start.'); };
  useEffect(() => { engine.current?.sync(changeToken, bpm); }, [changeToken, bpm]);
  const stop = () => {
    serial.current++; playing.current = false; engine.current?.cancelScheduled(); scheduled.current = []; pulseQueue.current = [];
    setRunning(false); setPosition(null);
  };
  const start = async () => {
    const id = ++serial.current;
    try {
      const a = getEngine(); await a.arm(); if (id !== serial.current) return;
      a.sync(project.current, refs.current.bpm); setState('running');
      playing.current = true; clock.current = { cursor: initialCursor(), nextAt: a.context.currentTime + .06 }; setRunning(true);
    } catch (err) { report(err); }
  };
  const audition = async (pad: number, velocity = 100) => {
    try {
      const a = getEngine(); const id = serial.current; await a.arm(); if (id !== serial.current) return;
      setState('running'); const current = project.current; a.sync(current, refs.current.bpm);
      const time = a.context.currentTime + .008;
      if (a.trigger(current, pad, velocity, time)) {
        hits.current.push({ pad, velocity, time }); setLastHit({ pad, velocity }); setPulses(Object.fromEntries(hits.current.map(hit => [hit.pad, hit.velocity])));
      }
    } catch (err) { report(err); }
  };
  const panic = () => { stop(); engine.current?.panic(project.current, refs.current.bpm); hits.current = []; setPulses({}); };
  const decode = async (file: File) => getEngine().decode(file);
  useEffect(() => {
    let visible: Position | null = null;
    let previousPulse = '';
    let previousLevel = idleLevels;
    const timer = window.setInterval(() => {
      const a = engine.current;
      if (!a || a.context.state !== 'running') return;
      const time = a.context.currentTime; const data = project.current;
      if (playing.current) {
        const { target, bpm } = refs.current;
        if (clock.current.nextAt < time - .2) clock.current.nextAt = time + .042;
        for (let budget = 0; clock.current.nextAt < time + .115 && budget < 16; budget++) {
          const f = frame(data, target, clock.current.cursor);
          if (!f) {
            playing.current = false; a.cancelScheduled(); scheduled.current = []; pulseQueue.current = [];
            visible = null; setPosition(null); setRunning(false); break;
          }
          const seconds = 60 / bpm * 4 / f.pattern.length;
          const at = clock.current.nextAt;
          const bar = f.bar ?? (target.mode === 'pattern' && target.ref.kind === 'bar' ? target.ref.bar : 0);
          for (const hit of stepHits(data, f.pattern, f.step, bar, f.pass)) {
            const noteTime = Math.max(time + .002, at + hit.offset * seconds);
            if (a.trigger(data, hit.pad, hit.velocity, noteTime, true)) pulseQueue.current.push({ pad: hit.pad, velocity: hit.velocity, time: noteTime });
          }
          scheduled.current.push({ time: at, position: { bar: f.bar, step: f.step, mode: target.mode } });
          clock.current = { cursor: f.next, nextAt: at + seconds };
        }
      } else { visible = null; }
      while (scheduled.current[0]?.time <= time) visible = scheduled.current.shift()!.position;
      if (visible) setPosition(visible);
      const due = pulseQueue.current.filter(h => h.time <= time);
      pulseQueue.current = pulseQueue.current.filter(h => h.time > time);
      if (due.length) { hits.current.push(...due); const latest = due[due.length - 1]; setLastHit({ pad: latest.pad, velocity: latest.velocity }); }
      hits.current = hits.current.filter(h => h.time + .13 >= time);
      const pulse = Object.fromEntries(hits.current.map(hit => [hit.pad, hit.velocity]));
      const tag = JSON.stringify(pulse);
      if (tag !== previousPulse) { previousPulse = tag; setPulses(pulse); }
      const current = a.read();
      const silent = (v: typeof idleLevels) => v.voices === 0 && !v.over && [...v.pads, ...v.buses, ...v.returns, ...v.main].every(p => p <= .001);
      if (!silent(current) || !silent(previousLevel)) setLevels(current);
      previousLevel = current;
    }, 24);
    return () => { clearInterval(timer); serial.current++; playing.current = false; engine.current?.dispose(); engine.current = null; };
  }, [project]);
  return { state, error, running, position, levels, pulses, lastHit, start, stop, panic, audition, decode, clearOver: () => { engine.current?.clearOver(); setLevels(old => old.over ? { ...old, over: false } : old); } };
}
export type Preview = ReturnType<typeof usePreview>;
