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
 *   fsatypes.h
 *
 * Abstract: Define all shared data types here, ie, those
 *           types shared among several components, such
 *           as host (driver + apps), adapter, and BIOS.
 *
 *
 --*/
#ifndef _FSATYPES_H
#define _FSATYPES_H

typedef	AAC_UINT32		AAC_BOOLEAN;

//
// Define a 64-bit address structure for use on
// a 32-bit processor architecture.
//
typedef struct {
	AAC_UINT32		lo32;
	AAC_UINT32		hi32;
} AAC_UINT64S, *PAAC_UINT64S;

//
// Container Types
//
typedef struct {
   AAC_UINT32 data[2];      // RMA FIX, make this a real serial number when we
	                    // know what it looks like.  Note, BIOS sees this
	                    // definition and it must be coded in such a way
	                    // that it appears to be 64 bits.  ints are 16 bits
	                    // in BIOS land; fortunately, longs are 32 bits.
} SerialNumberT;



//
//	***********************
//	DON'T CHANGE THE ORDER, ctdevsw use this order to map the drivers
//	***********************
//	drivers for CT_NONE to CT_PASSTHRU
//

typedef enum _FSAVOLTYPE {
	CT_NONE = 0,				
	CT_VOLUME,					
	CT_MIRROR,
	CT_STRIPE,
	CT_RAID5,
	CT_SSRW,
	CT_SSRO,
	CT_MORPH,
	CT_PASSTHRU,
	CT_RAID4,
	CT_RAID10,			// stripe of mirror
	CT_RAID00,			// stripe of stripe
	CT_VOLUME_OF_MIRRORS,		// volume of mirror
	CT_PSEUDO_RAID3,		// really raid4
	CT_LAST_VOLUME_TYPE
} _E_FSAVOLTYPE;

#ifdef AAC_32BIT_ENUMS
typedef	_E_FSAVOLTYPE	FSAVOLTYPE;
#else
typedef	AAC_UINT32	FSAVOLTYPE;
#endif


//
// Types of objects addressable in some fashion by the client.
// This is a superset of those objects handled just by the filesystem
// and includes "raw" objects that an administrator would use to
// configure containers and filesystems.
//
typedef enum _FTYPE {
    FT_REG = 1,     // regular file
    FT_DIR,         // directory
    FT_BLK,         // "block" device - reserved
    FT_CHR,         // "character special" device - reserved
    FT_LNK,         // symbolic link
    FT_SOCK,        // socket
    FT_FIFO,        // fifo
    FT_FILESYS,     // ADAPTEC's "FSA"(tm) filesystem
    FT_DRIVE,       // physical disk - addressable in scsi by bus/target/lun
    FT_SLICE,       // virtual disk - raw volume - slice
    FT_PARTITION,   // FSA partition - carved out of a slice - building block for containers
    FT_VOLUME,      // Container - Volume Set
    FT_STRIPE,      // Container - Stripe Set
    FT_MIRROR,      // Container - Mirror Set
    FT_RAID5,       // Container - Raid 5 Set
    FT_DATABASE     // Storage object with "foreign" content manager
} _E_FTYPE;

#ifdef AAC_32BIT_ENUMS
typedef	_E_FTYPE	FTYPE;
#else
typedef	AAC_UINT32	FTYPE;
#endif

//
// Host side memory scatter gather list
// Used by the adapter for read, write, and readdirplus operations
//
typedef  PAAC_UINT8 HOSTADDRESS;

typedef struct _SGENTRY {
	HOSTADDRESS 	SgAddress;		/* 32-bit Base address. */
	AAC_UINT32	SgByteCount;		/* Length. */
} SGENTRY;

typedef SGENTRY *PSGENTRY;

//
// SGMAP
//
// This is the SGMAP structure for all commands that use
// 32-bit addressing.
//
// Note that the upper 16 bits of SgCount are used as flags.
// Only the lower 16 bits of SgCount are actually used as the
// SG element count.
//
typedef struct _SGMAP {
	AAC_UINT32		SgCount;
	SGENTRY			SgEntry[1];
} SGMAP;
typedef SGMAP *PSGMAP;

//
// SGMAP64
//
// This is the SGMAP structure for 64-bit container commands.
//
typedef struct _SGMAP64 {
	AAC_UINT8	SgCount;
	AAC_UINT8	SgSectorsPerPage;
	AAC_UINT16	SgByteOffset; // For the first page 
	AAC_UINT64S	SgEntry[1];	// Must be last entry
} SGMAP64;
typedef SGMAP64 *PSGMAP64;




//
// attempt at common time structure across host and adapter
//
typedef struct __TIME_T {
	AAC_UINT32	tv_sec;		/* seconds (maybe, depends upon host) */
	AAC_UINT32	tv_usec;	/* and nanoseconds (maybe, depends upon host)*/
} TIME_T;
typedef TIME_T *PTIME_T;

#ifndef _TIME_T
#define timespec __TIME_T
#define ts_sec	tv_sec
#define ts_nsec	tv_usec
#endif

typedef struct _ContainerCreationInfo
{
	AAC_UINT8 		ViaBuildNumber;		// e.g., 588
	AAC_UINT8 		MicroSecond;		// e.g., 588
	AAC_UINT8	 	Via;			// e.g.,	1 = FSU,
							//			2 = API,
	AAC_UINT8	 	YearsSince1900; 	// e.g., 1997 = 97
	AAC_UINT32		Date;			//
							// unsigned 	Month		:4;		// 1 - 12
							// unsigned 	Day			:6;		// 1 - 32
							// unsigned 	Hour		:6;		// 0 - 23
							// unsigned 	Minute		:6;		// 0 - 60
							// unsigned 	Second		:6;		// 0 - 60
	SerialNumberT	ViaAdapterSerialNumber;		// e.g., 0x1DEADB0BFAFAF001
} ContainerCreationInfo;

#endif // _FSATYPES_H


