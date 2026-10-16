#!/usr/bin/env python3
"""Check that player indicators preserve native OBJ graphics and kart palettes.

A detached BIOS-backed test exercises allocation/restoration across random
native OAM layouts. Gameplay tests separately cover the actual race pipeline.
"""
import argparse
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
for key in ('runner','bios','results'):p.add_argument('--'+key,required=True,type=lambda value:Path(value).resolve())
a=p.parse_args();root=Path(__file__).resolve().parents[2];a.results.mkdir(parents=True,exist_ok=True)
source=r'''
#include "ringIdentity.O3.c"
u16 ring_identityBackup[24][64],ring_identityTiles[24];
u32 ring_identityAllocated,ring_identityOverflows,ring_identityNative8bpp;
u32 ring_identityPublishLines,ring_identityRestoreLines;
u16 ring_identityOldTiles[24];
u32 ring_identityPendingRestore,ring_identityPendingUpload;
u32 ring_identityResident[16];
u32 ring_identityFirstTile;
u32 ring_identityUploads,ring_identitySlowPrepares;
volatile BgAffineDstData BgAffineBuf[2][96];
void* memset(void* dst,int value,unsigned count){volatile u8* p=dst;while(count--)*p++=value;return dst;}
static DmaQueue queue;
static DmaQueueEntry update;
u16* oam_ringBuffer(void){return (u16*)OAM;}
unsigned oam_ringObjectCount(void){return 64;}
DmaQueue* dmaq_getVBlankDmaQueue(void){return &queue;}
static u16 palette[256],originalPalette[256];
static u16 originalOam[512];
static u32 randomState;
static u32 random32(void){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
u16* pltt_getBuffer(int buffer){return palette;}
void pltt_setDirtyFlag(bool32 dirty){}
void ring_publishNativeMenuPalette(void){}
bool32 ring_showRaceIdentity(void){return TRUE;}
u32 ring_identityWanted(void){return 0xFFFFFF;}
int main(void){
 volatile u32* result=(volatile u32*)0x02000000;
 vu16* oam=(vu16*)OAM;vu16* vram=(vu16*)OBJ_VRAM0;
 u16* before=(u16*)0x02008000;
 int test,i,j,index,kind,id,nativeCount=32;
 randomState=0x351ea7d9;
 REG_DISPCNT=0x40;
 for(test=0;test<128;test++){
  for(i=0;i<0x4000;i++)before[i]=vram[i]=random32();
  for(i=0;i<256;i++)originalPalette[i]=palette[i]=((vu16*)OBJ_PLTT)[i]=random32();
  for(i=0;i<128;i++){oam[i*4]=0x200;oam[i*4+1]=oam[i*4+2]=oam[i*4+3]=0;}
  for(i=0;i<nativeCount;i++){
   /* Eight full-resolution kart slots plus varied native world sprites. */
   oam[i*4]=(random32()&255);oam[i*4+1]=(random32()&511)|(i<8?0xC000:0x4000);
   oam[i*4+2]=i<8?0x200+i*64:((random32()%120)*4);
  }
  /* P8 kart subtiles can use a virtual-looking tile number with native bank 15. */
  oam[7*4+1]=0x4000;oam[7*4+2]=0xF3E0;
  for(kind=0;kind<4;kind++)for(id=0;id<8;id++){
   index=nativeCount+kind*8+id;
   oam[index*4]=kind==0?0x40F6:kind==1?0x80D7:kind==2?0x8000:7;
   oam[index*4+1]=kind==0?0x1F8:kind==1?0x1FC:kind==2?0:7;
   oam[index*4+2]=(0x3E0+kind*8+id)|(id<<12);
  }
  for(i=0;i<512;i++)originalOam[i]=oam[i];
  ring_prepareIdentityGraphics();
  ring_beginIdentityPublication();
  ring_publishIdentityPalette();
  if(ring_identityOverflows||ring_identityNative8bpp){result[0]=0xbad10001;goto fail;}
  for(i=0;i<nativeCount*4;i++)if(oam[i]!=originalOam[i]){result[0]=0xbad10002;goto fail;}
  for(i=0;i<256;i++)if(palette[i]!=originalPalette[i]||((i&15)&&((vu16*)OBJ_PLTT)[i]!=originalPalette[i])){result[0]=0xbad10003;goto fail;}
  for(i=0;i<nativeCount;i++){
   int tile=originalOam[i*4+2]&1023,count=i<7?64:4;
   for(j=tile*16;j<(tile+count)*16;j++)if(vram[j]!=before[j]){result[0]=0xbad10004;goto fail;}
  }
  for(kind=0;kind<4;kind++)for(id=0;id<8;id++){
   int tile,count=kind==3?32:64,art=kind==0?id:kind==3?16+id:8+id;
   index=nativeCount+kind*8+id;tile=oam[index*4+2]&1023;
   if(!(oam[index*4]&0x2000)||(tile&1)){result[0]=0xbad10005;goto fail;}
   for(j=0;j<count;j++)if(vram[tile*16+j]!=sIdentityArt[art][j]){result[0]=0xbad10006;goto fail;}
  }
  /* Stable frames retain their atlas without any texture transfers. */
  for(i=0;i<512;i++)oam[i]=originalOam[i];
  ring_prepareIdentityGraphics();
  if(ring_identityPendingRestore||ring_identityPendingUpload){result[0]=0xbad10009;goto fail;}
  ring_beginIdentityPublication();ring_publishIdentityPalette();
  /* A late native sprite claims a resident slot after earlier identities
   * have already been patched. Retry allocation without losing their tags. */
  {
   unsigned oldTile=ring_identityTiles[0];
   for(i=0;i<512;i++)oam[i]=originalOam[i];
   oam[63*4]=0;oam[63*4+1]=0;oam[63*4+2]=oldTile|0xF000;
   ring_prepareIdentityGraphics();
   if(!(ring_identityPendingRestore&1)||ring_identityTiles[0]==oldTile){result[0]=0xbad1000b;goto fail;}
   ring_beginIdentityPublication();ring_publishIdentityPalette();
   for(j=oldTile*16;j<oldTile*16+16;j++)if(vram[j]!=before[j]){result[0]=0xbad1000c;goto fail;}
   for(i=32;i<63;i++)if(!(oam[i*4]&0x2000)){result[0]=0xbad1000d;goto fail;}
  }
  /* A native animation must reclaim its VRAM without losing new DMA data. */
  {
   unsigned oldTile=ring_identityTiles[0];
   for(i=0;i<512;i++)oam[i]=originalOam[i];
   update.dst=(void*)(OBJ_VRAM0+oldTile*32);update.config=0x80000040;update.link.next=0;
   queue.list.head=&update.link;
   ring_prepareIdentityGraphics();
   if(!(ring_identityPendingRestore&1)||ring_identityTiles[0]==oldTile){result[0]=0xbad1000a;goto fail;}
   ring_beginIdentityPublication();
   for(j=oldTile*16;j<oldTile*16+64;j++)before[j]=vram[j]=random32();
   ring_publishIdentityPalette();queue.list.head=0;
  }
  ring_restoreIdentityTiles();
  if(ring_identityAllocated){result[0]=0xbad10007;goto fail;}
  for(i=0;i<0x4000;i++)if(vram[i]!=before[i]){result[0]=0xbad10008;goto fail;}
 }
 result[0]=0x600dc0de;result[1]=128;for(;;){}
 fail:result[1]=test;result[2]=i;result[3]=j;result[4]=index;for(;;){}
}
'''
(a.results/'main.c').write_text(source)
(a.results/'bios.s').write_text('.include "asm/macros.inc"\n.syntax unified\n.text\nthumb_func_start CpuFastSet\nCpuFastSet:\n swi 12\n bx lr\nthumb_func_start VBlankIntrWait\nVBlankIntrWait:\n swi 5\n bx lr\n')
lib=subprocess.check_output(["arm-none-eabi-gcc","-print-libgcc-file-name"],text=True).strip().rsplit("/",1)[0]
commands=[
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-I',str(root),'-o',str(a.results/'bios.o'),str(a.results/'bios.s')],
 ['arm-none-eabi-gcc','-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-std=gnu89','-fwrapv','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-ffreestanding','-fno-builtin','-DMKSC_RING=1','-I'+str(root/'src'),'-I'+str(root/'include'),'-I'+str(root/'lib'),'-c',str(a.results/'main.c'),'-o',str(a.results/'main.o')],
 ['arm-none-eabi-ld','--gc-sections','-T',str(root/'tools/ring-poc/link.ld'),'-o',str(a.results/'identity.elf'),*[str(a.results/k) for k in ('start.o','main.o','bios.o')],'-L'+lib,'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'identity.elf'),str(a.results/'identity.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'identity.gba'),'-p','-tIDENTITYCHECK','--silent']]
for command in commands:
 proc=subprocess.run(command,capture_output=True,text=True,cwd=root)
 assert proc.returncode==0,(command,proc.stderr)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'identity.gba'),'--bios',str(a.bios),'--players','1','--frames','4000','--dump','0x02000000','--dump-words','5'],capture_output=True,text=True,timeout=300)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0 and not proc.stderr,proc.stderr
result=json.loads(proc.stdout)
assert result['nodes'][0]['dump'][:2]==[0x600dc0de,128],result['nodes'][0]['dump']
print('PASS identity atlas: 128 native OAM layouts; native sprites, kart textures and all opaque palette colors preserved; stable frames reuse their atlas, native DMA evicts entries safely, and every borrowed VRAM tile is restored exactly')
