#!/usr/bin/env python3
"""Measure raw levels and create a shared bridge comparison (NumPy 2.2.6)."""
import csv
from pathlib import Path
import runpy
import sys

import numpy as np

here = Path(__file__).parent
common = runpy.run_path(str(here / 'measure-wave-a-context.py'))
rate = common['RATE']


def rms_active(signal):
    # 20 ms windows, keep windows within 30 dB of peak short-time power.
    # This intentionally isn't an ITU/EBU loudness measure.
    block = rate // 50
    energy = np.mean(signal[:len(signal) // block * block].reshape(-1, block, 2)**2, axis=(1, 2))
    active = energy[energy >= np.max(energy) * .001]
    return np.sqrt(np.mean(active)), len(active) / 50


def main(root):
    with (here / 'wave-b-forty-creative-brief.tsv').open() as stream:
        ids = [row['preset_id'] for row in csv.DictReader(stream, delimiter='\t')]
    bridge = ('voxfx.riser.lantern-parallax', 'voxfx.riser.vapour-splice')
    start = round(rate * 240 / 132)
    source = {id_: common['load'](root / 'solo' / (id_ + '.wav')) for id_ in ids}
    levels = {id_: rms_active(signal) for id_, signal in source.items()}
    # Match the two added FX components without exceeding the quieter cue's
    # +24 dB cap. This is gated RMS matching, not perceived loudness matching.
    target_rms = min(.02, *(levels[id_][0] * 10**(24 / 20) for id_ in bridge))
    length = max(start + len(source[id_]) for id_ in bridge)
    bed = common['backing'](length, 132, 62, 'riser', True, 1200 / 132)
    directory = root / 'bridge-ab'
    directory.mkdir(exist_ok=True)
    common['save'](directory / 'bed.wav', bed)
    with (root / 'levels.tsv').open('w') as out:
        out.write('id\traw-peak-dBFS\tactive-rms-dBFS\tactive-window-s\tbridge-trim-dB\tbridge-peak-dBFS\tbridge-FX-rms-dBFS\n')
        for id_, signal in source.items():
            value, active = levels[id_]
            other = '-\t-\t-'
            if id_ in bridge:
                trim = target_rms / value
                mix = bed.copy()
                mix[start:start + len(signal)] += trim * signal
                peak = np.max(np.abs(mix))
                if not np.isfinite(peak) or peak >= .80:
                    raise ValueError(f'{id_}: unsafe mix peak {peak}')
                common['save'](directory / (id_.rsplit('.', 1)[1] + '.wav'), mix)
                other = f'{20*np.log10(trim):.3f}\t{20*np.log10(peak):.3f}\t{20*np.log10(value*trim):.3f}'
            out.write(f'{id_}\t{20*np.log10(np.max(np.abs(signal))):.3f}\t{20*np.log10(value):.3f}\t{active:.3f}\t{other}\n')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: measure-wave-b-signals.py <Wave B 40 export directory>')
    main(Path(sys.argv[1]))
