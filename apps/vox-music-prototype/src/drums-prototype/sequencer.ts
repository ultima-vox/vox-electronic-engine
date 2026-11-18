import { getPattern, hashUnit, sourceRef } from './model';
import type { DrumProject, Pattern, PatternRef } from './model';

export type Target = { mode: 'pattern'; ref: PatternRef } | { mode: 'song'; loopBar: number | null };
export type Cursor = { bar: number; step: number; pass: number };
export type Hit = { pad: number; velocity: number; offset: number };
export const initialCursor = (): Cursor => ({ bar: 0, step: 0, pass: 0 });
export function frame(project: DrumProject, target: Target, cursor: Cursor): { pattern: Pattern; step: number; bar: number | null; next: Cursor; pass: number } | null {
  const bar = target.mode === 'song' ? Math.min(target.loopBar ?? cursor.bar, project.songLength - 1) : null;
  const p = getPattern(project, target.mode === 'pattern' ? target.ref : { kind: 'bar', bar: bar ?? 0 });
  if (!p) return null;
  const step = cursor.step % p.length;
  let next: Cursor = { ...cursor, step: step + 1 };
  if (next.step >= p.length) {
    if (bar === null || (target.mode === 'song' && target.loopBar !== null) || bar + 1 >= project.songLength) next = { bar: 0, step: 0, pass: cursor.pass + 1 };
    else next = { ...next, bar: bar + 1, step: 0 };
  }
  return { pattern: p, step, bar, next, pass: cursor.pass };
}
export function hasSignal(project: DrumProject, index: number) {
  const pad = project.pads[index];
  return !!pad && !pad.mute && (!project.pads.some(p => p.solo) || pad.solo);
}
export function stepHits(project: DrumProject, pattern: Pattern, step: number, bar: number, pass: number): Hit[] {
  const hits: Hit[] = [];
  pattern.tracks.forEach((track, pad) => {
    const entry = track[step];
    if (!entry || !hasSignal(project, pad)) return;
    if (entry.probability < 100 && hashUnit(1 + pad * 389 + step * 5999 + bar * 30377 + pass * 9449) * 100 >= entry.probability) return;
    for (let r = 0; r < entry.ratchet; r++) hits.push({ pad, velocity: entry.velocity, offset: r / entry.ratchet + entry.micro / 100 });
  });
  return hits.sort((a, b) => a.offset - b.offset);
}

export function buildMidi(project: DrumProject, target: Target, bpm: number) {
  const division = 480;
  const events: { tick: number; on: boolean; note: number; velocity: number }[] = [];
  const bars = target.mode === 'song' && target.loopBar === null ? project.songLength : 1;
  for (let i = 0; i < bars; i++) {
    const bar = target.mode === 'song' ? Math.min(target.loopBar ?? i, project.songLength - 1) : target.ref.kind === 'bar' ? target.ref.bar : 0;
    const ref = target.mode === 'pattern' ? target.ref : sourceRef(project.song[bar].source);
    const pattern = target.mode === 'song' ? project.song[bar].override ?? getPattern(project, ref) : getPattern(project, ref);
    if (!pattern) continue;
    const stepTick = 4 * division / pattern.length;
    for (let n = 0; n < pattern.length; n++) {
      for (const hit of stepHits(project, pattern, n, bar, 0)) {
        const tick = Math.max(0, Math.round(i * division * 4 + (n + hit.offset) * stepTick));
        const note = project.pads[hit.pad].note;
        events.push({ tick, on: true, note, velocity: hit.velocity }, { tick: tick + Math.max(1, Math.floor(stepTick / 5)), on: false, note, velocity: 0 });
      }
    }
  }
  const tempo = Math.round(60000000 / bpm);
  const bytes = [0, 255, 81, 3, (tempo >> 16) & 255, (tempo >> 8) & 255, tempo & 255, 0, 255, 88, 4, 4, 2, 24, 8];
  function delta(n: number) {
    const vlq = [n & 127];
    while ((n >>= 7) > 0) vlq.unshift((n & 127) | 128);
    return vlq;
  }
  let last = 0;
  for (const e of events.sort((a, b) => a.tick - b.tick || Number(a.on) - Number(b.on))) {
    bytes.push(...delta(e.tick - last), e.on ? 0x99 : 0x89, e.note, e.velocity); last = e.tick;
  }
  bytes.push(...delta(Math.max(0, bars * 4 * division - last)), 255, 47, 0);
  const size = bytes.length;
  return new Uint8Array([77, 84, 104, 100, 0, 0, 0, 6, 0, 0, 0, 1, division >> 8, division & 255, 77, 84, 114, 107, size >>> 24, size >>> 16 & 255, size >>> 8 & 255, size & 255, ...bytes]);
}
