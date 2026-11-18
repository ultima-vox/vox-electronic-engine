import { createCurve, evaluate, shapes } from '../design-system/curves';
import type { Curve } from '../design-system/curves';
import { clamp, linear, toNormalized, toPlain, unit01 } from '../design-system/parameters';
import type { Parameter } from '../design-system/parameters';

export const layerLetters = ['A','B','C','D'] as const;
export const curveNames = ['Amp','Pitch','Cutoff','Pan'] as const;
export const families = ['Noise Riser','Downlifter','Tonal Sweep / Shepard','Whoosh / Doppler','Impact / Boom','Zap / Laser','Psy Blip / Squelch','Metallic / Resonant','Texture / Drone','Granular Cloud','Glitch / Stutter','Formant / Alien'] as const;
export const categories = ['Riser','Downlifter','Whoosh','Impact','Tonal','Texture'] as const;
export const styles = ['Psy','Goa','Techno','Progressive','Ambient','Cinematic'] as const;
export const effectTypes = ['Off','EQ / Filter','Drive','Wavefold','Phaser','Flanger','Chorus','Delay','Reverb','Bitcrush','Ring Mod','Trance Gate'] as const;
export const triggerModes = ['One-shot','Gate','Retrigger','Overlap','Ignore'] as const;
export const syncChoices = ['1/4','1/8','1/16','1/32','1/8T','1/16T'] as const;
export const divisions: Record<typeof syncChoices[number], number> = { '1/4': 4, '1/8': 8, '1/16': 16, '1/32': 32, '1/8T': 12, '1/16T': 24 };
export const waves = ['Sine','Triangle','Saw','Square'] as const;
export const layerParams = ['gain','pan','width','pitch','cutoff','offset','duration'] as const;
export type LayerParam = typeof layerParams[number];
export const layerSpecs: Record<LayerParam, Parameter> = {
  gain: linear('gain','Gain',0,100,65,1,'%'), pan: linear('pan','Pan',-100,100,0,1,'%'), width: linear('width','Width',0,200,100,1,'%'),
  pitch: linear('pitch','Pitch',-36,36,0,.1,'st'), cutoff: { ...linear('cutoff','Cutoff',20,20000,5000,1,'Hz'), scale: 'log', keyStep: 100 },
  offset: linear('offset','Offset',0,90,0,1,'%'), duration: linear('duration','Duration',5,100,100,1,'%'),
};
export const macroSources = Array.from({ length: 8 }, (_, i) => `Macro ${i + 1}`);
export const sources = ['LFO 1','LFO 2','Step envelope','Random S&H','Triggered ADSR','Event phase','Velocity','Self follower',...macroSources];
export const destinations = layerLetters.flatMap((letter, i) => (['gain','pan','pitch','cutoff','width'] as const).map(field => ({ id: `${letter}.${field}`, label: `${letter} · ${layerSpecs[field].label}`, layer: i, field })));
export const fxLimits = { layers: 4, curves: 20, macros: 8, routes: 64, slots: 4, events: 8, grains: 64, grid: 128 } as const;
export type Family = typeof families[number];
// Distinct family fields survive a family switch; no generic "character" maps to unrelated semantics.
export const familyControls = {
  'Noise Riser': { key: 'noiseBlend', label: 'Noise blend' }, 'Downlifter': { key: 'fallDepth', label: 'Fall depth' },
  'Tonal Sweep / Shepard': { key: 'octaveSpread', label: 'Octave spread' }, 'Whoosh / Doppler': { key: 'dopplerDepth', label: 'Doppler depth' },
  'Impact / Boom': { key: 'subBody', label: 'Sub body' }, 'Zap / Laser': { key: 'fmDepth', label: 'FM depth' },
  'Psy Blip / Squelch': { key: 'combDepth', label: 'Comb depth' }, 'Metallic / Resonant': { key: 'damping', label: 'Damping' },
  'Texture / Drone': { key: 'detuneSpread', label: 'Detune spread' }, 'Granular Cloud': { key: 'grainSpread', label: 'Grain spread' },
  'Glitch / Stutter': { key: 'sliceAmount', label: 'Slice amount' }, 'Formant / Alien': { key: 'vowelMorph', label: 'Vowel morph' },
} as const satisfies Record<Family, { key: string; label: string }>;
export type FamilyParam = typeof familyControls[Family]['key'];
const familyDefaults = () => Object.fromEntries(Object.values(familyControls).map(({key}) => [key, 50])) as Record<FamilyParam, number>;

export type TriggerMode = typeof triggerModes[number];
export type EffectType = typeof effectTypes[number];
export type FXLayer = Record<LayerParam, number> & { active: boolean; solo: boolean; family: Family; sourceSettings: Record<FamilyParam, number>; sample: string };
export type FXSlot = { type: EffectType; bypass: boolean; wet: number; amount: number; trim: number };
export type LFOSpec = { shape: typeof waves[number]; rate: number; sync: boolean; division: typeof syncChoices[number] };
export type Route = { id: number; source: string; target: string; depth: number; polarity: 'Unipolar' | 'Bipolar'; min: number; max: number; smoothing: number };
export type FXHit = { note: number; velocity: number; probability: number; ratchet: number };
export type Program = {
  version: 1; name: string; category: typeof categories[number]; style: typeof styles[number]; seed: number;
  lengthMode: 's' | 'bars'; seconds: number; bars: number; intensity: number; complexity: number; energy: number; stereo: number; key: number;
  macros: number[]; layers: FXLayer[]; curves: Curve[]; slots: FXSlot[]; lfos: LFOSpec[]; step: Curve; sampleHold: number;
  adsr: { attack: number; decay: number; sustain: number; release: number }; routes: Route[]; 
  triggerMode: TriggerMode; division: typeof syncChoices[number]; patternBars: number; triggerGrid: (FXHit | null)[]; quality: 'Off'|'2×'|'4×'; master: number;
};
const typical = [0, 8, 9, 2] as const;
export const fxHit: FXHit = { note: 48, velocity: 100, probability: 100, ratchet: 1 };
export const newRoute = (id: number, source = 'LFO 1', target = 'A.gain'): Route => ({ id, source, target, depth: 30, min: 0, max: 100, polarity: 'Bipolar', smoothing: 12 });
export const defaultCurves = () => Array.from({ length: 20 }, (_, n) => {
  const type = n % 4;
  const curve = type === 0 ? createCurve([[0,.04],[.22,.29],[.72,.91],[1,0]]) : type === 1 ? createCurve([[0,.28],[.54,.52],[1,.68]]) : type === 2 ? createCurve([[0,.13],[.25,.32],[.7,.73],[1,.93]]) : createCurve([[0,.45],[.34,.59],[.65,.4],[1,.55]]);
  if (n !== 16) curve[0].shape = 'smoothstep';
  return curve;
});
export function defaultProgram(): Program {
  return { version: 1, name: 'Cascade Rise', category: 'Riser', style: 'Cinematic', seed: 2026, lengthMode: 's', seconds: 4, bars: 2, intensity: 58, complexity: 48, energy: 62, stereo: 66, key: 0,
    layers: typical.map((id, n) => ({ family: families[id], active: n !== 2, solo: false, gain: [70,41,45,36][n], pan: [0,-16,17,8][n], width: [100,114,120,96][n], pitch: 0, cutoff: [8200,3100,7500,6400][n], offset: 0, duration: 100, sourceSettings: familyDefaults(), sample: '' })),
    curves: defaultCurves(), macros: [58,32,65,44,27,52,48,36], slots: (['EQ / Filter','Drive','Delay','Reverb'] as const).map((type, n) => ({ type, bypass: false, wet: [65,23,32,38][n], amount: [4400,34,240,1.8][n], trim: 0 })),
    lfos: [{ shape: 'Sine', rate: .5, sync: false, division: '1/4' }, { shape: 'Triangle', rate: 1.25, sync: true, division: '1/8' }],
    step: createCurve([[0,.19],[.25,.79],[.5,.35],[.75,.87],[1,.19]]).map(node => ({ ...node, shape: 'hold' })), sampleHold: .5,
    adsr: { attack: 12, decay: 36, sustain: 65, release: 40 }, routes: [newRoute(1, 'LFO 1', 'A.pan'), { ...newRoute(2,'Macro 3', 'A.cutoff'), depth: 25 }],
    triggerMode: 'Overlap', division: '1/16', patternBars: 4, triggerGrid: Array.from({ length: 128 }, (_, i) => [0,14,16,27,32,40,48,59].includes(i) ? { ...fxHit, note: 48 + Math.floor(i / 16) * 2 } : null), quality: 'Off', master: 80,
  };
}
export const durationSeconds = (p: Program) => p.lengthMode === 'bars' ? p.bars * 2 : p.seconds; // explicitly 120 BPM local, 4/4
function rand(seed: number) {
  let n = Math.imul(seed ^ (seed >>> 16), 0x45d9f3b);
  n = Math.imul(n ^ (n >>> 16), 0x45d9f3b);
  return ((n ^ (n >>> 16)) >>> 0) / 4294967296;
}
const categoryFamilies: Record<Program['category'], number[]> = { Riser: [0,2,8], Downlifter: [1,4,8], Whoosh: [3,0,10], Impact: [4,7,5], Tonal: [2,5,6,11], Texture: [8,9,11,7] };
export type Locks = { layers: boolean; curves: boolean; routing: boolean };
export function generate(p: Program, locks: Locks): Program {
  const options = categoryFamilies[p.category];
  const style = styles.indexOf(p.style);
  const rng = (i: number) => rand(p.seed * 101 + i * 533 + style * 291);
  const curves = defaultCurves().map((curve, i) => curve.map((node, n) => ({ ...node, y: n === 0 && i % 4 === 0 ? .02 : n === curve.length - 1 && i % 4 === 0 ? 0 : +unit01(node.y + (rng(i * 7 + n) - .5) * (.2 + p.complexity / 180)).toFixed(3) })));
  const names: Record<Program['category'], string> = { Riser:'Rise', Downlifter:'Descent', Whoosh:'Passage', Impact:'Strike', Tonal:'Signal', Texture:'Cloud' };
  const chain: EffectType[] = ['EQ / Filter', p.energy > 55 ? 'Drive' : 'Chorus', 'Delay', 'Reverb'];
  return { ...p, name: `${p.style} ${names[p.category]} · ${p.seed}`, layers: locks.layers ? p.layers : p.layers.map((layer, i) => {
      const family = families[options[Math.floor(rng(i === 0 ? 2 : i + 4) * options.length)]];
      return { ...layer, family, active: i === 0 || rng(i + 7) > .6 - p.complexity / 180,
        gain: Math.min(100, Math.round(p.intensity * (i ? .45 + rng(i + 8) * .45 : 1.15))),
        pan: i ? Math.round((rng(i + 12) - .5) * p.stereo) : 0, pitch: p.key,
        sourceSettings: { ...layer.sourceSettings, [familyControls[family].key]: Math.round(p.energy / 2 + rng(i + 16) * 50) },
      };
    }),
    curves: locks.curves ? p.curves : curves,
    slots: locks.routing ? p.slots : chain.map((type, n) => ({ type, bypass: false, wet: Math.round(20 + rng(n + 20) * p.energy * .65), trim: 0, amount: effectSpec(type).defaultValue })),
    routes: locks.routing ? p.routes : [newRoute(1,'Macro 1','A.gain'), { ...newRoute(2, 'LFO 1', 'B.pan'), depth: Math.round(p.stereo / 2), polarity: 'Bipolar' }],
  };
}
export const demoPrograms = [
  { id: 'cascade', program: defaultProgram() },
  { id: 'ember', program: { ...generate({ ...defaultProgram(), category: 'Impact', style: 'Techno', seed: 904, seconds: 1.4 }, { layers: false, curves: false, routing: false }), name: 'Ember Strike' } },
  { id: 'glass', program: { ...generate({ ...defaultProgram(), category: 'Texture', style: 'Ambient', seed: 5790, seconds: 12 }, { layers: false, curves: false, routing: false }), name: 'Glass Drift' } },
  { id: 'orbit', program: { ...generate({ ...defaultProgram(), category: 'Whoosh', style: 'Goa', seed: 4030, seconds: 2.65 }, { layers: false, curves: false, routing: false }), name: 'Orbit Pass' } },
];
export function effectSpec(type: EffectType): Parameter {
  switch (type) {
    case 'EQ / Filter': return { ...linear('effect-amount','Cutoff',30,18000,4400,1,'Hz'), scale: 'log', keyStep: 100 };
    case 'Delay': return linear('effect-amount','Time',5,2000,240,1,'ms');
    case 'Reverb': return linear('effect-amount','Decay',.1,12,1.8,.01,'s');
    case 'Ring Mod': return { ...linear('effect-amount','Carrier',10,3000,120,1,'Hz'), scale: 'log' };
    case 'Bitcrush': return linear('effect-amount','Resolution',2,24,12,1,'bit');
    case 'Trance Gate': return linear('effect-amount','Duty cycle',0,100,50,1,'%');
    case 'Drive': case 'Wavefold': return linear('effect-amount','Drive',0,100,34,1,'%');
    default: return linear('effect-amount','Depth',0,100,28,1,'%');
  }
}
export function lfoValue(lfo: LFOSpec, time: number) {
  const hz = lfo.sync ? divisions[lfo.division] / 2 : lfo.rate;
  const t = ((time * hz) % 1 + 1) % 1;
  return lfo.shape === 'Sine' ? .5 + .5 * Math.sin(t * 2 * Math.PI) : lfo.shape === 'Triangle' ? 1 - Math.abs(2 * t - 1) : lfo.shape === 'Saw' ? t : t <= .5 ? 1 : 0;
}
export type ModInput = { time: number; phase: number; velocity: number };
export function sourceValue(p: Program, id: string, input: ModInput): number | null {
  if (id === 'Self follower') return null;
  if (id.startsWith('Macro ')) return (p.macros[Number(id.slice(6)) - 1] ?? 0) / 100;
  if (id === 'LFO 1' || id === 'LFO 2') return lfoValue(p.lfos[id === 'LFO 1' ? 0 : 1], input.time);
  if (id === 'Step envelope') return evaluate(p.step, input.phase);
  if (id === 'Random S&H') return rand(p.seed * 13 + Math.floor(input.time / p.sampleHold));
  if (id === 'Triggered ADSR') {
    const attack = p.adsr.attack / 180; const decay = p.adsr.decay / 185; const sustain = p.adsr.sustain / 100; const release = p.adsr.release / 202;
    return input.phase === 0 ? 0 : input.phase < attack ? input.phase / attack : input.phase < attack + decay ? 1 - (1 - sustain) * ((input.phase - attack) / decay) : input.phase > 1 - release ? sustain * (1 - input.phase) / release : sustain;
  }
  return id === 'Event phase' ? input.phase : input.velocity / 127;
}
export function effectiveValue(p: Program, layer: number, field: LayerParam, input: ModInput): number | undefined {
  const id = `${layerLetters[layer]}.${field}`; const routes = p.routes.filter(route => route.target === id && route.source !== 'Self follower');
  if (!routes.length) return undefined;
  const spec = layerSpecs[field];
  const change = routes.reduce((total, route) => {
    const v = sourceValue(p, route.source, input) ?? 0;
    const bounded = route.min / 100 + (route.max - route.min) / 100 * v;
    return total + (route.polarity === 'Bipolar' ? bounded * 2 - 1 : bounded) * route.depth / 100;
  }, 0);
  return clamp(toPlain(spec, toNormalized(spec, p.layers[layer][field]) + change), spec.min, spec.max);
}

// Reject incompatible, unbounded, or ambiguous disk/localStorage payloads before state publication.
const object = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const finite = (n: unknown, min: number, max: number, whole = false): n is number => typeof n === 'number' && Number.isFinite(n) && n >= min && n <= max && (!whole || Number.isInteger(n));
const member = (v: unknown, opts: readonly string[]) => typeof v === 'string' && opts.includes(v);
const yes = (v: unknown) => typeof v === 'boolean';
const fixed = (v: unknown, size: number, valid: (value: unknown) => boolean): boolean => Array.isArray(v) && v.length === size && v.every(valid);
function curveValid(v: unknown) {
  return Array.isArray(v) && v.length >= 2 && v.length <= 16 && new Set(v.map(n => n?.id)).size === v.length && v.every((node, i) => object(node) && finite(node.id,0,Number.MAX_SAFE_INTEGER - 10,true) && finite(node.x,0,1) && finite(node.y,0,1) && member(node.shape, shapes) && (i === 0 ? node.x === 0 : (i !== v.length - 1 || node.x === 1) && node.x - v[i - 1].x >= .0099));
}
function validSourceSettings(v: unknown) { return object(v) && Object.values(familyControls).every(({key}) => finite(v[key],0,100)); }
function hitValid(v: unknown) {
  return v === null || object(v) && finite(v.note,0,127,true) && finite(v.velocity,1,127,true) && finite(v.probability,0,100,true) && finite(v.ratchet,1,4,true);
}
export function validateProgram(v: unknown): Program | null {
  if (!object(v) || v.version !== 1 || typeof v.name !== 'string' || !v.name.trim() || v.name.length > 52 || !member(v.category,categories) || !member(v.style,styles)
    || !finite(v.seed,1,999999,true) || !member(v.lengthMode,['s','bars']) || !finite(v.seconds,.05,32) || !finite(v.bars,1,16,true) || !finite(v.key,0,11,true)
    || !['intensity','complexity','energy','stereo','master'].every(key => finite(v[key],0,100)) || !finite(v.sampleHold,.05,8) || !member(v.triggerMode,triggerModes)
    || !member(v.division,syncChoices) || !finite(v.patternBars,1,4,true) || !member(v.quality,['Off','2×','4×']) || !object(v.adsr)) return null;
  const a = v.adsr;
  if (!['attack','decay','sustain','release'].every(key => finite(a[key],0,100))) return null;
  if (!fixed(v.layers,4,l => object(l) && member(l.family,families) && yes(l.active) && yes(l.solo) && typeof l.sample === 'string' && l.sample.length <= 100 && validSourceSettings(l.sourceSettings) && layerParams.every(key => finite(l[key],layerSpecs[key].min,layerSpecs[key].max)))
    || !fixed(v.curves,20,curveValid) || !fixed(v.macros,8,n => finite(n,0,100)) || !curveValid(v.step) || !fixed(v.triggerGrid,128,hitValid)
    || !fixed(v.slots,4,s => {
      if (!object(s) || !member(s.type,effectTypes)) return false;
      const spec = effectSpec(s.type as EffectType);
      return yes(s.bypass) && finite(s.wet,0,100) && finite(s.amount,spec.min,spec.max) && finite(s.trim,-24,12);
    }) || !fixed(v.lfos,2,l => object(l) && member(l.shape,waves) && finite(l.rate,.01,24) && yes(l.sync) && member(l.division,syncChoices))) return null;
  if (!Array.isArray(v.routes) || v.routes.length > fxLimits.routes || new Set(v.routes.map(r => r?.id)).size !== v.routes.length || !v.routes.every(r => object(r) && finite(r.id,1,Number.MAX_SAFE_INTEGER-100,true) && member(r.source,sources) && destinations.some(d => d.id === r.target) && finite(r.depth,-100,100) && member(r.polarity,['Unipolar','Bipolar']) && finite(r.min,0,100) && finite(r.max,r.min,100) && finite(r.smoothing,0,250))) return null;
  return v as unknown as Program;
}
