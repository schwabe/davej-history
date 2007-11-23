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
 *   comproto.h
 *
 * Abstract: Global routines for the commuication interface that are device
 *           independant.
 *
 *
 *
 --*/
#ifndef _COMM_PROTO
#define _COMM_PROTO

//
// define the routines we need so we can commuicate with the
// fsa adapter
//

//
// The following 4 dpc routines will support commuication from the adapter to the
// host. There is one DPC routine to deal with each type of queue that supports
// commuication from the adapter. (adapter to host resposes, adapter to host commands)
// These routines will simply pull off the QE and set an event. In the case of a
// adapter to host command they will also put the FIB on a queue to be processed by
// a FS thread running at passive level.
//

// Handle queue not full notification to the file system thread waiting for a queue entry
u_int CommonNotFullDpc(PCOMM_REGION CommRegion);

// Adapter to host normal priority responses
u_int HostResponseNormalDpc(PCOMM_QUE OurQueue);

// Adapter to host high priority responses
u_int HostResponseHighDpc(PCOMM_QUE OurQueue);

// Adapter to host high priority commands
u_int HostCommandHighDpc(PCOMM_QUE OurQueue);

// Adapter to host normal priority commands
u_int HostCommandNormDpc(PCOMM_QUE OurQueue);
int SendSynchFib(void * Arg, FIB_COMMAND Command, void * Data, u16 Size, void * Response, u16 *ResponseSize);
PFIB_CONTEXT AllocateFib(void * Adapter);
void FreeFib (PFIB_CONTEXT FibContext);
void FreeFibFromDpc(PFIB_CONTEXT FibContext);
AAC_STATUS DeallocateFib(PFIB_CONTEXT FibContext);
AAC_STATUS SendFib(FIB_COMMAND Command,  PFIB_CONTEXT FibContext, u32 Size, COMM_PRIORITIES Priority, int Wait, void * WaitOn, int ResponseExpected, PFIB_CALLBACK FibCallback, void *FibCallbackContext);
AAC_STATUS CompleteFib(PFIB_CONTEXT FibContext);
AAC_STATUS CompleteAdapterFib(PFIB_CONTEXT FibContext, u16 Size);
AAC_STATUS InitializeFib(PFIB_CONTEXT FibContext);
void *FsaGetFibData(PFIB_CONTEXT FibContext);
AAC_STATUS AfaCommOpenAdapter(void *AdapterArg);
AAC_STATUS AfaCommCloseAdapter(void *AdapterArg);
void AfaCommInterruptHost(void *Adapter,ADAPTER_EVENT AdapterEvent);

#endif // _COMM_PROTO
