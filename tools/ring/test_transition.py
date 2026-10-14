#!/usr/bin/env python3
"""Re-enter game selection from Rainbow Road and launch Battle course two."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','rom','elf','bios','results'):p.add_argument('--'+k,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
symbols={f[2]:int(f[0],16) for l in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines() if len(f:=l.split())==3}
r=subprocess.run([str(a.runner),'--ring','--rom',str(a.rom),'--bios',str(a.bios),'--players','8','--frames','14000','--input',str(Path(__file__).parent/'inputs/vs-to-battle.csv'),'--progress-file',str(a.results/'progress.csv'),'--dump',hex(min(symbols['sCourse'],symbols['sRetro'])),'--dump-words',str(abs(symbols['sCourse']-symbols['sRetro'])//4+1),'--barrier-before-input','--barrier-state','0x03006B40','--screens',str(a.results/'transition')],capture_output=True,text=True,timeout=900)
(a.results/'result.json').write_text(r.stdout);(a.results/'result.stderr').write_text(r.stderr)
assert r.returncode==0 and not r.stderr,(r.returncode,r.stderr)
d=json.loads(r.stdout);assert d['collisions']==0
states=[]
for n in d['nodes']:
 g=n['game'];b=n['barrier']
 assert g['raceFlags']&0x800 and g['humanCount']==8
 assert [n['dump'][(symbols['sCourse']-min(symbols['sCourse'],symbols['sRetro']))//4],n['dump'][(symbols['sRetro']-min(symbols['sCourse'],symbols['sRetro']))//4]]==[1,0],('Battle course two was not selected',n['dump'][:2])
 assert b['errors']==b['timeouts']==0
 assert all(x['balloons']==3 for x in g['drivers'])
 states.append([r['position'] for r in g['drivers']])
assert all(s==states[0] for s in states)
print('PASS: Rainbow Road → pause/change track → Back through each menu → Battle course two; all eight consoles synchronized')
