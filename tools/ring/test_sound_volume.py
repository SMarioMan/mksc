#!/usr/bin/env python3
"""Compare positional attenuation with the matching native object."""
import argparse, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','bios','results'):p.add_argument('--'+k,required=True,type=Path)
p.add_argument('--mode',choices=('thumb','arm'),default='thumb')
a=p.parse_args();root=Path(__file__).resolve().parents[2];a.results.mkdir(parents=True,exist_ok=True)
(a.results/'main.c').write_text(r'''
#include "common.h"
typedef struct { s32 x,y; } Position;
u16 nativeVolume(void*,const Position*);
u16 sound_getDistanceVolume(void*,const Position*);
static u32 scene[0x850/4],state;
static u32 next(void) {state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 volatile u16* timer=(volatile u16*)0x0400010C;
 int test;u32 nativeCycles=0,optimizedCycles=0;
 state=0x2ffa7943;
 for(test=0;test<131072;test++) {
  Position point;u16 expected,actual;
  scene[0x83c/4]=next();scene[0x840/4]=next();
  point.x=scene[0x83c/4]+(test&1?(s32)next()%0x3000000:next());
  point.y=scene[0x840/4]+(test&1?(s32)next()%0x3000000:next());
  timer[1]=0;timer[0]=0;timer[1]=0x80;
  expected=nativeVolume(scene,&point);nativeCycles+=timer[0];timer[1]=0;
  timer[0]=0;timer[1]=0x80;
  actual=sound_getDistanceVolume(scene,&point);optimizedCycles+=timer[0];timer[1]=0;
  if(actual!=expected) {result[0]=0xbad00001;result[1]=test;result[2]=expected;result[3]=actual;for(;;){}}
 }
 result[0]=0x600dc0de;result[1]=131072;result[2]=nativeCycles;result[3]=optimizedCycles;for(;;){}
}
''')
lib=subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0]
commands=[
 ['arm-none-eabi-objcopy','--redefine-sym','sound_getDistanceVolume=nativeVolume',str(root/'build/src/soundQueries.o'),str(a.results/'native.o')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 *[['arm-none-eabi-gcc','-mcpu=arm7tdmi','-m'+a.mode if name=='optimized.o' else '-mthumb','-mthumb-interwork','-O3','-std=gnu89','-fwrapv','-fno-strict-aliasing','-ffreestanding','-fno-builtin','-I'+str(root/'src'),'-I'+str(root/'include'),'-I'+str(root/'lib'),'-c',str(src),'-o',str(a.results/name)] for src,name in [(a.results/'main.c','main.o'),(root/'src/soundQueries.c','optimized.o')]],
 ['arm-none-eabi-ld','-T',str(root/'tools/ring-poc/link.ld'),'-o',str(a.results/'volume.elf'),*[str(a.results/k) for k in ('start.o','main.o','native.o','optimized.o')],'-L'+lib,'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'volume.elf'),str(a.results/'volume.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'volume.gba'),'-p','-tSOUNDVOLUME','--silent']]
for c in commands:subprocess.run(c,cwd=root,check=True,capture_output=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'volume.gba'),'--bios',str(a.bios),'--players','1','--frames','4000','--dump','0x02000000'],capture_output=True,text=True,timeout=600)
(a.results/'result.json').write_text(proc.stdout);assert proc.returncode==0,proc.stderr
d=json.loads(proc.stdout);values=d['nodes'][0]['dump'];assert values[:2]==[0x600dc0de,131072],d
print(f'PASS sound volume: native and modern {a.mode} C match 131072 signed/wrapped position cases; mean cycles native {values[2]/131072:.1f}, modern {values[3]/131072:.1f}',flush=True)
