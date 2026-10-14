#!/usr/bin/env python3
"""Verify that the compiled collision scan actually calls the IWRAM helper."""
import argparse, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--elf',required=True,type=Path)
a=p.parse_args()
symbols={}
for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(a.elf)],text=True).splitlines():
 fields=line.split()
 if len(fields)==3:symbols[fields[2]]=int(fields[0],16)
assert symbols['__ring_irq_start']<=symbols['firstMinimumCheckpoint']<symbols['__ring_irq_end']<=0x03007500
code=subprocess.check_output(['arm-none-eabi-objdump','-d',str(a.elf),'--disassemble=sortByCheckpoint'],text=True)
assert re.search(r'\bbl\b[^\n]*firstMinimumCheckpoint',code),'The compiler inlined the scan back into ROM'
print('PASS hot code: collision scan calls the loaded IWRAM helper; RAM code stays below the stack reserve')
