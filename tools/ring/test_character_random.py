#!/usr/bin/env python3
"""Choose Random on every console, wait for host OK, and observe native roulette."""
import argparse,json,struct,subprocess
from collections import deque
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--players',nargs='+',type=int,choices=range(2,9),default=[2,8])
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
symbols={f[2]:int(f[0],16)for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines()if len(f:=line.split())==3}
rom=a.rom.read_bytes();navigation=[struct.unpack_from('<hhbbbb',rom,symbols['_080D9274']-0x08000000+i*8)[2:]for i in range(9)]
def route(start):
 queue=deque([(start,[])]);seen={start}
 while queue:
  at,path=queue.popleft()
  if at==4:return path
  for key,next in zip((64,128,32,16),navigation[at]):
   if next>=0 and next not in seen:seen.add(next);queue.append((next,path+[key]))
 raise AssertionError('Random is not reachable')
for count in a.players:
 rows=[]
 for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
  frame,slot,key=line.split(',');frame=int(frame)
  if int(slot)==0 and frame<1950:rows.extend((frame,i,int(key,16))for i in range(count if frame<1500 else 1))
 for i in range(count):
  for step,key in enumerate(route(i+(i>=4))):rows.extend(((2100+step*40,i,key),(2115+step*40,i,0)))
  rows.extend(((2400,i,1),(2415,i,0)))
 rows.extend(((3000,0,1),(3015,0,0)))
 inputs=a.results/f'random-{count}.csv';inputs.write_text(''.join(f'{f},{i},{k:x}\n'for f,i,k in sorted(rows)))
 resolved=None
 for phase,frame in (('await-host',2700),('roulette',3040),('course',3600)):
  name=f'{count}-{phase}'
  proc=subprocess.run([str(a.runner),'--ring','--ring-quantum','2048','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(count),'--frames',str(frame),'--input',str(inputs),'--watch',hex(symbols['sNativeCharactersReady']),'--dump','0x0202A400','--dump-words',str(0x738//4),'--keys-address',hex(symbols['sNativeCharacterChoices']),'--key-slots','4','--barrier-before-input','--barrier-state',hex(symbols['sRing']),'--screens',str(a.results/name)],capture_output=True,text=True,timeout=300)
  (a.results/(name+'.json')).write_text(proc.stdout);(a.results/(name+'.stderr')).write_text(proc.stderr)
  assert proc.returncode==0 and not proc.stderr,(name,proc.stderr)
  result=json.loads(proc.stdout);assert result['bios']=='real' and not result['collisions']
  choices=[]
  for node in result['nodes']:
   assert not node['barrier']['errors'] and not node['barrier']['timeouts'],name
   assert node['watch']&255==(1<<count)-1,(name,'Random must allow multiple confirmations')
   roster=list(struct.pack('<4H',*node['menu']['heldKeys']))[:count];choices.append(roster)
   page=node['dump'][0];nativePhase=node['dump'][0x4FC//4]
   if phase=='await-host':assert page==1 and nativePhase==4 and roster==[4]*count,(name,page,nativePhase,roster)
   elif phase=='roulette':assert page==1 and nativePhase==6,(name,'Native roulette did not run',page,nativePhase)
   else:
    assert page==2 and len(set(roster))==count and 4 not in roster,(name,page,roster)
    assert node['dump'][0x18//4]==roster[node['slot']]-(roster[node['slot']]>4),(name,'Native final character differs from common roster')
  assert all(roster==choices[0]for roster in choices),(name,'Random choices differ across consoles')
  if phase=='roulette':resolved=choices[0]
  elif phase=='course':assert choices[0]==resolved,(name,'Roulette changed the common final choices')
  print(f'PASS {name}: native host confirmation/roulette and synchronized unique roster',flush=True)
