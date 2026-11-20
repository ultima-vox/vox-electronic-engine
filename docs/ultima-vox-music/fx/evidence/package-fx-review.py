#!/usr/bin/env python3
"""Build a private-transfer-ready archive from the pinned A120 and B56 solo exports.

Usage: package-fx-review.py <wave-a-export> <wave-b-export> <new.zip>
Verifies before writing; never publishes, generates audio, or overwrites a ZIP.
"""
import csv
import hashlib
import io
import os
from pathlib import Path
import re
import sys
import tempfile
import zipfile

EVIDENCE = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_tsv(data):
    return list(csv.DictReader(io.StringIO(data.decode('utf-8')), delimiter='\t'))


def hashes(file):
    result = {}
    for line in file.read_text().splitlines():
        digest, name = line.split('  ', 1)
        name = name.removeprefix('./')
        if name in result or not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError(f'{file}: invalid or duplicate member {name}')
        result[name] = digest
    return result


def build(wave_a, wave_b, output):
    if output.exists():
        raise FileExistsError(output)
    a_hash = hashes(EVIDENCE / 'wave-a-120-members.sha256')
    b_hash = {}
    for tranche in ('thirtytwo', 'forty', 'fortyeight', 'fiftysix'):
        for name, digest in hashes(EVIDENCE / f'wave-b-{tranche}-members.sha256').items():
            if name.startswith(('solo/', 'presets/', 'sources/')):
                if name in b_hash:
                    raise ValueError(f'duplicate Wave B member: {name}')
                b_hash[name] = digest

    entries = {}

    def add(name, path, digest=None):
        if name in entries:
            raise ValueError(f'duplicate archive entry: {name}')
        entries[name] = (path, digest or sha(path.read_bytes()))

    def check_metadata(root, generated, pinned):
        if (root / generated).read_bytes() != (EVIDENCE / pinned).read_bytes():
            raise ValueError(f'{generated} differs from pinned {pinned}')
        add(f'evidence/{pinned}', EVIDENCE / pinned)
        return read_tsv((root / generated).read_bytes())

    a_rows = check_metadata(wave_a, 'manifest.tsv', 'wave-a-120-manifest.tsv')
    check_metadata(wave_a, 'audition-settings.tsv', 'wave-a-120-audition.tsv')
    b_rows = check_metadata(wave_b, 'manifest.tsv', 'wave-b-fiftysix-manifest.tsv')
    check_metadata(wave_b, 'settings.tsv', 'wave-b-fiftysix-settings.tsv')
    metrics = check_metadata(wave_b, 'metrics.tsv', 'wave-b-fiftysix-metrics.tsv')
    if len(a_rows) != 120 or len(b_rows) != 56 or len(metrics) != 168:
        raise ValueError('unexpected pinned review scope')

    levels = {}
    for tranche in ('forty', 'fortyeight', 'fiftysix'):
        file = EVIDENCE / f'wave-b-{tranche}-levels.tsv'
        add(f'evidence/{file.name}', file)
        for row in read_tsv(file.read_bytes()):
            levels[row['id']] = row['active-rms-dBFS']

    fields = ('id', 'wave', 'name', 'role', 'style', 'mode', 'bpm', 'arrangement-slot', 'tonality',
              'raw-wav', 'raw-wav-sha256', 'portable', 'source', 'source-sha256', 'provenance',
              'measured-active-rms-dBFS', 'review-status', 'arrangement-reference', 'printed-FX-trim-dB',
              'mix-peak-dBFS', 'audible-placement', 'mono-masking-result', 'decision', 'reviewer')
    matrix = io.StringIO()
    writer = csv.DictWriter(matrix, fieldnames=fields, delimiter='\t', lineterminator='\n', restval='-')
    writer.writeheader()
    ids, all_audio, sources = set(), set(), set()
    for wave, root, rows, pins in (('A', wave_a, a_rows, a_hash), ('B', wave_b, b_rows, b_hash)):
        for row in rows:
            id_ = row.get('id', row.get('ID'))
            if not re.fullmatch(r'voxfx\.[a-z0-9-]+\.[a-z0-9-]+', id_) or id_ in ids or 'review-only' not in row['status']:
                raise ValueError(f'invalid or duplicate review entry {id_}')
            ids.add(id_)
            wav_member = f'{id_}.wav' if wave == 'A' else f'solo/{id_}.wav'
            preset_member = f'presets/{id_}.voxfx'
            wav_sha = pins[wav_member]
            if wav_sha in all_audio or (wave == 'B' and wav_sha != row['solo-48k-sha256']):
                raise ValueError(f'invalid or duplicate WAV fingerprint for {id_}')
            all_audio.add(wav_sha)
            wav, preset = f'Wave-{wave}/solo/{id_}.wav', f'Wave-{wave}/{preset_member}'
            add(wav, root / wav_member, wav_sha)
            add(preset, root / preset_member, pins[preset_member])
            source = 'built-in'
            source_sha = '-'
            if wave == 'B' and row['source'] != 'built-in':
                filename = row['source']
                if not re.fullmatch(r'[a-z0-9-]+\.wav', filename):
                    raise ValueError(f'invalid reference {filename}')
                member = f'sources/{filename}'
                source, source_sha = f'Wave-B/{member}', pins[member]
                if source_sha != row['source-sha256']:
                    raise ValueError(f'source hash disagreement for {id_}')
                if source not in sources:
                    sources.add(source)
                    add(source, root / member, source_sha)
            tonality = row.get('tonality') if wave == 'B' else (row['scale'] if row['pitched-source'] == '1' else 'keyless')
            writer.writerow({'id': id_, 'wave': wave, 'name': row['name'], 'role': row['role'],
                             'style': row['style'], 'mode': row['mode'], 'bpm': row['bpm'],
                             'arrangement-slot': row.get('use', row.get('use-case')),
                             'tonality': tonality,
                             'raw-wav': wav, 'raw-wav-sha256': wav_sha, 'portable': preset,
                             'source': source, 'source-sha256': source_sha, 'provenance': row['provenance'],
                             'measured-active-rms-dBFS': levels.get(id_, '-'), 'review-status': 'not reviewed'})
    if len(ids) != 176 or len(sources) != 2:
        raise ValueError('unexpected catalog or asset inventory')
    add('READ ME.txt', EVIDENCE / 'fx-review-176-readme.txt')
    for tag in ('thirtytwo', 'forty', 'fortyeight', 'fiftysix'):
        file = EVIDENCE / f'wave-b-{tag}-creative-brief.tsv'
        add(f'evidence/{file.name}', file)
    # Verify every source before opening the archive; the output is local-only.
    for name, (path, digest) in entries.items():
        if sha(path.read_bytes()) != digest:
            raise ValueError(f'content mismatch: {name}')
    matrix_data = matrix.getvalue().encode('utf-8')
    members = {name: digest for name, (_, digest) in entries.items()}
    members['arrangement-review.tsv'] = sha(matrix_data)
    listing = ''.join(f'{digest}  {name}\n' for name, digest in sorted(members.items())).encode()
    timestamp = (2026, 11, 19, 0, 0, 0)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        staged = Path(temporary) / 'verified.zip'
        with zipfile.ZipFile(staged, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
            for name in sorted(members):
                data = matrix_data if name == 'arrangement-review.tsv' else entries[name][0].read_bytes()
                if sha(data) != members[name]:
                    raise ValueError(f'input changed during packaging: {name}')
                archive.writestr(zipfile.ZipInfo(name, timestamp), data, compress_type=zipfile.ZIP_DEFLATED)
            archive.writestr(zipfile.ZipInfo('SHA256SUMS', timestamp), listing, compress_type=zipfile.ZIP_DEFLATED)
        with zipfile.ZipFile(staged) as archive:
            if len(archive.namelist()) != len(members) + 1 or archive.read('SHA256SUMS') != listing:
                raise ValueError('malformed consolidated archive')
            for name, digest in members.items():
                if sha(archive.read(name)) != digest:
                    raise ValueError(f'archive verification failed: {name}')
        staged.chmod(0o600)
        os.link(staged, output)  # Publish only verified bytes; fail rather than replace.
    with output.open('rb') as stream:
        print(f'{hashlib.file_digest(stream, "sha256").hexdigest()}  {output}  ({len(members)} verified members)')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    build(*(Path(arg) for arg in sys.argv[1:]))
