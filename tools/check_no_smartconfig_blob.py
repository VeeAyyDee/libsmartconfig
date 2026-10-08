#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Audit a linked example, not merely its component dependency declarations."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--map", type=Path, required=True)
    p.add_argument("--elf", type=Path, required=True)
    p.add_argument("--nm", default="xtensa-esp-elf-nm")
    p.add_argument("--protocol", choices=("v1", "airkiss", "v2"), required=True)
    args = p.parse_args()
    linked = args.map.read_text(errors="replace")
    # An unused LOAD archive entry is harmless. An archive member is not.
    members = re.findall(r"(?<![\w])libsmartconfig\.a\([^)]*\)", linked)
    symbols = subprocess.run(
        [args.nm, "--defined-only", str(args.elf)], check=True,
        text=True, capture_output=True).stdout
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    vendor = sorted(s for s in names if s.startswith("esp_smartconfig_"))
    prefix = {"v1": "sc_touch", "airkiss": "sc_airkiss", "v2": "sc_touch2"}[args.protocol]
    required = {prefix + "_idf_" + suffix for suffix in ("start", "poll", "stop")}
    missing = sorted(required - names)
    report = {
        "status": "fail" if members or vendor or missing else "pass",
        "protocol": args.protocol,
        "vendor_smartconfig_archive_members": sorted(set(members)),
        "vendor_smartconfig_defined_symbols": vendor,
        "missing_our_adapter_symbols": missing,
        "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        "map_sha256": hashlib.sha256(args.map.read_bytes()).hexdigest(),
        "scope": "SmartConfig only; Wi-Fi/PHY proprietary code may remain linked",
    }
    print(json.dumps(report, indent=2))
    raise SystemExit(report["status"] != "pass")


if __name__ == "__main__":
    main()
