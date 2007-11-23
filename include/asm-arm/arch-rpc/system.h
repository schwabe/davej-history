/*
 * linux/include/asm-arm/arch-rpc/system.h
 *
 * Copyright (c) 1996-1999 Russell King
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/iomd.h>
#include <asm/io.h>

#define arch_do_idle()		processor.u.armv3v4._do_idle()

extern __inline__ void arch_reset(char mode)
{
	extern void ecard_reset(int card);

	ecard_reset(-1);

	outb(0, IOMD_ROMCR0);

	/*
	 * Jump into the ROM
	 */
	processor.u.armv3v4.reset(0);
}
#endif
