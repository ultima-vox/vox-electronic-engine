import { clamp } from '../design-system/parameters';
import type { Context, PatternRef } from './model';

export const docks = ['pads', 'inspector', 'generator', 'library', 'routing', 'settings'] as const;
export const editors = ['step', 'song', 'sound', 'sample'] as const;
export const mixerModes = ['collapsed', 'compact', 'expanded'] as const;
export const dockNames: Record<Dock, string> = { pads: 'Pad Rack', inspector: 'Inspector', generator: 'Generator', library: 'Library', routing: 'Routing', settings: 'Diagnostics' };
export const editorNames: Record<Editor, string> = { step: 'Step Sequencer', song: 'Song Arranger', sound: 'Sound Editor', sample: 'Sample Editor' };
export type Dock = typeof docks[number];
export type Editor = typeof editors[number];
export type Zone = 'left' | 'right';
export type MixerMode = typeof mixerModes[number];
export type Layout = { version: 1; preset: string; left: Dock[]; right: Dock[]; active: Record<Zone, Dock | null>; leftWidth: number; rightWidth: number; mixer: MixerMode; mixerAuto: boolean; mixHeight: number; primary: Editor; secondary: Editor | null; split: 'side' | 'stack'; ratio: number; padView: 'grid' | 'list'; pin: Context | null };
export const presetNames = ['Beatmaking','Composition','Sound Design','Mixing'] as const;
export type PresetName = typeof presetNames[number];
export function workspace(name: PresetName = 'Beatmaking'): Layout {
  const base: Layout = { version: 1, preset: name, left: ['pads'], right: ['inspector'], active: { left: 'pads', right: 'inspector' }, leftWidth: 264, rightWidth: 224, mixer: 'compact', mixerAuto: true, mixHeight: 258, primary: 'step', secondary: null, split: 'side', ratio: 50, padView: 'grid', pin: null };
  if (name === 'Composition') return { ...base, primary: 'song', secondary: 'step', split: 'stack', ratio: 39, mixer: 'collapsed' };
  if (name === 'Sound Design') return { ...base, primary: 'sound', secondary: 'sample', split: 'stack', ratio: 60, mixer: 'collapsed' };
  if (name === 'Mixing') return { ...base, right: ['inspector', 'routing'], active: { left: 'pads', right: 'routing' }, mixer: 'expanded', mixerAuto: false };
  return base;
}
const oneOf = <T extends string>(value: unknown, all: readonly T[]): value is T => typeof value === 'string' && (all as readonly string[]).includes(value);
const record = (v: unknown): v is Record<string, unknown> => v !== null && typeof v === 'object' && !Array.isArray(v);
const int = (v: unknown, max: number): v is number => typeof v === 'number' && Number.isInteger(v) && v >= 0 && v < max;
function validRef(value: unknown): value is PatternRef {
  if (!record(value)) return false;
  return value.kind === 'bar' ? int(value.bar, 64) : value.kind === 'source' && int(value.slot, 4) && oneOf(value.variation, ['A','B']);
}
function validPin(value: unknown): value is Context {
  if (!record(value)) return false;
  if (value.kind === 'song') return int(value.bar, 64);
  if (!int(value.pad, 16)) return false;
  if (value.kind === 'pad') return true;
  return value.kind === 'steps' && validRef(value.target) && Array.isArray(value.indices) && value.indices.length > 0 && value.indices.length <= 64 && value.indices.every(i => int(i, 64));
}
const dimension = (v: unknown, fallback: number, min: number, max: number) => typeof v === 'number' && Number.isFinite(v) ? Math.round(clamp(v, min, max)) : fallback;
export const bounds = { width: [180, 340], height: [197, 360], ratio: [30, 70] } as const;
export function parseLayout(input: unknown): Layout {
  const def = workspace();
  if (!record(input) || input.version !== 1 || !Array.isArray(input.left) || !Array.isArray(input.right)) return def;
  const all = [...input.left, ...input.right];
  if (all.length > docks.length || new Set(all).size !== all.length || all.some(p => !oneOf(p, docks))) return def;
  const left = input.left as Dock[]; const right = input.right as Dock[];
  const active = record(input.active) ? input.active : {};
  const primary = oneOf(input.primary, editors) ? input.primary : def.primary;
  return { ...def, left, right, active: { left: left.includes(active.left as Dock) ? active.left as Dock : left[0] ?? null, right: right.includes(active.right as Dock) ? active.right as Dock : right[0] ?? null },
    preset: typeof input.preset === 'string' && input.preset.trim().length > 0 ? input.preset.trim().slice(0, 34) : def.preset,
    primary, secondary: oneOf(input.secondary, editors) && input.secondary !== primary ? input.secondary : null,
    split: input.split === 'stack' ? 'stack' : 'side', ratio: dimension(input.ratio, def.ratio, ...bounds.ratio),
    leftWidth: dimension(input.leftWidth, def.leftWidth, ...bounds.width), rightWidth: dimension(input.rightWidth, def.rightWidth, ...bounds.width),
    mixer: oneOf(input.mixer, mixerModes) ? input.mixer : def.mixer, mixerAuto: input.mixerAuto !== false, mixHeight: dimension(input.mixHeight, def.mixHeight, ...bounds.height),
    padView: input.padView === 'list' ? 'list' : 'grid', pin: validPin(input.pin) ? input.pin : null,
  };
}
export function moveDock(layout: Layout, dock: Dock, zone: Zone | null): Layout {
  const next = { ...layout, left: [...layout.left], right: [...layout.right], active: { ...layout.active } };
  for (const side of ['left', 'right'] as const) {
    if (side !== zone) next[side] = next[side].filter(d => d !== dock);
    if (!next[side].includes(next.active[side] as Dock)) next.active[side] = next[side][0] ?? null;
  }
  if (zone) {
    if (!next[zone].includes(dock)) next[zone].push(dock);
    next.active[zone] = dock;
  }
  return next;
}
export function showEditor(layout: Layout, editor: Editor, slot: 'primary' | 'secondary'): Layout {
  if (slot === 'primary') return { ...layout, primary: editor, secondary: layout.secondary === editor ? layout.primary : layout.secondary };
  return { ...layout, secondary: editor === layout.primary ? null : editor };
}
