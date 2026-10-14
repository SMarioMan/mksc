#!/usr/bin/env python3
"""Check native-centered left/right Battle spawn pairs on all four arenas."""
import argparse
import json
import math
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','bios','results'):p.add_argument('--'+key,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
base=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',')
 if int(i)==0 and int(f)<3500:base.append((int(f),int(k,16)))
for arena in range(4):
 grids={}
 for n in (4,8):
  events=[]
  for f,k in base:
   if f in (2200,2215):continue
   events.extend((f,i,k) for i in range(n if f<1500 or f in (2000,2015) else 1))
  events.extend([(1600,0,128),(1615,0,0)])
  for j in range(arena):events.extend([(2300+j*40,0,128),(2320+j*40,0,0)])
  name=f'arena-{arena+1}-{n}';inputs=a.results/(name+'.csv')
  inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(events)))
  proc=subprocess.run([str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(n),
   '--frames','4300','--input',str(inputs),'--dump','0x03002E68','--barrier-before-input','--barrier-state','0x03006B40'],capture_output=True,text=True,timeout=300)
  (a.results/(name+'.json')).write_text(proc.stdout);(a.results/(name+'.stderr')).write_text(proc.stderr)
  assert proc.returncode==0,(name,proc.stderr)
  data=json.loads(proc.stdout)
  assert data['collisions']==0 and all(x['barrier']['errors']==x['barrier']['timeouts']==0 for x in data['nodes']),name
  assert all(x['game']['humanCount']==n and x['game']['raceFlags']&0x800 for x in data['nodes']),name
  grids[n]=data['nodes'][0]['dump']
  assert all(x['dump']==grids[n] for x in data['nodes']),name
 for i in range(4):
  center=grids[4][i*4:i*4+4];left=grids[8][i*4:i*4+4];right=grids[8][(i+4)*4:(i+5)*4]
  def signed(x):return x-2**32 if x>=2**31 else x
  cx,cy,lx,ly,rx,ry=map(signed,(center[0],center[1],left[0],left[1],right[0],right[1]))
  assert lx+rx==2*cx and ly+ry==2*cy,(arena,i,'Pair not centered',center,left,right)
  assert left[2]==right[2]==center[2],(arena,i,'Facing changed')
  angle=(center[2]&65535)*2*math.pi/65536
  dx,dy=rx-cx,ry-cy
  assert dx*math.cos(angle)+dy*math.sin(angle)>0,(arena,i,'New player is not on the right')
  assert abs(dx*math.sin(angle)-dy*math.cos(angle))<4096,(arena,i,'Offset not perpendicular to facing')
  assert math.hypot(rx-lx,ry-ly)>=8*65536,(arena,i,'Spawn pair too close')
 print(f'PASS arena {arena+1}: four centered left/right pairs, native facing, synchronized on all eight consoles',flush=True)
