#!/usr/bin/env python3
"""Extract the checked-in PowerShell blocks for parse-only Windows preflight.

Uses PyYAML and CMake, but does not execute PowerShell, a validator, or CI.
"""
import argparse
from pathlib import Path
import re
import subprocess
import textwrap

import yaml


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('output', type=Path, help='new scratch directory (must not exist)')
    cli.add_argument('--cmake', default='cmake')
    args = cli.parse_args()
    root = Path(__file__).resolve().parents[2]
    workflow = yaml.safe_load((root / '.github/workflows/suite.yml').read_text(encoding='utf-8'))
    validator = next(step['run'] for step in workflow['jobs']['windows-suite']['steps']
                     if step.get('id') == 'suite-validator')
    _, inventory = validator.split('# FX inventory preflight begin\n')
    inventory, _ = inventory.split('# FX inventory preflight end')
    inventory = textwrap.dedent(inventory.rstrip())
    required = re.search(r"^\$required = @\(([^\n]+)\)$", inventory, re.M)
    if not required:
        raise SystemExit('Cannot read the validator’s required bundle list')
    names = re.findall(r"'([^']+)'", required[1])
    src = subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD'], text=True).strip()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cmake_check = out / 'read-suite-identity.cmake'
    identity = root / 'cmake/suite/SuiteIdentity.cmake'
    cmake_check.write_text(f'include([=[{identity.as_posix()}]=])\n' + '''
foreach(key IN LISTS VOX_SUITE_PRODUCTS)
  message("VOX_BUNDLE=${VOX_SUITE_${key}_NAME}")
endforeach()
''', encoding='utf-8')
    result = subprocess.run([args.cmake, '-P', cmake_check], check=True, capture_output=True, text=True)
    expected = re.findall(r'^VOX_BUNDLE=(.+)$', result.stderr, re.M)
    if len(expected) != 3 or len(set(expected)) != 3 or sorted(names) != sorted(expected):
        raise SystemExit(f'Bundle name mismatch: workflow={names}, SuiteIdentity.cmake={expected}')
    (out / 'bundle-names.txt').write_text('\n'.join(expected) + '\n', encoding='ascii')
    (out / 'inventory-only.ps1').write_text('param($bundles)\n' + inventory + '\n', encoding='ascii')
    blocks = 0
    for job_name, job in workflow['jobs'].items():
        default_shell = job.get('defaults', {}).get('run', {}).get('shell')
        for index, step in enumerate(job['steps']):
            shell = step.get('shell', default_shell)
            # Windows Actions defaults to pwsh for run steps without a shell.
            if 'run' in step and shell in (None, 'pwsh', 'powershell'):
                name = f'{job_name}-{index:02d}.ps1'
                # Current run blocks use env values. A future inline Actions
                # substitution needs an explicit fixture, not blind execution.
                if '${{' in step['run']:
                    raise SystemExit(f'{name} has an unhandled Actions substitution')
                (out / name).write_text(step['run'], encoding='utf-8-sig')
                blocks += 1
    for name in ('Capture-FxProcessorException.ps1', 'Use-PinnedCheckout.ps1'):
        (out / name).write_bytes((root / '.github/scripts' / name).read_bytes())
    (out / 'source.txt').write_text(src + '\n', encoding='ascii')
    print(f'Checkout {src}; {blocks} PowerShell run blocks extracted, not executed')
    print('SuiteIdentity.cmake matches workflow bundle names: ' + ', '.join(expected))


if __name__ == '__main__':
    main()
