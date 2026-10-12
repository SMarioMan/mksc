#!/usr/bin/env python3
"""Validate BGM policy, including a moving race through the leader's final lap."""
import argparse,csv,json,subprocess,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--policy',choices=['off','shared'],required=True)
p.add_argument('--boot-ring',action='store_true')
p.add_argument('--host',type=int,choices=range(8),default=0)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
symbols={}
for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines():
 f=line.split()
 if len(f)==3:symbols[f[2]]=int(f[0],16)
assert symbols['sRing']==0x03006B40,'Protocol diagnostic ABI moved'
base=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',');f=int(f)
 if int(i)==0 and f<3500 and (not a.boot_ring or f>=1500):base.append((f,int(k,16)))
rows=[]
for f,k in base:
 rows.extend((f,i if f<1500 or f in (2000,2015) else a.host,k) for i in range(8 if f<1500 or f in (2000,2015) else 1))
inputs=a.results/'inputs.csv';inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
video=a.results/'video.csv'
command=[str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players','8',
 '--frames',str(50000 if a.policy=='shared' else 3900),'--input',str(inputs),
 '--video-from','3300','--video-log',str(video),'--barrier-before-input','--barrier-state','0x03006B40']
if a.policy=='shared':command+=['--until-scene',hex(symbols['results_main']|1),'--autodrive-frame','3500','--autofire']
proc=subprocess.run(command,capture_output=True,text=True,timeout=600)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0,(proc.returncode,proc.stderr)
result=json.loads(proc.stdout)
assert result['collisions']==0
assert all(n['barrier']['errors']==n['barrier']['timeouts']==0 for n in result['nodes'])
frames=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(video.open())]
if a.policy=='off':
 assert all(r['musicHeader'] in (0,struct.unpack_from('<I',a.rom.read_bytes(),symbols['gSongTable']-0x08000000+8)[0]) for r in frames),'Music player started despite --no-music'
 print('PASS no-music: player zero remains stopped on all eight consoles; effects use other players',flush=True)
else:
 rom=a.rom.read_bytes();table=symbols['gSongTable']-0x08000000
 course=struct.unpack_from('<I',rom,table+25*8)[0];jingle=struct.unpack_from('<I',rom,table+19*8)[0];winner=struct.unpack_from('<I',rom,table+5*8)[0]
 game=[r for r in frames if r['dispcnt']&7==1 and r['simulationFrames']>=80 and r['sceneMain']==symbols['race_main']|1]
 peers=[r for r in game if r['player']!=a.host]
 assert peers and all(not r['musicStatus']&65535 or r['musicStatus']&0x80000000 for r in peers),'Peer BGM player is audible'
 owner=[r for r in game if r['player']==a.host]
 assert owner and all(r['musicHeader'] in (course,jingle) or (r['musicHeader']==winner and r['leadLap']>=4) for r in owner),'Individual power-up or wrong course BGM played'
 assert all(r['musicTempo'] in (256,320) for r in owner)
 assert any(r['musicHeader']==jingle for r in game),'Shared final-lap jingle was never played'
 firstFinal=min(r['frame'] for r in game if r['leadLap']>=3)
 for i in (a.host,):
  final=[r for r in game if r['player']==i and r['leadLap']>=3 and r['frame']>firstFinal+1 and r['musicHeader']==course]
  assert len(final)>100 and all(r['musicTempo']==320 for r in final),'Leader final-lap tempo not applied'
 rates=[]
 for i in range(8):
  peer=[r for r in game if r['player']==i]
  rates.append((peer[-1]['simulationFrames']-peer[0]['simulationFrames'])/(peer[-1]['frame']-peer[0]['frame']))
 (a.results/'rates.json').write_text(json.dumps(rates,indent=2))
 assert min(rates)>=.95,('Full moving race runs below 95% speed',rates)
 print(f'PASS shared-music: course/final-lap BGM only, host-only BGM and leader tempo; complete moving eight-player race at {min(rates):.3f} updates/video frame',flush=True)
