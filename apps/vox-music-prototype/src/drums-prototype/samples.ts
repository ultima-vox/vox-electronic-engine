import { renderDonorSample, SR } from './donor-samples';
import type { Sample } from './model';

export const sampleLoopSeconds = .6;
export const canNormalize = (peak: number) => peak >= .000001;
export const samplePeakMultiplier = (peak: number, normalize: boolean) => normalize && canNormalize(peak) ? 1 / peak : 1;

/** One source definition is shared by wave preview and audible sample start/stop. */
export function prepareSample(name: string, buffer: AudioBuffer, donorId?: string): Sample {
  const reverseBuffer = new AudioBuffer({ numberOfChannels: buffer.numberOfChannels, length: buffer.length, sampleRate: buffer.sampleRate });
  const channels = Array.from({ length: buffer.numberOfChannels }, (_, c) => buffer.getChannelData(c));
  let peak = 0;
  for (let c = 0; c < channels.length; c++) {
    const target = reverseBuffer.getChannelData(c);
    for (let i = 0; i < target.length; i++) {
      const n = channels[c][target.length - i - 1];
      if (!Number.isFinite(n)) throw new Error('The audio contains invalid PCM values.');
      target[i] = n;
      peak = Math.max(peak, Math.abs(n));
    }
  }
  const peaks: Sample['peaks'] = Array.from({ length: 240 }, (_, column) => {
    const first = Math.floor(column * buffer.length / 240);
    const end = Math.max(first + 1, Math.floor((column + 1) * buffer.length / 240));
    let lo = 0, hi = 0;
    for (const ch of channels) for (let i = first; i < end; i++) { lo = Math.min(lo, ch[i]); hi = Math.max(hi, ch[i]); }
    return [lo, hi];
  });
  return { name, buffer, reverseBuffer, peaks, peak, donorId };
}

const cache = new Map<string, Sample>();
export function donorSample(id: string, name: string) {
  if (cache.has(id)) return cache.get(id)!;
  const data = renderDonorSample(id);
  if (!data) return null;
  const buffer = new AudioBuffer({ numberOfChannels: 1, length: data.length, sampleRate: SR });
  buffer.copyToChannel(data, 0);
  const result = prepareSample(name, buffer, id);
  cache.set(id, result);
  return result;
}
