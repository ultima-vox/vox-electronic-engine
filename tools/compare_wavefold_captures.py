#!/usr/bin/env python3
"""Compare captured float outputs of this checkout with a fixed Git header."""
import array
import csv
import math
import pathlib
import subprocess
import sys
import tempfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='f3a30a7b15cf53253f727751715a770885f7b726'
if sys.byteorder != 'little': raise SystemExit('capture requires a little-endian host')
with tempfile.TemporaryDirectory(prefix='vox-adaa-compare-') as temp:
    path=pathlib.Path(temp);(path/'engine').mkdir()
    (path/'engine/Effects.h').write_bytes(subprocess.check_output(
        ['git','show',BASE+':products/fx/engine/Effects.h'],cwd=ROOT))
    source=str(ROOT/'tools/fx_wavefold_capture.cpp')
    common=['g++','-std=c++20','-O3']
    subprocess.run(common+['-I'+str(path),'-I'+str(ROOT/'products/fx'),source,'-o',str(path/'base')],check=True)
    subprocess.run(common+['-I'+str(ROOT/'products/fx'),source,'-o',str(path/'current')],check=True)
    writer=csv.writer(sys.stdout,lineterminator='\n')
    writer.writerow(['rate_hz','target_hz','bin','samples','max_abs_diff','rms_diff'])
    for rate in (44100,48000,96000):
        for frequency in (2000,5000,8000):
            bin_index=round(frequency*16384/rate)
            args=list(map(str,(rate,bin_index,16384,0)))
            values=[]
            for exe in ('base','current'):
                result=array.array('f');result.frombytes(subprocess.check_output([str(path/exe),*args],cwd=ROOT));values.append(result)
            if any(len(v)!=16384 for v in values):raise RuntimeError('unexpected capture size')
            diffs=[float(a)-float(b) for a,b in zip(*values)]
            writer.writerow([rate,frequency,bin_index,len(diffs),
                             format(max(map(abs,diffs)),'.12g'),
                             format(math.sqrt(sum(d*d for d in diffs)/len(diffs)),'.12g')])
