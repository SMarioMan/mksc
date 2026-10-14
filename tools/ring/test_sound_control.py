#!/usr/bin/env python3
"""Compare mixer controls with exact native instructions and full track state."""
import argparse, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','bios','results'):p.add_argument('--'+k,required=True,type=Path)
a=p.parse_args();root=Path(__file__).resolve().parents[2];a.results.mkdir(parents=True,exist_ok=True)
native=root/'build/src/mp2000/m4a.o'
symbols={}
for line in subprocess.check_output(['arm-none-eabi-nm','-S',str(native)],text=True).splitlines():
 f=line.split()
 if len(f)==4:symbols[f[3]]=(int(f[0],16),int(f[1],16))
subprocess.run(['arm-none-eabi-objcopy','-O','binary','--only-section=.text',str(native),str(a.results/'native-text.bin')],check=True)
text=(a.results/'native-text.bin').read_bytes();assembly='.include "asm/macros.inc"\n.syntax unified\n.text\n'
for name in ('m4aMPlayVolumeControl','m4aMPlayPitchControl'):
 offset,size=symbols[name];blob=a.results/(name+'.bin');blob.write_bytes(text[offset:offset+size])
 assembly+=f'thumb_func_start native_{name}\nnative_{name}:\n.incbin "{blob}"\n'
(a.results/'native.s').write_text(assembly)
(a.results/'main.c').write_text(r'''
#include "common.h"
#include "gba/m4a_internal.h"
void* memcpy(void* destination,const void* source,unsigned bytes) {u8*d=destination;const u8*s=source;while(bytes--)*d++=*s++;return destination;}
void native_m4aMPlayVolumeControl(struct MusicPlayerInfo*,u16,u16);
void native_m4aMPlayPitchControl(struct MusicPlayerInfo*,u16,s16);
static struct MusicPlayerInfo expectedPlayer,actualPlayer;
static struct MusicPlayerTrack expectedTracks[16],actualTracks[16];
static u32 state;
static u32 next(void) {state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 volatile u16* timer=(volatile u16*)0x0400010C;
 int test,i;u32 nativeCycles=0,modernCycles=0;
 state=0x428f629d;
 for(test=0;test<16384;test++) {
  u16 selected=test%4==0?0:test%4==1?1:test%4==2?65535:next();
  u16 volume=next();s16 pitch=next();
  for(i=0;i<sizeof(expectedPlayer)/4;i++)((u32*)&expectedPlayer)[i]=next();
  for(i=0;i<sizeof(expectedTracks)/4;i++)((u32*)expectedTracks)[i]=next();
  expectedPlayer.ident=test%7?ID_NUMBER:next();expectedPlayer.trackCount=test%17;
  actualPlayer=expectedPlayer;expectedPlayer.tracks=expectedTracks;actualPlayer.tracks=actualTracks;
  for(i=0;i<sizeof(expectedTracks)/4;i++)((u32*)actualTracks)[i]=((u32*)expectedTracks)[i];
  timer[1]=0;timer[0]=0;timer[1]=0x80;
  native_m4aMPlayVolumeControl(&expectedPlayer,selected,volume);
  native_m4aMPlayPitchControl(&expectedPlayer,selected,pitch);
  nativeCycles+=timer[0];timer[1]=0;
  timer[0]=0;timer[1]=0x80;
  m4aMPlayVolumeControl(&actualPlayer,selected,volume);
  m4aMPlayPitchControl(&actualPlayer,selected,pitch);
  modernCycles+=timer[0];timer[1]=0;
  expectedPlayer.tracks=actualPlayer.tracks;
  for(i=0;i<sizeof(expectedPlayer)/4;i++)if(((u32*)&expectedPlayer)[i]!=((u32*)&actualPlayer)[i])goto bad;
  for(i=0;i<sizeof(expectedTracks)/4;i++)if(((u32*)expectedTracks)[i]!=((u32*)actualTracks)[i])goto bad;
 }
 result[0]=0x600dc0de;result[1]=16384;result[2]=nativeCycles;result[3]=modernCycles;for(;;){}
 bad:result[0]=0xbad00001;result[1]=test;result[2]=i;for(;;){}
}
''')
lib=subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0]
commands=[
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-I',str(root),'-o',str(a.results/'native.o'),str(a.results/'native.s')],
 *[['arm-none-eabi-gcc','-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-std=gnu89','-DMKSC_RING=1','-fwrapv','-fno-strict-aliasing','-ffreestanding','-fno-builtin','-I'+str(root/'src'),'-I'+str(root/'include'),'-I'+str(root/'lib'),'-c',str(src),'-o',str(a.results/name)] for src,name in [(a.results/'main.c','main.o'),(root/'src/ringSoundControl.O3.c','modern.o')]],
 ['arm-none-eabi-ld','-T',str(root/'tools/ring-poc/link.ld'),'-o',str(a.results/'control.elf'),*[str(a.results/k) for k in ('start.o','main.o','native.o','modern.o')],'-L'+lib,'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'control.elf'),str(a.results/'control.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'control.gba'),'-p','-tMPLAYCONTROL','--silent']]
for c in commands:subprocess.run(c,cwd=root,check=True,capture_output=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'control.gba'),'--bios',str(a.bios),'--players','1','--frames','8000','--dump','0x02000000'],capture_output=True,text=True,timeout=600)
(a.results/'result.json').write_text(proc.stdout);assert proc.returncode==0,proc.stderr
d=json.loads(proc.stdout);v=d['nodes'][0]['dump'];assert v[:2]==[0x600dc0de,16384],d
print(f'PASS mixer controls: exact native instructions and C match full player/16-track state across 16384 masks, flags, locks and parameters; cycles/control pair native {v[2]/16384:.1f}, modern {v[3]/16384:.1f}',flush=True)
