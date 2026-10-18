#!/usr/bin/env python3
"""Check a automatic map members with real BIOS, keys and native course geometry."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + name, required=True, type=lambda value: Path(value).resolve())
p.add_argument('--consoles', nargs='+', type=int, choices=range(9, 17), default=list(range(9, 17)))
p.add_argument('--modes', nargs='+', choices=('vs', 'battle', 'gp'), default=['vs', 'battle', 'gp'])
p.add_argument('--workers', type=int, choices=range(1, 5), default=2)
p.add_argument('--frames', type=int, default=4800)
a = p.parse_args()
if a.frames < 4800:
    p.error('Capture at least 4800 frames, including moving gameplay')
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
a.results.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]

def symbols(path):
    return {f[2]: int(f[0], 16) for line in subprocess.check_output(
        ['arm-none-eabi-nm', '-n', str(path)], text=True).splitlines()
        if len(f := line.split()) == 3}

s = symbols(a.elf)
image = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(image).hexdigest() + '.gba')
if not rom.exists():
    rom.write_bytes(image)
subprocess.run(['python3', root/'tools/ring/test_game.py', '--inputs-only',
                '--results', a.results/'inputs', '--players', '8',
                '--modes', *a.modes], check=True, capture_output=True)

def trunc_div(numerator, denominator):
    return numerator // denominator if numerator >= 0 else -((-numerator) // denominator)

def run(count, mode):
    name = f'{mode}-{count}'
    fixture = f'{mode}-8'
    rows = [tuple(int(v, 16 if i == 2 else 10) for i, v in enumerate(line.split(',')))
            for line in (a.results/'inputs'/(fixture+'.csv')).read_text().splitlines()]
    # All consoles boot the same image and enter discovery. Later map
    # consoles forward the host's choices without submitting local input.
    rows += [(frame, slot, keys) for slot in range(8, count)
             for frame, source, keys in rows.copy() if not source and frame < 1500]
    inputs = a.results/(name+'.csv')
    inputs.write_text(''.join(f'{frame},{slot},{keys:x}\n' for frame, slot, keys in sorted(rows)))
    video = a.results/(name+'-video.csv')
    command = [str(a.runner), '--ring', '--ring-quantum', '2048', '--players', str(count),
               '--rom', str(rom), '--bios', str(a.bios),
               '--frames', str(a.frames), '--input', str(inputs), '--video-from', '4500',
               '--video-log', str(video), '--audio-state', hex(s['ring_audioTiming']),
               '--sound-table', hex(s['gMPlayTable']), '--barrier-before-input',
               '--barrier-state', hex(s['sRing']), '--watch', '0x04000000',
               '--dump', '0x06002000', '--dump-words', '4096', '--screens', str(a.results/name)]
    if mode != 'battle':
        command += ['--autodrive-frame', '3500']
    process = subprocess.run(command, capture_output=True, text=True, timeout=1200)
    (a.results/(name+'.json')).write_text(process.stdout)
    (a.results/(name+'.stderr')).write_text(process.stderr)
    assert process.returncode == 0 and not process.stderr, (name, process.returncode, process.stderr[:1000])
    data = json.loads(process.stdout)
    assert data['bios'] == 'real' and not data['collisions']
    assert len(data['nodes']) == count
    states = []
    racers = 8
    for node in data['nodes']:
        barrier, game = node['barrier'], node['game']
        assert barrier['count'] == 8 and barrier['errors'] == barrier['timeouts'] == 0
        assert barrier['id'] == node['slot']
        assert game['sceneMain'] == s['race_main'] | 1 and game['humanCount'] == 8
        assert len({driver['character'] for driver in game['drivers'][:racers]}) == racers
        states.append([(driver['position'], driver['lap'], driver['balloons']) for driver in game['drivers'][:racers]])
    assert all(state == states[0] for state in states), (name, 'Spectator changed deterministic physics')
    for monitor in data['nodes'][8:]:
        assert monitor['game']['localId'] == 0 and monitor['watch'] & 65535 == 0x1441
        markers = monitor['game']['hud']['mapDisplayMarkers']
        assert [marker[0] for marker in markers] == list(range(racers)), (name, 'Missing map markers', markers)
        shift = monitor['game']['hud']['minimapShift'] - 8
        for (index, x, y), driver in zip(markers, monitor['game']['drivers'][:racers]):
            expected = [48 + trunc_div(driver['position'][0] >> shift, 114),
                        8 + trunc_div(driver['position'][1] >> shift, 114)]
            assert abs(x-expected[0]) <= 1 and abs(y-expected[1]) <= 1, (name, index, [x,y], expected)
        reference = struct.pack('<4096I', *data['nodes'][0]['dump'])[:2048]
        actual = struct.pack('<4096I', *monitor['dump'])[8192:8192+4160]
        expected = bytes(176 + color if color else 0 for packed in reference
                         for color in (packed & 15, packed >> 4))
        assert sum(packed != 0 for packed in reference) > 32, (name, 'Empty native overview')
        assert actual[:4096] == expected and not any(actual[4096:]), (name, 'Incomplete or incorrect course overview')
    timeline = [[] for _ in range(count)]
    for row in csv.DictReader(video.open()):
        timeline[int(row['player'])].append({key: int(value) for key, value in row.items()})
    for index, frames in enumerate(timeline):
        assert len(frames) >= 300
        elapsed = frames[-1]['frame'] - frames[0]['frame']
        updates = frames[-1]['simulationFrames'] - frames[0]['simulationFrames']
        assert updates / elapsed >= 0.95, (name, index, 'Slow map-linked gameplay', updates, elapsed)
        assert all(frame['invalidObjects'] == 0 for frame in frames)
        assert frames[-1]['audioLateSegments'] == frames[0]['audioLateSegments'], (name, index, 'Late PCM')
        assert frames[-1]['audioMissedFrames']-frames[0]['audioMissedFrames'] == frames[-1]['audioRecoveredFrames']-frames[0]['audioRecoveredFrames'], (name, index, 'Unfilled PCM')
    for display in timeline[8:]:
        for frame in display:
            assert frame['dispcnt'] == 0x1441 and frame['objects'] == racers, (name, 'Map display flickered')
            assert frame['musicStatus'] & 0x80000000 or not frame['musicStatus'] & 65535, (name, 'Map played music')
            assert frame['engineHeader'] == frame['voiceHeader'] == frame['effectHeader'] == 0
            assert all(frame[f'audioStarts{i}'] == 0 for i in range(11)), (name, 'Map started an audio player')
        assert len({frame['mapOverviewHash'] for frame in display}) == 1, (name, 'Map geometry or palette flickered')
    print(f'PASS {name}: {count} consoles, {count-8} automatic maps, {racers} common racers, exact whole-course geometry, aligned markers, silent display', flush=True)
    return name

passed, failures = [], []
with ThreadPoolExecutor(max_workers=a.workers) as pool:
    pending = {pool.submit(run, count, mode): f'{mode}-{count}' for count in a.consoles for mode in a.modes}
    for future in as_completed(pending):
        try:
            passed.append(future.result())
        except Exception as error:
            failures.append([pending[future], str(error)])
            print('FAIL', pending[future], str(error), flush=True)
        (a.results/'summary.json').write_text(json.dumps({'rom_sha256': hashlib.sha256(image).hexdigest(),
            'passed': passed, 'failures': failures}, indent=2)+'\n')
if failures:
    raise SystemExit('Map display checks failed')
