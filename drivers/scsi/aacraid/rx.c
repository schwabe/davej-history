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
 *  rx.c
 *
 * Abstract: Hardware miniport for Drawbridge specific hardware functions.
 *
 --*/

#include "osheaders.h"


#include "AacGenericTypes.h"

#include "aac_unix_defs.h"

#include "fsatypes.h"
#include "comstruc.h"
#include "fsact.h"
#include "protocol.h"

#define DEFINE_PCI_IDS
#include "rxcommon.h"
#include "monkerapi.h"

#include "fsaport.h"
#include "fsaioctl.h"

#include "pcisup.h"
#include "rx.h"

#include "port.h"

#define BugCheckFileId                   (FSAFS_BUG_CHECK_CYCLONESUP)

// #define RxBugCheck(A,B,C) { KeBugCheckEx(0x00000AFA, __LINE__, (u32)A, (u32)B,(u32)C ); }

#define RxBugCheck(A, B, C) {	cmn_err(CE_PANIC, "aacdisk : line %s, 0x%x, 0x%x, 0x%x ", __LINE__, A, B, C);  }

#define NUM_TICKS_PER_SECOND (1000 * 1000 * 10) /* time is in 100 nanoseconds */


//
// The list of all the Rx adapter structures
//

PRx_ADAPTER_EXTENSION	RxAdapterList;

int RxInitDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, u32 AdapterNumber, u32 PciBus, u32 PciSlot);
int RxSendSynchFib(void * Arg1, u32 FibPhysicalAddress);

FSA_USER_VAR	RxUserVars[] = {
	{ "AfaPortPrinting", (u32 *)&AfaPortPrinting, NULL },
};


//
// Declare private use routines for this modual
//


/*++

Routine Description:
    The Isr routine for fsa Rx based adapter boards.

Arguments:

Return Value:
	TRUE - if the interrupt was handled by this isr
	FALSE - if the interrupt was not handled by this isr

--*/

u_int RxPciIsr (PRx_ADAPTER_EXTENSION AdapterExtension)
{
	u32	DoorbellBits;
	u8	InterruptStatus, Mask;
	u_int OurInterrupt = INTR_UNCLAIMED;

	//cmn_err(CE_WARN, "RxPciIsr entered\n");

	InterruptStatus = Rx_READ_UCHAR(AdapterExtension, MUnit.OISR);

	//
	// Read mask and invert because drawbridge is reversed.
	//
	// This allows us to only service interrupts that have been enabled.
	//

	Mask = ~(Rx_READ_UCHAR(AdapterExtension, MUnit.OIMR));

	// Check to see if this is our interrupt.  If it isn't just return FALSE.

	if (InterruptStatus & Mask) {
		DoorbellBits = Rx_READ_ULONG(AdapterExtension, OutboundDoorbellReg);
		OurInterrupt = INTR_CLAIMED;
		if (DoorbellBits & DoorBellPrintfReady) {
			cmn_err(CE_CONT, "%s:%s", OsGetDeviceName(AdapterExtension), AdapterExtension->Common->PrintfBufferAddress);
			Rx_WRITE_ULONG(AdapterExtension, MUnit.ODR,DoorBellPrintfReady); //clear PrintfReady
			Rx_WRITE_ULONG(AdapterExtension, InboundDoorbellReg,DoorBellPrintfDone);
		} else if (DoorbellBits & DoorBellAdapterNormCmdReady) {	// Adapter -> Host Normal Command Ready
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, HostNormCmdQue);
			Rx_WRITE_ULONG(AdapterExtension, MUnit.ODR, DoorBellAdapterNormCmdReady);
		} else if (DoorbellBits & DoorBellAdapterNormRespReady) {	// Adapter -> Host Normal Response Ready
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, HostNormRespQue);
			Rx_WRITE_ULONG(AdapterExtension, MUnit.ODR,DoorBellAdapterNormRespReady);
		} else if (DoorbellBits & DoorBellAdapterNormCmdNotFull) {	// Adapter -> Host Normal Command Not Full
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, AdapNormCmdNotFull);
			Rx_WRITE_ULONG(AdapterExtension, MUnit.ODR, DoorBellAdapterNormCmdNotFull);
		} else if (DoorbellBits & DoorBellAdapterNormRespNotFull) {	// Adapter -> Host Normal Response Not Full
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, AdapNormRespNotFull);
			Rx_WRITE_ULONG(AdapterExtension, MUnit.ODR, DoorBellAdapterNormRespNotFull);
		}
	}
	return(OurInterrupt);
}

/*++

Routine Description:
	This routine will enable the corresponding adapter event to cause an interrupt on 
	the host.

Arguments:
	AdapterExtension - Which adapter to enable.
	AdapterEvent - Which adapter event.
	AtDeviceIrq - Whether the system is in DEVICE irql

Return Value:
	Nothing.

--*/

void RxEnableInterrupt(void *Arg1, ADAPTER_EVENT AdapterEvent, int AtDeviceIrq)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PRx_ADAPTER_EXTENSION AdapterExtension = (PRx_ADAPTER_EXTENSION) CommonExtension->MiniPort;

	//cmn_err(CE_WARN, "RxEnableInterrupt");
	switch (AdapterEvent) {
		case HostNormCmdQue:
			AdapterExtension->LocalMaskInterruptControl &= ~(OUTBOUNDDOORBELL_1);
			break;
		case HostNormRespQue:
			AdapterExtension->LocalMaskInterruptControl &= ~(OUTBOUNDDOORBELL_2);
			break;
		case AdapNormCmdNotFull:
			AdapterExtension->LocalMaskInterruptControl &= ~(OUTBOUNDDOORBELL_3);
			break;
		case AdapNormRespNotFull:
			AdapterExtension->LocalMaskInterruptControl &= ~(OUTBOUNDDOORBELL_4);
			break;
	}
}

/*++

Routine Description:

	This routine will disable the corresponding adapter event to cause an interrupt on 
	the host.

Arguments:

	AdapterExtension - Which adapter to enable.

	AdapterEvent - Which adapter event.

	AtDeviceIrq - Whether the system is in DEVICE irql

Return Value:

    Nothing.

--*/

void RxDisableInterrupt(void *Arg1, ADAPTER_EVENT AdapterEvent, int AtDeviceIrq)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PRx_ADAPTER_EXTENSION AdapterExtension = (PRx_ADAPTER_EXTENSION) CommonExtension->MiniPort;

	//cmn_err(CE_WARN, "RxEnableInterrupt");

	switch (AdapterEvent) 
	{
		case HostNormCmdQue:
			AdapterExtension->LocalMaskInterruptControl |= (OUTBOUNDDOORBELL_1);
			break;
		case HostNormRespQue:
			AdapterExtension->LocalMaskInterruptControl |= (OUTBOUNDDOORBELL_2);
			break;
		case AdapNormCmdNotFull:
			AdapterExtension->LocalMaskInterruptControl |= (OUTBOUNDDOORBELL_3);
			break;
		case AdapNormRespNotFull:
			AdapterExtension->LocalMaskInterruptControl |= (OUTBOUNDDOORBELL_4);
			break;
	}
}



void RxDetachDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension)
{
	PRx_ADAPTER_EXTENSION AdapterExtension = CommonExtension->MiniPort;

	//
	// Free the register mapping.
	//

	OsDetachDevice( AdapterExtension);
	kfree(AdapterExtension);
}


/*++

Routine Description:
	Scans the PCI bus looking for the Rx card. When found all resources for the
	device will be allocated and the interrupt vectors and csrs will be allocated and
	mapped.

 	The device_interface in the commregion will be allocated and linked to the comm region.

Arguments:

Return Value:
    TRUE - if the device was setup with not problems
    FALSE - if the device could not be mapped and init successfully

--*/

int RxInitDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, u32 AdapterNumber, u32 PciBus, u32 PciSlot)
{
	AAC_STATUS Status;
	PRx_ADAPTER_EXTENSION AdapterExtension = NULL;
	FSA_NEW_ADAPTER NewAdapter;
	u32 StartTime, EndTime, WaitTime;
	u32 InitStatus;
	int instance;
	int nIntrs;
	char * name;

	AfaPortPrint("In init device.\n");

	//cmn_err(CE_WARN, "In RxInitDevice");

//	AdapterExtension->Common->AdapterIndex = AdapterIndex;
	CommonExtension->AdapterNumber = AdapterNumber;

	CommonExtension->PciBusNumber = PciBus;
	CommonExtension->PciSlotNumber = PciSlot;

	AdapterExtension = kmalloc(sizeof(Rx_ADAPTER_EXTENSION), GFP_KERNEL);
	AdapterExtension->Common = CommonExtension;
	CommonExtension->MiniPort = AdapterExtension;

	instance = OsGetDeviceInstance(AdapterExtension);
	name     = OsGetDeviceName(AdapterExtension);
	//
	// Map in the registers from the adapter, register space 0 is config space,
	// register space 1 is the memery space.
	//

	if (OsMapDeviceRegisters(AdapterExtension)) {
		cmn_err(CE_CONT, "%s%d: can't map device registers\n",
				OsGetDeviceName(AdapterExtension), instance);
		return(FAILURE);
	}

	//
	// Check to see if the board failed any self tests.
	//

	if (Rx_READ_ULONG( AdapterExtension, IndexRegs.Mailbox[7]) & SELF_TEST_FAILED) {
		cmn_err(CE_CONT, "%s%d: adapter self-test failed\n",
				OsGetDeviceName(AdapterExtension), instance);
		return(FAILURE);
	}
	//cmn_err(CE_WARN, "RxInitDevice: %s%d: adapter passwd self-test\n",
	//			OsGetDeviceName(AdapterExtension), instance);

	//
	// Check to see if the board panic'd while booting.
	//

	if (Rx_READ_ULONG( AdapterExtension, IndexRegs.Mailbox[7]) & KERNEL_PANIC) {
		cmn_err(CE_CONT, "%s%d: adapter kernel panic'd\n",
				OsGetDeviceName(AdapterExtension), instance);
		return(FAILURE);
	}

	StartTime = jiffies/HZ;
	WaitTime = 0;

	//
	//  Wait for the adapter to be up and running. Wait up until 3 minutes.
	//

	while (!(Rx_READ_ULONG( AdapterExtension, IndexRegs.Mailbox[7]) & KERNEL_UP_AND_RUNNING)) {
		EndTime = jiffies/HZ;
		WaitTime = EndTime - StartTime;
		if ( WaitTime > (3 * 10) ) {
			InitStatus = Rx_READ_ULONG( AdapterExtension, IndexRegs.Mailbox[7]) >> 16;
			cmn_err(CE_CONT, "%s%d: adapter kernel failed to start, init status = %d\n",
					OsGetDeviceName(AdapterExtension), instance, InitStatus);
			return(FAILURE);
		}
	}

	if (OsAttachInterrupt(AdapterExtension,RxISR)) {
		cmn_err(CE_WARN, "%s%d RxInitDevice: failed OsAttachIntterupt", name, instance);
		return(FAILURE);
	}

	//
	// Fill in the function dispatch table.
	//

	AdapterExtension->Common->AdapterFuncs.SizeOfFsaPortFuncs = sizeof(FSAPORT_FUNCS);
	AdapterExtension->Common->AdapterFuncs.AllocateAdapterCommArea = AfaPortAllocateAdapterCommArea;
	AdapterExtension->Common->AdapterFuncs.FreeAdapterCommArea = AfaPortFreeAdapterCommArea;
	AdapterExtension->Common->AdapterFuncs.AllocateAndMapFibSpace = AfaPortAllocateAndMapFibSpace;
	AdapterExtension->Common->AdapterFuncs.UnmapAndFreeFibSpace = AfaPortUnmapAndFreeFibSpace;
	AdapterExtension->Common->AdapterFuncs.InterruptAdapter = RxInterruptAdapter;
	AdapterExtension->Common->AdapterFuncs.EnableInterrupt = RxEnableInterrupt;
	AdapterExtension->Common->AdapterFuncs.DisableInterrupt = RxDisableInterrupt;
	AdapterExtension->Common->AdapterFuncs.NotifyAdapter = RxNotifyAdapter;
	AdapterExtension->Common->AdapterFuncs.ResetDevice = RxResetDevice;
	AdapterExtension->Common->AdapterFuncs.InterruptHost = NULL;
	AdapterExtension->Common->AdapterFuncs.SendSynchFib = RxSendSynchFib;

	NewAdapter.AdapterExtension = CommonExtension;
	NewAdapter.AdapterFuncs = &AdapterExtension->Common->AdapterFuncs;
	NewAdapter.AdapterInterruptsBelowDpc = FALSE;
	NewAdapter.AdapterUserVars = RxUserVars;
	NewAdapter.AdapterUserVarsSize = sizeof(RxUserVars) / sizeof(FSA_USER_VAR);

	NewAdapter.Dip = CommonExtension->OsDep.dip;
	
	if (AfaCommInitNewAdapter( &NewAdapter ) == NULL) {
		cmn_err(CE_WARN, "AfaCommInitNewAdapter failed\n");
		return (FAILURE);
	}

	AdapterExtension->Common->Adapter = NewAdapter.Adapter;

	if (AdapterExtension->Common->Adapter == NULL) {
		AfaPortLogError(AdapterExtension->Common, FAILURE, NULL, 0);
		cmn_err(CE_WARN, "%s%d RxInitDevice: No Adapter pointer", name, instance);
		return (FAILURE);
	}

	//
	// Start any kernel threads needed
	//
	OsStartKernelThreads(AdapterExtension);

	//
	// Tell the adapter that all is configure, and it can start accepting requests
	//

	RxStartAdapter(AdapterExtension);

	//
	// Put this adapter into the list of Rx adapters
	//

	AdapterExtension->Next = RxAdapterList;
	RxAdapterList = AdapterExtension;

	AdapterExtension->Common->AdapterConfigured = TRUE;

#ifdef AACDISK
	//
	// Call the disk layer to initialize itself.
	//

	AfaDiskInitNewAdapter( AdapterExtension->Common->AdapterNumber, AdapterExtension->Common->Adapter );
#endif

init_done:
	AdapterExtension->Common->AdapterPrintfsToScreen = FALSE;
	return(0);
}

void RxStartAdapter(PRx_ADAPTER_EXTENSION AdapterExtension)
{
	u32 ReturnStatus;
	LARGE_INTEGER HostTime;
	u32 ElapsedSeconds;
	PADAPTER_INIT_STRUCT InitStruct;

	//cmn_err(CE_WARN, "RxStartAdapter");
	//
	// Fill in the remaining pieces of the InitStruct.
	//

	InitStruct = AdapterExtension->Common->InitStruct;
	InitStruct->HostPhysMemPages = AfaPortGetMaxPhysicalPage(AdapterExtension->Common);
	ElapsedSeconds = jiffies/HZ;
	InitStruct->HostElapsedSeconds = ElapsedSeconds;

	//
	// Tell the adapter we are back and up and running so it will scan its command
	// queues and enable our interrupts
	//

	AdapterExtension->LocalMaskInterruptControl =
		(DoorBellPrintfReady | OUTBOUNDDOORBELL_1 | OUTBOUNDDOORBELL_2 | OUTBOUNDDOORBELL_3 | OUTBOUNDDOORBELL_4);

	//
	// First clear out all interrupts.  Then enable the one's that we can handle.
	//

	Rx_WRITE_UCHAR( AdapterExtension, MUnit.OIMR, 0xff);
	Rx_WRITE_ULONG( AdapterExtension, MUnit.ODR, 0xffffffff);
//	Rx_WRITE_UCHAR(AdapterExtension, MUnit.OIMR, ~(u8)OUTBOUND_DOORBELL_INTERRUPT_MASK);
	Rx_WRITE_UCHAR( AdapterExtension, MUnit.OIMR, 0xfb);

	RxSendSynchCommand(AdapterExtension, 
			 INIT_STRUCT_BASE_ADDRESS, 
			 (u32) AdapterExtension->Common->PhysicalInitStruct,
			 0,
			 0,
			 0,
			 &ReturnStatus);
}


void RxResetDevice(void * Arg1)
{
}

/*++

Routine Description:
	The will cause the adapter to take a break point.

Arguments:
	None

Return Value:
	Nothing

--*/

void RxInterruptAdapter(void * Arg1)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PRx_ADAPTER_EXTENSION AdapterExtension = (PRx_ADAPTER_EXTENSION) CommonExtension->MiniPort;

	u32 ReturnStatus;
	RxSendSynchCommand(AdapterExtension, 
			   BREAKPOINT_REQUEST,
			   0,
			   0,
			   0,
			   0,
			   &ReturnStatus);
}

/*++

Routine Description:
    Will read the adapter CSRs to find the reason the adapter has
    interrupted us.

Arguments:
    AdapterEvent - Enumerated type the returns the reason why we were interrutped.

Return Value:
    Nothing

--*/

void RxNotifyAdapter(void * Arg1, HOST_2_ADAP_EVENT AdapterEvent)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PRx_ADAPTER_EXTENSION AdapterExtension = (PRx_ADAPTER_EXTENSION) CommonExtension->MiniPort;
	u32 ReturnStatus;

	//cmn_err(CE_WARN, "RxNotifyAdapter %d", AdapterEvent);

	switch (AdapterEvent) 
	{
        	case AdapNormCmdQue:
			Rx_WRITE_ULONG(AdapterExtension, MUnit.IDR,INBOUNDDOORBELL_1);
			break;

		case HostNormRespNotFull:
			Rx_WRITE_ULONG(AdapterExtension, MUnit.IDR,INBOUNDDOORBELL_4);
			break;

		case AdapNormRespQue:
			Rx_WRITE_ULONG(AdapterExtension, MUnit.IDR,INBOUNDDOORBELL_2);
			break;

		case HostNormCmdNotFull:
			Rx_WRITE_ULONG(AdapterExtension, MUnit.IDR,INBOUNDDOORBELL_3);
			break;

		case HostShutdown:
//			RxSendSynchCommand(AdapterExtension, HOST_CRASHING, 0, 0, 0, 0, &ReturnStatus);
			break;

		case FastIo:
			Rx_WRITE_ULONG(AdapterExtension, MUnit.IDR,INBOUNDDOORBELL_6);
			break;

		case AdapPrintfDone:
			Rx_WRITE_ULONG(AdapterExtension, MUnit.IDR,INBOUNDDOORBELL_5);
			break;

		default:
			RxBugCheck(0,0,0);
			AfaPortPrint("Notify requested with an invalid request 0x%x.\n",AdapterEvent);
			break;
	}
}

/*++

Routine Description:
	This routine will send a synchronous comamnd to the adapter and wait for its
	completion.

Arguments:
	AdapterExtension - Pointer to adapter extension structure.
	Command - Which command to send
	Parameter1 - 4	- Parameters for command
	ReturnStatus - return status from adapter after completion of command

Return Value:
	AAC_STATUS

--*/

AAC_STATUS RxSendSynchCommand(void * Arg1, u32 Command, u32 Parameter1, u32 Parameter2, u32 Parameter3, u32 Parameter4, u32 *ReturnStatus)
{
	PRx_ADAPTER_EXTENSION AdapterExtension = (PRx_ADAPTER_EXTENSION) Arg1;
	u32 StartTime,EndTime,WaitTime;
	int CommandSucceeded;

	//cmn_err(CE_WARN, "RxSendSyncCommand");
	//
	// Write the Command into Mailbox 0
	//

	Rx_WRITE_ULONG( AdapterExtension, InboundMailbox0, Command);

	//
	// Write the parameters into Mailboxes 1 - 4
	//

	Rx_WRITE_ULONG( AdapterExtension, InboundMailbox1, Parameter1);
	Rx_WRITE_ULONG( AdapterExtension, InboundMailbox2, Parameter2);
	Rx_WRITE_ULONG( AdapterExtension, InboundMailbox3, Parameter3);
	Rx_WRITE_ULONG( AdapterExtension, InboundMailbox4, Parameter4);

	//
	// Clear the synch command doorbell to start on a clean slate.
	//
		
	Rx_WRITE_ULONG( AdapterExtension, OutboundDoorbellReg, OUTBOUNDDOORBELL_0);

	//
	// disable doorbell interrupts
	//

	Rx_WRITE_UCHAR( AdapterExtension, MUnit.OIMR, 
					Rx_READ_UCHAR(AdapterExtension, MUnit.OIMR) | 0x04);

	//
	// force the completion of the mask register write before issuing the interrupt.
	//

	Rx_READ_UCHAR ( AdapterExtension, MUnit.OIMR);

	//
	// Signal that there is a new synch command
	//

	Rx_WRITE_ULONG( AdapterExtension, InboundDoorbellReg, INBOUNDDOORBELL_0);
	CommandSucceeded = FALSE;
	StartTime = jiffies/HZ;
	WaitTime = 0;

	while (WaitTime < 30) { // wait up to 30 seconds
		drv_usecwait(5);				// delay 5 microseconds to let Mon960 get info.

		//
		// Mon110 will set doorbell0 bit when it has completed the command.
		//

		if (Rx_READ_ULONG(AdapterExtension, OutboundDoorbellReg) & OUTBOUNDDOORBELL_0) {

			//
			// clear the doorbell.
			//

			Rx_WRITE_ULONG(AdapterExtension, OutboundDoorbellReg, OUTBOUNDDOORBELL_0);

			CommandSucceeded = TRUE;
			break;
		}
		EndTime = jiffies/HZ;
		WaitTime = EndTime - StartTime;
	}

	if (CommandSucceeded != TRUE) {
		//
		// restore interrupt mask even though we timed out
		//

		Rx_WRITE_UCHAR(AdapterExtension, MUnit.OIMR, 
			 		   Rx_READ_ULONG(AdapterExtension, MUnit.OIMR) & 0xfb);

		return (STATUS_IO_TIMEOUT);
	}

	//
	// Pull the synch status from Mailbox 0.
	//

	*ReturnStatus = Rx_READ_ULONG(AdapterExtension, IndexRegs.Mailbox[0]);

	//
	// Clear the synch command doorbell.
	//
		
	Rx_WRITE_ULONG(AdapterExtension, OutboundDoorbellReg, OUTBOUNDDOORBELL_0);

	//
	// restore interrupt mask
	//

	Rx_WRITE_UCHAR(AdapterExtension, MUnit.OIMR, 
		 		   Rx_READ_ULONG(AdapterExtension, MUnit.OIMR) & 0xfb);

	//
	// Return SUCCESS
	//
	return (STATUS_SUCCESS);
}

/*++

Routine Description:

	This routine will send a synchronous fib to the adapter and wait for its
	completion.

Arguments:

	AdapterExtension - Pointer to adapter extension structure.
	FibPhysicalAddress - Physical address of fib to send.


Return Value:

	int

--*/

int RxSendSynchFib(void * Arg1, u32 FibPhysicalAddress)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PRx_ADAPTER_EXTENSION AdapterExtension = (PRx_ADAPTER_EXTENSION) CommonExtension->MiniPort;
	u32 returnStatus;

	if (RxSendSynchCommand( AdapterExtension,
			    SEND_SYNCHRONOUS_FIB,
			    FibPhysicalAddress,
			    0,
			    0,
			    0,
			    &returnStatus ) != STATUS_SUCCESS ) 
	{
		return (FALSE);
	}
	return (TRUE);
}

