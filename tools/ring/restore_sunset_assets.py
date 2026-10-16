#!/usr/bin/env python3
"""Import the supplied Japanese Sunset Wilds artwork as a checked asset delta.

The normal build consumes only the small BPS fixture. To recreate it, provide
both original ROMs to this script; neither ROM is copied into the repository.
Only Sunset Wilds course record 26 and its preview tiles are imported. Course
headers use relative offsets, so appending the record preserves relocation.
"""
import argparse
from difflib import SequenceMatcher
import hashlib
from pathlib import Path
import struct
import zlib
from restore_battle_assets import apply_bps, compressed_asset

BASE = 0x258000
COURSE = 26
JAPANESE_SHA1 = '88ffde363b05264a99a4d5ada0c80a00196a94d7'
US_SHA1 = '9d327c030c3e2d9007990518594f70c3340ac56f'
FIXTURE = Path(__file__).parent / 'fixtures/japanese-sunset-art.bps'


def course_record(rom):
    begin, end = struct.unpack_from('<2I', rom, BASE + COURSE * 4)
    return rom[BASE + begin:BASE + end]


def source_assets(rom):
    return course_record(rom) + compressed_asset(rom, 0x91718)


def restore(course_data, source):
    assets = apply_bps(source_assets(source), FIXTURE.read_bytes())
    size = struct.unpack_from('<I', assets)[0]
    course, cover = assets[4:4 + size], assets[4 + size:]
    assert len(course) == 0xA88C and len(cover) == 3836
    destination = len(course_data)
    course_data += course
    struct.pack_into('<I', course_data, COURSE * 4, destination)
    return cover


def number(value):
    out = bytearray()
    while True:
        byte = value & 127
        value >>= 7
        if not value:
            out.append(byte | 128)
            return out
        out.append(byte)
        value -= 1


def make_delta(source, target):
    patch = bytearray(b'BPS1') + number(len(source)) + number(len(target)) + number(0)
    position = copied = 0
    for match in SequenceMatcher(None, source, target).get_matching_blocks():
        if match.b > position:
            literal = target[position:match.b]
            patch += number(((len(literal) - 1) << 2) | 1) + literal
        if match.size:
            delta = match.a - copied
            patch += number(((match.size - 1) << 2) | 2)
            patch += number((abs(delta) << 1) | (delta < 0))
            copied = match.a + match.size
        position = match.b + match.size
    patch += struct.pack('<II', zlib.crc32(source), zlib.crc32(target))
    patch += struct.pack('<I', zlib.crc32(patch))
    assert apply_bps(source, patch) == target
    return patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--us-rom', required=True, type=Path)
    parser.add_argument('--japanese-rom', required=True, type=Path)
    args = parser.parse_args()
    us, japanese = args.us_rom.read_bytes(), args.japanese_rom.read_bytes()
    assert hashlib.sha1(us).hexdigest() == US_SHA1
    assert hashlib.sha1(japanese).hexdigest() == JAPANESE_SHA1
    course = course_record(japanese)
    # Japanese TrackDef for Sunset Wilds (preview graphics, same palette).
    cover = compressed_asset(japanese, 0xAD03C)
    assert compressed_asset(us, 0x91660) == compressed_asset(japanese, 0xACF84)
    target = struct.pack('<I', len(course)) + course + cover
    patch = make_delta(source_assets(us), target)
    FIXTURE.write_bytes(patch)
    print(f'Imported Japanese Sunset Wilds artwork: {len(patch)}-byte verified delta')


if __name__ == '__main__':
    main()
