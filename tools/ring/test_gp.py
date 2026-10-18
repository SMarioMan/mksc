#!/usr/bin/env python3
"""Complete GP race one and advance with real inputs, checking points and CPUs."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','rom','elf','bios','results'):p.add_argument('--'+k,required=True,type=Path)
p.add_argument('--map-rom',type=Path,help='Append a map display built with matching options')
p.add_argument('--players',nargs='+',type=int,choices=range(2,9),default=list(range(2,9)))
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
sym={f[2]:int(f[0],16) for l in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines() if len(f:=l.split())==3}
assert hashlib.sha1(a.bios.read_bytes()).hexdigest()=='300c20df6731a33952ded8c436f7f186d25d3492'
rom_data=a.rom.read_bytes();a.rom=a.results/(hashlib.sha256(rom_data).hexdigest()+'.gba')
if not a.rom.exists():a.rom.write_bytes(rom_data)
if a.map_rom:
 map_data=a.map_rom.read_bytes();role=sym['ring_defaultMapDisplay']-0x08000000
 assert struct.unpack_from('<I',rom_data,role)[0]==0 and struct.unpack_from('<I',map_data,role)[0]==1
 assert rom_data[:role]+rom_data[role+4:]==map_data[:role]+map_data[role+4:]
 a.map_rom=a.results/(hashlib.sha256(map_data).hexdigest()+'.gba')
 if not a.map_rom.exists():a.map_rom.write_bytes(map_data)
base=[]
for l in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=l.split(',');f=int(f)
 if int(i)==0 and f<3500 and f not in (2500,2515):base.append((f,int(k,16)))
for n in a.players:
 rows=[]
 for f,k in base:
  rows.extend((f,i,k) for i in range(n if f<1500 or f in (2000,2015) else 1))
 rows.extend([(1600,0,64),(1615,0,0)])
 if a.map_rom:rows.extend((f,n,k) for f,i,k in rows.copy() if i==0 and f<1500)
 inputs=a.results/f'gp-{n}.csv';inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
 for advance in (False,True):
  name=f'gp-{n}-'+('next' if advance else 'points')
  cmd=[str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(n),'--frames','50000','--input',str(inputs),'--autodrive-frame','3500','--autofire','--barrier-before-input','--barrier-state','0x03006B40','--dump',hex(sym['sGPPoints']),'--until-scene',hex(sym['race_main' if advance else 'results_main']|1),'--screens',str(a.results/name)]
  if a.map_rom:cmd+=['--ring-map-rom',str(a.map_rom)]
  if advance:cmd+=['--auto-confirm-scene',hex(sym['results_main']|1),'--watch',hex(sym['sCourse']),'--until-watch-value','1']
  r=subprocess.run(cmd,capture_output=True,text=True,timeout=600)
  (a.results/(name+'.json')).write_text(r.stdout);(a.results/(name+'.stderr')).write_text(r.stderr)
  assert r.returncode==0 and not r.stderr,(name,r.stderr)
  d=json.loads(r.stdout);assert d['bios']=='real' and d['collisions']==0
  assert len(d['nodes'])==n+bool(a.map_rom)
  states=[]
  for node in d['nodes']:
   assert node['barrier']['errors']==node['barrier']['timeouts']==0
   assert node['barrier']['count']==n and node['barrier']['id']==(8 if a.map_rom and node['slot']==n else node['slot'])
   points=list(struct.unpack('<8H',struct.pack('<4I',*node['dump'][:4])))
   assert sorted(points,reverse=True)==[9,6,3,1,0,0,0,0],(name,points)
   racers=node['game']['drivers'];assert len(racers)==8 and node['game']['humanCount']==n
   if not advance:assert sum(r['lap']>=4 for r in racers)>=7
   if advance:assert node['watch']==1
   states.append((points,[r['position'] for r in racers]))
  assert all(s==states[0] for s in states),(name,'GP CPU/human desynchronization')
  print(f'PASS {name}: eight racers, {n} humans/{8-n} CPUs, common 9/6/3/1 points'+('; next cup course loaded' if advance else ''),flush=True)
