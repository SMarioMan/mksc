#!/usr/bin/env python3
"""Compare native and optimized look-aside animation writes on the real BIOS."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','bios','results'):p.add_argument('--'+k,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
s=(root/'asm/text08051f84.s').read_text();s=s[s.index('\tthumb_func_start ring_stockLookAsideAnimation'):s.index('\tthumb_func_start sub_805AAF0')]
s=s.replace('\t.else\n\tthumb_func_start driver_requestLookAsideAnimation\ndriver_requestLookAsideAnimation: @ 0x0805AA6C\n\t.endif\n','')
(a.results/'native.s').write_text('.include "asm/macros.inc"\n.syntax unified\n.text\n'+s)
source=r'''
#include "common.h"
#include "scene.h"
static u32 nativeDriver[104], optimizedDriver[104], randomState;
const u8 _080F2C14[2]={8,17};
void ring_stockLookAsideAnimation(Driver*,int);
void driver_requestLookAsideAnimation(Driver*,int);
bool32 ring_active(void) { return 1; }
void ring_serviceAudio(void) {}
void ring_serviceRenderAudio(void) {}
void ring_stockTargetResponse(Driver* p) {}
void sub_803D214(Driver* p,int r) {}
u8 sub_803CA78(Driver* p) { return ((u8*)p)[0x56]&127; }
static u32 next(void) { randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState; }
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 const u16 scales[6]={0,255,256,511,512,65535};
 const u8 facing[4]={0,1,126,127};
 int test,i,right;
 randomState=0x8dff34a1;
 for(test=0;test<16384;test++) {
  for(i=0;i<104;i++)nativeDriver[i]=next();
  ((u8*)nativeDriver)[0x6e]=(((u8*)nativeDriver)[0x6e]&0xf0)|(test&15);
  *(u16*)((u8*)nativeDriver+0x24)=scales[(test>>4)%6];
  ((u8*)nativeDriver)[0x56]=facing[(test>>7)&3];
  for(i=0;i<104;i++)optimizedDriver[i]=nativeDriver[i];
  right=(test>>9)&3;
  ring_stockLookAsideAnimation((Driver*)nativeDriver,right);
  driver_requestLookAsideAnimation((Driver*)optimizedDriver,right);
  for(i=0;i<104;i++)if(nativeDriver[i]!=optimizedDriver[i]) {result[0]=0xbad00001;result[1]=test;result[2]=i;return 0;}
 }
 result[0]=0x600dc0de;result[1]=16384;
 for(;;) {}
}
'''
(a.results/'main.c').write_text(source)
(a.results/'link.ld').write_text((root/'tools/ring-poc/link.ld').read_text()+'\ngSceneState = 0x03002E20;\n')
for cmd in [
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-I',str(root),'-I',str(root/'include'),'-o',str(a.results/'native.o'),str(a.results/'native.s')],
 *[['arm-none-eabi-gcc','-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-fwrapv','-ffreestanding','-fno-builtin','-DMKSC_RING=1','-I',str(root/'src'),'-I',str(root/'include'),'-I',str(root/'lib'),'-c',str(src),'-o',str(a.results/name)] for src,name in [(a.results/'main.c','main.o'),(root/'src/ringTarget.O3.c','target.o')]],
 ['arm-none-eabi-ld','-T',str(a.results/'link.ld'),'-o',str(a.results/'animation.elf'),*[str(a.results/k) for k in ('start.o','native.o','main.o','target.o')],'-L'+subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0],'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'animation.elf'),str(a.results/'animation.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'animation.gba'),'-p','-tANIMTEST','-cANIM','--silent']]:subprocess.run(cmd,cwd=root,check=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'animation.gba'),'--bios',str(a.bios),'--players','1','--frames','1500','--dump','0x02000000'],capture_output=True,text=True,timeout=300)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0,(proc.returncode,proc.stderr)
d=json.loads(proc.stdout);assert d['nodes'][0]['dump'][:2]==[0x600dc0de,16384],d
print('PASS: native Thumb and optimized C animation writes match across 16384 fixtures, all animation gates, facing and scale boundaries',flush=True)
