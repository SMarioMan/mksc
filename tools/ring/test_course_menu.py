#!/usr/bin/env python3
"""Read published menu pixels to verify Retro names and the Select hint."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','bios','results'):p.add_argument('--'+key,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
base=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',');f=int(f)
 if int(i)==0 and f<2350:base.append((f,int(k,16)))
font=[list(map(int,x.split(','))) for x in re.findall(r'\{([\d,]+)\}',(Path(__file__).resolve().parents[2]/'src/ringFont.h').read_text())]
def glyph(c):return ord(c)-65 if 'A'<=c<='Z' else ord(c)-48+26 if c.isdigit() else 36
for case,last in [('cup',2150),('track',2300)]:
 events=[]
 for f,k in base:
  if f>=last:continue
  events.extend((f,i,k) for i in range(2 if f<1500 or f in (2000,2015) else 1))
 events.extend([(2100,0,4),(2115,0,0)])
 inputs=a.results/(case+'.csv');inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(events)))
 proc=subprocess.run([str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players','2','--frames',str(last),
  '--input',str(inputs),'--screens',str(a.results/case)],capture_output=True,text=True,timeout=120)
 (a.results/(case+'.json')).write_text(proc.stdout);(a.results/(case+'.stderr')).write_text(proc.stderr)
 assert proc.returncode==0,(case,proc.stderr)
 data=json.loads(proc.stdout);assert data['collisions']==0
 for slot in range(2):
  raw=(a.results/f'{case}-{slot}.ppm').read_bytes();header,pixels=raw.split(b'\n255\n',1)
  assert b'240 160' in header
  def check(x,y,text,color):
   for c in text:
    for row,bits in enumerate(font[glyph(c)]):
     for col in range(5):
      if bits&(16>>col):
       at=((y+row)*240+x+col)*3
       assert tuple(pixels[at:at+3])==color,(case,slot,text,c,row,col,tuple(pixels[at:at+3]))
    x+=6
  check(144,132,'RETRO',(255,255,255))
  check(12,148,'SELECT  SUPER OR RETRO',(255,255,0))
  if case=='track':
   for i,name in enumerate(['MARIO CIRCUIT 1','DONUT PLAINS 1','GHOST VALLEY 1','BOWSER CASTLE 1']):
    check(20,40+i*22,name,(255,255,0) if i==0 else (255,255,255))
 print(f'PASS {case}: Select hint and Retro label'+('; all four rendered Retro names' if case=='track' else ''),flush=True)
