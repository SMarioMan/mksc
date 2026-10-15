#!/usr/bin/env python3
"""Run the actual ARM HDMA kernel against native projection arithmetic on a GBA."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('runner', 'bios', 'results'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--prefix', default='arm-none-eabi-')
a = p.parse_args()
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
root = Path(__file__).resolve().parents[2]
a.results.mkdir(parents=True, exist_ok=True)
source = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef int s32;
typedef short s16;
typedef struct { s16 pa,pb,pc,pd; s32 dx,dy; } Affine;
volatile Affine BgAffineBuf[2][96];
s32 gSinTable[1024];
static u32 camera[256];
extern void sub_SetHDMA(void*);
static u32 state;
static u32 random32(void) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
int main(void) {
    volatile u32* result = (volatile u32*)0x02000000;
    int i, test;
    state = 0x5EED1234;
    for (i = 0; i < 1024; i++) gSinTable[i] = (int)(random32() % 65537) - 32768;
    for (test = 0; test < 4096; test++) {
        Affine expected[96];
        int yaw = (s16)(test * 17), q = yaw / 64, bank = test & 1;
        s32 cosAngle = gSinTable[(256 - q) & 1023];
        s32 sinAngle = gSinTable[q & 1023];
        s32 worldX,worldY,screenX,scanline;
        *(s16*)((u8*)camera + 0x14) = yaw;
        camera[0x18/4] = random32(); camera[0x1C/4] = random32();
        camera[0x24/4] = random32(); camera[0x28/4] = random32();
        camera[0x50/4] = bank;
        worldX = (s32)camera[0x18/4] / 256;
        worldY = (s32)camera[0x1C/4] / 256;
        screenX = -(s32)camera[0x24/4];
        scanline = 159 - (s32)camera[0x28/4];
        for (i = 95; i >= 0; i--, scanline--) {
            s32 x = random32(), y = random32();
            s32 pa = y * cosAngle / 32768;
            s32 pb = -x * sinAngle / 32768;
            s32 pc = y * sinAngle / 32768;
            s32 pd = x * cosAngle / 32768;
            camera[0x80/4 + i*2] = x;
            camera[0x80/4 + i*2+1] = y;
            expected[i].pa = pa; expected[i].pb = pb;
            expected[i].pc = pc; expected[i].pd = pd;
            expected[i].dx = worldX + screenX * pa + scanline * pb;
            expected[i].dy = worldY + screenX * pc + scanline * pd;
        }
        sub_SetHDMA(camera);
        if (camera[0x50/4] != bank) { result[0] = 0xBAD00001; break; }
        for (i = 0; i < 96; i++) {
            volatile Affine* got = &BgAffineBuf[bank][i];
            Affine* want = &expected[i];
            if (got->pa != want->pa || got->pb != want->pb ||
                got->pc != want->pc || got->pd != want->pd ||
                got->dx != want->dx || got->dy != want->dy) {
                result[0] = 0xBAD00002; result[1] = test; result[2] = i;
                break;
            }
        }
        if (i != 96) break;

    }
    if (test == 4096) result[0] = 0x600DC0DE;
    for (;;) {}
}
'''
(a.results / 'camera-test.c').write_text(source)
link = (root / 'tools/ring-poc/link.ld').read_text().replace('*(.text*)', '*(.text*) *(.ring_irq*)')
(a.results / 'link.ld').write_text(link)
commands = [
    [a.prefix+'as', '-mcpu=arm7tdmi', '-o', str(a.results/'start.o'), str(root/'tools/ring-poc/start.s')],
    [a.prefix+'as', '-mcpu=arm7tdmi', '-o', str(a.results/'camera.o'), str(root/'nonmatching/ringCamera.s')],
    [a.prefix+'gcc', '-mcpu=arm7tdmi', '-mthumb', '-mthumb-interwork', '-O2', '-fwrapv', '-ffreestanding',
     '-fno-builtin', '-c', str(a.results/'camera-test.c'), '-o', str(a.results/'main.o')],
    [a.prefix+'ld', '-T', str(a.results/'link.ld'), '-o', str(a.results/'camera-test.elf'),
     str(a.results/'start.o'), str(a.results/'main.o'), str(a.results/'camera.o'),
     '-L'+subprocess.check_output([a.prefix+'gcc','-print-libgcc-file-name'],text=True).strip().rsplit('/',1)[0], '-lgcc'],
    [a.prefix+'objcopy', '-O','binary', str(a.results/'camera-test.elf'), str(a.results/'camera-test.gba')],
    [str(root/'tools/gbafix/gbafix'), str(a.results/'camera-test.gba'), '-p', '-tCAMERATEST', '-cHDMA', '--silent'],
]
for command in commands:
    subprocess.run(command, check=True)
proc = subprocess.run([str(a.runner), '--rom', str(a.results/'camera-test.gba'), '--bios', str(a.bios),
                       '--players', '1', '--frames', '5000', '--dump', '0x02000000'],
                      capture_output=True, text=True, timeout=120)
(a.results/'result.json').write_text(proc.stdout)
(a.results/'result.stderr').write_text(proc.stderr)
assert proc.returncode == 0, proc.stderr
result = json.loads(proc.stdout)
assert result['bios'] == 'real' and result['nodes'][0]['dump'][0] == 0x600DC0DE, result
print('PASS: ARM HDMA matches native arithmetic for 393,216 scanlines, including signed overflow, both buffers, and a stable write bank')
