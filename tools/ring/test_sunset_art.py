#!/usr/bin/env python3
"""Verify Japanese Sunset Wilds records and their native course preview DMA."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
from restore_battle_assets import apply_bps
from restore_sunset_assets import FIXTURE,source_assets,course_record
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','rom','elf','bios','results'):p.add_argument('--'+key,required=True,type=Path)
p.add_argument('--players',nargs='+',type=int,choices=range(2,9),default=[2,8])
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];clean=(root/'baserom.gba').read_bytes();rom=a.rom.read_bytes()
symbols={f[2]:int(f[0],16) for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines() if len(f:=line.split())==3}
assets=apply_bps(source_assets(clean),FIXTURE.read_bytes());size=struct.unpack_from('<I',assets)[0];course=assets[4:4+size];cover=assets[4+size:]
def word(at):return struct.unpack_from('<I',rom,at)[0]
data=symbols['courseData_bin']-0x08000000;record=data+word(data+26*4)
assert rom[record:record+size]==course,'Japanese course sprite record was not linked'
cover_address=symbols['ring_sunsetJapaneseCover']-0x08000000
assert rom[cover_address:cover_address+len(cover)]==cover
assert word(symbols['_080E75C8']-0x08000000)==cover_address+0x08000000
assert course!=course_record(clean),'The imported course must contain the Japanese changes'
# Decode only the preview's standard BIOS LZ77 stream.
at=4;tiles=bytearray();length=int.from_bytes(cover[1:4],'little')
while len(tiles)<length:
 flags=cover[at];at+=1
 for bit in range(7,-1,-1):
  if flags&(1<<bit):
   count=(cover[at]>>4)+3;distance=((cover[at]&15)<<8|cover[at+1])+1;at+=2
   for _ in range(count):tiles.append(tiles[-distance])
  else:tiles.append(cover[at]);at+=1
  if len(tiles)>=length:break
for count in a.players:
 events=[]
 for line in (Path(__file__).parent/'inputs/ring-vs.csv').read_text().splitlines():
  f,i,k=line.split(',');f=int(f)
  if int(i)==0 and f<2200:events.extend((f,j,int(k,16)) for j in range(count if f<1500 or f in (2000,2015) else 1))
 for f,k in ((3000,128),(3040,128),(3400,1),(3900,128),(3940,128),(3980,128)):
  events.extend(((f,0,k),(f+15,0,0)))
 inputs=a.results/f'preview-{count}.csv';inputs.write_text(''.join(f'{f},{i},{k:x}\n'for f,i,k in sorted(events)))
 proc=subprocess.run([str(a.runner),'--ring','--ring-quantum','2048','--rom',str(a.rom),'--bios',str(a.bios),'--players',str(count),'--frames','4300','--input',str(inputs),'--dump','0x06004000','--dump-words',str(length//4),'--screens',str(a.results/f'preview-{count}')],capture_output=True,text=True,timeout=300)
 (a.results/f'preview-{count}.json').write_text(proc.stdout);(a.results/f'preview-{count}.stderr').write_text(proc.stderr)
 assert proc.returncode==0 and not proc.stderr,proc.stderr
 result=json.loads(proc.stdout);assert result['bios']=='real' and not result['collisions']
 for node in result['nodes']:
  actual=struct.pack('<'+'I'*len(node['dump']),*node['dump'])
  assert actual==tiles,('Native Sunset Wilds preview differs from Japanese artwork',count,node['slot'])
 print(f'PASS Japanese Sunset Wilds sprites/relative record and native preview tiles on {count} consoles',flush=True)
