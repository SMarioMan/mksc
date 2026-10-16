#!/usr/bin/env python3
"""Run a ring regression visibly at real-time speed, with sound and tiled windows."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
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
a = p.parse_args()
for key in ('gui_test', 'rom', 'bios', 'fixture'):
    path = getattr(a, key)
    if path is not None:
        path = path.resolve()
        if not path.is_file(): p.error(f'{key}: file does not exist: {path}')
        setattr(a, key, path)
if hashlib.sha1(a.bios.read_bytes()).hexdigest() != '300c20df6731a33952ded8c436f7f186d25d3492':
    p.error('Use the supplied real GBA BIOS')
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
    if a.fixture: env['MGBA_RING_GUI_REPLAY'] = str(a.fixture)
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
