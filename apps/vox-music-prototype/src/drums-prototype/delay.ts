import { clamp } from '../design-system/parameters';

// Beat ratios from donor B src/audio/engine.ts; this workbench is driven only by its local tempo.
export const delayDivisions = { '1/16': .25, '1/8': .5, '1/8 dotted': .75, '1/4': 1 } as const;
export type DelayDivision = keyof typeof delayDivisions;
export function delaySeconds(division: DelayDivision, bpm: number) {
  return delayDivisions[division] * 60 / clamp(bpm, 50, 220);
}
