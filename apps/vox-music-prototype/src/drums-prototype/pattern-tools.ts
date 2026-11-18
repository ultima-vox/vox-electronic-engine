import { baseStep } from './model';
import type { Pattern, Step } from './model';

export const patternTools = [
  { value: 'left', label: 'Shift left · 1 step' }, { value: 'right', label: 'Shift right · 1 step' },
  { value: 'double', label: 'Double first half' }, { value: 'reverse', label: 'Reverse steps' },
  { value: 'invert', label: 'Invert selected lane' },
] as const;
export type PatternTool = typeof patternTools[number]['value'];

function sameStep(a: Step | null, b: Step | null) {
  return a === b || (a !== null && b !== null && a.velocity === b.velocity && a.probability === b.probability && a.ratchet === b.ratchet && a.micro === b.micro);
}

export function replacePattern(old: Pattern, next: Pattern) {
  return old.length === next.length && old.tracks.every((row, pad) =>
    old.laneLocks[pad] === next.laneLocks[pad] && row.every((hit, step) =>
      old.stepLocks[pad][step] === next.stepLocks[pad][step] && sameStep(hit, next.tracks[pad][step]))) ? old : next;
}

/** Donor B Sequencer.tsx transforms, adapted to typed hits and to the visible length of the exact edit target. */
export function transformPattern(pattern: Pattern, tool: PatternTool, selectedPad: number): Pattern {
  const { length } = pattern;
  let changed = false;
  const tracks = pattern.tracks.map((row, pad) => {
    if (tool === 'invert' && pad !== selectedPad) return row;
    const next = row.map((hit, step) => {
      if (step >= length) return hit;
      switch (tool) {
        case 'left': return row[(step + 1) % length];
        case 'right': return row[(step + length - 1) % length];
        case 'double': return row[step % (length / 2)];
        case 'reverse': return row[length - step - 1];
        case 'invert': return hit ? null : { ...baseStep };
      }
    });
    if (next.every((hit, step) => sameStep(hit, row[step]))) return row;
    changed = true;
    return next;
  });
  // Manual tools don't move the cell-based generator locks or alter the hidden tail.
  return changed ? { ...pattern, tracks } : pattern;
}
