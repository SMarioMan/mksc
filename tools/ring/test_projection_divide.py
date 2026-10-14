#!/usr/bin/env python3
"""Check every signed quotient boundary for the actual projection divider."""
from pathlib import Path
import subprocess, tempfile

root = Path(__file__).resolve().parents[2]
source = (root/'src/ringProjection.O3.c').read_text()
source = source[source.index('static const u32 sProjectionReciprocal'):source.index('/* Preserve the native')]
program = r'''
#include <stdint.h>
#include <stdio.h>
typedef int32_t s32;
typedef uint32_t u32;
static u32 umul3232H32(u32 a, u32 b) { return (uint64_t)a*b >> 32; }
''' + source + r'''
int main(void) {
    uint64_t checked = 0;
    unsigned scale, quotient;
    int offset, sign;
    for (scale = 32; scale <= 1024; scale++) {
        int divisor = scale * 256;
        for (quotient = 0; quotient <= (UINT64_C(1)<<31)/divisor; quotient++)
            for (offset = -1; offset <= 1; offset++) {
                int64_t magnitude = (int64_t)quotient*divisor + offset;
                if (magnitude < 0 || magnitude > (INT64_C(1)<<31)) continue;
                for (sign = -1; sign <= 1; sign += 2) {
                    int64_t value = sign*magnitude;
                    if (value > INT32_MAX) continue;
                    if (projectScreenCoordinate(value, scale) != value/divisor) return 1;
                    checked++;
                }
            }
        if (projectScreenCoordinate(INT32_MIN, scale) != INT32_MIN/divisor ||
            projectScreenCoordinate(INT32_MAX, scale) != INT32_MAX/divisor) return 2;
    }
    printf("PASS projection division: %llu signed quotient boundaries, all 993 scales and INT_MIN/INT_MAX\n", (unsigned long long)checked);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.c').write_text(program)
    subprocess.run(['cc', '-O2', '-I', str(root/'src'), str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
