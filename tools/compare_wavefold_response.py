#!/usr/bin/env python3
"""Measure the Wavefold slot's linear response, parent versus ADAA2 candidate.

Usage: python3 tools/compare_wavefold_response.py OUTPUT_DIRECTORY
A low-level bin-centred sine keeps every slot inside the fold's linear region,
so the output/input ratio is the method's own response (gain, phase and any
low-pass droop). The theoretical column is the exact ADAA response for a linear
segment: first order multiplies by S(w) = (1+e^-jw)/2, second order by
(1+e^-jw+e^-j2w)/3. The latter has a null at fs/3 and is retained only to
reproduce the rejected prototype. These are Chain-level diagnostics, not host
renders or listening approval.
"""
import argparse
import csv
import math
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PARENT = 'c7ee608a0879087d9dade1f2471c69a1bde92732'


def wrap(degrees):
    while degrees > 180:
        degrees -= 360
    while degrees <= -180:
        degrees += 360
    return degrees


def run(binary, rate, amount, wet, slots, amplitude):
    text = subprocess.check_output(
        [str(binary), str(rate), str(amount), str(wet), str(slots), str(amplitude)],
        text=True)
    rows = []
    for line in text.splitlines():
        rate_hz, amt, w, s, f0, mag, phase = line.split(',')
        rows.append((float(f0), float(mag), float(phase)))
    return rows


def theory(f0, rate, amount, wet, slots, method):
    w = 2 * math.pi * f0 / rate
    if method == 'adaa1':
        s = complex((1 + math.cos(w)) / 2, -math.sin(w) / 2)
    else:
        s = complex((1 + math.cos(w) + math.cos(2 * w)) / 3,
                    -(math.sin(w) + math.sin(2 * w)) / 3)
    gain = 1 + 7 * amount
    h = (1 - wet) + wet * gain * s
    hp = (1 - wet) + wet * gain
    h = (h / hp) ** slots
    return 20 * math.log10(abs(h)), math.degrees(math.atan2(h.imag, h.real))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=pathlib.Path)
    parser.add_argument('--method', choices=('adaa1', 'adaa2'), default='adaa1')
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vox-wavefold-response-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'engine').mkdir()
        (tmp / 'engine/Effects.h').write_bytes(subprocess.check_output(
            ['git', 'show', PARENT + ':products/fx/engine/Effects.h'], cwd=ROOT))
        binaries = {}
        for name, include in [('parent', tmp), ('candidate', ROOT / 'products/fx')]:
            binary = tmp / name
            subprocess.run(['g++', '-std=c++20', '-O3', '-I' + str(include),
                            str(ROOT / 'tools/fx_wavefold_sweep.cpp'), '-o', str(binary)],
                           check=True)
            binaries[name] = binary
        configs = []
        for rate in (44100, 48000, 96000):
            for amount in (1.0, .28):
                for wet in (1.0, .5):
                    configs.append((rate, amount, wet, 1, .05))
            for wet in (1.0, .5):
                # 0.002 * 2.96^4 stays below the fold corner for four slots.
                configs.append((rate, .28, wet, 4, .002))
        rows = []
        for rate, amount, wet, slots, amplitude in configs:
            old = run(binaries['parent'], rate, amount, wet, slots, amplitude)
            new = run(binaries['candidate'], rate, amount, wet, slots, amplitude)
            if len(old) != len(new) or not old:
                raise RuntimeError('capture length mismatch')
            for (f0, old_mag, old_phase), (_, new_mag, new_phase) in zip(old, new):
                t_mag, t_phase = theory(f0, rate, amount, wet, slots, args.method)
                rows.append([rate, amount, wet, slots, f0, old_mag, new_mag,
                             new_mag - old_mag, old_phase, new_phase,
                             wrap(new_phase - old_phase), t_mag, t_phase])
        path = args.directory / 'wavefold-response.csv'
        with path.open('w', newline='') as f:
            writer = csv.writer(f, lineterminator='\n')
            writer.writerow(['rate_hz', 'amount', 'wet', 'slots', 'frequency_hz',
                             'parent_mag_db', 'candidate_mag_db', 'delta_mag_db',
                             'parent_phase_deg', 'candidate_phase_deg',
                             'delta_phase_deg', 'theory_delta_mag_db',
                             'theory_delta_phase_deg'])
            writer.writerows(rows)
        mag_error = max(abs(r[7] - r[11]) for r in rows)
        phase_error = max(abs(wrap(r[10] - r[12])) for r in rows)
        print(f'{len(rows)} measured points; theory max error '
              f'{mag_error:.6f} dB, {phase_error:.6f} deg')
        print('rate amount wet slots  worst droop   at Hz   @1k    @5k    @10k   @0.4rate')
        for rate, amount, wet, slots in {(r[0], r[1], r[2], r[3]) for r in rows}:
            group = [r for r in rows if (r[0], r[1], r[2], r[3]) ==
                     (rate, amount, wet, slots)]
            worst = min(group, key=lambda r: r[7])
            def near(hz):
                r = min(group, key=lambda x: abs(x[4] - hz))
                return f'{r[7]:+.2f}'
            print(f'{rate:5.0f} {amount:4.2f} {wet:4.1f} {slots:5d} '
                  f'{worst[7]:+8.2f} {worst[4]:7.0f}  {near(1000):>6} {near(5000):>6} '
                  f'{near(10000):>6} {near(.4 * rate):>7}')

if __name__ == '__main__':
    main()
