#!/usr/bin/env python3
"""Execute the ROM mixer entry and check its PCM destination across VBlank."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for k in ('runner','bios','results'):p.add_argument('--'+k,required=True,type=Path)
p.add_argument('--ahead',type=int,choices=(2,3),default=2)
a=p.parse_args();a.results.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
s=(root/'asm/m4a_1.s').read_text();s=s[s.index('\tthumb_func_start SoundMain\n'):s.index('\tthumb_func_start SoundMainRAM\n')]
# A stub at the original RAM entry records the actual r5 destination. The
# native music callback advances the counter to simulate an intervening IRQ.
stub=r'''
.thumb
.thumb_func
SoundMainRAM_Buffer:
 ldr r0, =chosenBuffer
 str r5, [r0]
 ldr r0, [sp, #0x18]
 ldr r1, =ID_NUMBER
 str r1, [r0]
 add sp, #0x1c
 pop {r0-r3}
 mov r8,r0
 mov r9,r1
 mov r10,r2
 mov r11,r3
 pop {r4-r7}
 pop {r0}
 bx r0
.thumb_func
_0805F61E:
 bx r3
.pool
'''
(a.results/'mixer.s').write_text('.include "asm/macros.inc"\n.include "include/gba_constants.inc"\n.include "include/m4a_constants.inc"\n.syntax unified\n.text\n'+s+stub)
source=r'''
#include "gba/m4a_internal.h"
static struct SoundInfo info;
static u32 enabled, override, crossBlank;
volatile u32 chosenBuffer;
void SoundMain(void);
u32 ring_audioMixCounter(struct SoundInfo* p) { return enabled ? (override ? override : p->pcmDmaCounter+1) : 0; }
static void dummy(void) {}
static void advance(struct MusicPlayerInfo* p) { if(crossBlank)info.pcmDmaCounter=info.pcmDmaCounter>1?info.pcmDmaCounter-1:9; }
int main(void) {
 volatile u32* result=(volatile u32*)0x02000000;
 int c, tag, active, irq, want, actual, tests=0;
 SOUND_INFO_PTR=&info;
 info.pcmDmaPeriod=9;info.pcmSamplesPerVBlank=176;info.CgbSound=dummy;
 info.MPlayMainHead=advance;
 for(active=0;active<2;active++)for(c=0;c<=9;c++)for(tag=0;tag<=10;tag++)for(irq=0;irq<2;irq++) {
  enabled=active;override=tag;crossBlank=irq;info.pcmDmaCounter=c;info.ident=ID_NUMBER;
  want=active?(tag?tag-1:c)-(AHEAD-1):(irq?(c>1?c-1:9):c);
  if(active&&want<=0)want=AHEAD==2?9:9+want;
  want=want>1?9-(want-1):0;
  SoundMain();actual=(chosenBuffer-(u32)info.pcmBuffer)/176;
  if(actual!=want||info.ident!=ID_NUMBER) {result[0]=0xbad00001;result[1]=tests;result[2]=actual;result[3]=want;return 0;}
  tests++;
 }
 result[0]=0x600dc0de;result[1]=tests;
 for(;;) {}
}
'''
source=source.replace('AHEAD',str(a.ahead))
(a.results/'main.c').write_text(source)
for cmd in [
 ['arm-none-eabi-as','-mcpu=arm7tdmi','-o',str(a.results/'start.o'),str(root/'tools/ring-poc/start.s')],
 ['arm-none-eabi-as','-mcpu=arm7tdmi','--defsym','MKSC_RING=1','--defsym','MKSC_RING_AUDIO_AHEAD='+str(a.ahead),'-I',str(root),'-I',str(root/'include'),'-o',str(a.results/'mixer.o'),str(a.results/'mixer.s')],
 ['arm-none-eabi-gcc','-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-ffreestanding','-I',str(root/'include'),'-c',str(a.results/'main.c'),'-o',str(a.results/'main.o')],
 ['arm-none-eabi-ld','-T',str(root/'tools/ring-poc/link.ld'),'-o',str(a.results/'audio.elf'),*[str(a.results/k) for k in ('start.o','mixer.o','main.o')],'-L'+subprocess.check_output(['arm-none-eabi-gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0],'-lgcc'],
 ['arm-none-eabi-objcopy','-O','binary',str(a.results/'audio.elf'),str(a.results/'audio.gba')],
 [str(root/'tools/gbafix/gbafix'),str(a.results/'audio.gba'),'-p','-tAUDIOQUEUE','-cAUDQ','--silent']]:subprocess.run(cmd,cwd=root,check=True)
proc=subprocess.run([str(a.runner),'--rom',str(a.results/'audio.gba'),'--bios',str(a.bios),'--players','1','--frames','600','--dump','0x02000000'],capture_output=True,text=True,timeout=180)
(a.results/'result.json').write_text(proc.stdout);(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode==0,(proc.returncode,proc.stderr)
d=json.loads(proc.stdout);assert d['nodes'][0]['dump'][:2]==[0x600dc0de,440],d
print('PASS actual mixer entry: 440 destination/counter/IRQ fixtures; stock path unchanged, ring segments remain stable across VBlank',flush=True)
