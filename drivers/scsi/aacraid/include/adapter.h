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
 *   Adapter.h
 *
 * Abstract:
 *   The module contains the definitions for a comm layer view of the adapter.
 *
 *
 *
 --*/

#ifndef _ADAPTER_
#define _ADAPTER_


typedef struct _GET_ADAPTER_FIB_CONTEXT {
	NODE_TYPE_CODE 		NodeTypeCode;	// used for verification of structure	
	NODE_BYTE_SIZE 		NodeByteSize;
	PFILE_OBJECT		FileObject;	// used for cleanup
	LIST_ENTRY		NextContext;	// used to link context's into a linked list
	OS_CV_T 		UserEvent;	// this is used to wait for the next fib to arrive.
	int			WaitingForFib;	// Set to true when thread is in WaitForSingleObject
	u32			FibCount;	// total number of FIBs on FibList
	LIST_ENTRY		FibList;
} GET_ADAPTER_FIB_CONTEXT;
typedef GET_ADAPTER_FIB_CONTEXT *PGET_ADAPTER_FIB_CONTEXT;


typedef struct _FIB_CONTEXT_ZONE_SEGMENT {
	struct _FIB_CONTEXT_ZONE_SEGMENT	*Next;
	u32					FibContextSegmentSize;
	void *					FibContextSegment;
	u32					ExtendSize;
	MAPFIB_CONTEXT				MapFibContext;
} FIB_CONTEXT_ZONE_SEGMENT;
typedef FIB_CONTEXT_ZONE_SEGMENT *PFIB_CONTEXT_ZONE_SEGMENT;

typedef struct _AFA_COMM_ADAPTER {
	struct _AFA_COMM_ADAPTER	*NextAdapter;
	//
	//  The following fields are used to allocate FIB context structures
	//  using the zone allocator, and other fixed sized structures from a
	//  small cache.  The mutex protects access to the zone/lists
	//

	ZONE_HEADER 		FibContextZone;
	OS_SPINLOCK			*FibContextZoneSpinLock;
	int 				FibContextZoneExtendSize;

	PFIB_CONTEXT_ZONE_SEGMENT	FibContextSegmentList;

	void *				FibContextTimedOutList;

	PFIB				SyncFib;
	u32				SyncFibPhysicalAddress;

	PCOMM_REGION 		CommRegion;

	OS_SPINLOCK_COOKIE	SpinLockCookie;

//	UNICODE_STRING		TimeoutString;

#ifdef GATHER_FIB_TIMES
	//
	// The following contains detailed timings of round trip to the adapter
	// depending on which Fib type
	//

	PALL_FIB_TIMES		FibTimes;
	LARGE_INTEGER 		FibTimesFrequency;
#endif

	//
	// The user API will use an IOCTL to register itself to receive FIBs
	// from the adapter.  The following list is used to keep track of all
	// the threads that have requested these FIBs.  The mutex is used to 
	// synchronize access to all data associated with the adapter fibs.
	//
	LIST_ENTRY			AdapterFibContextList;
	OS_CVLOCK			*AdapterFibMutex;

	//
	// The following holds which FileObject is allow to send configuration
	// commands to the adapter that would modify the configuration.
	//
	// This is controlled by the FSACTL_OPEN_ADAPTER_CONFIG and FSACTL_CLOSE_ADAPTER_CONFIG
	// ioctls.
	//
	PFILE_OBJECT		AdapterConfigFileObject;

	//
	// The following is really here because of the simulator
	//
	int				InterruptsBelowDpc;

	//
	// The following is the device specific extension.
	//
	void *			AdapterExtension;	
	PFSAPORT_FUNCS		AdapterFuncs;
	void			*Dip;

	//
	// The following are user variables that are specific to the mini port.
	//
	PFSA_USER_VAR		AdapterUserVars;
	u32			AdapterUserVarsSize;

	//
	// The following is the number of the individual adapter..i.e. \Device\Afa0
	//
	s32			AdapterNumber;

	AFACOMM_FUNCS		CommFuncs;

	PAFA_CLASS_DRIVER	ClassDriverList;

	int			AifThreadStarted;

} AFA_COMM_ADAPTER;

typedef AFA_COMM_ADAPTER *PAFA_COMM_ADAPTER;


#define FsaAllocateAdapterCommArea(Adapter, BaseAddress, Size, Alignment) \
	Adapter->AdapterFuncs->AllocateAdapterCommArea(Adapter->AdapterExtension, BaseAddress, Size, Alignment)

#define FsaFreeAdapterCommArea(Adapter) \
	Adapter->AdapterFuncs->FreeAdapterCommArea(Adapter->AdapterExtension)


#define AllocateAndMapFibSpace(Adapter, MapFibContext) \
	Adapter->AdapterFuncs->AllocateAndMapFibSpace(Adapter->AdapterExtension, MapFibContext)

#define UnmapAndFreeFibSpace(Adapter, MapFibContext) \
	Adapter->AdapterFuncs->UnmapAndFreeFibSpace(Adapter->AdapterExtension, MapFibContext)

#define InterruptAdapter(Adapter) \
	Adapter->AdapterFuncs->InterruptAdapter(Adapter->AdapterExtension)

#define NotifyAdapter(Adapter, AdapterEvent) \
	Adapter->AdapterFuncs->NotifyAdapter(Adapter->AdapterExtension, AdapterEvent)

#define EnableInterrupt(Adapter, AdapterEvent, AtDeviceIrq) \
	Adapter->AdapterFuncs->EnableInterrupt(Adapter->AdapterExtension, AdapterEvent, AtDeviceIrq)

#define DisableInterrupt(Adapter, AdapterEvent, AtDeviceIrq) \
	Adapter->AdapterFuncs->DisableInterrupt(Adapter->AdapterExtension, AdapterEvent, AtDeviceIrq)


#endif // _ADAPTER_
