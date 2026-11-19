#!/usr/bin/env python3
"""Verify a Wave B staging export and rebuild the listening archive locally.

Usage: python3 package-wave-b-review.py <export-directory> <destination.zip> [--counterpoints | --signals | --crossings]
Does not upload, alter the export directory, or overwrite a destination.
"""

import hashlib
from pathlib import Path
import sys
import tempfile
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def package(root, destination, profile=''):
    profiles = {'': ('twentyfour', 54), '--counterpoints': ('thirtytwo', 88), '--signals': ('forty', 25), '--crossings': ('fortyeight', 22)}
    tranche, count = profiles[profile]
    evidence = Path(__file__).resolve().parent
    prefix = f'wave-b-{tranche}-'
    sidecars = {'READ ME.txt': 'package-readme.txt'}
    if profile:
        sidecars['creative-brief.tsv'] = 'creative-brief.tsv'
    hashes = (evidence / (prefix + 'members.sha256')).read_bytes()
    expected_archive = (evidence / (prefix + 'archive.sha256')).read_text().split()[0]
    expected = dict(line.split('  ', 1)[::-1] for line in hashes.decode().splitlines())
    if len(expected) != count or destination.exists():
        raise ValueError(f'Expected the {count} pinned members and a new archive path')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent) as tmp:
        with zipfile.ZipFile(tmp, 'w') as z:
            for name, digest in expected.items():
                source = evidence / (prefix + sidecars[name]) if name in sidecars else root / name
                data = source.read_bytes()
                if sha(data) != digest:
                    raise ValueError(f'{name}: SHA-256 does not match the staged render or metadata')
                entry = zipfile.ZipInfo(name, (2026, 11, 19, 0, 0, 0))
                z.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED)
            entry = zipfile.ZipInfo('members.sha256', (2026, 11, 19, 0, 0, 0))
            z.writestr(entry, hashes, compress_type=zipfile.ZIP_DEFLATED)
        tmp.seek(0)
        data = tmp.read()
        digest = sha(data)
        # ZIP compression bytes can vary between zlib versions. Member hashes
        # above are authoritative; print both archive hashes for this case.
        with destination.open('xb') as out:
            out.write(data)
    print(f'{digest}  {destination}')
    print('Matches original archive' if digest == expected_archive else f'Original ZIP {expected_archive}; all members match')


if __name__ == '__main__':
    if len(sys.argv) != 3 and not (len(sys.argv) == 4 and sys.argv[3] in ('--counterpoints', '--signals', '--crossings')):
        sys.exit(__doc__)
    package(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3] if len(sys.argv) == 4 else '')
