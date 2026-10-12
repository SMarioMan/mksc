#!/usr/bin/env python3
"""Compare optimized target responses with the actual native Thumb routine."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','bios','results'):p.add_argument('--'+k,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
def extract(file,start,end):
 s=(root/file).read_text();return s[s.index(start):s.index(end)]
native=extract('asm/text08033f14.s','\tthumb_func_start ring_stockTargetResponse','\tthumb_func_start sub_0803D17C')
# The extraction starts inside the variant's conditional symbol declaration.
native=native.replace('\t.else\n\tthumb_func_start sub_803D00C\nsub_803D00C: @ 0x0803D00C\n\t.endif\n','')
native+=extract('asm/text0802F5C0.s','\tthumb_func_start sub_802F5F4','\tthumb_func_start sub_802F638')
native+=extract('asm/text08031468.s','\tthumb_func_start sub_8032C74','\tthumb_func_start sub_8032CA0')
(a.results/'native.s').write_text('.include "asm/macros.inc"\n.syntax unified\n.text\n'+native)
source=r'''
#include "common.h"
#include "scene.h"
static u32 drivers[8][104], log[16], want[16], used, randomState=0x729edc42;
void ring_stockTargetResponse(Driver*);
void sub_803D00C(Driver*);
bool32 ring_active(void) { return 1; }
void sub_803D214(Driver* p, int event) { log[used++]=((u32)p-(u32)drivers)/sizeof(drivers[0])*16+event; }
static u32 rand32(void) { randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState; }
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 int test,i,j,k,n;
 for(test=0;test<4096;test++) {
  n=1+(test&7);
  gSceneState.raceState.nrCheckpoints=1+(test%127);
  gSceneState.raceState.driverCapacity=n;
  for(i=0;i<n;i++) {
   u8* d=(u8*)drivers[i];
   *(s16*)(d+0x1e)=rand32(); *(s16*)(d+0x20)=rand32();
   d[0x90]=(test+i%3)&127;
   gSceneState.raceState.drivers[i]=(Driver*)drivers[i];
  }
  for(i=0;i<n;i++) {
   used=0; ring_stockTargetResponse((Driver*)drivers[i]);k=used;
   for(j=0;j<k;j++)want[j]=log[j];
   used=0;sub_803D00C((Driver*)drivers[i]);
   if(used!=k) {result[0]=0xbad00001;result[1]=test;result[2]=i;return 0;}
   for(j=0;j<k;j++)if(want[j]!=log[j]) {result[0]=0xbad00002;result[1]=test;result[2]=i;return 0;}
  }
 }
 result[0]=0x600dc0de;result[1]=4096;
 for(;;) {}
}
'''
(a.results/'main.c').write_text(source)
(a.results/'link.ld').write_text((root/'tools/ring-poc/link.ld').read_text()+'\ngSceneState = 0x03002E20;\n')
for cmd in [
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-I',str(root),'-I',str(root/'include'),'-o',str(a.results/'native.o'),str(a.results/'native.s')],
 *[['arm-none-eabi-gcc','-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-fwrapv','-ffreestanding','-fno-builtin','-DMKSC_RING=1','-I',str(root/'src'),'-I',str(root/'include'),'-I',str(root/'lib'),'-c',str(src),'-o',str(a.results/name)] for src,name in [(a.results/'main.c','main.o'),(root/'src/ringTarget.O3.c','target.o')]],
 ['arm-none-eabi-ld','-T',str(a.results/'link.ld'),'-o',str(a.results/'target.elf'),*[str(a.results/k) for k in ('start.o','native.o','main.o','target.o')],'-L'+subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0],'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'target.elf'),str(a.results/'target.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'target.gba'),'-p','-tTARGETTEST','-cTARG','--silent']]:subprocess.run(cmd,cwd=root,check=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'target.gba'),'--bios',str(a.bios),'--players','1','--frames','4000','--dump','0x02000000'],capture_output=True,text=True,timeout=120)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0,(proc.returncode,proc.stderr)
d=json.loads(proc.stdout);assert d['nodes'][0]['dump'][:2]==[0x600dc0de,4096],d
print('PASS target responses: native Thumb and optimized C agree across 4096 fixtures, all 1–8 racer counts and signed depth/wrap boundaries')
