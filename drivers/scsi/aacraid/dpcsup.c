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
 *  dpcsup.c
 *
 * Abstract: All DPC processing routines for the cyclone board occur here.
 *
 *
 --*/
#include "comprocs.h"


//
//  The Bug check file id for this module
//

#define BugCheckFileId                   (FSAFS_BUG_CHECK_DPCSUP)
#define Dbg                              (DEBUG_TRACE_DPCSUP)

/*++

Routine Description:
    This DPC routine will be queued when the adapter interrupts us to let us know the queue is
    no longer full. The Isr will pass the queue that we will set the not full event.

Arguments:
    Dpc - Pointer to this routine.
    Dummy - is a pointer to the comm region which is global so we don't need it anyway
    Queue is a pointer to the queue structure we will operate on.
    MoreData2 are DPC parameters we don't need for this function. Maybe we can add some accounting
        stuff in here.

Return Value:
    Nothing.

--*/

unsigned int CommonNotFullDpc(PCOMM_REGION CommRegion)
{
#ifdef unix_queue_full
	KeSetEvent(&Queue->QueueFull, 0, FALSE);
#endif
}

int GatherFibTimes = 0;

// XXX - hack this in until I figure out which header file should contain it. <smb>
extern u32 FibGetMeterSize(PFIB pFib, u32 MeterType, char SubCommand);

/*++

Routine Description:
    This DPC routine will be queued when the adapter interrupts us to let us know there
    is a response on our normal priority queue. We will pull off all QE there are and wake
    up all the waiters before exiting. We will take a spinlock out on the queue before operating
    on it.

Arguments:
    Dpc - Pointer to this routine.
    queue is a pointer to the queue structure we will operate on.
    MoreData1&2 are DPC parameters we don't need for this function. Maybe we can add some accounting
        stuff in here.

Return Value:
    Nothing.

--*/

unsigned int HostResponseNormalDpc(PCOMM_QUE queue)
{
	PAFA_COMM_ADAPTER Adapter = queue->Adapter;
	PQUEUE_ENTRY entry;
	PFIB Fib;
	PCOMM_FIB_CONTEXT FibContext;
	int consumed = 0;
	KIRQL OldIrql;
	LARGE_INTEGER ResponseAllocSize;

#ifdef commdebug
	FsaCommPrint("entering the host normal reponse dpc routine.\n");
#endif

//      cmn_err( CE_CONT, "^HostNormalResponseDpc entered\n");

	OsSpinLockAcquire(queue->QueueLock);

	//
	// Keep pulling response QEs off the response queue and waking
	// up the waiters until there are no more QEs. We then return
	// back to the system. If no response was requesed we just
	// deallocate the Fib here and continue.
	//

loop:
	while (GetConsumerEntry(Adapter, queue, &entry)) 
	{
		int IsFastResponse;
		IsFastResponse = (int) (entry->FibAddress & 0x01);
		Fib = (PFIB) (entry->FibAddress & ~0x01);
		FreeConsumerEntry(Adapter, queue, HostNormRespQueue);
#ifdef API_THROTTLE
		//
		// Throttling support to improve CLI responsiveness under load.
		// Check if this FIB was participating in throttle I/O.
		// If so, take the spinlock to protect the throttle values
		// and count the completion. Then signal the next waiting
		// thread.
		//

		if ((Fib->Header.Flags & ThrottledFib) != 0) {
			PCOMM_REGION CommRegion = Adapter->CommRegion;
			FsaCommPrint("NormResponseDpc Throttled FIB Completion - FIB: %x, ThrottledFibs: %d\n",
				     Fib, CommRegion->ThrottleOutstandingFibs);

			//
			// Account for the completion.
			//
			ASSERT(CommRegion->ThrottleOutstandingFibs > 0);
			CommRegion->ThrottleOutstandingFibs--;	// One less FIB outstanding
			KeReleaseSemaphore(&CommRegion->ThrottleReleaseSema, 0, 1, FALSE); 			// Release a waiting thread
		}	// End of throttle code
#endif		// #ifdef API_THROTTLE

		FibContext = (PCOMM_FIB_CONTEXT) Fib->Header.SenderData;
		ASSERT(FibContext->Fib == Fib);

		//
		// Remove this FibContext from the Outstanding I/O queue.
		// But only if it has not already been timed out.
		//
		// If the fib has been timed out already, then just continue.
		// The caller has already been notified that the fib timed out.
		//

		if (!(FibContext->Flags & FIB_CONTEXT_FLAG_TIMED_OUT)) {
			RemoveEntryList(&FibContext->QueueEntry);
			Adapter->CommRegion->AdapNormCmdQue.NumOutstandingIos--;
		} else {
			FsaCommLogEvent(FibContext,
					FsaCommData.DeviceObject,
					FSAFS_TIMED_OUT_FIB_COMPLETED,
					STATUS_UNSUCCESSFUL, BugCheckFileId | __LINE__, FACILITY_FSAFS_ERROR_CODE, NULL, TRUE);
			continue;
		}
		OsSpinLockRelease(queue->QueueLock);

		if (IsFastResponse) {
			//
			// doctor the fib
			//
			*(FSASTATUS *) Fib->data = ST_OK;
			Fib->Header.XferState |= AdapterProcessed;
		}

#ifdef GATHER_FIB_TIMES
		if (GatherFibTimes) {
			LARGE_INTEGER FibTime, FibTimeDelta;
			LARGE_INTEGER AdapterFibTimeDelta;
			PFIB_TIMES FibTimesPtr;
			u32 Index;
			u32 LowAdapterFibTimeDelta;

			FibTimesPtr = FibContext->FibTimesPtr;
			FibTime.QuadPart = KeQueryPerformanceCounter(NULL).QuadPart >> 7;
			FibTimeDelta = RtlLargeIntegerSubtract(FibTime, FibContext->FibTimeStamp);

			if (RtlLargeIntegerLessThan(FibTimeDelta, FibTimesPtr->Minimum)) {
				FibTimesPtr->Minimum = FibTimeDelta;
			}
			if (RtlLargeIntegerGreaterThan(FibTimeDelta, FibTimesPtr->Maximum)) {
				FibTimesPtr->Maximum = FibTimeDelta;
			}

			FibTimesPtr->TotalTime = RtlLargeIntegerAdd(FibTimesPtr->TotalTime, FibTimeDelta);
			FibTimesPtr->TotalFibs = RtlLargeIntegerAdd(FibTimesPtr->TotalFibs, RtlConvertLongToLargeInteger(1));

			if (Fib->Header.ReceiverTimeDone > Fib->Header.ReceiverTimeStart) {
				AdapterFibTimeDelta = RtlConvertLongToLargeInteger(Fib->Header.ReceiverTimeDone - Fib->Header.ReceiverTimeStart);
			} else {
				AdapterFibTimeDelta = RtlConvertLongToLargeInteger(Fib->Header.ReceiverTimeDone +
					((u32) (0xffffffff) - Fib->Header.ReceiverTimeStart));
			}

			if (RtlLargeIntegerLessThan(AdapterFibTimeDelta, FibTimesPtr->AdapterMinimum)) {
				FibTimesPtr->AdapterMinimum = AdapterFibTimeDelta;
			}
			if (RtlLargeIntegerGreaterThan(AdapterFibTimeDelta, FibTimesPtr->AdapterMaximum)) {
				FibTimesPtr->AdapterMaximum = AdapterFibTimeDelta;
			}

			FibTimesPtr->AdapterTotalTime = RtlLargeIntegerAdd(FibTimesPtr->AdapterTotalTime, AdapterFibTimeDelta);
			if (GatherFibTimes == 1) {
				LowAdapterFibTimeDelta = AdapterFibTimeDelta.LowPart;
			} else {
				LowAdapterFibTimeDelta = FibTimeDelta.LowPart;
			}

			if ((LowAdapterFibTimeDelta & 0xffffff00) == 0) {
				Index = (LowAdapterFibTimeDelta & 0xf0) >> 4;
			} else if ((LowAdapterFibTimeDelta & 0xfffff000) == 0) {
				Index = 16 + ((LowAdapterFibTimeDelta & 0xf00) >> 8);
			} else if ((LowAdapterFibTimeDelta & 0xffff0000) == 0) {
				Index = 32 + ((LowAdapterFibTimeDelta & 0xf000) >> 12);
			} else if ((LowAdapterFibTimeDelta & 0xfff00000) == 0) {

				Index = 48 + ((LowAdapterFibTimeDelta & 0xf0000) >> 16);
			} else {
				Index = 64;
			}
			FibTimesPtr->AdapterBuckets[Index]++;
		}
#endif
		ASSERT((Fib->Header.XferState & (AdapterProcessed | HostOwned | SentFromHost)) ==
		       (AdapterProcessed | HostOwned | SentFromHost));

		FIB_COUNTER_INCREMENT(FsaCommData.FibRecved);
		ASSERT(FsaCommData.FibsSent >= FsaCommData.FibRecved);

		if (Fib->Header.Command == NuFileSystem) {
			FSASTATUS *pstatus = (FSASTATUS *) Fib->data;
			if (*pstatus & 0xffff0000) {
				u32 Hint = *pstatus;
				*pstatus = ST_OK;
			}
		}
		if (Fib->Header.XferState & (NoResponseExpected | Async)) {
			ASSERT(FibContext->FibCallback);
			if (Fib->Header.XferState & NoResponseExpected)
				FIB_COUNTER_INCREMENT(FsaCommData.NoResponseRecved);
			else
				FIB_COUNTER_INCREMENT(FsaCommData.AsyncRecved);
			//
			// NOTE:  we can not touch the FibContext after this call, because it may have been
			// deallocated.
			//
			FibContext->FibCallback(FibContext->FibCallbackContext, FibContext, STATUS_SUCCESS);
		} else {
			OsCvLockAcquire(FibContext->FsaEventMutex);
			FibContext->FibComplete = 1;
			OsCv_signal(&FibContext->FsaEvent);
			OsCvLockRelease(FibContext->FsaEventMutex);
			FIB_COUNTER_INCREMENT(FsaCommData.NormalRecved);
		}
		consumed++;
		OsSpinLockAcquire(queue->QueueLock);
	}
	if (consumed > FsaCommData.PeakFibsConsumed)
		FsaCommData.PeakFibsConsumed = consumed;
	if (consumed == 0)
		FsaCommData.ZeroFibsConsumed++;
	if (FsaCommData.HardInterruptModeration) {
		//
		// Re-Enable the interrupt from the adapter, then recheck to see if anything has 
		// been put on the queue.  This removes the race condition that exists between the
		// last time we checked the queue, and when we re-enabled the interrupt.
		//
		// If there is something on the queue, then go handle it.
		//
		EnableInterrupt(Adapter, HostNormRespQue, FALSE);
		if (ConsumerEntryAvailable(Adapter, queue)) {
			DisableInterrupt(Adapter, HostNormRespQue, FALSE);
			goto loop;
		}
	}
#ifdef commdebug
	FsaCommPrint("Exiting host normal reponse dpc routine after consuming %d QE(s).\n", consumed);
#endif
	OsSpinLockRelease(queue->QueueLock);
}

/*++

Routine Description:
    This DPC routine wiol be queued when the adapter interrupts us to let us know there
    is a response on our high priority queue. We will pull off all QE there are and wake
    up all the waiters before exiting. We will take a spinlock out on the queue before operating
    on it.

Arguments:
    Dpc - Pointer to this routine.
    queue is a pointer to the queue structure we will operate on.
    MoreData1&2 are DPC parameters we don't need for this function. Maybe we can add some accounting
        stuff in here.

Return Value:
    Nothing.

--*/

unsigned int HostResponseHighDpc(PCOMM_QUE queue)
{
}

/*++

Routine Description:
    This DPC routine will be queued when the adapter interrupts us to let us know there
    is a command on our high priority queue. We will pull off all QE there are and wake
    up all the waiters before exiting. We will take a spinlock out on the queue before operating
    on it.

Arguments:
    Dpc - Pointer to this routine.
    queue is a pointer to the queue structure we will operate on.
    MoreData1&2 are DPC parameters we don't need for this function. Maybe we can add some accounting
        stuff in here.

Return Value:
    Nothing.

--*/

unsigned int HostCommandHighDpc(PCOMM_QUE queue)
{
}


/*++

Routine Description:
    This DPC routine will be queued when the adapter interrupts us to let us know there
    is a command on our normal priority queue. We will pull off all QE there are and wake
    up all the waiters before exiting. We will take a spinlock out on the queue before operating
    on it.

Arguments:
    Dpc - Pointer to this routine.
    queue is a pointer to the queue structure we will operate on.
    MoreData1&2 are DPC parameters we don't need for this function. Maybe we can add some accounting
        stuff in here.

Return Value:
    Nothing.

--*/

unsigned int HostCommandNormDpc(PCOMM_QUE queue)
{
	PAFA_COMM_ADAPTER Adapter = queue->Adapter;
	PQUEUE_ENTRY entry;

	OsSpinLockAcquire(queue->QueueLock);

	//
	// Keep pulling response QEs off the response queue and waking
	// up the waiters until there are no more QEs. We then return
	// back to the system.
	//

	while (GetConsumerEntry(Adapter, queue, &entry)) {
		PFIB Fib;
		Fib = (PFIB) entry->FibAddress;

		if (Adapter->AifThreadStarted) {
//                      cmn_err(CE_CONT, "^Received AIF, putting onto command queue\n");
			InsertTailList(&queue->CommandQueue, &Fib->Header.FibLinks);
			FreeConsumerEntry(Adapter, queue, HostNormCmdQueue);
			OsCv_signal(&queue->CommandReady);
		} else {
			COMM_FIB_CONTEXT FibContext;
			FreeConsumerEntry(Adapter, queue, HostNormCmdQueue);
			OsSpinLockRelease(queue->QueueLock);
//                      cmn_err(CE_CONT, "^Received AIF, thread not started\n");
			RtlZeroMemory(&FibContext, sizeof(COMM_FIB_CONTEXT));
			FibContext.NodeTypeCode = FSAFS_NTC_FIB_CONTEXT;
			FibContext.NodeByteSize = sizeof(COMM_FIB_CONTEXT);
			FibContext.Fib = Fib;
			FibContext.FibData = Fib->data;
			FibContext.Adapter = Adapter;

			//
			// Set the status of this FIB
			//

			*(FSASTATUS *) Fib->data = ST_OK;
			CompleteAdapterFib(&FibContext, sizeof(FSASTATUS));

			OsSpinLockAcquire(queue->QueueLock);
		}
	}
	OsSpinLockRelease(queue->QueueLock);
}
