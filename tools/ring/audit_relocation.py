#!/usr/bin/env python3
"""Check aligned and packed ROM callbacks after a controlled code shift.

Build the same variant twice, adding --code-padding 0x400 for the second.
This also scans incbin data, where fixed callback addresses can evade a source
search. Matches in compressed data require investigation if the check fails.
"""
import argparse
from pathlib import Path
import struct
import re

p = argparse.ArgumentParser(description=__doc__)
for key in ('rom', 'elf', 'shifted-rom', 'shifted-elf'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--padding', type=lambda s: int(s, 0), default=0x400)
p.add_argument('--start', type=lambda s: int(s, 0), default=0x1D8,
               help='First shifted ROM offset (end of the fixed startup code)')
a = p.parse_args()
if a.padding <= 0 or a.padding % 4:
    p.error('Use positive, word-aligned padding')

def functions(path):
    data = path.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01', 'Expected an ELF32 little-endian target'
    section_offset = struct.unpack_from('<I', data, 32)[0]
    section_size, section_count = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, section_offset + i * section_size) for i in range(section_count)]
    result = {}
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
            if index and name:
                end = strings.index(0, name)
                symbol = strings[name:end].decode()
                symbols[symbol] = value
            if index and info & 15 == 2 and 0x08000000 + a.start <= value < 0x0A000000:
                result[symbol] = value
    return result, symbols

original_functions, symbols = functions(a.elf)
shifted_functions, _ = functions(a.shifted_elf)
for name, value in original_functions.items():
    assert shifted_functions.get(name) == value + a.padding, (name, 'Function did not shift uniformly')
addresses = {value: name for name, value in original_functions.items()}
original, shifted = a.rom.read_bytes(), a.shifted_rom.read_bytes()
# PCM samples and native course blobs store bytes/relative offsets, not ARM
# callbacks. A few sample amplitudes happen to equal function addresses. Prove
# these opaque assets remain byte-identical rather than rewriting their pixels
# or audio as pointers. Script and menu tables are deliberately not excluded.
opaque = []
for name, start in symbols.items():
    if not (re.fullmatch(r'wave(?:_\d+|[0-9A-Fa-f]{8})', name) or name in ('courseData_bin', '_08089A04', '_080907B0', 'ring_restoredAudioSamples')):
        continue
    end = symbols.get(name + '_end')
    if end is None or not 0x08000000 <= start < end:
        continue
    start -= 0x08000000
    end -= 0x08000000
    if name in ('_08089A04', '_080907B0'):
        assert original[start] == 0x10 and int.from_bytes(original[start+1:start+4], 'little') == 0x1440, 'Expected LZ image asset'
    if name == '_080907B0':
        # Validate this complete compressed stream, including the interior
        # bytes that once acquired a false pointer label in the decompilation.
        cursor, produced = start + 4, 0
        while produced < 0x1440:
            flags = original[cursor]; cursor += 1
            for bit in range(7, -1, -1):
                if produced >= 0x1440: break
                if flags >> bit & 1:
                    first, second = original[cursor:cursor+2]; cursor += 2
                    assert ((first & 15) << 8 | second) + 1 <= produced, 'Invalid LZ back reference'
                    produced += (first >> 4) + 3
                else:
                    produced += 1; cursor += 1
        assert produced == 0x1440 and cursor == end, 'LZ asset boundary is not exact'
    assert original[start:end] == shifted[start + a.padding:end + a.padding], (name, 'Opaque asset changed')
    opaque.append((start, end))
# Packed script pointers can start between aligned words. Four more matches
# belong to native LZ images; validate complete streams and their exact padded
# boundaries before treating their compressed bytes as opaque.
for name, following, size in (
    ('_08085294', '_08085EE0', 0x1440),
    ('_080990CC', '_08099CFC', 0x1440),
    ('_080942E0', '_08095250', 0x1440),
    ('_08099DB4', '_0809A8CC', 0x1440),
    ('title_sDriversBgTiles2', 'title_sDriversBgTiles3', 0x1600),
):
    start, end = symbols[name] - 0x08000000, symbols[following] - 0x08000000
    assert original[start] == 0x10 and int.from_bytes(original[start+1:start+4], 'little') == size
    cursor, produced = start + 4, 0
    while produced < size:
        flags = original[cursor]; cursor += 1
        for bit in range(7, -1, -1):
            if produced >= size: break
            if flags >> bit & 1:
                first, second = original[cursor:cursor+2]; cursor += 2
                assert ((first & 15) << 8 | second) + 1 <= produced, (name, 'Invalid LZ back reference')
                produced += (first >> 4) + 3
            else:
                produced += 1; cursor += 1
    assert produced == size and 0 <= end - cursor < 4 and not any(original[cursor:end]), (name, 'Invalid image boundary/padding')
    assert original[start:end] == shifted[start+a.padding:end+a.padding], (name, 'Compressed image changed')
    opaque.append((start, end))
# Sunset sky 18 uses five separately compressed graphics strips. Its first
# strip can also contain a coincidental callback value after code grows.
sky = symbols['skyData_bin'] - 0x08000000
sky_end = symbols['skyData_bin_end'] - 0x08000000
sky_header = sky + struct.unpack_from('<I', original, sky + 18 * 4)[0]
assert original[sky_header + 1] & 3 == 3, 'Expected split compressed sky'
sky_graphics = sky_header + struct.unpack_from('<I', original, sky_header + 4)[0]
first_strip, second_strip = struct.unpack_from('<HH', original, sky_graphics)
start, limit = sky_graphics + first_strip, sky_graphics + second_strip
assert sky <= sky_header < sky_graphics < start < limit <= sky_end
assert original[start:start + 4] == bytes((0x10, 0, 8, 0))
cursor, produced = start + 4, 0
while produced < 0x800:
    flags = original[cursor]; cursor += 1
    for bit in range(7, -1, -1):
        if produced >= 0x800: break
        if flags >> bit & 1:
            first, second = original[cursor:cursor+2]; cursor += 2
            assert ((first & 15) << 8 | second) + 1 <= produced, 'Invalid sky LZ back reference'
            produced += (first >> 4) + 3
        else:
            produced += 1; cursor += 1
assert produced == 0x800 and cursor <= limit, 'Invalid sky strip boundary'
assert original[start:cursor] == shifted[start+a.padding:cursor+a.padding], 'Compressed sky changed'
opaque.append((start, cursor))
checked = 0
packed = 0
sample_matches = 0
failures = []
for offset in range(0, len(original) - 3):
    value = struct.unpack_from('<I', original, offset)[0]
    if value not in addresses:
        continue
    if any(start <= offset and offset + 4 <= end for start, end in opaque):
        sample_matches += 1
        continue
    # Three bytes crossing native Thumb instructions can resemble an odd
    # function pointer after new code changes its address. This exact sequence
    # constructs a fixed-point mask: movs r1,#imm; lsls r1,r1,#15/16; orrs r0,r1.
    # It also occurs in the unmodified embedded single-pak child executable.
    # Validate all three instructions, their alignment and their shifted bytes;
    # do not exempt general instruction regions or packed script tables.
    if offset & 1 and original[offset:offset+5] in (b'\x21\xc9\x03\x08\x43', b'\x21\x09\x04\x08\x43'):
        instruction_start = offset - 1
        moved = instruction_start + (a.padding if instruction_start >= a.start else 0)
        assert original[instruction_start:instruction_start+6] == shifted[moved:moved+6], 'Native mask instructions changed'
        sample_matches += 1
        continue
    destination = offset + (a.padding if offset >= a.start else 0)
    actual = struct.unpack_from('<I', shifted, destination)[0]
    checked += 1
    packed += bool(offset % 4)
    if actual != value + a.padding:
        failures.append((hex(0x08000000 + offset), addresses[value], hex(actual)))
assert not failures, ('Unrelocated aligned/packed function pointers', failures)
assert checked, 'No function pointers were examined'
rare = symbols['gCreditsRareLine'] - 0x08000000
assert original[rare:rare+4] == shifted[rare+a.padding:rare+a.padding+4] == bytes((11,13,9,8)), 'Packed credits layout was relocated as a pointer'
assert struct.unpack_from('<I', shifted, rare+a.padding+4)[0] == struct.unpack_from('<I', original, rare+4)[0] + a.padding, 'Credits text pointer did not relocate'
print(f'PASS relocation: {len(addresses)} ROM functions shift by {a.padding} bytes; {checked} callbacks relocate ({packed} packed, {checked-packed} aligned); {sample_matches} coincidences in byte-identical PCM/course/LZ image assets')
print('PASS credits: packed coordinates/count/delay remain native; the following text pointer relocates')
