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
 *   comsup.h
 *
 * Abstract: This module defines the data structures that make up the 
 *           commuication region for the FSA filesystem. This region is
 *           how the host based code commuicates both control and data
 *           to the adapter based code. 
 *
 *
 *
 --*/
#ifndef _COMM_SUP_DEF
#define _COMM_SUP_DEF


//
// The adapter interface specs all queues to be located in the same physically
// contigous block. The host structure that defines the commuication queues will
// assume they are each a seperate physically contigous memory region that will
// support them all being one big contigous block.
// There is a command and response queue for each level and direction of
// commuication. These regions are accessed by both the host and adapter.
//
typedef struct _COMM_QUE {
	PHYSICAL_ADDRESS	LogicalAddress; // This is the address we give the adapter
	PQUEUE_ENTRY 		BaseAddress;    // This is the system virtual address 
	QUEUE_HEADERS 		Headers;        // A pointer to the producer and consumer queue headers for this queue
	u32 			QueueEntries;   // Number of queue entries on this queue
	OS_CV_T			QueueFull;      // Event to wait on if the queue is full
	OS_CV_T			CommandReady;   // Indicates there is a Command ready from the adapter on this queue.
                                		// This is only valid for adapter to host command queues.                                        
	OS_SPINLOCK	 	*QueueLock;	// Spinlock for this queue must take this lock before accessing the lock
	KIRQL 			SavedIrql;	// Previous IRQL when the spin lock is taken
	ddi_softintr_t		ConsumerRoutine;// The DPC routine which will consume entries off this queue
						// Only queues which the host will be the consumer will this field be valid
	LIST_ENTRY 		CommandQueue;	// A queue of FIBs which need to be prcessed by the FS thread. This is
						// only valid for command queues which receive entries from the adapter.
	LIST_ENTRY		OutstandingIoQueue;	// A queue of outstanding fib's to the adapter.
	u32			NumOutstandingIos;	// Number of entries on outstanding queue.
	void *			Adapter;		// Back pointer to adapter structure
} COMM_QUE;
typedef COMM_QUE *PCOMM_QUE;


typedef struct _COMM_REGION {
	COMM_QUE HostNormCmdQue;	    // Command queue for normal priority commands from the host
	COMM_QUE HostNormRespQue;           // A response for normal priority adapter responses
    
	COMM_QUE HostHighCmdQue;            // Command queue for high priority commands from the host
	COMM_QUE HostHighRespQue;           // A response for normal priority adapter responses
    
	COMM_QUE AdapNormCmdQue;            // Command queue for normal priority command from the adapter
	COMM_QUE AdapNormRespQue;           // A response for normal priority host responses

	COMM_QUE AdapHighCmdQue;            // Command queue for high priority command from the adapter
	COMM_QUE AdapHighRespQue;           // A response for high priority host responses

	//
	// The 2 threads below are the threads which handle command traffic from the
	// the adapter. There is one for normal priority and one for high priority queues.
	// These threads will wait on the commandready event for it's queue.
	//

	HANDLE NormCommandThread;
	HANDLE HighCommandThread;

	//
	// This dpc routine will handle the setting the of not full event when the adapter
	// lets us know the queue is not longer full via interrupt
	//

	KDPC QueueNotFullDpc;

#ifdef API_THROTTLE
	//
	// Support for data I/O throttling to improve CLI performance
	// while the system is under load.
	// This is the throttling mechanism built into the COMM layer.
	// Look in commsup.c, dpcsup.c and comminit.c for use.
	//

	int		ThrottleLimit;				// Max queue depth of data ops allowed during throttle
	int		ThrottleOutstandingFibs;		// Number of data FIBs outstanding to adapter
	LARGE_INTEGER	ThrottleTimeout;			// Duration of a a throttle period
	LARGE_INTEGER	ThrottleWaitTimeout;			// Timeout for a suspended threads to wait
	int		ThrottleActive;				// Is there a current throttle active period ?
	KTIMER		ThrottleTimer;				// Throttle timer to end a throttle period.
	KDPC		ThrottleDpc;				// Throttle timer timeout DPC routine.
	KSEMAPHORE	ThrottleReleaseSema;			// Semaphore callers of SendFib wait on during a throttle.

	unsigned int	ThrottleExceptionsCount;		// Number of times throttle exception handler executed (0!)
	unsigned int	ThrottleTimerFires;			// Debug info - #times throttle timer Dpc has fired
	unsigned int	ThrottleTimerSets;			// Debug info - #times throttle timer was set

	unsigned int	ThrottledFibs;
	unsigned int	ThrottleTimedoutFibs;
	unsigned int	ApiFibs;
	unsigned int	NonPassiveFibs;
	unsigned int	TotalFibs;
	unsigned int	FSInfoFibs;
#endif // #ifdef API_THROTTLE
} COMM_REGION;
typedef COMM_REGION *PCOMM_REGION;

#endif // _COMM_SUP
