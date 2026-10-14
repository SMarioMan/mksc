#!/usr/bin/env python3
"""Compare ARM collision code with the exact matching native Thumb object."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','bios','results'):p.add_argument('--'+k,required=True,type=Path)
p.add_argument('--mode',choices=('thumb','arm'),default='thumb')
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
source=r'''
#include "common.h"
#include "scene.h"
#include "collision.h"
SceneState gSceneState;
u8 col_sCpoiStartIndicesActive[128],col_sCpoiStartIndicesPassive[128];
static Actor nativeActors[88], optimizedActors[88];
static CollisionList nativeList, optimizedList;
static u32 nativeLog[16384],optimizedLog[16384],logCount,randomState;
static int variant;
void native_col_collide(CollisionList*);
void* memcpy(void* dst,const void* src,unsigned n) {u8*d=dst;const u8*s=src;while(n--)*d++=*s++;return dst;}
static u32 random32(void) {randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
static void onCollision(Actor* a,Actor* b) {
 u32* log=variant?optimizedLog:nativeLog;
 if(logCount>=16384)for(;;){}
 log[logCount++]=a->field_30|b->field_30<<8;
 /* Change geometry/filter state: later pairs must observe these writes. */
 a->posX+=(b->field_30-a->field_30)*4096;
 a->posY^=(b->field_30&3)<<16;
 if((a->field_30+b->field_30)%11==0)a->field_38=2;
}
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 int test,i,j,nA,nP;u32 nativeCount;
 randomState=0x6124379b;
 for(test=0;test<4096;test++) {
  result[5]=test;
  nA=test%25;nP=(test/25)%65;
  nativeList.activeCount=optimizedList.activeCount=nA;
  nativeList.passiveCount=optimizedList.passiveCount=nP;
  gSceneState.raceState.nrCheckpoints=1+(test%128);
  gSceneState.raceState.curRaceStateUnknown=test&1?RACE_CUR_RACE_STATE_UNKNOWN_MG_MODE:0;
  for(i=0;i<nA+nP;i++) {
   Actor* a=&nativeActors[i];
   for(j=0;j<sizeof(Actor)/4;j++)((u32*)a)[j]=0;
   a->posX=(s32)random32()%(1<<22);a->posY=(s32)random32()%(1<<22);a->posZ=(s32)random32()%(1<<21);
   if(!(test&31))a->posX=random32();
   a->sizeX=random32()%256;a->sizeY=random32()%256;a->sizeZ=random32()%256;
   a->checkpointId=random32()%gSceneState.raceState.nrCheckpoints;
   a->field_30=i;a->field_38=random32()%3;a->collideFunc=(random32()&3)?onCollision:0;
   optimizedActors[i]=*a;
   if(i<nA) {nativeList.activeList[i]=a;optimizedList.activeList[i]=&optimizedActors[i];}
   else {nativeList.passiveList[i-nA]=a;optimizedList.passiveList[i-nA]=&optimizedActors[i];}
  }
  variant=0;logCount=0;native_col_collide(&nativeList);nativeCount=logCount;
  variant=1;logCount=0;col_collide(&optimizedList);
  if(logCount!=nativeCount)goto bad;
  for(i=0;i<logCount;i++)if(nativeLog[i]!=optimizedLog[i])goto bad;
  for(i=0;i<nA+nP;i++)for(j=0;j<sizeof(Actor)/4;j++)if(((u32*)&nativeActors[i])[j]!=((u32*)&optimizedActors[i])[j])goto bad;
  for(i=0;i<nA;i++)if(nativeList.activeList[i]-nativeActors!=optimizedList.activeList[i]-optimizedActors)goto bad;
  for(i=0;i<nP;i++)if(nativeList.passiveList[i]-nativeActors!=optimizedList.passiveList[i]-optimizedActors)goto bad;
 }
 result[0]=0x600dc0de;result[1]=4096;for(;;){}
 bad:result[0]=0xbad00001;result[1]=test;result[2]=i;result[3]=j;result[4]=logCount;for(;;){}
}
'''
(a.results/'main.c').write_text(source)
rename=sum((['--redefine-sym',name+'=native_'+name] for name in ('col_collide','col_addActive','col_addPassive','col_clearLists')),[])
for cmd in [
 ['arm-none-eabi-objcopy',*rename,str(root/'build/src/collision.o'),str(a.results/'native.o')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 *[['arm-none-eabi-gcc','-mcpu=arm7tdmi','-m'+a.mode if name=='collision.o' else '-mthumb','-mthumb-interwork','-O3','-DMKSC_RING=1','-fwrapv','-fno-strict-aliasing','-ffreestanding','-fno-builtin','-I',str(root/'src'),'-I',str(root/'include'),'-I',str(root/'lib'),'-c',str(src),'-o',str(a.results/name)] for src,name in [(a.results/'main.c','main.o'),(root/'src/collision.c','collision.o')]],
 ['arm-none-eabi-ld','-T',str(root/'tools/ring-poc/link.ld'),'-o',str(a.results/'collision.elf'),*[str(a.results/k) for k in ('start.o','native.o','main.o','collision.o')],'-L'+subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0],'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'collision.elf'),str(a.results/'collision.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'collision.gba'),'-p','-tCOLLISION','-cCOLL','--silent']]:subprocess.run(cmd,cwd=root,check=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'collision.gba'),'--bios',str(a.bios),'--players','1','--frames','40000','--dump','0x02000000'],capture_output=True,text=True,timeout=600)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0,(proc.returncode,proc.stderr)
d=json.loads(proc.stdout);assert d['nodes'][0]['dump'][:2]==[0x600dc0de,4096],d
print('PASS collision: exact native Thumb object and '+a.mode+' C match callback order, actor bytes and checkpoint ordering across 4096 fixtures and full 24/64-list limits',flush=True)
