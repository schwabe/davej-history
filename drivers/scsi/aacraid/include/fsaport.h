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
 *   fsaport.h
 *
 * Abstract: This module defines all of the globally used procedures in the FSA
 *           file system.
 *
 *
 *
 --*/
#ifndef _FSAPORT_
#define _FSAPORT_

//
// The scatter/gather map context is the information we 
// we need to keep the map and transfer data to and from the
// adapter.
//

typedef struct _SGMAP_CONTEXT {
	caddr_t		BaseAddress;
	void * 		MapRegBase;
	u32 		NumberMapRegs;
	PSGMAP		SgMapPtr;
	u32		ByteCount;		// Used to check the Mdl length.
	int		WriteToDevice;
	struct buf	*bp;
} SGMAP_CONTEXT;
typedef SGMAP_CONTEXT *PSGMAP_CONTEXT;

typedef struct _MAPFIB_CONTEXT {
	PMDL	Mdl;    
	void * 	MapRegBase;
	u32 	NumberMapRegs;
	void *	FibVirtualAddress;
	u32	Size;
	void *	FibPhysicalAddress;
} MAPFIB_CONTEXT;
typedef MAPFIB_CONTEXT *PMAPFIB_CONTEXT;

typedef int (*PFSA_ALLOCATE_ADAPTER_COMM_AREA)(void * AdapterExtension, void *	*BaseAddress, u32 Size, u32 Alignment);
typedef int (*PFSA_FREE_ADAPTER_COMM_AREA)(void * AdapterExtension);
typedef void (*PFSA_FREE_DMA_RESOURCES)(void * AdapterExtension, PSGMAP_CONTEXT SgMapContext);
typedef int (*PFSA_ALLOCATE_AND_MAP_FIB_SPACE)(void * AdapterExtension, PMAPFIB_CONTEXT MapFibContext);
typedef int (*PFSA_UNMAP_AND_FREE_FIB_SPACE)(void * AdapterExtension, PMAPFIB_CONTEXT MapFibContext);
typedef void (*PFSA_INTERRUPT_ADAPTER)(void * AdapterExtension);
typedef void(*PFSA_NOTIFY_ADAPTER)(void * AdapterExtension, HOST_2_ADAP_EVENT AdapterEvent);
typedef void (*PFSA_RESET_DEVICE)(void * AdapterExtension);
typedef void * (*PFSA_ADAPTER_ADDR_TO_SYSTEM_ADDR)(void * AdapterExtension, void * AdapterAddress);
typedef void (*PFSA_INTERRUPT_HOST)(void * Adapter, ADAPTER_EVENT AdapterEvent);
typedef void (*PFSA_ENABLE_INTERRUPT)(void * Adapter, ADAPTER_EVENT AdapterEvent, int AtDeviceIrq);
typedef void (*PFSA_DISABLE_INTERRUPT)(void * Adapter, ADAPTER_EVENT AdapterEvent, int AtDeviceIrq);
typedef AAC_STATUS (*PFSA_OPEN_ADAPTER) (void * Adapter);
typedef int (*PFSA_DEVICE_CONTROL) (void * Adapter, PAFA_IOCTL_CMD IoctlCmdPtr);
typedef AAC_STATUS (*PFSA_CLOSE_ADAPTER) (void * Adapter);
typedef int (*PFSA_SEND_SYNCH_FIB) (void * Adapter,u32 FibPhysicalAddress);

typedef struct _FSAPORT_FUNCS {
	u32					SizeOfFsaPortFuncs;

	PFSA_ALLOCATE_ADAPTER_COMM_AREA		AllocateAdapterCommArea;
	PFSA_FREE_ADAPTER_COMM_AREA		FreeAdapterCommArea;
	PFSA_ALLOCATE_AND_MAP_FIB_SPACE		AllocateAndMapFibSpace;
	PFSA_UNMAP_AND_FREE_FIB_SPACE		UnmapAndFreeFibSpace;
	PFSA_INTERRUPT_ADAPTER			InterruptAdapter;
	PFSA_NOTIFY_ADAPTER			NotifyAdapter;
	PFSA_ENABLE_INTERRUPT			EnableInterrupt;
	PFSA_DISABLE_INTERRUPT			DisableInterrupt;
	PFSA_RESET_DEVICE			ResetDevice;
	PFSA_ADAPTER_ADDR_TO_SYSTEM_ADDR	AdapterAddressToSystemAddress;

	PFSA_INTERRUPT_HOST			InterruptHost;
	PFSA_OPEN_ADAPTER			OpenAdapter;
	PFSA_DEVICE_CONTROL			DeviceControl;
	PFSA_CLOSE_ADAPTER			CloseAdapter;

	PFSA_SEND_SYNCH_FIB			SendSynchFib;
} FSAPORT_FUNCS;

typedef FSAPORT_FUNCS *PFSAPORT_FUNCS;

typedef AAC_STATUS (*PFSA_SETVAR_CALLBACK) (void * Adapter, u32 NewValue);

typedef struct _FSA_USER_VAR {
	char			Name[32];
	u32			*Address;
	PFSA_SETVAR_CALLBACK	SetVarCallback;
} FSA_USER_VAR;

typedef FSA_USER_VAR *PFSA_USER_VAR;

typedef struct _FSA_NEW_ADAPTER {
	void *			AdapterExtension;
	PFSAPORT_FUNCS		AdapterFuncs;
	void *			Adapter;
	int			AdapterInterruptsBelowDpc;
	PFSA_USER_VAR		AdapterUserVars;
	u32			AdapterUserVarsSize;
	void			*Dip;
} FSA_NEW_ADAPTER;
typedef FSA_NEW_ADAPTER *PFSA_NEW_ADAPTER;

#define	FSAFS_GET_NEXT_ADAPTER			CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 2048, METHOD_NEITHER, FILE_ANY_ACCESS)
#define	FSAFS_INIT_NEW_ADAPTER			CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 2049, METHOD_NEITHER, FILE_ANY_ACCESS)

#endif
