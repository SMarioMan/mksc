.syntax unified
.cpu arm7tdmi
.section .text.start, "ax"
.arm
.global _start
_start:
    b boot
    .space 0xbc
boot:
    mov r0, #0x1f
    msr cpsr_c, r0
    ldr sp, =0x03007f00
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    ldr r0, =main
    bx r0
