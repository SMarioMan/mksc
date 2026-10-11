	.include "asm/macros.inc"

	.syntax unified
	.text

	thumb_func_start sub_802D708
sub_802D708: @ 0x0802D708
	push {r4, r5, r6, lr}
	bl sub_802E1C4
	ldr r1, _0802D758 @ =0x03002C80
	ldrh r2, [r1, #0x16]
	movs r0, #0xff
	lsls r0, r0, #8
	ands r0, r2
	movs r2, #0
	strh r0, [r1, #0x16]
	movs r0, #1
	rsbs r0, r0, #0
	strb r0, [r1]
	strb r2, [r1, #1]
	movs r2, #0
	adds r6, r1, #0
	adds r6, #0xd
	movs r3, #0
	adds r5, r1, #0
	adds r5, #0x11
	adds r4, r1, #0
_0802D732:
	lsls r0, r2, #0x18
	lsrs r0, r0, #0x18
	adds r1, r0, r6
	strb r3, [r1]
	adds r0, r0, r5
	strb r3, [r0]
	adds r2, #1
	cmp r2, #3
	ble _0802D732
	movs r0, #0
	strb r0, [r4, #0xa]
	strb r0, [r4, #0xb]
	strb r0, [r4, #0xc]
	ldr r1, _0802D75C @ =0x00003FFF
	ldr r0, _0802D760 @ =0x03000208
	strh r1, [r0]
	pop {r4, r5, r6}
	pop {r0}
	bx r0
	.align 2, 0
_0802D758: .4byte 0x03002C80
_0802D75C: .4byte 0x00003FFF
_0802D760: .4byte 0x03000208

	thumb_func_start sio2_vblank
sio2_vblank: @ 0x0802D764
	push {r4, r5, r6, lr}
	ldr r6, _0802D7D8 @ =0x03000208
	ldrh r5, [r6]
	bl sub_802E4DC
	movs r1, #0
	cmp r0, #0
	bne _0802D776
	movs r1, #1
_0802D776:
	cmp r1, #0
	beq _0802D7F4
	ldr r2, _0802D7DC @ =0x03002C80
	movs r1, #0
	movs r0, #0
	ldrsb r0, [r2, r0]
	cmp r0, #0
	bne _0802D788
	movs r1, #1
_0802D788:
	cmp r1, #0
	beq _0802D814
	ldr r0, _0802D7E0 @ =0x04000128
	ldrh r1, [r0]
	movs r0, #0x80
	ands r0, r1
	cmp r0, #0
	beq _0802D7CC
	movs r0, #0
	strh r0, [r2, #0x16]
	ldr r2, _0802D7E4 @ =0x04000134
	ldrb r0, [r2, #1]
	movs r1, #0xc0
	orrs r0, r1
	strb r0, [r2, #1]
	movs r0, #1
	movs r1, #0xc1
	rsbs r1, r1, #0
	bl irq_updateMask
	ldr r1, _0802D7E8 @ =0x04000202
	movs r0, #0xc0
	strh r0, [r1]
	ldr r4, _0802D7EC @ =main_dummyIrqHandler
	movs r0, #7
	adds r1, r4, #0
	bl irq_setHandler
	movs r0, #6
	adds r1, r4, #0
	bl irq_setHandler
	bl sub_802D708
_0802D7CC:
	adds r0, r5, #0
	bl sub_802E010
	ldr r0, _0802D7F0 @ =0x00003FFF
	strh r0, [r6]
	b _0802D814
	.align 2, 0
_0802D7D8: .4byte 0x03000208
_0802D7DC: .4byte 0x03002C80
_0802D7E0: .4byte 0x04000128
_0802D7E4: .4byte 0x04000134
_0802D7E8: .4byte 0x04000202
_0802D7EC: .4byte main_dummyIrqHandler
_0802D7F0: .4byte 0x00003FFF
_0802D7F4:
	ldr r0, _0802D81C @ =0x03002C80
	movs r1, #0
	ldrsb r1, [r0, r1]
	movs r0, #1
	rsbs r0, r0, #0
	cmp r1, r0
	bne _0802D804
	movs r1, #0
_0802D804:
	ldr r0, _0802D820 @ =0x04000130
	ldrh r2, [r0]
	ldr r0, _0802D824 @ =0x03004F58
	lsls r1, r1, #1
	adds r1, r1, r0
	ldr r0, _0802D828 @ =0x000003FF
	ands r0, r2
	strh r0, [r1]
_0802D814:
	pop {r4, r5, r6}
	pop {r0}
	bx r0
	.align 2, 0
_0802D81C: .4byte 0x03002C80
_0802D820: .4byte 0x04000130
_0802D824: .4byte 0x03004F58
_0802D828: .4byte 0x000003FF

	thumb_func_start sio2_update
sio2_update: @ 0x0802D82C
	push {r4, r5, r6, r7, lr}
	mov r7, sl
	mov r6, sb
	mov r5, r8
	push {r5, r6, r7}
	sub sp, #0x10
	movs r0, #1
	str r0, [sp, #0xc]
	ldr r4, _0802D8D8 @ =0x03002C80
	ldrh r1, [r4, #0x16]
	movs r0, #0x80
	lsls r0, r0, #8
	ands r0, r1
	cmp r0, #0
	beq _0802D8D4
	bl sub_802E5C4
	adds r2, r0, #0
	movs r0, #1
	rsbs r0, r0, #0
	cmp r2, r0
	bne _0802D882
	movs r0, #0
	ldrsb r0, [r4, r0]
	cmp r0, r2
	beq _0802D882
	ldrh r0, [r4, #0x16]
	movs r1, #0xff
	lsls r1, r1, #8
	ands r1, r0
	movs r0, #0
	strh r1, [r4, #0x16]
	strb r0, [r4, #1]
	movs r1, #0
	adds r4, #0xd
	movs r3, #0
_0802D874:
	lsls r0, r1, #0x18
	lsrs r0, r0, #0x18
	adds r0, r0, r4
	strb r3, [r0]
	adds r1, #1
	cmp r1, #3
	ble _0802D874
_0802D882:
	ldr r5, _0802D8D8 @ =0x03002C80
	movs r7, #0
	strb r2, [r5]
	ldr r6, _0802D8DC @ =0x03004F38
	ldr r0, [r6]
	cmp r0, #0xa
	ble _0802D8C4
	strh r7, [r5, #0x16]
	ldr r2, _0802D8E0 @ =0x04000134
	ldrb r0, [r2, #1]
	movs r1, #0xc0
	orrs r0, r1
	strb r0, [r2, #1]
	movs r0, #1
	movs r1, #0xc1
	rsbs r1, r1, #0
	bl irq_updateMask
	ldr r1, _0802D8E4 @ =0x04000202
	movs r0, #0xc0
	strh r0, [r1]
	ldr r4, _0802D8E8 @ =main_dummyIrqHandler
	movs r0, #7
	adds r1, r4, #0
	bl irq_setHandler
	movs r0, #6
	adds r1, r4, #0
	bl irq_setHandler
	bl sub_802D708
	str r7, [r6]
_0802D8C4:
	bl sub_802E4DC
	movs r1, #0
	cmp r0, #0
	bne _0802D8D0
	movs r1, #1
_0802D8D0:
	cmp r1, #0
	bne _0802D8EC
_0802D8D4:
	movs r0, #0
	b _0802DB44
	.align 2, 0
_0802D8D8: .4byte 0x03002C80
_0802D8DC: .4byte 0x03004F38
_0802D8E0: .4byte 0x04000134
_0802D8E4: .4byte 0x04000202
_0802D8E8: .4byte main_dummyIrqHandler
_0802D8EC:
	mov sl, r5
	b _0802DAD8
_0802D8F0:
	ldr r0, _0802D970 @ =0x03004F38
	movs r4, #0
	str r4, [r0]
	mov r0, sp
	bl sub_802DF08
	movs r1, #1
	rsbs r1, r1, #0
	cmp r0, r1
	bne _0802D906
	b _0802DAD2
_0802D906:
	mov r1, sl
	strb r4, [r1, #0xa]
	ldr r2, _0802D974 @ =0x03002C80
	mov ip, r2
	ldr r5, _0802D978 @ =0x0000FFFF
	mov r3, ip
	mov r2, sp
_0802D914:
	ldrh r0, [r2]
	cmp r0, r5
	beq _0802D924
	movs r0, #1
	lsls r0, r4
	ldrb r1, [r3, #0xa]
	orrs r0, r1
	strb r0, [r3, #0xa]
_0802D924:
	adds r2, #2
	adds r4, #1
	cmp r4, #3
	ble _0802D914
	mov r3, ip
	ldrb r0, [r3, #0xa]
	cmp r0, #0
	bne _0802D936
	b _0802DAD2
_0802D936:
	ldrb r1, [r3, #0xb]
	cmp r0, r1
	beq _0802DA08
	movs r4, #0
	mov r5, ip
	movs r2, #0xd
	add r2, ip
	mov r8, r2
	ldr r3, _0802D97C @ =0x00003FFF
	mov sb, r3
	movs r6, #0
	mov r3, sp
	movs r7, #0
_0802D950:
	ldrh r1, [r3]
	adds r2, r1, #0
	ldr r0, _0802D978 @ =0x0000FFFF
	cmp r2, r0
	bne _0802D980
	movs r0, #1
	lsls r0, r4
	ldrb r1, [r5, #0xb]
	bics r1, r0
	strb r1, [r5, #0xb]
	lsrs r0, r6, #0x18
	add r0, r8
	movs r1, #0
	strb r1, [r0]
	b _0802D9B8
	.align 2, 0
_0802D970: .4byte 0x03004F38
_0802D974: .4byte 0x03002C80
_0802D978: .4byte 0x0000FFFF
_0802D97C: .4byte 0x00003FFF
_0802D980:
	mov r0, sb
	ands r0, r1
	cmp r0, sb
	beq _0802D9B8
	ldr r0, _0802D9FC @ =0x00004B4D
	cmp r2, r0
	bne _0802D9A0
	movs r0, #1
	lsls r0, r4
	ldrb r1, [r5, #0xb]
	orrs r0, r1
	strb r0, [r5, #0xb]
	lsrs r0, r6, #0x18
	add r0, r8
	movs r1, #2
	strb r1, [r0]
_0802D9A0:
	ldrb r1, [r5, #0xc]
	adds r0, r1, #0
	asrs r0, r4
	movs r2, #1
	ands r0, r2
	cmp r0, #0
	bne _0802D9B8
	adds r0, r2, #0
	lsls r0, r4
	orrs r1, r0
	mov r2, ip
	strb r1, [r2, #0xc]
_0802D9B8:
	ldr r1, _0802DA00 @ =0x03004F58
	adds r0, r7, r1
	ldr r2, _0802DA04 @ =0x000003FF
	strh r2, [r0]
	movs r0, #0x80
	lsls r0, r0, #0x11
	adds r6, r6, r0
	adds r3, #2
	adds r7, #2
	adds r4, #1
	cmp r4, #3
	ble _0802D950
	movs r2, #2
	mov r3, sl
	ldrh r1, [r3, #0x16]
	movs r3, #0xff
	lsls r3, r3, #8
	adds r0, r3, #0
	ands r1, r0
	movs r3, #0
	orrs r1, r2
	mov r0, sl
	strh r1, [r0, #0x16]
	strb r3, [r0, #1]
	ldrb r0, [r0, #0xc]
	mov r1, sl
	ldrb r1, [r1, #0xa]
	cmp r0, r1
	bne _0802DAD2
	mov r2, ip
	strb r3, [r2, #0xc]
	movs r3, #0
	str r3, [sp, #0xc]
	b _0802DAEE
	.align 2, 0
_0802D9FC: .4byte 0x00004B4D
_0802DA00: .4byte 0x03004F58
_0802DA04: .4byte 0x000003FF
_0802DA08:
	mov r1, ip
	ldrh r0, [r1, #0x16]
	movs r2, #0xff
	lsls r2, r2, #8
	adds r1, r2, #0
	ands r0, r1
	movs r3, #1
	orrs r0, r3
	mov r1, ip
	strh r0, [r1, #0x16]
	movs r4, #0
	movs r2, #0xd
	add r2, ip
	mov sb, r2
	movs r7, #0
	mov r6, ip
	adds r6, #2
	mov r5, sp
	ldr r3, _0802DA48 @ =0x00003FFF
	mov r8, r3
_0802DA30:
	ldrh r2, [r5]
	adds r1, r2, #0
	ldr r0, _0802DA4C @ =0x0000FFFF
	cmp r1, r0
	bne _0802DA50
	strh r2, [r6]
	lsrs r0, r7, #0x18
	add r0, sb
	movs r1, #0
	strb r1, [r0]
	b _0802DA80
	.align 2, 0
_0802DA48: .4byte 0x00003FFF
_0802DA4C: .4byte 0x0000FFFF
_0802DA50:
	mov r0, r8
	ands r0, r2
	cmp r0, r8
	beq _0802DA80
	ldr r3, _0802DB04 @ =0x00004B4D
	cmp r1, r3
	beq _0802DA80
	ldr r0, _0802DB08 @ =0x03002C80
	ldrb r1, [r0, #0xc]
	adds r0, r1, #0
	asrs r0, r4
	movs r3, #1
	ands r0, r3
	cmp r0, #0
	bne _0802DA80
	adds r0, r3, #0
	lsls r0, r4
	orrs r1, r0
	mov r0, ip
	strb r1, [r0, #0xc]
	strh r2, [r6]
	lsrs r0, r7, #0x18
	add r0, sb
	strb r3, [r0]
_0802DA80:
	movs r1, #0x80
	lsls r1, r1, #0x11
	adds r7, r7, r1
	adds r6, #2
	adds r5, #2
	adds r4, #1
	cmp r4, #3
	ble _0802DA30
	mov r2, sl
	ldrb r0, [r2, #0xc]
	ldrb r3, [r2, #0xb]
	cmp r0, r3
	bne _0802DAD2
	movs r5, #0
	movs r4, #0
	ldr r6, _0802DB08 @ =0x03002C80
	ldr r7, _0802DB0C @ =0x000003FF
	adds r3, r6, #2
	ldr r2, _0802DB10 @ =0x03004F58
_0802DAA6:
	ldrb r0, [r6, #0xb]
	asrs r0, r4
	movs r1, #1
	ands r0, r1
	cmp r0, #0
	beq _0802DAB4
	adds r5, #1
_0802DAB4:
	ldrh r1, [r3]
	adds r0, r7, #0
	ands r0, r1
	strh r0, [r2]
	adds r3, #2
	adds r2, #2
	adds r4, #1
	cmp r4, #3
	ble _0802DAA6
	movs r0, #0
	mov r2, sl
	strb r5, [r2, #1]
	strb r0, [r2, #0xc]
	movs r3, #0
	str r3, [sp, #0xc]
_0802DAD2:
	ldr r0, [sp, #0xc]
	cmp r0, #0
	beq _0802DAEE
_0802DAD8:
	movs r0, #0
	bl sub_802E548
	str r0, [sp, #8]
	cmp r0, #0
	ble _0802DAE6
	b _0802D8F0
_0802DAE6:
	ldr r1, _0802DB14 @ =0x03004F38
	ldr r0, [r1]
	adds r0, #1
	str r0, [r1]
_0802DAEE:
	ldr r1, [sp, #8]
	cmp r1, #2
	bgt _0802DB42
	ldr r0, _0802DB08 @ =0x03002C80
	ldrb r0, [r0, #0x16]
	cmp r0, #1
	beq _0802DB18
	cmp r0, #2
	beq _0802DB3C
	b _0802DB42
	.align 2, 0
_0802DB04: .4byte 0x00004B4D
_0802DB08: .4byte 0x03002C80
_0802DB0C: .4byte 0x000003FF
_0802DB10: .4byte 0x03004F58
_0802DB14: .4byte 0x03004F38
_0802DB18:
	ldr r0, _0802DB30 @ =0x04000130
	ldrh r0, [r0]
	ldr r1, _0802DB34 @ =0x000003FF
	ands r1, r0
	ldr r0, _0802DB38 @ =0x03004F50
	ldrh r2, [r0]
	movs r0, #0xe0
	lsls r0, r0, #5
	ands r0, r2
	orrs r1, r0
	b _0802DB3E
	.align 2, 0
_0802DB30: .4byte 0x04000130
_0802DB34: .4byte 0x000003FF
_0802DB38: .4byte 0x03004F50
_0802DB3C:
	ldr r1, _0802DB54 @ =0x00004B4D
_0802DB3E:
	ldr r0, _0802DB58 @ =0x03000208
	strh r1, [r0]
_0802DB42:
	ldr r0, [sp, #0xc]
_0802DB44:
	add sp, #0x10
	pop {r3, r4, r5}
	mov r8, r3
	mov sb, r4
	mov sl, r5
	pop {r4, r5, r6, r7}
	pop {r1}
	bx r1
	.align 2, 0
_0802DB54: .4byte 0x00004B4D
_0802DB58: .4byte 0x03000208

	thumb_func_start sub_0802DB5C
sub_0802DB5C: @ 0x0802DB5C
	push {r4, lr}
	ldr r2, _0802DBA4 @ =0x04000128
	ldrh r1, [r2]
	movs r0, #8
	ands r0, r1
	lsls r0, r0, #0x10
	lsrs r1, r0, #0x10
	cmp r1, #0
	bne _0802DBB4
	ldr r0, _0802DBA8 @ =0x03002C80
	strh r1, [r0, #0x16]
	adds r2, #0xc
	ldrb r0, [r2, #1]
	movs r1, #0xc0
	orrs r0, r1
	strb r0, [r2, #1]
	movs r0, #1
	movs r1, #0xc1
	rsbs r1, r1, #0
	bl irq_updateMask
	ldr r1, _0802DBAC @ =0x04000202
	movs r0, #0xc0
	strh r0, [r1]
	ldr r4, _0802DBB0 @ =main_dummyIrqHandler
	movs r0, #7
	adds r1, r4, #0
	bl irq_setHandler
	movs r0, #6
	adds r1, r4, #0
	bl irq_setHandler
	bl sub_802D708
	b _0802DC10
	.align 2, 0
_0802DBA4: .4byte 0x04000128
_0802DBA8: .4byte 0x03002C80
_0802DBAC: .4byte 0x04000202
_0802DBB0: .4byte main_dummyIrqHandler
_0802DBB4:
	ldr r3, _0802DC18 @ =0x03002C80
	movs r1, #0
	movs r0, #0
	ldrsb r0, [r3, r0]
	cmp r0, #0
	ble _0802DBC2
	movs r1, #1
_0802DBC2:
	cmp r1, #0
	beq _0802DC10
	ldrh r1, [r2]
	movs r0, #0x80
	ands r0, r1
	cmp r0, #0
	beq _0802DC04
	movs r0, #0
	strh r0, [r3, #0x16]
	ldr r2, _0802DC1C @ =0x04000134
	ldrb r0, [r2, #1]
	movs r1, #0xc0
	orrs r0, r1
	strb r0, [r2, #1]
	movs r0, #1
	movs r1, #0xc1
	rsbs r1, r1, #0
	bl irq_updateMask
	ldr r1, _0802DC20 @ =0x04000202
	movs r0, #0xc0
	strh r0, [r1]
	ldr r4, _0802DC24 @ =main_dummyIrqHandler
	movs r0, #7
	adds r1, r4, #0
	bl irq_setHandler
	movs r0, #6
	adds r1, r4, #0
	bl irq_setHandler
	bl sub_802D708
_0802DC04:
	ldr r4, _0802DC28 @ =0x03000208
	ldrh r0, [r4]
	bl sub_802E010
	ldr r0, _0802DC2C @ =0x00003FFF
	strh r0, [r4]
_0802DC10:
	pop {r4}
	pop {r0}
	bx r0
	.align 2, 0
_0802DC18: .4byte 0x03002C80
_0802DC1C: .4byte 0x04000134
_0802DC20: .4byte 0x04000202
_0802DC24: .4byte main_dummyIrqHandler
_0802DC28: .4byte 0x03000208
_0802DC2C: .4byte 0x00003FFF

	thumb_func_start sub_802DC30
sub_802DC30: @ 0x0802DC30
	push {lr}
	ldr r0, _0802DC40 @ =_080DE17C
	bl sub_802E0D4
	bl sub_802D708
	pop {r0}
	bx r0
	.align 2, 0
_0802DC40: .4byte _080DE17C

	thumb_func_start sio2_stop
sio2_stop: @ 0x0802DC44
	push {r4, lr}
	ldr r2, _0802DC7C @ =0x04000134
	ldrb r0, [r2, #1]
	movs r1, #0xc0
	orrs r0, r1
	strb r0, [r2, #1]
	movs r1, #0xc1
	rsbs r1, r1, #0
	movs r0, #1
	bl irq_updateMask
	ldr r1, _0802DC80 @ =0x04000202
	movs r0, #0xc0
	strh r0, [r1]
	ldr r4, _0802DC84 @ =main_dummyIrqHandler
	movs r0, #7
	adds r1, r4, #0
	bl irq_setHandler
	movs r0, #6
	adds r1, r4, #0
	bl irq_setHandler
	bl sub_802D708
	pop {r4}
	pop {r0}
	bx r0
	.align 2, 0
_0802DC7C: .4byte 0x04000134
_0802DC80: .4byte 0x04000202
_0802DC84: .4byte main_dummyIrqHandler

	thumb_func_start sub_802DC88
sub_802DC88: @ 0x0802DC88
	push {lr}
	bl sub_802E4DC
	movs r1, #0
	cmp r0, #0
	bne _0802DC96
	movs r1, #1
_0802DC96:
	adds r0, r1, #0
	pop {r1}
	bx r1

