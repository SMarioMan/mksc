#!/usr/bin/env python3
"""Compare linked restored instruments/PCM with the supplied reference audio.

This checks every changed audio object after resolving its original ARM
relocations, rather than expecting the reference patch's fixed ROM addresses.
"""
import argparse
import hashlib
from pathlib import Path
import struct
from restore_audio_assets import ELF, BASE, SAMPLE_BASE, PATCH_SHA256, STOCK_SHA1, apply_ips, sound_sections


def symbols(elf):
    return {name: value for table in elf.symbols.values() for name, value, size, info, section in table
            if name and section and not name.startswith(('$', '.'))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch', required=True, type=Path)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--elf', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    stock = (root / 'baserom.gba').read_bytes()
    patch = args.patch.read_bytes()
    assert hashlib.sha1(stock).hexdigest() == STOCK_SHA1
    assert hashlib.sha256(patch).hexdigest() == PATCH_SHA256
    reference = apply_ips(stock, patch)
    actual = args.rom.read_bytes()
    linked = symbols(ELF(args.elf))
    blob = linked['ring_restoredAudioSamples']
    blob_end = linked['ring_restoredAudioSamples_end']
    assert actual[blob - BASE:blob_end - BASE] == reference[SAMPLE_BASE:], 'Restored PCM payload differs'
    objects = count = 0
    sample_targets = set()
    for obj, section_name, start, size in sound_sections(root / 'mksc.map'):
        if stock[start:start + size] == reference[start:start + size]:
            continue
        original = ELF(root / 'build' / obj)
        section = original.names.index(section_name)
        entries = [symbol for table in original.symbols.values() for symbol in table if symbol[4] == section]
        name, offset, length, info, target = next(symbol for symbol in entries if symbol[3] >> 4 == 1)
        destination = linked[name] - offset
        expected = bytearray(reference[start:start + size])
        for index, header in enumerate(original.sections):
            if header[1] != 9 or header[7] != section:
                continue
            for offset, info in struct.iter_unpack('<II', original.contents(index)):
                assert info & 255 == 2
                name, value, length, binding, target = original.symbols[header[6]][info >> 8]
                pointer = struct.unpack_from('<I', expected, offset)[0]
                if pointer >= BASE + SAMPLE_BASE:
                    sample_targets.add(pointer)
                    relocated = blob + pointer - BASE - SAMPLE_BASE
                else:
                    addend = struct.unpack_from('<I', original.contents(section), offset)[0]
                    if target == section and binding & 15 == 3:
                        relocated = destination + addend
                    else:
                        relocated = linked[name] + addend
                struct.pack_into('<I', expected, offset, relocated & 0xFFFFFFFF)
                count += 1
        assert actual[destination - BASE:destination - BASE + size] == expected, (obj, 'Instrument/sample data differs')
        objects += 1
    assert objects == 38 and len(sample_targets) == 89, (objects, len(sample_targets))
    # Keep the native output rate/volume/channel budget: doubling the mixer
    # rate exceeds the eight-player Battle CPU budget. Source samples and
    # instrument assignments still match the restoration exactly.
    assert b'\x00\xc6\x93\x00' in actual[linked['m4aSoundInit'] - BASE:(linked['m4aSoundInit'] - BASE) + 192]
    print(f'PASS restored audio: {objects} objects, {count} relocations, {len(sample_targets)} distinct PCM references, exact PCM payload and native 10512 Hz mixer')


if __name__ == '__main__':
    main()
