#!/usr/bin/env python3
"""Build the separate ring variant without replacing the matching stock ROM."""
from pathlib import Path
import subprocess
import argparse
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--boot-ring", action="store_true", help="Boot directly into the ring connection menu")
p.add_argument("--code-padding", type=lambda value: int(value, 0), default=0,
               help="Insert aligned ROM padding before game code to test relocation")
music = p.add_mutually_exclusive_group()
music.add_argument("--music", choices=("normal", "off", "shared"), default="normal", help="Music policy; effects remain enabled")
music.add_argument("--no-music", dest="music", action="store_const", const="off")
music.add_argument("--shared-music", dest="music", action="store_const", const="shared")
a = p.parse_args()
if a.code_padding < 0 or a.code_padding > 65536 or a.code_padding % 4:
    p.error("Code padding must be a multiple of four from 0 to 65536 bytes")
root = Path(__file__).resolve().parents[2]
out = root / 'build-ring'
out.mkdir(exist_ok=True)
# Compiler defines are not make dependencies. Rebuild when policy changes.
configuration = f"{int(a.boot_ring)}:{a.music}"
stamp = out / 'ring-options.txt'
if not stamp.exists() or stamp.read_text() != configuration:
    for obj in out.rglob('*.o'):
        obj.unlink()
    stamp.write_text(configuration)
link = (root / 'ld_script.ld').read_text()
if a.code_padding:
    link = link.replace('        asm/crt0.o(.text);',
                        '        asm/crt0.o(.text);\n        . += %d;' % a.code_padding)
link = link.replace('        src/vcount.o(COMMON);', '''        src/vcount.o(COMMON);
        . = 0x6B40;
        src/ringLink.o(.ring_state);
        src/ringLink.o(.bss);
        src/ringLink.o(COMMON);
        ASSERT(. < 0x7500, "Ring state overlaps the system stack reserve");''')
link = link.replace('        data/characterTiles.o(.rodata*);', '''        data/characterTiles.o(.rodata*);
        src/ringLink.o(.text);
        src/ringTarget.O3.o(.text);
        src/ringLink.o(.rodata);''')
link = link.replace('    /* Discard everything not specifically mentioned above. */', '''    .ring_irq ALIGN(ADDR(.bss) + SIZEOF(.bss), 4) : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 4))
    {
        __ring_irq_start = .;
        src/ringLink.o(.ring_irq);
        src/camera.o(.ring_irq);
        . = ALIGN(4);
        __ring_irq_end = .;

    }
    __ring_irq_load = LOADADDR(.ring_irq);
    ASSERT(__ring_irq_end <= 0x03007500, "Ring IRQ code overlaps the system stack reserve")

    /* Discard everything not specifically mentioned above. */''')
(out / 'ld_script.ld').write_text(link)
symbols = (root / 'symbols.txt').read_text()
for name, address in [('gKeyBuf', '0x03006B00'), ('gKeyTriggerBuf', '0x03006B10'),
                      ('gKeyRepeatTriggerBuf', '0x03006B20'), ('sio2_sKeyBuf', '0x03006B30')]:
    import re
    symbols = re.sub(rf'^{name} = .*?;', f'{name} = {address};', symbols, flags=re.M)
symbols = symbols.replace('gSelectedCharacters = 0x0300002C;', 'gSelectedCharacters = ring_selectedCharacters;')
(out / 'symbols.txt').write_text(symbols)
subprocess.run(['make', 'tools', 'libraries'], cwd=root, check=True)
subprocess.run(['make', 'RING=1', f'RING_BOOT={int(a.boot_ring)}', f'RING_MUSIC={a.music}', '-j5', 'mksc-ring.gba', 'mksc-ring.sym'], cwd=root, check=True)
