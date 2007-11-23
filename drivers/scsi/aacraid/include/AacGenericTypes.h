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
 *
 *  AacGenericTypes.h
 *
 * Abstract:
 *
 *     The module defines the generic data types that all of the other header files
 *     depend upon.
 --*/
#ifndef _AAC_GENERIC_TYPES
#define _AAC_GENERIC_TYPES

typedef	s8	AAC_INT8, *PAAC_INT8;
typedef s16	AAC_INT16, *PAAC_INT16;
typedef s32	AAC_INT32, *PAAC_INT32;
typedef s64	AAC_INT64, *PAAC_INT64;

typedef u8	AAC_UINT8, *PAAC_UINT8;
typedef u16	AAC_UINT16, *PAAC_UINT16;
typedef u32	AAC_UINT32, *PAAC_UINT32;
typedef u64	AAC_UINT64, *PAAC_UINT64;

typedef void	AAC_VOID, *PAAC_VOID;

//
// this compiler uses 32 bit enum data types
//

#define	AAC_32BIT_ENUMS	1
#define FAILURE 1
#define INTR_UNCLAIMED 1
#define INTR_CLAIMED 0

#endif // _AAC_GENERIC_TYPES

