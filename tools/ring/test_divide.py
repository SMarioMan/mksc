#!/usr/bin/env python3
"""Exhaustively compare the actual sprite division helper with signed division."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/ringLink.c').read_text()
source = source[source.index('static const u16 sSpriteReciprocal'):source.index('static inline u8 RING_IRQ checksum')]
program = r'''
#include <stdint.h>
#include <stdio.h>
typedef int32_t s32;
typedef uint32_t u32;
typedef uint16_t u16;
#define RING_IRQ
static s32 __divsi3(s32 n, s32 d) { return n / d; }
''' + source + r'''
int main(void) {
    int n, d;
    for (d = 1; d <= 4096; d++)
        for (n = -65536; n <= 65536; n++)
            if (ring_spriteDivide(n, d) != n / d) return 1;
    for (d = -8192; d <= 8192; d++) if (d)
        for (n = -131072; n <= 131072; n += 257)
            if (ring_spriteDivide(n, d) != n / d) return 2;
    puts("PASS: 536 million exact scale quotients and fallback boundary cases");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(program)
    subprocess.run(['cc', '-O2', '-I', str(root / 'src'), str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
