#!/usr/bin/env python3
"""Drive one of three humans through a finish while the others stay on lap one.

Only keypad input changes game state. Require one final-lap cue and sustained
fast course music after both a host finish and a non-host finish.
"""
import argparse,csv,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):p.add_argument('--'+key,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
symbols={f[2]:int(f[0],16) for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines() if len(f:=line.split())==3}
rom=a.rom.read_bytes()
def song(i):return struct.unpack_from('<I',rom,symbols['gSongTable']-0x08000000+8*i)[0]
events=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',');f=int(f)
 if int(i)==0 and f<3500:events.extend((f,j,k) for j in range(3 if f<1500 or f in (2000,2015) else 1))
inputs=a.results/'inputs.csv';inputs.write_text(''.join(f'{f},{i},{k}\n' for f,i,k in events))
for finisher in (0,1):
 video=a.results/f'finisher-{finisher+1}.csv'
 proc=subprocess.run([str(a.runner),'--ring','--ring-quantum','2048','--rom',str(a.rom),'--bios',str(a.bios),'--players','3','--frames','18000','--input',str(inputs),'--autodrive-frame','3500','--autodrive-mask',str(1<<finisher),'--video-from','3500','--video-log',str(video),'--barrier-before-input','--barrier-state',hex(symbols['sRing'])],capture_output=True,text=True,timeout=600)
 (a.results/f'finisher-{finisher+1}.json').write_text(proc.stdout);(a.results/f'finisher-{finisher+1}.stderr').write_text(proc.stderr)
 assert proc.returncode==0 and not proc.stderr,proc.stderr
 result=json.loads(proc.stdout);assert result['bios']=='real' and not result['collisions']
 for node in result['nodes']:
  drivers=node['game']['drivers'];assert drivers[finisher]['lap']>=4,'Selected racer never finished'
  assert all(d['lap']<3 for i,d in enumerate(drivers[:3]) if i!=finisher)
  assert node['barrier']['errors']==node['barrier']['timeouts']==0
 frames=[{k:int(v) for k,v in row.items()} for row in csv.DictReader(video.open())]
 host=[r for r in frames if r['player']==0]
 first_final=min(r['frame'] for r in host if r['leadLap']>=3)
 final=[r for r in host if r['leadLap']>=3 and r['frame']>first_final+1 and r['musicHeader']==song(25)]
 assert len(final)>100 and all(r['musicTempo']==320 for r in final),'Final-lap tempo reset'
 assert not any(r['musicHeader'] in {song(i) for i in (5,6,7)} for r in host),'Individual finish replaced shared BGM'
 jingles=sum(r['musicHeader']==song(19) and (i==0 or host[i-1]['musicHeader']!=song(19)) for i,r in enumerate(host))
 assert jingles==1,('Final-lap jingle restarted',jingles)
 assert all(r['musicHeader']==song(25) and r['musicTempo']==320 and not r['musicStatus']&0x80000000 for r in host[-300:]),'Post-finish music changed or stopped'
 print(f'PASS P{finisher+1} finishes alone: course and fast tempo persist, one final-lap cue',flush=True)
