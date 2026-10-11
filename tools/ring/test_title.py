#!/usr/bin/env python3
"""Check all three title link labels and video-rate menu updates with real BIOS."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + name, type=Path, required=True)
p.add_argument('--nm', default='arm-none-eabi-nm')
a = p.parse_args()
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
symbols = {}
for line in subprocess.check_output([a.nm, '-n', str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
a.results.mkdir(parents=True, exist_ok=True)
data = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(data).hexdigest() + '.gba')
if not rom.exists():
    rom.write_bytes(data)
inputs = a.results / 'title.csv'
inputs.write_text('700,0,8\n715,0,0\n820,0,80\n835,0,0\n900,0,1\n915,0,0\n1000,0,80\n1015,0,0\n1100,0,80\n1115,0,0\n')

def run(name, frames, watch, dump=None):
    command = [str(a.runner), '--rom', str(rom), '--bios', str(a.bios),
               '--players', '1', '--frames', str(frames), '--input', str(inputs),
               '--watch', hex(watch), '--screens', str(a.results / name)]
    if dump is not None:
        command += ['--dump', hex(dump), '--dump-words', '256']
    proc = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (a.results / (name + '.json')).write_text(proc.stdout)
    (a.results / (name + '.stderr')).write_text(proc.stderr)
    assert proc.returncode == 0 and not proc.stderr, proc.stderr
    return json.loads(proc.stdout)['nodes'][0]

node = run('labels', 1200, symbols['title_sState'], 0x07000000)
state = node['watch']
assert 0x02000000 <= state <= 0x0203FC00, hex(state)
oam = node['dump']
tiles = {oam[i + 1] & 1023 for i in range(0, len(oam), 2) if (oam[i] & 65535) != 512}
assert {0x360, 0x380, 0x3F0}.issubset(tiles), ('Missing Single-Pak, Multi-Pak, or Ring label', tiles)
first = run('speed-start', 1200, state + 0x390)['watch']
last = run('speed-end', 1400, state + 0x390)['watch']
assert 195 <= last - first <= 205, ('Title redraws must follow VBlank', first, last)
print(f'PASS: all link labels present; {last - first} menu updates in 200 video frames')
