#!/usr/bin/env python3
"""Summarize paired stress CSV/log files; never select a best trial."""
import argparse
import collections
import csv
import pathlib
import re
import sys

parser=argparse.ArgumentParser()
parser.add_argument('directory',type=pathlib.Path)
args=parser.parse_args()
pattern=re.compile(r'^(\d+) Hz (\d+) samples (\d+) initial voices: peak ([\d.]+) DC ([\d.]+) grains (\d+) alloc (\d+) lock (\d+)')
writer=csv.writer(sys.stdout,lineterminator='\n')
writer.writerow(['version','trial','rate_hz','frames','initial_voices','pattern','samples',
                 'wall_p50_us','wall_p95_us','wall_p99_us','wall_max_us',
                 'cpu_p50_us','cpu_p95_us','cpu_p99_us','cpu_max_us',
                 'wall_misses','cpu_misses','peak','finite_window_dc','allocations','locks'])
for trial in (1,2,3):
    for version in ('original','fixed'):
        stem=args.directory/f'fx-adaa-pair-{version}-{trial}'
        stats={};active=None
        for line in stem.with_suffix('.log').read_text().splitlines():
            if line.startswith('Random MIDI without Pattern'):active='0'
            elif line.startswith('Max-ratchet synchronized Pattern'):active='1'
            m=pattern.match(line)
            if m:
                if active is None:raise RuntimeError('scenario label missing')
                rate,frames,voices,peak,dc,grains,alloc,locks=m.groups()
                stats[(rate,frames,voices,active)]=(peak,dc,alloc,locks)
        with stem.with_suffix('.csv').open(newline='') as f:
            rows=list(csv.DictReader(f))
        groups=collections.defaultdict(list)
        for row in rows:
            groups[(row['rate_hz'],row['frames'],row['initial_voices'],row['pattern'])].append(row)
        if len(groups)!=72 or len(rows)!=11520:raise RuntimeError('unexpected raw timing coverage')
        for key,items in sorted(groups.items(),key=lambda item:tuple(map(int,item[0]))):
            if key not in stats or len(items)!=160:raise RuntimeError('missing matching case')
            wall=sorted(float(x['wall_us']) for x in items)
            cpu=sorted(float(x['thread_cpu_us']) for x in items)
            if min(cpu)<0:raise RuntimeError('thread CPU counter unavailable')
            pct=lambda values:[f'{values[i]:.3f}' for i in (79,151,158,159)]
            writer.writerow([version,trial,*key,len(items),*pct(wall),*pct(cpu),
                             sum(int(x['wall_miss']) for x in items),sum(int(x['cpu_miss']) for x in items),
                             *stats[key]])
