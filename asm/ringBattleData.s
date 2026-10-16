.ifdef MKSC_RING
.section .rodata
.align 2
.include "build-ring/retro-battle-defs.inc"
.align 2
.global ring_sunsetJapaneseCover
ring_sunsetJapaneseCover:
.incbin "build-ring/japanese-sunset-cover.bin"
.endif
