#!/usr/bin/env python3
"""Complete real-input ring rounds and verify all consoles' rankings and wins.

Vs. follows native route coordinates using only the keypad and periodically
uses items. Battle drives all but player one around arena four until eliminated.
Snapshots stop at the same committed-input barrier in the results scene.
"""
import argparse
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
p.add_argument('--players', nargs='+', type=int, default=list(range(2,9)))
p.add_argument('--modes', nargs='+', choices=['vs','battle'], default=['vs','battle'])
p.add_argument('--workers', type=int, choices=range(1,5), default=3)
a = p.parse_args()
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
rom_data = a.rom.read_bytes()
rom = a.results / (hashlib.sha256(rom_data).hexdigest()+'.gba')
if not rom.exists():
    rom.write_bytes(rom_data)
base = []
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
    f,i,k = line.split(',')
    if int(i) == 0 and int(f) < 3500:
        base.append((int(f),int(k,16)))
def run(n,mode):
    name = f'{mode}-{n}'
    rows = []
    for f,k in base:
        if mode == 'battle' and f in (2350,2365):
            continue
        rows.extend((f,i,k) for i in range(n if f < 1500 or f in (2000,2015) else 1))
    if mode == 'battle':
        rows.extend([(1600,0,128),(1615,0,0)])
        for step in range(3):
            rows.extend([(2070+step*40,0,128),(2090+step*40,0,0)])
        rows.extend((3500,i,0 if i == 0 else 17) for i in range(n))
    inputs = a.results/(name+'.csv')
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
    command = [str(a.runner),'--ring','--rom',str(rom),'--bios',str(a.bios),
               '--players',str(n),'--frames',str(50000 if mode == 'vs' else 30000),
               '--input',str(inputs),'--dump',hex(symbols['sPlaces']),
               '--barrier-state',hex(symbols['sRing']),
               '--until-scene',hex(symbols['results_main'] | 1),
               '--screens',str(a.results/name)]
    if mode == 'vs':
        command += ['--autodrive-frame','3500','--autofire']
    proc = subprocess.run(command,capture_output=True,text=True,timeout=600)
    (a.results/(name+'.json')).write_text(proc.stdout)
    (a.results/(name+'.stderr')).write_text(proc.stderr)
    assert proc.returncode == 0 and not proc.stderr, (name,proc.stderr[:2000])
    result = json.loads(proc.stdout)
    assert result['collisions'] == 0, name
    states = []
    rounds = []
    for node in result['nodes']:
        assert node['game']['sceneMain'] == symbols['results_main'] | 1, (name,node['game'])
        barrier = node['barrier']
        assert barrier['errors'] == barrier['timeouts'] == 0, (name,barrier)
        assert barrier['count'] == n and barrier['id'] == node['slot'], (name,barrier)
        rounds.append(barrier['round'])
        raw = struct.pack('<32I',*node['dump'])
        places = list(raw[:n])
        wins = list(struct.unpack_from('<8H',raw,symbols['sWins']-symbols['sPlaces']))[:n]
        assert sorted(places) == list(range(n)), (name,places)
        assert sum(wins) == 1 and wins[places[0]] == 1, (name,places,wins)
        if mode == 'battle':
            assert places[0] == 0, (name,places)
            assert [r['balloons'] for r in node['game']['drivers'][:n]] == [3]+[0]*(n-1), (name,node['game'])
        else:
            assert sum(r['lap'] >= 4 for r in node['game']['drivers'][:n]) >= n-1, (name,node['game'])
        states.append((places,wins,[r['position'] for r in node['game']['drivers'][:n]]))
    assert len(set(rounds)) == 1 and all(s == states[0] for s in states), (name,rounds,states)
    return f'PASS {name}: complete round, {n} ranked players, matching positions and win counts'
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
