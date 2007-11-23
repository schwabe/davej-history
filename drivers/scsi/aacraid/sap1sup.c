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
 *  sap1sup.c
 *
 * Abstract: Drawbridge specific support functions
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
#include "sap1common.h"
#include "monkerapi.h"

#include "fsaport.h"
#include "fsaioctl.h"


#include "pcisup.h"
#include "sap1.h"

#include "port.h"

#include "nodetype.h"
#include "comsup.h"
#include "afacomm.h"
#include "adapter.h"

#define BugCheckFileId                   (FSAFS_BUG_CHECK_CYCLONESUP)

// #define SaBugCheck(A,B,C) { KeBugCheckEx(0x00000AFA, __LINE__, (u32)A, (u32)B,(u32)C ); }

#define SaBugCheck(A, B, C) {	cmn_err(CE_PANIC, "aacdisk : line %s, 0x%x, 0x%x, 0x%x ", __LINE__, A, B, C);  }

#define NUM_TICKS_PER_SECOND (1000 * 1000 * 10) /* time is in 100 nanoseconds */

int MiniPortRevision = Sa_MINIPORT_REVISION;

//
// The list of all the Sa adapter structures
//

PSa_ADAPTER_EXTENSION SaAdapterList;
int SaInitDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, u32 AdapterNumber, u32 PciBus, u32 PciSlot);
int SaSendSynchFib( void * Arg1, u32 FibPhysicalAddress);

FSA_USER_VAR	SaUserVars[] = {
	{ "AfaPortPrinting", (u32 *)&AfaPortPrinting, NULL },
};


//
// Declare private use routines for this modual
//

/*++

Routine Description:
	The Isr routine for fsa Sa based adapter boards.

Arguments:

Return Value:
	TRUE - if the interrupt was handled by this isr
	FALSE - if the interrupt was not handled by this isr

--*/

u_int SaPciIsr (PSa_ADAPTER_EXTENSION AdapterExtension)
{
	u16 InterruptStatus, Mask;
	u_int OurInterrupt = INTR_UNCLAIMED;

//	cmn_err(CE_WARN, "SaPciIsr entered\n");
	InterruptStatus = Sa_READ_USHORT( AdapterExtension, DoorbellReg_p);

	//
	// Read mask and invert because drawbridge is reversed.
	//
	// This allows us to only service interrupts that have been enabled.
	//

	Mask = ~(Sa_READ_USHORT( AdapterExtension, SaDbCSR.PRISETIRQMASK));

	// Check to see if this is our interrupt.  If it isn't just return FALSE.
	if (InterruptStatus & Mask) {
		OurInterrupt = INTR_CLAIMED;
		if (InterruptStatus & PrintfReady) {
			cmn_err(CE_WARN, "%s:%s", OsGetDeviceName(AdapterExtension), AdapterExtension->Common->PrintfBufferAddress);
			Sa_WRITE_USHORT( AdapterExtension, DoorbellClrReg_p,PrintfReady); //clear PrintfReady
			Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s,PrintfDone);
		} else if (InterruptStatus & DOORBELL_1) {	// Adapter -> Host Normal Command Ready
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, HostNormCmdQue);
			Sa_WRITE_USHORT( AdapterExtension, DoorbellClrReg_p, DOORBELL_1);
		} else if (InterruptStatus & DOORBELL_2) {	// Adapter -> Host Normal Response Ready
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, HostNormRespQue);
			Sa_WRITE_USHORT( AdapterExtension, DoorbellClrReg_p,DOORBELL_2);
		} else if (InterruptStatus & DOORBELL_3) {	// Adapter -> Host Normal Command Not Full
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, AdapNormCmdNotFull);
			Sa_WRITE_USHORT( AdapterExtension, DoorbellClrReg_p, DOORBELL_3);
		} else if (InterruptStatus & DOORBELL_4) {	// Adapter -> Host Normal Response Not Full
			AdapterExtension->Common->AdapterFuncs.InterruptHost(AdapterExtension->Common->Adapter, AdapNormRespNotFull);
			Sa_WRITE_USHORT( AdapterExtension, DoorbellClrReg_p, DOORBELL_4);
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

void SaEnableInterrupt(void * Arg1, ADAPTER_EVENT AdapterEvent, int AtDeviceIrq)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PSa_ADAPTER_EXTENSION AdapterExtension = (PSa_ADAPTER_EXTENSION) CommonExtension->MiniPort;

	switch (AdapterEvent) {
		case HostNormCmdQue:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRICLEARIRQMASK, DOORBELL_1 );
			break;
		case HostNormRespQue:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRICLEARIRQMASK, DOORBELL_2 );
			break;
		case AdapNormCmdNotFull:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRICLEARIRQMASK, DOORBELL_3 );
			break;
		case AdapNormRespNotFull:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRICLEARIRQMASK, DOORBELL_4 );
			break;
		default:
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

void SaDisableInterrupt(void * Arg1, ADAPTER_EVENT AdapterEvent, int AtDeviceIrq)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PSa_ADAPTER_EXTENSION AdapterExtension = (PSa_ADAPTER_EXTENSION) CommonExtension->MiniPort;

	switch (AdapterEvent) 
	{
		case HostNormCmdQue:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRISETIRQMASK, DOORBELL_1 );
			break;
		case HostNormRespQue:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRISETIRQMASK, DOORBELL_2 );
			break;
		case AdapNormCmdNotFull:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRISETIRQMASK, DOORBELL_3 );
			break;
		case AdapNormRespNotFull:
			Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRISETIRQMASK, DOORBELL_4 );
			break;
		default:
	}
}

void SaDetachDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension)
{
	PSa_ADAPTER_EXTENSION AdapterExtension = CommonExtension->MiniPort;
	//
	// Free the register mapping.
	//
	OsDetachDevice(AdapterExtension);
	kfree(AdapterExtension);
}


/*++

Routine Description:
	Scans the PCI bus looking for the Sa card. When found all resources for the
	device will be allocated and the interrupt vectors and csrs will be allocated and
	mapped.

 	The device_interface in the commregion will be allocated and linked to the comm region.

Arguments:

Return Value:
    TRUE - if the device was setup with not problems
    FALSE - if the device could not be mapped and init successfully

--*/

int SaInitDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, u32 AdapterNumber, u32 PciBus, u32 PciSlot)
{
	AAC_STATUS Status;
	PSa_ADAPTER_EXTENSION AdapterExtension = NULL;
	FSA_NEW_ADAPTER NewAdapter;
	u32 StartTime, EndTime, WaitTime;
	u32 InitStatus;
	int instance;
	char *name;

	AfaPortPrint("In init device.\n");

//	cmn_err(CE_NOTE, "SaInitDevice ");

//	AdapterExtension->Common->AdapterIndex = AdapterIndex;
	CommonExtension->AdapterNumber = AdapterNumber;
	CommonExtension->PciBusNumber = PciBus;
	CommonExtension->PciSlotNumber = PciSlot;
	AdapterExtension = kmalloc(sizeof(Sa_ADAPTER_EXTENSION), GFP_KERNEL);
	AdapterExtension->Common = CommonExtension;
	CommonExtension->MiniPort = AdapterExtension;

	instance = OsGetDeviceInstance(AdapterExtension);
	name     = OsGetDeviceName(AdapterExtension);

	//
	// Map in the registers from the adapter, register space 0 is config space,
	// register space 1 is the memery space.
	//

	if (OsMapDeviceRegisters(AdapterExtension)){
		cmn_err(CE_WARN, "%s%d SaInitDevice: failed OsMapDeviceRegisters", name, instance);
		return(FAILURE);
	}

	//
	// Check to see if the board failed any self tests.
	//

	if (Sa_READ_ULONG( AdapterExtension, Mailbox7) & SELF_TEST_FAILED) {
		cmn_err(CE_WARN, "%s%d: adapter self-test failed\n",
				name, instance);
		return(FAILURE);
	}

	//
	// Check to see if the board panic'd while booting.
	//

	if (Sa_READ_ULONG( AdapterExtension, Mailbox7) & KERNEL_PANIC) {
		cmn_err(CE_WARN, "%s%d: adapter kernel panic'd\n",
				name, instance);
		return(FAILURE);
	}

	StartTime = jiffies/HZ;
	WaitTime = 0;

	//
	//  Wait for the adapter to be up and running. Wait up until 3 minutes.
	//

	while (!(Sa_READ_ULONG( AdapterExtension, Mailbox7) & KERNEL_UP_AND_RUNNING)) {
		EndTime = jiffies/HZ;
		WaitTime = EndTime - StartTime;
		if ( WaitTime > (3 * 60) ) {
			InitStatus = Sa_READ_ULONG( AdapterExtension, Mailbox7) >> 16;
			cmn_err(CE_WARN, "%s%d: adapter kernel failed to start, init status = %d\n",
					name, instance, InitStatus);
			return(FAILURE);
		}
	}

	if (OsAttachInterrupt(AdapterExtension, SaISR)) {
		cmn_err(CE_WARN, "%s%d SaInitDevice: failed OsAttachIntterupt", name, instance);
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
	AdapterExtension->Common->AdapterFuncs.InterruptAdapter = SaInterruptAdapter;
	AdapterExtension->Common->AdapterFuncs.EnableInterrupt = SaEnableInterrupt;
	AdapterExtension->Common->AdapterFuncs.DisableInterrupt = SaDisableInterrupt;
	AdapterExtension->Common->AdapterFuncs.NotifyAdapter = SaNotifyAdapter;
	AdapterExtension->Common->AdapterFuncs.ResetDevice = SaResetDevice;
	AdapterExtension->Common->AdapterFuncs.InterruptHost = NULL;
	AdapterExtension->Common->AdapterFuncs.SendSynchFib = SaSendSynchFib;

	NewAdapter.AdapterExtension = CommonExtension;
	NewAdapter.AdapterFuncs = &AdapterExtension->Common->AdapterFuncs;
	NewAdapter.AdapterInterruptsBelowDpc = FALSE;
	NewAdapter.AdapterUserVars = SaUserVars;
	NewAdapter.AdapterUserVarsSize = sizeof(SaUserVars) / sizeof(FSA_USER_VAR);
	NewAdapter.Dip = CommonExtension->OsDep.dip;

	if ( AfaCommInitNewAdapter( &NewAdapter ) == NULL) {
			cmn_err(CE_WARN, "SaInitDevice: AfaCommInitNewAdapter failed\n");
			return (FAILURE);
	};

	AdapterExtension->Common->Adapter = NewAdapter.Adapter;

	if (AdapterExtension->Common->Adapter == NULL) {
		AfaPortLogError(AdapterExtension->Common, FAILURE, NULL, 0);
		cmn_err(CE_WARN, "%s%d SaInitDevice: No Adapter pointer", name, instance);
		return (FAILURE); 
	}
	//
	// Start any kernel threads needed
	OsStartKernelThreads(AdapterExtension);
	//
	// Tell the adapter that all is configure, and it can start accepting requests
	//

	SaStartAdapter(AdapterExtension);

	//
	// Put this adapter into the list of Sa adapters
	//

	AdapterExtension->Next = SaAdapterList;
	SaAdapterList = AdapterExtension;
	AdapterExtension->Common->AdapterConfigured = TRUE;
#ifdef AACDISK
	//
	// Call the disk layer to initialize itself.
	//

	AfaDiskInitNewAdapter( AdapterExtension->Common->AdapterNumber, AdapterExtension->Common->Adapter );
#endif
init_done:
	AdapterExtension->Common->AdapterPrintfsToScreen = FALSE;
	return (0);
init_error:
	return (FAILURE);
}

void SaStartAdapter(PSa_ADAPTER_EXTENSION AdapterExtension)
{
	u32 ReturnStatus;
	LARGE_INTEGER HostTime;
	u32 ElapsedSeconds;
	PADAPTER_INIT_STRUCT InitStruct;

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
		(PrintfReady | DOORBELL_1 | DOORBELL_2 | DOORBELL_3 | DOORBELL_4);

	//
	// First clear out all interrupts.  Then enable the one's that we can handle.
	//

	Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRISETIRQMASK, (u16) 0xffff );
	Sa_WRITE_USHORT( AdapterExtension,  SaDbCSR.PRICLEARIRQMASK,
					(PrintfReady | DOORBELL_1 | DOORBELL_2 | DOORBELL_3 | DOORBELL_4) );

	SaSendSynchCommand(AdapterExtension, 
			 INIT_STRUCT_BASE_ADDRESS, 
			 (u32) AdapterExtension->Common->PhysicalInitStruct,
			 0,
			 0,
			 0,
			 &ReturnStatus);
}


void SaResetDevice(void * Arg1)
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

void SaInterruptAdapter(void * Arg1)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PSa_ADAPTER_EXTENSION AdapterExtension = (PSa_ADAPTER_EXTENSION) CommonExtension->MiniPort;
	u32 ReturnStatus;

	SaSendSynchCommand(AdapterExtension, 
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

void SaNotifyAdapter(void * Arg1, HOST_2_ADAP_EVENT AdapterEvent)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PSa_ADAPTER_EXTENSION AdapterExtension = (PSa_ADAPTER_EXTENSION) CommonExtension->MiniPort;
	u32 ReturnStatus;

	switch (AdapterEvent) 
	{
        	case AdapNormCmdQue:
			Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s,DOORBELL_1);
			break;
		case HostNormRespNotFull:
			Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s,DOORBELL_4);
			break;
		case AdapNormRespQue:
			Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s,DOORBELL_2);
			break;
		case HostNormCmdNotFull:
			Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s,DOORBELL_3);
			break;
		case HostShutdown:
//			SaSendSynchCommand(AdapterExtension, HOST_CRASHING, 0, 0, 0, 0, &ReturnStatus);
			break;
		case FastIo:
			Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s,DOORBELL_6);
			break;
		case AdapPrintfDone:
			Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s,DOORBELL_5);
			break;
		default:
			SaBugCheck(0,0,0);
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

AAC_STATUS SaSendSynchCommand(void * Arg1, u32 Command, u32 Parameter1, u32 Parameter2, u32 Parameter3, u32 Parameter4, u32 * ReturnStatus)
{
	PSa_ADAPTER_EXTENSION AdapterExtension = (PSa_ADAPTER_EXTENSION) Arg1;
	u32 StartTime,EndTime,WaitTime;
	int CommandSucceeded;

	//
	// Write the Command into Mailbox 0
	//

	Sa_WRITE_ULONG( AdapterExtension, Mailbox0, Command);

	//
	// Write the parameters into Mailboxes 1 - 4
	//

	Sa_WRITE_ULONG( AdapterExtension, Mailbox1, Parameter1);
	Sa_WRITE_ULONG( AdapterExtension, Mailbox2, Parameter2);
	Sa_WRITE_ULONG( AdapterExtension, Mailbox3, Parameter3);
	Sa_WRITE_ULONG( AdapterExtension, Mailbox4, Parameter4);

	//
	// Clear the synch command doorbell to start on a clean slate.
	//
		
	Sa_WRITE_USHORT( AdapterExtension, DoorbellClrReg_p, DOORBELL_0);

	//
	// Signal that there is a new synch command
	//

	Sa_WRITE_USHORT( AdapterExtension, DoorbellReg_s, DOORBELL_0);

	CommandSucceeded = FALSE;

	StartTime = jiffies/HZ;
	WaitTime = 0;

	while (WaitTime < 30) { // wait up to 30 seconds
		drv_usecwait(5);				// delay 5 microseconds to let Mon960 get info.
		//
		// Mon110 will set doorbell0 bit when it has completed the command.
		//
		if( Sa_READ_USHORT( AdapterExtension, DoorbellReg_p) & DOORBELL_0 )  {
			CommandSucceeded = TRUE;
			break;
		}
		EndTime = jiffies/HZ;
		WaitTime = EndTime - StartTime;
	}

	if (CommandSucceeded != TRUE) {
		return (STATUS_IO_TIMEOUT);
	}

	//
	// Clear the synch command doorbell.
	//
		
	Sa_WRITE_USHORT( AdapterExtension, DoorbellClrReg_p, DOORBELL_0);

	//
	// Pull the synch status from Mailbox 0.
	//

	*ReturnStatus = Sa_READ_ULONG( AdapterExtension, Mailbox0);

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

int SaSendSynchFib(void * Arg1, u32 FibPhysicalAddress)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	PSa_ADAPTER_EXTENSION AdapterExtension = (PSa_ADAPTER_EXTENSION) CommonExtension->MiniPort;
	u32 returnStatus;

	if (SaSendSynchCommand( AdapterExtension,
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

