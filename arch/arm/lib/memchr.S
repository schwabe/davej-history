/*
 * linux/arch/arm/lib/memchr.S
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

@ I have this hunch that this one can be optimised further.
@ Beware false optimisation of ldrb the byte BEYOND the end of buffer (could
@ be beyond end of memory) before testing to see if length had been exhausted
@ on the previous byte.
ENTRY(memchr)				@ 2nd argument char
		stmfd	sp!, {lr}
		and	r1, r1, #0xff	@ cast 2nd argument to char
		add	r2, r2, r0	@ Point to end (exclusive)

1:		teq	r0, r2
		beq	2f		@ No more to do
		ldrb	r3, [r0], #1
		teq	r3, r1
		bne	1b
		sub	r0, r0, #1
		LOADREGS(fd, sp!, {pc})

2:		mov	r0, #0		@ Got to end and not found.
		LOADREGS(fd, sp!, {pc})
