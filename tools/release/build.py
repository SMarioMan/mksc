#!/usr/bin/env python3
"""Build the complete ROM/IPS matrix in a disposable source copy.

Inputs stay outside the Docker image. No emulation or BIOS is required for
this packaging check; gameplay validation remains a separate release gate.
"""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from ips import apply, create

SOURCE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOURCE / 'tools/ring'))
from restore_audio_assets import PATCH_SHA256, apply_ips

STOCK_SHA1 = '9d327c030c3e2d9007990518594f70c3340ac56f'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-rom', type=Path, default=Path('/inputs/baserom.gba'))
    audio = parser.add_mutually_exclusive_group()
    audio.add_argument('--music-restoration-ips', type=Path)
    audio.add_argument('--without-restored-audio', action='store_true',
                       help='Build the eight original-sample variants instead of all sixteen')
    parser.add_argument('--output', type=Path, default=Path('/out'), help='An empty writable directory')
    parser.add_argument('--jobs', type=int, choices=range(1,65), default=4)
    args = parser.parse_args()
    try:
        base = args.base_rom.read_bytes()
    except OSError as error:
        parser.error(str(error))
    if hashlib.sha1(base).hexdigest() != STOCK_SHA1:
        parser.error('Expected the original US ROM, SHA-1 ' + STOCK_SHA1)
    restoration = None
    if not args.without_restored_audio:
        restoration = (args.music_restoration_ips or Path('/inputs/MusicRestore.ips')).resolve()
        try:
            patch = restoration.read_bytes()
        except OSError as error:
            parser.error(str(error) + '; supply the Music Restore IPS, or explicitly use --without-restored-audio')
        if digest(patch) != PATCH_SHA256:
            parser.error('The supplied Music Restore IPS has an unexpected SHA-256')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        parser.error('--output must be empty, to avoid mixing old and new releases')

    def ignore(directory, names):
        # Retain the installed matching compiler and its ARM libraries.
        return [n for n in names if n in ('.git', '__pycache__', 'build', 'build-ring', 'release', 'releases')
                or n.endswith(('.gba', '.elf', '.map', '.sym', '.sav', '.ips', '.pyc'))]

    with tempfile.TemporaryDirectory(prefix='mksc-release-') as temporary:
        work = Path(temporary) / 'source'
        stage = Path(temporary) / 'output'
        stage.mkdir()
        (stage / 'logs').mkdir()
        shutil.copytree(SOURCE, work, ignore=ignore)
        (work / 'baserom.gba').write_bytes(base)
        variants = {}
        files = {}

        def run(name, command, cwd=work):
            print('BUILD', name, flush=True)
            log = stage / 'logs' / (name + '.log')
            with log.open('w') as stream:
                result = subprocess.run([str(v) for v in command], cwd=cwd,
                                        stdout=stream, stderr=subprocess.STDOUT)
            if result.returncode:
                print('\n'.join(log.read_text(errors='replace').splitlines()[-50:]), file=sys.stderr)
                raise RuntimeError(name + ' failed; release was not published')

        def publish(name, stem, options):
            for extension in ('gba', 'elf', 'map', 'sym'):
                shutil.copyfile(stem.with_suffix('.' + extension), stage / (name + '.' + extension))
            target = (stage / (name + '.gba')).read_bytes()
            patch = create(base, target)
            # Use both the strict release decoder and the independently
            # implemented importer for the user's reference audio patch.
            reconstructed = apply(base, patch)
            if reconstructed != target or apply_ips(base, patch) != target:
                raise RuntimeError('IPS reconstruction differs: ' + name)
            expected = digest(target)
            if digest(reconstructed) != expected:
                raise RuntimeError('IPS SHA-256 differs: ' + name)
            (stage / (name + '.ips')).write_bytes(patch)
            variants[name] = {**options, 'bytes': len(target), 'sha256': expected,
                              'ips_sha256': digest(patch), 'ips_applied_sha256': digest(reconstructed)}
            print('VERIFIED', name, expected, flush=True)

        run('stock', ['make', '-j' + str(args.jobs)])
        if (work / 'mksc.gba').read_bytes() != base:
            raise RuntimeError('Clean ROM no longer matches its original hash/bytes')
        publish('mksc', work / 'mksc', {'kind': 'stock'})
        for restored, art, boot, music in itertools.product(
                (False, True) if restoration else (False,), ('original', 'japanese'),
                (False, True), ('normal', 'shared')):
            name = ('mksc-ring' + ('-boot' if boot else '') + ('-shared' if music == 'shared' else '')
                    + ('-japanese-sunset' if art == 'japanese' else '')
                    + ('-restored-audio' if restored else ''))
            command = ['python3', 'tools/ring/build.py', '--jobs', args.jobs,
                       '--music', music, '--sunset-art', art]
            if boot:
                command.append('--boot-ring')
            if restored:
                command.extend(['--music-restoration-ips', restoration])
            run(name, command)
            publish(name, work / 'mksc-ring', {'kind': 'ring', 'boot_ring': boot, 'music': music,
                    'sunset_art': art, 'restored_audio': restored,
                    'ring_consoles': 16, 'racer_slots': 8, 'automatic_maps': True})
        run('ring-poc', ['make', '-C', 'tools/ring-poc', 'OUT=' + str(work / 'poc')])
        publish('ring-poc', work / 'poc/ring-poc', {'kind': 'homebrew'})

        for path in sorted(stage.rglob('*')):
            if path.is_file():
                files[str(path.relative_to(stage))] = digest(path.read_bytes())
        manifest = {'format_version': 1, 'base_rom_sha1': STOCK_SHA1, 'base_rom_sha256': digest(base),
                    'all_variants': restoration is not None, 'ring_variant_count': len(variants) - 2,
                    'music_restoration_ips_sha256': PATCH_SHA256 if restoration else None,
                    'agbcc_revision': os.environ.get('AGBCC_REVISION', 'unrecorded local toolchain'),
                    'arm_gcc': subprocess.check_output(['arm-none-eabi-gcc', '--version'], text=True).splitlines()[0],
                    'variants': variants, 'files_sha256': files,
                    'ips_validation': 'Two decoders; reconstructed bytes and SHA-256 match each built ROM',
                    'gameplay_validation': 'Not performed by this packaging command; use tools/ring/verify_release.py'}
        (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (stage / 'PATCHES.txt').write_text(
            'Apply each IPS to the original US Mario Kart: Super Circuit ROM.\n'
            'Base SHA-1: ' + STOCK_SHA1 + '\n'
            'All patches are round-trip verified; expected target hashes are in manifest.json.\n'
            'Every ring image includes automatic map consoles 9-16. No separate map image exists.\n'
            'ring-poc is standalone homebrew; its optional IPS also uses the same US base and truncates it.\n'
            'mksc.ips reproduces the unchanged original ROM.\n'
            'ROMs and debugging symbols stay outside the patch-only distribution archive.\n')
        with zipfile.ZipFile(stage / 'mksc-patches.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(stage.glob('*.ips')) + [stage / 'manifest.json', stage / 'PATCHES.txt']:
                archive.write(path, path.name)
        sums = [(str(p.relative_to(stage)), digest(p.read_bytes())) for p in sorted(stage.rglob('*')) if p.is_file()]
        (stage / 'SHA256SUMS').write_text(''.join(h + '  ' + name + '\n' for name, h in sums))
        # All variants and every patch must succeed before any deliverable is published.
        for path in stage.iterdir():
            shutil.move(str(path), str(output / path.name))
        print('PASS:', len(variants), 'ROMs and verified IPS patches;', manifest['ring_variant_count'],
              'ring variants. Output:', output, flush=True)


if __name__ == '__main__':
    main()
