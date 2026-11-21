#!/usr/bin/env python3
"""Check analysis arithmetic and captured evidence completeness, not quality."""
import csv
import math
import pathlib
import numpy as np
from study import N, outside_harmonics

root = pathlib.Path(__file__).resolve().parents[2]
n = np.arange(N)
pure = np.sin(2*np.pi*17*n/N)
mixed = pure + .5*np.sin(2*np.pi*37*n/N)
assert outside_harmonics(pure, 17) < 1e-12
assert abs(outside_harmonics(mixed, 17) - .2) < 1e-10
with (root/'docs/ultima-vox-music/fx/evidence/wavefold-study-20261121.csv').open() as f:
    rows = list(csv.DictReader(f))
assert len(rows) == 45
keys = {(int(r['rate_hz']), int(r['target_hz']), r['method']) for r in rows}
for rate in (44100,48000,96000):
    for target in (2000,5000,8000):
        for name in ('production','filtered_2x_offline','smooth_sine_adaa_alternative','filtered_2x_linear_only'):
            assert (rate,target,name) in keys
    for name in ('production','filtered_2x_offline','smooth_sine_adaa_alternative'):
        assert (rate,7000,name) in keys
for r in rows:
    assert abs(float(r['actual_hz'])-int(r['target_hz'])) <= int(r['rate_hz'])/(2*N)+1e-4
    assert math.isfinite(float(r['rms'])) and math.isfinite(float(r['peak']))
    if r['outside_harmonic_ratio_lower_bound']:
        assert 0 <= float(r['outside_harmonic_ratio_lower_bound']) <= 1
anchors = {2000:.1233,5000:.8683,8000:.9041}
for target,expected in anchors.items():
    actual=next(float(r['outside_harmonic_ratio_lower_bound']) for r in rows
                if r['rate_hz']=='48000' and r['target_hz']==str(target) and r['method']=='production')
    assert abs(actual-expected)<.001
with (root/'docs/ultima-vox-music/fx/evidence/wavefold-spectra-20261121.csv').open() as f:
    spectra=list(csv.DictReader(f))
assert len(spectra)==1152
for rate in ('44100','48000','96000'):
    for target in ('2000','5000','8000','7000'):
        for name in ('production','filtered_2x_offline','smooth_sine_adaa_alternative'):
            group=[r for r in spectra if r['rate_hz']==rate and r['target_hz']==target and r['method']==name]
            assert len(group)==32 and {int(r['rank']) for r in group}==set(range(1,33))
            assert 0 <= sum(float(r['positive_power_fraction']) for r in group) <= 1.000001
with (root/'docs/ultima-vox-music/fx/evidence/wavefold-runtime-20261121.csv').open() as f:
    timings=list(csv.DictReader(f))
assert len(timings)==1440
for voices in ('1','4','8'):
    for trial in ('1','2','3'):
        assert sum(r['initial_voices']==voices and r['trial']==trial for r in timings)==160
for r in timings:
    assert float(r['wall_us'])>=0 and float(r['thread_cpu_us'])>=0
    assert int(r['wall_miss'])==(float(r['wall_us'])>float(r['budget_us']))
    assert int(r['cpu_miss'])==(float(r['thread_cpu_us'])>float(r['budget_us']))
print('Wavefold diagnostic arithmetic and raw data coverage PASS; no quality gate claimed')
