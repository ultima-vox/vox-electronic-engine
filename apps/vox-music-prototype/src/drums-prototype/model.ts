import { clamp, linear } from '../design-system/parameters';
import type { Parameter } from '../design-system/parameters';
import type { DelayDivision } from './delay';
import { padColours } from '../design-system/tokens';

export const lengths = [16, 32, 64] as const;
export type Length = typeof lengths[number];
export type Variation = 'A' | 'B';
export type PatternKey = { slot: number; variation: Variation };
export type PatternRef = (PatternKey & { kind: 'source' }) | { kind: 'bar'; bar: number };
export type Context = { kind: 'pad'; pad: number } | { kind: 'steps'; pad: number; target: PatternRef; indices: number[] } | { kind: 'song'; bar: number };
export type Source = 'Synth' | 'Sample' | 'Layer';
export type Sample = { name: string; buffer: AudioBuffer; reverseBuffer: AudioBuffer; peaks: [number, number][]; peak: number; donorId?: string };
export type Player = { sample: Sample | null; start: number; end: number; reverse: boolean; normalize: boolean; loop: boolean; gain: number };
export type PadParameter = 'level' | 'pan' | 'tune' | 'decay' | 'cutoff' | 'drive' | 'send1' | 'send2';
export type PadData = Record<PadParameter, number> & { name: string; note: number; mute: boolean; solo: boolean; out: number; source: Source; player: Variation; players: Record<Variation, Player>; choke: number };
export type Step = { velocity: number; probability: number; ratchet: number; micro: number };
export type Pattern = { length: Length; tracks: (Step | null)[][]; laneLocks: boolean[]; stepLocks: boolean[][] };
export type Bar = { source: PatternKey; override: Pattern | null };
export type DrumProject = { pads: PadData[]; kit: string; kitLevel: number; songLength: Length; song: Bar[]; patterns: Record<Variation, Pattern>[]; delayReturn: number; reverbReturn: number; delayFeedback: number; delayDivision: DelayDivision; polyphony: number };

// Monotonic kit/pattern identities for the browser fixture, not native ParameterIDs.
export const voices = [
  { name: 'Kick', group: 'kick', wave: 'sine', hz: 53, noise: 0, sweep: 5.5, level: .85, decay: .38 },
  { name: 'Snare', group: 'snare', wave: 'triangle', hz: 174, noise: .73, sweep: .4, level: .6, decay: .25 },
  { name: 'Clap', group: 'snare', wave: 'triangle', hz: 300, noise: 1, sweep: 0, level: .55, decay: .2 },
  { name: 'Rim', group: 'snare', wave: 'triangle', hz: 565, noise: .21, sweep: .25, level: .52, decay: .094 },
  { name: 'Closed Hat', group: 'cymbal', wave: 'square', hz: 831, noise: .94, sweep: 0, level: .54, decay: .071 },
  { name: 'Open Hat', group: 'cymbal', wave: 'square', hz: 711, noise: .97, sweep: 0, level: .53, decay: .31 },
  { name: 'Shaker', group: 'cymbal', wave: 'triangle', hz: 2420, noise: .96, sweep: 0, level: .49, decay: .14 },
  { name: 'Ride', group: 'cymbal', wave: 'square', hz: 614, noise: .8, sweep: .1, level: .43, decay: .79 },
  { name: 'Low Tom', group: 'tom', wave: 'sine', hz: 83, noise: .06, sweep: 1.1, level: .66, decay: .42 },
  { name: 'Mid Tom', group: 'tom', wave: 'sine', hz: 123, noise: .08, sweep: 1.2, level: .65, decay: .34 },
  { name: 'High Tom', group: 'tom', wave: 'sine', hz: 169, noise: .09, sweep: 1.4, level: .61, decay: .27 },
  { name: 'Crash', group: 'cymbal', wave: 'triangle', hz: 516, noise: .98, sweep: .1, level: .50, decay: 1.05 },
  { name: 'Cowbell', group: 'perc', wave: 'square', hz: 557, noise: .04, sweep: 0, level: .49, decay: .16 },
  { name: 'Perc', group: 'perc', wave: 'triangle', hz: 313, noise: .26, sweep: .7, level: .57, decay: .14 },
  { name: 'Tambourine', group: 'perc', wave: 'triangle', hz: 1163, noise: .89, sweep: 0, level: .44, decay: .23 },
  { name: 'Clave', group: 'perc', wave: 'sine', hz: 1327, noise: .12, sweep: .12, level: .46, decay: .085 },
] as const;
export const padColor = (index: number) => padColours[voices[index].group];
export const outputName = (index: number) => index ? `Out ${index + 1}` : 'Main';
export const kitNames = ['Analog Motion', 'Dry Circuit', 'Soft Relay'] as const;
export const baseStep: Step = { velocity: 100, probability: 100, ratchet: 1, micro: 0 };
export const stepSpecs: Record<keyof Step, Parameter> = {
  velocity: { ...linear('velocity','Velocity',1,127,100,1), keyStep: 4 },
  probability: { ...linear('probability','Probability',0,100,100,1,'%'), keyStep: 5 },
  ratchet: linear('ratchet','Ratchet',1,4,1,1,'×'),
  micro: { ...linear('micro','Microtiming',-25,25,0,.1,'%'), keyStep: 1 },
};
export function drumSpec(index: number, key: PadParameter): Parameter {
  const id = `demo.pad.${index + 1}.${key}`; const voice = voices[index];
  switch (key) {
    case 'level': return { ...linear(id,'Level',0,1,voice.level,.001), keyStep: .02, format: n => `${Math.round(n * 100)}%` };
    case 'pan': return { ...linear(id,'Pan',-1,1,0,.001), keyStep: .05, format: n => Math.abs(n) < .005 ? 'Center' : `${n < 0 ? 'L' : 'R'} ${Math.round(Math.abs(n) * 100)}` };
    case 'tune': return { ...linear(id,'Tune',-24,24,0,.01,'st'), keyStep: 1 };
    case 'decay': return { ...linear(id,'Decay',.01,10,voice.decay,.001,'s'), keyStep: .025, scale: 'log', format: n => n < 1 ? `${Math.round(n * 1000)} ms` : `${n.toFixed(2)} s` };
    case 'cutoff': return { ...linear(id,'Cutoff',20,20000,20000,1,'Hz'), scale: 'log', keyStep: 120 };
    case 'drive': return { ...linear(id,'Drive',0,100,0,1,'%'), keyStep: 5 };
    case 'send1': case 'send2': return { ...linear(id,key === 'send1' ? 'Delay Send' : 'Reverb Send',0,1,0,.001), keyStep: .05, format: n => `${Math.round(n * 100)}%` };
  }
}
export function defaultPad(index: number): PadData {
  const emptyPlayer = (): Player => ({ sample: null, start: 0, end: 1, reverse: false, normalize: false, loop: false, gain: 1 });
  const voice = voices[index];
  return { name: voice.name, note: 36 + index, out: 0, mute: false, solo: false, source: 'Synth', player: 'A', players: { A: emptyPlayer(), B: emptyPlayer() }, choke: index === 4 || index === 5 ? 1 : 0,
    level: voice.level, tune: 0, decay: voice.decay, pan: 0, cutoff: 20000, drive: 0, send1: 0, send2: 0 };
}
export function createKit(name: string) {
  return voices.map((voice, index) => {
    const pad = defaultPad(index);
    if (name === 'Dry Circuit') return { ...pad, decay: Math.max(.01, +(voice.decay * .62).toFixed(3)), tune: index < 4 ? -2 : 2, drive: index < 4 ? 18 : 4 };
    if (name === 'Soft Relay') return { ...pad, decay: +(voice.decay * 1.38).toFixed(3), tune: index < 4 ? -4 : -1, cutoff: index < 4 ? 8400 : 6400, send2: .19 };
    return pad;
  });
}
export function createPattern(): Pattern {
  return { length: 16, tracks: voices.map(() => Array<Step | null>(64).fill(null)), laneLocks: voices.map(() => false), stepLocks: voices.map(() => Array<boolean>(64).fill(false)) };
}
const starter: [number, number][][] = [
  [[0, 125], [4, 112], [8, 121], [12, 110]], [[4, 104], [12, 117]], [[4, 91], [12, 102]], [[3, 67], [11, 86], [14, 55]],
  [[0, 89], [2, 115], [4, 73], [6, 105], [8, 91], [10, 116], [12, 75], [14, 104]], [[6, 96], [14, 99]], [[1, 76], [5, 86], [9, 76], [13, 92]], [[0, 70], [8, 77]],
  [[9, 88]], [[11, 86]], [[14, 75], [15, 105]], [[0, 74]], [[3, 84], [10, 78]], [[5, 83], [9, 72], [15, 106]], [[6, 76], [13, 79]], [[7, 82], [15, 95]],
];
function demoPattern(slot: number, variation: Variation) {
  const result = createPattern();
  const offset = variation === 'B' ? 1 : 0;
  if (slot === 3) return result;
  starter.forEach((steps, lane) => steps.forEach(([grid, velocity], i) => {
    if (slot === 1 && lane > 6 && i % 2 === 0) return;
    if (slot === 2 && lane > 2 && lane % 2 === 0) return;
    const step = (grid + (lane < 3 ? 0 : offset) + (slot === 2 && lane === 0 && i % 2 ? 2 : 0)) % 16;
    result.tracks[lane][step] = { ...baseStep, velocity: velocity - (offset && i % 2 ? 12 : 0) };
  }));
  return result;
}
export function initialProject(): DrumProject {
  return { kit: kitNames[0], pads: createKit(kitNames[0]), kitLevel: .78, songLength: 16, delayReturn: .55, reverbReturn: .52, delayFeedback: .28, delayDivision: '1/8', polyphony: 48,
    patterns: Array.from({ length: 4 }, (_, s) => ({ A: demoPattern(s, 'A'), B: demoPattern(s, 'B') })),
    song: Array.from({ length: 64 }, (_, n) => ({ source: { slot: n % 8 === 7 ? 2 : n % 8 < 4 ? 0 : 1, variation: n % 4 === 3 ? 'B' : 'A' }, override: null })),
  };
}
export const sourceRef = (key: PatternKey): PatternRef => ({ kind: 'source', ...key });
export const patternName = (key: PatternKey) => `P${String(key.slot + 1).padStart(2, '0')} · ${key.variation}`;
export function patternRefEqual(a: PatternRef, b: PatternRef) {
  return a.kind === 'bar' ? b.kind === 'bar' && a.bar === b.bar : b.kind === 'source' && a.slot === b.slot && a.variation === b.variation;
}
export function getPattern(project: DrumProject, ref: PatternRef): Pattern | null {
  if (ref.kind === 'source') return project.patterns[ref.slot]?.[ref.variation] ?? null;
  if (ref.bar >= project.songLength) return null;
  const bar = project.song[ref.bar];
  if (!bar) return null;
  return bar.override ?? getPattern(project, sourceRef(bar.source));
}
export function editPattern(project: DrumProject, ref: PatternRef, edit: (p: Pattern) => Pattern): DrumProject {
  const before = getPattern(project, ref);
  if (!before) return project;
  const next = edit(before);
  if (next === before) return project;
  if (ref.kind === 'source') return { ...project, patterns: project.patterns.map((pair, i) => i === ref.slot ? { ...pair, [ref.variation]: next } : pair) };
  return { ...project, song: project.song.map((bar, i) => i === ref.bar ? { ...bar, override: next } : bar) };
}
export function editPad(project: DrumProject, index: number, update: (pad: PadData) => PadData) {
  const pad = project.pads[index]; if (!pad) return project;
  const next = update(pad); return next === pad ? project : { ...project, pads: project.pads.map((item, n) => n === index ? next : item) };
}
export function editBar(project: DrumProject, bar: number, update: (value: Bar) => Bar) {
  if (bar < 0 || bar >= project.songLength) return project;
  const before = project.song[bar]; const next = update(before);
  return next === before ? project : { ...project, song: project.song.map((item, i) => i === bar ? next : item) };
}
export function editSteps(p: Pattern, pad: number, indices: number[], edited: (step: Step | null, index: number) => Step | null) {
  const indicesSet = new Set(indices.filter(i => i >= 0 && i < p.length));
  let changed = false;
  const tracks = p.tracks.map((track, i) => i !== pad ? track : track.map((step, s) => {
    if (!indicesSet.has(s)) return step;
    const next = edited(step, s); changed ||= next !== step; return next;
  }));
  return changed ? { ...p, tracks } : p;
}
export function hashUnit(seed: number) {
  let n = seed | 0;
  n = Math.imul(n ^ (n >>> 16), 0x7feb352d); n = Math.imul(n ^ (n >>> 15), 0x846ca68b);
  return ((n ^ (n >>> 16)) >>> 0) / 4294967296;
}
export const styles = ['Full-on / Psy', 'Goa', 'House', 'Techno', 'Trap', 'Breaks', 'Ambient', 'Uplifting Trance', 'Anthem Trance', 'Dark Psy', 'Melodic Trance', 'Hard Rock', 'Heavy Metal', 'Symphonic Metal', 'Nordic Metal', 'Tech House', 'Drum & Bass', 'Melodic Drum & Bass', 'Melodic Techno', 'Progressive Trance', 'Dream Trance', 'Hard Trance', 'Symphonic Trance'] as const;
export const generatorAmounts = ['Density', 'Groove', 'Syncopation', 'Swing', 'Humanize', 'Complexity', 'Fills', 'Variation'] as const;
export type Amount = typeof generatorAmounts[number];
export type DraftSettings = Record<Amount, number> & { seed: number };
export const draftDefaults: DraftSettings = { seed: 42, Density: 45, Groove: 58, Syncopation: 30, Swing: 12, Humanize: 16, Complexity: 36, Fills: 18, Variation: 24 };
export function draftGrid(pattern: Pattern, d: DraftSettings): Pattern {
  // This is a bounded GUI fixture, not a genre algorithm. Style is deliberately not an input.
  const quarter = pattern.length / 4;
  return { ...pattern, tracks: pattern.tracks.map((lane, pad) => pattern.laneLocks[pad] ? lane : lane.map((step, i) => {
    if (i >= pattern.length || pattern.stepLocks[pad][i]) return step;
    const seed = d.seed * 311 + i * 67 + pad * 29;
    const beat = i % quarter === 0;
    const foundation = pad === 0 && beat || pad === 1 && (i === quarter || i === quarter * 3) || pad === 4 && i % (quarter / 2) === 0;
    const chance = (d.Density / 125) * (i % 2 ? d.Syncopation / 100 : 1) * (pad > 5 ? d.Complexity / 160 : .9) + (i >= pattern.length - quarter ? d.Fills / 280 : 0);
    if (!foundation && hashUnit(seed) >= chance) return null;
    return { velocity: Math.round(clamp(81 + (beat ? d.Groove * .35 : 0) + (hashUnit(seed + 7) - .5) * d.Humanize, 1, 127)), probability: 100, ratchet: !beat && hashUnit(seed + 9) < d.Variation / 400 ? 2 : 1, micro: i % 2 ? Math.min(22, d.Swing * .22) : 0 };
  })) };
}
