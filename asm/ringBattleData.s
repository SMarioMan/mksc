.ifdef MKSC_RING
.section .rodata
.align 2
.include "build-ring/retro-battle-defs.inc"
.ifdef MKSC_RING_JAPANESE_SUNSET
.align 2
.global ring_sunsetJapaneseCover
ring_sunsetJapaneseCover:
.incbin "build-ring/japanese-sunset-cover.bin"
.endif
.endif
