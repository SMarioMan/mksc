#!/usr/bin/env python3
"""Drive a complete four-course Retro GP cup using real keypad inputs."""
import argparse
import hashlib
import csv
from collections import defaultdict
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, choices=range(2,9), default=list(range(2,9)))
p.add_argument('--engine', type=int, choices=range(3), default=0, help='0=50cc, 1=100cc, 2=150cc')
p.add_argument('--races', type=int, choices=(1,4), default=4, help='Stop at the first results, or complete the cup')
p.add_argument('--results-frames', type=int, default=90, help='Observe the final results without confirming')
p.add_argument('--analyze-only', action='store_true', help='Recheck the existing immutable recording without emulating')
p.add_argument('--cup', type=int, choices=range(5), default=0, help='Retro cup index; Special is 4')
p.add_argument('--consoles', type=int, choices=range(9,17), help='Include automatic map consoles; requires eight racers')
p.add_argument('--video-log', action='store_true', help='Record course-by-course graphics diagnostics')
p.add_argument('--bowser-p4', action='store_true', help='Select Bowser on P4 and swap Toad to P8')
p.add_argument('--autofire', action='store_true', help='Also use items while driving')
a = p.parse_args()
if any(n < 2 or n > 8 for n in a.players):
    p.error('GP supports two through eight humans')
if a.bowser_p4 and a.players != [8]:
    p.error('--bowser-p4 requires eight human racers')
if a.results_frames < 90:
    p.error('Observe at least 90 settled results frames')
if a.consoles and a.players != [8]:
    p.error('--consoles requires --players 8')
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
a.results.mkdir(parents=True, exist_ok=True)
symbols = {}
for line in subprocess.check_output(['arm-none-eabi-nm', '-n', str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)
image = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(image).hexdigest() + '.gba')
if a.analyze_only and not rom.exists():
    p.error('Reanalysis requires the original immutable ROM copy')
if not rom.exists():
    rom.write_bytes(image)

def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value

# The native standings table uses four portrait tiles per character. Compare
# the uploaded bytes with the original art, not merely nonempty screen pixels.
portrait_hashes = []
for character in range(8):
    pointers = struct.unpack_from('<2I', image, symbols['_080DB3DC'] - 0x08000000 + character * 8)
    tiles = [struct.unpack_from('<2H', image, pointer - 0x08000000 + 2) for pointer in pointers]
    graphics = bytearray()
    for row in tiles:
        for tile in row:
            offset = symbols['_0809AE2C'] - 0x08000000 + ((tile & 1023) - 0x80) * 32
            graphics.extend(image[offset:offset+32])
    portrait_hashes.append(fnv(graphics))

base = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    frame, slot, keys = line.split(',')
    if int(slot) == 0 and int(frame) < 3500 and int(frame) not in (2500, 2515, 3000, 3015, 3200, 3215):
        base.append((int(frame), int(keys, 16)))
for count in a.players:
    events = []
    for frame, keys in base:
        events.extend((frame, i, keys) for i in range(count if frame < 1500 or frame in (2000, 2015) else 1))
    if count > 1:
        events.extend([(1600, 0, 64), (1615, 0, 0)])
    events.extend([(2800, 0, 256), (2815, 0, 0), (3200, 0, 1), (3215, 0, 0), (3500, 0, 1), (3515, 0, 0)])
    if a.bowser_p4:
        # Native character select is a 3x3 grid with Random in the center.
        # Exchange Toad (cell 3) and Bowser (cell 8) before reserving either.
        for frame, first, second in ((1900,16,32),(1930,16,32),(1960,128,64)):
            events.extend([(frame,3,first),(frame+15,3,0),
                           (frame,7,second),(frame+15,7,0)])
    for step in range(a.engine):
        events.extend([(1720 + step * 35, 0, 128), (1735 + step * 35, 0, 0)])
    for step in range(a.cup):
        events.extend([(2900 + step * 40, 0, 128), (2915 + step * 40, 0, 0)])
    if a.consoles:
        events.extend((f, slot, k) for slot in range(8,a.consoles)
                      for f,i,k in events.copy() if not i and f < 1500)
    name = f'gp-cup-{a.cup}-{count}'
    if not a.analyze_only and (a.results/(name+'.json')).exists():
        p.error('Use a fresh results directory, or --analyze-only to preserve the recording')
    inputs = a.results / (name + '.csv')
    fixture = ''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(events))
    if a.analyze_only:
        assert inputs.read_text() == fixture, 'Reanalysis requires the same menu fixture'
    else:
        inputs.write_text(fixture)
    command = [
        str(a.runner), '--ring', '--ring-quantum', '2048', '--rom', str(rom), '--bios', str(a.bios),
        '--players', str(a.consoles or count), '--frames', '90000', '--input', str(inputs),
        '--autodrive-frame', '4500', '--barrier-before-input',
        '--barrier-state', '0x03006B40', '--until-scene', hex(symbols['results_main'] | 1),
        '--watch', hex(symbols['sGPCupComplete']),
        '--settle-frames', str(a.results_frames),
        '--dump', hex(symbols['sGPPoints']), '--record-inputs', str(a.results/(name+'-recorded.csv')), '--screens', str(a.results / name)
    ]
    if a.races == 4:
        command.extend(['--auto-confirm-scene',hex(symbols['results_main'] | 1),'--until-watch-value','1'])
    else:
        command.extend(['--release-inputs-scene',hex(symbols['results_main'] | 1)])
    command.extend(['--progress-file', str(a.results / (name + '-progress.csv'))])
    if a.video_log:
        command.extend(['--video-from','4500','--video-log',str(a.results/(name+'-video.csv')),
                        '--capture-scene',hex(symbols['results_main'] | 1),
                        '--capture-prefix',str(a.results/(name+'-results'))])
    if a.autofire:
        command.append('--autofire')
    if a.analyze_only:
        result = json.loads((a.results/(name+'.json')).read_text())
    else:
        proc = subprocess.run(command, capture_output=True, text=True, timeout=3600)
        (a.results / (name + '.json')).write_text(proc.stdout)
        (a.results / (name + '.stderr')).write_text(proc.stderr)
        assert proc.returncode == 0 and not proc.stderr, (name, proc.returncode, proc.stderr)
        result = json.loads(proc.stdout)
    assert result['bios'] == 'real' and result['collisions'] == 0
    assert len(result['nodes']) == (a.consoles or count)
    states = []
    for node in result['nodes']:
        assert node['watch'] == (a.races == 4)
        assert node['barrier']['errors'] == node['barrier']['timeouts'] == 0
        points = struct.unpack('<8H', struct.pack('<4I', *node['dump'][:4]))
        assert sum(points) == 19 * a.races, (name, points)
        drivers = node['game']['drivers']
        assert len(drivers) == 8 and node['game']['humanCount'] == count
        assert node['game']['engineClass'] == a.engine, 'Recorded engine class differs from the requested fixture'
        if a.bowser_p4:
            assert drivers[3]['character'] & 7 == 2, 'P4 must be Bowser'
            assert drivers[7]['character'] & 7 == 6, 'P8 must be Toad'
        assert [d['points'] for d in drivers] == list(points), (name, 'Native racer totals differ')
        standings = node['game']['nativeStandings']
        totals = [row['points'] for row in standings]
        assert totals == sorted(points, reverse=True), (name, 'Native standings are not sorted')
        assert sorted(row['menuCharacter'] for row in standings) == list(range(8))
        assert all(row['place'] == totals.index(row['points']) for row in standings), (name, 'Native tied ranks differ')
        assert node['game']['finishLap'] == 0x85, (name, 'Retro GP must retain five laps')
        assert sum(d['lap'] >= 6 for d in drivers) >= 7
        states.append((points, [d['position'] for d in drivers]))
    assert all(s == states[0] for s in states), (name, 'Cup standings or physics diverged')
    if a.video_log:
        results_scene, race_scene = symbols['results_main'] | 1, symbols['race_main'] | 1
        expected_headers = set(range(32 + a.cup * 4, 32 + a.cup * 4 + a.races))
        first_result, first_race = {}, {}
        portrait_frames, map_frames = defaultdict(int), defaultdict(int)
        geometry = defaultdict(set)
        errors = []
        map_ready = set()
        rendered_checks = 0
        with (a.results/(name+'-video.csv')).open() as stream:
            for raw in csv.DictReader(stream):
                row = {key:int(value) for key,value in raw.items()}
                header, slot, scene, frame = (row[k] for k in ('headerIndex','player','sceneMain','frame'))
                if header not in expected_headers:
                    continue
                key = (header,slot)
                if scene == race_scene:
                    first_race.setdefault(key,frame)
                if scene == results_scene and slot < count and row['leadLap'] >= 6:
                    first_result.setdefault(key,frame)
                    if frame - first_result[key] >= 30:
                        portrait_frames[key] += 1
                        if 'resultPortraitShown' in row:
                            rendered_checks += 1
                            if row['resultPortraitShown'] != 255 and len(errors) < 20:
                                errors.append({'kind':'obscured-portrait','header':header,'slot':slot,'frame':frame,'visible_mask':row['resultPortraitShown']})
                        for rank in range(8):
                            character = row[f'resultCharacter{rank}']
                            if character >= 8 or row[f'resultPortrait{rank}Hash'] != portrait_hashes[character]:
                                if len(errors) < 20:
                                    errors.append({'kind':'portrait','header':header,'slot':slot,'frame':frame,
                                                   'rank':rank,'character':character,'hash':row[f'resultPortrait{rank}Hash']})
                if slot >= 8 and scene in (race_scene,results_scene) and frame - first_race.get(key,frame) >= 300:
                    if key not in map_ready:
                        if row['dispcnt'] != 0x1441 or row['mapOverviewColoredPixels'] < 256:
                            continue
                        map_ready.add(key)
                    map_frames[key] += 1
                    geometry[key].add(row['mapOverviewHash'])
                    if row['dispcnt'] != 0x1441 or row['mapOverviewColoredPixels'] < 256:
                        if len(errors) < 20:
                            errors.append({'kind':'black-map','header':header,'slot':slot,'frame':frame,
                                           'colored_pixels':row['mapOverviewColoredPixels']})
        report = {'status':'failed', 'rom_sha256':hashlib.sha256(image).hexdigest(),
                  'cup':a.cup,'engine':a.engine,'racers':count,'consoles':a.consoles or count,
                  'portrait_frames':{str(k):v for k,v in portrait_frames.items()},
                  'map_frames':{str(k):v for k,v in map_frames.items()},
                  'map_hashes':{str(k):sorted(v) for k,v in geometry.items()},'errors':errors,'rendered_portrait_frames_checked':rendered_checks}
        (a.results/(name+'-graphics.json')).write_text(json.dumps(report,indent=2)+'\n')
        # Verify the displayed image too: nonempty VRAM alone did not catch
        # the reported black screen. These captures are taken after settling.
        map_captures = [capture for slot in range(8,a.consoles or 8)
                        for capture in a.results.glob(name+f'-results-f*-p{slot}.ppm')]
        if a.consoles:
            assert len(map_captures) == a.races * (a.consoles - 8), 'Missing course-by-course map screenshots'
            for capture in map_captures:
                header, size, maximum, pixels = capture.read_bytes().split(b'\n',3)
                assert header == b'P6' and size == b'240 160' and maximum == b'255'
                colored = sum(any(pixels[i:i+3]) for i in range(0,len(pixels),3))
                assert colored > 1000, (name,'Black rendered map',capture.name,colored)
        assert not errors, (name,'Course-by-course graphics errors',errors[:3])
        assert all(portrait_frames[h,slot] >= 30 for h in expected_headers for slot in range(count)), 'Missing settled standings coverage'
        assert all(portrait_frames[max(expected_headers),slot] >= a.results_frames - 35 for slot in range(count)), 'Final results observation ended early'
        if a.consoles:
            assert all(map_frames[h,slot] >= 300 for h in expected_headers for slot in range(8,a.consoles)), 'Missing map course coverage'
            assert all(len(geometry[h,slot]) == 1 for h in expected_headers for slot in range(8,a.consoles)), 'Course map changed during results'
            assert all(len({next(iter(geometry[h,slot])) for h in expected_headers}) == a.races
                       for slot in range(8,a.consoles)), 'A previous course map was reused'
            assert all(geometry[h,slot] == geometry[h,8] for h in expected_headers
                       for slot in range(8,a.consoles)), 'Map consoles show different course geometry'
        report['status'] = 'passed'
        (a.results/(name+'-graphics.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS {name}: {a.races} five-lap races, common {19*a.races}-point total, {count} humans/{8-count} CPUs', flush=True)
