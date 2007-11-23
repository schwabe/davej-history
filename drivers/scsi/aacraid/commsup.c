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
 *  commsup.c
 *
 * Abstract: Contain all routines that are required for FSA host/adapter
 *    commuication.
 *
 *
 --*/
#include "comprocs.h"

#define BugCheckFileId                   (FSAFS_BUG_CHECK_COMMSUP)

int CommPrinting;

void ThrottleExceptionHandler(PCOMM_REGION comm, AAC_STATUS Status);
void ThrottlePeriodEndDpcRtn(PKDPC Dpc, void *DeferredContext, void *SystemArgument1, void *SystemArgument2);

/*++

Routine Description:
	This routine will free all resources used by a given FibContextSegment.

Arguments:
	Adapter - The adapter that this COMM_FIB_CONTEXT will communicate with.
	ZoneSegment - The segment to release resources from.

Return Value:
	TRUE - All resources were properly freed.
	FALSE - An Error occured while freeing resources.

--*/

int FsaFreeFibContextSegment(PAFA_COMM_ADAPTER Adapter, PFIB_CONTEXT_ZONE_SEGMENT	ZoneSegment)
{
	PCOMM_FIB_CONTEXT fc;
	int i;
	
	// Account for the ZONE_SEGMENT_HEADER before the first actual FibContext.
	for (i = 0, fc = (PCOMM_FIB_CONTEXT)((unsigned char *)ZoneSegment->FibContextSegment + sizeof(ZONE_SEGMENT_HEADER));
		 i < ZoneSegment->ExtendSize; i++, fc++) {
		OsCvLockDestroy( fc->FsaEventMutex );
		OsCv_destroy( &fc->FsaEvent );
	}
	UnmapAndFreeFibSpace(Adapter, &ZoneSegment->MapFibContext);
	kfree(ZoneSegment->FibContextSegment);
	kfree(ZoneSegment);
	return TRUE;
}

/*++

Routine Description:

	This routine will walk through the FibContextSegmentList and free up all
	resources used by the FibContextZone.

Arguments:

	Adapter - The adapter that this COMM_FIB_CONTEXT will communicate with.

Return Value:

	TRUE - All resources were properly freed.
	FALSE - An Error occured while freeing resources.

--*/

int FsaFreeFibContextZone(PAFA_COMM_ADAPTER Adapter)
{
	PFIB_CONTEXT_ZONE_SEGMENT zone, next;
	zone = Adapter->FibContextSegmentList;

	while (zone) {
		next = zone->Next;
		FsaFreeFibContextSegment( Adapter, zone );
		zone = next;
	}
	return (TRUE);
}

	

int FsaExtendFibContextZone(PAFA_COMM_ADAPTER Adapter)
{
	int ExtendSize;
	KIRQL SavedIrql;
	u32 segallocsize, fiballocsize;
	void * FibContextSegment;
	PCOMM_FIB_CONTEXT fc;
	PFIB Fib;
	void * fib_pa;
	int i;
	PFIB_CONTEXT_ZONE_SEGMENT seg;
	
	//
	// Allocate space to describe this zone segment.
	//

	seg = kmalloc(sizeof( FIB_CONTEXT_ZONE_SEGMENT ), GFP_KERNEL);
	ExtendSize = Adapter->FibContextZoneExtendSize;
	segallocsize = (ExtendSize * sizeof(COMM_FIB_CONTEXT)) + sizeof(ZONE_SEGMENT_HEADER);

	FibContextSegment = kmalloc(segallocsize, GFP_KERNEL);
	if (FibContextSegment == NULL) {
		return (FALSE);
	}	

	RtlZeroMemory( FibContextSegment, segallocsize );
	seg->FibContextSegment = FibContextSegment;
	seg->FibContextSegmentSize = segallocsize;
	seg->ExtendSize = ExtendSize;
	fiballocsize = ExtendSize * sizeof(FIB);
	seg->MapFibContext.Size = fiballocsize;
	AllocateAndMapFibSpace( Adapter, &seg->MapFibContext );
	Fib = seg->MapFibContext.FibVirtualAddress;
	fib_pa = seg->MapFibContext.FibPhysicalAddress;
	RtlZeroMemory( Fib, fiballocsize );
	// Account for the ZONE_SEGMENT_HEADER before the first actual FibContext.

	for (i = 0, fc = (PCOMM_FIB_CONTEXT)((u8 *)FibContextSegment + sizeof(ZONE_SEGMENT_HEADER));
		 i < ExtendSize; i++, fc++) {
		fc->Adapter = Adapter;
		fc->Fib = Fib;
		fc->FibData = (void *) fc->Fib->data;
		OsCv_init( &fc->FsaEvent);
		fc->FsaEventMutex = OsCvLockAlloc();
		OsCvLockInit( fc->FsaEventMutex, Adapter->SpinLockCookie );
		Fib->Header.XferState = 0xffffffff;
		Fib->Header.SenderSize = sizeof(FIB);
		fc->LogicalFibAddress.LowPart = (u32) fib_pa;
		Fib = (PFIB)((u8 *)Fib + sizeof(FIB));
		fib_pa = (void *)((u8 *)fib_pa + sizeof(FIB));
	}

	//
	// If FibContextZone.TotalSegmentSize is non-zero, then a zone has already been
	// initialized, we just need to extend it.
	//

	if (Adapter->FibContextZone.TotalSegmentSize) {
		OsSpinLockAcquire( Adapter->FibContextZoneSpinLock );
		ExExtendZone( &Adapter->FibContextZone,
					  FibContextSegment,
					  segallocsize );
		OsSpinLockRelease( Adapter->FibContextZoneSpinLock );
	} else {
		if (ExInitializeZone( &Adapter->FibContextZone,
				sizeof(COMM_FIB_CONTEXT),
    	                	FibContextSegment,
	                	segallocsize ) != STATUS_SUCCESS)
			FsaBugCheck(0,0,0);
	}

	//
	// Add this segment to the adapter's list of segments
	//
	seg->Next = Adapter->FibContextSegmentList;
	Adapter->FibContextSegmentList = seg;
	return (TRUE);
}




/*++

Routine Description:
	This routine creates a new COMM_FIB_CONTEXT record

Arguments:
	Adapter - The adapter that this COMM_FIB_CONTEXT will communicate with.

Return Value:
	PCOMM_FIB_CONTEXT - returns a pointer to the newly allocate COMM_FIB_CONTEXT Record

--*/

PFIB_CONTEXT AllocateFib (void * AdapterArg)
{
	PAFA_COMM_ADAPTER Adapter = (PAFA_COMM_ADAPTER) AdapterArg;
	KIRQL SavedIrql;
	PCOMM_FIB_CONTEXT fc;
	int FullZoneLoopCounter = 0;

	//
	// Acquire the zone spin lock, and check to see if the zone is full.
	// If it is, then release the spin lock and allocate more fibs for the 
	// zone.  The ExtendFibZone routine will re-acquire the spin lock to add
	// the new fibs onto the zone.
	//

	OsSpinLockAcquire( Adapter->FibContextZoneSpinLock );

	while (ExIsFullZone( &Adapter->FibContextZone )) 
	{
		if (++FullZoneLoopCounter >  10)
			FsaBugCheck(0,0,0);
		OsSpinLockRelease( Adapter->FibContextZoneSpinLock );
		if (FsaExtendFibContextZone(Adapter) == FALSE) {  
			return (NULL);
		}
		OsSpinLockAcquire( Adapter->FibContextZoneSpinLock );
	}

	//
	//  At this point we now know that the zone has at least one more
	//  IRP context record available.  So allocate from the zone and
	//  then release the mutex.
	//

	fc = (PCOMM_FIB_CONTEXT) ExAllocateFromZone( &Adapter->FibContextZone );
	OsSpinLockRelease( Adapter->FibContextZoneSpinLock );

	//
	//  Set the proper node type code and node byte size
	//

	fc->NodeTypeCode = FSAFS_NTC_FIB_CONTEXT;
	fc->NodeByteSize = sizeof( COMM_FIB_CONTEXT );

	// 
	// Null out fields that depend on being zero at the start of each I/O
	//

	fc->Fib->Header.XferState = 0;
	fc->FibCallback = NULL;
	fc->FibCallbackContext = NULL;

	//
	//  return and tell the caller
	//

	return ((PFIB_CONTEXT) fc);
}


/*++

Routine Description:

    This routine deallocates and removes the specified COMM_FIB_CONTEXT record
    from the Fsafs in memory data structures.  It should only be called
    by FsaCompleteRequest.

Arguments:

   	fc - Supplies the COMM_FIB_CONTEXT to remove

Return Value:

    None

--*/

void FreeFib (PFIB_CONTEXT Context)
{
	KIRQL SavedIrql;
	PCOMM_FIB_CONTEXT fc = Context;

	ASSERT(fc->NodeTypeCode == FSAFS_NTC_FIB_CONTEXT);

	OsSpinLockAcquire( fc->Adapter->FibContextZoneSpinLock );

	if (fc->Flags & FIB_CONTEXT_FLAG_TIMED_OUT) {
		FsaCommData.TimedOutFibs++;
		fc->Next = fc->Adapter->FibContextTimedOutList;
		fc->Adapter->FibContextTimedOutList = fc;
	} else {
		ASSERT(fc->Fib->Header.XferState == 0);
		if (fc->Fib->Header.XferState != 0) {
			cmn_err(CE_WARN, "FreeFib, XferState != 0, fc = 0x%x, XferState = 0x%x\n", 
				 fc, fc->Fib->Header.XferState);
		}
		ExFreeToZone( &fc->Adapter->FibContextZone, fc );
	}	
	OsSpinLockRelease( fc->Adapter->FibContextZoneSpinLock );
	//
	//  return and tell the caller
	//
	return;
}


/*++

Routine Description:

    This routine deallocates and removes the specified COMM_FIB_CONTEXT record
    from the Fsafs in memory data structures.  It should only be called
    from the dpc routines to from dpc to free an FibContext from an async or
	no response io

Arguments:

    fc - Supplies the COMM_FIB_CONTEXT to remove

Return Value:

    None

--*/

void FreeFibFromDpc(PFIB_CONTEXT Context)
{
	PCOMM_FIB_CONTEXT fc = (PCOMM_FIB_CONTEXT) Context;
	ASSERT(fc->NodeTypeCode == FSAFS_NTC_FIB_CONTEXT);

	OsSpinLockAcquire(fc->Adapter->FibContextZoneSpinLock);
	if (fc->Flags & FIB_CONTEXT_FLAG_TIMED_OUT) {
		FsaCommData.TimedOutFibs++;
		fc->Next = fc->Adapter->FibContextTimedOutList;
		fc->Adapter->FibContextTimedOutList = fc;
	} else {
		ASSERT(fc->Fib->Header.XferState == 0);
		if (fc->Fib->Header.XferState != 0) {
			cmn_err(CE_WARN, "FreeFibFromDpc, XferState != 0, fc = 0x%x, XferState = 0x%x\n", 
				 fc, fc->Fib->Header.XferState);
		}
		ExFreeToZone( &fc->Adapter->FibContextZone, fc );
	}
	OsSpinLockRelease(fc->Adapter->FibContextZoneSpinLock);
	//
	//  return and tell the caller
	//
	return;
}



/*++

Routine Description:
    Will initialize a FIB of the requested size.
    
Arguments:
    Fib is a pointer to a location which will receive the address of the allocated
        FIB.
    Size is the size of the Fib to allocate.

Return Value:
    NT_SUCCESS if a Fib was returned to the caller.
    NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS InitializeFib(PFIB_CONTEXT Context)
{
	PCOMM_FIB_CONTEXT fc = (PCOMM_FIB_CONTEXT) Context;
	PFIB Fib = fc->Fib;

	Fib->Header.StructType = TFib;
	Fib->Header.Size = sizeof(FIB);
        Fib->Header.XferState = HostOwned | FibInitialized | FibEmpty | FastResponseCapable;
	Fib->Header.SenderFibAddress = 0;
	Fib->Header.ReceiverFibAddress = 0;
	Fib->Header.SenderSize = sizeof(FIB);
	return(STATUS_SUCCESS);
}
    
/*++

Routine Description:
    Will allocate and initialize a FIB of the requested size and return a
    pointer to the structure. The size allocated may be larger than the size
    requested due to allocation performace optimizations.
    
Arguments:
    Fib is a pointer to a location which will receive the address of the allocated
        FIB.
    Size is the size of the Fib to allocate.
    JustInitialize is a boolean which indicates a Fib has been allocated most likly in an
        imbedded structure the FS always allocates. So just initiaize it and return.
    
Return Value:
    NT_SUCCESS if a Fib was returned to the caller.
    NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS AllocatePoolFib(PFIB *Fib, u16 Size)
{
}
    
/*++

Routine Description:

    Will deallocate and return to the free pool the FIB pointed to by the
    caller. Upon return accessing locations pointed to by the FIB parameter
    could cause system access faults.

Arguments:

    Fib is a pointer to the FIB that caller wishes to deallocate.
    
Return Value:

    NT_SUCCESS if a Fib was returned to the caller.
    NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS DeallocateFib(PFIB_CONTEXT Context)
{
	PCOMM_FIB_CONTEXT fc = (PCOMM_FIB_CONTEXT) Context;
	PFIB Fib = fc->Fib;

	if ( Fib->Header.StructType != TFib ) {
        	FsaCommPrint("Error CompleteFib called with a non Fib structure.\n");
        	return(STATUS_UNSUCCESSFUL);
	}

	Fib->Header.XferState = 0;        
	return(STATUS_SUCCESS);
}


/*++

Routine Description:
    Gets a QE off the requested response queue and gets the response FIB into
    host memory. The FIB may already be in host memory depending on the bus
    interface, or may require the host to DMA it over from the adapter. The routine
    will return the FIB to the caller.

Arguments:
    ResponseQueue - Is the queue the caller wishes to have the response gotten from.
    Fib - Is the Fib which was the response from the adapter

Return Value:
    NT_SUCCESS if a Fib was returned to the caller.
    NT_ERROR if there was no Fib to return to the caller.
    bkpfix - add in all the other possible errors ect

--*/

AAC_STATUS GetResponse(PCOMM_QUE ResponseQueue, PFIB Fib)
{
	return(STATUS_UNSUCCESSFUL);
}

//
// Commuication primitives define and support the queuing method we use to
// support host to adapter commuication. All queue accesses happen through
// these routines and are the only routines which have a knowledge of the
// how these queues are implemented.
//

/*++

Routine Description:

    With a priority the routine returns a queue entry if the queue has free entries. If the queue
    is full(no free entries) than no entry is returned and the function returns FALSE otherwise TRUE is
    returned.

Arguments:

    Priority is an enumerated type which determines which priority level
        command queue the QE is going to be queued on.

    entry is a pointer to the address of where to return the address of
        the queue entry from the requested command queue.

    Index is a pointer to the address of where to store the index of the new
        queue entry returned.

	DontInterrupt - We set this true if the queue state is such that we don't
		need to interrupt the adapter for this queue entry.

Return Value:

    TRUE - If a queue entry is returned
    FALSE - If there are no free queue entries on the requested command queue.

--*/

int GetEntry(PAFA_COMM_ADAPTER Adapter, QUEUE_TYPES WhichQueue, PQUEUE_ENTRY *entry, PQUEUE_INDEX Index, u32 *DontInterrupt)
{
	u32 QueueOffset;
	int status;
	PCOMM_REGION comm;

	comm = Adapter->CommRegion;

	//
	// All of the queues wrap when they reach the end, so we check to see if they
	// have reached the end and if they have we just set the index back to zero.
	// This is a wrap. You could or off the high bits in all updates but this is
	// a bit faster I think.
	//

	if (WhichQueue == AdapHighCmdQueue) 
	{
	        *Index = *(comm->AdapHighCmdQue.Headers.ProducerIndex);
		if (*Index - 2 == *(comm->AdapHighCmdQue.Headers.ConsumerIndex))
			*DontInterrupt = TRUE; 
	        if (*Index >= ADAP_HIGH_CMD_ENTRIES)
        		*Index = 0;
		if (*Index + 1 == *(comm->AdapHighCmdQue.Headers.ConsumerIndex)) 
		{ // Queue is full
			status = FALSE;
			cmn_err(CE_WARN, "Adapter High Command Queue full, %d outstanding",
					comm->AdapHighCmdQue.NumOutstandingIos);
		} else {
	        	QueueOffset = sizeof(QUEUE_ENTRY) * (*Index);
	        	*entry = QueueOffset + comm->AdapHighCmdQue.BaseAddress;
			status = TRUE;
		}
	}
	else if (WhichQueue == AdapNormCmdQueue) 
	{
	        *Index = *(comm->AdapNormCmdQue.Headers.ProducerIndex);
		if (*Index - 2 == *(comm->AdapNormCmdQue.Headers.ConsumerIndex))
			*DontInterrupt = TRUE; 
		//
		// If we are at the end of the QUEUE then wrap back to 
		// the beginning.
	        //
	        if (*Index >= ADAP_NORM_CMD_ENTRIES) 
        		*Index = 0; // Wrap to front of the Producer Queue.

		//
	        // The IEEE spec says that it the producer is one behind the consumer then
	        // the queue is full.
	        //       

		ASSERT(*(comm->AdapNormCmdQue.Headers.ConsumerIndex) != 0);
	        if (*Index + 1 == *(comm->AdapNormCmdQue.Headers.ConsumerIndex)) 
	        {
	        	// Queue is full
			cmn_err(CE_WARN, "Adapter Norm Command Queue full, %d outstanding",
				comm->AdapNormCmdQue.NumOutstandingIos);
			status = FALSE;
		} else {        
		       	//
			// The success case just falls through and returns the a valid queue entry.
			//
#ifdef commdebug
		        FsaCommPrint("queue entry = %x.\n",comm->AdapNormCmdQue.BaseAddress + *Index);
		        FsaCommPrint("GetEntry: Index = %d, QueueOffset = %x, entry = %x, *entry = %x.\n",
				*Index, QueueOffset, entry, *entry);
#endif
			*entry = comm->AdapNormCmdQue.BaseAddress + *Index;
			status = TRUE;
		}
	}
	else if (WhichQueue == AdapHighRespQueue) 
	{
	        *Index = *(comm->AdapHighRespQue.Headers.ProducerIndex);
		if (*Index - 2 == *(comm->AdapHighRespQue.Headers.ConsumerIndex))
			*DontInterrupt = TRUE; 
	        if (*Index >= ADAP_HIGH_RESP_ENTRIES)
        		*Index = 0;
	        if (*Index + 1 == *(comm->AdapHighRespQue.Headers.ConsumerIndex)) { // Queue is full
			status = FALSE;
			cmn_err(CE_WARN, "Adapter High Resp Queue full, %d outstanding",
					comm->AdapHighRespQue.NumOutstandingIos);
		}
		else
		{							
	        	*entry = comm->AdapHighRespQue.BaseAddress + *Index;
    			status = TRUE;
		} 
	}
	else if (WhichQueue == AdapNormRespQueue) 
	{
	        *Index = *(comm->AdapNormRespQue.Headers.ProducerIndex);
		if (*Index - 2 == *(comm->AdapNormRespQue.Headers.ConsumerIndex))
			*DontInterrupt = TRUE; 
		//
		// If we are at the end of the QUEUE then wrap back to 
		// the beginning.
	        //

	        if (*Index >= ADAP_NORM_RESP_ENTRIES) 
			*Index = 0; // Wrap to front of the Producer Queue.
		//
		// The IEEE spec says that it the producer is one behind the consumer then
		// the queue is full.
		//       

	        if (*Index + 1 == *(comm->AdapNormRespQue.Headers.ConsumerIndex)) { // Queue is full
			status = FALSE; 
			cmn_err(CE_WARN, "Adapter Norm Resp Queue full, %d outstanding",
					comm->AdapNormRespQue.NumOutstandingIos);
		}
		else
		{        
	       		//
			// The success case just falls through and returns the a valid queue entry.
			//
	        	*entry = comm->AdapNormRespQue.BaseAddress + *Index;
#ifdef commdebug
		        FsaCommPrint("queue entry = %x.\n",comm->AdapNormRespQue.BaseAddress + *Index);
		        FsaCommPrint("GetEntry: Index = %d, entry = %x, *entry = %x.\n",*Index, entry, *entry);
#endif
			status = TRUE;
		}     
	}
	else
	{
		cmn_err(CE_PANIC, "GetEntry: invalid queue %d", WhichQueue);
	}
	return (status);
}
   
#ifdef API_THROTTLE

/*++

Routine Description:
	This routine implements data I/O throttling. Throttling occurs when
	a CLI FIB is detected. To ensure the CLI responds quickly (the user
	is waiting for the response), this mechanism restricts the queue
	depth of data IOs at the adapter for a period of time (called the
	Throttle Period, default 5 seconds).

	The mechanism uses a counted semaphore to place threads into a wait
	state should there be too many data I/Os outstanding.

	At the start of a throttle period (indicated by the first CLI FIB)
	a timer is started. When the timer expires, new requests can go to
	the adapter freely. Throttled requests gradually drain to the
	adapter as each outstanding throttle I/O completes.

	To avoid hurting regular I/O performance, we use a flag in the FIB
	header to mark FIBs involved in throttling. This means we only need
	take the extra spinlock in the response DPC routine for FIBs who
	were subject to throttling. If no throttling is occurring, the cost
	to the regular code paths is a handful of instructions.

Arguments:
	Adapter - Pointer to per-adapter context. This is used to locate the
			  throttle information for this adapter.
	Fib		- Pointer to the header for the fib being sent.

Return Value:
	None.

--*/

void ThrottleCheck(PAFA_COMM_ADAPTER Adapter, PFIB Fib)
{
	PCOMM_REGION comm = Adapter->CommRegion;
	AAC_STATUS	 status;

	//
	// This routine is called under protection of the queue spinlock.
	// As such we are allowed to check and change the counts for the
	// throttle.
	// Check the FIB. If its not a data operation, send it on without
	// throttle check. If it is a data operation, check for throttle.
	//
	comm->TotalFibs++;							// Keep statistics

	if ((Fib->Header.XferState & ApiFib) != 0) {
		comm->ApiFibs++;							// Keep statistics
		//
		// Its an API fib. If the throttle is not already active,
		// make it so. This will prevent new data Fibs being sent
		// if they exceed the throttle check.
		//
		if (!comm->ThrottleActive) {
			int		 InQue;

			// This causes new data I/Os to be throttled
			comm->ThrottleActive = TRUE;			
			//
			// Schedule a timer for the throttle active period. When
			// it expires, we'll be called back at routine ThrottleDpcRoutine
			// above. This will signify the throttle active period ended
			// and any waiting threads will be signalled to restart.
			//

			FsaCommPrint("Throttle Period Start - comm: %x\n", comm);
			comm->ThrottleTimerSets++;
			InQue = KeSetTimer( &comm->ThrottleTimer,
					comm->ThrottleTimeout,
					&comm->ThrottleDpc);
			ASSERT(InQue == FALSE);
		}
		return;
	}

	//
	// Its a non-API fib, so subject to throttle checks.
	// The following are exempt from throttling:
	//		o FIBs marked as "throttle exempt" by upper layers.
	//		o I/Os issued from a raised IRQL. We can't suspend
	//		  a thread when at raised IRQL so throttling is exempt.
	//

	if (comm->AdapNormCmdQue.SavedIrql != PASSIVE_LEVEL) {
		comm->NonPassiveFibs++;
		FsaCommPrint("ThrottleCheck: Non-Passive level FIB bypasses throttle: %x\n", Fib);
		return;
	}

	if (comm->ThrottleActive) {
		//
		// Throttle is active.
		// Check if the FIB is a read or write. If so, and its to the
		// file system information area, let it through without throttling.
		//
		if (Fib->Header.Command == ContainerCommand) {
			PBLOCKREAD BlockDisk = (PBLOCKREAD) &Fib->data;
			//
			// *** Note *** We are using read and write command formats
			// interchangably here. This is ok for this purpose as the
			// command is in the same place for both. Read and write command
			// formats are different at higher offsets though.
			//
			if ( ((BlockDisk->Command == VM_CtBlockRead) ||
				  (BlockDisk->Command == VM_CtBlockWrite)) &&
				  (BlockDisk->BlockNumber <= FILESYSTEM_INFO_MAX_BLKNO)) {
				comm->FSInfoFibs++;							// Keep statistics
				return;
			}
		}
		//
		// Throttle the FIB.
		// Mark it as throttle active so that it can signal a waiter
		// when it completes.

		comm->ThrottledFibs++;
		Fib->Header.Flags |= ThrottledFib;
		
		//
		// Release the spinlock so we can wait the thread if necessary.
		// Since we specify a timeout, check the caller is at passive level.
		//

		OsSpinLockRelease((comm->AdapNormCmdQue.QueueLock), comm->AdapNormCmdQue.SavedIrql);
		FsaCommPrint("ThrottleCheck - Thread Suspension - FIB: %x\n", Fib);
		status = KeWaitForSingleObject(&comm->ThrottleReleaseSema,
					Executive,							// Don't allow user APCs to wake us
					KernelMode,							// Wait in kernel mode
					FALSE,								// Not alertable
					&comm->ThrottleWaitTimeout);	// Timeout after this time
		//
		// Check the signal status. If we've timed out, clear the throttle
		// flag on the FIB to avoid us signalling the semaphore on completion.
		// We never acquired the semaphore.
		//
		if (status == STATUS_TIMEOUT) {
			comm->ThrottleTimedoutFibs++;
			FsaCommPrint("ThrottledFib Timed Out - FIB: %x\n", Fib);
			Fib->Header.Flags &= ~ThrottledFib;						// Clear the throttledfib flag
		} else {
			ASSERT(status == STATUS_SUCCESS);						// No other return is possible
		}

		//
		// We've been woken up and can now send the FIB to the adapter.
		// Acquire the spinlock again so we can get a queue entry. This
		// returns to GetQueueEntry.
		//

		FsaCommPrint("ThrottleCheck - Thread Resume - FIB: %x\n", Fib);
		KeAcquireSpinLock((comm->AdapNormCmdQue.QueueLock), &(comm->AdapNormCmdQue.SavedIrql));
		comm->ThrottleOutstandingFibs++;		// There's another throttle controlled FIB going.
		return;
	}
}

#endif //#ifdef API_THROTTLE

int GetQueueEntryTimeouts = 0;
/*++

Routine Description:
	Gets the next free QE off the requested priorty adapter command queue and
	associates the Fib with the QE. The QE represented by index is ready to
	insert on the queue when this routine returns success.

Arguments:
	Index is the returned value which represents the QE which is ready to
        insert on the adapter's command queue.

	Priority is an enumerated type which determines which priority level
        command queue the QE is going to be queued on.

	Fib is a pointer to the FIB the caller wishes to have associated with the QE.

	Wait is a boolean which determines if the routine will wait if there are
        no free QEs on the requested priority command queue.

	FibContext is where the driver stores all system resources required to execute the
        command requested from the calling thread. This includes mapping resources for
        the FIB and the 'users' buffer.

	DontInterrupt - We set this true if the queue state is such that we don't
	need to interrupt the adapter for this queue entry.

Return Value:
	NT_SUCCESS if a Fib was returned to the caller.
	NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS GetQueueEntry(PAFA_COMM_ADAPTER Adapter, PQUEUE_INDEX Index, 
	QUEUE_TYPES WhichQueue, PFIB Fib, int wait, PCOMM_FIB_CONTEXT fc, 
	u32 *DontInterrupt)
{
	PQUEUE_ENTRY QueueEntry = NULL;
	int MapAddress = FALSE;
	int timeouts = 0;
	AAC_STATUS status;
	PCOMM_REGION comm;

	comm = Adapter->CommRegion;

	//
	// Get the spinlock for the queue we are putting a command on
	//

	if (WhichQueue == AdapHighCmdQueue) 
        	OsSpinLockAcquire(comm->AdapHighCmdQue.QueueLock);
	else if (WhichQueue == AdapNormCmdQueue)
        	OsSpinLockAcquire(comm->AdapNormCmdQue.QueueLock);
	else if (WhichQueue == AdapHighRespQueue)
        	OsSpinLockAcquire(comm->AdapHighRespQue.QueueLock);
	else if (WhichQueue == AdapNormRespQueue)
		OsSpinLockAcquire(comm->AdapNormRespQue.QueueLock);
	else {
        	FsaCommPrint("Invalid queue priority passed to GetQueueEntry.\n");
        	return(FSA_INVALID_QUEUE);
	}
    
	//
	// Get the pointers to a queue entry on the queue the caller wishes to queue
	// a command request on. If there are no entries then wait if that is what the
	// caller requested. 
	//

	if (WhichQueue == AdapHighCmdQueue) 
	{
		while ( !GetEntry(Adapter, AdapHighCmdQueue, &QueueEntry, Index, DontInterrupt) ) 
		{
			// if no entries wait for some if caller wants to
			cmn_err(CE_PANIC, "GetEntries failed (1)\n");
		}
		//
	        // Setup queue entry with a command, status and Fib mapped
	        //
	        QueueEntry->Size = Fib->Header.Size;
	        MapAddress = TRUE;
	}
	else if (WhichQueue == AdapNormCmdQueue) 
	{
#ifdef API_THROTTLE
		//
		// Check if this is a data I/O that may be throttled. Throttling
		// occurs if FAST is trying to issue FIBs to the adapter for management
		// commands. If a FAST FIB is detected by ThrottleCheck it is allowed
		// to go to the adapter (barring no queue entries being available). If
		// the FAST fib is the first FAST fib to be detected, a data I/O throttle
		// period is started. New incoming data I/Os are restricted so that only
		// a few can be outstanding to the adapter at once. This prevents the
		// FAST FIBs being starved out from the disks. Once the period has ended,
		// normal service is resumed.
		// New data FIBs (i.e. non-FAST FIB) can be initiated so long as they
		// don't exceed the throttle maximum of outstanding FIBs. If the new FIB
		// *would* exceed the maximum, the thread is put to sleep, waiting on the
		// data I/O throttle synchronization semaphore. When the first data FIB completes
		// that takes the outstanding count to below the threshold, the throttle
		// synchronization semaphore is signalled once. It is signaled for each completing
		// FIB until the throttle period ends and all throttled FIBs have completed.
		//
		ThrottleCheck(Adapter, Fib);					// Thread may be suspended here with spinlock released !
#endif	// #ifdef API_THROTTLE
#ifdef commdebug
		FsaCommPrint("Requesting a qe address in address %x.\n", &QueueEntry);
#endif
		while ( !GetEntry(Adapter, AdapNormCmdQueue, &QueueEntry, Index, DontInterrupt) ) 
		{
		 	// if no entries wait for some if caller wants to
			cmn_err(CE_PANIC, "GetEntries failed (2)\n");
		}
 
		//
		// Setup queue entry with command, status and Fib mapped
		//

#ifdef commdebug
		FsaCommPrint("We got a QE from the normal command queue address of %x, Index returned = %d\n", *QueueEntry, *Index);
#endif
		QueueEntry->Size = Fib->Header.Size;
		MapAddress = TRUE;
	}
	else if (WhichQueue == AdapHighRespQueue) 
	{
	        while ( !GetEntry(Adapter, AdapHighRespQueue, &QueueEntry, Index, DontInterrupt) ) 
		{
			// if no entries wait for some if caller wants to
		}
		//
		// Setup queue entry with command, status and Fib mapped
        	//
		QueueEntry->Size = Fib->Header.Size;
        	QueueEntry->FibAddress = Fib->Header.SenderFibAddress;     			// Restore adapters pointer to the FIB
		Fib->Header.ReceiverFibAddress = Fib->Header.SenderFibAddress;		// Let the adapter now where to find its data
        	MapAddress = FALSE;
	} else if (WhichQueue == AdapNormRespQueue) 
	{
        	while ( !GetEntry(Adapter, AdapNormRespQueue, &QueueEntry, Index, DontInterrupt) ) 
        	{
        		// if no entries wait for some if caller wants to
		}
		//
		// Setup queue entry with command, status, adapter's pointer to the Fib it sent
		//
	        QueueEntry->Size = Fib->Header.Size;
	        QueueEntry->FibAddress = Fib->Header.SenderFibAddress;     			// Restore adapters pointer to the FIB
		Fib->Header.ReceiverFibAddress = Fib->Header.SenderFibAddress;		// Let the adapter now where to find its data
        	MapAddress = FALSE;
	}
                
	//
	// If MapFib is true than we need to map the Fib and put pointers in the queue entry.
	//

	if (MapAddress) 
	{
		QueueEntry->FibAddress = (u32)(fc->LogicalFibAddress.LowPart);
	}
    
	//
	// Return
	//
#ifdef commdebug    
	FsaCommPrint("Queue Entry contents:.\n");
	FsaCommPrint("  Command =               %d.\n", QueueEntry->Command);
	FsaCommPrint("  status  =               %x.\n", QueueEntry->Status);
	FsaCommPrint("  Rec Fib address low =   %x.\n", QueueEntry->FibAddressLow);        
	FsaCommPrint("  Fib size in bytes =     %d.\n", QueueEntry->Size);
#endif
	return(FSA_SUCCESS);
}

/*++

Routine Description:

    Gets the next free QE off the requested priorty adapter command queue and
      associates the Fib with the QE. The QE represented by index is ready to
    insert on the queue when this routine returns success.

Arguments:

    Index is the returned value which represents the QE which is ready to
        insert on the adapter's command queue.

    WhichQueue tells us which queue the caller wishes to have the entry put.
        
Return Value:

    NT_SUCCESS if a Fib was returned to the caller.
    NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS InsertQueueEntry(PAFA_COMM_ADAPTER Adapter, QUEUE_INDEX Index, 
				QUEUE_TYPES WhichQueue, u32 DontInterrupt)
{
	PCOMM_REGION comm;
	comm = Adapter->CommRegion;

	//
	// We have already verified the queue in getentry, but we still have to make
	// sure we don't wrap here too.
	//

	if (WhichQueue == AdapHighCmdQueue) 
	{
	        *(comm->AdapHighCmdQue.Headers.ProducerIndex) = Index + 1;
	        OsSpinLockRelease(comm->AdapHighCmdQue.QueueLock);
		if (!DontInterrupt)
		        NotifyAdapter(Adapter, AdapHighCmdQue);
        }
        else if (WhichQueue == AdapNormCmdQueue) 
        {
#ifdef commdebug
		FsaCommPrint("InsertQueueEntry: Inerting with an index of %d.\n",Index);
#endif
		*(comm->AdapNormCmdQue.Headers.ProducerIndex) = Index + 1;
		OsSpinLockRelease(comm->AdapNormCmdQue.QueueLock);
		if (!DontInterrupt)
		        NotifyAdapter(Adapter, AdapNormCmdQue);
	}
	else if (WhichQueue == AdapHighRespQueue) 
	{
	        *(comm->AdapHighRespQue.Headers.ProducerIndex) = Index + 1;
		OsSpinLockRelease(comm->AdapHighRespQue.QueueLock);
		if (!DontInterrupt)
		        NotifyAdapter(Adapter, AdapHighRespQue);
	}
	else if (WhichQueue == AdapNormRespQueue) 
	{
	        *(comm->AdapNormRespQue.Headers.ProducerIndex) = Index + 1;
	        OsSpinLockRelease(comm->AdapNormRespQue.QueueLock);
		if (!DontInterrupt)
		        NotifyAdapter(Adapter, AdapNormRespQue);
	}
	else
	{        
        	FsaCommPrint("Invalid queue priority passed to InsertQueueEntry.\n");
        	return(FSA_INVALID_QUEUE_PRIORITY);
	}
	return(FSA_SUCCESS);                
}

extern int GatherFibTimes;

/*++

Routine Description:
	This routine will send a synchronous FIB to the adapter and wait for its
	completion.

Arguments:
	DeviceExtension - Pointer to adapter extension structure.

Return Value:
	int

--*/

int SendSynchFib(void *Arg, FIB_COMMAND Command, void *Data, u16 Size, 
		void *Response, u16 *ResponseSize)
{
	PAFA_COMM_ADAPTER Adapter = Arg;
	FIB *Fib;
	u32 returnStatus;

	Fib = Adapter->SyncFib;

	Fib->Header.StructType = TFib;
	Fib->Header.Size = sizeof(FIB);
	Fib->Header.XferState = HostOwned | FibInitialized | FibEmpty;
	Fib->Header.ReceiverFibAddress = 0;
	Fib->Header.SenderSize = sizeof(FIB);
	Fib->Header.SenderFibAddress = (u32)Fib;
	Fib->Header.Command = Command;

	//
	// Copy the Data portion into the Fib.
	//
	RtlCopyMemory( Fib->data, Data, Size );
	Fib->Header.XferState |= (SentFromHost | NormalPriority);
    
	//
	// Set the size of the Fib we want to send to the adapter
	//
	Fib->Header.Size = sizeof(FIB_HEADER) + Size;
	if (!Adapter->AdapterFuncs->SendSynchFib( Adapter->AdapterExtension, Adapter->SyncFibPhysicalAddress )) {
			return (FALSE);
	}

	//
	// Copy the response back to the caller's buffer.
	//
	RtlCopyMemory( Response, Fib->data, Fib->Header.Size - sizeof(FIB_HEADER) );
	*ResponseSize = Fib->Header.Size - sizeof(FIB_HEADER);

	//
	// Indicate success
	//
	return (TRUE);
}

//
// Define the highest level of host to adapter communication routines. These
// routines will support host to adapter FS commuication. These routines have
// no knowledge of the commuication method used. This level sends and receives
// FIBs. This level has no knowledge of how these FIBs get passed back and forth.
//

/*++

Routine Description:
	Sends the requested FIB to the adapter and optionally will wait for a
	response FIB. If the caller does not wish to wait for a response than
	an event to wait on must be supplied. This event will be set when a
	response FIB is received from the adapter.

Arguments:
	Fib is a pointer to the FIB the caller wishes to send to the adapter.
	Size - Size of the data portion of the Fib.
	Priority is an enumerated type which determines which priority level
        	the caller wishes to send this command at. 
	wait is a boolean which determines if the routine will wait for the
        	completion Fib to be returned(TRUE), or return when the Fib has been
        	successfully received by the adapter(FALSE).
	waitOn is only vaild when wait is FALSE. The Event will be set when the response
        	FIB has been returned by the adapter.
	ReturnFib is an optional pointer to a FIB that if present the response FIB will
        	copied to.     
       
Return Value:
	NT_SUCCESS if a Fib was returned to the caller.
	NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS SendFib(FIB_COMMAND Command, PFIB_CONTEXT Context, u32 Size,
	COMM_PRIORITIES Priority, int wait, void * WaitOn,
	int ResponseExpected, PFIB_CALLBACK FibCallback, void * FibCallbackContext)
{
	PCOMM_FIB_CONTEXT fc = (PCOMM_FIB_CONTEXT) Context;
	QUEUE_INDEX Index;
	QUEUE_TYPES WhichQueue;
	LARGE_INTEGER Timeout;
	AAC_STATUS status;
	PAFA_COMM_ADAPTER Adapter = fc->Adapter;
	u32 DontInterrupt = FALSE;
	PFIB Fib = fc->Fib;
	PCOMM_QUE OurQueue;

//	cmn_err( CE_NOTE, "^SendFib entered\n");

	Timeout = FsaCommData.AdapterTimeout;

	if (!(Fib->Header.XferState & HostOwned)) 
	{
        	FsaCommPrint("SendFib was called with a xfer state not set to HostOwned!\n");
		FsaCommLogEvent(fc,
			FsaCommData.DeviceObject, 
			FSAFS_FIB_INVALID, 
			STATUS_UNSUCCESSFUL, 
			BugCheckFileId | __LINE__,
			FACILITY_FSAFS_ERROR_CODE,
			NULL,
			TRUE);			
		return(STATUS_UNSUCCESSFUL);
	}
    
	//
	// There are 5 cases with the wait and reponse requested flags. The only invalid cases
	// are if the caller requests to wait and  does not request a response and if the
	// caller does not want a response and the Fib is not allocated from pool. If a response
	// is not requesed the Fib will just be deallocaed by the DPC routine when the response
	// comes back from the adapter. No further processing will be done besides deleting the
	// Fib. We will have a debug mode where the adapter can notify the host it had a problem
	// and the host can log that fact.

	if (wait && !ResponseExpected) 
	{
		FsaCommLogEvent(fc,
			FsaCommData.DeviceObject, 
			FSAFS_FIB_INVALID, 
			STATUS_UNSUCCESSFUL, 
			BugCheckFileId | __LINE__,
			FACILITY_FSAFS_ERROR_CODE,
			NULL,
			TRUE);			
		return(STATUS_UNSUCCESSFUL);
	}
	else if (!wait && ResponseExpected) 
	{
		Fib->Header.XferState |= (Async | ResponseExpected);
		FIB_COUNTER_INCREMENT(FsaCommData.AsyncSent);
	}
	else if (!wait && !ResponseExpected) 
	{
		Fib->Header.XferState |= NoResponseExpected;
		FIB_COUNTER_INCREMENT(FsaCommData.NoResponseSent);
	}
	else if (wait && ResponseExpected) 
	{
//      	OsCv_init(&fc->FsaEvent, NULL, CV_DRIVER, NULL);
        	Fib->Header.XferState |= ResponseExpected;
		FIB_COUNTER_INCREMENT(FsaCommData.NormalSent);
	} 
	Fib->Header.SenderData = (u32)fc; // so we can complete the io in the dpc routine

	//
	// Set FIB state to indicate where it came from and if we want a response from the
	// adapter. Also load the command from the caller.
	//

	Fib->Header.SenderFibAddress = (u32)Fib;
	Fib->Header.Command = Command;
	Fib->Header.XferState |= SentFromHost;
	fc->Fib->Header.Flags = 0;				// Zero the flags field - its internal only...
    
	//
	// Set the size of the Fib we want to send to the adapter
	//

	Fib->Header.Size = sizeof(FIB_HEADER) + Size;
	if (Fib->Header.Size > Fib->Header.SenderSize) 
	{
		return(STATUS_BUFFER_OVERFLOW);
	}                
	//
	// Get a queue entry connect the FIB to it and send an notify the adapter a command is ready.
	//
            
	if (Priority == FsaHigh) 
	{
	        Fib->Header.XferState |= HighPriority;
		WhichQueue = AdapHighCmdQueue;
		OurQueue = &Adapter->CommRegion->AdapHighCmdQue;
	}
	else
	{
        	Fib->Header.XferState |= NormalPriority;
        	WhichQueue = AdapNormCmdQueue;
		OurQueue = &Adapter->CommRegion->AdapNormCmdQue;
	}

#ifdef GATHER_FIB_TIMES
	if (GatherFibTimes) 
	{
		if (Command == NuFileSystem) 
		{
			FSACOMMAND FsaCommand;
			PREAD ReadCommand;
			PWRITE WriteCommand;
			int Index;
			FsaCommand = (FSACOMMAND)Fib->data[0];
			
			switch (FsaCommand) {
				case Read:
					ReadCommand = (PREAD)Fib->data;
					Index = ReadCommand->ByteCount >> 9;		// divide by 512
					fc->FibTimesPtr = &Adapter->FibTimes->Read[Index];
					break;

				case Write:
					WriteCommand = (PWRITE)Fib->data;
					Index = WriteCommand->ByteCount >> 9;		// divide by 512
					fc->FibTimesPtr = &Adapter->FibTimes->Write[Index];
					break;

				default:
					fc->FibTimesPtr = &Adapter->FibTimes->FileSys[FsaCommand];
					break;
			}
		}
		else
		{
			fc->FibTimesPtr = &Adapter->FibTimes->Other;
		}
	}
#endif

#ifdef FIB_CHECKSUMS
	if (FsaCommData.do_fib_checksums) 
	{
		int i;
		unsigned char *pFib;
		u32 CheckSum = 0;
		Fib->Header.FibCheckSum = 0;	// Zero out checksum before computing

		pFib = (unsigned char *)Fib;
		for (i = 0; i < Fib->Header.Size; i++) {
			CheckSum += *pFib++;
		}
		Fib->Header.FibCheckSum = CheckSum;
	}
#endif

	if ( GetQueueEntry( Adapter, &Index, WhichQueue, Fib, TRUE, fc, &DontInterrupt) != FSA_SUCCESS ) 
	{
		return(STATUS_UNSUCCESSFUL);
		//FsaBugCheck(IrpContext, Index, WhichQueue);
	}

#ifdef commdebug
	FsaCommPrint("SendFib: inserting a queue entry at index %d.\n",Index);
	FsaCommPrint("Fib contents:.\n");
	FsaCommPrint("  Command =               %d.\n", Fib->Header.Command);
	FsaCommPrint("  XferState  =            %x.\n", Fib->Header.XferState );
	FsaCommPrint("  Send Fib low address =  %x.\n", Fib->Header.SenderFibAddressLow);
	FsaCommPrint("  Send Fib high address = %x.\n",  Fib->Header.SenderFibAddressHigh);
	FsaCommPrint("  Sender data low =		%x.\n", Fib->Header.SenderDataLow);
	FsaCommPrint("  Sender data High =		%x.\n", Fib->Header.SenderDataHigh);    	
	FsaCommPrint("  Rec Fib address low =   %x.\n", Fib->Header.ReceiverFibAddressLow);        
#endif

	//
	// Fill in the Callback and CallbackContext if we are not going to wait.
	//

	if (!wait) {
		fc->FibCallback = FibCallback;
		fc->FibCallbackContext = FibCallbackContext;
	}

#ifdef GATHER_FIB_TIMES    
	if (GatherFibTimes) {
		fc->FibTimeStamp.QuadPart = KeQueryPerformanceCounter(NULL).QuadPart >> 7;
	}
#endif // GATHER_FIB_TIMES

	FIB_COUNTER_INCREMENT(FsaCommData.FibsSent);

#ifdef unix_fib_timeout
	// 
	// Put this fib onto the Outstanding I/O queue and increment the number of outstanding fibs.
	//

	KeQueryTickCount( &fc->TimeoutValue );
	fc->TimeoutValue.QuadPart += FsaCommData.FibTimeoutIncrement;
#endif
	InsertTailList( &OurQueue->OutstandingIoQueue, &fc->QueueEntry );
	OurQueue->NumOutstandingIos++;

	fc->FibComplete = 0;

	if ( InsertQueueEntry( Adapter, Index, WhichQueue, (DontInterrupt & FsaCommData.EnableInterruptModeration)) != FSA_SUCCESS ) {
	        return(STATUS_UNSUCCESSFUL);
	        //FsaBugCheck(IrpContext, Index, WhichQueue);
	}

	//
	// If the caller wanted us to wait for response wait now. 
	// If Timeouts are enabled than set the timeout otherwise wait forever.
	//
    
	FSA_DO_PERF_INC(FibsSent)

	if (wait) 
	{
		OsCvLockAcquire( fc->FsaEventMutex );
		while (fc->FibComplete == 0) {
			OsCv_wait( &fc->FsaEvent, fc->FsaEventMutex );
		}	
		OsCvLockRelease( fc->FsaEventMutex );
		if ( (fc->Flags & FIB_CONTEXT_FLAG_TIMED_OUT) ) {
			return(STATUS_IO_TIMEOUT);
        	} else {
       			return(STATUS_SUCCESS);
		}
	}

	//
	// If the user does not want a response than return success otherwise return pending
	// 

	ASSERT( FibCallback );

	if (ResponseExpected)
        	return(STATUS_PENDING);
	else
        	return(STATUS_SUCCESS);
}

/*++

Routine Description:

    Will return a pointer to the entry on the top of the queue requested that we are a consumer
    of, and return the address of the queue entry. It does not change the state of the queue. 

Arguments:

    OurQueue - is the queue the queue entry should be removed from.

    entry - is a pointer where the address  of the queue entry should be returned.    
    
Return Value:

    TRUE if there was a queue entry on the response queue for the host to consume.
    FALSE if there were no queue entries to consume.
    
--*/

int GetConsumerEntry(PAFA_COMM_ADAPTER Adapter, PCOMM_QUE OurQueue, PQUEUE_ENTRY *entry)
{
	QUEUE_INDEX Index;
	int status;

	if (*OurQueue->Headers.ProducerIndex == *OurQueue->Headers.ConsumerIndex) 
	{
		status = FALSE;
	} else {
		//
		// The consumer index must be wrapped if we have reached the end of
		// the queue. 
		// Else we just use the entry pointed to by the header index
		//
	    
		if (*OurQueue->Headers.ConsumerIndex >= OurQueue->QueueEntries) 
			Index = 0;		
		else
	        	Index = *OurQueue->Headers.ConsumerIndex;
		*entry = OurQueue->BaseAddress + Index;
#ifdef commdebug
		FsaCommPrint("Got a QE at Index %d, QE Addrss of %x.\n",Index,*entry);
#endif
		status = TRUE;
	}
	return(status);
}

int ConsumerEntryAvailable(PAFA_COMM_ADAPTER Adapter, PCOMM_QUE OurQueue)
{
	return (*OurQueue->Headers.ProducerIndex != *OurQueue->Headers.ConsumerIndex);
}

/*++

Routine Description:
    Frees up the current top of the queue we are a consumer of. If the queue was full
    notify the producer that the queue is no longer full.

Arguments:
    OurQueue - is the queue we will free the current consumer entry on.

Return Value:
    TRUE if there was a queue entry on the response queue for the host to consume.
    FALSE if there were no queue entries to consume.
    
--*/

void FreeConsumerEntry(PAFA_COMM_ADAPTER Adapter, PCOMM_QUE OurQueue, QUEUE_TYPES WhichQueue)
{
	int WasFull = FALSE;
	HOST_2_ADAP_EVENT Notify;

	if (*OurQueue->Headers.ProducerIndex+1 == *OurQueue->Headers.ConsumerIndex)
		WasFull = TRUE;
        
	if (*OurQueue->Headers.ConsumerIndex >= OurQueue->QueueEntries)
        	*OurQueue->Headers.ConsumerIndex = 1;
	else
        	*OurQueue->Headers.ConsumerIndex += 1;
        
	if (WasFull) 
	{
        	switch (WhichQueue) 
        	{
			case HostNormCmdQueue:
                		Notify = HostNormCmdNotFull;
                		break;
			case HostHighCmdQueue:
				Notify = HostHighCmdNotFull;
                		break;
			case HostNormRespQueue:
                		Notify = HostNormRespNotFull;
               			break;
			case HostHighRespQueue:
                		Notify = HostHighRespNotFull;
               			break;
		}
		NotifyAdapter(Adapter, Notify);
	}
}        

/*++

Routine Description:
	Will do all necessary work to complete a FIB that was sent from the adapter.

Arguments:
	Fib is a pointer to the FIB that caller wishes to complete processing on. 
	Size - Size of the completion Packet(Opitional). If not present than the current
           largest size in the Fib will be used
	Adapter - Pointer to which adapter sent this FIB

Return Value:
	NT_SUCCESS if a Fib was returned to the caller.
	NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS CompleteAdapterFib(PFIB_CONTEXT Context, u16 Size)
{
	PCOMM_FIB_CONTEXT fc = Context;
	PFIB Fib = fc->Fib;
	PAFA_COMM_ADAPTER Adapter = fc->Adapter;
	u32 DontInterrupt = FALSE;

	if (Fib->Header.XferState == 0)
        	return(STATUS_SUCCESS);

	//
	// If we plan to do anything check the structure type first.
	// 
	if ( Fib->Header.StructType != TFib ) 
	{
        	FsaCommPrint("Error CompleteFib called with a non Fib structure.\n");
        	return(STATUS_UNSUCCESSFUL);
	}

	//
	// This block handles the case where the adapter had sent us a command and we
	// have finished processing the command. We call completeFib when we are done
	// processing the command and want to send a response back to the adapter. This
	// will send the completed cdb to the adapter.
	//

	if (Fib->Header.XferState & SentFromAdapter) 
	{
        	Fib->Header.XferState |= HostProcessed;
        	if (Fib->Header.XferState & HighPriority) 
        	{
        		QUEUE_INDEX Index;
			if (Size) 
			{
				Size += sizeof(FIB_HEADER);
				if (Size > Fib->Header.SenderSize) 
					return(STATUS_BUFFER_OVERFLOW);
				Fib->Header.Size = Size;
			}
			if (GetQueueEntry(Adapter, &Index, AdapHighRespQueue, Fib, TRUE, NULL, &DontInterrupt) != STATUS_SUCCESS) 
			{
				FsaCommPrint("CompleteFib got an error geting a queue entry for a response.\n");
				return(FSA_FATAL);
			}
			if (InsertQueueEntry(Adapter, Index, AdapHighRespQueue, 
				(DontInterrupt & (int)FsaCommData.EnableInterruptModeration)) != STATUS_SUCCESS) 
			{
				FsaCommPrint("CompleteFib failed while inserting entry on the queue.\n");
			}
		}
		else if (Fib->Header.XferState & NormalPriority) 
		{
			QUEUE_INDEX Index;

			if (Size) 
			{
				Size += sizeof(FIB_HEADER);
				if (Size > Fib->Header.SenderSize) 
					return(STATUS_BUFFER_OVERFLOW);
				Fib->Header.Size = Size;
			}
			if (GetQueueEntry(Adapter, &Index, AdapNormRespQueue, Fib, TRUE, NULL, &DontInterrupt) != STATUS_SUCCESS) 
			{
		                FsaCommPrint("CompleteFib got an error geting a queue entry for a response.\n");
        		        return(FSA_FATAL);
			}
			if (InsertQueueEntry(Adapter, Index, AdapNormRespQueue, 
				(DontInterrupt & (int)FsaCommData.EnableInterruptModeration)) != STATUS_SUCCESS) 
			{
				FsaCommPrint("CompleteFib failed while inserting entry on the queue.\n");
			}
		}
	}
	else 
	{
        	cmn_err(CE_WARN, "CompleteFib: Unknown xferstate detected.\n");
		FsaBugCheck(0,0,0);
	}   
	return(STATUS_SUCCESS);
}

/*++

Routine Description:
    Will do all necessary work to complete a FIB. If the caller wishes to
    reuse the FIB after post processing has been completed Reinitialize
    should be called set to TRUE, otherwise the FIB will be returned to the
    free FIB pool. If Reinitialize is set to TRUE then the FIB header is
    reinitialzied and is ready for reuse on return from this routine.

Arguments:
    Fib is a pointer to the FIB that caller wishes to complete processing on. 
    Size - Size of the completion Packet(Opitional). If not present than the current
           largest size in the Fib will be used
    Reinitialize is a boolean which determines if the routine will ready the
        completed FIB for reuse(TRUE) or not(FALSE).

Return Value:
    NT_SUCCESS if a Fib was returned to the caller.
    NT_ERROR if event was an invalid event. 

--*/

AAC_STATUS CompleteFib(PFIB_CONTEXT Context)
{
	PCOMM_FIB_CONTEXT fc = (PCOMM_FIB_CONTEXT) Context;
	PAFA_COMM_ADAPTER Adapter = fc->Adapter;
	PFIB Fib = fc->Fib;

	//
	// Check for a fib which has already been completed
	//

	//	ASSERT(Fib->Header.XferState & AdapterProcessed);
	if (Fib->Header.XferState == 0)
        	return(STATUS_SUCCESS);

	//
	// If we plan to do anything check the structure type first.
	// 

	if ( Fib->Header.StructType != TFib ) {
        	FsaCommPrint("Error CompleteFib called with a non Fib structure.\n");
        	return(STATUS_UNSUCCESSFUL);
	}

#if 0
	//#if FSA_ADAPTER_METER
	//
	// Meter the completion
	//
	fsaMeterEnd(						// meter the end of an operation
		&(Adapter->FibMeter),			// .. the meter
		IrpContext->FibMeterType,		// .. type of operation
		&(IrpContext->FibStartTime),	// .. ptr to operation start timestamp
		FibGetMeterSize(Fib,			// .. number of bytes in operation
				IrpContext->FibMeterType, 
				IrpContext->FibSubCommand));
#endif // FSA_ADAPTER_METER
	
	//
	// This block completes a cdb which orginated on the host and we just need
	// to deallocate the cdb or reinit it. At this point the command is complete
	// that we had sent to the adapter and this cdb could be reused.
	//
	
	if ( (Fib->Header.XferState & SentFromHost) &&
		(Fib->Header.XferState & AdapterProcessed)) 
	{
	        ASSERT(fc->LogicalFibAddress.LowPart != 0);
	        return( DeallocateFib(fc) ); 
		//
		// This handles the case when the host has aborted the I/O to the
		// adapter because the adapter is not responding
		//

	}
	else if (Fib->Header.XferState & SentFromHost) 
	{
		ASSERT(fc->LogicalFibAddress.LowPart != 0);
		return( DeallocateFib(fc) ); 
	}
	else if (Fib->Header.XferState & HostOwned) 
	{
		return(DeallocateFib(fc));
	}
	else 
	{
        	cmn_err(CE_WARN, "CompleteFib: Unknown xferstate detected.\n");
		FsaBugCheck(0,0,0);
	}   
	return(STATUS_SUCCESS);
}

/*++

Routine Description:

	This routine handles a driver notify fib from the adapter and dispatches it to 
	the appropriate routine for handling.

Arguments:

	Adapter - Which adapter this fib is from
	fc - Pointer to FibContext from adapter.
    
Return Value:

    Nothing.
    
--*/

void HandleDriverAif(PAFA_COMM_ADAPTER Adapter, PCOMM_FIB_CONTEXT fc)
{
	PFIB Fib = fc->Fib;
	PAFA_CLASS_DRIVER dev;
	int Handled = FALSE;


	//
	// First loop through all of the class drivers to give them a chance to handle
	// the Fib.
	//

	dev = Adapter->ClassDriverList;
	while (dev) {
		if (dev->HandleAif) {
			if (dev->HandleAif( dev->ClassDriverExtension, fc ) ) {
				Handled = TRUE;
				break;
			}
		}
		dev = dev->Next;
	}

	if (!Handled) {
		//
		// Set the status of this FIB to be Invalid parameter.
		//
//		*(FSASTATUS *)Fib->data = ST_INVAL;
		*(FSASTATUS *)Fib->data = ST_OK;
		CompleteAdapterFib(fc, sizeof(FSASTATUS));
	}
}

/*++

Routine Description:
    Waits on the commandready event in it's queue. When the event gets set it will
    pull FIBs off it's queue. It will continue to pull FIBs off till the queue is empty.
    When the queue is empty it will wait for more FIBs.

Arguments:
    Context is used. All data os global
    
Return Value:
    Nothing.
    
--*/

int NormCommandThread(PAFA_COMM_ADAPTER Adapter)
{
	PFIB fib, newfib;
	COMM_FIB_CONTEXT fc; // for error logging
	KIRQL SavedIrql;
	PCOMM_REGION comm = Adapter->CommRegion;
	PLIST_ENTRY entry;
	PGET_ADAPTER_FIB_CONTEXT afc;

	//
	// We can only have one thread per adapter for AIF's.
	//

	if (Adapter->AifThreadStarted) {
		return (EINVAL);
	}

	// cmn_err(CE_NOTE, "AIF thread started");

	//
	// Let the DPC know it has a place to send the AIF's to.
	//

	Adapter->AifThreadStarted = TRUE;
	RtlZeroMemory(&fc, sizeof(COMM_FIB_CONTEXT));
	OsSpinLockAcquire(comm->HostNormCmdQue.QueueLock);

	while (TRUE) {
		//
		// NOTE : the QueueLock is held at the top of each loop.
		//

		ASSERT(OsSpinLockOwned(comm->HostNormCmdQue.QueueLock));

		while (!IsListEmpty(&(comm->HostNormCmdQue.CommandQueue))) {
			PLIST_ENTRY entry;
			PAIFCOMMANDTOHOST AifCommandToHost;

			entry = RemoveHeadList(&(comm->HostNormCmdQue.CommandQueue));
			OsSpinLockRelease(comm->HostNormCmdQue.QueueLock);
			fib = CONTAINING_RECORD( entry, FIB, Header.FibLinks );
						
			//
			// We will process the FIB here or pass it to a worker thread that is TBD. We Really
			// can't do anything at this point since we don't have anything defined for this thread to
			// do.
			//
			
			// cmn_err(CE_NOTE, "Got Fib from the adapter with a NORMAL priority, command 0x%x.\n", fib->Header.Command);

			RtlZeroMemory( &fc, sizeof(COMM_FIB_CONTEXT) );
			fc.NodeTypeCode = FSAFS_NTC_FIB_CONTEXT;
			fc.NodeByteSize = sizeof( COMM_FIB_CONTEXT );
			fc.Fib = fib;
			fc.FibData = fib->data;
			fc.Adapter = Adapter;
			
			//
			// We only handle AifRequest fibs from the adapter.
			//

			ASSERT(fib->Header.Command == AifRequest);
			AifCommandToHost = (PAIFCOMMANDTOHOST) fib->data;
			if (AifCommandToHost->command == AifCmdDriverNotify) {
				HandleDriverAif( Adapter, &fc );
			} else {
				OsCvLockAcquire(Adapter->AdapterFibMutex);
				entry = Adapter->AdapterFibContextList.Flink;

				//
				// For each Context that is on the AdapterFibContextList, make a copy of the
				// fib, and then set the event to wake up the thread that is waiting for it.
				//

				while (entry != &Adapter->AdapterFibContextList) {
					//
					// Extract the AdapterFibContext
					//

					afc = CONTAINING_RECORD( entry, GET_ADAPTER_FIB_CONTEXT, NextContext );

					//  Warning: sleep possible while holding spinlock
					newfib = kmalloc(sizeof(FIB), GFP_KERNEL);
					if (newfib) {
						//
						// Make the copy of the FIB
						//

						RtlCopyMemory(newfib, fib, sizeof(FIB));

						//
						// Put the FIB onto the AdapterFibContext's FibList
						//

						InsertTailList(&afc->FibList, &newfib->Header.FibLinks);
						afc->FibCount++;

						// 
						// Set the event to wake up the thread that will waiting.
						//
						OsCv_signal(&afc->UserEvent);
					}
					entry = entry->Flink;
				}

				//
				// Set the status of this FIB
				//
				*(FSASTATUS *)fib->data = ST_OK;
				CompleteAdapterFib( &fc, sizeof(FSASTATUS) );
				OsCvLockRelease(Adapter->AdapterFibMutex);
			}
			OsSpinLockAcquire(comm->HostNormCmdQue.QueueLock);
		}

		//
		// There are no more AIF's,  call cv_wait_sig to wait for more
		// to process.
		//

		// cmn_err(CE_NOTE, "no more AIF's going to sleep\n");

		if (OsCv_wait_sig( &(comm->HostNormCmdQue.CommandReady), 
						 comm->HostNormCmdQue.QueueLock ) == 0) {
			OsSpinLockRelease(comm->HostNormCmdQue.QueueLock);
			Adapter->AifThreadStarted = FALSE;
			// cmn_err(CE_NOTE, "AifThread awoken by a signal\n");
			return (EINTR);
		}				 
		// cmn_err(CE_NOTE, "^Aif thread awake, going to look for more AIF's\n");
	}
}
    

void *FsaGetFibData(PFIB_CONTEXT Context)
{
	PCOMM_FIB_CONTEXT fc = (PCOMM_FIB_CONTEXT) Context;
	return ((void *)fc->Fib->data);
}	    
                              

#ifdef API_THROTTLE

/*++

Routine Description:

	This routine is called as a DPC when a throttle period expires. It
	restarts all threads suspended due to the throttling flow control.
	
	The throttling counted semaphore is signalled for all waiting threads
	and the indicator of throttling active is cleared.

Arguments:
	Dpc		- Pointer to Dpc structure. Not used.
	DefferedContext - Pointer to per-adapter context. This is used to locate the
						  throttle information for this adapter.
	SystemArgument1	- Not used
	SystemArgument2 - Not used
	
Return Value:
	None.

--*/

void ThrottlePeriodEndDpcRtn(PKDPC Dpc, void * DeferredContext, void * SystemArgument1, void * SystemArgument2)
{
	PCOMM_REGION comm;
	PAFA_COMM_ADAPTER Adapter = (PAFA_COMM_ADAPTER) DeferredContext;

	comm = Adapter->CommRegion;

	//
	// Acquire the spinlock protecting the throttle status.
	//
	OsSpinLockAcquire(comm->AdapNormCmdQue.QueueLock);
	FsaCommPrint("ThrottlePeriodEndDpc\n");

	//
	// Check that the timer has fired as many times as it was set !
	//

	comm->ThrottleTimerFires++;
	ASSERT(comm->ThrottleTimerFires == comm->ThrottleTimerSets);

	//
	// The throttle period is now over. Restart all threads waiting
	// on the throttle being released.
	// Clear the throttle active indicator. This will allow new FIBs
	// to be sent to the adapter once we release the spinlock on exiting
	// the DPC. This means all restarted threads will be runnable
	// threads by then.
	//

	ASSERT(comm->ThrottleActive == TRUE);		// The throttle had better be on !
	comm->ThrottleActive = FALSE;				// This allows new data FIBs to go to the adapter on dpc exit

	OsSpinLockRelease(comm->AdapNormCmdQue.QueueLock);
}

#endif // #ifdef API_THROTTLE

