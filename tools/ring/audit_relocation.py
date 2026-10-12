#!/usr/bin/env python3
"""Check every aligned ROM function-pointer word after a controlled code shift.

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
    if not (re.fullmatch(r'wave_\d+', name) or name == 'courseData_bin'):
        continue
    end = symbols.get(name + '_end')
    if end is None or not 0x08000000 <= start < end:
        continue
    start -= 0x08000000
    end -= 0x08000000
    assert original[start:end] == shifted[start + a.padding:end + a.padding], (name, 'Opaque asset changed')
    opaque.append((start, end))
checked = 0
sample_matches = 0
failures = []
for offset in range(0, len(original) - 3, 4):
    value = struct.unpack_from('<I', original, offset)[0]
    if value not in addresses:
        continue
    if any(start <= offset < end for start, end in opaque):
        sample_matches += 1
        continue
    destination = offset + (a.padding if offset >= a.start else 0)
    actual = struct.unpack_from('<I', shifted, destination)[0]
    checked += 1
    if actual != value + a.padding:
        failures.append((hex(0x08000000 + offset), addresses[value], hex(actual)))
assert not failures, ('Unrelocated function-pointer words', failures)
assert checked, 'No function pointers were examined'
print(f'PASS relocation: {len(addresses)} ROM functions shift by {a.padding} bytes; {checked} callback words relocate; {sample_matches} coincidences in byte-identical PCM/course assets')
