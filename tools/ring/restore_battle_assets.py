#!/usr/bin/env python3
"""Restore a separate SNES Battle set from BlueYoshi_57EML's reference BPS.

Only the supplied small delta is tracked. ROM-derived outputs stay in the
ignored build directory. Relative course pointers and symbol-based menu
definitions remain valid when the executable and its assets move.
"""
import argparse
import struct
import zlib
from pathlib import Path


def apply_bps(source, patch):
    assert patch[:4] == b'BPS1'
    at = 4

    def number():
        nonlocal at
        value, shift = 0, 1
        while True:
            byte = patch[at]
            at += 1
            value += (byte & 127) * shift
            if byte & 128:
                return value
            shift <<= 7
            value += shift

    source_size, target_size, metadata = number(), number(), number()
    assert source_size == len(source)
    at += metadata
    output = bytearray()
    source_position = target_position = 0
    while len(output) < target_size:
        command = number()
        action, length = command & 3, (command >> 2) + 1
        if action == 0:
            output += source[len(output):len(output) + length]
        elif action == 1:
            output += patch[at:at + length]
            at += length
        else:
            delta = number()
            delta = -(delta >> 1) if delta & 1 else delta >> 1
            if action == 2:
                source_position += delta
                output += source[source_position:source_position + length]
                source_position += length
            else:
                target_position += delta
                for _ in range(length):
                    output.append(output[target_position])
                    target_position += 1
    assert len(output) == target_size and at == len(patch) - 12
    assert struct.unpack_from('<III', patch, at) == (
        zlib.crc32(source), zlib.crc32(output), zlib.crc32(patch[:-4]))
    return output


def restore(source, restored):
    base, end, first, tail = 0x258000, 0x308000, 0x2FD014, 0x400000
    # Keep every original arena. Append the four restored course records and
    # their new tile/palette streams, eliminating the patch's unused ROM gap.
    courses = bytearray(source[base:end])
    courses += restored[first:end]
    courses += restored[tail:]
    # The ROM's arena menu order differs from its physical record order.
    # Keep the reference patch's Battle 1..4 order, matching hidden IDs20..23.
    for index, record in enumerate((47, 45, 44, 46)):
        original = base + struct.unpack_from('<I', source, base + record * 4)[0]
        destination = end - base + original - first
        struct.pack_into('<I', courses, (20 + index) * 4, destination)
        # Course headers store offsets relative to their own record, never
        # absolute CPU addresses. Only new streams cross the removed gap.
        for field in range(0x80, 0xE8, 4):
            relative = struct.unpack_from('<I', restored, original + field)[0]
            if relative and tail <= original + relative < len(restored):
                relative -= tail - end
                struct.pack_into('<I', courses, destination + field, relative)
                assert 0 <= destination + relative < len(courses)
    return courses


def compressed_asset(rom, start):
    """Read one complete BIOS LZ77 stream, including its alignment padding."""
    assert rom[start] == 0x10
    size = int.from_bytes(rom[start + 1:start + 4], 'little')
    at, produced = start + 4, 0
    while produced < size:
        flags = rom[at]
        at += 1
        for bit in range(7, -1, -1):
            if flags & (1 << bit):
                length = (rom[at] >> 4) + 3
                distance = ((rom[at] & 15) << 8 | rom[at + 1]) + 1
                assert distance <= produced
                at += 2
                produced += length
            else:
                at += 1
                produced += 1
            if produced >= size:
                break
    return rom[start:(at + 3) & ~3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source = args.rom.read_bytes()
    restored = apply_bps(source, (Path(__file__).parent / 'fixtures/snes-battle-restoration.bps').read_bytes())
    args.output.mkdir(parents=True, exist_ok=True)

    def write(name, value):
        path = args.output / name
        if not path.exists() or path.read_bytes() != value:
            path.write_bytes(value)

    write('courseData.bin', restore(source, restored))
    definitions = []
    for index, record in enumerate((47, 45, 44, 46)):
        source_index = record - 44
        values = list(struct.unpack_from('<13I', restored, 0xE7994 + source_index * 56))
        write(f'retro-battle-cover-{index}.bin', compressed_asset(restored, values[9] - 0x08000000))
        write(f'retro-battle-palette-{index}.bin', compressed_asset(restored, values[10] - 0x08000000))
        values[0] = 20 + index
        if values[3] == record:
            values[3] = 20 + index
        words = [f'_0{value:07X}' if value >= 0x08000000 else str(value) for value in values]
        words[9] = f'ring_retroBattleCover{index}'
        # Keep each thumbnail's reference palette with its graphics. The
        # reference shares a palette between arenas, whereas the original
        # GBA definitions select different palettes.
        words[10] = f'ring_retroBattlePalette{index}'
        definitions += [f'.global ring_retroBattleDef{index}', f'ring_retroBattleDef{index}:',
                        '.4byte ' + ', '.join(words), '.align 2',
                        f'ring_retroBattleCover{index}:',
                        f'.incbin "build-ring/retro-battle-cover-{index}.bin"', '.align 2',
                        f'ring_retroBattlePalette{index}:',
                        f'.incbin "build-ring/retro-battle-palette-{index}.bin"', '.align 2']
    write('retro-battle-defs.inc', ('\n'.join(definitions) + '\n').encode())
    print('Restored four separate Battle records; original GBA records preserved')


if __name__ == '__main__':
    main()
