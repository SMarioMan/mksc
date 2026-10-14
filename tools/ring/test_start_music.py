#!/usr/bin/env python3
"""Preserve native race-opening songs before starting host-only course BGM."""
import argparse
import csv
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--players', type=int, choices=range(2, 9), default=8)
p.add_argument('--host', type=int, default=0)
p.add_argument('--boot-ring', action='store_true')
a = p.parse_args()
if not 0 <= a.host < a.players:
    p.error('The host must exist in the session')
a.results.mkdir(parents=True, exist_ok=True)
symbols = {}
for line in subprocess.check_output(['arm-none-eabi-nm', '-n', str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
rom = a.rom.read_bytes()
song = lambda i: struct.unpack_from('<I', rom, symbols['gSongTable'] - 0x08000000 + i * 8)[0]
for mode in ('vs', 'battle', 'gp'):
    rows = []
    for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
        frame, player, key = (int(value, 16 if i == 2 else 10) for i, value in enumerate(line.split(',')))
        if player or frame >= 3500 or (a.boot_ring and frame < 1500):
            continue
        if mode != 'vs' and frame in (2500, 2515):
            continue
        for peer in range(a.players if frame < 1500 or frame in (2000, 2015) else 1):
            rows.append((frame, peer if frame < 1500 or frame in (2000, 2015) else a.host, key))
    if mode != 'vs':
        rows += [(1600, a.host, 128 if mode == 'battle' else 64), (1615, a.host, 0)]
    inputs = a.results / (mode + '-inputs.csv')
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(rows)))
    video = a.results / (mode + '-video.csv')
    proc = subprocess.run([str(a.runner), '--ring', '--rom', str(a.rom), '--bios', str(a.bios),
                           '--players', str(a.players), '--frames', '4000', '--input', str(inputs),
                           '--video-from', '2000', '--video-log', str(video), '--barrier-before-input',
                           '--barrier-state', hex(symbols['sRing'])], capture_output=True, text=True, timeout=120)
    (a.results / (mode + '.json')).write_text(proc.stdout)
    (a.results / (mode + '.stderr')).write_text(proc.stderr)
    assert proc.returncode == 0 and not proc.stderr, (mode, proc.stderr[:1000])
    result = json.loads(proc.stdout)
    assert not result['collisions']
    assert all(not n['barrier']['errors'] and not n['barrier']['timeouts'] for n in result['nodes'])
    frames = [{k: int(v) for k, v in r.items()} for r in csv.DictReader(video.open())]
    race = [r for r in frames if r['sceneMain'] == symbols['race_main'] | 1]
    owner = [r for r in race if r['player'] == a.host]
    opening = [r for r in owner if r['musicHeader'] in (song(3), song(4))]
    assert opening and any(r['musicStatus'] & 65535 and not r['musicStatus'] & 0x80000000 for r in opening), mode
    assert not opening[-1]['musicStatus'] & 65535 or opening[-1]['musicStatus'] & 0x80000000, (mode, 'Opening song interrupted', opening[-1])
    course = song(18) if mode == 'battle' else song(25)
    assert any(r['musicHeader'] == course and r['frame'] > opening[-1]['frame'] for r in owner), (mode, 'Course BGM never resumed')
    peers = [r for r in race if r['player'] != a.host]
    assert peers and all(not r['musicStatus'] & 65535 or r['musicStatus'] & 0x80000000 for r in peers), (mode, 'Peer BGM audible')
    print(f'PASS {mode}-{a.players}: native opening completes, then host-only course music starts', flush=True)
