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
 *   commfibcontext.h
 *
 * Abstract: defines the _COMM_FIB_CONTEXT strcuture
 *
 *
 *
 --*/
#ifndef _COMM_FIB_CONTEXT_
#define _COMM_FIB_CONTEXT_

typedef struct _COMM_FIB_CONTEXT {
	void *		Next;	// this is used by the zone allocation

	//
	//  Type and size of this record (must be FSA_NTC_FIB_CONTEXT)
	//
	//  NOTE:  THIS STRUCTURE MUST REMAIN 64-bit ALIGNED IN SIZE, SINCE
	//         IT IS ZONE ALLOCATED, AND REPINNED_BCBS_ARRAY_SIZE AFFECTS
	//         ITS SIZE.
	//

	NODE_TYPE_CODE NodeTypeCode;
	NODE_BYTE_SIZE NodeByteSize;

	//
	//	The Adapter that this I/O is destined for.
	//

	PAFA_COMM_ADAPTER Adapter;

	PHYSICAL_ADDRESS LogicalFibAddress;

	//
	// This is the event the sendfib routine will wait on if the
	// caller did not pass one and this is synch io.
	//

	OS_CV_T 	FsaEvent;
	OS_CVLOCK	*FsaEventMutex;

	u32	FibComplete;	// gets set to 1 when fib is complete
    
	PFIB_CALLBACK FibCallback;
	void * FibCallbackContext;

	u32	Flags;


#ifdef GATHER_FIB_TIMES
	LARGE_INTEGER 	FibTimeStamp;
	PFIB_TIMES		FibTimesPtr;
#endif

	//
	// The following is used to put this fib context onto the Outstanding I/O queue.
	//
		
	LIST_ENTRY	QueueEntry;	

	//
	// The following is used to timeout a fib to the adapter.
	//

	LARGE_INTEGER	TimeoutValue;

	void *FibData;
	PFIB Fib;
} COMM_FIB_CONTEXT;

typedef COMM_FIB_CONTEXT *PCOMM_FIB_CONTEXT;
#define FIB_CONTEXT_FLAG_TIMED_OUT		(0x00000001)

#endif /* _COMM_FIB_CONTEXT_ */
