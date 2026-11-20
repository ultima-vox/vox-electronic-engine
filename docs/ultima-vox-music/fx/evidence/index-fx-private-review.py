#!/usr/bin/env python3
"""Verify frozen A120+B112 locally and emit a transfer index; never copy any media.

Usage: index-fx-private-review.py <A120-root> <B112-root> <new-metadata-dir>
"""
import argparse
import csv
import hashlib
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
MATRIX = 'fx-review-232-working-matrix.tsv'
MATRIX_SHA = '72fd9042415c70fec14e760ce16ebe4ae852bb50118c2c9e6e6deb6f039a1213'


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def pins(file):
    result = {}
    for line in (HERE / file).read_text(encoding='utf-8').splitlines():
        sha, name = line.split('  ', 1)
        name = name.removeprefix('./')
        if name in result or not re.fullmatch(r'[0-9a-f]{64}', sha):
            raise ValueError(f'invalid pin: {file}: {name}')
        result[name] = sha
    return result


def index(a, b, output):
    if digest(HERE / MATRIX) != MATRIX_SHA:
        raise ValueError('the as-published working matrix changed')
    old = pins('fx-review-176-members.sha256')
    prior_a = pins('wave-a-120-members.sha256')
    b_pins = {k.removeprefix('Wave-B/'): v for k, v in old.items() if k.startswith('Wave-B/')}
    for tranche in ('eighty', 'onehundredtwelve'):
        for name, sha in pins(f'wave-b-{tranche}-members.sha256').items():
            if name.startswith(('solo/', 'presets/', 'sources/')):
                if name in b_pins:
                    raise ValueError(f'duplicate B pin: {name}')
                b_pins[name] = sha
    if len(b_pins) != 226:
        raise ValueError(f'wrong B payload scope: {len(b_pins)}')

    roots = {'A': a, 'B': b, 'evidence': HERE}
    assets = {}

    def add(delivery, root, name, sha, kind):
        if delivery in assets:
            raise ValueError(f'duplicate delivery path: {delivery}')
        candidate = roots[root] / name
        if digest(candidate) != sha:
            raise ValueError(f'hash mismatch: {delivery}')
        assets[delivery] = (root, name, kind, candidate.stat().st_size, sha)

    with (HERE / MATRIX).open(newline='', encoding='utf-8') as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    ids, sources = set(), set()
    for row in rows:
        id_, wave = row['id'], row['wave']
        if (wave not in ('A', 'B') or id_ in ids or row['review-status'] != 'not reviewed' or
                not re.fullmatch(r'voxfx\.[a-z0-9-]+\.[a-z0-9-]+', id_)):
            raise ValueError(f'invalid review row: {id_}')
        ids.add(id_)
        wav, preset = f'solo/{id_}.wav', f'presets/{id_}.voxfx'
        if row['raw-wav'] != f'Wave-{wave}/{wav}' or row['portable'] != f'Wave-{wave}/{preset}':
            raise ValueError(f'invalid working matrix paths: {id_}')
        local_wav = f'{id_}.wav' if wave == 'A' else wav
        wave_pins = prior_a if wave == 'A' else b_pins
        for member, local, kind in ((wav, local_wav, 'raw-solo'), (preset, preset, 'portable-recipe')):
            sha = wave_pins[local]
            delivery = f'Wave-{wave}/{member}'
            if (kind == 'raw-solo' and sha != row['raw-wav-sha256']) or old.get(delivery, sha) != sha:
                raise ValueError(f'pin disagreement: {delivery}')
            add(delivery, wave, local, sha, kind)
        if row['source'] != 'built-in':
            path = row['source']
            if wave != 'B' or not re.fullmatch(r'Wave-B/sources/[a-z0-9-]+\.wav', path):
                raise ValueError(f'unexpected source: {id_}')
            local = path.removeprefix('Wave-B/')
            sha = b_pins[local]
            if row['source-sha256'] not in ('-', sha):
                raise ValueError(f'source pin disagreement: {id_}')
            if path not in sources:
                add(path, 'B', local, sha, 'required-source')
                sources.add(path)
    if len(ids) != 232 or len(sources) != 2 or sum(r['wave'] == 'A' for r in rows) != 120 or len(assets) != 466:
        raise ValueError('unmatched catalog, WAV, recipe or source')
    last_pins = pins('wave-b-onehundredtwelve-members.sha256')
    add(MATRIX, 'evidence', MATRIX, MATRIX_SHA, 'working-matrix')
    add('Wave-A/audition-settings.tsv', 'A', 'audition-settings.tsv', prior_a['audition-settings.tsv'], 'replay-settings')
    add('Wave-B/settings.tsv', 'B', 'settings.tsv', last_pins['wave-b-onehundredtwelve-settings.tsv'], 'replay-settings')

    # Do not write a partial index when local input is missing or changed.
    output.mkdir(parents=True, exist_ok=False)
    with (output / 'private-transfer-index.tsv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f, delimiter='\t', lineterminator='\n')
        writer.writerow(('delivery-path', 'local-root', 'local-path', 'kind', 'bytes', 'sha256'))
        writer.writerows((name, *assets[name]) for name in sorted(assets))
    with (output / 'SHA256SUMS').open('w', newline='', encoding='ascii') as f:
        for name in sorted(assets):
            f.write(f'{assets[name][-1]}  {name}\n')
    print(f'{len(ids)} review-only cues; {len(assets)} checked members; metadata only')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('a', 'b', 'output'):
        parser.add_argument(arg, type=Path)
    args = parser.parse_args()
    index(args.a, args.b, args.output)
