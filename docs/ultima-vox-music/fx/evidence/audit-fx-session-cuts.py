#!/usr/bin/env python3
"""Verify a Wave B export against a pinned baseline; emit metadata only.

Usage: audit-fx-session-cuts.py <export-root> <new-metadata-directory> [--edit-points]
The output is never an audio archive. Requires NumPy 2.2.6.
"""
import argparse
import collections
import csv
import hashlib
import math
from pathlib import Path
import re
import runpy


HERE = Path(__file__).resolve().parent


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def read_tsv(path):
    with path.open(newline='', encoding='utf-8') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def audit(root, output, edit_points=False):
    baseline_count, old_b, new_b = (200, 80, 112) if edit_points else (176, 56, 80)
    old_prefix = 'wave-b-eighty' if edit_points else 'wave-b-fiftysix'
    new_prefix = 'wave-b-onehundredtwelve' if edit_points else 'wave-b-eighty'
    baseline_path = HERE / f'fx-review-{baseline_count}-working-matrix.tsv'
    baseline = read_tsv(baseline_path)
    prior_b = {row['id'] for row in baseline if row['wave'] == 'B'}
    ids = {row['id'] for row in baseline}
    fingerprints = {row['raw-wav-sha256'] for row in baseline}
    if (len(ids) != baseline_count or len(baseline) != baseline_count or
            len(prior_b) != old_b or len(fingerprints) != baseline_count):
        raise ValueError('wrong baseline scope')

    checked = set()
    for line in (HERE / 'fx-review-176-members.sha256').read_text().splitlines():
        sha, name = line.split('  ', 1)
        if name.startswith(('Wave-B/solo/', 'Wave-B/presets/', 'Wave-B/sources/')):
            relative = name.removeprefix('Wave-B/')
            if relative in checked or digest(root / relative) != sha:
                raise ValueError(f'pinned Wave B member changed: {relative}')
            checked.add(relative)
    if edit_points:
        pinned_metadata = set()
        for line in (HERE / 'wave-b-eighty-members.sha256').read_text().splitlines():
            sha, name = line.split('  ', 1)
            if name.startswith(('solo/', 'presets/')):
                if name in checked or digest(root / name) != sha:
                    raise ValueError(f'pinned Session Cuts member changed: {name}')
                checked.add(name)
            elif name in {baseline_path.name, *(f'{old_prefix}-{name}.tsv' for name in ('manifest', 'metrics', 'settings'))}:
                if name in pinned_metadata or digest(HERE / name) != sha:
                    raise ValueError(f'pinned Session Cuts metadata changed: {name}')
                pinned_metadata.add(name)
            else:
                raise ValueError(f'unexpected Session Cuts pin: {name}')
        if len(pinned_metadata) != 4:
            raise ValueError('missing Session Cuts metadata pins')
    if len(checked) != 2 * old_b + 2:
        raise ValueError('missing pinned Wave B members')

    manifest = read_tsv(root / 'manifest.tsv')
    rates = read_tsv(root / 'metrics.tsv')
    settings = read_tsv(root / 'settings.tsv')
    for name in ('manifest', 'metrics', 'settings'):
        original = (HERE / f'{old_prefix}-{name}.tsv').read_bytes()
        if not (root / f'{name}.tsv').read_bytes().startswith(original):
            raise ValueError(f'pinned B{old_b} {name} prefix changed')
    b_ids = {row['id'] for row in manifest}
    if (len(manifest) != new_b or len(b_ids) != new_b or b_ids & ids != prior_b or
            [row['id'] for row in settings] != [row['id'] for row in manifest] or
            len(rates) != new_b * 3 or {(row['id'], row['rate']) for row in rates} !=
            {(id_, rate) for id_ in b_ids for rate in ('44100', '48000', '96000')}):
        raise ValueError('incomplete Wave B export')
    # The exporter checks decoded PCM metrics as well as Program structural
    # uniqueness and portable rerenders. This independently checks the table.
    for row in rates:
        peak, dc, mono, tail = (float(row[key]) for key in ('peak', 'DC', 'mono/stereo-rms', 'last-5ms-peak'))
        if not (.0001 < peak < .5 and 0 <= dc < .01 and .8 <= mono <= 1.000001 and 0 <= tail < .001):
            raise ValueError(f'out-of-bound render check: {row}')

    tools = runpy.run_path(str(HERE / 'measure-wave-b-signals.py'))
    load, active_rms = tools['common']['load'], tools['rms_active']
    added, pins = [], {}
    for cue in manifest[old_b:]:
        id_ = cue['id']
        if (id_ in ids or not re.fullmatch(r'voxfx\.[a-z0-9-]+\.[a-z0-9-]+', id_) or
                cue['status'] != 'staged-review-only' or cue['source'] != 'built-in'):
            raise ValueError(f'invalid new Wave B row: {id_}')
        ids.add(id_)
        wav, preset = f'solo/{id_}.wav', f'presets/{id_}.voxfx'
        sha = digest(root / wav)
        if sha != cue['solo-48k-sha256'] or sha in fingerprints:
            raise ValueError(f'changed or duplicate 48 kHz WAV: {id_}')
        fingerprints.add(sha)
        pins.update({wav: sha, preset: digest(root / preset)})
        signal = load(root / wav)
        rms, seconds = active_rms(signal)
        peak = abs(signal).max()
        if not (math.isfinite(rms) and math.isfinite(peak) and 0 < rms <= peak):
            raise ValueError(f'bad level data: {id_}')
        db = 20 * math.log10(rms)
        row = dict.fromkeys(baseline[0], '-')
        row.update({'id': id_, 'wave': 'B', 'name': cue['name'], 'role': cue['role'], 'style': cue['style'],
                    'mode': cue['mode'], 'bpm': cue['bpm'], 'arrangement-slot': cue['use'], 'tonality': cue['tonality'],
                    'raw-wav': f'Wave-B/{wav}', 'raw-wav-sha256': sha, 'portable': f'Wave-B/{preset}',
                    'source': cue['source'], 'provenance': cue['provenance'], 'measured-active-rms-dBFS': f'{db:.3f}',
                    'raw-peak-dBFS': f'{20 * math.log10(peak):.3f}', 'active-window-s': f'{seconds:.3f}',
                    'review-queue': 'prioritize-gain-staging' if db < -60 else 'regular-listening',
                    'review-status': 'not reviewed'})
        added.append(row)
    roles = collections.Counter(row['role'] for row in added)
    if (len(added) != new_b - old_b or len(roles) != 8 or set(roles.values()) != {(new_b - old_b) // 8} or
            len({row['mode'] for row in added}) != 6):
        raise ValueError('unexpected Wave B role/mode coverage')

    # Write no report until all pinned inputs and new identities are verified.
    output.mkdir(parents=True, exist_ok=False)
    for name in ('manifest', 'settings', 'metrics'):
        target = output / f'{new_prefix}-{name}.tsv'
        target.write_bytes((root / f'{name}.tsv').read_bytes())
        pins[target.name] = digest(target)
    matrix = output / f'fx-review-{baseline_count + len(added)}-working-matrix.tsv'
    # Preserve the old serialized rows, including all existing reviewer fields.
    matrix.write_bytes(baseline_path.read_bytes())
    with matrix.open('a', newline='', encoding='utf-8') as f:
        csv.DictWriter(f, fieldnames=list(baseline[0]), delimiter='\t', lineterminator='\n').writerows(added)
    pins[matrix.name] = digest(matrix)
    (output / f'{new_prefix}-members.sha256').write_text(''.join(f'{sha}  {name}\n' for name, sha in sorted(pins.items())))
    print(f'{len(checked)} prior B files unchanged; {len(added)} new cues, {len(ids)} total; no audio copied')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('export', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--edit-points', action='store_true')
    args = parser.parse_args()
    audit(args.export, args.output, args.edit_points)
