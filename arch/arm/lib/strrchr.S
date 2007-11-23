/*
 * linux/arch/arm/lib/strrchr.S
 *
 * Copyright (C) 1995-1999 Russell King
 *
 * ASM optimised string functions
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include "constants.h"

		.text

		@ Technically this will fail a regression test that tries
		@ strrchr (buffer, 0), by returning NULL rather than
		@ buffer + strlen (buffer). But even glibc doesnt test this
ENTRY(strrchr)	@ 2nd argument is int not char
		stmfd	sp!, {lr}
		mov	r3, r0
		ldrb	r2, [r3], #1
		mov	r0, #0		@ prime return result.
1:		teq	r2, r1		@ Found one?
		subeq	r0, r3, #1
		teq	r2, #0
		ldrneb	r2, [r3], #1	@ Assume that well be looping at least
		bne	1b		@ twice, so put LDM outside loop.
		LOADREGS(fd, sp!, {pc})

