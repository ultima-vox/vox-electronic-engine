#!/usr/bin/env python3
"""Compare parent point-sampled Wavefold and this checkout, at Chain level.

Usage: python3 tools/compare_wavefold_stationary.py OUTPUT_DIRECTORY
The generated listening pairs are not host renders or listening approval.
"""
import array
import csv
import hashlib
import math
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
PARENT = 'c7ee608a0879087d9dade1f2471c69a1bde92732'
if len(sys.argv) != 2 or sys.byteorder != 'little':
    raise SystemExit(__doc__)
out = pathlib.Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)

def mean(a): return math.fsum(a) / len(a)
def rms(a): return math.sqrt(math.fsum(x*x for x in a) / len(a))

def save_wave(path, samples, factor, rate):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        # Equal RMS after matching; common gain only reduces peaks if needed.
        w.writeframes(b''.join(struct.pack('<h', round(max(-1., min(1., v*factor))*32767)) for v in samples))

with tempfile.TemporaryDirectory(prefix='vox-wavefold-stationary-') as tmp:
    temp = pathlib.Path(tmp); (temp/'engine').mkdir()
    (temp/'engine/Effects.h').write_bytes(subprocess.check_output(
        ['git', 'show', PARENT+':products/fx/engine/Effects.h'], cwd=ROOT))
    for name, include in [('parent', temp), ('candidate', ROOT/'products/fx')]:
        subprocess.run(['g++', '-std=c++20', '-O3', '-I'+str(include), '-I'+str(ROOT/'products/fx'),
                        str(ROOT/'tools/fx_wavefold_stationary.cpp'), '-o', str(temp/name)], check=True)

    def capture(name, rate, mode, wet, slots):
        result = array.array('f')
        result.frombytes(subprocess.check_output([str(temp/name), str(rate), str(mode), str(wet), str(slots)]))
        if len(result) != (65536 if mode == 0 else 2*rate) or not all(math.isfinite(x) for x in result):
            raise RuntimeError('invalid or nonfinite capture')
        return result

    with (out/'stationary.csv').open('w', newline='') as f, (out/'phase.csv').open('w', newline='') as phase_file:
        writer = csv.writer(f, lineterminator='\n'); phase_writer = csv.writer(phase_file, lineterminator='\n')
        writer.writerow(['rate_hz','wet','slots','samples','input_mean','parent_mean','candidate_mean','parent_abs_dc','candidate_abs_dc','mean_difference','threshold','parent_rms','candidate_rms'])
        phase_writer.writerow(['rate_hz','wet','slots','frequency_hz','parent_fundamental','candidate_fundamental','parent_phase_deg','candidate_phase_deg','phase_difference_deg','parent_vs_dry_db','candidate_vs_dry_db'])
        cosine = [math.cos(2*math.pi*i/256) for i in range(256)]
        sine = [math.sin(2*math.pi*i/256) for i in range(256)]
        def component(a):
            re = math.fsum(x*cosine[i%256] for i,x in enumerate(a))*2/len(a)
            im = -math.fsum(x*sine[i%256] for i,x in enumerate(a))*2/len(a)
            return complex(re,im)
        for rate in (44100, 48000, 96000):
            dry = component(capture('parent', rate, 0, 0., 1))
            for wet in (0., .5, 1.):
                for slots in (1, 4):
                    old = capture('parent', rate, 0, wet, slots)
                    new = capture('candidate', rate, 0, wet, slots)
                    # This coherent input is exactly antisymmetric per 256 samples.
                    old_mean, new_mean = mean(old), mean(new)
                    threshold = 1e-4
                    old_rms, new_rms = rms(old), rms(new)
                    writer.writerow([rate,wet,slots,len(old),0,old_mean,new_mean,abs(old_mean),abs(new_mean),new_mean-old_mean,threshold,old_rms,new_rms])
                    if abs(old_mean) >= threshold or abs(new_mean) >= threshold or min(old_rms,new_rms) < 1e-6:
                        raise RuntimeError('stationary DC limit exceeded; retain the output')
                    old_h, new_h = component(old), component(new)
                    if min(abs(old_h),abs(new_h),abs(dry)) < 1e-10: raise RuntimeError('fundamental is too small for phase measurement')
                    arg = lambda z: math.degrees(math.atan2(z.imag,z.real))
                    phase_writer.writerow([rate,wet,slots,rate/256,abs(old_h),abs(new_h),arg(old_h),arg(new_h),arg(new_h/old_h),20*math.log10(abs(old_h/dry)),20*math.log10(abs(new_h/dry))])

    with (out/'listening-pairs.csv').open('w', newline='') as f:
        writer = csv.writer(f, lineterminator='\n')
        writer.writerow(['rate_hz','parent_rms','candidate_rms','parent_gain','candidate_gain','common_peak_gain','matched_rms','rms_difference','parent_sha256','candidate_sha256'])
        for rate in (44100, 48000, 96000):
            old = capture('parent', rate, 1, 1., 4); new = capture('candidate', rate, 1, 1., 4)
            old_rms, new_rms = rms(old), rms(new)
            if min(old_rms, new_rms) <= 1e-12: raise RuntimeError('silent capture')
            target = min(old_rms, new_rms)
            old_gain, new_gain = target/old_rms, target/new_rms
            peak = max(max(abs(x*old_gain) for x in old), max(abs(x*new_gain) for x in new))
            common = min(1., .98/peak) if peak else 1.
            old_path, new_path = out/f'{rate}-parent.wav', out/f'{rate}-candidate.wav'
            save_wave(old_path, old, old_gain*common, rate); save_wave(new_path, new, new_gain*common, rate)
            diff = math.sqrt(math.fsum((a*old_gain-b*new_gain)**2 for a,b in zip(old,new))/len(old))*common
            digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
            writer.writerow([rate,old_rms,new_rms,old_gain,new_gain,common,target*common,diff,digest(old_path),digest(new_path)])
