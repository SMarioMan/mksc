#!/usr/bin/env python3
"""Change from a retained late Vs. course into Battle or a fresh GP cup."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','rom','elf','bios','results'):p.add_argument('--'+k,required=True,type=Path)
p.add_argument('--players',nargs='+',type=int,choices=range(2,9),default=[2,8])
p.add_argument('--destinations',nargs='+',choices=['battle','gp'],default=['battle','gp'])
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
symbols={f[2]:int(f[0],16) for l in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines() if len(f:=l.split())==3}
base=[]
for line in (Path(__file__).parent/'inputs/vs-to-battle.csv').read_text().splitlines():
 f,i,k=line.split(',');base.append((int(f),int(i),int(k,16)))
for count in a.players:
 for destination in a.destinations:
  name=f'{destination}-{count}';rows=[]
  for f,i,k in base:
   if i>=count:continue
   if destination=='gp':
    if f in (12300,12315):continue
    if f==10800:k=64
   rows.append((f,i,k))
  inputs=a.results/(name+'.csv');inputs.write_text(''.join(f'{f},{i},{k:x}\n'for f,i,k in rows))
  start=min(symbols['sCourse'],symbols['sRetro'])
  r=subprocess.run([str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(count),'--frames','14000','--input',str(inputs),'--progress-file',str(a.results/(name+'-progress.csv')),'--dump',hex(start),'--dump-words',str(abs(symbols['sCourse']-symbols['sRetro'])//4+1),'--barrier-before-input','--barrier-state','0x03006B40','--screens',str(a.results/name)],capture_output=True,text=True,timeout=900)
  (a.results/(name+'.json')).write_text(r.stdout);(a.results/(name+'.stderr')).write_text(r.stderr)
  assert r.returncode==0 and not r.stderr,(name,r.returncode,r.stderr)
  d=json.loads(r.stdout);assert d['collisions']==0
  states=[]
  for n in d['nodes']:
   g=n['game'];b=n['barrier']
   assert g['raceFlags']&(0x800 if destination=='battle'else 0x100) and g['humanCount']==count,(name,g)
   course=n['dump'][(symbols['sCourse']-start)//4];retro=n['dump'][(symbols['sRetro']-start)//4]
   assert [course,retro]==([1,0] if destination=='battle'else[0,0]),(name,'Previous mode leaked its course',course,retro)
   assert b['errors']==b['timeouts']==0,(name,b)
   racers=g['drivers'][:count if destination=='battle'else 8]
   if destination=='battle':assert all(x['balloons']==3 for x in racers),name
   else:assert len({x['character']for x in racers})==8,(name,'GP CPU roster incomplete')
   states.append([r['position']for r in racers])
  assert all(s==states[0]for s in states),(name,'Peers disagree after mode change')
  print(f'PASS {name}: retained Rainbow Road → native menu → '+('Battle course two'if destination=='battle'else'GP cup first course, full eight-racer grid'),flush=True)
