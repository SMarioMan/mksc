@ Optional sample payload. Every reference to it is an assembler relocation;
@ the historical IPS's fixed 0x08400000 sample addresses are never retained.
.section .rodata
.ifdef MKSC_RING_MUSIC_RESTORE
.balign 4
.global ring_restoredAudioSamples
.global ring_restoredAudioSamples_end
ring_restoredAudioSamples:
.incbin "build-ring/restored-audio/samples.bin"
ring_restoredAudioSamples_end:
.endif
