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
 *   AfaComm.h
 *
 * Abstract:
 *   This module defines all of the external interfaces to the AFA comm layer.
 *
 *
 *
 --*/
#ifndef _AFACOMM_
#define _AFACOMM_

#include "fsaport.h"

typedef void	*PFIB_CONTEXT;

typedef void (*PFIB_CALLBACK)(void * FibCallbackContext, PFIB_CONTEXT FibContext, AAC_STATUS Status);
typedef PFIB_CONTEXT (*PAFA_COMM_ALLOCATE_FIB) (void * AdapterExtension);
typedef void (*PAFA_COMM_FREE_FIB) (PFIB_CONTEXT FibContext);
typedef AAC_STATUS(*PAFA_COMM_DEALLOCATE_FIB) (PFIB_CONTEXT FibContext);
typedef void(*PAFA_COMM_FREE_FIB_FROM_DPC) (PFIB_CONTEXT FibContext);
typedef AAC_STATUS(*PAFA_COMM_INITIALIZE_FIB) (PFIB_CONTEXT FibContext);
typedef void *(*PAFA_COMM_GET_FIB_DATA) (PFIB_CONTEXT FibContext);
typedef AAC_STATUS(*PAFA_COMM_SEND_FIB) (FIB_COMMAND Command, PFIB_CONTEXT FibContext, u32 Size, COMM_PRIORITIES Priority, int Wait, void * WaitOn, int ResponseExpected, PFIB_CALLBACK FibCallback, void * FibCallbackContext);
typedef AAC_STATUS(*PAFA_COMM_COMPLETE_FIB) (PFIB_CONTEXT FibContext);
typedef AAC_STATUS(*PAFA_COMM_COMPLETE_ADAPTER_FIB) (PFIB_CONTEXT FibContext, u16 Size);
typedef int (*PAFA_COMM_SEND_SYNCH_FIB) (void * AdapterExtension, FIB_COMMAND Command, void *	Data, u16 Size, void * Response, u16 *ResponseSize);

typedef struct _AFACOMM_FUNCS 
{
	u32					SizeOfAfaCommFuncs;
	PAFA_COMM_ALLOCATE_FIB			AllocateFib;
	PAFA_COMM_FREE_FIB			FreeFib;
	PAFA_COMM_FREE_FIB_FROM_DPC		FreeFibFromDpc;
	PAFA_COMM_DEALLOCATE_FIB		DeallocateFib;
	PAFA_COMM_INITIALIZE_FIB		InitializeFib;
	PAFA_COMM_GET_FIB_DATA			GetFibData;
	PAFA_COMM_SEND_FIB			SendFib;
	PAFA_COMM_COMPLETE_FIB			CompleteFib;
	PAFA_COMM_COMPLETE_ADAPTER_FIB		CompleteAdapterFib;
	PAFA_COMM_SEND_SYNCH_FIB		SendSynchFib;
	PFSA_ADAPTER_ADDR_TO_SYSTEM_ADDR	AdapterAddressToSystemAddress;
} AFACOMM_FUNCS;
typedef AFACOMM_FUNCS *PAFACOMM_FUNCS;


typedef AAC_STATUS(*PAFA_CLASS_OPEN_ADAPTER) (void * Adapter);
typedef AAC_STATUS(*PAFA_CLASS_CLOSE_ADAPTER) (void * Adapter);
typedef int(*PAFA_CLASS_DEV_CONTROL) (void * Adapter, PAFA_IOCTL_CMD IoctlCmdPtr, int * Status);
typedef int(*PAFA_CLASS_HANDLE_AIF) (void * Adapter, PFIB_CONTEXT FibContext);

typedef struct _AFA_NEW_CLASS_DRIVER {
	void *			        ClassDriverExtension;
	PAFA_CLASS_OPEN_ADAPTER	        OpenAdapter;
        PAFA_CLASS_CLOSE_ADAPTER	CloseAdapter;
        PAFA_CLASS_DEV_CONTROL 	        DeviceControl;
	PAFA_CLASS_HANDLE_AIF	        HandleAif;
	PFSA_USER_VAR			UserVars;
	u32				NumUserVars;
} AFA_NEW_CLASS_DRIVER;
typedef AFA_NEW_CLASS_DRIVER *PAFA_NEW_CLASS_DRIVER;


typedef struct _AFA_NEW_CLASS_DRIVER_RESPONSE {
	PAFACOMM_FUNCS		CommFuncs;
	void *			CommPortExtension;
	void *			MiniPortExtension;
	OS_SPINLOCK_COOKIE	SpinLockCookie;
	void			*Dip;
} AFA_NEW_CLASS_DRIVER_RESPONSE;
typedef AFA_NEW_CLASS_DRIVER_RESPONSE *PAFA_NEW_CLASS_DRIVER_RESPONSE;


typedef struct _AFA_CLASS_DRIVER {
	struct _AFA_CLASS_DRIVER	*Next;
	void *				ClassDriverExtension;
	PAFA_CLASS_OPEN_ADAPTER		OpenAdapter;
	PAFA_CLASS_CLOSE_ADAPTER	CloseAdapter;
	PAFA_CLASS_DEV_CONTROL 		DeviceControl;
	PAFA_CLASS_HANDLE_AIF		HandleAif;
} AFA_CLASS_DRIVER;
typedef AFA_CLASS_DRIVER *PAFA_CLASS_DRIVER;


#endif // _AFACOMM_
