#!/usr/bin/env python3
"""Compare every ring console's CC tab with unmodified four-pak graphics."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','bios','results'):
    p.add_argument('--'+key,required=True,type=lambda value:Path(value).resolve())
p.add_argument('--players',nargs='+',type=int,choices=range(2,9),default=[2,5,6,7,8])
a=p.parse_args();root=Path(__file__).resolve().parents[2]
a.results.mkdir(parents=True,exist_ok=True)
assert hashlib.sha1(a.bios.read_bytes()).hexdigest()=='300c20df6731a33952ded8c436f7f186d25d3492'
stock=root/'baserom.gba'
assert hashlib.sha1(stock.read_bytes()).hexdigest()=='9d327c030c3e2d9007990518594f70c3340ac56f'

def tab(path):
    magic,size,maximum,pixels=path.read_bytes().split(b'\n',3)
    assert (magic,size,maximum)==(b'P6',b'240 160',b'255')
    # Opaque CC inset, excluding the changing background outside its frame.
    return b''.join(pixels[(y*240)*3:(y*240+24)*3] for y in range(88,104))

def capture(name,players,engine,ring):
    fixture=(root/'tools/ring/inputs/ring-vs.csv') if ring else (root.parent/'mgba/tools/link/inputs/stock-vs.csv')
    rows=[]
    for line in fixture.read_text().splitlines():
        frame,slot,keys=line.split(',');frame,slot=int(frame),int(slot)
        if ring:
            if slot or frame>=1950:continue
            rows.extend((frame,i,int(keys,16)) for i in range(players if frame<1500 else 1))
        elif frame<1800:rows.append((frame,slot,int(keys,16)))
    for step in range(engine):
        frame=(1730 if ring else 1600)+step*35
        rows.extend([(frame,0,128),(frame+15,0,0)])
    inputs=a.results/(name+'.csv')
    inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
    command=[str(a.runner),'--rom',str(a.rom if ring else stock),'--bios',str(a.bios),'--players',str(players),
             '--frames','2400','--input',str(inputs),'--screens',str(a.results/name),
             '--dump','0x0202A400','--dump-words','9']
    if ring:command+=['--ring','--ring-quantum','2048','--barrier-before-input','--barrier-state','0x03006B40']
    result=subprocess.run(command,capture_output=True,text=True,timeout=300)
    (a.results/(name+'.json')).write_text(result.stdout)
    (a.results/(name+'.stderr')).write_text(result.stderr)
    assert result.returncode==0 and not result.stderr,(name,result.returncode,result.stderr)
    data=json.loads(result.stdout)
    for node in data['nodes']:
        assert node['dump'][0]==1 and node['dump'][5]==engine,(name,'Wrong character/class page',node['slot'],node['dump'])
    return [tab(a.results/f'{name}-{slot}.ppm') for slot in range(players)]

references=[]
for engine in range(3):
    native=capture(f'stock-{engine}',4,engine,False)
    assert all(image==native[0] for image in native),'Stock consoles disagree on CC graphics'
    references.append(native[0])
    for players in a.players:
        actual=capture(f'ring-{engine}-{players}',players,engine,True)
        assert all(image==native[0] for image in actual),(engine,players,'CC tab differs from the unmodified ROM')
        print(f'PASS {players}P {50 if engine==0 else 100 if engine==1 else 150}cc: CC pixels match native four-pak on every console',flush=True)
assert len(set(references))==3,'CC inset did not distinguish all three classes'
