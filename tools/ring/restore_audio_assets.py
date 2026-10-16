#!/usr/bin/env python3
"""Import only the sample/instrument changes from the supplied Music Restore IPS.

Generated assembly keeps every original ABS32 relocation and gives appended
samples a linker-owned base. Graphics and rendering changes in the reference
patch are deliberately excluded. The patch and ROMs remain local build inputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

PATCH_SHA256 = 'ebbad9fbb573b38c0bb18a811b7cff5bf2cdf3ab14be030cdb681f9f8772ae42'
STOCK_SHA1 = '9d327c030c3e2d9007990518594f70c3340ac56f'
BASE = 0x08000000
SAMPLE_BASE = 0x00400000


def apply_ips(source, patch):
    assert patch[:5] == b'PATCH', 'Expected an IPS patch'
    output = bytearray(source)
    pos = 5
    while patch[pos:pos + 3] != b'EOF':
        offset = int.from_bytes(patch[pos:pos + 3], 'big')
        count = int.from_bytes(patch[pos + 3:pos + 5], 'big')
        pos += 5
        if count:
            data = patch[pos:pos + count]
            assert len(data) == count
            pos += count
        else:
            count = int.from_bytes(patch[pos:pos + 2], 'big')
            data = patch[pos + 2:pos + 3] * count
            pos += 3
            assert count and len(data) == count
        if offset + count > len(output):
            output.extend(bytes(offset + count - len(output)))
        output[offset:offset + count] = data
    pos += 3
    if pos != len(patch):
        assert len(patch) == pos + 3
        size = int.from_bytes(patch[pos:], 'big')
        output = output[:size] + bytes(max(0, size - len(output)))
    return bytes(output)


class ELF:
    """Read the small ELF32 subset needed for ARM data relocations."""
    def __init__(self, path):
        self.data = path.read_bytes()
        assert self.data[:7] == b'\x7fELF\x01\x01\x01'
        offset = struct.unpack_from('<I', self.data, 32)[0]
        size, count, strings_index = struct.unpack_from('<3H', self.data, 46)
        self.sections = [struct.unpack_from('<10I', self.data, offset + i * size) for i in range(count)]
        names = self.contents(strings_index)
        self.names = [self.string(names, section[0]) for section in self.sections]
        self.symbols = {}
        for index, section in enumerate(self.sections):
            if section[1] != 2:
                continue
            names = self.contents(section[6])
            self.symbols[index] = [
                (self.string(names, name), value, length, info, target)
                for name, value, length, info, other, target in struct.iter_unpack('<IIIBBH', self.contents(index))]

    @staticmethod
    def string(data, offset):
        return data[offset:data.index(0, offset)].decode()

    def contents(self, index):
        section = self.sections[index]
        return self.data[section[4]:section[4] + section[5]]


def sound_sections(map_path):
    section = None
    for line in map_path.read_text().splitlines():
        match = re.match(r' (\.rodata\S*)\s*(.*)', line)
        if match:
            section, line = match.groups()
        match = re.fullmatch(r'\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(sound/\S+\.o)\s*', line)
        if match and int(match[2], 16):
            yield match[3], section, int(match[1], 16) - BASE, int(match[2], 16)


def generate(root, patch_path, output):
    stock = (root / 'baserom.gba').read_bytes()
    patch = patch_path.read_bytes()
    assert hashlib.sha1(stock).hexdigest() == STOCK_SHA1, 'Wrong US base ROM'
    assert hashlib.sha256(patch).hexdigest() == PATCH_SHA256, 'Unrecognized Music Restore IPS'
    assert (root / 'mksc.gba').read_bytes() == stock, 'Build matching stock objects before importing audio'
    restored = apply_ips(stock, patch)
    assert len(restored) > SAMPLE_BASE
    clean = ELF(root / 'mksc.elf')
    globals_by_address = {}
    for table in clean.symbols.values():
        for name, value, size, info, section in table:
            if name and section and info >> 4 == 1 and BASE <= value < BASE + len(stock):
                globals_by_address.setdefault(value, name)
    samples = restored[SAMPLE_BASE:]
    sample_targets = set()
    rules, changed = [], []
    output.mkdir(parents=True, exist_ok=True)
    for obj, section_name, start, length in sound_sections(root / 'mksc.map'):
        data = restored[start:start + length]
        if data == stock[start:start + length]:
            continue
        elf = ELF(root / 'build' / obj)
        section = elf.names.index(section_name)
        original_data = elf.contents(section)
        assert len(data) == len(original_data) == length
        labels = {}
        for table in elf.symbols.values():
            for name, value, size, info, target in table:
                if target == section and name and not name.startswith(('$', '.')):
                    labels.setdefault(value, []).append((name, info >> 4 == 1))
        relocations = {}
        for index, header in enumerate(elf.sections):
            if header[1] != 9 or header[7] != section:
                continue
            for offset, info in struct.iter_unpack('<II', elf.contents(index)):
                assert info & 255 == 2, (obj, 'Expected R_ARM_ABS32')
                name, value, size, binding, target = elf.symbols[header[6]][info >> 8]
                addend = struct.unpack_from('<I', original_data, offset)[0]
                old = struct.unpack_from('<I', stock, start + offset)[0]
                new = struct.unpack_from('<I', data, offset)[0]
                if new >= BASE + SAMPLE_BASE:
                    relative = new - BASE - SAMPLE_BASE
                    assert relative + 16 <= len(samples)
                    wave_type, status, frequency, loop, size = struct.unpack_from('<HHIII', samples, relative)
                    assert wave_type == 0 and status in (0, 0x4000) and 0 < size <= len(samples) - relative - 16
                    assert loop < size and frequency > 0
                    sample_targets.add(relative)
                    expression = f'ring_restoredAudioSamples + 0x{relative:X}'
                elif new != old:
                    assert new in globals_by_address, (obj, offset, hex(new), 'Unknown changed pointer')
                    expression = globals_by_address[new]
                else:
                    if target == section and binding & 15 == 3:
                        name = '.LrestoredAudioSection'
                    assert name
                    expression = name + (f' + 0x{addend:X}' if addend else '')
                relocations[offset] = expression
        # All appended sample pointers must originate in a real data relocation.
        # Never scan PCM data for accidental pointer-looking words.
        if '/bank/' in obj:
            for offset in range(0, length - 3, 4):
                pointer = struct.unpack_from('<I', data, offset)[0]
                if BASE + SAMPLE_BASE <= pointer < BASE + len(restored):
                    assert offset in relocations, (obj, offset, 'Missing sample relocation')
        lines = [f'.section {section_name}', '.balign 4', '.LrestoredAudioSection:']
        stops = sorted(set(labels) | set(relocations) | {length})
        cursor = 0
        for stop in stops:
            assert cursor <= stop <= length, (obj, cursor, stop)
            while cursor < stop:
                end = min(stop, cursor + 24)
                lines.append('.byte ' + ','.join(f'0x{byte:02X}' for byte in data[cursor:end]))
                cursor = end
            for name, exported in labels.get(stop, []):
                if exported:
                    lines.append('.global ' + name)
                lines.append(name + ':')
            if stop in relocations:
                lines.append('.4byte ' + relocations[stop])
                cursor += 4
        target = output / obj.replace('.o', '.s')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('\n'.join(lines) + '\n')
        relative = target.relative_to(root).as_posix()
        rules += [f'$(OBJ_DIR)/{obj}: {relative}', '\t$(AS) $(ASFLAGS) -o $@ $<']
        changed.append(obj)
    assert len(sample_targets) == 89 and len(changed) == 38, (len(sample_targets), len(changed))
    blob = output / 'samples.bin'
    blob.write_bytes(samples)
    metadata = {'patch_sha256': PATCH_SHA256, 'samples_sha256': hashlib.sha256(samples).hexdigest(),
                'sample_offsets': sorted(sample_targets), 'changed_objects': changed}
    (output / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (output / 'objects.mk').write_text('\n'.join(rules) + '\n')
    print(f'Imported {len(changed)} audio objects and {len(sample_targets)} sample references; excluded all non-audio changes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    generate(root, args.patch, args.output or root / 'build-ring/restored-audio')


if __name__ == '__main__':
    main()
