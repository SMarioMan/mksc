#!/usr/bin/env python3
"""Record an extended shared-audio race; check ownership and PCM deadlines."""
import argparse, csv, hashlib, json, struct, subprocess, wave
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','rom','elf','bios','results'):p.add_argument('--'+k,required=True,type=Path)
p.add_argument('--frames',type=int,default=36000)
p.add_argument('--gp',action='store_true',help='Record a complete four-course Retro GP cup')
p.add_argument('--boot-ring',action='store_true',help='Skip title inputs for a ROM built with --boot-ring')
p.add_argument('--analyze-only',action='store_true',help='Analyze already captured JSON, CSV and WAV files')
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
symbols={}
for l in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines():
 f=l.split()
 if len(f)==3:symbols[f[2]]=int(f[0],16)
rom=a.rom.read_bytes()
inputs=a.results/'inputs.csv';events=[]
for l in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=l.split(',');f=int(f)
 if int(i)==0 and f<2200 and (not a.boot_ring or f>=1500):events.extend((f,j,int(k,16)) for j in range(8 if f<1500 or f in (2000,2015) else 1))
# Luigi Circuit reproduces the rain animation/audio contention.
if a.gp:events.extend(((1600,0,64),(1615,0,0)))
course_keys=((2800,4),(3200,1),(3500,1)) if a.gp else ((3000,128),(3040,128),(3400,1),(4300,1),(4600,1))
for f,k in course_keys:
 events.extend(((f,0,k),(f+15,0,0)))
inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(events)))
video=a.results/'video.csv'
cmd=[str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players','8','--frames',str(a.frames),
 '--input',str(inputs),'--autodrive-frame','5500','--autofire','--video-from','1500','--video-log',str(video),
 '--audio-prefix',str(a.results/'pcm'),'--audio-state',hex(symbols['ring_audioTiming']),
 '--sound-table',hex(symbols['gMPlayTable']),'--barrier-state',hex(symbols['sRing']),'--barrier-before-input']
if a.gp:cmd.extend(['--auto-confirm-scene',hex(symbols['results_main']|1),'--until-scene',hex(symbols['results_main']|1),'--watch',hex(symbols['sGPCupComplete']),'--until-watch-value','1'])
if not a.analyze_only:
 proc=subprocess.run(cmd,capture_output=True,text=True,timeout=3600)
 (a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
 assert proc.returncode==0 and not proc.stderr,(proc.returncode,proc.stderr[:1000])
d=json.loads((a.results/'result.json').read_text());assert not d['collisions']
if a.gp:assert all(n['watch']==1 for n in d['nodes']),'Cup did not finish'
assert all(n['barrier']['errors']==n['barrier']['timeouts']==0 for n in d['nodes'])
song=lambda i:struct.unpack_from('<I',rom,symbols['gSongTable']-0x08000000+8*i)[0]
def ids(name,c,stride):return struct.unpack_from('<HH',rom,symbols[name]-0x08000000+c*stride)
allowed=[];special={song(i):c for c,i in enumerate(struct.unpack_from('<8H',rom,symbols['_080ECB78']-0x08000000))}
for i,n in enumerate(d['nodes']):
 c=n['game']['drivers'][i]['character']&7
 allowed.append({song(v) for name,stride in (('_080ECBC8',8),('_080ECC08',8),('_080ECC48',4),('_080ECC88',4)) for v in ids(name,c,stride)})
rows=[[] for _ in range(8)]
with video.open() as f:
 for r in csv.DictReader(f):rows[int(r['player'])].append({k:int(v) for k,v in r.items()})
report={'rom_sha256':hashlib.sha256(rom).hexdigest(),'frames':a.frames,'nodes':[]}
for i,rs in enumerate(rows):
 race=[r for r in rs if r['sceneMain']==symbols['race_main']|1]
 assert len(race)>(10000 if a.gp else 6000),(i,'No extended moving race',len(race))
 assert all(not r['voiceHeader'] or r['voiceHeader'] in allowed[i] for r in race),(i,'Foreign character voice')
 char=d['nodes'][i]['game']['drivers'][i]['character']&7
 assert all(r['effectHeader'] not in special or special[r['effectHeader']]==char for r in race),(i,'Foreign special voice')
 assert race[-1]['audioStarts3']==race[-1]['audioStarts4']==0,(i,'Remote engine/voice accepted')
 assert race[-1]['audioStarts1']>0 and race[-1]['audioStarts2']>0,(i,'Own engine/voice absent')
 assert rs[-1]['audioMissedFrames']==rs[-1]['audioRecoveredFrames'],(i,'Unfilled PCM segment')
 assert rs[-1]['audioLateSegments']==0,(i,'Late PCM segment')
 assert all(r['fifoSource']<=r['pcmBufferEnd'] for r in race),(i,'FIFO DMA read past native buffer')
 path=a.results/f'pcm-{i}.wav'
 with wave.open(str(path),'rb') as w:
  assert w.getnchannels()==2 and w.getsampwidth()==2 and w.getframerate()==65536
  frames=w.getnframes();assert frames>(max(r['frame'] for r in rs)-1502)/60*65000
  peak=0;nonzero=0;clipped=0
  while b:=w.readframes(65536):
   samples=struct.unpack('<'+'h'*(len(b)//2),b)
   peak=max(peak,max(abs(x) for x in samples));nonzero+=sum(x!=0 for x in samples);clipped+=sum(abs(x)>=32767 for x in samples)
  assert nonzero>65536,(i,'PCM is silent')
 report['nodes'].append({'player':i+1,'character':char,'audio_frames':frames,'peak':peak,'nonzero_samples':nonzero,'clipped_samples':clipped,
  'missed_services':rs[-1]['audioMissedFrames'],'recovered_segments':rs[-1]['audioRecoveredFrames'],'late_segments':rs[-1]['audioLateSegments']})
(a.results/'summary.json').write_text(json.dumps(report,indent=2))
print('PASS extended eight-player shared PCM: local voices/engines, no unfilled or late segments; per-instance WAV recordings retained',flush=True)
