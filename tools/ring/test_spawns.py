#!/usr/bin/env python3
"""Check authored eight-marker Battle grids and inward facing at 2–8 peers."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'rom', 'elf', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, choices=range(2,9), default=list(range(2,9)))
p.add_argument('--sets', nargs='+', choices=('super','retro'), default=['super','retro'])
a = p.parse_args()
a.results.mkdir(parents=True, exist_ok=True)
symbols = {f[2]:int(f[0],16) for line in subprocess.check_output(
    ['arm-none-eabi-nm','-n',str(a.elf)], text=True).splitlines() if len(f:=line.split())==3}
rom = a.rom.read_bytes()
def word(address):
    return struct.unpack_from('<I', rom, address - 0x08000000)[0]
base = []
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
    f,i,k = line.split(',')
    if int(i)==0 and int(f)<3500:
        base.append((int(f),int(k,16)))
for group in a.sets:
    for arena in range(4):
        header = 52+arena if group=='retro' else word(symbols['gBattleCup']+arena*4)
        definition = word(symbols['gTrackDefTable']+header*4)
        data = symbols['courseData_bin'] + word(symbols['courseData_bin']+word(definition)*4)
        marker = data + word(data+0x98)
        authored = {}
        while rom[marker-0x08000000]:
            kind,x,y,_ = rom[marker-0x08000000:marker-0x08000000+4]
            if 0x81<=kind<=0x88:
                authored[kind-0x81] = ((x*8+(0 if header&32 else 4))*65536, (y*8+4)*65536)
            marker += 4
        assert set(authored)==set(range(8)),(group,arena,'Missing authored marker')
        center = tuple(sum(v[axis] for v in authored.values())/8 for axis in (0,1))
        assert len(set(authored.values()))==8,(group,arena,'Overlapping authored markers')
        for n in a.players:
            events = []
            for f,k in base:
                if f in (2500,2515):continue
                events.extend((f,i,k) for i in range(n if f<1500 or f in (2000,2015) else 1))
            events += [(1600,0,128),(1615,0,0)]
            if group=='retro':
                events += [(2850,0,256),(2865,0,0)]
            for j in range(arena):
                events += [(2600+j*40,0,128),(2620+j*40,0,0)]
            name = f'{group}-{arena+1}-{n}'
            inputs = a.results/(name+'.csv')
            inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(events)))
            proc = subprocess.run([str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),
                '--ring-quantum','2048','--players',str(n),'--frames','4600','--input',str(inputs),
                '--dump','0x03002E68','--keys-address',hex(symbols['ring_battleSpawnTerrain']),
                '--key-slots','8','--barrier-before-input','--barrier-state','0x03006B40',
                '--screens',str(a.results/name)],capture_output=True,text=True,timeout=300)
            (a.results/(name+'.json')).write_text(proc.stdout)
            (a.results/(name+'.stderr')).write_text(proc.stderr)
            assert proc.returncode==0 and not proc.stderr,(name,proc.stderr)
            data = json.loads(proc.stdout)
            assert data['bios']=='real' and data['collisions']==0,name
            grid = data['nodes'][0]['dump']
            for node in data['nodes']:
                assert node['barrier']['errors']==node['barrier']['timeouts']==0,name
                assert node['game']['humanCount']==n and node['game']['raceFlags']&0x800,name
                assert node['game']['headerIndex']==header,(name,'Wrong arena set',node['game']['headerIndex'],header)
                assert node['dump']==grid,name
                assert all(not (surface&0x80) for surface in node['menu']['heldKeys']),(name,'Spawn in a void',node['menu']['heldKeys'])
            for slot in range(8):
                x,y,yaw,_ = grid[slot*4:slot*4+4]
                assert (x,y)==authored[slot],(name,slot,'Authored marker moved',(x,y),authored[slot])
                angle = yaw*2*math.pi/65536
                dx,dy = center[0]-x,center[1]-y
                assert dx*math.sin(angle)-dy*math.cos(angle)>0,(name,slot,'Facing away from arena')
                assert abs(dx*math.cos(angle)+dy*math.sin(angle))/math.hypot(dx,dy)<.003,(name,slot,'Incorrect heading')
            print(f'PASS {name}: eight authored markers, valid terrain, inward headings, synchronized',flush=True)
