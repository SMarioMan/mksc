#!/usr/bin/env python3
"""Check discovery audio without starting a race, using real BIOS boot."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import wave
p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
if hashlib.sha1(a.bios.read_bytes()).hexdigest() != '300c20df6731a33952ded8c436f7f186d25d3492':
    p.error('Use the supplied real GBA BIOS')
a.results.mkdir(parents=True, exist_ok=True)
rom_data = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(rom_data).hexdigest() + '.gba')
if not rom.exists():
    rom.write_bytes(rom_data)
symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output(
    ['arm-none-eabi-nm', '-n', str(a.elf)], text=True).splitlines() if len(f := line.split()) == 3}
song = struct.unpack_from('<I', rom_data, symbols['gSongTable'] - 0x08000000 + 40 * 8)[0]
# Enter the connection screen without ever electing a host. Its old silence
# assertion is replaced by the requested stock link BGM and an advancing mixer.
inputs = a.results / 'waiting.csv'
inputs.write_text(''.join(line + '\n' for line in
    (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines()
    if int(line.split(',')[0]) < 1500))
video = a.results / 'waiting-video.csv'
proc = subprocess.run([str(a.runner), '--ring', '--ring-quantum', '2048',
                       '--rom', str(rom), '--bios', str(a.bios),
                       '--players', '2', '--frames', '2400', '--input', str(inputs),
                       '--video-from', '1200', '--video-log', str(video),
                       '--audio-state', hex(symbols['ring_audioTiming']),
                       '--audio-prefix', str(a.results / 'waiting'),
                       '--screens', str(a.results / 'lobby')],
                      capture_output=True, text=True, timeout=180)
(a.results / 'lobby.json').write_text(proc.stdout)
(a.results / 'lobby.stderr').write_text(proc.stderr)
assert proc.returncode == 0 and not proc.stderr, proc.stderr
result = json.loads(proc.stdout)
frames = list(csv.DictReader(video.open()))
for node in result['nodes']:
    assert node['siocnt'] == 0x5082, node  # Slave armed at the normal-32 fast clock setting.
    slot = node['slot']
    peer = [row for row in frames if int(row['player']) == slot and int(row['frame']) < 2400]
    assert len(peer) == 1200, slot
    assert all(int(row['musicHeader']) == song and int(row['musicStatus']) & 0xffff
               and not int(row['musicStatus']) & 0x80000000 for row in peer), ('Stock link music stopped', slot)
    assert int(peer[-1]['audioMixFrames']) - int(peer[0]['audioMixFrames']) >= 1198, ('Mixer froze', slot)
    with wave.open(str(a.results / f'waiting-{slot}.wav')) as recording:
        pcm = recording.readframes(recording.getnframes())
        assert recording.getnframes() >= recording.getframerate() * 18, ('Missing lobby PCM', slot)
    blocks = [pcm[i:i + 1584] for i in range(0, len(pcm) - 1584, 1584)]
    assert sum(any(block) for block in blocks) > len(blocks) // 2, ('Silent link BGM', slot)
    assert len(set(blocks)) > len(blocks) // 2, ('Looping frozen audio buffer', slot)
print('PASS: both unassigned consoles play stock link music with continuously serviced, changing PCM')
