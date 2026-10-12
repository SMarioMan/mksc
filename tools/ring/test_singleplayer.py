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
p.add_argument('--character', type=int, choices=range(8), default=0, help='Native menu index; 6 traverses DK to Wario')
p.add_argument('--hle', action='store_true', help='Exercise optional HLE; real BIOS is the default')
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
for frame in (700,900,1050,1200,1450,2850,3250,3500,3800,4300,4600,5000):
    rows.extend([(frame, 8 if frame == 700 else 1), (frame + 15, 0)])
# Native selection uses a 3x3 grid with Random in the middle. Start there;
# Wario deliberately moves left to Toad, down to DK, then right to Wario.
paths = [(32,64),(64,),(16,64),(32,),(16,),(32,128),(32,128,16),(16,128)]
for step, key in enumerate(paths[a.character]):
    rows.extend([(2250+step*200,key),(2265+step*200,0)])
rows.sort()
inputs = a.results / 'single-player.csv'
inputs.write_text(''.join(f'{f},0,{k:x}\n' for f,k in rows))
trace = a.results / 'voices.csv'
command = [str(a.runner), '--rom', str(rom), '--bios', str(a.bios),
                       '--players', '1', '--frames', '14000', '--input', str(inputs),
                       '--autodrive-frame', '6500', '--autofire',
                       '--trace-memory', hex(script), '--trace-file', str(trace),
                       '--screens', str(a.results / 'single-player')]
if a.hle:
    bios_index = command.index('--bios')
    del command[bios_index:bios_index + 2]
    command += ['--hle']
proc = subprocess.run(command,
                      capture_output=True, text=True, timeout=180)
(a.results / 'single-player.json').write_text(proc.stdout)
(a.results / 'single-player.stderr').write_text(proc.stderr)
assert proc.returncode == 0 and not proc.stderr, proc.stderr[:2000]
result = json.loads(proc.stdout)
assert result['bios'] == ('hle' if a.hle else 'real')
data = result['nodes'][0]
assert data['game']['humanCount'] == 1, data['game']
assert all(d['address'] for d in data['game']['drivers']), data['game']
assert data['game']['drivers'][0]['character'] == [0,1,3,6,7,4,5,2][a.character], data['game']
calls = [line for line in trace.read_text().splitlines()
         if int(line.split(',')[2],16) == script + 12 and line.split(',')[4] == '2']
assert calls, 'Character voice callback was not exercised'
print(f'PASS: single-player GP with eight racers and {len(calls)} character-voice callbacks')
