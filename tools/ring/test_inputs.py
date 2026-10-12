#!/usr/bin/env python3
"""Compare the optimized input conversion with the native action bit mapping."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/ringLink.c').read_text()
source = source[source.index('u16 ring_translateInput'):source.index('void ring_extendDriverInputs')]
program = '#include <stdint.h>\n#include <stdio.h>\ntypedef uint16_t u16;\n' + source + r'''
int main(void) {
    const int held[] = {16,32,128,64,2,1,256,512};
    const int actions[] = {256,512,1024,2048,16384,32768,16,64};
    const int triggered[] = {256,512,4,8}, triggers[] = {4,128,8192,4096};
    int h, t, i, expected;
    for (h = 0; h < 1024; h++) for (t = 0; t < 1024; t++) {
        expected = 0;
        for (i = 0; i < 8; i++) if (h & held[i]) expected |= actions[i];
        for (i = 0; i < 4; i++) if (t & triggered[i]) expected |= triggers[i];
        if (ring_translateInput(h, t) != expected) return 1;
    }
    puts("PASS: all 1,048,576 keypad/trigger combinations match the native input mapping");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(program)
    subprocess.run(['cc', '-O2', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
