#!/usr/bin/env python3
"""Compare actor projection with the original assembly using the real BIOS."""
import argparse, hashlib, json, subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for key in ('runner', 'bios', 'results'):
    p.add_argument('--' + key, required=True, type=Path)
a = p.parse_args()
assert hashlib.sha1(a.bios.read_bytes()).hexdigest() == '300c20df6731a33952ded8c436f7f186d25d3492'
root = Path(__file__).resolve().parents[2]
a.results.mkdir(parents=True, exist_ok=True)
(a.results / 'main.c').write_text(r'''
#include "camera.h"
static Camera camera;
static Actor actor;
static u32 randomState;
static u32 random32(void) { randomState ^= randomState << 13; randomState ^= randomState >> 17; randomState ^= randomState << 5; return randomState; }
int ring_active(void) { return 1; }
void ring_stockProjectActor(Camera*, Actor*, Vec2s16*, s16*);
int main(void) {
    volatile u32* result = (volatile u32*)0x02000000;
    int test, i;
    u32 nativeCycles = 0, optimizedCycles = 0, visible = 0;
    volatile u16* timer = (volatile u16*)0x0400010C;
    randomState = 0x7391fa35;
    for (test = 0; test < 32768; test++) {
        Vec2s16 expected = {1234, -5678}, actual = expected;
        s16 expectedScale = 123, actualScale = 123;
        camera.elevation = 256 + random32() % 0x1000000;
        camera.screenPos.x = random32(); camera.screenPos.y = random32(); camera.unk380 = random32();
        camera.unk38.x = random32(); camera.unk38.y = random32(); camera.unk38.z = (s32)random32() % 0x4000000;
        for (i = 0; i < 9; i++) ((s32*)camera.unk5C)[i] = (s32)random32() % 131073;
        actor.posX = camera.unk38.x + (s32)random32() % 0x4000000;
        actor.posY = camera.unk38.y + (s32)random32() % 0x4000000;
        actor.posZ = random32();
        if (!(test & 7)) actor.posX = random32();
        if (!(test & 15)) camera.unk38.z = random32();
        u32 nativeElapsed, optimizedElapsed;
        timer[1] = 0; timer[0] = 0; timer[1] = 0x80;
        ring_stockProjectActor(&camera, &actor, &expected, &expectedScale);
        nativeElapsed = timer[0]; timer[1] = 0;
        timer[0] = 0; timer[1] = 0x80;
        cam_CalcProjectActor(&camera, &actor, &actual, &actualScale);
        optimizedElapsed = timer[0]; timer[1] = 0;
        if (expectedScale >= 0) { nativeCycles += nativeElapsed; optimizedCycles += optimizedElapsed; visible++; }
        if (actualScale != expectedScale || actual.x != expected.x || actual.y != expected.y) {
            result[0] = 0xbad00001; result[1] = test;
            result[2] = (u16)expectedScale; result[3] = (u16)actualScale;
            result[4] = (u16)expected.x | (u32)(u16)expected.y << 16;
            result[5] = (u16)actual.x | (u32)(u16)actual.y << 16;
            for (;;) {}
        }
    }
    result[0] = 0x600dc0de; result[1] = 32768;
    result[6] = nativeCycles; result[7] = optimizedCycles; result[8] = visible;
    for (;;) {}
}
''')
(a.results / 'div.s').write_text('.include "asm/macros.inc"\n.syntax unified\n.text\nthumb_func_start Div\nDiv:\n swi 6\n bx lr\nthumb_func_start umul3232H32\numul3232H32:\n adr r2, highMultiply\n bx r2\n.align 2\n.arm\nhighMultiply:\n umull r2,r3,r0,r1\n mov r0,r3\n bx lr\n')
library = subprocess.check_output(['arm-none-eabi-gcc', '-print-libgcc-file-name'], text=True).strip().rsplit('/', 1)[0]
commands = [
    ['arm-none-eabi-as', '-mcpu=arm7tdmi', '--defsym', 'MKSC_RING=1', '-I', str(root), '-o', str(a.results/'native.o'), str(root/'nonmatching/camProjectActor.s')],
    ['arm-none-eabi-as', '-mcpu=arm7tdmi', '-I', str(root), '-o', str(a.results/'div.o'), str(a.results/'div.s')],
    ['arm-none-eabi-as', '-mcpu=arm7tdmi', '-o', str(a.results/'start.o'), str(root/'tools/ring-poc/start.s')],
    *[['arm-none-eabi-gcc', '-mcpu=arm7tdmi', '-mthumb', '-mthumb-interwork', '-O3', '-DMKSC_RING=1', '-fwrapv', '-fno-strict-aliasing', '-ffreestanding', '-fno-builtin', '-I'+str(root/'src'), '-I'+str(root/'include'), '-I'+str(root/'lib'), '-c', str(src), '-o', str(a.results/name)] for src, name in [(a.results/'main.c', 'main.o'), (root/'src/ringProjection.O3.c', 'projection.o')]],
    ['arm-none-eabi-ld', '-T', str(root/'tools/ring-poc/link.ld'), '-o', str(a.results/'projection.elf'), *[str(a.results/k) for k in ('start.o', 'main.o', 'projection.o', 'native.o', 'div.o')], '-L'+library, '-lgcc'],
    ['arm-none-eabi-objcopy', '-O', 'binary', str(a.results/'projection.elf'), str(a.results/'projection.gba')],
    [str(root/'tools/gbafix/gbafix'), str(a.results/'projection.gba'), '-p', '-tPROJECTION', '--silent'],
]
for command in commands:
    subprocess.run(command, cwd=root, check=True, capture_output=True)
proc = subprocess.run([str(a.runner), '--ring', '--rom', str(a.results/'projection.gba'), '--bios', str(a.bios), '--players', '2', '--frames', '4000', '--dump', '0x02000000', '--dump-words', '9'], capture_output=True, text=True, timeout=600)
(a.results/'result.json').write_text(proc.stdout)
assert proc.returncode == 0, proc.stderr
data = json.loads(proc.stdout)
assert all(n['dump'][:2] == [0x600dc0de, 32768] for n in data['nodes']), data
print('PASS projection: original assembly and Thumb C match 32768 camera/actor cases, clipped outputs, signed truncation and wrapped products', flush=True)
native, optimized, visible = data['nodes'][0]['dump'][6:9]
assert visible > 1000, ('Insufficient unclipped projections', visible)
print(f'Projection cycles per unclipped actor: native {native/visible:.1f}, Thumb C {optimized/visible:.1f}; {visible} unclipped cases', flush=True)
