#!/usr/bin/env python3
"""Eliminate players 1–4 and verify the four surviving players occupy the HUD."""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
events = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    f, i, k = line.split(',')
    f = int(f)
    if int(i) == 0 and f < 3500 and f not in (2350, 2365):
        events.extend((f, j, int(k, 16)) for j in range(8 if f < 1500 or f in (2000, 2015) else 1))
events.extend([(1600, 0, 128), (1615, 0, 0)])
for step in range(3):
    events.extend([(2070 + step * 40, 0, 128), (2090 + step * 40, 0, 0)])
events.extend((3500, i, 17 if i < 4 else 0) for i in range(8))
inputs = a.results / 'survivors.csv'
inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(events)))
proc = subprocess.run([
    str(a.runner), '--ring', '--rom', str(a.rom), '--bios', str(a.bios),
    '--players', '8', '--frames', '9000', '--input', str(inputs),
    '--barrier-before-input', '--barrier-state', '0x03006B40',
    '--screens', str(a.results / 'survivors')
], capture_output=True, text=True, timeout=1200)
(a.results / 'survivors.json').write_text(proc.stdout)
(a.results / 'survivors.stderr').write_text(proc.stderr)
assert proc.returncode == 0 and not proc.stderr, (proc.returncode, proc.stderr)
result = json.loads(proc.stdout)
assert result['bios'] == 'real' and result['collisions'] == 0
tiles = [0, 1, 7, 3, 5, 6, 2, 4]
states = []
for node in result['nodes']:
    assert node['barrier']['errors'] == node['barrier']['timeouts'] == 0
    drivers = node['game']['drivers']
    assert [d['balloons'] for d in drivers] == [0] * 4 + [3] * 4
    for row, id in enumerate(range(4, 8)):
        tile = 0xD080 + tiles[drivers[id]['character'] & 7] * 4
        assert node['game']['hud']['portraits'][row] == list(range(tile, tile + 4))
    states.append([d['position'] for d in drivers])
assert all(s == states[0] for s in states)
print('PASS: surviving players 5–8 replace eliminated players 1–4 in all eight Battle HUDs')
