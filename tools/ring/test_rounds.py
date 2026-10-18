#!/usr/bin/env python3
"""Complete real-input ring rounds and verify all consoles' rankings and scores.

Vs. follows native route coordinates using only the keypad and periodically
uses items. Battle drives all but player one around Shy Guy Desert's pits.
Snapshots stop at the same committed-input barrier in the results scene.
"""
import argparse
import csv
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import struct
import subprocess
p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--nm', default='arm-none-eabi-nm')
p.add_argument('--map-rom', type=Path, help='Append a dedicated display built with the same options')
p.add_argument('--players', nargs='+', type=int, default=list(range(2,9)))
p.add_argument('--modes', nargs='+', choices=['vs','battle'], default=['vs','battle'])
p.add_argument('--engine', type=int, choices=range(3), default=0, help='Vs. engine class: 0=50cc, 1=100cc, 2=150cc')
p.add_argument('--workers', type=int, choices=range(1,5), default=3)
p.add_argument('--toad-150cc-retro', action='store_true', help='Reproduce host Toad finishing SNES Mario Circuit 1 at 150cc')
p.add_argument('--hle', action='store_true', help='Exercise optional HLE; real BIOS is the default')
p.add_argument('--shared-audio', action='store_true', help='Require a host-only common result cue after the round ends')
p.add_argument('--results-frames', type=int, default=30, help='Observe the native results background; >=600 also checks sustained motion')
a = p.parse_args()
if not 30 <= a.results_frames <= 10000:p.error('Results observation must be 30..10000 frames')
if any(n < 2 or n > 8 for n in a.players):
    p.error('Player counts must be 2 through 8')
if hashlib.sha1(a.bios.read_bytes()).hexdigest() != '300c20df6731a33952ded8c436f7f186d25d3492':
    p.error('Use the supplied real GBA BIOS')
symbols = {}
for line in subprocess.check_output([a.nm,'-n',str(a.elf)], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0],16)
a.results.mkdir(parents=True, exist_ok=True)
dumpBase=min(symbols['sPlaces'],symbols['sRoundScores'])
dumpWords=(max(symbols['sPlaces']+8,symbols['sRoundScores']+16)-dumpBase+3)//4
assert dumpWords<=4096
rom_data = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(rom_data).hexdigest()+'.gba')
if not rom.exists():
    rom.write_bytes(rom_data)
map_rom = None
if a.map_rom:
    map_data = a.map_rom.read_bytes()
    role = symbols['ring_defaultMapDisplay'] - 0x08000000
    assert struct.unpack_from('<I',rom_data,role)[0] == 0 and struct.unpack_from('<I',map_data,role)[0] == 1
    assert rom_data[:role] + rom_data[role+4:] == map_data[:role] + map_data[role+4:]
    map_rom = a.results/(hashlib.sha256(map_data).hexdigest()+'.gba')
    if not map_rom.exists():map_rom.write_bytes(map_data)
base = []
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
    f,i,k = line.split(',')
    if int(i) == 0 and int(f) < 3500:
        base.append((int(f),int(k,16)))
def run(n,mode):
    name = f'{mode}-{n}'
    rows = []
    for f,k in base:
        if mode=='battle' and f in (2500,2515):continue
        rows.extend((f,i,k) for i in range(n if f < 1500 or f in (2000,2015) else 1))
    if mode == 'battle':
        rows.extend([(1600,0,128),(1615,0,0)])
        for step in range(3):
            rows.extend([(2600+step*40,0,128),(2620+step*40,0,0)])
        rows.extend((3500,i,0 if i == 0 else 17) for i in range(n))
    elif not a.toad_150cc_retro:
        for step in range(a.engine):
            frame = 1720 + step * 35
            rows.extend([(frame,0,128),(frame+15,0,0)])
    if a.toad_150cc_retro:
        assert mode == 'vs' and n == 8, 'Finish regression uses eight-player Vs.'
        rows = [row for row in rows if row[0] < 1500]
        rows.extend([(1500,0,8),(1515,0,0),(1650,0,1),(1665,0,0),
                     (1850,0,128),(1865,0,0),(1890,0,128),(1905,0,0),
                     (1950,0,1),(1965,0,0),(2150,0,128),(2165,0,0),
                     (2150,3,64),(2165,3,0)])
        rows.extend((2300,i,1) for i in range(n))
        rows.extend((2315,i,0) for i in range(n))
        rows.extend((f,0,k) for f,k in ((2500,1),(2515,0),(2800,256),(2815,0),(3200,1),(3215,0),
                    (3500,1),(3515,0),(3800,1),(3815,0),(4100,1),(4115,0)))
    if map_rom:rows.extend((f,n,k) for f,i,k in rows.copy() if i==0 and f<1500)
    inputs = a.results/(name+'.csv')
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
    command = [str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(rom),'--bios',str(a.bios),
               '--players',str(n),'--frames',str(50000 if mode == 'vs' else 30000),
               '--input',str(inputs),'--dump',hex(dumpBase),'--dump-words',str(dumpWords),
               '--barrier-before-input', '--barrier-state',hex(symbols['sRing']),
               '--until-scene',hex(symbols['results_main'] | 1),
               '--settle-frames',str(a.results_frames),
               '--release-inputs-scene',hex(symbols['results_main'] | 1),
               '--screens',str(a.results/name)]
    if map_rom:command += ['--ring-map-rom',str(map_rom)]
    if a.results_frames >= 600:
        command += ['--video-log',str(a.results/(name+'-video.csv')),'--sound-table',hex(symbols['gMPlayTable']),
                    '--audio-state',hex(symbols['ring_audioTiming'])]
    if a.shared_audio:
        mixer=struct.unpack_from('<I',rom_data,symbols['gMPlayTable']-0x08000000)[0]
        command += ['--keys-address',hex(mixer),'--key-slots','4']
    if mode == 'vs':
        command += ['--record-inputs',str(a.results/(name+'-recorded.csv'))]
        command += ['--autodrive-frame','5500' if a.toad_150cc_retro else '3500','--autofire']
    if a.hle:
        bios_index = command.index('--bios')
        del command[bios_index:bios_index + 2]
        command += ['--hle']
    proc = subprocess.run(command,capture_output=True,text=True,timeout=600)
    (a.results/(name+'.json')).write_text(proc.stdout)
    (a.results/(name+'.stderr')).write_text(proc.stderr)
    assert proc.returncode == 0 and not proc.stderr, (name,proc.stderr[:2000])
    result = json.loads(proc.stdout)
    assert len(result['nodes']) == n + bool(map_rom)
    assert result['bios'] == ('hle' if a.hle else 'real'), name
    assert result['collisions'] == 0, name
    if a.results_frames >= 600:
        with (a.results/(name+'-video.csv')).open() as stream:
            observed=[{k:int(v) for k,v in row.items()} for row in csv.DictReader(stream)]
        for player in range(n + bool(map_rom)):
            frames=[r for r in observed if r['player']==player and r['sceneMain']==symbols['results_main']|1]
            assert len(frames)>=a.results_frames-5,(name,player,'Results exited prematurely',len(frames))
            assert all(r['dispcnt']&7==1 and not r['dispcnt']&128 for r in frames[10:]),(name,player,'Results lost the native course display')
            assert all(r['engineHeader']==0 for r in frames[10:]),(name,player,'Engine audio restarted on the scoreboard')
            for channel in (1,3):
                assert frames[-1][f'audioStarts{channel}']==frames[10][f'audioStarts{channel}'],(name,player,'Results restarted a local or remote engine',channel)
            assert frames[-1]['audioLateSegments']==frames[10]['audioLateSegments'],(name,player,'Scoreboard PCM missed its deadline')
            assert frames[-1]['audioMissedFrames']-frames[10]['audioMissedFrames']==frames[-1]['audioRecoveredFrames']-frames[10]['audioRecoveredFrames'],(name,player,'Unfilled scoreboard PCM segment')
            if mode=='vs':
                assert len({r['kartPosition0'] for r in frames[30:]})>10,(name,player,'Results racer stopped moving')
            if map_rom and player==n:
                assert all(r['dispcnt']==0x1441 for r in frames[10:]),(name,'Map left the full-course display')
                mapping=[r for r in observed if r['player']==player and r['frame']>=4500 and r['sceneMain'] in (symbols['race_main']|1,symbols['results_main']|1)]
                assert mapping and all(r['mapOverviewHash'] for r in mapping)
                assert len({r['mapOverviewHash'] for r in mapping})==1,(name,'Course geometry/palette changed at results')
            elif not a.shared_audio or player==0:
                cue=struct.unpack_from('<I',rom_data,symbols['gSongTable']-0x08000000+17*8)[0]
                assert all(r['musicHeader']==cue and r['musicStatus']&65535 and not r['musicStatus']&0x80000000 for r in frames[10:]),(name,player,'Results theme stopped')
    states = []
    rounds = []
    for node in result['nodes']:
        if mode == 'vs' and not a.toad_150cc_retro:
            assert node['game']['engineClass']==a.engine,(name,'Wrong engine class',node['game']['engineClass'],a.engine)
        if a.toad_150cc_retro:
            assert node['game']['engineClass']==2,('Finish fixture did not select 150cc',node['game']['engineClass'])
            first=struct.unpack_from('<I',rom_data,symbols['gRetroCups']-0x08000000)[0]
            assert node['game']['headerIndex']==first,('Finish fixture did not select Retro Mario Circuit 1',node['game']['headerIndex'],first)
            assert node['game']['drivers'][0]['character']==6, 'Host must be Toad'
            assert node['game']['drivers'][0]['lap']>=4,'Host did not cross the finish line'
        assert node['game']['sceneMain'] == symbols['results_main'] | 1, (name,node['game'])
        barrier = node['barrier']
        assert barrier['errors'] == barrier['timeouts'] == 0, (name,barrier)
        assert barrier['count'] == n and barrier['id'] == (8 if map_rom and node['slot']==n else node['slot']), (name,barrier)
        if a.shared_audio:
            audio=node['menu']['heldKeys']
            header=audio[0] | audio[1]<<16
            status=audio[2] | audio[3]<<16
            if node['slot']==0:
                cue=struct.unpack_from('<I',rom_data,symbols['gSongTable']-0x08000000+17*8)[0]
                assert header==cue and status&65535 and not status&0x80000000,(name,'Common result cue missing',header,status)
            else:assert not status&65535 or status&0x80000000,(name,'Peer result music is audible',node['slot'])
        rounds.append(barrier['round'])
        raw = struct.pack('<'+'I'*len(node['dump']),*node['dump'])
        places = list(raw[symbols['sPlaces']-dumpBase:symbols['sPlaces']-dumpBase+n])
        scores = list(struct.unpack_from('<8H',raw,symbols['sRoundScores']-dumpBase))[:n]
        assert sorted(places) == list(range(n)), (name,places)
        awards = [10,8,6,4,3,2,1,0][:n]
        assert [scores[player] for player in places] == awards, (name,places,scores,awards)
        if mode == 'battle':
            arena=struct.unpack_from('<I',rom_data,symbols['gBattleCup']-0x08000000+12)[0]
            assert node['game']['headerIndex']==arena,(name,'Wrong Battle completion arena')
            assert places[0] == 0, (name,places)
            assert [r['balloons'] for r in node['game']['drivers'][:n]] == [3]+[0]*(n-1), (name,node['game'])
        else:
            assert sum(r['lap'] >= 4 for r in node['game']['drivers'][:n]) >= n-1, (name,node['game'])
        states.append((places,scores,[r['position'] for r in node['game']['drivers'][:n]]))
    assert len(set(rounds)) == 1 and all(s == states[0] for s in states), (name,rounds,states)
    return f'PASS {name}: complete round, {n} ranked players, matching positions and 10/8/6/4/3/2/1/0 link awards'
with ThreadPoolExecutor(max_workers=a.workers) as pool:
    futures = [pool.submit(run,n,mode) for n in a.players for mode in a.modes]
    failures = []
    for future in as_completed(futures):
        try:
            print(future.result(),flush=True)
        except Exception as error:
            failures.append(str(error))
            print(f'FAIL: {error}',flush=True)
    if failures:
        raise SystemExit('Round regressions failed: ' + '; '.join(failures))
