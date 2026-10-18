#!/usr/bin/env python3
"""Build the separate ring variant without replacing the matching stock ROM."""
from pathlib import Path
import subprocess
import argparse
import hashlib
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--boot-ring", action="store_true", help="Boot directly into the ring connection menu")
p.add_argument("--sunset-art", choices=("original", "japanese"), default="original",
               help="Choose Sunset Wilds artwork; the default preserves the US art")
p.add_argument("--code-padding", type=lambda value: int(value, 0), default=0,
               help="Insert aligned ROM padding before game code to test relocation")
music = p.add_mutually_exclusive_group()
music.add_argument("--music", choices=("normal", "shared"), default="normal", help="Music policy; effects remain enabled")
music.add_argument("--shared-music", dest="music", action="store_const", const="shared")
p.add_argument("--music-restoration-ips", type=Path, help="Optional supplied Music Restore IPS: import its samples and instruments only")
a = p.parse_args()
if a.music_restoration_ips:
    a.music_restoration_ips = a.music_restoration_ips.resolve()
if a.code_padding < 0 or a.code_padding > 65536 or a.code_padding % 4:
    p.error("Code padding must be a multiple of four from 0 to 65536 bytes")
root = Path(__file__).resolve().parents[2]
out = root / 'build-ring'
out.mkdir(exist_ok=True)
# Compiler defines are not make dependencies. Rebuild when policy changes.
audio_hash = hashlib.sha256(a.music_restoration_ips.read_bytes()).hexdigest() if a.music_restoration_ips else "original"
configuration = f"{int(a.boot_ring)}:{a.music}:{a.sunset_art}:{audio_hash}"
stamp = out / 'ring-options.txt'
if not stamp.exists() or stamp.read_text() != configuration:
    for obj in out.rglob('*.o'):
        obj.unlink()
    (out / 'retro-battle.stamp').unlink(missing_ok=True)
    stamp.write_text(configuration)
link = (root / 'ld_script.ld').read_text()
# Native decompression scratch extends beyond the frame heap. Put counters
# above the last native global (0x0203EC8C), outside all graphics scratch.
link = link.replace('        gMenuState = .; . += 4;', '        gMenuState = .; . += 4;\n        . = 0x3ED00;\n        ring_audioTiming = .; . += 64;\n        ring_audioDmaOverride = .; . += 4;\n        ring_audioMenuStarts = .; . += 44;\n        . = 0x3ED80;\n        src/ringSessionState.o(.bss);\n        src/ringSessionState.o(COMMON);\n        src/ringMap.O3.o(.bss);\n        src/ringMap.O3.o(COMMON);\n        ASSERT(. <= 0x40000, "Audio counters exceed external RAM");')
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
        src/ringProjection.O3.o(.text);
        src/ringProjection.O3.o(.rodata*);
        src/ringSoundControl.O3.o(.text);
        src/ringIdentity.O3.o(.text);
        src/ringIdentity.O3.o(.rodata*);
        src/ringMap.O3.o(.text);
        src/ringMap.O3.o(.rodata*);
        src/ringLink.o(.rodata*);
        asm/ringBattleData.o(.rodata);
        asm/ringRestoredAudio.o(.rodata);''')
link = link.replace('    /* Discard everything not specifically mentioned above. */', '''    .ring_irq ALIGN(ADDR(.bss) + SIZEOF(.bss), 4) : AT(ALIGN(LOADADDR(.text) + SIZEOF(.text), 4))
    {
        __ring_irq_start = .;
        src/ringLink.o(.ring_irq);
        src/camera.o(.ring_irq);
        src/oam.O3.o(.ring_irq);
        src/collision.o(.ring_irq);
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
# Modern Thumb C uses EABI quotient names; the matching ROM already links
# the equivalent agbcc signed/unsigned helpers. Keep one relocatable copy.
symbols += '\n__aeabi_idiv = __divsi3;\n__aeabi_uidiv = __udivsi3;\n'
(out / 'symbols.txt').write_text(symbols)
subprocess.run(['make', 'tools', 'libraries'], cwd=root, check=True)
if a.music_restoration_ips:
    subprocess.run(['make', '-j4'], cwd=root, check=True)
    subprocess.run(['python3', 'tools/ring/restore_audio_assets.py', '--patch', str(a.music_restoration_ips)], cwd=root, check=True)
subprocess.run(['make', 'RING=1', f'RING_MUSIC_RESTORE={int(bool(a.music_restoration_ips))}', f'RING_BOOT={int(a.boot_ring)}', f'RING_MUSIC={a.music}', f'RING_SUNSET_ART={a.sunset_art}', '-j5', 'mksc-ring.gba', 'mksc-ring.sym'], cwd=root, check=True)
