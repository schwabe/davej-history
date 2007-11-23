/*
 * linux/arch/arm/lib/strchr.S
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

ENTRY(strchr)
		ldrb	r2, [r0], #1
		stmfd	sp!,{lr}	@ StrongARM: LDR not followed by use
1:		teq	r2, r1
		teqne	r2, #0
		ldrneb	r2, [r0], #1
		bne	1b
		teq	r2, r1		@ If they are the same we found char.
		movne	r0, #0
		subeq	r0, r0, #1	@ Even if we were looking for 0 :-)
		LOADREGS(fd, sp!, {pc})
