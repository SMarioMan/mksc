#!/usr/bin/env python3
"""Verify highest-balloon HUD rows with living lower ranks and eliminations."""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--analyze-only', action='store_true', help='Check existing mixed/survivors snapshots')
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
events = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    f, i, k = line.split(',')
    f = int(f)
    if int(i) == 0 and f < 3500 and f not in (2200, 2215):
        events.extend((f, j, int(k, 16)) for j in range(8 if f < 1500 or f in (2000, 2015) else 1))
events.extend([(1600, 0, 128), (1615, 0, 0)])
for step in range(3):
    events.extend([(2300 + step * 40, 0, 128), (2320 + step * 40, 0, 0)])
events.extend((3500, i, 17 if i < 4 else 0) for i in range(8))
inputs = a.results / 'survivors.csv'
inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(events)))
tiles = [0, 1, 7, 3, 5, 6, 2, 4]
for name, frames in [('mixed', 5200), ('survivors', 9000)]:
    snapshot = a.results / (name + '.json')
    if not a.analyze_only:
        proc = subprocess.run([
            str(a.runner), '--ring', '--rom', str(a.rom), '--bios', str(a.bios),
            '--players', '8', '--frames', str(frames), '--input', str(inputs),
            '--barrier-before-input', '--barrier-state', '0x03006B40',
            '--screens', str(a.results / name)
        ], capture_output=True, text=True, timeout=1200)
        snapshot.write_text(proc.stdout)
        (a.results / (name + '.stderr')).write_text(proc.stderr)
        assert proc.returncode == 0 and not proc.stderr, (proc.returncode, proc.stderr)
    result = json.loads(snapshot.read_text())
    assert result['bios'] == 'real' and result['collisions'] == 0
    states = []
    for node in result['nodes']:
        assert node['barrier']['errors'] == node['barrier']['timeouts'] == 0
        drivers = node['game']['drivers']
        assert len({d['character'] for d in drivers}) == 8, 'Portraits must distinguish all racers'
        if name == 'survivors':
            assert [d['balloons'] for d in drivers] == [0] * 4 + [3] * 4
        else:
            # A first-surviving-ID implementation would incorrectly include
            # this living low-index racer ahead of someone with more balloons.
            assert any(0 < d['balloons'] < 3 for d in drivers[:4])
            assert all(d['balloons'] == 3 for d in drivers[4:])
        ranked = sorted(range(8), key=lambda i: (-drivers[i]['balloons'], i))[:4]
        assert ranked == list(range(4, 8))
        for row, id in enumerate(ranked):
            tile = 0xD080 + tiles[drivers[id]['character'] & 7] * 4
            assert node['game']['hud']['portraits'][row] == list(range(tile, tile + 4))
        states.append([d['position'] for d in drivers])
    assert all(s == states[0] for s in states)
    print(f'PASS {name}: four highest balloon counts occupy all eight Battle HUDs', flush=True)
