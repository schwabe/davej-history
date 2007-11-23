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
 *  commctrl.c
 *
 * Abstract: Contains all routines for control of the AFA comm layer
 *
--*/

#include "comprocs.h"
#include "osheaders.h"
#include "ostypes.h"

#include <revision.h>

/*++

Routine Description:
	This routine validates the revision of the caller with the current revision
	of the filesystem.

Arguments:
	adapter - Supplies which adapter is being processed.
	Irp - Supplies the Irp being processed.
	IrpContext - Supplies the IrpContext.

Return Value:
	AAC_STATUS

--*/

AAC_STATUS FsaCtlCheckRevision(PAFA_COMM_ADAPTER adapter, PAFA_IOCTL_CMD IoctlCmdPtr)
{
	RevCheck APIRevCheck;
	RevCheckResp APIRevCheckResp;
	RevComponent APICallingComponent;
	u32 APIBuildNumber;

	if (COPYIN((caddr_t) IoctlCmdPtr->arg, (caddr_t) & APIRevCheck, sizeof(RevCheck), IoctlCmdPtr->flag)) {
		return EFAULT;
	}

	APICallingComponent = APIRevCheck.callingComponent;
	APIBuildNumber = APIRevCheck.callingRevision.buildNumber;
	APIRevCheckResp.possiblyCompatible = RevCheckCompatibility(RevMiniportDriver, APICallingComponent, APIBuildNumber);
	APIRevCheckResp.adapterSWRevision.external.ul = RevGetExternalRev();
	APIRevCheckResp.adapterSWRevision.buildNumber = RevGetBuildNumber();

	if (COPYOUT((caddr_t) & APIRevCheckResp, (caddr_t) IoctlCmdPtr->arg, sizeof(RevCheckResp), IoctlCmdPtr->flag)) {
		return EFAULT;
	}
	return 0;
}


int AfaCommAdapterDeviceControl(void *arg, PAFA_IOCTL_CMD IoctlCmdPtr)
{
	PAFA_COMM_ADAPTER adapter = (PAFA_COMM_ADAPTER) arg;
	int status = ENOTTY;
	PAFA_CLASS_DRIVER dev;

	//
	// First loop through all of the class drivers to give them a chance to handle
	// the Device control first.
	//

	dev = adapter->ClassDriverList;

	while (dev) {
		if (dev->DeviceControl) {
			if (dev->DeviceControl(dev->ClassDriverExtension, IoctlCmdPtr, &status)) {
				return (status);
			}
		}
		dev = dev->Next;
	}

	switch (IoctlCmdPtr->cmd) {
	case FSACTL_SENDFIB:
		status = AfaCommCtlSendFib(adapter, IoctlCmdPtr);
		break;

	case FSACTL_AIF_THREAD:
		status = AfaCommCtlAifThread(adapter, IoctlCmdPtr);
		break;

	case FSACTL_OPEN_GET_ADAPTER_FIB:
		status = FsaCtlOpenGetAdapterFib(adapter, IoctlCmdPtr);
		break;

	case FSACTL_GET_NEXT_ADAPTER_FIB:
		status = FsaCtlGetNextAdapterFib(adapter, IoctlCmdPtr);
		break;

	case FSACTL_CLOSE_GET_ADAPTER_FIB:
		status = FsaCtlCloseGetAdapterFib(adapter, IoctlCmdPtr);
		break;

	case FSACTL_MINIPORT_REV_CHECK:
		status = FsaCtlCheckRevision(adapter, IoctlCmdPtr);
		break;

	default:
		status = ENOTTY;
		break;
	}
	return status;
}

/*++

Routine Description:
	This routine registers a new class driver for the comm layer.
	It will return a pointer to the communication functions for the class driver
	to use.

Arguments:
	adapter - Supplies which adapter is being processed.
	Irp - Supplies the Irp being processed.

Return Value:
	STATUS_SUCCESS		 - Everything OK.

--*/

AAC_STATUS AfaCommRegisterNewClassDriver(PAFA_COMM_ADAPTER adapter,
	      PAFA_NEW_CLASS_DRIVER NewClassDriver, PAFA_NEW_CLASS_DRIVER_RESPONSE NewClassDriverResponse)
{
	AAC_STATUS status;
	PAFA_CLASS_DRIVER dev;

	dev = (PAFA_CLASS_DRIVER)kmalloc(sizeof(AFA_CLASS_DRIVER), GFP_KERNEL);

	if (dev == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		return status;
	}
	//
	// If the class driver has sent in user Vars, then copy them into the global
	// area.
	//

	if (NewClassDriver->NumUserVars) {
		PFSA_USER_VAR NewUserVars;
		NewUserVars = kmalloc((FsaCommData.NumUserVars +
					     NewClassDriver->NumUserVars) * sizeof(FSA_USER_VAR), GFP_KERNEL);

		//
		// First copy the existing into the new area.
		//

		RtlCopyMemory(NewUserVars, FsaCommData.UserVars, FsaCommData.NumUserVars * sizeof(FSA_USER_VAR));

		//
		// Next copy the new vars passed in from class driver.
		//

		RtlCopyMemory((NewUserVars + FsaCommData.NumUserVars),
			      NewClassDriver->UserVars, NewClassDriver->NumUserVars * sizeof(FSA_USER_VAR));

		//
		// Free up the old user vars.
		//

		kfree(FsaCommData.UserVars);

		//
		// Point the global to the new area.
		//

		FsaCommData.UserVars = NewUserVars;

		//
		// Update the total count.
		//

		FsaCommData.NumUserVars += NewClassDriver->NumUserVars;
	}

	dev->OpenAdapter = NewClassDriver->OpenAdapter;
	dev->CloseAdapter = NewClassDriver->CloseAdapter;
	dev->DeviceControl = NewClassDriver->DeviceControl;
	dev->HandleAif = NewClassDriver->HandleAif;
	dev->ClassDriverExtension = NewClassDriver->ClassDriverExtension;

	dev->Next = adapter->ClassDriverList;
	adapter->ClassDriverList = dev;

	//
	// Now return the information needed by the class driver to communicate to us.
	//

	NewClassDriverResponse->CommFuncs = &adapter->CommFuncs;
	NewClassDriverResponse->CommPortExtension = adapter;
	NewClassDriverResponse->MiniPortExtension = adapter->AdapterExtension;
	NewClassDriverResponse->SpinLockCookie = adapter->SpinLockCookie;
	NewClassDriverResponse->Dip = adapter->Dip;

	return (STATUS_SUCCESS);
}

/*++

Routine Description:
	This routine sends a fib to the adapter on behalf of a user level
	program.

Arguments:
	adapter - Supplies which adapter is being processed.
	IoctlCmdPtr - Pointer to the arguments to the IOCTL call

Return Value:
	STATUS_INVALID_PARAMETER - If the AdapterFibContext was not a valid pointer.
	STATUS_INSUFFICIENT_RESOURCES - If a memory allocation failed.
	STATUS_SUCCESS		 - Everything OK.

--*/

int AfaCommCtlSendFib(PAFA_COMM_ADAPTER adapter, PAFA_IOCTL_CMD IoctlCmdPtr)
{
	PFIB KFib;
	PCOMM_FIB_CONTEXT fc;
	PSGMAP_CONTEXT SgMapContext;
	SGMAP_CONTEXT _SgMapContext;
	QUEUE_TYPES WhichQueue;
	void *UsersAddress;
	AAC_STATUS status;

	fc = AllocateFib(adapter);

	KFib = fc->Fib;

	//
	// First copy in the header so that we can check the size field.
	//

	if (COPYIN((caddr_t) IoctlCmdPtr->arg, (caddr_t) KFib, sizeof(FIB_HEADER), IoctlCmdPtr->flag)) {
		FreeFib(fc);
		status = EFAULT;
		return (status);
	}
	//
	//      Since we copy based on the fib header size, make sure that we
	//      will not overrun the buffer when we copy the memory. Return
	//      an error if we would.
	//

	if (KFib->Header.Size > sizeof(FIB) - sizeof(FIB_HEADER)) {
		FreeFib(fc);
		status = EINVAL;
		return status;
	}

	if (COPYIN((caddr_t) IoctlCmdPtr->arg, (caddr_t) KFib, KFib->Header.Size + sizeof(FIB_HEADER), IoctlCmdPtr->flag)) {
		FreeFib(fc);
		status = EFAULT;
		return (status);
	}

	WhichQueue = AdapNormCmdQueue;

	if (KFib->Header.Command == TakeABreakPt) {
		InterruptAdapter(adapter);
		//
		// Since we didn't really send a fib, zero out the state to allow 
		// cleanup code not to assert.
		//
		KFib->Header.XferState = 0;
	} else {
		if (SendFib(KFib->Header.Command, fc, KFib->Header.Size,
		     FsaNormal, TRUE, NULL, TRUE, NULL, NULL) != FSA_SUCCESS) {
			FsaCommPrint("User SendFib failed!.\n");
			FreeFib(fc);
			return (ENXIO);
		}
		if (CompleteFib(fc) != FSA_SUCCESS) {
			FsaCommPrint("User Complete FIB failed.\n");
			FreeFib(fc);
			return (ENXIO);
		}
	}
	//
	//      Make sure that the size returned by the adapter (which includes
	//      the header) is less than or equal to the size of a fib, so we
	//      don't corrupt application data. Then copy that size to the user
	//      buffer. (Don't try to add the header information again, since it
	//      was already included by the adapter.)
	//
	ASSERT(KFib->Header.Size <= sizeof(FIB));
	if (COPYOUT((caddr_t) KFib, (caddr_t) IoctlCmdPtr->arg, KFib->Header.Size, IoctlCmdPtr->flag)) {
		FreeFib(fc);
		status = EFAULT;
		return (status);
	}
	FreeFib(fc);
	return (0);
}

/*++

Routine Description:
	This routine will act as the AIF thread for this adapter.

Arguments:
	adapter - Supplies which adapter is being processed.
	IoctlCmdPtr - Pointer to the arguments to the IOCTL call

Return Value:
	STATUS_INVALID_PARAMETER - If the AdapterFibContext was not a valid pointer.
	STATUS_INSUFFICIENT_RESOURCES - If a memory allocation failed.
	STATUS_SUCCESS		 - Everything OK.

--*/

int AfaCommCtlAifThread(PAFA_COMM_ADAPTER adapter, PAFA_IOCTL_CMD IoctlCmdPtr)
{
	return (NormCommandThread(adapter));
}



#ifdef GATHER_FIB_TIMES
/*++

Routine Description:
	This routine returns the gathered fibtimes to the user.

Arguments:
	adapter - Supplies which adapter is being processed.
	Irp - Supplies the Irp being processed.

Return Value:
	STATUS_INVALID_PARAMETER - If the AdapterFibContext was not a valid pointer.
	STATUS_INSUFFICIENT_RESOURCES - If a memory allocation failed.
	STATUS_SUCCESS		 - Everything OK.

--*/

AAC_STATUS AfaCommGetFibTimes(PAFA_COMM_ADAPTER adapter, PIRP Irp)
{
	PALL_FIB_TIMES AllFibTimes;
	PLARGE_INTEGER FreqPtr;
	PIO_STACK_LOCATION IrpSp;

	//
	//  Get a pointer to the current Irp stack location
	//

	IrpSp = IoGetCurrentIrpStackLocation(Irp);
	FreqPtr = (PLARGE_INTEGER) IrpSp->Parameters.FileSystemControl.Type3InputBuffer;
	*FreqPtr = adapter->FibTimesFrequency;
	AllFibTimes = (PALL_FIB_TIMES) ((PUCHAR) FreqPtr + sizeof(LARGE_INTEGER));
	RtlCopyMemory(AllFibTimes, adapter->FibTimes, sizeof(ALL_FIB_TIMES));
	Irp->IoStatus.Information = 0;
	return (STATUS_SUCCESS);

}

/*++

Routine Description:
	This routine zero's the FibTimes structure within the adapter structure.

Arguments:
	adapter - Supplies which adapter is being processed.
	Irp - Supplies the Irp being processed.

Return Value:
	STATUS_INVALID_PARAMETER - If the AdapterFibContext was not a valid pointer.
	STATUS_INSUFFICIENT_RESOURCES - If a memory allocation failed.
	STATUS_SUCCESS		 - Everything OK.
--*/

AAC_STATUS AfaCommZeroFibTimes(PAFA_COMM_ADAPTER adapter, PIRP Irp)
{
	PFIB_TIMES FibTimesPtr;
	int i;
	PIO_STACK_LOCATION IrpSp;

	//
	//  Get a pointer to the current Irp stack location
	//

	IrpSp = IoGetCurrentIrpStackLocation(Irp);

	//
	// Initialize the Fib timing data structures
	//
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

	Irp->IoStatus.Information = 0;

	return (STATUS_SUCCESS);
}
#endif				// GATHER_FIB_TIMES

#ifndef unix_aif

/*++

Routine Description:
	This routine will get the next Fib, if available, from the AdapterFibContext
	passed in from the user.

Arguments:
	adapter - Supplies which adapter is being processed.
	Irp - Supplies the Irp being processed.

Return Value:
	STATUS_INVALID_PARAMETER - If the AdapterFibContext was not a valid pointer.
	STATUS_INSUFFICIENT_RESOURCES - If a memory allocation failed.
	STATUS_SUCCESS		 - Everything OK.

--*/

int FsaCtlOpenGetAdapterFib(PAFA_COMM_ADAPTER adapter, PAFA_IOCTL_CMD IoctlCmdPtr)
{
	PGET_ADAPTER_FIB_CONTEXT afc;
	int status;

	//
	// The context must be allocated from NonPagedPool because we need to use MmIsAddressValid.
	//

	afc = kmalloc(sizeof(GET_ADAPTER_FIB_CONTEXT), GFP_KERNEL);
	if (afc == NULL) {
		status = ENOMEM;
	} else {
		afc->NodeTypeCode = FSAFS_NTC_GET_ADAPTER_FIB_CONTEXT;
		afc->NodeByteSize = sizeof(GET_ADAPTER_FIB_CONTEXT);

		//
		// Initialize the conditional variable use to wait for the next AIF.
		//

		OsCv_init(&afc->UserEvent);

		//
		// Set WaitingForFib to FALSE to indicate we are not in a WaitForSingleObject
		//

		afc->WaitingForFib = FALSE;

		//
		// Initialize the FibList and set the count of fibs on the list to 0.
		//

		afc->FibCount = 0;
		InitializeListHead(&afc->FibList);

		//
		// Now add this context onto the adapter's AdapterFibContext list.
		//

		OsCvLockAcquire(adapter->AdapterFibMutex);
		InsertTailList(&adapter->AdapterFibContextList, &afc->NextContext);
		OsCvLockRelease(adapter->AdapterFibMutex);

		if (COPYOUT(&afc, (caddr_t) IoctlCmdPtr->arg, sizeof(PGET_ADAPTER_FIB_CONTEXT), IoctlCmdPtr->flag)) {
			status = EFAULT;
		} else {
			status = 0;
		}
	}
	return (status);
}

/*++

Routine Description:
	This routine will get the next Fib, if available, from the AdapterFibContext
	passed in from the user.

Arguments:
	adapter - Supplies which adapter is being processed.
	Irp - Supplies the Irp being processed.

Return Value:
	STATUS_INVALID_PARAMETER - If the AdapterFibContext was not a valid pointer.
	STATUS_NO_MORE_ENTRIES - There are no more Fibs for this AdapterFibContext.
	STATUS_SUCCESS		 - Everything OK.

--*/

int FsaCtlGetNextAdapterFib(PAFA_COMM_ADAPTER adapter, PAFA_IOCTL_CMD IoctlCmdPtr)
{
	GET_ADAPTER_FIB_IOCTL AdapterFibIoctl;
	PGET_ADAPTER_FIB_CONTEXT afc;
	PFIB Fib;
	int status;

	if (COPYIN((caddr_t) IoctlCmdPtr->arg, (caddr_t) & AdapterFibIoctl, sizeof(GET_ADAPTER_FIB_IOCTL), IoctlCmdPtr->flag)) {
		return (EFAULT);
	}
	//
	// Extract the AdapterFibContext from the Input parameters.
	//

	afc = (PGET_ADAPTER_FIB_CONTEXT) AdapterFibIoctl.AdapterFibContext;

	//
	// Verify that the HANDLE passed in was a valid AdapterFibContext
	//
	// rpbfix : determine if there is a way to validate the AdapterFibContext address.
	//

	if ((afc->NodeTypeCode != FSAFS_NTC_GET_ADAPTER_FIB_CONTEXT)
	    || (afc->NodeByteSize != sizeof(GET_ADAPTER_FIB_CONTEXT))) {
		return (EINVAL);

	}

	status = STATUS_SUCCESS;
	OsCvLockAcquire(adapter->AdapterFibMutex);
	//
	// If there are no fibs to send back, then either wait or return EAGAIN
	//
return_fib:

	if (!IsListEmpty(&afc->FibList)) {
		PLIST_ENTRY entry;
		//
		// Pull the next fib from the FibList
		//
		entry = RemoveHeadList(&afc->FibList);
		Fib = CONTAINING_RECORD(entry, FIB, Header.FibLinks);
		afc->FibCount--;
		if (COPYOUT(Fib, AdapterFibIoctl.AifFib, sizeof(FIB), IoctlCmdPtr->flag)) {
			OsCvLockRelease(adapter->AdapterFibMutex);
			kfree(Fib);
			return (EFAULT);

		}
		//
		// Free the space occupied by this copy of the fib.
		//

		kfree(Fib);
		status = 0;
	} else {
		if (AdapterFibIoctl.Wait) {
			if (OsCv_wait_sig(&afc->UserEvent, adapter->AdapterFibMutex) == 0) {
				status = EINTR;
			} else {
				goto return_fib;
			}
		} else {
			status = EAGAIN;
		}
	}
	OsCvLockRelease(adapter->AdapterFibMutex);
	return (status);
}

/*++

Routine Description:
	This routine will close down the AdapterFibContext passed in from the user.

Arguments:
	adapter - Supplies which adapter is being processed.
	Irp - Supplies the Irp being processed.

Return Value:
	STATUS_INVALID_PARAMETER - If the AdapterFibContext was not a valid pointer.
	STATUS_SUCCESS		 - Everything OK.

--*/

int FsaCtlCloseGetAdapterFib(PAFA_COMM_ADAPTER adapter, PAFA_IOCTL_CMD IoctlCmdPtr)
{
	PGET_ADAPTER_FIB_CONTEXT afc;
	AAC_STATUS status;

	//
	// Extract the AdapterFibContext from the Input parameters
	//

	afc = (PGET_ADAPTER_FIB_CONTEXT) IoctlCmdPtr->arg;

	if (afc == 0) {
		cmn_err(CE_WARN, "FsaCtlCloseGetAdapterFib: AdapterFibContext is NULL");
		return (EINVAL);
	}
	//
	// Verify that the HANDLE passed in was a valid AdapterFibContext
	//
	//      rpbfix : verify pointer sent in from user.
	//

	if ((afc->NodeTypeCode != FSAFS_NTC_GET_ADAPTER_FIB_CONTEXT)
	    || (afc->NodeByteSize != sizeof(GET_ADAPTER_FIB_CONTEXT))) {
		return (EINVAL);
	}

	OsCvLockAcquire(adapter->AdapterFibMutex);
	status = FsaCloseAdapterFibContext(adapter, afc);
	OsCvLockRelease(adapter->AdapterFibMutex);

	return (status);
}

int FsaCloseAdapterFibContext(PAFA_COMM_ADAPTER adapter, PGET_ADAPTER_FIB_CONTEXT afc)
{
	int status;
	PFIB Fib;

	//
	// First free any FIBs that have not been consumed yet.
	//

	while (!IsListEmpty(&afc->FibList)) {
		PLIST_ENTRY entry;
		//
		// Pull the next fib from the FibList
		//
		entry = RemoveHeadList(&afc->FibList);
		Fib = CONTAINING_RECORD(entry, FIB, Header.FibLinks);
		afc->FibCount--;
		//
		// Free the space occupied by this copy of the fib.
		//
		kfree(Fib);
	}
	//
	// Remove the Context from the AdapterFibContext List
	//
	RemoveEntryList(&afc->NextContext);
	OsCv_destroy(&afc->UserEvent);
	//
	// Invalidate context
	//
	afc->NodeTypeCode = 0;
	//
	// Free the space occupied by the Context
	//
	kfree(afc);
	status = STATUS_SUCCESS;
	return status;
}
#endif

/*++

Routine Description:
	The routine will get called by the miniport each time a user issues a CreateFile on the DeviceObject
	for the adapter.

	The main purpose of this routine is to set up any data structures that may be needed
	to handle any requests made on this DeviceObject.

Arguments:
	adapter - Pointer to which adapter miniport was opened.

Return Value:
	STATUS_SUCCESS

--*/

AAC_STATUS AfaCommOpenAdapter(void *arg)
{
	PAFA_COMM_ADAPTER adapter = (PAFA_COMM_ADAPTER) arg;
	AAC_STATUS status = STATUS_SUCCESS;
	PAFA_CLASS_DRIVER dev;

	dev = adapter->ClassDriverList;

	while (dev) {
		if (dev->OpenAdapter) {
			status = dev->OpenAdapter(dev->ClassDriverExtension);
			if (status != STATUS_SUCCESS)
				break;
		}
		dev = dev->Next;
	}
	return (status);
}

/*++

Routine Description:
	This routine will get called by the miniport each time a user issues a CloseHandle on the DeviceObject
	for the adapter.

	The main purpose of this routine is to cleanup any data structures that have been set up
	while this FileObject has been opened.

	This routine loops through all of the AdapterFibContext structures to determine if any need
	to be deleted for this FileObject.

Arguments:
	adapter - Pointer to adapter miniport
	Irp - Pointer to Irp that caused this close

Return Value:
	Status value returned from File system driver AdapterClose

--*/

AAC_STATUS AfaCommCloseAdapter(void *arg)
{
	PAFA_COMM_ADAPTER adapter = (PAFA_COMM_ADAPTER) arg;
	PLIST_ENTRY entry, next;
	PGET_ADAPTER_FIB_CONTEXT afc;
	AAC_STATUS status = STATUS_SUCCESS;
	PAFA_CLASS_DRIVER dev;

	OsCvLockAcquire(adapter->AdapterFibMutex);
	entry = adapter->AdapterFibContextList.Flink;

	//
	// Loop through all of the AdapterFibContext, looking for any that
	// were created with the FileObject that is being closed.
	//
	while (entry != &adapter->AdapterFibContextList) {
		//
		// Extract the AdapterFibContext
		//
		afc = CONTAINING_RECORD(entry, GET_ADAPTER_FIB_CONTEXT, NextContext);
		// 
		// Save the next entry because CloseAdapterFibContext will delete the AdapterFibContext
		//
		next = entry->Flink;
		entry = next;
	}

#ifdef unix_config_file
	//
	// If this FileObject had the adapter open for configuration, then release it.
	//
	if (adapter->AdapterConfigFileObject == IrpSp->FileObject) {

		adapter->AdapterConfigFileObject = NULL;

	}
#endif

	OsCvLockRelease(adapter->AdapterFibMutex);
	dev = adapter->ClassDriverList;
	while (dev) {
		if (dev->CloseAdapter) {
			status = dev->CloseAdapter(dev->ClassDriverExtension);
			if (status != STATUS_SUCCESS)
				break;
		}
		dev = dev->Next;
	}
	return status;
}
