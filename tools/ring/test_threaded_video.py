#!/usr/bin/env python3
"""Require threaded rendering to preserve exact linked execution and output."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import wave

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'bios', 'inputs', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
captures = []
for name, quantum, threaded in (('baseline', 2048, False), ('threaded', 2048, True)):
    target = a.results / name
    target.mkdir(exist_ok=True)
    command = [str(a.runner), '--ring', '--ring-quantum', str(quantum),
               '--rom', str(a.rom), '--bios', str(a.bios), '--players', '8',
               '--frames', '6600', '--input', str(a.inputs),
               '--autodrive-frame', '3500', '--autofire',
               '--barrier-before-input', '--barrier-state', '0x03006B40',
               '--screens', str(target / 'screen'), '--audio-prefix', str(target / 'audio')]
    if threaded:
        command.append('--threaded-video')
    proc = subprocess.run(command, capture_output=True, text=True, timeout=600)
    (target / 'result.json').write_text(proc.stdout)
    (target / 'result.stderr').write_text(proc.stderr)
    assert proc.returncode == 0 and not proc.stderr, (name, proc.stderr)
    state = json.loads(proc.stdout)
    assert state['bios'] == 'real' and not state['collisions'], name
    output = []
    for node in state['nodes']:
        slot = node['slot']
        assert node['barrier']['errors'] == node['barrier']['timeouts'] == 0, name
        with wave.open(str(target / f'audio-{slot}.wav')) as recording:
            audio = recording.readframes(recording.getnframes())
            assert recording.getnframes() > 48000 * 30, (name, slot, 'Missing audio capture')
            format_ = (recording.getnchannels(), recording.getsampwidth(), recording.getframerate())
        output.append((node['game'], node['barrier'],
                       (target / f'screen-{slot}.ppm').read_bytes(), format_, audio))
    captures.append((state, output))

assert captures[0][0] == captures[1][0], 'Threaded rendering changed CPU, transport or game state'
for slot, (reference, actual) in enumerate(zip(captures[0][1], captures[1][1])):
    assert reference == actual, (slot, 'Threaded rendering changed framebuffer, audio format or PCM')
print('PASS threaded renderer: exact snapshots, framebuffers and PCM match on all eight consoles', flush=True)
