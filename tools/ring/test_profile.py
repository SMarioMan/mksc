#!/usr/bin/env python3
"""Require instruction profiling to preserve the linked game's exact execution."""
import argparse, hashlib, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','inputs','results'):
 p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--analyze-only',action='store_true',help='Compare existing captured snapshots and video logs')
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
assert hashlib.sha1(a.bios.read_bytes()).hexdigest()=='300c20df6731a33952ded8c436f7f186d25d3492'
symbols={}
for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines():
 f=line.split()
 if len(f)==3:symbols[f[2]]=int(f[0],16)
captures=[]
for enabled in (False,True):
 name='profile' if enabled else 'normal';video=a.results/(name+'.csv')
 cmd=[str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players','8','--frames','6600',
  '--input',str(a.inputs),'--autodrive-frame','5500','--autofire','--video-from','6000','--video-log',str(video),
  '--audio-state',hex(symbols['ring_audioTiming']),'--sound-table',hex(symbols['gMPlayTable']),
  '--timing-state',hex(symbols['ring_frameTiming']),'--barrier-before-input','--barrier-state',hex(symbols['sRing'])]
 if enabled:cmd+=['--profile-file',str(a.results/'instructions.csv'),'--profile-from','6000','--profile-until','6300']
 if not a.analyze_only:
  proc=subprocess.run(cmd,capture_output=True,text=True,timeout=900)
  (a.results/(name+'.json')).write_text(proc.stdout);(a.results/(name+'.stderr')).write_text(proc.stderr)
  assert proc.returncode==0 and not proc.stderr,(name,proc.returncode,proc.stderr)
 captures.append((json.loads((a.results/(name+'.json')).read_text()),video.read_bytes()))
for snapshot,_ in captures:snapshot.pop('profileScheduling',None)
assert captures[0][0]==captures[1][0], 'Profiling changed CPU/transport/game state; both snapshots are retained'
assert captures[0][1]==captures[1][1], 'Profiling changed per-video gameplay, audio or rendering timing'
assert len((a.results/'instructions.csv').read_text().splitlines())>1000,'No instruction profile captured'
print('PASS profiler: eight-player Yoshi Desert has byte-identical snapshots and per-video timing with profiling enabled or disabled',flush=True)
