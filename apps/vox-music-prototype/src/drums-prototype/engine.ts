import { clamp } from '../design-system/parameters';
import { hashUnit, voices } from './model';
import type { DrumProject, Sample } from './model';
import { hasSignal } from './sequencer';
import { delaySeconds } from './delay';
import { prepareSample, sampleLoopSeconds, samplePeakMultiplier } from './samples';

type Tap = { node: AnalyserNode; window: Float32Array<ArrayBuffer> };
type Strip = { gain: GainNode; pan: StereoPannerNode; tap: Tap; send1: GainNode; send2: GainNode; out: number };
type Voice = { start: number; choke: number; pattern: boolean; sources: AudioScheduledSourceNode[]; nodes: AudioNode[]; stopAt?: number };
type Effects = { delay: DelayNode; feedback: GainNode; delayOut: GainNode; room: ConvolverNode; roomOut: GainNode; taps: [Tap, Tap] };
export type Levels = { pads: number[]; main: [number, number]; returns: [number, number]; buses: number[]; rms: number; over: boolean; voices: number };
export const idleLevels: Levels = { pads: Array(16).fill(0), main: [0, 0], returns: [0, 0], buses: Array(9).fill(0), rms: 0, over: false, voices: 0 };

/** Disposable, browser-only voice graph. No node or parameter IDs are native bindings. */
export class PreviewEngine {
  readonly context = new AudioContext();
  private readonly busInputs: GainNode[];
  private readonly busTaps: Tap[];
  private readonly strips: Strip[];
  private readonly masterTaps: [Tap, Tap];
  private readonly monitor: GainNode;
  private readonly mainSplit: ChannelSplitterNode;
  private readonly impulses: AudioBuffer;
  private readonly noise: AudioBuffer;
  private readonly driveCurve: Float32Array<ArrayBuffer>;
  private readonly active = new Set<Voice>();
  private effects: Effects;
  private over = false;
  private disposed = false;
  constructor() {
    const a = this.context;
    this.monitor = a.createGain(); this.monitor.gain.value = .8; this.monitor.connect(a.destination);
    this.mainSplit = a.createChannelSplitter(2);
    this.masterTaps = [this.tap(), this.tap()];
    this.mainSplit.connect(this.masterTaps[0].node, 0); this.mainSplit.connect(this.masterTaps[1].node, 1);
    this.busTaps = Array.from({ length: 9 }, () => this.tap());
    this.busInputs = this.busTaps.map((tap, i) => { const gain = a.createGain(); gain.channelCount = 2; gain.channelCountMode = 'explicit'; gain.connect(tap.node); tap.node.connect(this.monitor); if (!i) gain.connect(this.mainSplit); return gain; });
    this.noise = a.createBuffer(1, a.sampleRate * 2, a.sampleRate);
    const noise = this.noise.getChannelData(0);
    for (let i = 0; i < noise.length; i++) noise[i] = 2 * hashUnit(i + 1) - 1;
    this.impulses = a.createBuffer(2, Math.floor(a.sampleRate * .72), a.sampleRate);
    for (let c = 0; c < 2; c++) {
      const data = this.impulses.getChannelData(c);
      for (let i = 0; i < data.length; i++) data[i] = (2 * hashUnit(i + c * data.length) - 1) * (1 - i / data.length) ** 3;
    }
    const drive = (v: number) => Math.tanh(v * 2.4) / Math.tanh(2.4);
    this.driveCurve = Float32Array.from({ length: 1024 }, (_, i) => drive(i / 511.5 - 1));
    this.effects = this.makeEffects();
    this.strips = voices.map(() => {
      const gain = a.createGain(); const pan = a.createStereoPanner(); const tap = this.tap(8192);
      const send1 = a.createGain(); const send2 = a.createGain();
      // AudioParams start at unity by default; an unprimed send would leak the first note to Main.
      gain.gain.value = 0; send1.gain.value = 0; send2.gain.value = 0;
      gain.connect(pan).connect(tap.node);
      tap.node.connect(this.busInputs[0]); tap.node.connect(send1); tap.node.connect(send2);
      send1.connect(this.effects.delay); send2.connect(this.effects.room);
      return { gain, pan, tap, send1, send2, out: 0 };
    });
  }
  private tap(size = 4096): Tap {
    // Peak taps use rolling audio windows so brief transients survive one slow UI frame.
    const node = this.context.createAnalyser(); node.fftSize = size;
    return { node, window: new Float32Array(node.fftSize) };
  }
  private makeEffects(): Effects {
    const a = this.context;
    const delay = a.createDelay(2); delay.delayTime.value = .237;
    const feedback = a.createGain(); feedback.gain.value = .28;
    const delayOut = a.createGain(); delayOut.gain.value = .55;
    const room = a.createConvolver(); room.buffer = this.impulses;
    const roomOut = a.createGain(); roomOut.gain.value = .52;
    const taps: [Tap, Tap] = [this.tap(), this.tap()];
    delay.connect(feedback).connect(delay); delay.connect(delayOut).connect(taps[0].node).connect(this.busInputs[0]);
    room.connect(roomOut).connect(taps[1].node).connect(this.busInputs[0]);
    return { delay, feedback, delayOut, room, roomOut, taps };
  }
  async arm() { if (!this.disposed && this.context.state !== 'running') await this.context.resume(); }
  sync(project: DrumProject, bpm: number) {
    if (this.disposed) return;
    const time = this.context.currentTime;
    this.strips.forEach((s, i) => {
      const pad = project.pads[i];
      s.gain.gain.setTargetAtTime(hasSignal(project, i) ? pad.level * project.kitLevel : 0, time, .006);
      s.pan.pan.setTargetAtTime(pad.pan, time, .008);
      s.send1.gain.setTargetAtTime(pad.send1 * .62, time, .012);
      s.send2.gain.setTargetAtTime(pad.send2 * .62, time, .012);
      if (pad.out !== s.out) { s.tap.node.disconnect(this.busInputs[s.out]); s.tap.node.connect(this.busInputs[pad.out]); s.out = pad.out; }
    });
    this.effects.delay.delayTime.setTargetAtTime(delaySeconds(project.delayDivision, bpm), time, .025);
    this.effects.feedback.gain.setTargetAtTime(project.delayFeedback, time, .02);
    this.effects.delayOut.gain.setTargetAtTime(project.delayReturn, time, .012);
    this.effects.roomOut.gain.setTargetAtTime(project.reverbReturn, time, .012);
  }
  trigger(project: DrumProject, index: number, velocity: number, at: number, pattern = false): boolean {
    if (this.disposed || !hasSignal(project, index)) return false;
    const pad = project.pads[index]; const player = pad.players[pad.player]; const v = voices[index]; const a = this.context;
    if (pad.source === 'Sample' && !player.sample) return false;
    const time = Math.max(a.currentTime, at);
    if (pad.choke) for (const voice of this.active) if (voice.choke === pad.choke && voice.start <= time) this.stop(voice, time + .003);
    if (this.active.size >= project.polyphony) {
      const oldest = [...this.active].find(voice => voice.start <= time);
      if (!oldest) return false;
      this.disposeVoice(oldest, time);
    }
    const volume = a.createGain(); volume.gain.value = (.08 + .92 * velocity / 127) * .62;
    const filter = a.createBiquadFilter(); filter.type = 'lowpass'; filter.frequency.value = Math.min(a.sampleRate * .48, pad.cutoff); filter.Q.value = .707;
    filter.connect(volume).connect(this.strips[index].gain);
    const voice: Voice = { start: time, choke: pad.choke, pattern, sources: [], nodes: [volume, filter] };
    let input: AudioNode = filter;
    if (pad.drive > 0) {
      const drive = a.createWaveShaper(); drive.curve = this.driveCurve;
      const before = a.createGain(); before.gain.value = 1 + pad.drive / 24;
      const after = a.createGain(); after.gain.value = 1 / (1 + pad.drive / 95);
      before.connect(drive).connect(after).connect(filter); voice.nodes.push(before, drive, after); input = before;
    }
    const pitch = 2 ** (pad.tune / 12);
    const tail = clamp(pad.decay, .01, 10);
    if (pad.source !== 'Sample') {
      if (v.noise < 1) {
        const osc = a.createOscillator(); const env = a.createGain(); osc.type = v.wave;
        osc.frequency.setValueAtTime(v.hz * pitch * (1 + v.sweep), time);
        if (v.sweep) osc.frequency.exponentialRampToValueAtTime(v.hz * pitch, time + Math.min(tail * .52, .13));
        const waveLevel = (1 - v.noise) * (v.group === 'kick' ? .92 : .68);
        env.gain.setValueAtTime(.0001, time); env.gain.exponentialRampToValueAtTime(Math.max(.0002, waveLevel), time + .002);
        env.gain.exponentialRampToValueAtTime(.0001, time + tail + .004);
        osc.connect(env).connect(input); osc.start(time); osc.stop(time + tail + .008);
        voice.sources.push(osc); voice.nodes.push(env);
      }
      if (v.noise > 0) {
        const noise = a.createBufferSource(); noise.buffer = this.noise; noise.loop = true; noise.playbackRate.value = pitch;
        const color = a.createBiquadFilter(); color.type = v.group === 'cymbal' ? 'highpass' : 'bandpass';
        color.frequency.value = v.group === 'cymbal' ? (index === 6 ? 4000 : 6600) : index === 2 ? 1240 : 1900; color.Q.value = v.group === 'cymbal' ? .72 : .9;
        const env = a.createGain(); env.gain.setValueAtTime(.0001, time);
        const level = v.noise * (v.group === 'cymbal' ? .34 : .84); const d = Math.max(.012, tail * (index === 4 ? .7 : 1));
        env.gain.exponentialRampToValueAtTime(level, time + .002);
        if (index === 2 && d > .045) {
          env.gain.exponentialRampToValueAtTime(.04, time + .014); env.gain.exponentialRampToValueAtTime(level * .72, time + .020);
          env.gain.exponentialRampToValueAtTime(.03, time + .034); env.gain.exponentialRampToValueAtTime(level * .57, time + .041);
        }
        env.gain.exponentialRampToValueAtTime(.0001, time + d + .004);
        noise.connect(color).connect(env).connect(input); noise.start(time, hashUnit(index * 37 + Math.round(time * 10000)) * .7); noise.stop(time + d + .01);
        voice.sources.push(noise); voice.nodes.push(env, color);
      }
    }
    if (pad.source !== 'Synth' && player.sample) {
      const source = a.createBufferSource(); const env = a.createGain();
      const sample = player.sample;
      source.buffer = player.reverse ? sample.reverseBuffer : sample.buffer; source.playbackRate.value = pitch;
      const first = Math.min(sample.buffer.length - 1, Math.floor(player.start * sample.buffer.length));
      const end = Math.max(first + 1, Math.round(player.end * sample.buffer.length));
      const duration = (end - first) / sample.buffer.sampleRate;
      const offset = (player.reverse ? sample.buffer.length - end : first) / sample.buffer.sampleRate;
      const t = player.loop ? sampleLoopSeconds : duration / pitch;
      const gain = player.gain * samplePeakMultiplier(sample.peak, player.normalize);
      const fadeIn = Math.min(.002, t / 4); const fadeOut = Math.min(.009, t / 4);
      env.gain.setValueAtTime(0, time); env.gain.linearRampToValueAtTime(gain, time + fadeIn); env.gain.setValueAtTime(gain, time + t - fadeOut); env.gain.linearRampToValueAtTime(0, time + t);
      source.connect(env).connect(input);
      if (player.loop) { source.loop = true; source.loopStart = offset; source.loopEnd = offset + duration; source.start(time, offset); }
      else source.start(time, offset, duration);
      source.stop(time + t + .004);
      voice.sources.push(source); voice.nodes.push(env);
    }
    if (!voice.sources.length) return false;
    this.active.add(voice);
    let left = voice.sources.length;
    for (const source of voice.sources) source.onended = () => { if (--left === 0) this.disposeVoice(voice); };
    return true;
  }
  private stop(voice: Voice, time: number) {
    if (voice.stopAt !== undefined && voice.stopAt <= time) return;
    voice.stopAt = time;
    const gain = voice.nodes[0] as GainNode;
    gain.gain.cancelScheduledValues(time); gain.gain.setTargetAtTime(0, time, .0015);
    for (const source of voice.sources) { try { source.stop(time + .007); } catch { /* already ended */ } }
  }
  private disposeVoice(voice: Voice, stopTime?: number) {
    if (!this.active.delete(voice)) return;
    for (const s of voice.sources) { s.onended = null; if (stopTime !== undefined) { try { s.stop(stopTime); } catch { /* ended */ } } s.disconnect(); }
    for (const node of voice.nodes) node.disconnect();
  }
  cancelScheduled() {
    const t = this.context.currentTime;
    for (const voice of this.active) if (voice.pattern && voice.start > t) this.disposeVoice(voice, t);
  }
  panic(project: DrumProject, bpm: number) {
    if (this.disposed) return;
    const a = this.context;
    for (const voice of this.active) this.stop(voice, a.currentTime);
    for (const s of this.strips) { s.send1.disconnect(); s.send2.disconnect(); }
    const { delay, feedback, delayOut, room, roomOut, taps } = this.effects;
    [delay, feedback, delayOut, room, roomOut, ...taps.map(t => t.node)].forEach(n => n.disconnect());
    this.effects = this.makeEffects();
    for (const s of this.strips) { s.send1.connect(this.effects.delay); s.send2.connect(this.effects.room); }
    this.sync(project, bpm);
  }
  read(): Levels {
    const measure = (tap: Tap) => {
      tap.node.getFloatTimeDomainData(tap.window);
      let peak = 0, squares = 0;
      for (const s of tap.window) { peak = Math.max(peak, Math.abs(s)); squares += s * s; }
      return { peak, squares: squares / tap.window.length };
    };
    const l = measure(this.masterTaps[0]), r = measure(this.masterTaps[1]);
    this.over ||= Math.max(l.peak, r.peak) >= 1;
    return { pads: this.strips.map(s => measure(s.tap).peak), main: [l.peak, r.peak], returns: [measure(this.effects.taps[0]).peak, measure(this.effects.taps[1]).peak], buses: this.busTaps.map(t => measure(t).peak), rms: Math.sqrt((l.squares + r.squares) / 2), over: this.over, voices: [...this.active].filter(voice => voice.start <= this.context.currentTime).length };
  }
  clearOver() { this.over = false; }
  async decode(file: File): Promise<Sample> {
    if (file.size <= 0 || file.size > 12 * 1024 * 1024) throw new Error('Choose an audio file under 12 MB.');
    const buffer = await this.context.decodeAudioData(await file.arrayBuffer());
    if (buffer.duration > 30 || buffer.numberOfChannels > 2 || buffer.length < 16) throw new Error('Use a mono/stereo sample, under 30 seconds.');
    return prepareSample(file.name, buffer);
  }
  dispose() {
    this.disposed = true;
    for (const voice of this.active) this.disposeVoice(voice, this.context.currentTime);
    void this.context.close().catch(() => {});
  }
}
