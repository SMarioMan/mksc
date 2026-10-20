#!/usr/bin/env python3
"""Check all eight native GP portraits in completed-frame PNG/PPM captures.

Requires Pillow and arm-none-eabi-nm. This compares opaque pixels with native ROM art/palettes,
independently of the renderer's VRAM diagnostics.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--rom', required=True, type=Path)
p.add_argument('--elf', required=True, type=Path)
p.add_argument('--images', required=True, nargs='+', type=Path)
p.add_argument('--report', type=Path)
a = p.parse_args()
symbols = {}
for line in subprocess.check_output(['arm-none-eabi-nm', '-n', str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16) - 0x08000000
rom = a.rom.read_bytes()
palette = (rom[symbols['_0809A9AC']:symbols['_0809A9AC']+64] +
           rom[symbols['_080AACF0']:symbols['_080AACF0']+64])
names = ['Mario', 'Luigi', 'Peach', 'Toad', 'Yoshi', 'DK', 'Wario', 'Bowser']
portraits = []
for character in range(8):
    pointers = struct.unpack_from('<2I', rom, symbols['_080DB3DC'] + character * 8)
    expected = []
    for cy, pointer in enumerate(pointers):
        for cx, tile in enumerate(struct.unpack_from('<2H', rom, pointer - 0x08000000 + 2)):
            start = symbols['_0809AE2C'] + ((tile & 1023) - 0x80) * 32
            for y in range(8):
                for x in range(8):
                    tx, ty = (7-x if tile & 0x400 else x), (7-y if tile & 0x800 else y)
                    color = (rom[start + ty*4 + tx//2] >> (4*(tx & 1))) & 15
                    if not color:
                        continue
                    value = struct.unpack_from('<H', palette, ((tile >> 12) * 16 + color - 192) * 2)[0]
                    rgb = tuple(((value >> shift) & 31) * 8 + (((value >> shift) & 31) >> 2)
                                for shift in (0,5,10))
                    expected.append((cx*8+x,cy*8+y,rgb))
    assert len(expected) >= 128
    portraits.append(expected)

report, failures = [], []
for path in a.images:
    image = Image.open(path).convert('RGB')
    assert image.width % 240 == image.height % 160 == 0, (path, image.size)
    scale = image.width // 240
    assert scale >= 1 and image.height == scale * 160
    image = image.resize((240,160), Image.Resampling.NEAREST)
    selected, scores = [], []
    for row in range(8):
        ratios = []
        for expected in portraits:
            # Native GL rounding differs from software's RGB5 bit replication
            # by up to two RGB8 levels, without changing the pixel geometry.
            matched = sum(max(abs(x-y) for x,y in zip(image.getpixel((32+px,24+row*16+py)),color)) <= 2
                          for px,py,color in expected)
            ratios.append(matched / len(expected))
        character = max(range(8), key=ratios.__getitem__)
        selected.append(character)
        scores.append(ratios[character])
    passed = len(set(selected)) == 8 and min(scores) >= 0.95
    report.append({'image':str(path),'passed':passed,'max_channel_error':2,'characters':[names[i] for i in selected],
                   'opaque_pixel_match':scores})
    if not passed:
        failures.append(str(path))
if a.report:
    a.report.write_text(json.dumps(report,indent=2)+'\n')
assert not failures, ('Missing or corrupted results portraits', failures)
print(f'PASS {len(report)} captures: all eight native portraits rendered')
