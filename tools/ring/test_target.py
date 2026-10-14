#!/usr/bin/env python3
"""Compare full driver animation state against the native Thumb target routine."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','bios','results'):p.add_argument('--'+k,required=True,type=Path)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
def extract(file,start,end):
 s=(root/file).read_text();return s[s.index(start):s.index(end)]
native=extract('asm/text08033f14.s','\tthumb_func_start ring_stockTargetResponse','\tthumb_func_start sub_0803D17C')
# The extraction starts inside the variant's conditional symbol declaration.
native=native.replace('\t.else\n\tthumb_func_start race_updateNearbyTargets\nrace_updateNearbyTargets: @ 0x0803D00C\n\t.endif\n','')
native+=extract('asm/text08033f14.s','\tthumb_func_start sub_803CA78','\tthumb_func_start sub_803CBE4')
native+=extract('asm/text08033f14.s','\tthumb_func_start sub_803D908','\tthumb_func_start sub_803D938')
native+=extract('asm/text0802F5C0.s','\tthumb_func_start sub_802F5F4','\tthumb_func_start sub_802F638')
native+=extract('asm/text08031468.s','\tthumb_func_start sub_8032C74','\tthumb_func_start sub_8032CA0')
(a.results/'native.s').write_text('.include "asm/macros.inc"\n.syntax unified\n.text\n'+native)
# Exercise the actual native animation writer behind its 8/9 dispatcher.
# Include the actual native camera query and vary its camera/driver state.
helper=extract('asm/text08051f84.s','\tthumb_func_start ring_stockLookAsideAnimation','\tthumb_func_start sub_805AAF0')
helper=helper.replace('\t.else\n\tthumb_func_start driver_requestLookAsideAnimation\ndriver_requestLookAsideAnimation: @ 0x0805AA6C\n\t.endif\n','')
(a.results/'native.s').write_text('.include "asm/macros.inc"\n.syntax unified\n.text\n'+native+helper)
data=(root/'baserom.gba').read_bytes()
tables='\n'.join('const u8 '+name+'[15]={'+','.join(str(v) for v in data[offset:offset+15])+'};' for name,offset in (('_080EC846',0xEC846),('_080EC855',0xEC855)))
source=r'''
#include "common.h"
#include "scene.h"
static u32 nativeDrivers[8][104], optimizedDrivers[8][104], randomState=0x729edc42;
const u8 _080F2C14[2]={8,17};
void ring_stockTargetResponse(Driver*);
void race_updateNearbyTargets(Driver*);
void ring_stockLookAsideAnimation(Driver*,int);
bool32 ring_active(void) { return 1; }
void ring_serviceAudio(void) {}
void ring_serviceRenderAudio(void) {}

void sub_803D214(Driver* p, int event) {
 if(((u8*)p)[0x4c]&8)return;
 if(event==8||event==9)ring_stockLookAsideAnimation(p,event==9);
}
static u32 rand32(void) { randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState; }
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 const u16 scales[6]={0,255,256,511,512,65535};
 const u8 facing[4]={0,1,126,127};
 int test,i,j,k,n;
 for(test=0;test<16384;test++) {
  result[5]=test;
  n=1+(test&7);
  *(s16*)((u8*)&gSceneState+0x85c)=rand32();
  *(s16*)((u8*)&gSceneState+0xbda)=rand32();
  gSceneState.raceState.nrCheckpoints=1+(test%127);
  gSceneState.raceState.driverCapacity=n;
  for(i=0;i<n;i++) {
   u8* d=(u8*)nativeDrivers[i];
   for(j=0;j<104;j++)nativeDrivers[i][j]=rand32();
   *(s16*)(d+0x1e)=rand32(); *(s16*)(d+0x20)=rand32();
   *(u16*)(d+0x24)=scales[(test+i)%6];
   d[0x56]=facing[(test+i)&3];
   d[0x6e]=(d[0x6e]&0xf0)|((test>>4)&15);
   d[0x90]=(test+i%3)&127;
   for(j=0;j<104;j++)optimizedDrivers[i][j]=nativeDrivers[i][j];
  }
  for(i=0;i<n;i++) {
   for(j=0;j<n;j++)gSceneState.raceState.drivers[j]=(Driver*)nativeDrivers[j];
   gSceneState.raceState.playerDriver=(Driver*)nativeDrivers[test%n];
   ring_stockTargetResponse((Driver*)nativeDrivers[i]);
   for(j=0;j<n;j++)gSceneState.raceState.drivers[j]=(Driver*)optimizedDrivers[j];
   gSceneState.raceState.playerDriver=(Driver*)optimizedDrivers[test%n];
   race_updateNearbyTargets((Driver*)optimizedDrivers[i]);
   for(j=0;j<n;j++)for(k=0;k<104;k++)if(nativeDrivers[j][k]!=optimizedDrivers[j][k]) {
    result[0]=0xbad00001;result[1]=test;result[2]=i;result[3]=j;result[4]=k;return 0;
   }
  }
 }
 result[0]=0x600dc0de;result[1]=16384;
 for(;;) {}
}
'''
(a.results/'main.c').write_text(source+tables)
(a.results/'link.ld').write_text((root/'tools/ring-poc/link.ld').read_text()+'\ngSceneState = 0x03002E20;\n')
for cmd in [
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-I',str(root),'-I',str(root/'include'),'-o',str(a.results/'native.o'),str(a.results/'native.s')],
 *[['arm-none-eabi-gcc','-mcpu=arm7tdmi','-marm' if name=='target.o' else '-mthumb','-mthumb-interwork','-O3','-fwrapv','-ffreestanding','-fno-builtin','-DMKSC_RING=1','-I',str(root/'src'),'-I',str(root/'include'),'-I',str(root/'lib'),'-c',str(src),'-o',str(a.results/name)] for src,name in [(a.results/'main.c','main.o'),(root/'src/ringTarget.O3.c','target.o')]],
 ['arm-none-eabi-ld','-T',str(a.results/'link.ld'),'-o',str(a.results/'target.elf'),*[str(a.results/k) for k in ('start.o','native.o','main.o','target.o')],'-L'+subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0],'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'target.elf'),str(a.results/'target.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'target.gba'),'-p','-tTARGETTEST','-cTARG','--silent']]:subprocess.run(cmd,cwd=root,check=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'target.gba'),'--bios',str(a.bios),'--players','1','--frames','40000','--dump','0x02000000'],capture_output=True,text=True,timeout=900)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0,(proc.returncode,proc.stderr)
d=json.loads(proc.stdout);assert d['nodes'][0]['dump'][:2]==[0x600dc0de,16384],d
print('PASS target state: native Thumb and optimized C match all driver bytes across 16384 fixtures, 1–8 racers, animation gates, signed depth and wrap boundaries')
