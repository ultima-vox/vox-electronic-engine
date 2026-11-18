import { useEffect, useRef, useState } from 'react';
import { divisions } from './model';
import type { Program } from './model';
import { pushEvent, releaseEvent, triggerChance } from './trigger';
import type { UIEvent } from './trigger';

export function useFXPreview(program: React.RefObject<Program>, active: boolean) {
  const repo = useRef<UIEvent[]>([]);
  const serial = useRef(0);
  const schedule = useRef({ running: false, next: 0, slot: 0, pass: 0, wait: [] as { at: number; note: number; velocity: number }[], releases: [] as { at: number; note: number }[] });
  const [running, setRunning] = useState(false);
  const [step, setStep] = useState(-1);
  const [view, setView] = useState<{ events: UIEvent[]; now: number }>({ events: [], now: 0 });
  const nowSeconds = () => performance.now() / 1000;
  function trigger(note: number, velocity = 100, source = 'Keyboard') {
    const now = nowSeconds();
    repo.current = pushEvent(repo.current, program.current, now, ++serial.current, note, velocity, source);
    setView({ events: repo.current, now });
  }
  function release(note: number, source = 'Keyboard') {
    const next = releaseEvent(repo.current, note, source);
    if (next.length !== repo.current.length) { repo.current = next; setView({ events: next, now: nowSeconds() }); }
  }
  function start() { schedule.current = { running: true, next: nowSeconds(), slot: 0, pass: 0, wait: [], releases: [] }; setRunning(true); }
  function stop() {
    schedule.current.running = false; schedule.current.wait = []; schedule.current.releases = [];
    repo.current = repo.current.filter(e => e.source !== 'Pattern' || !e.gate);
    setRunning(false); setStep(-1); setView({ events: repo.current, now: nowSeconds() });
  }
  function panic() { schedule.current.running = false; schedule.current.wait = []; schedule.current.releases = []; repo.current = []; setRunning(false); setStep(-1); setView({ events: [], now: nowSeconds() }); }
  useEffect(() => {
    if (!active) { panic(); return; }
    const clock = setInterval(() => {
      const state = schedule.current; const now = nowSeconds(); const p = program.current;
      let changed = false;
      if (state.running) {
        const count = divisions[p.division] * p.patternBars; const duration = 2 / divisions[p.division];
        if (state.next < now - .33) state.next = now;
        for (let budget = 0; budget < 16 && state.next <= now; budget++) {
          state.slot %= count;
          const n = state.slot; const hit = p.triggerGrid[n]; setStep(n);
          if (hit && triggerChance(p.seed, n, state.pass) < hit.probability) for (let note = 0; note < hit.ratchet; note++) state.wait.push({ at: state.next + note * duration / hit.ratchet, note: hit.note, velocity: hit.velocity });
          state.slot = (n + 1) % count; if (!state.slot) state.pass++;
          state.next += duration;
        }
        state.releases = state.releases.filter(e => { if (e.at > now) return true; const next = releaseEvent(repo.current, e.note, 'Pattern'); if (next.length !== repo.current.length) { repo.current = next; changed = true; } return false; });
        state.wait = state.wait.filter(e => {
          if (e.at > now) return true;
          repo.current = pushEvent(repo.current, p, e.at, ++serial.current, e.note, e.velocity, 'Pattern'); changed = true;
          if (p.triggerMode === 'Gate') state.releases.push({ at: e.at + duration * .44, note: e.note });
          return false;
        });
      }
      if (repo.current.length || changed) { repo.current = repo.current.filter(e => e.end > now); setView({ events: repo.current, now }); }
    }, 24);
    return () => clearInterval(clock);
  }, [program, active]);
  const event = view.events.at(-1);
  const phase = event ? Math.max(0, Math.min(1, (view.now - event.start) / (event.end - event.start))) : 0;
  return { events: view.events, event, phase, running, step, modInput: { time: event ? Math.max(0, view.now - event.start) : 0, phase, velocity: event?.velocity ?? 0 }, trigger, release, start, stop, panic };
}
export type FXPreview = ReturnType<typeof useFXPreview>;
