#!/usr/bin/env python3
"""Build the separate ring variant without replacing the matching stock ROM."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[2]
out = root / 'build-ring'
out.mkdir(exist_ok=True)
link = (root / 'ld_script.ld').read_text()
link = link.replace('        src/vcount.o(COMMON);', '''        src/vcount.o(COMMON);
        . = 0x6B40;
        src/ringLink.o(.bss);
        src/ringLink.o(COMMON);
        ASSERT(. < 0x7500, "Ring state overlaps the system stack reserve");''')
link = link.replace('        data/characterTiles.o(.rodata*);', '''        data/characterTiles.o(.rodata*);
        src/ringLink.o(.text);
        src/ringLink.o(.rodata);''')
(out / 'ld_script.ld').write_text(link)
symbols = (root / 'symbols.txt').read_text()
for name, address in [('gKeyBuf', '0x03006B00'), ('gKeyTriggerBuf', '0x03006B10'),
                      ('gKeyRepeatTriggerBuf', '0x03006B20'), ('sio2_sKeyBuf', '0x03006B30')]:
    import re
    symbols = re.sub(rf'^{name} = .*?;', f'{name} = {address};', symbols, flags=re.M)
symbols = symbols.replace('gSelectedCharacters = 0x0300002C;', 'gSelectedCharacters = ring_selectedCharacters;')
(out / 'symbols.txt').write_text(symbols)
subprocess.run(['make', 'tools', 'libraries'], cwd=root, check=True)
subprocess.run(['make', 'RING=1', '-j5', 'mksc-ring.gba', 'mksc-ring.sym'], cwd=root, check=True)
