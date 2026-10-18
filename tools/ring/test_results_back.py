#!/usr/bin/env python3
"""Replay a complete Vs. race, then back through course/character selection.

Requires test_rounds.py --modes vs --engine 1 captures from this exact ROM.
All interaction uses the recorded keypad; no menu or gameplay RAM is changed.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import wave

p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'elf', 'bios', 'baseline', 'results'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, choices=range(2, 9), default=[2, 8])
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
rom_bytes = a.rom.read_bytes()
assert (a.baseline / (hashlib.sha256(rom_bytes).hexdigest() + '.gba')).read_bytes() == rom_bytes, 'Baseline ROM differs'
symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output(
    ['arm-none-eabi-nm', '-n', str(a.elf)], text=True).splitlines() if len(f := line.split()) == 3}

for count in a.players:
    first = json.loads((a.baseline / f'vs-{count}.json').read_text())
    assert all(n['game']['sceneMain'] == symbols['results_main'] | 1 and
               n['game']['engineClass'] == 1 for n in first['nodes']), 'Complete a 100cc Vs. baseline first'
    frame = max(n['frame'] for n in first['nodes']) + 200
    events = [tuple(int(v, 16 if i == 2 else 10) for i, v in enumerate(line.split(',')))
              for line in (a.baseline / f'vs-{count}-recorded.csv').read_text().splitlines()]
    def press(offset, keys, players=(0,)):
        for slot in players:
            events.extend(((frame + offset, slot, keys), (frame + offset + 15, slot, 0)))
    press(0, 1)       # Results -> course selection.
    press(600, 2)     # Retained course preview -> course grid.
    press(1200, 2)    # Course grid -> character selection.
    press(1800, 2)    # Character -> class selection: the reported softlock.
    press(2400, 128)  # Change 100cc to 150cc after backing out.
    press(2600, 1)
    press(3100, 1, range(count))
    press(3400, 1)    # Host OK after all players confirm again.
    for offset in (3900, 4200, 4500):
        press(offset, 1)
    inputs = a.results / f'vs-{count}.csv'
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(events)))
    for stage, end in (('back', 2300), ('reenter', 5100)):
        name = f'{stage}-{count}'
        video = a.results / (name + '-video.csv')
        command = [str(a.runner), '--ring', '--ring-quantum', '2048', '--rom', str(a.rom),
                   '--bios', str(a.bios), '--players', str(count), '--frames', str(frame + end),
                   '--input', str(inputs), '--barrier-before-input', '--barrier-state', hex(symbols['sRing']),
                   '--dump', '0x0202A400', '--dump-words', '9', '--screens', str(a.results / name)]
        if stage == 'back':
            command += ['--video-from', str(frame + 1600), '--video-log', str(video),
                        '--audio-state', hex(symbols['ring_audioTiming']), '--audio-prefix', str(a.results / name)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=900)
        (a.results / (name + '.json')).write_text(result.stdout)
        (a.results / (name + '.stderr')).write_text(result.stderr)
        assert result.returncode == 0 and not result.stderr, (name, result.returncode, result.stderr[:2000])
        data = json.loads(result.stdout)
        assert data['bios'] == 'real' and data['collisions'] == 0
        for node in data['nodes']:
            assert node['barrier']['errors'] == node['barrier']['timeouts'] == 0, (name, node['barrier'])
            if stage == 'back':
                # The native class page clears raceSpeed until A confirms;
                # the retained 100cc cursor lives in its animation state.
                assert node['dump'][0] == 0 and node['dump'][4] == 2, (name, 'Did not return to Vs. class page', node['dump'])
            else:
                assert node['game']['sceneMain'] == symbols['race_main'] | 1, (name, 'Could not start another race', node['game'])
                assert node['game']['humanCount'] == count and node['game']['engineClass'] == 2, (name, node['game'])
        if stage == 'back':
            rows = list(csv.DictReader(video.open()))
            for slot in range(count):
                peer = [r for r in rows if int(r['player']) == slot and int(r['frame']) >= frame + 2000]
                assert len(peer) >= 290, (name, 'Menu stopped advancing', slot)
                assert int(peer[-1]['audioMixFrames']) - int(peer[0]['audioMixFrames']) >= len(peer) - 2, (name, 'Mixer froze', slot)
            with wave.open(str(a.results / f'{name}-0.wav')) as recording:
                recording.setpos(max(0, recording.getnframes() - recording.getframerate() * 3))
                pcm = recording.readframes(recording.getframerate() * 3)
            blocks = [pcm[i:i + 1584] for i in range(0, len(pcm) - 1584, 1584)]
            assert blocks and len(set(blocks)) > len(blocks) // 2, (name, 'Frozen menu PCM')
        print(f'PASS {name}: ' + ('native Back completes; mixer and PCM keep advancing' if stage == 'back' else
                                 'all players confirm again and enter a new 150cc race'), flush=True)
