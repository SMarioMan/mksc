#!/usr/bin/env python3
"""Exercise native character confirmation and duplicate locks with 2–8 peers."""
import argparse
from collections import deque
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):
    p.add_argument('--'+key, required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, choices=range(2,9), default=list(range(2,9)))
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
symbols = {f[2]:int(f[0],16) for line in subprocess.check_output(
    ['arm-none-eabi-nm','-n',str(a.elf)], text=True).splitlines() if len(f:=line.split())==3}
rom = a.rom.read_bytes()
nav = [struct.unpack_from('<hhbbbb',rom,symbols['_080D9274']-0x08000000+i*8)[2:] for i in range(9)]

def route(start, finish):
    queue = deque([(start,[])])
    seen = {start}
    while queue:
        cell, keys = queue.popleft()
        if cell == finish:
            return keys
        for direction, target in zip((64,128,32,16),nav[cell]):
            if target >= 0 and target not in seen:
                seen.add(target)
                queue.append((target,keys+[direction]))
    raise AssertionError('Disconnected native character navigation')

base = []
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
    frame, player, keys = line.split(',')
    if int(player)==0 and int(frame)<1950:
        base.append((int(frame),int(keys,16)))
for count in a.players:
    rows = [(f,i,k) for f,k in base for i in range(count if f<1500 else 1)]
    rows += [(2000,i,1) for i in range(count-1)] + [(2015,i,0) for i in range(count-1)]
    initial = count-1+(count-1>=4)
    for start, path in ((2100,route(initial,0)),(2600,route(0,initial))):
        for step, key in enumerate(path):
            rows += [(start+step*40,count-1,key),(start+step*40+15,count-1,0)]
    rows += [(2450,count-1,1),(2465,count-1,0),(3000,count-1,1),(3015,count-1,0)]
    inputs = a.results/f'characters-{count}.csv'
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
    for phase, frame, page, ready in [('locked',2500,1,(1<<(count-1))-1),('confirmed',3450,2,(1<<count)-1)]:
        name=f'{count}-{phase}'
        command=[str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(count),
                 '--frames',str(frame),'--input',str(inputs),'--watch',hex(symbols['sNativeCharactersReady']),
                 '--dump','0x0202A400','--dump-words','32','--barrier-before-input','--barrier-state','0x03006B40',
                 '--screens',str(a.results/name)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=300)
        (a.results/(name+'.json')).write_text(result.stdout)
        (a.results/(name+'.stderr')).write_text(result.stderr)
        assert result.returncode==0 and not result.stderr,(name,result.stderr)
        data=json.loads(result.stdout)
        assert data['bios']=='real' and data['collisions']==0,name
        for node in data['nodes']:
            assert node['dump'][0]==page,(name,'Wrong native page',node['dump'][0])
            assert node['watch']&255==ready,(name,'Incorrect confirmation or duplicate lock',node['watch'])
            assert node['barrier']['errors']==node['barrier']['timeouts']==0,name
        print(f'PASS {name}: native page {page}, common ready mask {ready:02x}',flush=True)
