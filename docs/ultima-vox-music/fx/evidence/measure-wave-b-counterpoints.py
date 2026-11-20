#!/usr/bin/env python3
"""Add deterministic mix A/B fixtures for the 8 counterpoints (NumPy 2.2.6).
Usage: python measure-wave-b-counterpoints.py <Wave B 32 export directory>
Original synthesized backing fixtures; this is not a listening verdict.
"""
import csv
from pathlib import Path
import runpy
import sys

import numpy as np

common = runpy.run_path(str(Path(__file__).with_name('measure-wave-a-context.py')))


def table(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def main(root):
    picks = table(Path(__file__).with_name('wave-b-thirtytwo-creative-brief.tsv'))
    catalog = {m['id']: m for m in table(root / 'manifest.tsv')}
    settings = {m['id']: m for m in table(root / 'settings.tsv')}
    directory = root / 'in-context'
    directory.mkdir(exist_ok=True)
    rate = common['RATE']
    targets = common['TARGET']
    with (root / 'context-metrics.tsv').open('w') as out:
        out.write('id\tFX-trim-dB\tbed-peak\tmix-peak\theadroom-dBFS\tcue-seconds\n')
        for pick in picks:
            cue = pick['preset_id']
            entry = catalog[cue]
            s = settings[cue]
            role = entry['role']
            bpm = float(s['BPM'])
            beat = 60 / bpm
            enter = {'impact': 4, 'riser': 4, 'fall': 4, 'sweep': 3, 'zap': 3.5,
                     'rhythm': 4, 'atmosphere': 0, 'stinger': 4}[role] * beat
            start = round(rate * enter)
            end = enter + (4 * int(entry['bars']) * beat if int(entry['bars']) else float(s['seconds']))
            signal = common['load'](root / 'solo' / (cue + '.wav'))
            bed_frames = round(rate * max(8 * beat, end + 4 * beat))
            bed = np.zeros((max(bed_frames, start + len(signal)), 2))
            note = int(s['MIDI-note']) if entry['root'] == '-' else int(entry['root'])
            minor = entry['tonality'].lower() in ('minor', 'dorian', 'phrygian')
            bed[:bed_frames] = common['backing'](bed_frames, bpm, note, role, minor, end)
            if role == 'impact':
                # Carve a short kickless pocket around the strike.
                t = (np.arange(len(bed)) - start) / rate
                bed *= (.25 + .75 * np.clip((np.abs(t) - .045) / .05, 0, 1))[:, None]
            mix = bed.copy()
            trim = min(10**(24 / 20), targets[role] / np.max(np.abs(signal)))
            mix[start:start + len(signal)] += signal * trim
            peak = np.max(np.abs(mix))
            if not .01 < peak < .80:
                raise ValueError(f'{cue}: invalid mix peak {peak}')
            out.write(f'{cue}\t{20*np.log10(trim):.3f}\t{np.max(np.abs(bed)):.8f}\t{peak:.8f}\t'
                      f'{-20*np.log10(peak):.3f}\t{start/rate:.5f}\n')
            common['save'](directory / (cue + '.with-fx.wav'), mix)
            common['save'](directory / (cue + '.without-fx.wav'), bed)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(Path(sys.argv[1]))
