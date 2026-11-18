import { durationSeconds, fxLimits } from './model';
import type { Program } from './model';
export type UIEvent = { id: number; note: number; velocity: number; source: string; start: number; end: number; gate: boolean };
export function pushEvent(events: UIEvent[], p: Program, now: number, id: number, note: number, velocity: number, source: string) {
  const live = events.filter(e => e.end > now);
  if (p.triggerMode === 'Ignore' && live.length) return live;
  const remaining = p.triggerMode === 'One-shot' ? [] : p.triggerMode === 'Retrigger' ? live.filter(e => e.note !== note) : live;
  return [...remaining.slice(-(fxLimits.events - 1)), { id, note, velocity, source, start: now, end: now + durationSeconds(p), gate: p.triggerMode === 'Gate' }];
}
export const releaseEvent = (events: UIEvent[], note: number, source: string) => events.filter(event => !(event.gate && event.note === note && event.source === source));
export function triggerChance(seed: number, step: number, pass: number) {
  let x = (seed * 977 + (step + 1) * 93239 + (pass + 1) * 73709) | 0; x ^= x << 13; x ^= x >>> 17; x ^= x << 5;
  return (x >>> 0) / 4294967296 * 100;
}
