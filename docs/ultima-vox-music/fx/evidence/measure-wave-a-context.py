#!/usr/bin/env python3
"""Role-specific PCM mix fixtures, not a substitute for a listening panel.
Usage: python measure-wave-a-context.py <review-root> (NumPy 2.2.6).
The backing is synthesized here, with no external audio or library samples.
"""
import csv
import pathlib
import sys
import wave

import numpy as np

RATE = 48000
TAU = 2*np.pi
# Peak target cap on the solo FX in a section, not a product preset change.
# Also limit boosts to 24 dB on all roles to avoid hiding a weak recipe.
TARGET = dict(impact=.145, riser=.095, fall=.092, sweep=.105, zap=.090,
              rhythm=.115, atmosphere=.054, stinger=.125)
PICKS = dict(impact='seismic-lock', riser='gold-meridian', fall='ebb-foam',
             sweep='lake-prism', zap='neon-morse', rhythm='citron-hop',
             atmosphere='fen-lamp', stinger='terrace-sign')


def load(path):
    with wave.open(str(path), 'rb') as f:
        assert (f.getframerate(), f.getnchannels(), f.getsampwidth()) == (RATE, 2, 3)
        b = np.frombuffer(f.readframes(f.getnframes()), dtype=np.uint8).reshape(-1, 2, 3).astype(np.int32)
    w = b[..., 0] | b[..., 1] << 8 | b[..., 2] << 16
    return ((w ^ 0x800000)-0x800000).astype(np.float64)/8388607


def save(path, stereo):
    with wave.open(str(path), 'wb') as f:
        f.setparams((2, 2, RATE, 0, 'NONE', 'not compressed'))
        f.writeframes(np.rint(stereo*32767).astype('<i2').tobytes())


def backing(n, bpm, note, role, minor, arrival):
    period = 60/bpm
    t = np.arange(n)/RATE
    beats = t/period
    phase = np.remainder(t, period)
    off = np.remainder(t-period*.5, period)
    short = np.remainder(t-period*.25, period*.5)
    atk = -np.expm1(-phase/.0018)
    kick = .40*np.sin(TAU*(48*phase+3.3*(-np.expm1(-phase/.035))))*atk*np.exp(-phase/.091)
    hz = 440*2**(((note % 12)+36-69)/12)
    bass = .155*np.sin(TAU*hz*off)*(-np.expm1(-off/.008))*np.exp(-off/.116)
    pad_env = .7-.3*np.cos(TAU*beats/16)
    third = 3 if minor else 4
    mid = .067*np.sin(TAU*hz*2**(third/12)*t)*pad_env
    top = .049*np.sin(TAU*hz*2**(19/12)*t)*pad_env
    tick = .044*(np.sin(TAU*5912*short)+.61*np.sin(TAU*8437*short))*np.exp(-short/.016)
    if role in ('riser', 'stinger'):
        # Drop/break one beat before arrival and return on its downbeat.
        space = (t < max(0, arrival-period)) | (t >= arrival)
        kick, bass = kick*space, bass*space
    if role == 'atmosphere':
        kick *= .43
        tick *= .38
    left = kick+bass+mid+.42*top+tick
    right = kick+bass+.46*mid+top+.72*tick
    taper = np.minimum(1, np.minimum(t/.014, (n-1-np.arange(n))/(RATE*.018)))
    return np.stack((left, right), axis=1)*taper[:, None]


def main(root):
    setup = {x['ID']: x for x in csv.DictReader((root/'audition-settings.tsv').open(), delimiter='\t')}
    context = root/'in-context'
    context.mkdir(exist_ok=True)
    with (root/'context-metrics.tsv').open('w') as out:
        out.write('ID\tsolo-peak\ttrim-dB\tbed-peak\tmix-peak\theadroom-dBFS\tFX-mono-rms-ratio\tlast-250ms-rms\tlast-5ms-peak\tcue-start-seconds\n')
        for row in csv.DictReader((root/'manifest.tsv').open(), delimiter='\t'):
            id_, role = row['ID'], row['role']
            s = setup[id_]
            fx = load(root/(id_+'.wav'))
            peak = np.max(np.abs(fx))
            trim = min(10**(24/20), TARGET[role]/peak)
            beat = 60/float(s['BPM'])
            cue = dict(impact=4, riser=4, fall=4, sweep=3, zap=3.5, rhythm=4, atmosphere=0, stinger=4)[role]
            start = round(RATE*cue*beat)
            bar_count = float(row['duration']) if row['duration-kind']=='bars' else 0
            duration = float(s['duration-param-seconds'])
            arrival = cue*beat + (bar_count*4*beat if bar_count else duration if role=='riser' else 0)
            # Backing covers the musical action, then continue the rendered FX tail unmasked.
            active = cue*beat+max(duration, bar_count*4*beat)
            n_bed = round(RATE*(max(beat*8, active+beat*4)))
            n = max(start+len(fx), n_bed)
            bed = np.zeros((n, 2))
            minor = row['scale'].lower() in ('minor', 'dorian', 'phrygian')
            root_note = int(s['MIDI-note']) if row['root']=='-' else int(row['root'])
            bed[:n_bed] = backing(n_bed, float(s['BPM']), root_note, role, minor, arrival)
            mix = bed.copy()
            mix[start:start+len(fx)] += fx*trim
            mix_peak = np.max(np.abs(mix))
            mono_ratio = np.sqrt(np.mean(fx.mean(axis=1)**2)/np.mean(fx**2))
            tail = np.sqrt(np.mean(fx[-RATE//4:]**2))
            edge = np.max(np.abs(fx[-RATE//200:]))
            if mix_peak >= .80 or mono_ratio <= .80 or edge >= .001:
                raise ValueError((id_, mix_peak, mono_ratio, edge))
            out.write(f'{id_}\t{peak:.8f}\t{20*np.log10(trim):.3f}\t{np.max(np.abs(bed)):.7f}\t{mix_peak:.7f}\t{-20*np.log10(mix_peak):.3f}\t{mono_ratio:.6f}\t{tail:.8f}\t{edge:.8f}\t{start/RATE:.5f}\n')
            if id_.endswith('.'+PICKS[role]):
                save(context/(id_+'.with-fx.wav'), mix)
                save(context/(id_+'.without-fx.wav'), bed)


if __name__ == '__main__':
    main(pathlib.Path(sys.argv[1]))
