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
 *   nvramioctl.h
 *
 * Abstract: This file defines the data structures related to querying
 *    and controlling the FSA NVRAM/WriteCache subsystem via the NVRAMIOCTL FIB.
 *
 --*/
#ifndef _NVRAMIOCTL_H_
#define _NVRAMIOCTL_H_ 1

/*
 * NVRAM/Write Cache subsystem states
 */
typedef enum _NVSTATUS {
	NVSTATUS_DISABLED = 0,		// present, clean, not being used
	NVSTATUS_ENABLED,		// present, possibly dirty, ready for use
	NVSTATUS_ERROR,			// present, dirty, contains dirty data
					// for bad/missing device
	NVSTATUS_BATTERY,		// present, bad or low battery, may contain dirty data
					// for bad/missing device
	NVSTATUS_UNKNOWN		// present?????
} _E_NVSTATUS;

#ifdef AAC_32BIT_ENUMS
typedef _E_NVSTATUS	NVSTATUS;
#else
typedef AAC_UINT32	NVSTATUS;
#endif

/*
 * NVRAM/Write Cache subsystem battery component states
 *
 */
//NB: this enum should be identical to battery_status in nvram.h
//	  or else collapsed into one enum someday
typedef enum _NVBATTSTATUS {
	NVBATTSTATUS_NONE = 0,		// battery has no power or is not present
	NVBATTSTATUS_LOW,		// battery is low on power
	NVBATTSTATUS_OK,		// battery is okay - normal operation possible only in this state
	NVBATTSTATUS_RECONDITIONING	// no battery present - reconditioning in process
} _E_NVBATTSTATUS;

#ifdef AAC_32BIT_ENUMS
typedef	_E_NVBATTSTATUS	NVBATTSTATUS;
#else
typedef AAC_UINT32	NVBATTSTATUS;
#endif

/*
 * battery transition type
 */
typedef enum _NVBATT_TRANSITION {
	NVBATT_TRANSITION_NONE = 0,	// battery now has no power or is not present
	NVBATT_TRANSITION_LOW,		// battery is now low on power
	NVBATT_TRANSITION_OK		// battery is now okay - normal operation possible only in this state
} _E_NVBATT_TRANSITION;

#ifdef AAC_32BIT_ENUMS
typedef _E_NVBATT_TRANSITION	NVBATT_TRANSITION;
#else
typedef	AAC_UINT32		NVBATT_TRANSITION;
#endif

/*
 * NVRAM Info structure returned for NVRAM_GetInfo call
 */
typedef struct _NVRAMDEVINFO {
	AAC_UINT32		NV_Enabled;		/* write caching enabled */
	AAC_UINT32		NV_Error;		/* device in error state */
	AAC_UINT32		NV_NDirty;		/* count of dirty NVRAM buffers */
	AAC_UINT32		NV_NActive;		/* count of NVRAM buffers being written */
} NVRAMDEVINFO, *PNVRAMDEVINFO;

typedef struct _NVRAMINFO {
	NVSTATUS		NV_Status;		/* nvram subsystem status */
	NVBATTSTATUS		NV_BattStatus;		/* battery status */
	AAC_UINT32		NV_Size;		/* size of WriteCache NVRAM in bytes */
	AAC_UINT32		NV_BufSize;		/* size of NVRAM buffers in bytes */
	AAC_UINT32		NV_NBufs;		/* number of NVRAM buffers */
	AAC_UINT32		NV_NDirty;		/* count of dirty NVRAM buffers */
	AAC_UINT32		NV_NClean;		/* count of clean NVRAM buffers */
	AAC_UINT32		NV_NActive;		/* count of NVRAM buffers being written */
	AAC_UINT32		NV_NBrokered;		/* count of brokered NVRAM buffers */
	NVRAMDEVINFO		NV_DevInfo[NFILESYS];	/* per device info */
	AAC_UINT32		NV_BattNeedsReconditioning;	/* boolean */
	AAC_UINT32		NV_TotalSize;		/* total size of all non-volatile memories in bytes */
} NVRAMINFO, *PNVRAMINFO;

#endif /* !_NVRAMIOCTL_H_ */


