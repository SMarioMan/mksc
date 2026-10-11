#!/usr/bin/env python3
"""Check consecutive BIOS-backed frames, including eight karts in one view.

Stationary Vs. preserves the eight-place grid so natural occlusion/movement
cannot masquerade as flicker. A frozen character menu checks completed bitmap
hashes on both pages. All interaction uses the hardware keypad.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'bios', 'results'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
a.results.mkdir(parents=True, exist_ok=True)
data = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(data).hexdigest() + '.gba')
if not rom.exists():
    rom.write_bytes(data)
base = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    frame, slot, keys = line.split(',')
    if int(slot) == 0 and int(frame) < 3500:
        base.append((int(frame), int(keys, 16)))

for case in ('menu', 'vs', 'battle'):
    rows = []
    for frame, keys in base:
        if case == 'menu' and frame >= 1950:
            continue
        if case == 'battle' and frame in (2350, 2365):
            continue
        shared = frame < 1500 or frame in (2000, 2015)
        rows.extend((frame, slot, keys) for slot in range(8 if shared else 1))
    if case == 'battle':
        rows.extend([(1600, 0, 128), (1615, 0, 0)])
    inputs = a.results / (case + '.csv')
    inputs.write_text(''.join(f'{f},{s},{k:x}\n' for f, s, k in sorted(rows)))
    video = a.results / (case + '-video.csv')
    first, last = (2100, 2400) if case == 'menu' else (3300, 4000)
    proc = subprocess.run([str(a.runner), '--ring', '--rom', str(rom), '--bios', str(a.bios),
                           '--players', '8', '--frames', str(last), '--input', str(inputs),
                           '--video-log', str(video), '--video-from', str(first),
                           '--dump', '0x03006B40', '--barrier-state', '0x03006B40',
                           '--screens', str(a.results / case)],
                          capture_output=True, text=True, timeout=300)
    (a.results / (case + '.json')).write_text(proc.stdout)
    (a.results / (case + '.stderr')).write_text(proc.stderr)
    assert proc.returncode == 0, proc.stderr
    state = json.loads(proc.stdout)
    assert not state['collisions']
    assert all(n['dump'][22:24] == [0, 0] for n in state['nodes'])
    frames = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(video.open())]
    frames = [row for row in frames if row['frame'] < last]
    assert len(frames) == (last - first) * 8, len(frames)
    assert all(not row['invalidObjects'] for row in frames)
    if case == 'menu':
        assert all(row['dispcnt'] & 7 == 4 for row in frames)
        assert len({row['bitmapHash'] for row in frames}) == 1, 'A frozen menu tore or lost content'
        assert {row['dispcnt'] & 16 for row in frames} == {0, 16}, 'Both drawing pages must be exercised'
        print(f'PASS menu: {len(frames)} complete, identical bitmap frames across both pages', flush=True)
    elif case == 'vs':
        rear = [row for row in frames if row['player'] == 7]
        assert all(all(row[f'kart{i}'] == 1 for i in range(8)) for row in rear), 'A kart disappeared from the stationary grid'
        assert all(row['markers'] == 7 for row in rear), 'An opponent lost its identity marker'
        textures = state['nodes'][7]['game']['drivers']
        assert all(d['graphicsMatch'] for d in textures), 'A kart VRAM bank differs from its native texture'
        assert len({d['graphicsSource'] for d in textures}) == 8, 'Distinct characters share a texture slot'
        print(f'PASS Vs.: all eight karts and seven identity markers in each of {len(rear)} consecutive frames', flush=True)
    else:
        assert all(row[f"kart{row['player']}"] for row in frames), 'A stationary local Battle kart disappeared'
        print(f'PASS Battle: {len(frames)} valid sprite frames with all local karts present', flush=True)
