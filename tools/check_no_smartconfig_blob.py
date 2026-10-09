#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Audit linked archive provenance and expected workflow symbols."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--map', type=Path, required=True)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--nm', default='xtensa-esp-elf-nm')
    p.add_argument('--protocol', choices=('standard', 'v1', 'airkiss', 'airkiss-encrypted', 'v2'), required=True)
    p.add_argument('--replacement-archive', type=Path,
                   help='Exact built original libsmartconfig.a; required for standard workflow')
    args = p.parse_args()
    if args.protocol == 'standard' and args.replacement_archive is None:
        p.error('standard workflow requires --replacement-archive')
    accepted_paths = []
    if args.replacement_archive is not None:
        archive = args.replacement_archive.resolve(strict=True)
        if archive.name != 'libsmartconfig.a':
            p.error('replacement archive must be named libsmartconfig.a')
        accepted_paths.append(str(archive))
        try:
            accepted_paths.append(str(archive.relative_to(args.map.resolve().parent)))
        except ValueError:
            pass
    linked = args.map.read_text(errors='replace')
    ours, rejected = [], []
    # Accept only an exact caller-identified archive path. LOAD alone is harmless.
    # Inspect every member separately, including paths with spaces.
    for line in linked.splitlines():
        for match in re.finditer(r'libsmartconfig\.a\([^)]*\)', line):
            prefix = line[:match.start()] + 'libsmartconfig.a'
            allowed = any(prefix.endswith(path) and
                          (len(prefix) == len(path) or prefix[-len(path)-1].isspace())
                          for path in accepted_paths)
            (ours if allowed else rejected).append(line.strip())
    symbols = subprocess.run([args.nm, '--defined-only', str(args.elf)],
                             check=True, text=True, capture_output=True).stdout
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    if args.protocol == 'standard':
        # SDK provides public wrapper/ACK, our archive provides internal receiver.
        required = {'esp_smartconfig_start', 'esp_smartconfig_stop',
                    'esp_smartconfig_internal_start', 'esp_smartconfig_internal_stop',
                    'esp_smartconfig_set_type', 'SC_EVENT'}
        forbidden = []
    else:
        prefix = {'v1': 'sc_touch', 'airkiss': 'sc_airkiss',
                  'airkiss-encrypted': 'sc_airkiss', 'v2': 'sc_touch2'}[args.protocol]
        required = {prefix + '_idf_' + suffix for suffix in ('start', 'poll', 'stop')}
        if args.protocol == 'airkiss-encrypted':
            required.remove('sc_airkiss_idf_start')
            required.update(('sc_airkiss_idf_start_with_key', 'sc_touch2_psa_decrypt'))
        forbidden = sorted(s for s in names if s.startswith('esp_smartconfig_'))
    missing = sorted(required - names)
    archive_missing = args.protocol == 'standard' and not ours
    report = {
        'status': 'fail' if rejected or forbidden or missing or archive_missing else 'pass',
        'protocol': args.protocol,
        'unapproved_smartconfig_archive_members': sorted(set(rejected)),
        'replacement_archive_members': sorted(set(ours)),
        'unexpected_standard_symbols': forbidden,
        'missing_workflow_symbols': missing,
        'replacement_archive_missing': archive_missing,
        'elf_sha256': hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        'map_sha256': hashlib.sha256(args.map.read_bytes()).hexdigest(),
        'scope': 'SmartConfig receiver archive only; SDK open wrapper/ACK and Wi-Fi/PHY remain',
    }
    print(json.dumps(report, indent=2))
    raise SystemExit(report['status'] != 'pass')


if __name__ == '__main__':
    main()
