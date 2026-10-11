ENTRY(_start)
MEMORY {
    rom (rx) : ORIGIN = 0x08000000, LENGTH = 4M
    ram (rw) : ORIGIN = 0x02000100, LENGTH = 256K - 256
}
SECTIONS {
    .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) } > rom
    .bss (NOLOAD) : {
        __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .;
    } > ram
    /DISCARD/ : { *(.comment) *(.ARM.exidx*) *(.ARM.attributes*) }
}
