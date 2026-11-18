// Procedural PCM generators from donor B src/audio/samples.ts @ 50933e54dcae83032206a47ed65bfabca478411a.
// The names below describe synthesized browser fixtures, not WAV files or native factory content.
export const SR = 44100;

const sampleNames = [
  ["Kick 01 - Deep", "Kick 02 - Punch", "Kick 03 - Sub 808"],
  ["Snare 01 - Tight", "Snare 02 - Fat", "Snare 03 - Snap"],
  ["Clap 01 - Studio", "Clap 02 - Room", "Clap 03 - Dry"],
  ["CHat 01 - Crisp", "CHat 02 - Tick", "CHat 03 - Dusty"],
  ["OHat 01 - Open", "OHat 02 - Long", "OHat 03 - Short"],
  ["Perc 01 - Tom", "Perc 02 - Low Tom", "Perc 03 - Electro Tom"],
  ["Perc 04 - Conga", "Perc 05 - Bongo", "Perc 06 - Blip"],
  ["Rim 01 - Click", "Rim 02 - Wood", "Rim 03 - Zap"],
] as const;

function rng(seed: number) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296 * 2 - 1;
  };
}

type FilterMode = "lp" | "hp" | "bp";
/** State variable filter applied to a whole array */
function svf(data: Float32Array, mode: FilterMode, fc: number, q = 0.7): Float32Array<ArrayBuffer> {
  const f = 2 * Math.sin(Math.PI * Math.min(fc, SR * 0.45) / SR);
  const damp = 1 / q;
  let low = 0, band = 0;
  const out = new Float32Array(data.length);
  for (let i = 0; i < data.length; i++) {
    low += f * band;
    const high = data[i] - low - damp * band;
    band += f * high;
    out[i] = mode === "lp" ? low : mode === "hp" ? high : band;
  }
  return out;
}

function make(seconds: number, fn: (t: number, i: number) => number): Float32Array<ArrayBuffer> {
  const n = Math.floor(seconds * SR);
  const d = new Float32Array(n);
  for (let i = 0; i < n; i++) d[i] = fn(i / SR, i);
  return d;
}

function fadeAndPeak(d: Float32Array<ArrayBuffer>, peak: number): Float32Array<ArrayBuffer> {
  const fade = Math.floor(0.004 * SR);
  for (let i = 0; i < fade && i < d.length; i++) d[d.length - 1 - i] *= i / fade;
  let max = 0;
  for (let i = 0; i < d.length; i++) max = Math.max(max, Math.abs(d[i]));
  const g = max > 0 ? peak / max : 1;
  for (let i = 0; i < d.length; i++) d[i] *= g;
  return d;
}

function kick(v: number) {
  const p = [
    { f0: 170, f1: 46, pd: 0.035, ad: 0.16, len: 0.6, click: 0.35, drive: 1.6 },
    { f0: 230, f1: 56, pd: 0.022, ad: 0.11, len: 0.42, click: 0.7, drive: 2.4 },
    { f0: 120, f1: 38, pd: 0.06, ad: 0.32, len: 0.9, click: 0.12, drive: 1.2 },
  ][v];
  const r = rng(11 + v);
  let ph = 0;
  const body = make(p.len, (t) => {
    const f = p.f1 + (p.f0 - p.f1) * Math.exp(-t / p.pd);
    ph += (2 * Math.PI * f) / SR;
    return Math.tanh(Math.sin(ph) * Math.exp(-t / p.ad) * p.drive);
  });
  const click = svf(make(p.len, (t) => r() * Math.exp(-t / 0.003)), "lp", 5000);
  for (let i = 0; i < body.length; i++) body[i] += click[i] * p.click;
  return fadeAndPeak(body, 0.9);
}

function snare(v: number) {
  const p = [
    { f: 190, td: 0.07, nd: 0.13, nm: 0.8, hp: 1200, len: 0.4 },
    { f: 160, td: 0.12, nd: 0.22, nm: 0.75, hp: 800, len: 0.55 },
    { f: 240, td: 0.04, nd: 0.09, nm: 1.0, hp: 2200, len: 0.3 },
  ][v];
  const r = rng(21 + v);
  let ph = 0;
  const tone = make(p.len, (t) => {
    const f = p.f * (1 + 0.6 * Math.exp(-t / 0.012));
    ph += (2 * Math.PI * f) / SR;
    return Math.sin(ph) * Math.exp(-t / p.td) * 0.7;
  });
  const noise = svf(make(p.len, (t) => r() * Math.exp(-t / p.nd)), "hp", p.hp);
  for (let i = 0; i < tone.length; i++) tone[i] += noise[i] * p.nm;
  return fadeAndPeak(tone, 0.85);
}

function clap(v: number) {
  const p = [
    { tail: 0.11, bp: 1500, gap: 0.011, len: 0.4 },
    { tail: 0.22, bp: 1200, gap: 0.014, len: 0.55 },
    { tail: 0.06, bp: 2000, gap: 0.008, len: 0.25 },
  ][v];
  const r = rng(31 + v);
  const raw = make(p.len, (t) => {
    let e = 0;
    for (let k = 0; k < 4; k++) {
      const dt = t - k * p.gap;
      if (dt >= 0) e = Math.max(e, Math.exp(-dt / 0.0035));
    }
    const tail = t >= 3 * p.gap ? Math.exp(-(t - 3 * p.gap) / p.tail) : 0;
    return r() * Math.max(e, tail * 0.8);
  });
  const d = svf(svf(raw, "bp", p.bp, 0.9), "hp", 500);
  return fadeAndPeak(d, 0.8);
}

const HAT_RATIOS = [2, 3, 4.16, 5.43, 6.79, 8.21];
function hat(open: boolean, v: number) {
  const dec = open ? [0.28, 0.5, 0.16][v] : [0.045, 0.025, 0.07][v];
  const len = open ? dec * 3.2 : dec * 6;
  const base = [205.3, 240, 190][v];
  const r = rng(41 + v + (open ? 5 : 0));
  const raw = make(len, (t) => {
    let s = 0;
    for (const m of HAT_RATIOS) s += Math.sign(Math.sin(2 * Math.PI * base * m * t));
    const n = r() * 0.8;
    return (s / 6 + n) * Math.exp(-t / dec);
  });
  const d = svf(svf(raw, "hp", 7000, 0.8), "hp", 5000, 0.7);
  return fadeAndPeak(d, 0.55);
}

function perc1(v: number) {
  const p = [
    { f0: 260, f1: 140, dec: 0.16, len: 0.5 },
    { f0: 190, f1: 90, dec: 0.24, len: 0.7 },
    { f0: 420, f1: 120, dec: 0.12, len: 0.4 },
  ][v];
  let ph = 0;
  const r = rng(51 + v);
  const d = make(p.len, (t) => {
    const f = p.f1 + (p.f0 - p.f1) * Math.exp(-t / 0.04);
    ph += (2 * Math.PI * f) / SR;
    return Math.sin(ph) * Math.exp(-t / p.dec) + r() * 0.15 * Math.exp(-t / 0.006);
  });
  return fadeAndPeak(d, 0.8);
}

function perc2(v: number) {
  const p = [
    { f0: 420, f1: 340, dec: 0.1, fm: 0.4, len: 0.35 },
    { f0: 620, f1: 520, dec: 0.07, fm: 0.3, len: 0.25 },
    { f0: 900, f1: 300, dec: 0.12, fm: 2.2, len: 0.35 },
  ][v];
  let ph = 0, ph2 = 0;
  const d = make(p.len, (t) => {
    const f = p.f1 + (p.f0 - p.f1) * Math.exp(-t / 0.03);
    ph2 += (2 * Math.PI * f * 1.5) / SR;
    ph += (2 * Math.PI * f) / SR + Math.sin(ph2) * p.fm * 0.2 * Math.exp(-t / 0.03);
    return Math.sin(ph) * Math.exp(-t / p.dec);
  });
  return fadeAndPeak(d, 0.75);
}

function rim(v: number) {
  const p = [
    { f: 1700, g: 800, dec: 0.012, len: 0.2 },
    { f: 1200, g: 540, dec: 0.025, len: 0.25 },
    { f: 2600, g: 1300, dec: 0.02, len: 0.3 },
  ][v];
  const r = rng(61 + v);
  const d = make(p.len, (t) => {
    const e = Math.exp(-t / p.dec);
    const zap = v === 2 ? Math.sin(2 * Math.PI * (p.f * Math.exp(-t * 18)) * t) : 0;
    return (Math.sin(2 * Math.PI * p.f * t) * 0.6 + Math.sin(2 * Math.PI * p.g * t) * 0.6 + r() * 0.5 + zap) * e;
  });
  return fadeAndPeak(svf(d, "hp", 400), 0.75);
}

const generators = [kick, snare, clap, (v: number) => hat(false, v), (v: number) => hat(true, v), perc1, perc2, rim];
export const donorSamples = sampleNames.flatMap((names, kind) => names.map((name, variant) => ({ id: `${kind}:${variant}`, name, kind, variant })));

export function renderDonorSample(id: string): Float32Array<ArrayBuffer> | null {
  const entry = donorSamples.find(sample => sample.id === id);
  if (!entry) return null;
  return generators[entry.kind](entry.variant);
}
