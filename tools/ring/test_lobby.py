#!/usr/bin/env python3
"""Check discovery audio without starting a race, using real BIOS boot."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'bios', 'results'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
if hashlib.sha1(a.bios.read_bytes()).hexdigest() != '300c20df6731a33952ded8c436f7f186d25d3492':
    p.error('Use the supplied real GBA BIOS')
a.results.mkdir(parents=True, exist_ok=True)
rom_data = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(rom_data).hexdigest() + '.gba')
if not rom.exists():
    rom.write_bytes(rom_data)
proc = subprocess.run([str(a.runner), '--ring', '--rom', str(rom), '--bios', str(a.bios),
                       '--players', '2', '--frames', '1450', '--input',
                       str(Path(__file__).parent / 'inputs/ring-vs.csv'),
                       '--dump', '0x03005FF0', '--dump-words', '396',
                       '--screens', str(a.results / 'lobby')],
                      capture_output=True, text=True, timeout=180)
(a.results / 'lobby.json').write_text(proc.stdout)
(a.results / 'lobby.stderr').write_text(proc.stderr)
assert proc.returncode == 0, proc.stderr
result = json.loads(proc.stdout)
for node in result['nodes']:
    assert node['siocnt'] == 0x5082, node  # Slave armed at the normal-32 fast clock setting.
    assert len(node['dump']) == 396 and not any(node['dump']), node['slot']
print('PASS: both waiting consoles clear the complete 1584-byte PCM buffer')
