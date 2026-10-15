#!/usr/bin/env python3
"""Check visible road projection changes on consecutive moving BIOS frames."""
import argparse
import csv
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
video = a.results / 'road-video.csv'
command = [str(a.runner), '--ring', '--ring-quantum', '2048',
           '--rom', str(a.rom), '--bios', str(a.bios), '--players', '8',
           '--frames', '6600', '--input', str(Path(__file__).parent / 'inputs/ring-vs.csv'),
           '--autodrive-frame', '3500', '--video-from', '6000', '--video-log', str(video),
           '--barrier-before-input', '--barrier-state', '0x03006B40']
# The stock fixture contains two participants. All eight consoles need its
# common title/connection/confirmation inputs, with only P1 navigating courses.
events = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    frame, slot, keys = line.split(',')
    if int(slot) or int(frame) >= 3500:
        continue
    events.extend((int(frame), i, keys) for i in range(8 if int(frame) < 1500 or int(frame) in (2000, 2015) else 1))
inputs = a.results / 'inputs.csv'
inputs.write_text(''.join(f'{f},{i},{k}\n' for f, i, k in sorted(events)))
command[command.index('--input') + 1] = str(inputs)
proc = subprocess.run(command, capture_output=True, text=True, timeout=300)
(a.results / 'result.json').write_text(proc.stdout)
(a.results / 'result.stderr').write_text(proc.stderr)
assert proc.returncode == 0 and not proc.stderr, proc.stderr
state = json.loads(proc.stdout)
assert state['bios'] == 'real' and not state['collisions']
assert all(not n['barrier']['errors'] and not n['barrier']['timeouts'] for n in state['nodes'])
rows = list(csv.DictReader(video.open()))
rates = []
for player in range(8):
    peer = [r for r in rows if int(r['player']) == player and int(r['frame']) < 6600]
    assert len(peer) == 600, (player, len(peer))
    changed = sum(x['roadProjectionHash'] != y['roadProjectionHash'] for x, y in zip(peer, peer[1:]))
    assert changed >= 595, (player, 'Visible road projection repeats', changed)
    assert all(r['roadDisplayBuffer'] == r['roadWriteBuffer'] == '0' for r in peer)
    rates.append({'player': player + 1, 'changed_intervals': changed, 'intervals': 599})
(a.results / 'rates.json').write_text(json.dumps(rates, indent=2) + '\n')
print('PASS all eight consoles: visible road projection changes every moving video frame')
