#!/usr/bin/env python3
"""Measure gameplay input updates per video frame, using the real GBA BIOS.

A stationary grid keeps every racer in view and avoids finish/pause scenes.
Video FPS alone cannot detect a game that advances its physics every other frame.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, default=list(range(5, 9)))
p.add_argument('--modes', nargs='+', choices=['vs', 'battle'], default=['vs', 'battle'])
a = p.parse_args()
assert all(2 <= n <= 8 for n in a.players)
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
a.results.mkdir(parents=True, exist_ok=True)
data = a.rom.read_bytes()
digest = hashlib.sha256(data).hexdigest()
rom = a.results / (digest + '.gba')
if not rom.exists():
    rom.write_bytes(data)
base = [tuple(int(x, 16 if i == 2 else 10) for i, x in enumerate(line.split(',')))
        for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines()]
rates = {}
commit_rates = {}
for n in a.players:
    for mode in a.modes:
        name = f'{mode}-{n}'
        events = []
        for frame, slot, keys in base:
            if slot or frame >= 3500 or (mode == 'battle' and frame in (2500, 2515)):
                continue
            events.extend((frame, i, keys) for i in range(n if frame < 1500 or frame in (2000, 2015) else 1))
        if mode == 'battle':
            events.extend([(1600, 0, 128), (1615, 0, 0)])
        inputs = a.results / (name + '.csv')
        inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(events)))
        video = a.results / (name + '-video.csv')
        proc = subprocess.run([str(a.runner), '--ring', '--ring-quantum', '2048', '--rom', str(rom), '--bios', str(a.bios),
                               '--players', str(n), '--frames', '5100', '--input', str(inputs),
                               '--video-from', '4500', '--video-log', str(video),
                               '--barrier-before-input', '--barrier-state', '0x03006B40'],
                              capture_output=True, text=True, timeout=300)
        (a.results / (name + '.json')).write_text(proc.stdout)
        (a.results / (name + '.stderr')).write_text(proc.stderr)
        assert proc.returncode == 0, (name, proc.stderr)
        state = json.loads(proc.stdout)
        assert state['collisions'] == 0, name
        assert all(node['barrier']['errors'] == node['barrier']['timeouts'] == 0 for node in state['nodes']), name
        rows = list(csv.DictReader(video.open()))
        rates[name] = []
        commit_rates[name] = []
        for i in range(n):
            peer = [row for row in rows if int(row['player']) == i and int(row['frame']) < 5100]
            assert len(peer) == 600, (name, i, len(peer))
            rate = (int(peer[-1]['simulationFrames']) - int(peer[0]['simulationFrames'])) / 599
            commit_rates[name].append((int(peer[-1]['commits']) - int(peer[0]['commits'])) / 599)
            rates[name].append(rate)
        (a.results / 'rates.json').write_text(json.dumps({'rom_sha256': digest, 'updates_per_video_frame': rates, 'commits_per_video_frame': commit_rates}, indent=2))
        assert min(commit_rates[name]) >= 0.95, (name, 'Input barriers run below 95% speed', commit_rates[name])
        assert min(rates[name]) >= 0.95, (name, 'Gameplay runs below 95% speed', rates[name])
        print(f'PASS {name}: {min(rates[name]):.3f} game updates per video frame', flush=True)
