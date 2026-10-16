#!/usr/bin/env python3
"""Compare full OAM buffers and affine state with the original renderer."""
import argparse, json, subprocess
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'bios', 'results'): p.add_argument('--'+key, required=True, type=Path)
a = p.parse_args(); root = Path(__file__).resolve().parents[2]
a.results.mkdir(parents=True, exist_ok=True)
source = r'''
#include "oam.O3.c"
u8 ring_balloonAffineIndex[8];
OamCellListEntry* ring_sOamCells;
static struct {u32 before;OamCellListEntry cells[96];u32 after;} expandedQueue;
void ring_allocateSpriteQueue(void) { ring_sOamCells = expandedQueue.cells; }
static u16 queuedTiles[128][4];
int gSinTable[1024];
void* memset(void* dst,int value,unsigned count) { volatile u8* at=dst;while(count--)*at++=value;return dst; }
bool32 ring_active(void) { return TRUE; }
void ring_serviceAudio(void) {}
void ring_serviceRenderAudio(void) {}
void ring_drawNativeMenuCursors(void) {}
s32 ring_spriteDivide(s32 n, s32 d) { return Div(n,d); }
static u32 state;
static u32 random32(void) { state^=state<<13;state^=state>>17;state^=state<<5;return state; }
static u16 before[512], expected[512];
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 int test,i,offset,affine,nextOffset,nextAffine,nativeResult,actualResult;
 state=0x852974b3;
 /* Reset must hide all objects while preserving tile and affine words. */
 for(i=0;i<512;i++) before[i]=sBuffer[i]=random32();
 oam_3002188=1;oam_300218C=sCopyDisabled=0;
 oam_reset();
 for(i=0;i<512;i++) {
  u16 wanted=(i%4<2)?(i%4?0:0x200):before[i];
  if(sBuffer[i]!=wanted) {result[0]=0xbad00006;result[1]=i;for(;;){}}
 }
 for(test=0;test<16384;test++) {
  u16 data[4]; OamCell original, actual;
  result[5]=test;
  data[0]=1; data[1]=(random32()&0x3fff)|((test%3)<<14);
  data[2]=random32();data[3]=random32();
  if(!(test&3)) { data[1]=248;data[2]=0x41f8; }
  original.cellData=data;original.offsetX=random32();original.offsetY=random32();
  original.attr01Bits=0;original.attr01Mask=-1;original.attr2Bits=0;original.attr2Mask=-1;
  if(test&1) {
   original.attr01Bits=random32();original.attr01Mask=random32();
   original.attr2Bits=random32();original.attr2Mask=random32();
  }
  original.affinePa=random32();original.affinePb=random32();original.affinePc=random32();original.affinePd=random32();
  original.scaleX=32+random32()%4065;original.scaleY=32+random32()%4065;
  actual=original;
  offset=(test%129)*4;affine=(test/129)%33;
  for(i=0;i<512;i++)before[i]=random32();
  for(i=0;i<512;i++)sBuffer[i]=before[i];
  sBufferOffset=offset;sNextAffineId=affine;oam_300218C=1;
  nativeResult=ring_stockRenderCell(&original);
  for(i=0;i<512;i++)expected[i]=sBuffer[i];
  nextOffset=sBufferOffset;nextAffine=sNextAffineId;
  for(i=0;i<512;i++)sBuffer[i]=before[i];
  for(i=0;i<8;i++)ring_balloonAffineIndex[i]=255;
  sBufferOffset=offset;sNextAffineId=affine;oam_300218C=1;
  actualResult=ring_renderSingleScaledCell(&actual,0,!(test&1));
  if(sBufferOffset!=nextOffset||sNextAffineId!=nextAffine||actualResult!=nativeResult)goto bad;
  for(i=0;i<512;i++)if(sBuffer[i]!=expected[i])goto bad;
  for(i=0;i<sizeof(OamCell)/4;i++)if(((u32*)&original)[i]!=((u32*)&actual)[i])goto bad;
 }
 /* The lean queue path must populate exactly the same entry as the native
  * general path, including masks, wrapped priority and zero-scale fallback. */
 for(test=0;test<4096;test++) {
  u16 data[4]; Vec2s16 position; struc_6 priority; OamCellListEntry wanted;
  int scaleX=random32()&2047,scaleY=random32()&2047;
  data[0]=1;data[1]=random32();data[2]=random32();data[3]=random32();
  position.x=random32();position.y=random32();
  priority.field0=random32();priority.priority=random32();
  if(!(test&15))scaleX=0;
  if(!(test&31))scaleY=0;
  ring_sOamCells=0;sCellListEntryCount=0;sCellListHead=sCellListTail=0;
  oam_renderCellDataGeneral(data,&position,scaleX,scaleY,0,&priority);
  for(i=0;i<sizeof(wanted)/4;i++)((volatile u32*)&wanted)[i]=((u32*)&sCellListEntries[0])[i];
  sCellListEntryCount=0;sCellListHead=sCellListTail=0;
  oam_renderCellData(data,&position,scaleX,scaleY,0,&priority);
  for(i=0;i<sizeof(wanted)/4;i++)if(((u32*)&wanted)[i]!=((u32*)&sCellListEntries[0])[i]) {
   result[0]=0xbad00007;result[1]=test;result[2]=i;for(;;){}
  }
 }
 /* HUD cells must also avoid the general path without changing OAM. */
 for(test=0;test<4096;test++) {
  u16 data[16]; Vec2s16 position;
  int scaleX=(test&1)?256:0,scaleY=(test&2)?256:0;
  data[0]=test%6;
  for(i=1;i<16;i++)data[i]=random32();
  position.x=random32();position.y=random32();
  offset=test%130*4;
  for(i=0;i<512;i++)before[i]=sBuffer[i]=random32();
  sBufferOffset=offset;sNextAffineId=0;
  oam_renderCellDataGeneral(data,&position,scaleX,scaleY,0,0);
  nextOffset=sBufferOffset;
  for(i=0;i<512;i++)expected[i]=sBuffer[i];
  for(i=0;i<512;i++)sBuffer[i]=before[i];
  sBufferOffset=offset;sNextAffineId=0;
  oam_renderCellData(data,&position,scaleX,scaleY,0,0);
  if(sBufferOffset!=nextOffset) {result[0]=0xbad00008;result[1]=test;for(;;){}}
  for(i=0;i<512;i++)if(sBuffer[i]!=expected[i]) {
   result[0]=0xbad00009;result[1]=test;result[2]=i;for(;;){}
  }
 }
 /* Exercise the actual queued path beyond the original 32-cell limit. */
 for (test=0;test<2;test++) {
  Vec2s16 position={120,80}; struc_6 priority;
  u32 seen[4]={0,0,0,0}; int capacity=test?128:32;
  expandedQueue.before=0x13579bdf;expandedQueue.after=0x2468ace0;
  ring_sOamCells=test?expandedQueue.cells:0;
  sBufferOffset=sNextAffineId=sCellListEntryCount=0;
  sCellListHead=sCellListTail=0;sCopyDisabled=1;oam_3002188=1;oam_300218C=0;
  for(i=0;i<128;i++) {
   queuedTiles[i][0]=1;queuedTiles[i][1]=0;queuedTiles[i][2]=0;queuedTiles[i][3]=i;
   priority.field0=0;priority.priority=i;
   oam_renderCellData(queuedTiles[i],&position,256,256,0,&priority);
  }
  if(sCellListEntryCount!=capacity) {result[0]=0xbad00002;result[1]=test;for(;;){}}
  oam_update();
  if(expandedQueue.before!=0x13579bdf||expandedQueue.after!=0x2468ace0) {result[0]=0xbad00005;result[1]=test;for(;;){}}
  if(sBufferOffset!=capacity*4) {result[0]=0xbad00003;result[1]=test;for(;;){}}
  for(i=0;i<capacity;i++) {
   u32 tile=sBuffer[i*4+2]&1023;
   if(tile>=capacity || (seen[tile/32]&(1u<<(tile%32)))) {result[0]=0xbad00004;result[1]=test;for(;;){}}
   seen[tile/32]|=1u<<(tile%32);
  }
 }
 result[0]=0x600dc0de;result[1]=16384;for(;;){}
 bad:result[0]=0xbad00001;result[1]=test;result[2]=i;result[3]=sBufferOffset;result[4]=nextOffset;for(;;){}
}
'''
(a.results/'main.c').write_text(source)
(a.results/'div.s').write_text('.include "asm/macros.inc"\n.syntax unified\n.text\nthumb_func_start Div\nDiv:\n swi 6\n bx lr\n')
(a.results/'link.ld').write_text((root/'tools/ring-poc/link.ld').read_text().replace('*(.text*)', '*(.text*) *(.ring_irq*)'))
lib = subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0]
commands = [
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-I',str(root),'-o',str(a.results/'div.o'),str(a.results/'div.s')],
 ['arm-none-eabi-gcc','-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-std=gnu89','-fwrapv','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-ffreestanding','-fno-builtin','-DMKSC_RING=1','-Wa,--defsym,MKSC_RING=1','-I'+str(root/'src'),'-I'+str(root/'include'),'-I'+str(root/'lib'),'-I'+str(root),'-c',str(a.results/'main.c'),'-o',str(a.results/'main.o')],
 ['arm-none-eabi-ld','--gc-sections','-T',str(a.results/'link.ld'),'-o',str(a.results/'oam.elf'),*[str(a.results/k) for k in ('start.o','main.o','div.o')],'-L'+lib,'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'oam.elf'),str(a.results/'oam.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'oam.gba'),'-p','-tOAMCOMPARE','--silent'],
]
for cmd in commands:
 result=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
 assert result.returncode==0,(cmd,result.stderr)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'oam.gba'),'--bios',str(a.bios),'--players','1','--frames','8000','--dump','0x02000000','--dump-words','6'],capture_output=True,text=True,timeout=600)
(a.results/'result.json').write_text(proc.stdout)
assert proc.returncode==0,proc.stderr
data=json.loads(proc.stdout);assert data['nodes'][0]['dump'][:2]==[0x600dc0de,16384],data
print('PASS OAM cells: all 1024 OAM bytes, cell mutations, affine allocation and return values match original assembly across 16384 varied cells and default/custom masks; 4096 queued and 4096 direct HUD cells match the general path; native 32-cell and expanded 128-cell queues preserve every sprite',flush=True)
