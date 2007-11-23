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
 *  comminit.c
 *
 * Abstract: This supports the initialization of the host adapter commuication interface.
 *    This is a platform dependent module for the pci cyclone board.
 *
 --*/
#include "comprocs.h"

#define BugCheckFileId                   (FSAFS_BUG_CHECK_COMMINIT)

void AfaCommBugcheckHandler(void * buf,u32 len);
void ThrottlePeriodEndDpcRtn(PKDPC Dpc,void * DeferredContext, void * SystemArgument1, void * SystemArgument2);
FSA_COMM_DATA FsaCommData;

AAC_STATUS HardInterruptModeration1Changed(void * AdapterContext, u32 nv)
{
	PAFA_COMM_ADAPTER adapter = AdapterContext;
	//
	// If we are using interrupt moderation, then disable the interrupt
	// until we need to use it.
	//
	if (FsaCommData.HardInterruptModeration1)
		DisableInterrupt( adapter, AdapNormCmdNotFull, FALSE );
	else
		EnableInterrupt( adapter, AdapNormCmdNotFull, FALSE );
	return (STATUS_SUCCESS);
}

AAC_STATUS FsaFibTimeoutChanged(void * AdapterContext, u32 nv)
{
	return (STATUS_SUCCESS);
}

#ifdef GATHER_FIB_TIMES
extern int GatherFibTimes;
#endif

FSA_USER_VAR FsaCommUserVars[] = {
#ifdef FIB_CHECKSUMS
	{ "do_fib_checksums", (u32 *)&FsaCommData.do_fib_checksums, NULL },
#endif
#ifdef GATHER_FIB_TIMES
	{ "GatherFibTimes", (u32 *)&GatherFibTimes, NULL },
#endif
	{ "EnableAdapterTimeouts", (u32 *)&FsaCommData.EnableAdapterTimeouts, NULL},
	{ "EnableInterruptModeration", (u32 *)&FsaCommData.EnableInterruptModeration, NULL },
	{ "FsaDataFibsSent", (u32 *) &FsaCommData.FibsSent, NULL },
	{ "FsaDataFibRecved", (u32 *) &FsaCommData.FibRecved, NULL },
	{ "HardInterruptModeration", (u32 *)&FsaCommData.HardInterruptModeration, NULL},
	{ "HardInterruptModeration1", (u32 *)&FsaCommData.HardInterruptModeration1, HardInterruptModeration1Changed},
	{ "EnableFibTimeoutBreak", (u32 *)&FsaCommData.EnableFibTimeoutBreak, NULL},
	{ "PeakFibsConsumed", (u32 *)&FsaCommData.PeakFibsConsumed, NULL },
	{ "ZeroFibsConsumed", (u32 *)&FsaCommData.ZeroFibsConsumed, NULL },
	{ "FibTimeoutSeconds", (u32 *) &FsaCommData.FibTimeoutSeconds, FsaFibTimeoutChanged },
};

#define NUM_COMM_USER_VARS	(sizeof(FsaCommUserVars) / sizeof(FSA_USER_VAR) )



/*++

Routine Description:
    This is the initialization routine for the FileArray Comm layer device driver.

Arguments:
    DriverObject - Pointer to driver object created by the system.

Return Value:
    AAC_STATUS - The function value is the final status from the initialization
		 operation.

--*/

AAC_STATUS AacCommDriverEntry(void)
{
	AAC_STATUS status;
	void * bugbuf;
	RtlZeroMemory( &FsaCommData, sizeof(FSA_COMM_DATA) );

	//
	// Load the global timeout value for the adapter timeout
	// Also init the global that enables or disables adapter timeouts
	//

//	FsaCommData.AdapterTimeout = RtlConvertLongToLargeInteger(-10*1000*1000*180);
	FsaCommData.FibTimeoutSeconds = 180;
	FsaCommData.EnableAdapterTimeouts = TRUE; 
//	FsaCommData.QueueFreeTimeout = RtlConvertLongToLargeInteger(QUEUE_ENTRY_FREE_TIMEOUT);
#ifdef unix_fib_timeout
	FsaCommData.FibTimeoutIncrement = (180 * 1000 * 1000 * 10) / KeQueryTimeIncrement();
#endif
	FsaCommData.EnableInterruptModeration = FALSE;

	//
	// Preload UserVars with all variables from the comm layer.  The class layers will
	// include theirs when they register.
	//

	FsaCommData.UserVars = kmalloc(NUM_COMM_USER_VARS * sizeof(FSA_USER_VAR), GFP_KERNEL);
	FsaCommData.NumUserVars = NUM_COMM_USER_VARS;

	RtlCopyMemory( FsaCommData.UserVars, &FsaCommUserVars, NUM_COMM_USER_VARS * sizeof(FSA_USER_VAR) );

#ifdef AACDISK
	//
	// Call the disk driver to initialize itself.
	//

	AacDiskDriverEntry();
#endif
	return (STATUS_SUCCESS);
}


/*++

Routine Description:
	This routine will release all of the resources used by a given queue.

Arguments:
	adapter - Which adapter the queue belongs to
	Queue - Pointer to the queue itself
	WhichQueue - Identifies which of the host queues this is.

Return Value:
	NONE.

--*/

void DetachNTQueue(PAFA_COMM_ADAPTER adapter, PCOMM_QUE queue, QUEUE_TYPES which)
{
	switch (which) 
	{
		case HostNormCmdQueue:
			Os_remove_softintr( queue->ConsumerRoutine );
			OsSpinLockDestroy( queue->QueueLock );
			OsCv_destroy( &queue->CommandReady );
			break;

		case HostHighCmdQueue:
			Os_remove_softintr( queue->ConsumerRoutine );
			OsSpinLockDestroy( queue->QueueLock );
			OsCv_destroy( &queue->CommandReady );
			break;

		case HostNormRespQueue:
			Os_remove_softintr( queue->ConsumerRoutine );
			OsSpinLockDestroy( queue->QueueLock );
			break;

		case HostHighRespQueue:
			Os_remove_softintr( queue->ConsumerRoutine );
			OsSpinLockDestroy( queue->QueueLock );
			break;

	        case AdapNormCmdQueue:
	        case AdapHighCmdQueue:
	        case AdapNormRespQueue:
	        case AdapHighRespQueue:
			OsCv_destroy( &queue->QueueFull );
			break;
	}
}
    
/*++

Routine Description:
    Will initialize all entries in the queue that is NT specific.

Arguments:

Return Value:
    Nothing there is nothing to allocate so nothing should fail

--*/

void InitializeNTQueue(PAFA_COMM_ADAPTER adapter, PCOMM_QUE queue, QUEUE_TYPES which)
{
	queue->NumOutstandingIos = 0;
	//
	// Store a pointer to the adapter structure.
	//
	queue->Adapter = adapter;
	InitializeListHead( &queue->OutstandingIoQueue );
	    
	switch (which) 
	{
	        case HostNormCmdQueue:
			OsCv_init( &queue->CommandReady);
			OsSpinLockInit( queue->QueueLock, adapter->SpinLockCookie);
			if (ddi_add_softintr( adapter->Dip, DDI_SOFTINT_HIGH, &queue->ConsumerRoutine, NULL,
								  NULL, (PUNIX_INTR_HANDLER)HostCommandNormDpc,
								  (caddr_t)queue ) != DDI_SUCCESS) {

				cmn_err(CE_CONT, "OS_addr_intr failed\n");					
			}					
			InitializeListHead(&queue->CommandQueue);
			break;

	        case HostHighCmdQueue:
			OsCv_init( &queue->CommandReady);
			OsSpinLockInit( queue->QueueLock, adapter->SpinLockCookie);
			if (ddi_add_softintr( adapter->Dip, DDI_SOFTINT_HIGH, &queue->ConsumerRoutine, NULL,
						  NULL, (PUNIX_INTR_HANDLER)HostCommandHighDpc,
						  (caddr_t) queue ) != DDI_SUCCESS) {

				cmn_err(CE_CONT, "OS_addr_intr failed\n");					
			}					
			InitializeListHead(&queue->CommandQueue);
			break;

        	case HostNormRespQueue:
			OsSpinLockInit( queue->QueueLock, adapter->SpinLockCookie);
			if (ddi_add_softintr( adapter->Dip, DDI_SOFTINT_HIGH, &queue->ConsumerRoutine, NULL,
						  NULL, (PUNIX_INTR_HANDLER)HostResponseNormalDpc, 
						  (caddr_t) queue ) != DDI_SUCCESS) {
				cmn_err(CE_CONT, "OS_addr_intr failed\n");					
			}					
			break;

		case HostHighRespQueue:
			OsSpinLockInit( queue->QueueLock, adapter->SpinLockCookie);
			if (ddi_add_softintr( adapter->Dip, DDI_SOFTINT_HIGH, &queue->ConsumerRoutine, NULL,
						  NULL, (PUNIX_INTR_HANDLER)HostResponseHighDpc, 
						  (caddr_t) queue ) != DDI_SUCCESS) {

				cmn_err(CE_CONT, "OS_addr_intr failed\n");					
			}					
			break;

	        case AdapNormCmdQueue:
	        case AdapHighCmdQueue:
	        case AdapNormRespQueue:
        	case AdapHighRespQueue:
			OsCv_init( &queue->QueueFull);
			break;
	}
}

/*++

Routine Description:
    Create and start the command receiver threads.

Arguments:

Return Value:
    Nothing

--*/

int StartFsaCommandThreads(PAFA_COMM_ADAPTER adapter)
{
    return(TRUE);
}

/*++

Routine Description:
	This routine will do all of the work necessary to timeout the given fib.

Arguments:
	adapter - Pointer to an adapter structure.
	fc - Pointer to the context to time out.

Return Value:
	Nothing.

--*/

void AfaCommTimeoutFib(PAFA_COMM_ADAPTER adapter, PCOMM_FIB_CONTEXT fc)
{
	PFIB Fib = fc->Fib;

#ifdef unix_fib_timeout
	if (Fib->Header.XferState & Async) {
		fc->FibCallback(fc->FibCallbackContext, fc, STATUS_IO_TIMEOUT);
	} else {
		KeSetEvent(&fc->FsaEvent, 0, FALSE);
	}
#endif
}

/*++

Routine Description:
	This DPC routine is executed by the expiration of a periodic timer.  The purpose of this routine
	is to check for fib's that should be timed out.

Arguments:
	Dpc - Pointer to this routine.

Return Value:
	Nothing.

--*/

void AfaCommTimeoutRoutine(PKDPC Dpc, void *NullArgument, void *Argument1, void * Argument2)
{
#ifdef unix_fib_timeout
	PCOMM_QUE OurQueue;
	PLIST_ENTRY Entry, NextEntry;
	LIST_ENTRY TimeoutQueue;
	PCOMM_FIB_CONTEXT fc;
	LARGE_INTEGER TickCount;
	PAFA_COMM_ADAPTER adapter;

	adapter = FsaCommData.AdapterList;
	while (adapter) {
		InitializeListHead( &TimeoutQueue );
		OurQueue = &adapter->CommRegion->AdapNormCmdQue;

//		KeAcquireSpinLockAtDpcLevel( OurQueue->QueueLock );
		OsSpinLockAcquire( OurQueue->QueueLock );
		KeQueryTickCount(&TickCount);
		Entry = OurQueue->OutstandingIoQueue.Flink;

		while (Entry != &OurQueue->OutstandingIoQueue) {
			fc = CONTAINING_RECORD( Entry, COMM_FIB_CONTEXT, QueueEntry );
			//
			// If the current tick count if less than the first fib on the queue, then
			// none of the fib's have timed out.
			//
			if (TickCount.QuadPart <= fc->TimeoutValue.QuadPart) {
				break;
			}	

			//
			// First, grab the next entry, then put this entry onto the queue to be timed out.
			//
			NextEntry = Entry->Flink;

			//
			// Mark the FibContext as timed out while we have the SpinLock.
			//
			fc = CONTAINING_RECORD( Entry, COMM_FIB_CONTEXT, QueueEntry );
			fc->Flags |= FIB_CONTEXT_FLAG_TIMED_OUT;
			fc->Fib->Header.XferState |= TimedOut;

			RemoveEntryList( Entry );
			OurQueue->NumOutstandingIos--;	

			InsertTailList( &TimeoutQueue, Entry );	
			Entry = NextEntry;
		}
//		KeReleaseSpinLockFromDpcLevel( OurQueue->QueueLock );
		OsSpinLockRelease( OurQueue->QueueLock );
		//
		// Now walk through all fibs that need to be timed out.
		//

		while (!IsListEmpty( &TimeoutQueue )) {
			Entry = RemoveHeadList( &TimeoutQueue );
			fc = CONTAINING_RECORD( Entry, COMM_FIB_CONTEXT, QueueEntry );
			AfaCommTimeoutFib( adapter, fc );
		}
		adapter = adapter->NextAdapter;
	}
#endif
}

/*++

Routine Description:
	This routine gets called to detach all resources that have been allocated for 
	this adapter.

Arguments:
	adapter - Pointer to the adapter structure to detach.

Return Value:
	TRUE - All resources have been properly released.
	FALSE - An error occured while trying to release resources.

--*/

int AacCommDetachAdapter(PAFA_COMM_ADAPTER adapter)
{
	PAFA_CLASS_DRIVER dev;
	//
	// First remove this adapter from the list of adapters.
	//

	if (FsaCommData.AdapterList == adapter) {
		FsaCommData.AdapterList = adapter->NextAdapter;
	} else {
		PAFA_COMM_ADAPTER CurrentAdapter, NextAdapter;

		CurrentAdapter = FsaCommData.AdapterList;
		NextAdapter = CurrentAdapter->NextAdapter;

		while (NextAdapter) {
			if (NextAdapter == adapter) {
				CurrentAdapter->NextAdapter = NextAdapter->NextAdapter;
				break;
			}
			CurrentAdapter = NextAdapter;
			NextAdapter = CurrentAdapter->NextAdapter;
		}
	}			
		
	//
	// First send a shutdown to the adapter.
	//

	AfaCommShutdown( adapter );

	//
	// Destroy the FibContextZone for this adapter.  This will free up all
	// of the fib space used by this adapter.
	//
	
	FsaFreeFibContextZone( adapter );

	//
	// Destroy the mutex used for synch'ing adapter fibs.
	//

	OsCvLockDestroy( adapter->AdapterFibMutex );

	//
	// Detach all of the host queues.
	//

	DetachNTQueue( adapter, &adapter->CommRegion->AdapHighRespQue, AdapHighRespQueue );
	DetachNTQueue( adapter, &adapter->CommRegion->AdapNormRespQue, AdapNormRespQueue );
	DetachNTQueue( adapter, &adapter->CommRegion->HostHighRespQue, HostHighRespQueue );
	DetachNTQueue( adapter, &adapter->CommRegion->HostNormRespQue, HostNormRespQueue );
	DetachNTQueue( adapter, &adapter->CommRegion->AdapHighCmdQue, AdapHighCmdQueue );
	DetachNTQueue( adapter, &adapter->CommRegion->AdapNormCmdQue, AdapNormCmdQueue );
	DetachNTQueue( adapter, &adapter->CommRegion->HostHighCmdQue, HostHighCmdQueue );
	DetachNTQueue( adapter, &adapter->CommRegion->HostNormCmdQue, HostNormCmdQueue );

	//
	// Destroy the mutex used to protect the FibContextZone
	//

	OsSpinLockDestroy( adapter->FibContextZoneSpinLock );

	//
	// Call the miniport to free the space allocated for the shared comm queues
	// between the host and the adapter.
	//

	FsaFreeAdapterCommArea( adapter );

	//
	// Free the memory used by the comm region for this adapter
	//

	kfree(adapter->CommRegion);

	//
	// Free the memory used by the adapter structure.
	//
	dev = adapter->ClassDriverList;
	adapter->ClassDriverList = adapter->ClassDriverList->Next;
	kfree(dev);
	kfree(adapter);

	return (TRUE);
}

void *AfaCommInitNewAdapter(PFSA_NEW_ADAPTER NewAdapter)
{
	void * bugbuf;
	PAFA_COMM_ADAPTER adapter;
	MAPFIB_CONTEXT MapFibContext;
	LARGE_INTEGER Time;
	char ErrorBuffer[60];

//	adapter = (PAFA_COMM_ADAPTER)ExAllocatePool(NonPagedPoolMustSucceed, sizeof(AFA_COMM_ADAPTER));
	adapter = (PAFA_COMM_ADAPTER) kmalloc( sizeof(AFA_COMM_ADAPTER) , GFP_KERNEL );

	if (adapter == NULL)
		return (NULL);

	RtlZeroMemory(adapter, sizeof(AFA_COMM_ADAPTER));


	//
	// Save the current adapter number and increment the total number.
	//

	adapter->AdapterNumber = FsaCommData.TotalAdapters++;


	//
	// Fill in the pointer back to the device specific structures.
	// The device specific driver has also passed a pointer for us to 
	// fill in with the Adapter object that we have created.
	//

	adapter->AdapterExtension = NewAdapter->AdapterExtension;
	adapter->AdapterFuncs = NewAdapter->AdapterFuncs;
	adapter->InterruptsBelowDpc = NewAdapter->AdapterInterruptsBelowDpc;
	adapter->AdapterUserVars = NewAdapter->AdapterUserVars;
	adapter->AdapterUserVarsSize = NewAdapter->AdapterUserVarsSize;

	adapter->Dip = NewAdapter->Dip;

	//
	// Fill in Our address into the function dispatch table
	//

	NewAdapter->AdapterFuncs->InterruptHost = AfaCommInterruptHost;
	NewAdapter->AdapterFuncs->OpenAdapter = AfaCommOpenAdapter;
	NewAdapter->AdapterFuncs->CloseAdapter = AfaCommCloseAdapter;
	NewAdapter->AdapterFuncs->DeviceControl = AfaCommAdapterDeviceControl;

	//
	// Ok now init the communication subsystem
	//

	adapter->CommRegion = (PCOMM_REGION)kmalloc(sizeof(COMM_REGION), GFP_KERNEL);
	if (adapter->CommRegion == NULL) {
		cmn_err(CE_WARN, "Error could not allocate comm region.\n");
		return (NULL);
	}
	RtlZeroMemory(adapter->CommRegion, sizeof(COMM_REGION));

	//
	// Get a pointer to the iblock_cookie
	//

	ddi_get_soft_iblock_cookie( adapter->Dip, DDI_SOFTINT_HIGH, &adapter->SpinLockCookie );

	if (!CommInit(adapter)) {
		FsaCommPrint("Failed to init the commuication subsystem.\n");
		return(NULL);
	}

#ifdef unix_fib_timeout
	//
	// If this is the first adapter, then start the timeout routine timer.
	//

	if (adapter->AdapterNumber == 0) {
		//
		// Initialize the DPC used to check for Fib timeouts.
		//

		KeInitializeDpc( &FsaCommData.TimeoutDPC, AfaCommTimeoutRoutine, NULL );

		//
		// Initialize the Timer used to check for Fib Timeouts.
		//

		KeInitializeTimer( &FsaCommData.TimeoutTimer );

		//
		// Set the timer to go off every 15 seconds.
		//

		Time.QuadPart = - (15 * 10 * 1000 * 1000);
		KeSetTimerEx( &FsaCommData.TimeoutTimer, Time, (15 * 1000), &FsaCommData.TimeoutDPC );
	}	
#endif


	//
	// Initialize the list of AdapterFibContext's.
	//

	InitializeListHead(&adapter->AdapterFibContextList);

	//
	// Initialize the fast mutex used for synchronization of the adapter fibs
	//

	adapter->AdapterFibMutex = OsCvLockAlloc();
	OsCvLockInit(adapter->AdapterFibMutex, NULL);

	//
	// Allocate and start the FSA command threads. These threads will handle
	// command requests from the adapter. They will wait on an event then pull
	// all CDBs off the thread's queue. Each CDB will be given to a worker thread
	// upto a defined limit. When that limit is reached wait a event will be waited
	// on till a worker thread is finished.
	//

	if (!StartFsaCommandThreads(adapter)) {
   		FsaCommPrint("Fsainit could not initilize the command receiver threads.\n");
		return (NULL);
	}

#ifdef unix_crash_dump
	//
	// Allocate and map a fib for use by the synch path, which is used for crash
	// dumps.
	//
	// Allocate an entire page so that alignment is correct.
	//

	adapter->SyncFib = kmalloc(PAGE_SIZE, GFP_KERNEL);
	MapFibContext.Fib = adapter->SyncFib;
	MapFibContext.Size = sizeof(FIB);
	MapFib( adapter, &MapFibContext );
	adapter->SyncFibPhysicalAddress = MapFibContext.LogicalFibAddress.LowPart;
#endif

	adapter->CommFuncs.SizeOfAfaCommFuncs = sizeof(AFACOMM_FUNCS);

	adapter->CommFuncs.AllocateFib = AllocateFib;

	adapter->CommFuncs.FreeFib = FreeFib;
	adapter->CommFuncs.FreeFibFromDpc = FreeFibFromDpc;
	adapter->CommFuncs.DeallocateFib = DeallocateFib;

	adapter->CommFuncs.InitializeFib = InitializeFib;
	adapter->CommFuncs.GetFibData = FsaGetFibData;
	adapter->CommFuncs.SendFib = SendFib;
	adapter->CommFuncs.CompleteFib = CompleteFib;
	adapter->CommFuncs.CompleteAdapterFib = CompleteAdapterFib;

	adapter->CommFuncs.SendSynchFib = SendSynchFib;

#ifdef GATHER_FIB_TIMES
	//
	// Initialize the Fib timing data structures
	//
	{
		PFIB_TIMES FibTimesPtr;
		int i;

		KeQueryPerformanceCounter(&adapter->FibTimesFrequency);
		adapter->FibTimesFrequency.QuadPart >>= 7;
		adapter->FibTimes = (PALL_FIB_TIMES)ExAllocatePool(NonPagedPool, sizeof(ALL_FIB_TIMES));
		RtlZeroMemory(adapter->FibTimes, sizeof(ALL_FIB_TIMES));

		for (i = 0; i < MAX_FSACOMMAND_NUM; i++) {
			FibTimesPtr = &adapter->FibTimes->FileSys[i];
			FibTimesPtr->Minimum.LowPart = 0xffffffff;
			FibTimesPtr->Minimum.HighPart = 0x7fffffff;
			FibTimesPtr->AdapterMinimum.LowPart = 0xffffffff;
			FibTimesPtr->AdapterMinimum.HighPart = 0x7fffffff;
		}
		for (i = 0; i < MAX_RW_FIB_TIMES; i++) {
			FibTimesPtr = &adapter->FibTimes->Read[i];
			FibTimesPtr->Minimum.LowPart = 0xffffffff;
			FibTimesPtr->Minimum.HighPart = 0x7fffffff;
			FibTimesPtr->AdapterMinimum.LowPart = 0xffffffff;
			FibTimesPtr->AdapterMinimum.HighPart = 0x7fffffff;
		}
		for (i = 0; i < MAX_RW_FIB_TIMES; i++) {
			FibTimesPtr = &adapter->FibTimes->Write[i];
			FibTimesPtr->Minimum.LowPart = 0xffffffff;
			FibTimesPtr->Minimum.HighPart = 0x7fffffff;
			FibTimesPtr->AdapterMinimum.LowPart = 0xffffffff;
			FibTimesPtr->AdapterMinimum.HighPart = 0x7fffffff;
		}
		FibTimesPtr = &adapter->FibTimes->Other;
		FibTimesPtr->Minimum.LowPart = 0xffffffff;
		FibTimesPtr->Minimum.HighPart = 0x7fffffff;
		FibTimesPtr->AdapterMinimum.LowPart = 0xffffffff;
		FibTimesPtr->AdapterMinimum.HighPart = 0x7fffffff;
	}
#endif
	//
	// Add this adapter in to our Adapter List.
	//

	adapter->NextAdapter = FsaCommData.AdapterList;
	FsaCommData.AdapterList = adapter;
	NewAdapter->Adapter = adapter;
//	AfaDiskInitNewAdapter( adapter->AdapterNumber, adapter );
	return (adapter);
}

AAC_STATUS CommInitialize(PAFA_COMM_ADAPTER adapter)
{
	//
	//  Now allocate and initialize the zone structures used as our pool
	//  of FIB context records.  The size of the zone is based on the
	//  system memory size.  We also initialize the mutex used to protect
	//  the zone.
	//
	adapter->FibContextZoneSpinLock= OsSpinLockAlloc();
	OsSpinLockInit( adapter->FibContextZoneSpinLock, adapter->SpinLockCookie );
	adapter->FibContextZoneExtendSize = 64;
	return (STATUS_SUCCESS);
}


    
/*++

Routine Description:
    Initializes the data structures that are required for the FSA commuication
    interface to operate.

Arguments:
    None - all global or allocated data.

Return Value:
    TRUE - if we were able to init the commuication interface.
    FALSE - If there were errors initing. This is a fatal error.
--*/

int CommInit(PAFA_COMM_ADAPTER adapter)
{
	u32 SizeOfHeaders = (sizeof(QUEUE_INDEX) * NUMBER_OF_COMM_QUEUES) * 2;
	u32 SizeOfQueues = sizeof(QUEUE_ENTRY) * TOTAL_QUEUE_ENTRIES;
	PQUEUE_INDEX Headers;
	PQUEUE_ENTRY Queues;
	u32 TotalSize;
	PCOMM_REGION CommRegion = adapter->CommRegion;

	CommInitialize( adapter );
	FsaCommPrint("CommInit: Queue entry size is 0x%x, Queue index size is 0x%x, Number of total entries is 0x%x, # queues = 0x%x.\n",
			  sizeof(QUEUE_ENTRY), sizeof(QUEUE_INDEX), TOTAL_QUEUE_ENTRIES, NUMBER_OF_COMM_QUEUES);
	//
	//
	// Allocate the physically contigous space for the commuication queue
	// headers. 
	//

	TotalSize = SizeOfHeaders + SizeOfQueues;
	if (!FsaAllocateAdapterCommArea(adapter, (void * *)&Headers, TotalSize, QUEUE_ALIGNMENT))
		return (FALSE);

#ifdef API_THROTTLE
	//
	// Initialize the throttle semaphore.
	// Its a counted semaphore so we can allow a
	// number of threads to be signalled by it at once.
	//

	CommRegion->ThrottleLimit = THROTTLE_MAX_DATA_FIBS;
	CommRegion->ThrottleTimeout = RtlConvertLongToLargeInteger(THROTTLE_PERIOD_DURATION);
	CommRegion->ThrottleWaitTimeout = RtlConvertLongToLargeInteger(THROTTLE_WAIT_DURATION);
	CommRegion->ThrottleActive = FALSE;						// Is there a current throttle active period ?
	CommRegion->ThrottleTimerFires = 0;						// No fires of throttle timer yet.
	CommRegion->ThrottleTimerSets = 0;

	CommRegion->ThrottledFibs = 0;
	CommRegion->ThrottleTimedoutFibs = 0;
	CommRegion->ApiFibs = 0;
	CommRegion->NonPassiveFibs = 0;
	CommRegion->TotalFibs = 0;
	CommRegion->FSInfoFibs = 0;
	CommRegion->ThrottleTimedoutFibs = 0;

	//
	// Initialize the semaphore controlling I/Os to the adapter.
	// We set it not signalled with a maximum signalled count
	// representing the maximum number of I/Os we'll allow at
	// once at the adapter.
	//
	KeInitializeSemaphore(&CommRegion->ThrottleReleaseSema,
						  CommRegion->ThrottleLimit,
						  CommRegion->ThrottleLimit);
	//
	// Initialize the Timer and Dpc for the Throttle timeout routine.
	//
	KeInitializeTimer(&CommRegion->ThrottleTimer);
	KeInitializeDpc(&CommRegion->ThrottleDpc,
					(PKDEFERRED_ROUTINE) &ThrottlePeriodEndDpcRtn,
					(void *) adapter);

#endif // #ifdef API_THROTTLE
	Queues = (PQUEUE_ENTRY)((unsigned char *)Headers + SizeOfHeaders);
	if (ddi_add_softintr( adapter->Dip, DDI_SOFTINT_HIGH, &CommRegion->QueueNotFullDpc, NULL,
						  NULL, (PUNIX_INTR_HANDLER)CommonNotFullDpc,
						  (caddr_t)CommRegion ) != DDI_SUCCESS) {
		cmn_err(CE_CONT, "Os_addr_intr failed\n");					
	}					

	// adapter to Host normal priority Command queue
	CommRegion->HostNormCmdQue.Headers.ProducerIndex = Headers++;
	CommRegion->HostNormCmdQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->HostNormCmdQue.Headers.ProducerIndex = HOST_NORM_CMD_ENTRIES;
	*CommRegion->HostNormCmdQue.Headers.ConsumerIndex = HOST_NORM_CMD_ENTRIES;

	CommRegion->HostNormCmdQue.SavedIrql = 0;
	CommRegion->HostNormCmdQue.BaseAddress = Queues;
	CommRegion->HostNormCmdQue.QueueEntries = HOST_NORM_CMD_ENTRIES;
	CommRegion->HostNormCmdQue.QueueLock = OsSpinLockAlloc();
	if (CommRegion->HostNormCmdQue.QueueLock == NULL) {
		return (FALSE);
	}
	InitializeNTQueue(adapter, &CommRegion->HostNormCmdQue, HostNormCmdQueue);
	Queues += HOST_NORM_CMD_ENTRIES;

	// Adapter to Host high priority command queue
	CommRegion->HostHighCmdQue.Headers.ProducerIndex = Headers++;
	CommRegion->HostHighCmdQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->HostHighCmdQue.Headers.ProducerIndex = HOST_HIGH_CMD_ENTRIES;
	*CommRegion->HostHighCmdQue.Headers.ConsumerIndex = HOST_HIGH_CMD_ENTRIES;
	CommRegion->HostHighCmdQue.SavedIrql = 0;
	CommRegion->HostHighCmdQue.BaseAddress = Queues;
	CommRegion->HostHighCmdQue.QueueEntries = HOST_HIGH_CMD_ENTRIES;
//	CommRegion->HostHighCmdQue.QueueLock = (PKSPIN_LOCK) ExAllocatePool(NonPagedPool, sizeof(KSPIN_LOCK));
	CommRegion->HostHighCmdQue.QueueLock = OsSpinLockAlloc();
	if (CommRegion->HostHighCmdQue.QueueLock == NULL) {
		return (FALSE);
	}
	InitializeNTQueue(adapter, &CommRegion->HostHighCmdQue, HostHighCmdQueue);
	Queues += HOST_HIGH_CMD_ENTRIES;

	// Host to adapter normal priority command queue
    
	CommRegion->AdapNormCmdQue.Headers.ProducerIndex = Headers++;
	CommRegion->AdapNormCmdQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->AdapNormCmdQue.Headers.ProducerIndex = ADAP_NORM_CMD_ENTRIES;
	*CommRegion->AdapNormCmdQue.Headers.ConsumerIndex = ADAP_NORM_CMD_ENTRIES;

	CommRegion->AdapNormCmdQue.SavedIrql = 0;    
	CommRegion->AdapNormCmdQue.BaseAddress = Queues;
	CommRegion->AdapNormCmdQue.QueueEntries = ADAP_NORM_CMD_ENTRIES;
	InitializeNTQueue(adapter, &CommRegion->AdapNormCmdQue, AdapNormCmdQueue);
    
	Queues += ADAP_NORM_CMD_ENTRIES;

	// host to adapter high priority command queue
	CommRegion->AdapHighCmdQue.Headers.ProducerIndex = Headers++;
	CommRegion->AdapHighCmdQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->AdapHighCmdQue.Headers.ProducerIndex = ADAP_HIGH_CMD_ENTRIES;
	*CommRegion->AdapHighCmdQue.Headers.ConsumerIndex = ADAP_HIGH_CMD_ENTRIES;

	CommRegion->AdapHighCmdQue.SavedIrql = 0;    
	CommRegion->AdapHighCmdQue.BaseAddress = Queues;
	CommRegion->AdapHighCmdQue.QueueEntries = ADAP_HIGH_CMD_ENTRIES;
	InitializeNTQueue(adapter, &CommRegion->AdapHighCmdQue, AdapHighCmdQueue);
    
	Queues += ADAP_HIGH_CMD_ENTRIES;

	// adapter to host normal priority response queue
	CommRegion->HostNormRespQue.Headers.ProducerIndex = Headers++;
	CommRegion->HostNormRespQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->HostNormRespQue.Headers.ProducerIndex = HOST_NORM_RESP_ENTRIES;
	*CommRegion->HostNormRespQue.Headers.ConsumerIndex = HOST_NORM_RESP_ENTRIES;

	CommRegion->HostNormRespQue.SavedIrql = 0;    
	CommRegion->HostNormRespQue.BaseAddress = Queues;
	CommRegion->HostNormRespQue.QueueEntries = HOST_NORM_RESP_ENTRIES;
//	CommRegion->HostNormRespQue.QueueLock = (PKSPIN_LOCK) ExAllocatePool(NonPagedPool, sizeof(KSPIN_LOCK));
	CommRegion->HostNormRespQue.QueueLock = OsSpinLockAlloc();
	if (CommRegion->HostNormRespQue.QueueLock == NULL) {
		return (FALSE);
	}
	InitializeNTQueue(adapter, &CommRegion->HostNormRespQue, HostNormRespQueue);
    
	Queues += HOST_NORM_RESP_ENTRIES;

	// adapter to host high priority response queue
    
	CommRegion->HostHighRespQue.Headers.ProducerIndex = Headers++;
	CommRegion->HostHighRespQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->HostHighRespQue.Headers.ProducerIndex = HOST_HIGH_RESP_ENTRIES;
	*CommRegion->HostHighRespQue.Headers.ConsumerIndex = HOST_HIGH_RESP_ENTRIES;

	CommRegion->HostHighRespQue.SavedIrql = 0;    
	CommRegion->HostHighRespQue.BaseAddress = Queues;
	CommRegion->HostHighRespQue.QueueEntries = HOST_HIGH_RESP_ENTRIES;
//	CommRegion->HostHighRespQue.QueueLock = (PKSPIN_LOCK) ExAllocatePool(NonPagedPool, sizeof(KSPIN_LOCK));
	CommRegion->HostHighRespQue.QueueLock = OsSpinLockAlloc();
	if (CommRegion->HostHighRespQue.QueueLock == NULL) {
		return (FALSE);
	}
	InitializeNTQueue(adapter, &CommRegion->HostHighRespQue, HostHighRespQueue);
	Queues += HOST_HIGH_RESP_ENTRIES;

	// host to adapter normal priority response queue
    
	CommRegion->AdapNormRespQue.Headers.ProducerIndex = Headers++;
	CommRegion->AdapNormRespQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->AdapNormRespQue.Headers.ProducerIndex = ADAP_NORM_RESP_ENTRIES;
	*CommRegion->AdapNormRespQue.Headers.ConsumerIndex = ADAP_NORM_RESP_ENTRIES;

	CommRegion->AdapNormRespQue.SavedIrql = 0;    
	CommRegion->AdapNormRespQue.BaseAddress = Queues;
	CommRegion->AdapNormRespQue.QueueEntries = ADAP_NORM_RESP_ENTRIES;
	InitializeNTQueue(adapter, &CommRegion->AdapNormRespQue, AdapNormRespQueue);
    
	Queues += ADAP_NORM_RESP_ENTRIES;

	// host to adapter high priority response queue
    
	CommRegion->AdapHighRespQue.Headers.ProducerIndex = Headers++;
	CommRegion->AdapHighRespQue.Headers.ConsumerIndex = Headers++;
	*CommRegion->AdapHighRespQue.Headers.ProducerIndex = ADAP_HIGH_RESP_ENTRIES;
	*CommRegion->AdapHighRespQue.Headers.ConsumerIndex = ADAP_HIGH_RESP_ENTRIES;

	CommRegion->AdapHighRespQue.SavedIrql = 0;    
	CommRegion->AdapHighRespQue.BaseAddress = Queues;
	CommRegion->AdapHighRespQue.QueueEntries = ADAP_HIGH_RESP_ENTRIES;
	InitializeNTQueue(adapter, &CommRegion->AdapHighRespQue, AdapHighRespQueue);

	CommRegion->AdapNormCmdQue.QueueLock = CommRegion->HostNormRespQue.QueueLock;
	CommRegion->AdapHighCmdQue.QueueLock = CommRegion->HostHighRespQue.QueueLock;
	CommRegion->AdapNormRespQue.QueueLock = CommRegion->HostNormCmdQue.QueueLock;
	CommRegion->AdapHighRespQue.QueueLock = CommRegion->HostHighCmdQue.QueueLock;

	return(TRUE);
}

/*++

Routine Description:
	This routine will send a shutdown request to each adapter.

Arguments:
	adapter - which adapter to send the shutdown to.

Return Value:
	NT status success.

--*/

AAC_STATUS AfaCommShutdown(PAFA_COMM_ADAPTER adapter)
{
	PFIB_CONTEXT fc;
	PCLOSECOMMAND CloseCommand;
	AAC_STATUS status;

	fc = AllocateFib( adapter );
	InitializeFib( fc );

	CloseCommand = (PCLOSECOMMAND) FsaGetFibData( fc );
	CloseCommand->Command = VM_CloseAll;
	CloseCommand->ContainerId = 0xffffffff;

	status = SendFib( ContainerCommand, fc, sizeof(CLOSECOMMAND), FsaNormal, TRUE, NULL, TRUE, NULL, NULL );

	if (status != STATUS_SUCCESS) {
		FreeFib( fc );
		goto ret;
	}
	CompleteFib( fc );
	FreeFib( fc );
	status = STATUS_SUCCESS;
ret:
	return (status);
}

/*++

Routine Description:
	This routine will shutdown the adapter if there is a bugcheck and
	copy the shutdown data from the adapter response into the buffer
	so it will show up in the host dump file.

Arguments:
	buf - This buffer will be written to the host dump by nt for us.
	len - The size of the buffer.

Return Value:
	N/A

--*/

void AfaCommBugcheckHandler(void * buf, u32 len)
{
	PAFA_COMM_ADAPTER adapter = FsaCommData.AdapterList;
	while (adapter) {
		NotifyAdapter(adapter, HostShutdown);
		adapter = adapter->NextAdapter;
	}
}	

void FsaCommLogEvent(PFIB_CONTEXT fc, PDEVICE_OBJECT DeviceObject,
		AAC_STATUS FsaStatus, AAC_STATUS AacStatus,
		u32 LocationCode, u16 Category,
		unsigned char * String, int DumpFib)
{
}

AfaCommProbeDisks(PAFA_COMM_ADAPTER adapter)
{
	PMNTINFO DiskInfo;
	PMNTINFORESPONSE DiskInfoResponse;
	AAC_STATUS status;
	PCOMM_FIB_CONTEXT fc;
    
	fc = AllocateFib( adapter );
	InitializeFib( fc );

	DiskInfo = (PMNTINFO) fc->Fib->data;
	DiskInfo->Command = VM_NameServe;
	DiskInfo->MntCount = 0;
	DiskInfo->MntType = FT_FILESYS;

	status = SendFib(ContainerCommand,
			fc,
	                sizeof(MNTINFO),
	                FsaNormal,
	                TRUE,
	                NULL,
	                TRUE,
	                NULL,
	                NULL);

	DiskInfoResponse = (PMNTINFORESPONSE) fc->Fib->data;

	if (DiskInfoResponse->MntRespCount) {
		cmn_err(CE_CONT, "container found on adapter, size = 0x%x blocks\n", 
				DiskInfoResponse->MntTable[0].Capacity);
	} else {
		cmn_err(CE_CONT, "no containers found on adapter\n");
	}
	CompleteFib( fc );
	FreeFib( fc );				 
}


