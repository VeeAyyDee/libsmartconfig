#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Owned link-audit policy tests; synthetic maps/ELFs are not build evidence."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='audit-fixture-', dir=ROOT / 'build') as directory:
    root = Path(directory)
    archive = root / 'component with spaces' / 'libsmartconfig.a'
    archive.parent.mkdir()
    archive.write_bytes(b'fixture archive')
    elf = root / 'example.elf'
    elf.write_bytes(b'fixture elf')
    mapping = root / 'example.map'
    symbol_file = root / 'symbols.txt'
    nm = root / ('fake-nm.cmd' if os.name == 'nt' else 'fake-nm')
    if os.name == 'nt':
        nm.write_text('@echo off\ntype "%~dp0symbols.txt"\n')
    else:
        nm.write_text('#!/usr/bin/env python3\nfrom pathlib import Path\nprint(Path(__file__).with_name("symbols.txt").read_text())\n')
    nm.chmod(0o755)
    base = [sys.executable, str(ROOT / 'tools/check_no_smartconfig_blob.py'),
            '--map', str(mapping), '--elf', str(elf), '--nm', str(nm),
            '--replacement-archive', str(archive)]
    symbols = ['esp_smartconfig_start', 'esp_smartconfig_stop',
               'esp_smartconfig_internal_start', 'esp_smartconfig_internal_stop',
               'esp_smartconfig_set_type', 'SC_EVENT']
    symbol_file.write_text('\n'.join('000 T ' + name for name in symbols))

    def check(protocol, passed):
        result = subprocess.run(base + ['--protocol', protocol], capture_output=True, text=True)
        assert result.stdout, result.stderr
        report = json.loads(result.stdout)
        assert (result.returncode == 0) == passed, report
        assert (report['status'] == 'pass') == passed

    mapping.write_text(f'{archive}(libsmartconfig_compat.c.obj)\n')
    check('standard', True)
    mapping.write_text(f'{archive.relative_to(root)}(libsmartconfig_compat.c.obj)\n')
    check('standard', True)
    mapping.write_text(f'{archive}(libsmartconfig_compat.c.obj)\n/vendor/libsmartconfig.a(smartconfig.o)\n')
    check('standard', False)
    mapping.write_text(f'/unexpected{archive}(libsmartconfig_compat.c.obj)\n')
    check('standard', False)
    mapping.write_text(f'LOAD {archive}\n')
    check('standard', False)
    mapping.write_text(f'{archive}(libsmartconfig_compat.c.obj)\n')
    symbol_file.write_text('000 T esp_smartconfig_start\n')
    check('standard', False)
    for protocol, prefix in [('v1', 'sc_touch'), ('airkiss', 'sc_airkiss'), ('v2', 'sc_touch2')]:
        symbol_file.write_text('\n'.join('000 T ' + prefix + '_idf_' + name for name in ('start','poll','stop')))
        check(protocol, True)
    symbol_file.write_text('\n'.join('000 T ' + name for name in
                         ('sc_airkiss_idf_start_with_key','sc_airkiss_idf_poll','sc_airkiss_idf_stop','sc_touch2_psa_decrypt')))
    check('airkiss-encrypted', True)
print('PASS: link-audit synthetic approved paths, spaces, contamination, required symbols and preserved optional workflows')
