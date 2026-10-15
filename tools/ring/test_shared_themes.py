#!/usr/bin/env python3
"""Compare shared Star/Boo/lightning policy with native Thumb music requests."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','bios','results'):p.add_argument('--'+key,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
(a.results/'main.c').write_text(r'''
#include "common.h"
#include "scene.h"
u16 ring_raceEffectMusic(const RaceState*);
static RaceState state;
static Driver drivers[8];
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 unsigned count,first,second,pair,star,small,boo,i,tests=0;
 unsigned nativeSongs[8], mask;
 typedef void (*NativeRequest)(unsigned);
 NativeRequest nativeRequest=(NativeRequest)0x080522A5;
 *(volatile unsigned*)0x03003658=(unsigned)&drivers[0];
 for(mask=0;mask<8;mask++) {
  extern volatile unsigned oracleLastSong;
  oracleLastSong=0;
  *(volatile unsigned char*)0x03003AB6=0;
  if(mask&1)nativeRequest(4);
  if(mask&2)nativeRequest(8);
  if(mask&4)nativeRequest(16);
  nativeSongs[mask]=oracleLastSong;
 }
 for(i=0;i<8;i++)state.drivers[i]=&drivers[i];
 for(count=2;count<=8;count++) {
  state.driverCount=count;
  for(pair=0;pair<4;pair++)for(first=0;first<(1u<<count);first++)for(second=0;second<(1u<<count);second++) {
   unsigned mask=(1<<count)-1, expected;
   star=pair==2?0:first;
   small=pair==1?0:second;
   boo=pair==1?second:pair==2?first:pair==3?~(first|second):0;
   expected=nativeSongs[(star&mask?4:0)|(boo&mask?2:0)|(small&mask?1:0)];
   for(i=0;i<8;i++) {
    drivers[i].starTimer=i>=count || star&(1<<i)?210:-1;
    drivers[i].booTimer=i>=count || boo&(1<<i)?360:0;
    drivers[i].shrinkTimer=i>=count || small&(1<<i)?1:0;
   }
   if(ring_raceEffectMusic(&state)!=expected) {
    result[0]=0xbad00001;result[1]=count;result[2]=star;result[3]=small;for(;;){}
   }
   tests++;
   if(!(tests&65535))result[10]=tests;
  }
 }
 result[0]=0x600dc0de;result[1]=tests;for(i=0;i<8;i++)result[2+i]=nativeSongs[i];for(;;){}
}
''')
# Execute the clean native music request handler at its original address.
# Its encoded branch/literal targets remain intact; only the two external
# dependencies are replaced with observational stubs at their native addresses.
clean=(root/'baserom.gba').read_bytes()
assert __import__('hashlib').sha1(clean).hexdigest()=='9d327c030c3e2d9007990518594f70c3340ac56f'
(a.results/'native.bin').write_bytes(clean[0x522A4:0x523DC])
(a.results/'native.s').write_text('.section .oracle_native,"ax"\n.incbin "'+str(a.results/'native.bin')+'"\n')
with (a.results/'main.c').open('a') as source:
 source.write("\nvolatile unsigned oracleLastSong;\n__attribute__((section(\".oracle_song\"))) void oracleSongStart(unsigned song) { oracleLastSong=song; }\n__attribute__((section(\".oracle_track\"))) unsigned oracleRaceMusic(void* state) { (void)state; return 25; }\n")
linker=(root/'tools/ring-poc/link.ld').read_text().replace('    .bss (NOLOAD)', '    .oracle_track 0x08032C38 : { *(.oracle_track) } > rom\n    .oracle_native 0x080522A4 : { *(.oracle_native) } > rom\n    .oracle_song 0x0805FF20 : { *(.oracle_song) } > rom\n    .bss (NOLOAD)')
(a.results/'oracle.ld').write_text(linker)
lib=subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0]
commands=[['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')]]
for source,name in ((a.results/'main.c','main.o'),(root/'src/ringSoundControl.O3.c','sound.o')):
 commands.append(['arm-none-eabi-gcc','-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-std=gnu89','-DMKSC_RING=1','-fwrapv','-fno-strict-aliasing','-ffreestanding','-fno-builtin',*[f'-I{root/d}'for d in ('src','include','lib')],'-c',str(source),'-o',str(a.results/name)])
commands.append(['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'native.o'),str(a.results/'native.s')])
commands += [ ['arm-none-eabi-ld','-T',str(a.results/'oracle.ld'),'-o',str(a.results/'themes.elf'),*[str(a.results/f)for f in ('start.o','main.o','sound.o','native.o')],'-L'+lib,'-lgcc'], ['arm-none-eabi-objcopy','-O','binary',str(a.results/'themes.elf'),str(a.results/'themes.gba')], [str(root/'tools/gbafix/gbafix'),str(a.results/'themes.gba'),'-p','-tSHAREDTHEMES','--silent']]
for command in commands:subprocess.run(command,cwd=root,check=True,capture_output=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'themes.gba'),'--bios',str(a.bios),'--players','1','--frames','8000','--dump','0x02000000'],capture_output=True,text=True,timeout=180)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0 and not proc.stderr,proc.stderr
values=json.loads(proc.stdout)['nodes'][0]['dump']
assert values[:2]==[0x600dc0de,4*sum(4**count for count in range(2,9))],values
assert values[2:10]==[0,11,12,12,10,10,10,10],('Native music priority/song IDs changed',values)
print('PASS 349504 common music fixtures against original Thumb request handler: all 2–8 racer counts, Star/Boo/lightning masks, overlap, expiration and native priority')
