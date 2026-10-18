#!/usr/bin/env python3
"""Select all 48 courses with real keys; profile and stress eight moving racers.

The output preserves video-by-video simulation/audio timing, instruction-cycle
profiles, screenshots and transport state. Menus and gameplay memory are never
modified by the harness. Full races use route-following keypad inputs and items.
"""
import argparse,bisect,csv,hashlib,json,statistics,subprocess
from types import SimpleNamespace
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--workers',type=int,default=3,choices=range(1,5))
p.add_argument('--courses',nargs='+',default=[f'{s}-{i}' for s in ('battle','retro-battle') for i in range(4)]+[f'{s}-{i}' for s in ('super','retro') for i in range(20)])
p.add_argument('--frames',type=int,default=18000)
p.add_argument('--profile-frames',type=int,default=300)
p.add_argument('--profile-from',type=int,default=6000,help='Start instruction profiling at this video frame')
p.add_argument('--profile-player',type=int,choices=range(8),default=0,help='Zero-based console to profile; choose the slow camera from timing_by_player')
p.add_argument('--analyze-only',action='store_true',help='Recheck existing captured JSON/video/profile files')
p.add_argument('--resume',action='store_true',help='Recheck completed passing cases and run the remainder')
p.add_argument('--audio-policy',choices=('shared','normal'),default='shared')
p.add_argument('--complete',action='store_true',help='Run each race through results, up to 70000 frames')
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
if a.profile_from<0 or a.profile_frames<0 or a.profile_from+a.profile_frames>100000:p.error('Profile interval must be within video frames 0..100000')
assert hashlib.sha1(a.bios.read_bytes()).hexdigest()=='300c20df6731a33952ded8c436f7f186d25d3492'
# A ROM hash alone cannot validate captures after changing the emulator.
# Include adjacent shared cores as well: rebuilding libmgba need not relink
# the runner executable. Resolve symlinks so version aliases are counted once.
runtime_files={a.runner.resolve()}
for directory in (a.runner.parent,a.runner.parent/'lib'):
 for pattern in ('libmgba.so*','libmgba*.dylib','mgba*.dll','libmgba*.dll'):
  runtime_files.update(path.resolve() for path in directory.glob(pattern) if path.is_file())
runtime_fingerprint={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(runtime_files)}
symbols={};functions=[]
for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines():
 f=line.split()
 if len(f)==3:
  symbols[f[2]]=int(f[0],16)
  if f[1].lower()=='t':functions.append((int(f[0],16),f[2]))
addresses=[x[0] for x in functions]
identity_symbols=[name for name in ('ring_identityOverflows','ring_identityNative8bpp','ring_identityPublishLines','ring_identityRestoreLines','ring_identityUploads','ring_identitySlowPrepares','ring_graphicsOverruns') if name in symbols]
observed_addresses=[symbols[name] for name in ['sCourse','sRetro']+identity_symbols]
courseBase=min(observed_addresses)
courseWords=(max(observed_addresses)-courseBase)//4+1
assert courseWords<=4096
rom=a.results/(hashlib.sha256(a.rom.read_bytes()).hexdigest()+'.gba')
if not rom.exists():rom.write_bytes(a.rom.read_bytes())
base=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',')
 if int(i)==0 and int(f)<2230:base.append((int(f),int(k,16)))
reports={};completed={}
if a.analyze_only and not (a.results/'summary.json').exists():p.error('Reanalysis requires the captured summary manifest')
if (a.resume or a.analyze_only) and (a.results/'summary.json').exists():
 previous=json.loads((a.results/'summary.json').read_text())
 if a.analyze_only:
  assert previous.get('profile_window',{'from':6000,'frames':300})=={'from':a.profile_from,'frames':a.profile_frames},'Reanalysis must retain the captured profile interval'
  assert previous.get('profile_player',0)==a.profile_player,'Reanalysis must retain the captured profile console'
  policies={v['audio_policy'] for v in previous['passed'].values()}
  assert not policies or policies=={a.audio_policy},'Reanalysis must retain the captured audio policy'
 assert previous['rom_sha256']==hashlib.sha256(rom.read_bytes()).hexdigest(),'Resume requires the same ROM'
 assert sorted(previous.get('runtime_fingerprint',{}).values())==sorted(runtime_fingerprint.values()),'Captured results require the same emulator executable and shared core bytes'
 completed={k:v for k,v in previous['passed'].items() if v.get('audio_policy')==a.audio_policy and v.get('profile_from',6000)==a.profile_from and v.get('profile_frames',300)==a.profile_frames and v.get('profile_player',0)==a.profile_player}
 if a.profile_frames:
  # Legacy instruction stepping changed the scheduler's yield points. Only
  # reuse captures made with observation inside the normal run-loop quantum.
  completed={k:v for k,v in completed.items() if json.loads((a.results/(k+'.json')).read_text()).get('profileScheduling')=='quantum'}
def run(case):
 kind,idx=case.rsplit('-',1);idx=int(idx);battle=kind in ('battle','retro-battle');assert 0<=idx<(4 if battle else 20)
 events=[]
 for f,k in base:events.extend((f,i,k) for i in range(8 if f<1500 or f in (2000,2015) else 1))
 def key(f,k):events.extend([(f,0,k),(f+15,0,0)])
 if battle:
  key(1600,128)
  for step in range(idx):key(2600+step*40,128)
  if kind=='retro-battle':key(2900,256)
  key(3300 if kind=='retro-battle' else 3000,1);key(3700,1);key(3900,1)
  # Battle has no race checkpoint route. Explicit human driving and item
  # inputs exercise motion/combat rather than profiling stationary karts.
  events.extend((5500,i,33 if i==0 else 17) for i in range(8))
  for frame in range(5650,a.frames,120):
   for player in range(8):
    held=33 if player==0 else 17
    events.extend(((frame+player*7,player,held|512),(frame+player*7+1,player,held)))
 else:
  if kind=='retro':key(2800,256)
  for step in range(idx//4):key(3000+step*40,128)
  key(3400,1)
  for step in range(idx%4):key(3900+step*40,128)
  key(4300,1);key(4600,1)
 inputs=a.results/(case+'.csv');inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(events)))
 video=a.results/(case+'-video.csv');profile=a.results/(case+'-profile.csv')
 command=[str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(rom),'--bios',str(a.bios),'--players','8',
 '--frames',str(70000 if a.complete and not battle else a.frames),'--input',str(inputs),
 '--video-from','6000','--video-log',str(video),
 '--sound-table',hex(symbols['gMPlayTable']),'--audio-state',hex(symbols['ring_audioTiming']),'--timing-state',hex(symbols['ring_frameTiming']),
 '--profile-file',str(profile),'--profile-from',str(a.profile_from),'--profile-until',str(a.profile_from+a.profile_frames),'--trace-player',str(a.profile_player),'--dump',hex(courseBase),'--dump-words',str(courseWords),
 '--barrier-before-input','--barrier-state',hex(symbols['sRing']),'--screens',str(a.results/case)]
 if not battle:command.extend(['--autodrive-frame','5500','--autofire'])
 if not battle:
  # A faster build can finish before a bounded performance run ends. Stop
  # at results and release the driving keypad there instead of accidentally
  # confirming through results into the title screen.
  command.extend(['--until-scene',hex(symbols['results_main']|1),'--release-inputs-scene',hex(symbols['results_main']|1)])
 if a.analyze_only or case in completed:
  proc=SimpleNamespace(returncode=0,stdout=(a.results/(case+'.json')).read_text(),stderr=(a.results/(case+'.stderr')).read_text())
 else:proc=subprocess.run(command,capture_output=True,text=True,timeout=2400)
 (a.results/(case+'.json')).write_text(proc.stdout);(a.results/(case+'.stderr')).write_text(proc.stderr)
 assert proc.returncode==0 and not proc.stderr,(case,proc.returncode,proc.stderr[:1000])
 data=json.loads(proc.stdout);assert data['bios']=='real' and data['collisions']==0
 if a.profile_frames:assert data.get('profileScheduling')=='quantum',(case,'Legacy profiling changed link scheduling; recapture with the current runner')
 assert all([n['dump'][(symbols['sCourse']-courseBase)//4],n['dump'][(symbols['sRetro']-courseBase)//4]]==[idx,int(kind.startswith('retro'))] for n in data['nodes']),(case,'Wrong course selected',data['nodes'][0]['dump'][:2])
 assert all(n['barrier']['errors']==n['barrier']['timeouts']==0 for n in data['nodes']),(case,'Transport failed')
 if identity_symbols:
  for node in data['nodes']:
   for name in ('ring_identityOverflows','ring_identityNative8bpp','ring_graphicsOverruns'):
    if name not in symbols:continue
    assert node['dump'][(symbols[name]-courseBase)//4]==0,(case,name,node['slot'])
   assert all(node['game']['hud']['kartPalettesMatch']),(case,'Native kart palette overwritten',node['slot'])
 assert all(n['game']['humanCount']==8 for n in data['nodes'])
 positions=[[d['position'] for d in n['game']['drivers']] for n in data['nodes']]
 assert all(v==positions[0] for v in positions),(case,'Physics divergence')
 peers=[[] for _ in range(8)]
 with video.open() as f:
  for row in csv.DictReader(f):
   if int(row['sceneMain'])==symbols['race_main']|1:peers[int(row['player'])].append({k:int(v) for k,v in row.items()})
 rates=[];windows=[];mixgaps=[]
 movement=[len({r[f'kartPosition{i}'] for r in peers[0]}) for i in range(8)]
 assert min(movement)>=100,(case,'A human racer did not move through the stress window',movement)
 for i,rows in enumerate(peers):
  assert len(rows)>1000,(case,i,'Insufficient moving gameplay',len(rows))
  rates.append((rows[-1]['simulationFrames']-rows[0]['simulationFrames'])/(rows[-1]['frame']-rows[0]['frame']))
  windows.append(min((rows[j+300]['simulationFrames']-rows[j]['simulationFrames'])/300 for j in range(len(rows)-300)))
  mixgaps.append((rows[-1]['audioMissedFrames']-rows[0]['audioMissedFrames']) - (rows[-1]['audioRecoveredFrames']-rows[0]['audioRecoveredFrames']))
  assert max(r['invalidObjects'] for r in rows)==0,(case,i,'Invalid sprites')
  assert all(r['balloonForeignPixels']==0 for r in rows) if battle else True
  assert rows[-1]['audioLateSegments']==rows[0]['audioLateSegments'],(case,i,'PCM segment prepared after its playback deadline')
  if a.audio_policy=='shared':
   assert rows[-1]['audioStarts3']==rows[-1]['audioStarts4']==0,(case,i,'Duplicated engine/voice channels')
 costs={}
 for row in csv.DictReader(profile.open()):
  pc=int(row['pc'],16);at=bisect.bisect_right(addresses,pc)-1;name=functions[at][1] if at>=0 else f'{pc:08x}'
  costs[name]=costs.get(name,0)+int(row['cycles'])
 summary={'course':case,'audio_policy':a.audio_policy,'profile_scheduling':data.get('profileScheduling','legacy'),'profile_from':a.profile_from,'profile_frames':a.profile_frames,'profile_player':a.profile_player,'rates':rates,'minimum_300_frame_rates':windows,'audio_missed_frames':mixgaps,'distinct_racer_positions':movement,
 'profile_cycles':sorted(costs.items(),key=lambda x:-x[1])[:30],
 'timing':{k:{'median':statistics.median(r[k] for r in peers[0]),'p99':sorted(r[k] for r in peers[0])[int(len(peers[0])*.99)],'max':max(r[k] for r in peers[0])} for k in ('exchangeCycles','renderCycles','publishCycles','mixCycles')}}
 summary['timing_by_player']=[{k:{'median':statistics.median(r[k] for r in rows),'p99':sorted(r[k] for r in rows)[int(len(rows)*.99)],'max':max(r[k] for r in rows)} for k in ('exchangeCycles','renderCycles','publishCycles','mixCycles')} for rows in peers]
 (a.results/(case+'-summary.json')).write_text(json.dumps(summary,indent=2))
 assert min(rates)>=.95 and min(windows)>=.95,(case,'Slow gameplay',rates,windows)
 assert max(mixgaps)==0,(case,'Skipped audio service frames',mixgaps)
 if identity_symbols:
  summary['identity_maximum_scanlines']={name:max(n['dump'][(symbols[name]-courseBase)//4] for n in data['nodes']) for name in ('ring_identityPublishLines','ring_identityRestoreLines')}
 return summary
failures=[]
with ThreadPoolExecutor(max_workers=a.workers) as pool:
 futures={pool.submit(run,c):c for c in a.courses}
 for future in as_completed(futures):
  case=futures[future]
  try:
   report=future.result();reports[case]=report;print(f"PASS {case}: min rate {min(report['rates']):.3f}; all queued audio segments prepared",flush=True)
  except Exception as e:failures.append(str(e));print(f'FAIL {case}: {e}',flush=True)
  (a.results/'summary.json').write_text(json.dumps({'rom_sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),'runtime_fingerprint':runtime_fingerprint,'profile_window':{'from':a.profile_from,'frames':a.profile_frames},'profile_player':a.profile_player,'audio_policy':a.audio_policy,'passed':reports,'failures':failures},indent=2))
if failures:raise SystemExit('Course regressions failed: '+'; '.join(failures))
