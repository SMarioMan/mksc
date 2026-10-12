/* Native HDMA projection arithmetic in ARM/IWRAM. All products wrap to
 * 32 bits and signed /32768 and /256 truncate toward zero, as in camera.c. */
.syntax unified
.section .ring_irq,"ax",%progbits
.arm
.align 2
.global sub_SetHDMA
.type sub_SetHDMA, %function
sub_SetHDMA:
    push {r0-r11, lr}
    mov r8, r0
    ldrsh r9, [r8, #0x14]
    cmp r9, #0
    addlt r9, r9, #63
    mov r9, r9, asr #6
    rsb r10, r9, #256
    mov r10, r10, lsl #22
    mov r10, r10, lsr #20
    ldr r11, =gSinTable
    ldr r0, [r11, r10]
    mov r9, r9, lsl #22
    mov r9, r9, lsr #20
    ldr r1, [r11, r9]
    ldr r2, [r8, #0x18]
    cmp r2, #0
    addlt r2, r2, #255
    mov r2, r2, asr #8
    ldr r3, [r8, #0x1c]
    cmp r3, #0
    addlt r3, r3, #255
    mov r3, r3, asr #8
    ldr r4, [r8, #0x24]
    rsb r4, r4, #0
    ldr r5, [r8, #0x28]
    rsb r5, r5, #159
    add r6, r8, #0x300
    add r6, r6, #0x78
    ldr r9, [r8, #0x50]
    ldr r7, =BgAffineBuf
    add r7, r7, r9, lsl #9
    add r7, r7, r9, lsl #10
    add r7, r7, #0x500
    add r7, r7, #0xf0
    mov r8, #96
.Lring_hdma_line:
    ldr r9, [r6]
    ldr r10, [r6, #4]
    mul r11, r10, r0
    mov ip, r11, asr #31
    add r11, r11, ip, lsr #17
    mov r11, r11, asr #15
    strh r11, [r7]
    mul ip, r4, r11
    add ip, ip, r2
    mul r11, r9, r1
    rsb r11, r11, #0
    mov lr, r11, asr #31
    add r11, r11, lr, lsr #17
    mov r11, r11, asr #15
    strh r11, [r7, #2]
    mla ip, r5, r11, ip
    str ip, [r7, #8]
    mul r11, r10, r1
    mov ip, r11, asr #31
    add r11, r11, ip, lsr #17
    mov r11, r11, asr #15
    strh r11, [r7, #4]
    mul ip, r4, r11
    add ip, ip, r3
    mul r11, r9, r0
    mov lr, r11, asr #31
    add r11, r11, lr, lsr #17
    mov r11, r11, asr #15
    strh r11, [r7, #6]
    mla ip, r5, r11, ip
    str ip, [r7, #12]
    sub r6, r6, #8
    sub r7, r7, #16
    sub r5, r5, #1
    subs r8, r8, #1
    bne .Lring_hdma_line
    ldr r8, [sp]
    ldr r9, [r8, #0x50]
    rsb r9, r9, #1
    str r9, [r8, #0x50]
    pop {r0-r11, lr}
    bx lr
    .ltorg
.size sub_SetHDMA, .-sub_SetHDMA
.thumb
.text
