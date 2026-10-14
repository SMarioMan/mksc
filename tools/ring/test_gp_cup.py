#!/usr/bin/env python3
"""Drive a complete four-course Retro GP cup using real keypad inputs."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, default=[2, 8])
p.add_argument('--autofire', action='store_true', help='Also use items while driving')
a = p.parse_args()
if any(n < 1 or n > 8 for n in a.players):
    p.error('GP supports one through eight humans')
a.results.mkdir(parents=True, exist_ok=True)
symbols = {}
for line in subprocess.check_output(['arm-none-eabi-nm', '-n', str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
base = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    frame, slot, keys = line.split(',')
    if int(slot) == 0 and int(frame) < 3500 and int(frame) not in (2200, 2215, 2500, 2515, 3000, 3015, 3200, 3215):
        base.append((int(frame), int(keys, 16)))
for count in a.players:
    events = []
    for frame, keys in base:
        if count == 1 and frame == 1500:
            keys = 4
        events.extend((frame, i, keys) for i in range(count if frame < 1500 or frame in (2000, 2015) else 1))
    if count > 1:
        events.extend([(1600, 0, 64), (1615, 0, 0)])
    events.extend([(2800, 0, 4), (2815, 0, 0), (3200, 0, 1), (3215, 0, 0), (3500, 0, 1), (3515, 0, 0)])
    name = f'gp-cup-{count}'
    inputs = a.results / (name + '.csv')
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(events)))
    command = [
        str(a.runner), '--ring', '--rom', str(a.rom), '--bios', str(a.bios),
        '--players', str(count), '--frames', '90000', '--input', str(inputs),
        '--autodrive-frame', '4500', '--barrier-before-input',
        '--barrier-state', '0x03006B40', '--until-scene', hex(symbols['results_main'] | 1),
        '--auto-confirm-scene', hex(symbols['results_main'] | 1),
        '--watch', hex(symbols['sGPCupComplete']), '--until-watch-value', '1',
        '--settle-frames', '3',
        '--dump', hex(symbols['sGPPoints']), '--screens', str(a.results / name)
    ]
    command.extend(['--progress-file', str(a.results / (name + '-progress.csv'))])
    if a.autofire:
        command.append('--autofire')
    proc = subprocess.run(command, capture_output=True, text=True, timeout=1800)
    (a.results / (name + '.json')).write_text(proc.stdout)
    (a.results / (name + '.stderr')).write_text(proc.stderr)
    assert proc.returncode == 0 and not proc.stderr, (name, proc.returncode, proc.stderr)
    result = json.loads(proc.stdout)
    assert result['bios'] == 'real' and result['collisions'] == 0
    states = []
    for node in result['nodes']:
        assert node['watch'] == 1
        assert node['barrier']['errors'] == node['barrier']['timeouts'] == 0
        points = struct.unpack('<8H', struct.pack('<4I', *node['dump'][:4]))
        assert sum(points) == 76, (name, points)
        drivers = node['game']['drivers']
        assert len(drivers) == 8 and node['game']['humanCount'] == count
        assert [d['points'] for d in drivers] == list(points), (name, 'Native racer totals differ')
        standings = node['game']['nativeStandings']
        totals = [row['points'] for row in standings]
        assert totals == sorted(points, reverse=True), (name, 'Native standings are not sorted')
        assert sorted(row['menuCharacter'] for row in standings) == list(range(8))
        assert all(row['place'] == totals.index(row['points']) for row in standings), (name, 'Native tied ranks differ')
        assert node['game']['finishLap'] == 0x85, (name, 'Retro GP must retain five laps')
        assert sum(d['lap'] >= 6 for d in drivers) >= 7
        states.append((points, [d['position'] for d in drivers]))
    assert all(s == states[0] for s in states), (name, 'Cup standings or physics diverged')
    print(f'PASS {name}: four five-lap races, common 76-point total, {count} humans/{8-count} CPUs', flush=True)
