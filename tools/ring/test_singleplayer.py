#!/usr/bin/env python3
"""Reproduce the shifted-build character-sound crash through ordinary GP inputs.

The route follower supplies keypad inputs and periodically presses L to use
items. A ROM read watch records execution of the original character-voice
script; no racer, item, CPU, or script state is injected into game memory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--nm', default='arm-none-eabi-nm')
a = p.parse_args()
if hashlib.sha1(a.bios.read_bytes()).hexdigest() != '300c20df6731a33952ded8c436f7f186d25d3492':
    p.error('Use the supplied real GBA BIOS')
symbols = {}
for line in subprocess.check_output([a.nm, '-n', str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
script = symbols['_080EC8F4']
a.results.mkdir(parents=True, exist_ok=True)
rom_data = a.rom.read_bytes()
# Verify the watched script really calls the relocated Thumb function.
assert int.from_bytes(rom_data[script - 0x08000000 + 12:script - 0x08000000 + 16], 'little') == symbols['sub_0803E2B4'] | 1
rom = a.results / (hashlib.sha256(rom_data).hexdigest() + '.gba')
if not rom.exists():
    rom.write_bytes(rom_data)
rows = []
for frame in (700,900,1050,1200,1450,1650,1900,2150,3500,3800):
    rows.extend([(frame, 8 if frame == 700 else 1), (frame + 15, 0)])
inputs = a.results / 'single-player.csv'
inputs.write_text(''.join(f'{f},0,{k:x}\n' for f,k in rows))
trace = a.results / 'voices.csv'
proc = subprocess.run([str(a.runner), '--rom', str(rom), '--bios', str(a.bios),
                       '--players', '1', '--frames', '11000', '--input', str(inputs),
                       '--autodrive-frame', '5000', '--autofire',
                       '--trace-memory', hex(script), '--trace-file', str(trace),
                       '--screens', str(a.results / 'single-player')],
                      capture_output=True, text=True, timeout=180)
(a.results / 'single-player.json').write_text(proc.stdout)
(a.results / 'single-player.stderr').write_text(proc.stderr)
assert proc.returncode == 0 and not proc.stderr, proc.stderr[:2000]
data = json.loads(proc.stdout)['nodes'][0]
assert data['game']['humanCount'] == 1, data['game']
assert all(d['address'] for d in data['game']['drivers']), data['game']
calls = [line for line in trace.read_text().splitlines()
         if int(line.split(',')[2],16) == script + 12 and line.split(',')[4] == '2']
assert calls, 'Character voice callback was not exercised'
print(f'PASS: single-player GP with eight racers and {len(calls)} character-voice callbacks')
