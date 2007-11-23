/*
 * linux/include/asm-arm/arch-l7200/hardware.h
 *
 * Copyright (C) 2000 Rob Scott (rscott@mtrob.fdns.net)
 *                    Steve Hill (sjhill@cotw.com)
 *
 * This file contains the hardware definitions for the 
 * LinkUp Systems L7200 SOC development board.
 *
 * Changelog:
 *   02-01-2000	 RS	Created L7200 version, derived from rpc code
 *   03-21-2000	SJH	Cleaned up file
 *   04-21-2000	 RS 	Changed mapping of I/O in virtual space
 *   04-25-2000	SJH	Removed unused symbols and such
 *   05-05-2000	SJH	Complete rewrite
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/* Hardware addresses of major areas.
 *  *_START is the physical address
 *  *_SIZE  is the size of the region
 *  *_BASE  is the virtual address
 */
#define RAM_START		0xf0000000
#define RAM_SIZE		0x02000000
#define RAM_BASE		0xc0000000

#define IO_START		0x80000000      /* I/O */
#define IO_SIZE			0x01000000
#define IO_BASE			0xd0000000

#define IO_START_2		0x90000000      /* I/O */
#define IO_SIZE_2		0x01000000
#define IO_BASE_2		0xd1000000

#define ISA_START		0x20000000	/* ISA */
#define ISA_SIZE		0x20000000
#define ISA_BASE		0xe0000000

#define FLUSH_BASE_PHYS		0x40000000	/* ROM */
#define FLUSH_BASE		0xdf000000

#define PARAMS_BASE		(PAGE_OFFSET + 0x0100)
#define Z_PARAMS_BASE		(RAM_START + PARAMS_OFFSET)

#define PCIO_BASE		IO_BASE

#endif