/*
 * linux/include/asm-arm/arch-ebsa110/system.h
 *
 * Copyright (C) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#define arch_do_idle()		processor.u.armv3v4._do_idle()

#define arch_reset(mode)	processor.u.armv3v4.reset(0x80000000)

#endif
