	.include "asm/macros.inc"

	.syntax unified
	.text

	thumb_func_start sub_802DCF0
sub_802DCF0: @ 0x0802DCF0
	push {r4, r5, r6, r7, lr}
	mov r7, sb
	mov r6, r8
	push {r6, r7}
	ldr r0, _0802DE04 @ =0x03004F60
	movs r4, #1
	str r4, [r0]
	movs r7, #0
	ldr r0, _0802DE08 @ =0x0400010E
	strh r7, [r0]
	ldr r1, _0802DE0C @ =0x03004FD8
	ldr r3, _0802DE10 @ =0x04000128
	ldrh r0, [r3]
	strh r0, [r1]
	ldr r2, _0802DE14 @ =0x03000270
	ldr r0, _0802DE18 @ =0x04000120
	ldrh r0, [r0]
	ldrh r1, [r2]
	strh r0, [r2]
	ldr r0, _0802DE1C @ =0x04000122
	ldrh r0, [r0]
	ldrh r1, [r2, #2]
	strh r0, [r2, #2]
	ldr r0, _0802DE20 @ =0x04000124
	ldrh r0, [r0]
	ldrh r1, [r2, #4]
	strh r0, [r2, #4]
	ldr r0, _0802DE24 @ =0x04000126
	ldrh r0, [r0]
	ldrh r1, [r2, #6]
	strh r0, [r2, #6]
	ldr r0, _0802DE28 @ =0x03000268
	ldr r0, [r0]
	movs r5, #0xc0
	lsls r5, r5, #7
	adds r1, r5, #0
	orrs r0, r1
	strh r0, [r3]
	ldr r0, _0802DE2C @ =0x030002AC
	ldr r1, [r0]
	adds r6, r0, #0
	cmp r1, #0
	bne _0802DD48
	b _0802DEDC
_0802DD48:
	ldr r0, [r6]
	ldr r1, _0802DE30 @ =0x03000210
	mov r8, r1
	ldr r3, _0802DE34 @ =0x030002B8
	mov ip, r3
	ldr r5, _0802DE38 @ =0x0300026C
	mov sb, r5
	cmp r0, #1
	bne _0802DD7C
	ldr r0, _0802DE3C @ =0x03000250
	str r7, [r0]
	movs r5, #0
	mov r3, r8
	mov r4, ip
_0802DD64:
	str r5, [r3]
	ldr r0, [r4]
	lsls r1, r7, #3
	adds r0, #0xc
	adds r0, r0, r1
	ldr r0, [r0]
	str r0, [r3, #8]
	str r0, [r3, #4]
	adds r3, #0x10
	adds r7, #1
	cmp r7, #3
	ble _0802DD64
_0802DD7C:
	movs r0, #2
	str r0, [r6]
	ldrh r1, [r2]
	ldrh r0, [r2, #2]
	adds r4, r0, #0
	ldrh r0, [r2, #4]
	adds r5, r0, #0
	ldrh r0, [r2, #6]
	adds r6, r0, #0
	movs r2, #0
	ldr r3, _0802DE40 @ =0x00003FFF
	adds r0, r3, #0
	ands r0, r1
	cmp r0, r3
	bne _0802DD9C
	movs r2, #1
_0802DD9C:
	ands r4, r3
	cmp r4, r3
	bne _0802DDA4
	adds r2, #1
_0802DDA4:
	ands r5, r3
	cmp r5, r3
	bne _0802DDAC
	adds r2, #1
_0802DDAC:
	ands r6, r3
	cmp r6, r3
	bne _0802DDB4
	adds r2, #1
_0802DDB4:
	cmp r2, #0
	ble _0802DDCE
	ldr r2, _0802DE44 @ =0x03004898
	ldr r0, [r2]
	adds r0, #1
	str r0, [r2]
	ldr r1, _0802DE48 @ =_080DE1C8
	lsls r0, r0, #2
	adds r0, r0, r1
	ldr r0, [r0]
	cmp r0, #0
	bne _0802DDCE
	str r0, [r2]
_0802DDCE:
	ldr r1, _0802DE4C @ =0x0400012A
	ldr r2, _0802DE50 @ =0x00007FFF
	adds r0, r2, #0
	strh r0, [r1]
	mov r3, ip
	ldr r1, [r3]
	ldr r2, [r1, #0xc]
	ldr r0, [r1, #0x10]
	lsls r0, r0, #1
	adds r2, r2, r0
	mov r5, r8
	ldr r0, [r5, #8]
	adds r3, r0, #2
	cmp r3, r2
	bne _0802DDEE
	ldr r3, [r1, #0xc]
_0802DDEE:
	lsls r0, r7, #4
	mov r1, r8
	adds r1, #4
	adds r0, r0, r1
	ldr r0, [r0]
	cmp r0, r3
	bne _0802DE58
	ldr r1, _0802DE54 @ =0x03000260
	movs r0, #2
	str r0, [r1]
	b _0802DE9C
	.align 2, 0
_0802DE04: .4byte 0x03004F60
_0802DE08: .4byte 0x0400010E
_0802DE0C: .4byte 0x03004FD8
_0802DE10: .4byte 0x04000128
_0802DE14: .4byte 0x03000270
_0802DE18: .4byte 0x04000120
_0802DE1C: .4byte 0x04000122
_0802DE20: .4byte 0x04000124
_0802DE24: .4byte 0x04000126
_0802DE28: .4byte 0x03000268
_0802DE2C: .4byte 0x030002AC
_0802DE30: .4byte 0x03000210
_0802DE34: .4byte 0x030002B8
_0802DE38: .4byte 0x0300026C
_0802DE3C: .4byte 0x03000250
_0802DE40: .4byte 0x00003FFF
_0802DE44: .4byte 0x03004898
_0802DE48: .4byte _080DE1C8
_0802DE4C: .4byte 0x0400012A
_0802DE50: .4byte 0x00007FFF
_0802DE54: .4byte 0x03000260
_0802DE58:
	movs r7, #0
	mov r4, r8
	adds r4, #8
	ldr r5, _0802DED0 @ =0x03000270
_0802DE60:
	mov r0, ip
	ldr r1, [r0]
	lsls r3, r7, #3
	adds r0, r1, #0
	adds r0, #0xc
	adds r0, r0, r3
	ldr r2, [r0]
	adds r1, #0x10
	adds r1, r1, r3
	ldr r0, [r1]
	lsls r0, r0, #1
	adds r2, r2, r0
	ldr r1, [r4]
	ldrh r0, [r5]
	strh r0, [r1]
	adds r1, #2
	str r1, [r4]
	cmp r1, r2
	bne _0802DE92
	mov r1, ip
	ldr r0, [r1]
	adds r0, #0xc
	adds r0, r0, r3
	ldr r0, [r0]
	str r0, [r4]
_0802DE92:
	adds r4, #0x10
	adds r5, #2
	adds r7, #1
	cmp r7, #3
	ble _0802DE60
_0802DE9C:
	mov r2, sb
	ldr r0, [r2]
	movs r1, #0xc0
	lsls r1, r1, #8
	ands r0, r1
	movs r1, #0x80
	lsls r1, r1, #7
	cmp r0, r1
	bne _0802DEF6
	ldr r0, _0802DED4 @ =0x03000264
	ldr r0, [r0]
	cmp r0, #0
	bne _0802DEF6
	mov r3, ip
	ldr r1, [r3]
	ldrh r0, [r1, #0x36]
	cmp r0, #0
	beq _0802DEF6
	ldr r1, _0802DED8 @ =0x0400010C
	rsbs r0, r0, #0
	strh r0, [r1]
	adds r1, #2
	movs r0, #0xc3
	strh r0, [r1]
	b _0802DEF6
	.align 2, 0
_0802DED0: .4byte 0x03000270
_0802DED4: .4byte 0x03000264
_0802DED8: .4byte 0x0400010C
_0802DEDC:
	ldr r2, _0802DF04 @ =0x0300026C
	ldr r0, [r2]
	movs r1, #3
	ands r0, r1
	cmp r0, #2
	beq _0802DEF6
	ldr r0, [r2]
	subs r1, #7
	ands r0, r1
	str r0, [r2]
	ldr r0, [r2]
	orrs r0, r4
	str r0, [r2]
_0802DEF6:
	pop {r3, r4}
	mov r8, r3
	mov sb, r4
	pop {r4, r5, r6, r7}
	pop {r0}
	bx r0
	.align 2, 0
_0802DF04: .4byte 0x0300026C

	thumb_func_start sub_802DF08
sub_802DF08: @ 0x0802DF08
	push {r4, r5, r6, r7, lr}
	mov r7, r8
	push {r7}
	adds r4, r0, #0
	ldr r1, _0802DF30 @ =0x030002AC
	ldr r0, [r1]
	cmp r0, #0
	beq _0802E000
	ldr r0, [r1]
	cmp r0, #1
	bne _0802DF38
	ldr r1, _0802DF34 @ =0x00007FFF
	adds r0, r1, #0
	strh r0, [r4]
	adds r4, #2
	strh r0, [r4]
	adds r4, #2
	strh r0, [r4]
	strh r0, [r4, #2]
	b _0802DFF8
	.align 2, 0
_0802DF30: .4byte 0x030002AC
_0802DF34: .4byte 0x00007FFF
_0802DF38:
	ldr r2, _0802DF4C @ =0x03000210
	ldr r1, [r2, #4]
	ldr r0, [r2, #8]
	cmp r1, r0
	bne _0802DF54
	ldr r1, _0802DF50 @ =0x00007FFF
	adds r0, r1, #0
	strh r0, [r4]
	adds r4, #2
	b _0802DFA2
	.align 2, 0
_0802DF4C: .4byte 0x03000210
_0802DF50: .4byte 0x00007FFF
_0802DF54:
	ldr r0, _0802DFB0 @ =0x03004FD8
	ldrh r1, [r0]
	movs r0, #0x40
	ands r0, r1
	cmp r0, #0
	beq _0802DFBC
	movs r7, #0
	adds r0, r4, #2
	mov ip, r0
	ldr r1, _0802DFB4 @ =0x030002B8
	mov r8, r1
	adds r6, r2, #4
_0802DF6C:
	mov r0, r8
	ldr r1, [r0]
	lsls r2, r7, #3
	adds r0, r1, #0
	adds r0, #0xc
	adds r5, r0, r2
	ldr r3, [r5]
	adds r1, #0x10
	adds r1, r1, r2
	ldr r0, [r1]
	lsls r0, r0, #1
	adds r3, r3, r0
	ldr r0, [r6]
	adds r0, #2
	str r0, [r6]
	cmp r0, r3
	bne _0802DF92
	ldr r0, [r5]
	str r0, [r6]
_0802DF92:
	adds r6, #0x10
	adds r7, #1
	cmp r7, #3
	ble _0802DF6C
	ldr r1, _0802DFB8 @ =0x00007FFF
	adds r0, r1, #0
	strh r0, [r4]
	mov r4, ip
_0802DFA2:
	strh r0, [r4]
	adds r4, #2
	strh r0, [r4]
	strh r0, [r4, #2]
	movs r0, #1
	rsbs r0, r0, #0
	b _0802E004
	.align 2, 0
_0802DFB0: .4byte 0x03004FD8
_0802DFB4: .4byte 0x030002B8
_0802DFB8: .4byte 0x00007FFF
_0802DFBC:
	movs r7, #0
	adds r6, r2, #4
	ldr r0, _0802DFFC @ =0x030002B8
	mov r8, r0
_0802DFC4:
	mov r0, r8
	ldr r1, [r0]
	lsls r2, r7, #3
	adds r0, r1, #0
	adds r0, #0xc
	adds r5, r0, r2
	ldr r3, [r5]
	adds r1, #0x10
	adds r1, r1, r2
	ldr r0, [r1]
	lsls r0, r0, #1
	adds r3, r3, r0
	ldr r1, [r6]
	ldrh r0, [r1]
	strh r0, [r4]
	adds r1, #2
	str r1, [r6]
	adds r4, #2
	cmp r1, r3
	bne _0802DFF0
	ldr r0, [r5]
	str r0, [r6]
_0802DFF0:
	adds r6, #0x10
	adds r7, #1
	cmp r7, #3
	ble _0802DFC4
_0802DFF8:
	movs r0, #5
	b _0802E004
	.align 2, 0
_0802DFFC: .4byte 0x030002B8
_0802E000:
	movs r0, #8
	rsbs r0, r0, #0
_0802E004:
	pop {r3}
	mov r8, r3
	pop {r4, r5, r6, r7}
	pop {r1}
	bx r1
	.align 2, 0

	thumb_func_start sub_802E010
sub_802E010: @ 0x0802E010
	push {r4, lr}
	lsls r0, r0, #0x10
	lsrs r2, r0, #0x10
	ldr r0, _0802E024 @ =0x03000264
	ldr r1, [r0]
	cmp r1, #3
	ble _0802E028
	movs r0, #4
	rsbs r0, r0, #0
	b _0802E0CC
	.align 2, 0
_0802E024: .4byte 0x03000264
_0802E028:
	ldr r0, _0802E050 @ =0x030002AC
	ldr r0, [r0]
	cmp r0, #0
	beq _0802E0C8
	ldr r0, _0802E054 @ =0x0400012A
	strh r2, [r0]
	ldr r3, _0802E058 @ =0x0300029C
	cmp r1, #0
	bne _0802E0B8
	movs r2, #0
	ldr r0, _0802E05C @ =0x030002B8
	ldr r0, [r0]
	ldrh r0, [r0, #0x36]
	cmp r0, #0
	beq _0802E060
	ldr r0, [r3]
	cmp r0, #0
	bne _0802E062
	b _0802E066
	.align 2, 0
_0802E050: .4byte 0x030002AC
_0802E054: .4byte 0x0400012A
_0802E058: .4byte 0x0300029C
_0802E05C: .4byte 0x030002B8
_0802E060:
	movs r2, #1
_0802E062:
	cmp r2, #0
	beq _0802E0B8
_0802E066:
	ldr r0, _0802E084 @ =0x0300026C
	ldr r0, [r0]
	movs r1, #0x80
	lsls r1, r1, #4
	ands r0, r1
	cmp r0, #0
	beq _0802E090
	ldr r1, _0802E088 @ =_080DE1C8
	ldr r0, _0802E08C @ =0x03004898
	ldr r0, [r0]
	lsls r0, r0, #2
	adds r0, r0, r1
	ldr r2, [r0]
	b _0802E092
	.align 2, 0
_0802E084: .4byte 0x0300026C
_0802E088: .4byte _080DE1C8
_0802E08C: .4byte 0x03004898
_0802E090:
	movs r2, #0
_0802E092:
	cmp r2, #0
	beq _0802E0A8
	ldr r1, _0802E0A4 @ =0x0400010C
	rsbs r0, r2, #0
	strh r0, [r1]
	adds r1, #2
	movs r0, #0xc3
	strh r0, [r1]
	b _0802E0B8
	.align 2, 0
_0802E0A4: .4byte 0x0400010C
_0802E0A8:
	ldr r2, _0802E0C0 @ =0x04000128
	ldr r0, _0802E0C4 @ =0x03000268
	ldr r0, [r0]
	movs r4, #0xc1
	lsls r4, r4, #7
	adds r1, r4, #0
	orrs r0, r1
	strh r0, [r2]
_0802E0B8:
	movs r0, #1
	str r0, [r3]
	movs r0, #5
	b _0802E0CC
	.align 2, 0
_0802E0C0: .4byte 0x04000128
_0802E0C4: .4byte 0x03000268
_0802E0C8:
	movs r0, #8
	rsbs r0, r0, #0
_0802E0CC:
	pop {r4}
	pop {r1}
	bx r1
	.align 2, 0

	thumb_func_start sub_802E0D4
sub_802E0D4: @ 0x0802E0D4
	push {r4, r5, r6, r7, lr}
	adds r4, r0, #0
	ldr r0, _0802E178 @ =0x03004F60
	movs r5, #0
	str r5, [r0]
	ldr r6, _0802E17C @ =0x030002B8
	str r4, [r6]
	ldr r0, _0802E180 @ =0x03004898
	str r5, [r0]
	ldr r1, _0802E184 @ =0x030002B0
	ldr r0, _0802E188 @ =0x030002AC
	str r5, [r0]
	ldr r0, [r0]
	str r0, [r1]
	ldr r2, _0802E18C @ =0x0300026C
	str r5, [r2]
	ldr r3, _0802E190 @ =0x03000268
	ldrh r1, [r4, #0x30]
	movs r0, #3
	ands r0, r1
	str r0, [r3]
	ldr r0, _0802E194 @ =0x0300027C
	str r5, [r0]
	ldr r0, [r4]
	str r0, [r2]
	ldrh r0, [r4, #0x2e]
	adds r7, r3, #0
	cmp r0, #0
	bne _0802E112
	movs r0, #0xa
	strh r0, [r4, #0x2e]
_0802E112:
	ldr r0, _0802E198 @ =0x03000280
	str r5, [r0, #4]
	ldr r1, [r0, #4]
	str r1, [r0]
	ldr r1, _0802E19C @ =0x030002C0
	ldr r0, [r6]
	ldrh r0, [r0, #0x2c]
	strh r0, [r1]
	ldr r4, _0802E1A0 @ =0x030002CC
	ldr r3, _0802E1A4 @ =0x030002D0
	ldr r5, _0802E1A8 @ =sub_802E60C
	movs r2, #3
_0802E12A:
	ldrh r0, [r1]
	lsls r0, r0, #1
	strh r0, [r1, #2]
	adds r1, #2
	subs r2, #1
	cmp r2, #0
	bge _0802E12A
	movs r2, #0
	str r2, [r3]
	str r2, [r4]
	ldr r0, _0802E1AC @ =0x04000134
	strh r2, [r0]
	ldr r3, _0802E1B0 @ =0x04000128
	ldr r0, [r7]
	movs r4, #0x80
	lsls r4, r4, #6
	adds r1, r4, #0
	orrs r0, r1
	strh r0, [r3]
	ldr r0, _0802E1B4 @ =0x0400010E
	strh r2, [r0]
	movs r0, #7
	adds r1, r5, #0
	bl irq_setHandler
	ldr r1, _0802E1B8 @ =sub_802E5E8
	movs r0, #6
	bl irq_setHandler
	ldr r1, _0802E1BC @ =0x000100C0
	movs r0, #2
	bl irq_updateMask
	ldr r1, _0802E1C0 @ =0x03000264
	movs r0, #0xf0
	str r0, [r1]
	pop {r4, r5, r6, r7}
	pop {r0}
	bx r0
	.align 2, 0
_0802E178: .4byte 0x03004F60
_0802E17C: .4byte 0x030002B8
_0802E180: .4byte 0x03004898
_0802E184: .4byte 0x030002B0
_0802E188: .4byte 0x030002AC
_0802E18C: .4byte 0x0300026C
_0802E190: .4byte 0x03000268
_0802E194: .4byte 0x0300027C
_0802E198: .4byte 0x03000280
_0802E19C: .4byte 0x030002C0
_0802E1A0: .4byte 0x030002CC
_0802E1A4: .4byte 0x030002D0
_0802E1A8: .4byte sub_802E60C
_0802E1AC: .4byte 0x04000134
_0802E1B0: .4byte 0x04000128
_0802E1B4: .4byte 0x0400010E
_0802E1B8: .4byte sub_802E5E8
_0802E1BC: .4byte 0x000100C0
_0802E1C0: .4byte 0x03000264

	thumb_func_start sub_802E1C4
sub_802E1C4: @ 0x0802E1C4
	push {r4, r5, r6, r7, lr}
	mov r7, sb
	mov r6, r8
	push {r6, r7}
	ldr r0, _0802E270 @ =0x030002B4
	movs r2, #0
	str r2, [r0]
	ldr r1, _0802E274 @ =0x030002B0
	ldr r0, _0802E278 @ =0x030002AC
	str r2, [r0]
	ldr r0, [r0]
	str r0, [r1]
	ldr r3, _0802E27C @ =0x0300026C
	ldr r0, [r3]
	movs r1, #4
	rsbs r1, r1, #0
	ands r0, r1
	str r0, [r3]
	ldr r0, _0802E280 @ =0x03000278
	str r2, [r0]
	ldr r0, _0802E284 @ =0x03000298
	str r2, [r0]
	ldr r0, _0802E288 @ =0x0300029C
	str r2, [r0]
	ldr r0, _0802E28C @ =0x03000280
	str r2, [r0, #4]
	ldr r1, [r0, #4]
	str r1, [r0]
	ldr r0, _0802E290 @ =0x030002A0
	str r2, [r0, #4]
	ldr r1, [r0, #4]
	str r1, [r0]
	ldr r0, _0802E294 @ =0x030002D4
	str r2, [r0]
	ldr r1, _0802E298 @ =0x03000250
	str r2, [r1]
	ldr r3, _0802E29C @ =0x030002B8
	ldr r0, [r3]
	ldr r0, [r0, #4]
	str r0, [r1, #8]
	str r0, [r1, #4]
	ldrh r0, [r1, #0xc]
	strh r2, [r1, #0xc]
	movs r4, #0
	mov sb, r3
	ldr r7, _0802E2A0 @ =0x03000210
	movs r6, #0
	ldr r0, _0802E2A4 @ =0x03000288
	mov r8, r0
	adds r3, r7, #0
	adds r5, r3, #0
	movs r1, #0xff
	mov ip, r1
_0802E22E:
	lsls r2, r4, #4
	mov r0, ip
	str r0, [r3]
	adds r0, r7, #4
	adds r2, r2, r0
	mov r1, sb
	ldr r0, [r1]
	lsls r1, r4, #3
	adds r0, #0xc
	adds r0, r0, r1
	ldr r0, [r0]
	str r0, [r5, #8]
	str r0, [r2]
	ldrh r0, [r3, #0xc]
	strh r6, [r3, #0xc]
	ldrh r0, [r3, #0xe]
	strh r6, [r3, #0xe]
	mov r0, r8
	adds r0, #4
	mov r8, r0
	subs r0, #4
	stm r0!, {r6}
	adds r5, #0x10
	adds r3, #0x10
	adds r4, #1
	cmp r4, #3
	ble _0802E22E
	pop {r3, r4}
	mov r8, r3
	mov sb, r4
	pop {r4, r5, r6, r7}
	pop {r0}
	bx r0
	.align 2, 0
_0802E270: .4byte 0x030002B4
_0802E274: .4byte 0x030002B0
_0802E278: .4byte 0x030002AC
_0802E27C: .4byte 0x0300026C
_0802E280: .4byte 0x03000278
_0802E284: .4byte 0x03000298
_0802E288: .4byte 0x0300029C
_0802E28C: .4byte 0x03000280
_0802E290: .4byte 0x030002A0
_0802E294: .4byte 0x030002D4
_0802E298: .4byte 0x03000250
_0802E29C: .4byte 0x030002B8
_0802E2A0: .4byte 0x03000210
_0802E2A4: .4byte 0x03000288

	thumb_func_start sub_802E2A8
sub_802E2A8: @ 0x0802E2A8
	push {r4, r5, lr}
	ldr r4, _0802E2BC @ =0x030002D4
	ldr r3, [r4]
	cmp r3, #0
	beq _0802E2C4
	cmp r3, #1
	beq _0802E330
	ldr r0, _0802E2C0 @ =0x03000264
	ldr r0, [r0]
	b _0802E3C8
	.align 2, 0
_0802E2BC: .4byte 0x030002D4
_0802E2C0: .4byte 0x03000264
_0802E2C4:
	ldr r0, _0802E314 @ =0x04000134
	strh r3, [r0]
	ldr r1, _0802E318 @ =0x0400012A
	ldr r0, _0802E31C @ =0x030002B8
	ldr r0, [r0]
	ldrh r0, [r0, #0x2c]
	mvns r0, r0
	strh r0, [r1]
	ldr r2, _0802E320 @ =0x04000128
	ldr r0, _0802E324 @ =0x03000268
	ldr r0, [r0]
	movs r5, #0xc0
	lsls r5, r5, #7
	adds r1, r5, #0
	orrs r0, r1
	strh r0, [r2]
	ldrh r0, [r2]
	adds r2, r0, #0
	movs r0, #8
	ands r0, r2
	cmp r0, #0
	beq _0802E3C4
	movs r0, #0x40
	ands r0, r2
	cmp r0, #0
	bne _0802E3C4
	ldr r1, _0802E328 @ =0x03000264
	movs r0, #4
	ands r0, r2
	str r0, [r1]
	cmp r0, #0
	beq _0802E308
	movs r0, #0xf0
	str r0, [r1]
_0802E308:
	ldr r0, _0802E32C @ =0x03004F60
	str r3, [r0]
	movs r0, #1
	str r0, [r4]
	b _0802E3C4
	.align 2, 0
_0802E314: .4byte 0x04000134
_0802E318: .4byte 0x0400012A
_0802E31C: .4byte 0x030002B8
_0802E320: .4byte 0x04000128
_0802E324: .4byte 0x03000268
_0802E328: .4byte 0x03000264
_0802E32C: .4byte 0x03004F60
_0802E330:
	ldr r0, _0802E37C @ =0x04000128
	ldrh r0, [r0]
	adds r2, r0, #0
	ldr r0, _0802E380 @ =0x03004F60
	ldr r0, [r0]
	cmp r0, #0
	beq _0802E398
	movs r1, #0x40
	ands r1, r2
	cmp r1, #0
	bne _0802E398
	ldr r0, _0802E384 @ =0x03000260
	str r1, [r0]
	ldr r1, _0802E388 @ =0x03000264
	movs r0, #0x30
	ands r0, r2
	asrs r0, r0, #4
	str r0, [r1]
	ldr r2, _0802E38C @ =0x03000210
	lsls r1, r0, #4
	adds r1, r1, r2
	ldrh r2, [r1, #0xe]
	strh r3, [r1, #0xe]
	ldr r3, _0802E390 @ =0x0300026C
	ldr r1, [r3]
	movs r2, #4
	rsbs r2, r2, #0
	ands r1, r2
	str r1, [r3]
	ldr r1, [r3]
	movs r2, #2
	orrs r1, r2
	str r1, [r3]
	ldr r1, _0802E394 @ =0x030002AC
	str r2, [r1]
	str r2, [r4]
	b _0802E3C8
	.align 2, 0
_0802E37C: .4byte 0x04000128
_0802E380: .4byte 0x03004F60
_0802E384: .4byte 0x03000260
_0802E388: .4byte 0x03000264
_0802E38C: .4byte 0x03000210
_0802E390: .4byte 0x0300026C
_0802E394: .4byte 0x030002AC
_0802E398:
	ldr r1, _0802E3D0 @ =0x0400012A
	ldr r0, _0802E3D4 @ =0x030002B8
	ldr r0, [r0]
	ldrh r0, [r0, #0x2c]
	mvns r0, r0
	strh r0, [r1]
	ldr r2, _0802E3D8 @ =0x04000128
	ldr r0, _0802E3DC @ =0x03000268
	ldr r0, [r0]
	movs r3, #0xc0
	lsls r3, r3, #7
	adds r1, r3, #0
	orrs r0, r1
	strh r0, [r2]
	ldr r0, _0802E3E0 @ =0x03000264
	ldr r0, [r0]
	cmp r0, #0
	bne _0802E3C4
	ldrh r0, [r2]
	movs r1, #0x80
	orrs r0, r1
	strh r0, [r2]
_0802E3C4:
	movs r0, #1
	rsbs r0, r0, #0
_0802E3C8:
	pop {r4, r5}
	pop {r1}
	bx r1
	.align 2, 0
_0802E3D0: .4byte 0x0400012A
_0802E3D4: .4byte 0x030002B8
_0802E3D8: .4byte 0x04000128
_0802E3DC: .4byte 0x03000268
_0802E3E0: .4byte 0x03000264

	thumb_func_start sub_0802E3E4
sub_0802E3E4: @ 0x0802E3E4
	push {r4, r5, r6, r7, lr}
	ldr r0, _0802E458 @ =0x03000264
	ldr r1, [r0]
	mov ip, r0
	cmp r1, #3
	bgt _0802E450
	ldr r0, _0802E45C @ =0x03000210
	lsls r1, r1, #4
	adds r1, r1, r0
	ldr r1, [r1]
	adds r4, r0, #0
	cmp r1, #0xff
	beq _0802E450
	ldr r2, _0802E460 @ =0x030002A0
	ldr r1, [r2]
	movs r3, #0
	ldr r0, [r2, #4]
	adds r7, r2, #0
	ldr r6, _0802E464 @ =0x0203EC40
	ldr r5, _0802E468 @ =0x030002A8
	cmp r1, r0
	beq _0802E41A
_0802E410:
	adds r1, #1
	adds r3, #1
	ldr r0, [r2, #4]
	cmp r1, r0
	bne _0802E410
_0802E41A:
	ldr r1, [r6]
	ldr r0, [r5]
	cmp r1, r0
	bne _0802E446
	ldr r0, _0802E46C @ =0x030002B8
	ldr r0, [r0]
	ldrh r0, [r0, #0x34]
	cmp r3, r0
	ble _0802E446
	mov r1, ip
	ldr r0, [r1]
	lsls r0, r0, #4
	adds r0, r0, r4
	movs r1, #0xff
	str r1, [r0]
	movs r0, #0xf0
	mov r2, ip
	str r0, [r2]
	str r1, [r4]
	str r1, [r4, #0x10]
	str r1, [r4, #0x20]
	str r1, [r4, #0x30]
_0802E446:
	ldr r0, [r5]
	str r0, [r6]
	ldr r0, [r7, #4]
	adds r0, #1
	str r0, [r7, #4]
_0802E450:
	pop {r4, r5, r6, r7}
	pop {r0}
	bx r0
	.align 2, 0
_0802E458: .4byte 0x03000264
_0802E45C: .4byte 0x03000210
_0802E460: .4byte 0x030002A0
_0802E464: .4byte 0x0203EC40
_0802E468: .4byte 0x030002A8
_0802E46C: .4byte 0x030002B8

	thumb_func_start sub_0802E470
sub_0802E470: @ 0x0802E470
	ldr r0, _0802E478 @ =0x03000264
	ldr r0, [r0]
	bx lr
	.align 2, 0
_0802E478: .4byte 0x03000264

	thumb_func_start sub_0802E47C
sub_0802E47C: @ 0x0802E47C
	ldr r0, _0802E484 @ =0x03000260
	ldr r0, [r0]
	bx lr
	.align 2, 0
_0802E484: .4byte 0x03000260

	thumb_func_start sub_0802E488
sub_0802E488: @ 0x0802E488
	bx lr
	.align 2, 0

	thumb_func_start sub_0802E48C
sub_0802E48C: @ 0x0802E48C
	ldr r1, _0802E494 @ =0x030002B0
	movs r0, #1
	str r0, [r1]
	bx lr
	.align 2, 0
_0802E494: .4byte 0x030002B0

	thumb_func_start sub_0802E498
sub_0802E498: @ 0x0802E498
	adds r2, r0, #0
	ldr r0, _0802E4A8 @ =0x03000260
	ldr r0, [r0]
	cmp r0, #0
	beq _0802E4AC
	movs r0, #2
	rsbs r0, r0, #0
	b _0802E4D8
	.align 2, 0
_0802E4A8: .4byte 0x03000260
_0802E4AC:
	ldr r1, _0802E4CC @ =0x03000288
	lsls r0, r2, #2
	adds r0, r0, r1
	ldr r1, [r0]
	movs r0, #1
	rsbs r0, r0, #0
	cmp r1, r0
	beq _0802E4D4
	ldr r0, _0802E4D0 @ =0x03000210
	lsls r1, r2, #4
	adds r1, r1, r0
	ldr r0, [r1]
	cmp r0, #0xff
	beq _0802E4D4
	movs r0, #0
	b _0802E4D8
	.align 2, 0
_0802E4CC: .4byte 0x03000288
_0802E4D0: .4byte 0x03000210
_0802E4D4:
	movs r0, #3
	rsbs r0, r0, #0
_0802E4D8:
	bx lr
	.align 2, 0

	thumb_func_start sub_802E4DC
sub_802E4DC: @ 0x0802E4DC
	ldr r0, _0802E4EC @ =0x030002AC
	ldr r0, [r0]
	cmp r0, #2
	beq _0802E4F0
	movs r0, #8
	rsbs r0, r0, #0
	b _0802E4F2
	.align 2, 0
_0802E4EC: .4byte 0x030002AC
_0802E4F0:
	movs r0, #0
_0802E4F2:
	bx lr

	thumb_func_start sub_0802E4F4
sub_0802E4F4: @ 0x0802E4F4
	ldr r0, _0802E4FC @ =0x03000250
	ldrh r0, [r0, #0xc]
	bx lr
	.align 2, 0
_0802E4FC: .4byte 0x03000250

	thumb_func_start sub_0802E500
sub_0802E500: @ 0x0802E500
	ldr r1, _0802E50C @ =0x03000210
	lsls r0, r0, #4
	adds r0, r0, r1
	ldrh r0, [r0, #0xc]
	bx lr
	.align 2, 0
_0802E50C: .4byte 0x03000210

	thumb_func_start sub_0802E510
sub_0802E510: @ 0x0802E510
	ldr r0, _0802E528 @ =0x03000250
	ldr r1, _0802E52C @ =0x030002B8
	ldr r1, [r1]
	ldr r2, [r0, #4]
	ldr r0, [r0, #8]
	ldr r1, [r1, #8]
	cmp r2, r0
	bhi _0802E530
	subs r0, r0, r2
	asrs r1, r0, #1
	b _0802E536
	.align 2, 0
_0802E528: .4byte 0x03000250
_0802E52C: .4byte 0x030002B8
_0802E530:
	subs r0, r2, r0
	asrs r0, r0, #1
	subs r1, r1, r0
_0802E536:
	ldr r2, _0802E544 @ =0x030002CC
	ldr r0, [r2]
	cmp r1, r0
	ble _0802E540
	str r1, [r2]
_0802E540:
	adds r0, r1, #0
	bx lr
	.align 2, 0
_0802E544: .4byte 0x030002CC

	thumb_func_start sub_802E548
sub_802E548: @ 0x0802E548
	ldr r1, _0802E570 @ =0x03000210
	lsls r2, r0, #4
	adds r3, r1, #4
	adds r3, r2, r3
	adds r1, #8
	adds r2, r2, r1
	ldr r1, _0802E574 @ =0x030002B8
	ldr r1, [r1]
	lsls r0, r0, #3
	adds r1, #0x10
	adds r1, r1, r0
	ldr r3, [r3]
	ldr r0, [r2]
	ldr r1, [r1]
	cmp r3, r0
	bhi _0802E578
	subs r0, r0, r3
	asrs r1, r0, #1
	b _0802E57E
	.align 2, 0
_0802E570: .4byte 0x03000210
_0802E574: .4byte 0x030002B8
_0802E578:
	subs r0, r3, r0
	asrs r0, r0, #1
	subs r1, r1, r0
_0802E57E:
	ldr r2, _0802E58C @ =0x030002D0
	ldr r0, [r2]
	cmp r1, r0
	ble _0802E588
	str r1, [r2]
_0802E588:
	adds r0, r1, #0
	bx lr
	.align 2, 0
_0802E58C: .4byte 0x030002D0

	thumb_func_start sub_0802E590
sub_0802E590: @ 0x0802E590
	ldr r0, _0802E598 @ =0x030002CC
	ldr r0, [r0]
	bx lr
	.align 2, 0
_0802E598: .4byte 0x030002CC

	thumb_func_start sub_0802E59C
sub_0802E59C: @ 0x0802E59C
	ldr r0, _0802E5A4 @ =0x030002D0
	ldr r0, [r0]
	bx lr
	.align 2, 0
_0802E5A4: .4byte 0x030002D0

	thumb_func_start sub_0802E5A8
sub_0802E5A8: @ 0x0802E5A8
	push {lr}
	bl sub_802E1C4
	bl sub_802E2A8
	pop {r1}
	bx r1
	.align 2, 0

	thumb_func_start sub_0802E5B8
sub_0802E5B8: @ 0x0802E5B8
	push {lr}
	bl sub_802E1C4
	pop {r0}
	bx r0
	.align 2, 0

	thumb_func_start sub_802E5C4
sub_802E5C4: @ 0x0802E5C4
	push {lr}
	bl sub_802E2A8
	pop {r1}
	bx r1
	.align 2, 0

	thumb_func_start sub_0802E5D0
sub_0802E5D0: @ 0x0802E5D0
	ldr r0, _0802E5E4 @ =0x030002AC
	ldr r0, [r0]
	movs r1, #8
	rsbs r1, r1, #0
	cmp r0, #2
	bne _0802E5DE
	movs r1, #0
_0802E5DE:
	adds r0, r1, #0
	bx lr
	.align 2, 0
_0802E5E4: .4byte 0x030002AC

	thumb_func_start sub_802E5E8
sub_802E5E8: @ 0x0802E5E8
	ldr r1, _0802E600 @ =0x0400010E
	movs r0, #0
	strh r0, [r1]
	ldr r2, _0802E604 @ =0x04000128
	ldr r0, _0802E608 @ =0x03000268
	ldr r0, [r0]
	movs r3, #0xc1
	lsls r3, r3, #7
	adds r1, r3, #0
	orrs r0, r1
	strh r0, [r2]
	bx lr
	.align 2, 0
_0802E600: .4byte 0x0400010E
_0802E604: .4byte 0x04000128
_0802E608: .4byte 0x03000268

	thumb_func_start sub_802E60C
sub_802E60C: @ 0x0802E60C
	push {lr}
	bl sub_802DCF0
	ldr r0, _0802E624 @ =0x030002B8
	ldr r0, [r0]
	ldr r0, [r0, #0x38]
	cmp r0, #0
	beq _0802E620
	bl _call_via_r0
_0802E620:
	pop {r0}
	bx r0
	.align 2, 0
_0802E624: .4byte 0x030002B8

	thumb_func_start sub_802E628
sub_802E628: @ 0x0802E628
	movs r0, #2
	rsbs r0, r0, #0
	bx lr
	.align 2, 0