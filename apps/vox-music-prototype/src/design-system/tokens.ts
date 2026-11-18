export const themes = {
  drums: { name: 'Drums', accent: '#f3bd72', material: 'Warm amber' },
  fx: { name: 'FX', accent: '#7ce2d0', material: 'Sea glass' },
  synth: { name: 'Synth', accent: '#c3a9f7', material: 'Soft violet' },
} as const;
export type Theme = keyof typeof themes;
export const surfaces = [
  ['Background', '--background', '#101218'], ['Panel', '--panel', '#181b22'],
  ['Raised', '--raised', '#22262f'], ['Well', '--well', '#11141a'],
  ['Border', '--line', '#313640'], ['Text', '--text', '#eef0f3'],
  ['Secondary', '--secondary', '#adb4c0'], ['Muted', '--muted', '#929aa9'],
  ['Success', '--success', '#89d4aa'], ['Warning', '--warning', '#f3bd72'], ['Error', '--error', '#fb9995'],
] as const;
export const padColours = {
  kick: '#f3bd72', snare: '#ef9b7e', cymbal: '#8ed3bc', tom: '#a7b8ef', perc: '#c7a6e8',
} as const;
export const space = [4, 8, 12, 16, 20, 24, 32] as const;
