#!/usr/bin/env python3
"""Resume a completed Battle round and verify cumulative results for round two.

Requires a matching test_rounds.py result directory. Uses its measured completion
frame to schedule results/arena confirmation; the next round uses the same
keypad controls, and stops only when all consoles report the second win.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
p=argparse.ArgumentParser(description=__doc__)
for name in ('runner','rom','elf','bios','baseline','results'):
    p.add_argument('--'+name,required=True,type=Path)
p.add_argument('--players',nargs='+',type=int,default=[2,8])
p.add_argument('--nm',default='arm-none-eabi-nm')
a=p.parse_args()
assert hashlib.sha1(a.bios.read_bytes()).hexdigest()=='300c20df6731a33952ded8c436f7f186d25d3492'
symbols={}
for line in subprocess.check_output([a.nm,'-n',str(a.elf)],text=True).splitlines():
    f=line.split()
    if len(f)==3:symbols[f[2]]=int(f[0],16)
a.results.mkdir(parents=True,exist_ok=True)
b=a.rom.read_bytes();rom=a.results/(hashlib.sha256(b).hexdigest()+'.gba')
if not rom.exists():rom.write_bytes(b)
for n in a.players:
    first=json.loads((a.baseline/f'battle-{n}.json').read_text())
    assert all(node['game']['sceneMain']==symbols['results_main']|1 for node in first['nodes'])
    # The baseline content-addressed ROM must be this build.
    assert (a.baseline/rom.name).read_bytes()==b,'Baseline is from another ROM'
    frame=max(node['frame'] for node in first['nodes'])+100
    rows=[]
    for line in (a.baseline/f'battle-{n}.csv').read_text().splitlines():
        f,i,k=line.split(',');rows.append((int(f),int(i),int(k,16)))
    rows.extend((frame,i,0) for i in range(n))
    rows.extend([(frame+100,0,1),(frame+115,0,0),(frame+500,0,1),(frame+515,0,0)])
    rows.extend((frame+1700,i,0 if i==0 else 17) for i in range(n))
    name=f'battle-repeat-{n}';inputs=a.results/(name+'.csv')
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
    proc=subprocess.run([str(a.runner),'--ring','--rom',str(rom),'--bios',str(a.bios),
                         '--players',str(n),'--frames',str(min(100000,frame*2+6000)),
                         '--input',str(inputs),'--barrier-before-input', '--barrier-state',hex(symbols['sRing']),
                         '--until-scene',hex(symbols['results_main']|1),
                         '--watch',hex(symbols['sWins']),'--until-watch-value','2',
                         '--dump',hex(symbols['sPlaces']),'--screens',str(a.results/name)],
                        capture_output=True,text=True,timeout=600)
    (a.results/(name+'.json')).write_text(proc.stdout)
    (a.results/(name+'.stderr')).write_text(proc.stderr)
    assert proc.returncode==0 and not proc.stderr,(name,proc.stderr[:1500])
    data=json.loads(proc.stdout);assert data['collisions']==0
    states=[]
    for node in data['nodes']:
        r=node['barrier'];assert r['errors']==r['timeouts']==0 and r['count']==n,(name,r)
        raw=struct.pack('<32I',*node['dump']);places=list(raw[:n])
        wins=list(struct.unpack_from('<8H',raw,symbols['sWins']-symbols['sPlaces']))[:n]
        assert sorted(places)==list(range(n)) and places[0]==0,(name,places)
        assert wins==[2]+[0]*(n-1),(name,wins)
        states.append((r['round'],places,wins,[d['position'] for d in node['game']['drivers'][:n]]))
    assert all(s==states[0] for s in states),(name,states)
    print(f'PASS {name}: two complete rounds, synchronized rankings and cumulative wins',flush=True)
