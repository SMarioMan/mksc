#!/usr/bin/env python3
"""Run a ring regression visibly at real-time speed, with sound and tiled windows."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--gui-test', required=True, type=Path, help='Qt build/qt/test-qt-ring_gui (or .exe)')
p.add_argument('--rom', required=True, type=Path, help='Normal-boot ring ROM; shared music is recommended')
p.add_argument('--bios', required=True, type=Path)
p.add_argument('--mode', choices=('vs', 'battle', 'gp'), default='vs')
p.add_argument('--players', type=int, choices=range(2, 9), default=8)
p.add_argument('--seconds', type=int, choices=range(35, 301), default=60, metavar='35..300', help='Moving gameplay duration, after BIOS and menu navigation')
p.add_argument('--fixture', type=Path, help='Replay an existing frame,player,hex-key-mask CSV instead of the default entry fixture')
p.add_argument('--log', type=Path, help='Also save the test output here')
p.add_argument('--display', choices=('software', 'opengl'), default='software', help='Window display backend')
p.add_argument('--hardware-renderer', action='store_true', help='Use the hardware GBA renderer (requires --display opengl and a compatible build)')
p.add_argument('--threaded-video', type=int, choices=(0, 1), help='Override the native software rendering worker')
p.add_argument('--results-symbols', type=Path, help='Verify finished GP results using the matching ROM ELF or full nm symbol table; requires --mode gp and --fixture')
p.add_argument('--engine', type=int, choices=(50,100,150), default=150, help='Expected engine class for GP results verification')
p.add_argument('--screenshots', type=Path, help='Keep P1..P8 results PNGs here; requires --results-symbols')
a = p.parse_args()
if a.hardware_renderer and a.display != 'opengl': p.error('--hardware-renderer requires --display opengl')
if a.results_symbols and (a.mode != 'gp' or not a.fixture):
    p.error('--results-symbols requires --mode gp and a recorded --fixture')
if a.screenshots and not a.results_symbols:
    p.error('--screenshots requires --results-symbols')
for key in ('gui_test', 'rom', 'bios', 'fixture', 'results_symbols'):
    path = getattr(a, key)
    if path is not None:
        path = path.resolve()
        if not path.is_file(): p.error(f'{key}: file does not exist: {path}')
        setattr(a, key, path)
if hashlib.sha1(a.bios.read_bytes()).hexdigest() != '300c20df6731a33952ded8c436f7f186d25d3492':
    p.error('Use the supplied real GBA BIOS')
results_scene = None
if a.results_symbols:
    data = a.results_symbols.read_bytes()
    if data.startswith(b'\x7fELF'):
        try:
            if data[4:6] != b'\x01\x01' or struct.unpack_from('<H',data,18)[0] != 40:
                raise ValueError('Use an ARM ELF32 little-endian file')
            start = struct.unpack_from('<I',data,32)[0]
            size, count = struct.unpack_from('<HH',data,46)
            if size < 40 or start + size * count > len(data):
                raise ValueError('Invalid ELF section table')
            sections = [struct.unpack_from('<10I',data,start+i*size) for i in range(count)]
            for section in sections:
                if section[1] != 2: continue  # SHT_SYMTAB, including local C functions
                strings = sections[section[6]]
                names = data[strings[4]:strings[4]+strings[5]]
                if section[9] < 16 or section[4]+section[5] > len(data):
                    raise ValueError('Invalid ELF symbols')
                for offset in range(section[4],section[4]+section[5],section[9]):
                    name, address = struct.unpack_from('<II',data,offset)
                    if names[name:names.find(b'\0',name)] == b'results_main':
                        results_scene = address | 1
                        code = sections[struct.unpack_from('<H',data,offset+14)[0]]
                        position = code[4] + (address & ~1) - code[3]
                        rom_offset = (address & ~1) - 0x08000000
                        image = a.rom.read_bytes()
                        if not (code[4] <= position and position + 32 <= min(len(data),code[4]+code[5])
                                and 0 <= rom_offset and rom_offset + 32 <= len(image)):
                            raise ValueError('Results function lies outside ROM/ELF code')
                        if data[position:position+32] != image[rom_offset:rom_offset+32]:
                            raise ValueError('Results function bytes do not match this ROM')
        except (ValueError,struct.error,IndexError) as error:
            p.error(str(error))
    else:
        for line in data.decode().splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[2] == 'results_main':
                results_scene = int(fields[0],16) | 1
    if results_scene is None or not 0x08000000 <= results_scene < 0x0A000000:
        p.error('No valid results_main symbol; use the matching ELF (the standard .sym exports globals only)')
if a.fixture:
    previous = -1
    for line, row in enumerate(a.fixture.read_text().splitlines(), 1):
        try:
            frame, player, keys = row.split(',')
            frame, player, keys = int(frame), int(player), int(keys, 16)
            assert frame >= previous and 0 <= player < a.players and 0 <= keys <= 0x3FF
        except (ValueError, AssertionError):
            p.error(f'Invalid or unsorted keypad row {line} in {a.fixture}')
        previous = frame
# Test-main isolates saved configuration, while the testcase copies the ROM
# and supplies private output paths. Never write saves alongside the user ROM.
with tempfile.TemporaryDirectory(prefix='mksc-visual-') as directory:
    subprocess.run([sys.executable, str(Path(__file__).with_name('test_game.py')), '--inputs-only', '--results', directory,
                    '--players', str(a.players), '--modes', a.mode], check=True, stdout=subprocess.DEVNULL)
    env = {key: value for key, value in os.environ.items() if not key.startswith('MGBA_RING_GUI_')}
    if env.get('QT_QPA_PLATFORM') in ('offscreen', 'minimal'): env.pop('QT_QPA_PLATFORM')
    if env.get('SDL_AUDIODRIVER') == 'dummy': env.pop('SDL_AUDIODRIVER')
    env.update(MGBA_RING_GUI_VISUAL='1', MGBA_RING_GUI_SYNC='1', MGBA_RING_GUI_MOTION='1', MGBA_RING_GUI_AUDIO='qt',
               MGBA_RING_GUI_AUDIO_FRAMES=str(round(a.seconds * 59.7275)), MGBA_RING_GUI_ROM=str(a.rom),
               MGBA_RING_GUI_BIOS=str(a.bios), MGBA_RING_GUI_INPUTS=directory)
    env['MGBA_RING_GUI_DISPLAY'] = str(int(a.display == 'opengl'))
    env['MGBA_RING_GUI_HARDWARE'] = str(int(a.hardware_renderer))
    if a.threaded_video is not None: env['MGBA_RING_GUI_THREADED'] = str(a.threaded_video)
    if a.fixture: env['MGBA_RING_GUI_REPLAY'] = str(a.fixture)
    if results_scene:
        env['MGBA_RING_GUI_FINISH_SCENE'] = hex(results_scene)
        env['MGBA_RING_GUI_ENGINE'] = str((50,100,150).index(a.engine))
    if a.screenshots:
        env['MGBA_RING_GUI_SCREENSHOTS'] = str(a.screenshots.resolve())
    print(f'Visual {a.mode}: {a.players} players, real BIOS, sound, {a.seconds}s moving gameplay.', flush=True)
    print('Windows are in P1..P8 order. Use the normal Pause command to pause the ring. Ctrl+C ends the review.', flush=True)
    log = a.log.open('w') if a.log else None
    try:
        process = subprocess.Popen([str(a.gui_test), f'ring:{a.mode}-{a.players}'], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        try:
            for line in process.stdout:
                print(line, end='', flush=True)
                if log: log.write(line); log.flush()
            code = process.wait()
        except KeyboardInterrupt:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
            code = 130
    finally:
        if log: log.close()
    raise SystemExit(code)
