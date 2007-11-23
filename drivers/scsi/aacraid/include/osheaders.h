/*++
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *   osheaders.h
 *
 * Abstract: Holds all of the header file includes for a particular O/S flavor.
 *
 --*/
#ifndef _OSHEADERS_H_
#define _OSHEADERS_H_

#include <linux/config.h>	// retrieve the kernel configuration info
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/spinlock.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <linux/malloc.h>
#include <linux/tqueue.h>
#include <ostypes.h>
#include "scsi.h"
#include "hosts.h"

#ifndef intptr_t
#define intptr_t void *
#endif

#ifndef cred_t
#define cred_t void
#endif

#ifndef paddr32_t
#define paddr32_t unsigned
#endif

#ifndef bzero 
#define bzero(b,len) memset(b,0,len)
#endif

#ifndef bcopy
#define bcopy(src,dst,len) memcpy(dst,src,len )
#endif

#ifndef DEVICE_NR
#define DEVICE_NR(device) ( ( ( MAJOR( device ) & 7 ) << 4 ) + ( MINOR( device ) >> 4 ) )
#endif

typedef unsigned uint_t;

typedef enum
{
	CE_PANIC = 0,
	CE_WARN,
	CE_NOTE, 
	CE_CONT, 
	CE_DEBUG,
	CE_DEBUG2,
	CE_TAIL
} CE_ENUM_T;

#define CMN_ERR_LEVEL CE_WARN

// usage of READ & WRITE as a typedefs in protocol.h
// conflicts with <linux/fs.h> definition.
#ifdef READ
#undef READ
#endif

#ifdef WRITE
#undef WRITE
#endif

typedef struct aac_options
{
	int message_level;
	int reverse_scan; 
} aac_options_t;

#endif // _OSHEADERS_H_

