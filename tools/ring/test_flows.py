#!/usr/bin/env python3
"""Exercise ring cancellation, wire loss/reconnection, and native pause replay.

Every action uses the keypad or emulator wire switch; no game RAM is written.
Each flow stops all consoles before physics at one committed-input barrier.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--'+name, required=True, type=Path)
p.add_argument('--nm', default='arm-none-eabi-nm')
p.add_argument('--players', nargs='+', type=int, default=[2,8])
p.add_argument('--cases', nargs='+', choices=['loss','pause','restart','cancel','characters'], default=['loss','pause','restart','cancel','characters'])
a=p.parse_args()
assert all(2<=n<=8 for n in a.players)
assert hashlib.sha1(a.bios.read_bytes()).hexdigest()=='300c20df6731a33952ded8c436f7f186d25d3492'
symbols={}
for line in subprocess.check_output([a.nm,'-n',str(a.elf)],text=True).splitlines():
    f=line.split()
    if len(f)==3:symbols[f[2]]=int(f[0],16)
a.results.mkdir(parents=True,exist_ok=True)
b=a.rom.read_bytes();rom=a.results/(hashlib.sha256(b).hexdigest()+'.gba')
if not rom.exists():rom.write_bytes(b)
base=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
    f,i,k=line.split(',')
    if int(i)==0 and int(f)<3500:base.append((int(f),int(k,16)))
for n in a.players:
    for case in a.cases:
        rows=[]
        for f,k in base:
            if case=='cancel' and f>=1500:continue
            rows.extend((f,i,k) for i in range(n if f<1500 or f in (2000,2015) else 1))
        extra=[];frames=6400
        if case=='characters':
            frames=5200
            rows.extend([(3800,0,8),(3815,0,0),(4200,0,128),(4215,0,0),
                         (4240,0,128),(4255,0,0),(4300,0,1),(4315,0,0)])
        elif case in ('pause','restart'):
            rows.extend([(3800,0,8),(3815,0,0),(4200,0,128),(4215,0,0),
                         (4300,0,1),(4315,0,0),(4600,0,1),(4615,0,0)])
            if case=='restart':rows=[row for row in rows if row[0] not in (4200,4215,4600,4615)]
        elif case=='cancel':
            rows.extend((1500,i,2) for i in range(n));rows.extend((1515,i,0) for i in range(n))
            # Enter Ring Link again after all local menus return to title.
            for f,k in [(2000,8),(2015,0),(2200,128),(2215,0),(2300,1),(2315,0),
                        (2450,128),(2465,0),(2530,128),(2545,0),(2650,1),(2665,0)]:
                rows.extend((f,i,k) for i in range(n))
            for f,k in [(3000,8),(3015,0),(3200,1),(3215,0),(3400,1),(3415,0),
                        (3600,1),(3615,0),(3800,1),(3815,0),(4000,1),(4015,0)]:
                rows.extend((f,i,k) for i in range(n if f in (3600,3615) else 1))
        else:
            extra=['--break-frame','3600','--restore-frame','3900'];frames=8000
            rows.extend((3920,i,0) for i in range(n))
            for f,k in [(4100,1),(4115,0),(4700,8),(4715,0),(4820,128),(4835,0),
                        (4900,1),(4915,0),(5000,128),(5015,0),(5080,128),(5095,0),
                        (5160,1),(5175,0)]:rows.extend((f,i,k) for i in range(n))
            for f,k in [(5500,8),(5515,0),(5650,1),(5665,0),(5800,1),(5815,0),
                        (6000,1),(6015,0),(6200,1),(6215,0),(6350,1),(6365,0)]:
                rows.extend((f,i,k) for i in range(n if f in (6000,6015) else 1))
        if case!='characters':rows.extend((frames-100,i,1) for i in range(n))
        name=f'{case}-{n}';inputs=a.results/(name+'.csv')
        inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
        command=[str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(rom),'--bios',str(a.bios),
                 '--players',str(n),'--frames',str(frames),'--input',str(inputs),
                 '--barrier-before-input', '--barrier-state',hex(symbols['sRing']),'--dump',hex(symbols['sRing']),
                 '--watch','0x0202A400','--keys-address','0x03006B00','--key-slots','8','--screens',str(a.results/name)]+extra
        proc=subprocess.run(command,capture_output=True,text=True,timeout=300)
        (a.results/(name+'.json')).write_text(proc.stdout)
        (a.results/(name+'.stderr')).write_text(proc.stderr)
        assert proc.returncode==0 and not proc.stderr,(name,proc.stderr[:1000])
        data=json.loads(proc.stdout);assert data['collisions']==0,name
        states=[]
        for node in data['nodes']:
            r=node['barrier'];g=node['game']
            assert r['errors']==r['timeouts']==0,(name,r)
            assert r['id']==node['slot'] and r['count']==n,(name,r)
            if case=='characters':
                assert g['sceneMain']==symbols['ring_main']|1 and node['watch']==1,(name,'Pause did not return to native character select',g,node['watch'])
                continue
            assert g['sceneMain']==symbols['race_main']|1 and g['humanCount']==n,(name,g)
            assert node['menu']['heldKeys']==[1]*n+[0]*(8-n),(name,node['menu'])
            states.append((r['round'],[v['position'] for v in g['drivers'][:n]]))
        assert all(s==states[0] for s in states),(name,states)
        print(f'PASS {name}: '+('native character page restored' if case=='characters' else f'fresh synchronized race with {n} humans'),flush=True)
