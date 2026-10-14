#!/usr/bin/env python3
"""Validate BGM policy, including a moving race through the leader's final lap."""
import argparse,csv,json,subprocess,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--policy',choices=['off','shared'],required=True)
p.add_argument('--boot-ring',action='store_true')
p.add_argument('--host',type=int,choices=range(8),default=0)
p.add_argument('--players',type=int,choices=range(2,9),default=8)
p.add_argument('--gp',action='store_true')
p.add_argument('--retro',action='store_true')
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
if a.host>=a.players:p.error('The host must exist in the session')
symbols={}
for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines():
 f=line.split()
 if len(f)==3:symbols[f[2]]=int(f[0],16)
assert symbols['sRing']==0x03006B40,'Protocol diagnostic ABI moved'
base=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',');f=int(f)
 if int(i)==0 and f<3500 and (not a.boot_ring or f>=1500) and (not a.gp or f not in (2500,2515)):base.append((f,int(k,16)))
rows=[]
for f,k in base:
 rows.extend((f,i if f<1500 or f in (2000,2015) else a.host,k) for i in range(a.players if f<1500 or f in (2000,2015) else 1))
if a.gp:rows.extend([(1600,a.host,64),(1615,a.host,0)])
if a.retro:
 rows=[r for r in rows if r[0]<2200]
 rows.extend((f,a.host,k) for f,k in ((2800,4),(2815,0),(3200,1),(3215,0),(3500,1),(3515,0)))
 if not a.gp:rows.extend((f,a.host,k) for f,k in ((4000,1),(4015,0),(4300,1),(4315,0)))
inputs=a.results/'inputs.csv';inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
video=a.results/'video.csv'
command=[str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(a.players),
 '--frames',str(70000 if a.policy=='shared' else 5000),'--input',str(inputs),
 '--video-from','3300','--video-log',str(video),'--barrier-before-input','--barrier-state','0x03006B40']
if a.policy=='shared':command+=['--until-scene',hex(symbols['results_main']|1),'--autodrive-frame','4500','--autofire']
proc=subprocess.run(command,capture_output=True,text=True,timeout=1200)
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
 cups=symbols['gRetroCups' if a.retro else 'gMkscCups']-0x08000000
 header=struct.unpack_from('<I',rom,cups)[0]
 track=struct.unpack_from('<I',rom,symbols['gTrackDefTable']-0x08000000+header*4)[0]-0x08000000
 song=struct.unpack_from('<I',rom,track+24)[0]
 if song==36:song=28
 course=struct.unpack_from('<I',rom,table+song*8)[0];jingle=struct.unpack_from('<I',rom,table+19*8)[0];winner=struct.unpack_from('<I',rom,table+5*8)[0]
 finalLap=5 if a.gp and a.retro else 3
 game=[r for r in frames if r['dispcnt']&7==1 and r['simulationFrames']>=80 and r['sceneMain']==symbols['race_main']|1]
 peers=[r for r in game if r['player']!=a.host]
 assert peers and all(not r['musicStatus']&65535 or r['musicStatus']&0x80000000 for r in peers),'Peer BGM player is audible'
 owner=[r for r in game if r['player']==a.host]
 # A stopped player can retain a different song header. Physics also starts
 # while the finite native opening fanfare is still audible; this must finish
 # before the shared course track replaces it.
 opening={struct.unpack_from('<I',rom,table+i*8)[0] for i in (3,4)}
 audible=[r for r in owner if r['musicStatus']&65535 and not r['musicStatus']&0x80000000]
 assert audible and all(r['musicHeader'] in (course,jingle) or
  (r['musicHeader'] in opening and r['leadLap']==0) or
  (r['musicHeader']==winner and r['leadLap']>finalLap) for r in audible),'Individual power-up or wrong course BGM played'
 assert all(r['musicTempo'] in (256,320) for r in owner)
 assert all(r['musicHeader']!=jingle and r['musicTempo']==256 for r in owner if r['leadLap']<finalLap),'Music accelerated before the native final lap'
 assert any(r['musicHeader']==jingle for r in game),'Shared final-lap jingle was never played'
 firstFinal=min(r['frame'] for r in game if r['leadLap']>=finalLap)
 for i in (a.host,):
  final=[r for r in game if r['player']==i and r['leadLap']>=finalLap and r['frame']>firstFinal+1 and r['musicHeader']==course]
  assert len(final)>100 and all(r['musicTempo']==320 for r in final),'Leader final-lap tempo not applied'
 rates=[]
 for i in range(a.players):
  peer=[r for r in game if r['player']==i]
  rates.append((peer[-1]['simulationFrames']-peer[0]['simulationFrames'])/(peer[-1]['frame']-peer[0]['frame']))
 (a.results/'rates.json').write_text(json.dumps(rates,indent=2))
 assert min(rates)>=.95,('Full moving race runs below 95% speed',rates)
 print(f'PASS shared-music: course/final-lap BGM only, host-only BGM and leader tempo on lap {finalLap}; complete moving {a.players}-player race at {min(rates):.3f} updates/video frame',flush=True)
