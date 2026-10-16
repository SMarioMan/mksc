#!/usr/bin/env python3
"""Check native Retro course lettering with native L/R navigation."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','bios','results'):p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--players',type=int,choices=range(2,9),default=8)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
rows=[]
for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
 f,i,k=line.split(',');f=int(f)
 if int(i) or f>=2230:continue
 rows.extend((f,j,int(k,16)) for j in range(a.players if f<1500 or f in (2000,2015) else 1))
rows.extend([(2800,0,256),(2815,0,0)])
inputs=a.results/'inputs.csv';inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f,i,k in sorted(rows)))
proc=subprocess.run([str(a.runner),'--ring', '--ring-quantum', '2048','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(a.players),'--frames','3350',
 '--input',str(inputs),'--dump','0x0202B5E4','--screens',str(a.results/'retro')],capture_output=True,text=True,timeout=600)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0 and not proc.stderr,(proc.returncode,proc.stderr)
data=json.loads(proc.stdout);assert data['bios']=='real' and data['collisions']==0
font=[list(map(int,x.split(','))) for x in re.findall(r'\{([\d,]+)\}',(Path(__file__).resolve().parents[2]/'src/ringFont.h').read_text())]
names=json.loads((Path(__file__).parent/'fixtures/native-retro-mushroom.json').read_text())
for node in data['nodes']:
 assert node['dump'][0]==1,'R did not switch the native course page to Retro'
 slot=node['slot'];raw=(a.results/f'retro-{slot}.ppm').read_bytes().split(b'\n255\n')[1]
 for i,item in enumerate(names):
  ink=bytes(raw[(y*240+x)*3:(y*240+x)*3+3]==bytes((107,0,148)) for y in range(83+i*16,94+i*16) for x in range(138,235))
  assert hashlib.sha256(ink).hexdigest()==item['ink_sha256'],(slot,item['name'],'Native lettering changed')
print(f'PASS native Retro menu: all four course names and native R-button page switch on {a.players} consoles',flush=True)
