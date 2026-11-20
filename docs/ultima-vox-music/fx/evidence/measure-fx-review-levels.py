#!/usr/bin/env python3
"""Audit the published A120+B56 solo references without changing the archive.

Usage: measure-fx-review-levels.py <wave-a-export> <wave-b-export> > working-matrix.tsv
Requires NumPy 2.2.6. All inputs are checked against the published review matrix.
"""
import csv
import hashlib
from pathlib import Path
import re
import runpy
import sys

import numpy as np

HERE = Path(__file__).resolve().parent


def measure(wave_a, wave_b):
    tools = runpy.run_path(str(HERE / 'measure-wave-b-signals.py'))
    load, active_rms = tools['common']['load'], tools['rms_active']
    with (HERE / 'fx-review-176-arrangements.tsv').open(newline='') as matrix:
        reader = csv.DictReader(matrix, delimiter='\t')
        fields = list(reader.fieldnames)
        rows = list(reader)
    if len(rows) != 176 or len({row['id'] for row in rows}) != 176:
        raise ValueError('expected 176 unique reference entries')
    for row in rows:
        id_, wave = row['id'], row['wave']
        if wave not in ('A', 'B') or not re.fullmatch(r'voxfx\.[a-z0-9-]+\.[a-z0-9-]+', id_):
            raise ValueError(f'unrecognized reference: {id_}')
        path = (wave_a if wave == 'A' else wave_b / 'solo') / f'{id_}.wav'
        with path.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != row['raw-wav-sha256']:
                raise ValueError(f'WAV differs from pinned reference: {id_}')
        signal = load(path)
        peak = np.max(np.abs(signal))
        rms, seconds = active_rms(signal)
        if not (np.isfinite(rms) and np.isfinite(peak) and rms > 0 and peak > 0):
            raise ValueError(f'invalid or silent reference: {id_}')
        peak_db, rms_db = 20*np.log10(peak), 20*np.log10(rms)
        if row['measured-active-rms-dBFS'] != '-' and f'{rms_db:.3f}' != row['measured-active-rms-dBFS']:
            raise ValueError(f'different measurement for previously measured reference: {id_}')
        row.update({'measured-active-rms-dBFS': f'{rms_db:.3f}', 'raw-peak-dBFS': f'{peak_db:.3f}',
                    'active-window-s': f'{seconds:.3f}',
                    'review-queue': 'prioritize-gain-staging' if rms_db < -60 else 'regular-listening'})
    # Emit the complete review sheet only after every raw input has passed verification.
    i = fields.index('review-status')
    fields[i:i] = ['raw-peak-dBFS', 'active-window-s', 'review-queue']
    writer = csv.DictWriter(sys.stdout, fieldnames=fields, delimiter='\t', lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    measure(*(Path(arg) for arg in sys.argv[1:]))
