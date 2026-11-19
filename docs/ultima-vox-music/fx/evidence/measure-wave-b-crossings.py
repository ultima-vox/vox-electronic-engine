#!/usr/bin/env python3
"""Measure raw incremental Wave B levels without modifying listening WAVs (NumPy 2.2.6)."""
import csv
from pathlib import Path
import runpy
import sys

import numpy as np

here = Path(__file__).parent
signals = runpy.run_path(str(here / 'measure-wave-b-signals.py'))


def main(root, tranche):
    with (here / f'wave-b-{tranche}-creative-brief.tsv').open() as stream:
        ids = [row['preset_id'] for row in csv.DictReader(stream, delimiter='\t')]
    with (root / 'levels.tsv').open('w') as out:
        out.write('id\traw-peak-dBFS\tactive-rms-dBFS\tactive-window-s\n')
        for id_ in ids:
            signal = signals['common']['load'](root / 'solo' / (id_ + '.wav'))
            rms, seconds = signals['rms_active'](signal)
            out.write(f'{id_}\t{20*np.log10(np.max(np.abs(signal))):.3f}\t{20*np.log10(rms):.3f}\t{seconds:.3f}\n')


if __name__ == '__main__':
    if len(sys.argv) != 2 and not (len(sys.argv) == 3 and sys.argv[2] == '--room-cuts'):
        sys.exit('usage: measure-wave-b-crossings.py <export directory> [--room-cuts]')
    main(Path(sys.argv[1]), 'fiftysix' if len(sys.argv) == 3 else 'fortyeight')
