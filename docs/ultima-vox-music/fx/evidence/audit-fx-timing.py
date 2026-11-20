#!/usr/bin/env python3
"""Check completeness of one three-profile FX-T18 capture; never certify release.

Usage: audit-fx-timing.py <routed.log> <authored.log> <typical.log>
Accepts direct stdout or verbose CTest output. Output is JSON; keep raw logs.
"""
import argparse
import itertools
import json
from pathlib import Path
import re


CASE = re.compile(r'^(\d+) Hz (\d+) samples (\d+) initial voices: .* alloc (\d+) lock (\d+) ')
SUMMARY = re.compile(r'misses wall/CPU/wall-only (\d+)/(\d+)/(\d+) of (\d+)$')
PAIRS = re.compile(r'thread CPU valid (\d+)/(\d+); zero deltas (\d+); min positive delta ([\d.]+) us$')
MISS = re.compile(r'deadline miss: rate=(\d+) frames=(\d+) initialVoices=(\d+) pattern=(on|off) block=(\d+) '
                  r'events=(\d+) grains=(\d+) wall_us=([\d.]+) cpu_us=(-?[\d.]+) budget_us=([\d.]+) '
                  r'wall_miss=([01]) cpu_miss=([01])$')


def read(path):
    data = path.read_bytes()
    return data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')


def audit(profile, path):
    pattern = None
    cases = {}
    env = []
    probes = {}
    binaries = []
    headlines = []
    cell = None
    for original in read(path).splitlines():
        line = re.sub(r'^\d+:\s*', '', original).strip()
        if line.startswith('Timing environment:'):
            env.append(line)
        elif line.startswith(('Allocation probe', 'Lock probe')):
            kind = line.split()[0].lower()
            if kind in probes:
                raise ValueError(f'{path}: duplicate {kind} probe statement')
            probes[kind] = line
        elif line.startswith(('Typical profile:', 'Non-default grain shape')):
            headlines.append(line)
        elif line == 'Random MIDI without Pattern':
            pattern = 'off'
        elif line == 'Max-ratchet synchronized Pattern + random MIDI':
            pattern = 'on'
        elif m := CASE.match(line):
            key = (*map(int, m.groups()[:3]), pattern)
            if pattern is None or key in cases:
                raise ValueError(f'{path}: duplicate cell or missing Pattern context: {key}')
            cell = cases[key] = {'alloc': int(m[4]), 'lock': int(m[5]), 'misses': {}}
        elif cell is not None and (m := SUMMARY.search(line)):
            if 'summary' in cell:
                raise ValueError(f'{path}: duplicate summary')
            cell['summary'] = tuple(map(int, m.groups()))
        elif cell is not None and (m := PAIRS.fullmatch(line)):
            if 'pairs' in cell:
                raise ValueError(f'{path}: duplicate CPU read coverage')
            cell['pairs'] = (*map(int, m.groups()[:3]), float(m[4]))
        elif m := MISS.fullmatch(line):
            config = (*map(int, m.groups()[:3]), m[4])
            block, events, grains = map(int, m.groups()[4:7])
            wall, cpu, budget = map(float, m.groups()[7:10])
            wall_miss, cpu_miss = map(int, m.groups()[10:])
            if (config not in cases or cases[config] is not cell or block in cell['misses'] or
                    not 0 <= block < 160 or not (wall_miss or cpu_miss) or
                    abs(budget - config[1]*1e6/config[0]) > .001 or cpu < 0 or
                    any(flag != (value > budget) and abs(value - budget) > .001
                        for flag, value in ((wall_miss, wall), (cpu_miss, cpu)))):
                raise ValueError(f'{path}: invalid callback miss detail: {line}')
            cell['misses'][block] = dict(rate=config[0], frames=config[1], initial_voices=config[2],
                                        pattern=config[3], block=block, events=events, grains=grains,
                                        wall_us=wall, cpu_us=cpu, budget_us=budget,
                                        wall_miss=wall_miss, cpu_miss=cpu_miss)
        elif line.startswith('FX JUCE runtime stress:'):
            binaries.append(line)

    expected = set(itertools.product((44100, 48000, 96000), (64, 128, 512),
                                     (1,) if profile == 'typical' else (1, 4, 8),
                                     ('off',) if profile == 'typical' else ('off', 'on')))
    if (set(cases) != expected or len(env) != 1 or set(probes) != {'allocation', 'lock'} or
            binaries != ['FX JUCE runtime stress: 0 failing cases'] or
            any(h.startswith('Typical profile:') for h in headlines) != (profile == 'typical') or
            any(h.startswith('Non-default grain shape') for h in headlines) != (profile == 'authored')):
        raise ValueError(f'{path}: incomplete or mislabeled {profile} capture')

    counts = dict(callbacks=0, cpu_pairs=0, zero_cpu_deltas=0, wall_misses=0, cpu_misses=0,
                  scoped_allocations=0, wrapped_locks=0, accounting_limited_cells=0)
    details = []
    for key, cell in cases.items():
        if 'pairs' not in cell or 'summary' not in cell:
            raise ValueError(f'{path}: missing CPU evidence for {key}')
        wall, cpu, wall_only, total = cell['summary']
        valid, requested, zeros, min_positive = cell['pairs']
        misses = list(cell['misses'].values())
        if (valid != 160 or total != 160 or requested != 160 or not 0 <= zeros <= valid or
                wall != sum(x['wall_miss'] for x in misses) or cpu != sum(x['cpu_miss'] for x in misses) or
                wall_only != sum(x['wall_miss'] and not x['cpu_miss'] for x in misses)):
            raise ValueError(f'{path}: callback counters do not reconcile for {key}')
        limits = min_positive == 0 or zeros > 0 or min_positive >= key[1] * 1e6 / key[0]
        counts['accounting_limited_cells'] += limits
        counts['callbacks'] += total
        counts['cpu_pairs'] += valid
        counts['zero_cpu_deltas'] += zeros
        counts['wall_misses'] += wall
        counts['cpu_misses'] += cpu
        counts['scoped_allocations'] += cell['alloc']
        counts['wrapped_locks'] += cell['lock']
        details.extend(misses)
    return dict(environment=env[0], probes=probes, cells=len(cases), counts=counts, deadline_callbacks=details)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('routed', 'authored', 'typical'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    try:
        result = {name: audit(name, path) for name, path in vars(args).items()}
    except (OSError, ValueError) as error:
        parser.exit(2, f'FX timing audit: {error}\n')
    print(json.dumps(dict(qualification='diagnostic only; verify timing resolution, load and host evidence',
                          profiles=result), indent=2))
