#!/usr/bin/env python3
"""Verify shared common menu effects play once, while character sounds stay local."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--players',type=int,choices=range(2,9),default=8)
p.add_argument('--host',type=int,default=3)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
assert a.host<a.players
symbols={f[2]:int(f[0],16)for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines()if len(f:=line.split())==3}
rows=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',');f=int(f)
 if int(i) or f>=2200:continue
 rows.extend((f,slot if f<1500 or f in (2000,2015)else a.host,int(k,16))for slot in range(a.players if f<1500 or f in (2000,2015)else 1))
rows.extend([(2500,a.host,256),(2515,a.host,0)])
inputs=a.results/'inputs.csv';inputs.write_text(''.join(f'{f},{i},{k:x}\n'for f,i,k in sorted(rows)))
outputs=[]
for frame in (2400,2750):
 result=subprocess.run([str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(a.players),'--frames',str(frame),'--input',str(inputs),'--dump',hex(symbols['ring_audioMenuStarts']),'--dump-words','11','--barrier-before-input','--barrier-state','0x03006B40'],capture_output=True,text=True,timeout=300)
 (a.results/f'{frame}.json').write_text(result.stdout);(a.results/f'{frame}.stderr').write_text(result.stderr)
 assert result.returncode==0 and not result.stderr,result.stderr
 data=json.loads(result.stdout)
 assert data['collisions']==0 and all(n['barrier']['errors']==n['barrier']['timeouts']==0 for n in data['nodes'])
 outputs.append(data['nodes'])
for before,after in zip(*outputs):
 delta=[b-a for a,b in zip(before['dump'],after['dump'])]
 if before['slot']==a.host:assert sum(delta[1:])>0,('Host course switch produced no effect',delta)
 else:assert not any(delta),('Duplicate shared menu sound on a peer',before['slot'],delta)
 assert before['dump'][2]>0,('Native local character confirmation voice was suppressed',before['slot'])
print(f'PASS shared menu audio: local character voices on {a.players} consoles; common course switch sounds only on software P1 (window {a.host+1})')
