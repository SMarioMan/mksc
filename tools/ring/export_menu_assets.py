#!/usr/bin/env python3
"""Export native-resolution art references from the user's private clean ROM.

Requires Pillow. Never commit the exported original graphics or the base ROM.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--rom', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
rom = a.rom.read_bytes()
if hashlib.sha1(rom).hexdigest() != '9d327c030c3e2d9007990518594f70c3340ac56f':
    p.error('Use the clean USA MKSC ROM')
a.output.mkdir(parents=True, exist_ok=True)

def decompress(address):
    at = address - 0x08000000
    assert rom[at] == 0x10
    size = int.from_bytes(rom[at+1:at+4], 'little')
    at += 4
    output = bytearray()
    while len(output) < size:
        flags = rom[at]
        at += 1
        for bit in range(7, -1, -1):
            if len(output) >= size:
                break
            if flags & (1 << bit):
                token = int.from_bytes(rom[at:at+2], 'big')
                at += 2
                for _ in range((token >> 12) + 3):
                    output.append(output[-((token & 4095) + 1)])
            else:
                output.append(rom[at])
                at += 1
    return output

def export(name, tiles, palette, width, height):
    image = Image.new('RGBA', (width, height))
    for y in range(height):
        for x in range(width):
            offset = (y//8*(width//8)+x//8)*32+y%8*4+x%8//2
            index = tiles[offset] >> (x%2*4) & 15
            color = palette[index]
            image.putpixel((x,y), ((color&31)*255//31, (color>>5&31)*255//31,
                                  (color>>10&31)*255//31, 255 if index else 0))
    image.save(a.output / (name+'.png'))

for kind, table, width in [('player', 0xD8E9C, 64), ('count', 0xD8EBC, 32)]:
    for i in range(4):
        graphics, colors = struct.unpack_from('<II', rom, table+i*8)
        palette = struct.unpack_from('<16H', rom, colors-0x08000000)
        export(f'{kind}-{i+1}-reference', decompress(graphics), palette, width, 16)
markers = decompress(0x080B042C)
palette = struct.unpack_from('<16H', rom, 0xAFE00)
for i in range(4):
    export(f'marker-{i+1}-reference', markers[i*128:(i+1)*128], palette, 16, 16)
(a.output/'asset-specification.json').write_text(json.dumps({
    'format': 'RGBA PNG, native resolution, transparent index zero, at most 15 opaque colors per image',
    'player': {'size':[64,16], 'names':[f'player-{n}.png' for n in range(5,9)]},
    'count': {'size':[32,16], 'names':[f'count-{n}.png' for n in range(5,9)]},
    'marker': {'size':[16,16], 'names':[f'marker-{n}.png' for n in range(5,9)]},
    'colors': {'5':'blue','6':'orange','7':'cyan','8':'purple'},
}, indent=2)+'\n')
print(f'Exported twelve references and an asset specification to {a.output}')
