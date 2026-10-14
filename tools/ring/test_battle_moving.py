#!/usr/bin/env python3
"""Measure moving eight-player Battle on every arena; check HUD and map dots."""
import argparse,csv,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','rom','bios','results'):p.add_argument('--'+k,required=True,type=Path)
p.add_argument('--boot-ring',action='store_true')
p.add_argument('--arenas',nargs='+',type=int,choices=range(4),default=list(range(4)))
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
base=[]
for l in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=l.split(',');f=int(f)
 if int(i)==0 and f<3500 and f not in (2200,2215) and (not a.boot_ring or f>=1500):base.append((f,int(k,16)))
colors=[31,992,1023,31775,31744,543,32736,20500]
character_tiles=[0,1,7,3,5,6,2,4]
for arena in a.arenas:
 events=[]
 for f,k in base:events.extend((f,i,k) for i in range(8 if f<1500 or f in (2000,2015) else 1))
 events.extend([(1600,0,128),(1615,0,0)])
 for step in range(arena):events.extend([(2300+step*40,0,128),(2320+step*40,0,0)])
 events.extend((3500,i,1 if i==0 else 17) for i in range(8))
 name=f'arena-{arena+1}';inputs=a.results/(name+'.csv');video=a.results/(name+'-video.csv')
 inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(events)))
 proc=subprocess.run([str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players','8','--frames','5700','--input',str(inputs),'--video-log',str(video),'--video-from','4500','--barrier-before-input','--barrier-state','0x03006B40','--screens',str(a.results/name)],capture_output=True,text=True,timeout=300)
 (a.results/(name+'.json')).write_text(proc.stdout);(a.results/(name+'.stderr')).write_text(proc.stderr)
 assert proc.returncode==0 and not proc.stderr,(name,proc.stderr)
 result=json.loads(proc.stdout);assert result['collisions']==0
 frames=[{k:int(v) for k,v in x.items()} for x in csv.DictReader(video.open())]
 rates=[]
 for i in range(8):
  peer=[x for x in frames if x['player']==i and x['dispcnt']&7==1]
  assert len(peer)>=1000,(name,i,'Battle finished before the speed window')
  rates.append((peer[-1]['simulationFrames']-peer[0]['simulationFrames'])/(peer[-1]['frame']-peer[0]['frame']))
 assert min(rates)>=.95,(name,'Moving Battle is below 95% speed',rates)
 assert all(not x['invalidObjects'] and not x['balloonForeignPixels'] for x in frames),(name,'Invalid sprites or untinted balloon texels')
 for node in result['nodes']:
  assert node['barrier']['errors']==node['barrier']['timeouts']==0
  racers=node['game']['drivers'];hud=node['game']['hud']
  order=sorted(range(8),key=lambda i:(-racers[i]['balloons'],i))[:4]
  for row,id in enumerate(order):
   tile=0xD080+character_tiles[racers[id]['character']&7]*4
   assert hud['portraits'][row]==list(range(tile,tile+4)),(name,order,hud['portraits'])
  assert hud['identityPalette']==[[c,((c>>1)&0x3DEF)+0x4210] for c in colors],(name,'Course animation overwrote the balloon palette',hud['identityPalette'])
  assert len(hud['minimapDots'])==8
  for id,x,y in hud['minimapDots']:
   rx,ry,_=racers[id]['position'];shift=hud['minimapShift']
   assert abs(x-(175+(rx>>shift)))<=1 and abs(y-(95+(ry>>shift)))<=1,(name,'Misaligned minimap dot',id,x,y,rx,ry,shift)
 (a.results/(name+'-rates.json')).write_text(json.dumps(rates))
 print(f'PASS {name}: moving Battle {min(rates):.3f} updates/video; ranked HUD, eight aligned dots, colored balloons',flush=True)
