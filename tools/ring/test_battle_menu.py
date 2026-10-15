#!/usr/bin/env python3
"""Check native Battle page switches against the supplied restoration assets."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
from restore_battle_assets import apply_bps

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, choices=range(2, 9), default=[2, 8])
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
clean = (root / 'baserom.gba').read_bytes()
restored = apply_bps(clean, (Path(__file__).parent / 'fixtures/snes-battle-restoration.bps').read_bytes())
rom = a.rom.read_bytes()
symbols = {f[2]: int(f[0], 16) for line in subprocess.check_output(
    ['arm-none-eabi-nm', '-n', str(a.elf)], text=True).splitlines() if len(f := line.split()) == 3}


def word(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def decompress(data, offset):
    assert data[offset] == 0x10
    size = int.from_bytes(data[offset + 1:offset + 4], 'little')
    at, out = offset + 4, bytearray()
    while len(out) < size:
        flags = data[at]
        at += 1
        for bit in range(7, -1, -1):
            if flags & (1 << bit):
                count = (data[at] >> 4) + 3
                distance = ((data[at] & 15) << 8 | data[at + 1]) + 1
                at += 2
                for _ in range(count):
                    out.append(out[-distance])
            else:
                out.append(data[at])
                at += 1
            if len(out) >= size:
                break
    return bytes(out[:size])


base = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    frame, slot, keys = line.split(',')
    if int(slot) == 0 and int(frame) < 2200:
        base.append((int(frame), int(keys, 16)))
for count in a.players:
    for arena, retro in [(i, True) for i in range(4)] + [(0, False)]:
        name = f'{"retro" if retro else "roundtrip"}-{arena + 1}-{count}'
        header = word(rom, symbols['gBattleCup'] - 0x08000000 + arena * 4)
        clean_def = word(clean, 0xE7FEC + header * 4) - 0x08000000
        reference_def = 0xE7994 + ((47, 45, 44, 46)[arena] - 44) * 56 if retro else clean_def
        reference = restored if retro else clean
        tiles = decompress(reference, word(reference, reference_def + 36) - 0x08000000)
        palette = decompress(reference, word(reference, reference_def + 40) - 0x08000000)
        old_palette = decompress(clean, word(clean, clean_def + 40) - 0x08000000)
        # Sample colors that distinguish the restored thumbnail from the GBA
        # palette it replaced. This catches graphics-only publication as well
        # as mistakenly retaining a symbol for the clean palette at that ROM address.
        changed = [i for i in range(0, len(palette), 2) if palette[i:i + 2] != old_palette[i:i + 2]]
        offset = min(changed[0] if changed else 0, len(palette) - 16) & ~1
        colors = list(struct.unpack_from('<8H', palette, offset))
        rows = [(f, i, k) for f, k in base for i in range(count if f < 1500 or f in (2000, 2015) else 1)]
        rows += [(1600, 0, 128), (1615, 0, 0), (2450, 0, 256), (2465, 0, 0)]
        for step in range(arena):
            rows += [(2300 + step * 40, 0, 128), (2315 + step * 40, 0, 0)]
        if not retro:
            rows += [(2600, 0, 512), (2615, 0, 0)]
        inputs = a.results / (name + '.csv')
        inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(rows)))
        proc = subprocess.run([str(a.runner), '--ring', '--ring-quantum', '2048',
            '--rom', str(a.rom), '--bios', str(a.bios), '--players', str(count),
            '--frames', '2850', '--input', str(inputs), '--dump', '0x06004000',
            '--dump-words', str(len(tiles) // 4), '--keys-address', hex(0x05000000 + offset),
            '--key-slots', '8', '--screens', str(a.results / name)], capture_output=True, text=True, timeout=300)
        (a.results / (name + '.json')).write_text(proc.stdout)
        (a.results / (name + '.stderr')).write_text(proc.stderr)
        assert proc.returncode == 0 and not proc.stderr, (name, proc.stderr)
        state = json.loads(proc.stdout)
        assert state['bios'] == 'real' and not state['collisions'], name
        for node in state['nodes']:
            actual = struct.pack('<' + 'I' * len(node['dump']), *node['dump'])
            assert actual == tiles, (name, node['slot'], 'Native preview graphics differ from reference')
            assert node['menu']['heldKeys'] == colors, (name, node['slot'], 'Preview palette not published')
        print(f'PASS {name}: native preview tiles and distinguishing palette colors match reference', flush=True)
