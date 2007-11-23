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
 *   osddi.c
 *
 * Abstract: This file contains all the proceedures which use LINUX specific Device 
 *		Driver Interfaces.
 *	
 --*/
#include "osheaders.h"

#include <linux/smp_lock.h>

#ifdef fsid_t
#undef fsid_t
#endif
#include "AacGenericTypes.h"
#include "aac_unix_defs.h"
#include "comstruc.h"
#include "monkerapi.h"
#include "protocol.h"
#include "fsafs.h"

#include "sap1common.h"
#include "fsaport.h"
#include "pcisup.h"
#include "sap1.h"
#include "nodetype.h"
#include "comsup.h"
#include "afacomm.h"
#include "adapter.h"


void AacSaPciIsr(int irq, void *irq_data, struct pt_regs *regs);
void AacRxPciIsr(int irq, void *irq_data, struct pt_regs *regs);
unsigned SaPciIsr(PSa_ADAPTER_EXTENSION AdapterExtension);
unsigned RxPciIsr(PSa_ADAPTER_EXTENSION AdapterExtension);


/*----------------------------------------------------------------------------*/

void AfaCommInterruptHost(void *AdapterArg, ADAPTER_EVENT AdapterEvent )
{
	PAFA_COMM_ADAPTER	Adapter = ( PAFA_COMM_ADAPTER ) AdapterArg;
	PCOMM_REGION CommRegion = Adapter->CommRegion;

	switch (AdapterEvent) {

		case HostNormRespQue:
	        	OsSoftInterruptTrigger( CommRegion->HostNormRespQue.ConsumerRoutine );
			// #REVIEW# - what do we do with this
			// if (FsaCommData.HardInterruptModeration)
			//	DisableInterrupt( Adapter, HostNormRespQue, TRUE );
			break;

		case AdapNormCmdNotFull:
	        	OsSoftInterruptTrigger( CommRegion->QueueNotFullDpc );
			break;

		case HostNormCmdQue:
		        OsSoftInterruptTrigger( CommRegion->HostNormCmdQue.ConsumerRoutine );
			break;

		case AdapNormRespNotFull:
		        OsSoftInterruptTrigger( CommRegion->QueueNotFullDpc );
			break;

		// #REVIEW# - what do we do with these
		case HostHighCmdQue:
		case HostHighRespQue:
		case AdapHighCmdNotFull:
		case AdapHighRespNotFull:
		case SynchCommandComplete:
		case AdapInternalError:
			break;
	}
}


// get the device name associated with this instance of the device
/*----------------------------------------------------------------------------*/

char *OsGetDeviceName(void *AdapterExtension )
{
	return(((Sa_ADAPTER_EXTENSION *)AdapterExtension)->Common->OsDep.scsi_host_ptr->hostt->name);
}


/*----------------------------------------------------------------------------*/

int OsGetDeviceInstance(void *AdapterExtension )
{
	return((int)((Sa_ADAPTER_EXTENSION *)AdapterExtension)->Common->OsDep.scsi_host_ptr->unique_id);
}


/*------------------------------------------------------------------------------
	OsMapDeviceRegisters()

	Postconditions:
		Return zero on success non-zero otherwise.
 *----------------------------------------------------------------------------*/

int OsMapDeviceRegisters(Sa_ADAPTER_EXTENSION *AdapterExtension)
{
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtension;

	CommonExtension = AdapterExtension->Common;

	if( AdapterExtension->Device = ( Sa_DEVICE_REGISTERS * )
			ioremap( ( unsigned long )CommonExtension->OsDep.scsi_host_ptr->base, 8192 ) )
	{
		cmn_err( CE_DEBUG, "Device mapped to virtual address 0x%x", AdapterExtension->Device ); 
		return( 0 );
	}
	else
	{	
		cmn_err( CE_WARN, "OsMapDeviceRegisters: ioremap() failed" );
		return( 1 );
	}
}


/*------------------------------------------------------------------------------
	OsUnMapDeviceRegisters()

	Postconditions:
 *----------------------------------------------------------------------------*/

void OsUnMapDeviceRegisters(Sa_ADAPTER_EXTENSION *AdapterExtension)
{
	iounmap((void *)AdapterExtension->Device);
}


/*----------------------------------------------------------------------------*/

int OsAttachInterrupt(Sa_ADAPTER_EXTENSION *AdapterExtension ,int WhichIsr)
{
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtension;
	void *irq_data;
	void (*Isr)();

	CommonExtension = AdapterExtension->Common;
	irq_data = (void *)AdapterExtension;

	switch (WhichIsr) 
	{
		case SaISR:
			Isr = AacSaPciIsr;
			break;
		case RxISR:
			Isr = AacRxPciIsr;
			break;
		default:
			cmn_err(CE_WARN, "OsAttachInterrupt: invalid ISR case: 0x%x", WhichIsr);
			return FAILURE;
	}

	if(request_irq(CommonExtension->OsDep.scsi_host_ptr->irq,	// interrupt number
				Isr,			// handler function
				SA_INTERRUPT|SA_SHIRQ,
				"aacraid",
				irq_data)) 
	{
		cmn_err(CE_DEBUG, "OsAttachInterrupt: Failed for IRQ: 0x%x", 
			CommonExtension->OsDep.scsi_host_ptr->irq);
		return(FAILURE);
	}
	return 0;
}


/*----------------------------------------------------------------------------*/

void AacSaPciIsr(int irq, void *irq_data, struct pt_regs *regs)
{
	// call the actual interrupt handler
	SaPciIsr((Sa_ADAPTER_EXTENSION *)irq_data);
}

/*----------------------------------------------------------------------------*/
void AacRxPciIsr(int irq, void *irq_data, struct pt_regs *regs)
{
	// call the actual interrupt handler
	RxPciIsr((Sa_ADAPTER_EXTENSION *)irq_data);
}


/*----------------------------------------------------------------------------*/
void OsDetachInterrupt(Sa_ADAPTER_EXTENSION *AdapterExtension)
{
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtension;
	void *irq_data;

	CommonExtension = AdapterExtension->Common;
	irq_data = (void *)AdapterExtension;

	free_irq(CommonExtension->OsDep.scsi_host_ptr->irq, irq_data);
}


/*----------------------------------------------------------------------------*/
void OsDetachDevice(Sa_ADAPTER_EXTENSION *AdapterExtension)
{
	OsUnMapDeviceRegisters( AdapterExtension );
	return 0;
}

/*----------------------------------------------------------------------------*/
u32 *OsAllocCommPhysMem(Sa_ADAPTER_EXTENSION *AdapterExtension, u32 size, u32 **virt_addr_pptr, u32 *phys_addr_ptr)
{
	if((*virt_addr_pptr = (u32 *)kmalloc(size,GFP_KERNEL)))
	{
		*phys_addr_ptr = virt_to_bus((volatile void *)*virt_addr_pptr);
		if(!*phys_addr_ptr)
		{
			cmn_err(CE_WARN, "OsAllocCommPhysMem: OsVirtToPhys failed");
		}
		return *virt_addr_pptr;
	}
	else
		return NULL;
}

OsAifKernelThread(Sa_ADAPTER_EXTENSION *AdapterExtension)
{
	struct fs_struct *fs;
	int i;
	struct task_struct *tsk;

	tsk = current;

	/*
	 * set up the name that will appear in 'ps'
	 * stored in  task_struct.comm[16].
	 */

	sprintf(tsk->comm, "AIFd");
	// use_init_fs_context();  only exists in 2.2.13 onward.
	lock_kernel();

	/*
	 * we were started as a result of loading the  module.
	 * free all of user space pages
	 */

	exit_mm(tsk);
	exit_files(tsk);
	exit_fs(tsk);

	fs = init_task.fs;
	tsk->fs = fs;

	tsk->session = 1;
	tsk->pgrp = 1;

	if (fs)
		atomic_inc(&fs->count);

	unlock_kernel();
	NormCommandThread(AdapterExtension);
	/* NOT REACHED */
}

/*----------------------------------------------------------------------------*/

void OsStartKernelThreads(Sa_ADAPTER_EXTENSION 	*AdapterExtension)
{
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtension;
	AFA_COMM_ADAPTER *Adapter;
	extern void NormCommandThread(void *Adapter);

	CommonExtension = AdapterExtension->Common;
	Adapter = (AFA_COMM_ADAPTER *)CommonExtension->Adapter;

	//
	// Start thread which will handle AdapterInititatedFibs from this adapter
	//
	CommonExtension->OsDep.thread_pid = kernel_thread((int (*)(void *))OsAifKernelThread, Adapter, 0 );
}

/*----------------------------------------------------------------------------*/

int AfaPortAllocateAndMapFibSpace(void * Arg1, PMAPFIB_CONTEXT MapFibContext )
{
	void *BaseAddress;
	u32  PhysAddress;

	if(!(BaseAddress = (u32 *)kmalloc(MapFibContext->Size, GFP_KERNEL)))
	{
		cmn_err( CE_WARN, "AfaPortAllocateAndMapFibSpace: OsAllocMemory failed" );
		return( FALSE );
	}

	PhysAddress = virt_to_bus(BaseAddress);
	
	MapFibContext->FibVirtualAddress = BaseAddress;
	MapFibContext->FibPhysicalAddress = (void *)PhysAddress;

	return (TRUE);
}

/*----------------------------------------------------------------------------*/

int AfaPortUnmapAndFreeFibSpace(void *Arg1, PMAPFIB_CONTEXT MapFibContext)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	kfree(MapFibContext->FibVirtualAddress);
	return (TRUE);
}

/*----------------------------------------------------------------------------*/

int AfaPortFreeAdapterCommArea(void * Arg1)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	kfree(CommonExtension->CommAddress);
	return (TRUE);
}


/* ================================================================================ */
/*
 * Not sure if the functions below here ever get called in the current code
 * These probably should be a different file.
 */
/*
ddi_dma_attr_t AfaPortDmaAttributes = {
	//rpbfix : we may want something different for I/O
	DMA_ATTR_V0,
	0,
	0xffffffff,
	0x0000ffff,
	1,
	1,
	1,
	0x0000ffff,
	0x0000ffff,
	17,
	512,
	0
};
*/

