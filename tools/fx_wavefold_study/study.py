#!/usr/bin/env python3
"""Offline Wavefold study; neither experimental path is production DSP.

Requires numpy, scipy and a C++20 compiler. Outputs a CSV on stdout. References
use zero-phase offline resample_poly, hence no RT, latency or host claims.
"""
import argparse
import csv
import pathlib
import subprocess
import sys
import tempfile
import time

import numpy as np
from scipy import signal

ROOT = pathlib.Path(__file__).resolve().parents[2]
N = 16384
RATES = (44100, 48000, 96000)
FREQUENCIES = (2000, 5000, 8000)


def fold(x):
    x = np.asarray(x, dtype=np.float64)
    return (2 / np.pi) * np.arcsin(np.sin(np.pi * np.clip(x * 8, -8, 8) * .5))


def oversampled(x, factor):
    # Offline, delay-compensated polyphase reference; not a causal plugin path.
    up = signal.resample_poly(x, factor, 1, window=('kaiser', 10.0))
    return signal.resample_poly(fold(up), 1, factor, window=('kaiser', 10.0))


def smooth_adaa(x):
    # Analytically valid alternative transfer sin(4*pi*x), NOT the same sound.
    k = 4 * np.pi
    previous = np.roll(x, 1)
    previous[0] = 0
    difference = x - previous
    midpoint = .5 * (x + previous)
    return np.sin(k * midpoint) * np.sinc(k * difference / (2 * np.pi))


def outside_harmonics(y, bin_index):
    spec = np.fft.rfft(y.astype(np.float64))
    power = np.abs(spec) ** 2
    # DC and Nyquist weights differ; don't include either in this comparison.
    power[0] = 0
    power[-1] = 0
    harmonics = np.arange(bin_index, len(power) - 1, bin_index)
    total = power.sum()
    return max(0.0, (total - power[harmonics].sum()) / total) if total else 0.0


def make_signal(rate, bin_index, multi):
    settle = rate // 2
    sample = np.arange(settle + N, dtype=np.float64)
    sine = lambda b: np.sin(2 * np.pi * b * sample / N)
    x = (.42 * sine(bin_index) + .31 * sine(round(9000*N/rate)) +
         .22 * sine(round(11000*N/rate))) if multi else sine(bin_index)
    return x.astype(np.float32), settle


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--spectra-csv', type=pathlib.Path,
                        help='optional top-32 spectral-bin CSV per signal/method')
    args = parser.parse_args()
    spectra_file = args.spectra_csv.open('w', newline='') if args.spectra_csv else None
    spectra = csv.writer(spectra_file, lineterminator='\n') if spectra_file else None
    if spectra:
        spectra.writerow(['rate_hz','target_hz','signal','method','rank','bin','frequency_hz',
                          'positive_power_fraction','inband_integer_harmonic'])
    with tempfile.TemporaryDirectory(prefix='vox-fx-wavefold-') as temp:
        binary = pathlib.Path(temp) / 'capture'
        subprocess.run(['g++', '-std=c++20', '-O3', '-I' + str(ROOT/'products/fx'),
                        str(ROOT/'tools/fx_wavefold_study/capture.cpp'), '-o', str(binary)], check=True)
        writer = csv.writer(sys.stdout, lineterminator='\n')
        writer.writerow(['rate_hz','target_hz','actual_hz','signal','method','rms','peak',
                         'outside_harmonic_ratio_lower_bound','waveform_error_vs_16x',
                         'offline_wall_ms','offline_thread_cpu_ms','linear_roundtrip_gain'])
        for rate in RATES:
            for target, multi in [(f, False) for f in FREQUENCIES] + [(7000, True)]:
                bin_index = round(target * N / rate)
                x, settle = make_signal(rate, bin_index, multi)
                captured = subprocess.run([str(binary), str(rate), str(bin_index), str(N), str(int(multi))],
                                          check=True, stdout=subprocess.PIPE).stdout
                baseline = np.frombuffer(captured, dtype=np.float32).copy()
                if baseline.size != N:
                    raise RuntimeError('production capture length mismatch')
                # Verify the ideal amount=1 model against the settled production path;
                # float parameter smoothing stalls just below 1 at finite precision.
                expected = fold(x[settle:]).astype(np.float32)
                if not np.allclose(baseline, expected, rtol=0, atol=2e-4):
                    raise RuntimeError('production transfer does not match the offline model')
                reference = oversampled(x, 16)[settle:settle+N]
                methods = [('production', lambda: baseline),
                           ('filtered_2x_offline', lambda: oversampled(x, 2)[settle:settle+N]),
                           ('smooth_sine_adaa_alternative', lambda: smooth_adaa(x)[settle:settle+N])]
                for name, fn in methods:
                    # Production capture was already performed in a separate
                    # process; never present merely returning its array as DSP cost.
                    # Other timings are three offline whole-buffer calls, not a
                    # realtime plugin cost or FX-T18 qualification.
                    walls, cpus = [], []
                    for _ in range(1 if name == 'production' else 3):
                        t, c = time.perf_counter_ns(), time.thread_time_ns()
                        y = fn()
                        cpus.append((time.thread_time_ns()-c)/1e6)
                        walls.append((time.perf_counter_ns()-t)/1e6)
                    err = float(np.mean((y-reference)**2) / max(np.mean(reference**2), 1e-30))
                    alias = '' if multi else f'{outside_harmonics(y, bin_index):.8f}'
                    if spectra:
                        power = np.abs(np.fft.rfft(y.astype(np.float64))) ** 2
                        power[0] = 0
                        power[-1] = 0
                        total = max(power.sum(), 1e-30)
                        for rank, b in enumerate(np.argsort(power)[-32:][::-1], 1):
                            harmonic = '' if multi else int(b > 0 and b % bin_index == 0)
                            spectra.writerow([rate,target,'three_tone' if multi else 'sine',name,
                                              rank,int(b),f'{b*rate/N:.6f}',
                                              f'{power[b]/total:.12g}',harmonic])
                    writer.writerow([rate,target,f'{bin_index*rate/N:.5f}',
                                     'three_tone' if multi else 'sine',name,
                                     f'{np.sqrt(np.mean(y*y)):.8f}',f'{np.max(np.abs(y)):.8f}',alias,
                                     f'{err:.8f}', '' if name == 'production' else f'{np.median(walls):.4f}',
                                     '' if name == 'production' else f'{np.median(cpus):.4f}', ''])
                if not multi:
                    # Linear filtering only, no Wavefold: frequency response of
                    # this offline up/down pair; not a causal implementation.
                    linear = signal.resample_poly(
                        signal.resample_poly(x, 2, 1, window=('kaiser', 10.0)),
                        1, 2, window=('kaiser', 10.0))[settle:settle+N]
                    dry = x[settle:settle+N]
                    gain = np.sqrt(np.mean(linear*linear) / np.mean(dry*dry))
                    writer.writerow([rate,target,f'{bin_index*rate/N:.5f}','sine',
                                     'filtered_2x_linear_only',f'{np.sqrt(np.mean(linear*linear)):.8f}',
                                     f'{np.max(np.abs(linear)):.8f}','','','','',f'{gain:.8f}'])
    if spectra_file:
        spectra_file.close()


if __name__ == '__main__':
    main()
