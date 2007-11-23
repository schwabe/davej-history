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
 *   pcisup.h
 *
 * Abstract: This module defines functions that are defined in PciSup.c
 *
 --*/
#ifndef _PCISUP_
#define _PCISUP_



	
/*
 * define which interrupt handler needs to be installed
 */

#define SaISR	1
#define RxISR	2

typedef struct _PCI_MINIPORT_COMMON_EXTENSION {
	u32				AdapterNumber;			// Which FSA# this miniport is
	u32				PciBusNumber;			// Which PCI bus we are located on
	u32				PciSlotNumber;			// Whiat PCI slot we are in

	void *				Adapter;				// Back pointer to Fsa adapter object
	u32				AdapterIndex;			// Index into PlxAdapterTypes array
	PDEVICE_OBJECT			DeviceObject;			// Pointer to our device object
	
	FSAPORT_FUNCS			AdapterFuncs;
	u32				FilesystemRevision; 		// Main driver's revision number
	
// 	KIRQL 				IsrIrql;				// irql our isr runs at
//	PKINTERRUPT			IsrObject;				// Our Interrupt object
//	u32				NumMapRegs; 			// Max amount map regs per dma allowed by the system
//	PADAPTER_OBJECT 		NtAdapter;				// The adapter object for the cyclone board
	
	PADAPTER_INIT_STRUCT		InitStruct;			// Holds initialization info to communicate with adapter
	void *				PhysicalInitStruct; 		// Holds physical address of the init struct
	
//	PFASTIO_STRUCT			FastIoCommArea; 		// pointer to common buffer used for FastIo communication (Host view)
	
	void *				PrintfBufferAddress;		// pointer to buffer used for printf's from the adapter
	
	int	 			AdapterPrintfsToScreen; 		
	int	 			AdapterConfigured;		// set to true when we know adapter can take FIBs
	
	void *				MiniPort;
	
	caddr_t				CommAddress;			// Base address of Comm area
	paddr32_t			CommPhysAddr;			// Physical Address of Comm area
	size_t				CommSize;

	OsKI_t 				OsDep;				// OS dependent kernel interfaces
} PCI_MINIPORT_COMMON_EXTENSION;

typedef PCI_MINIPORT_COMMON_EXTENSION *PPCI_MINIPORT_COMMON_EXTENSION;

typedef int (*PFSA_MINIPORT_INIT) (PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, u32 AdapterNumber, u32 PciBus, u32 PciSlot);

typedef struct _FSA_MINIPORT {
	u16			VendorId;
	u16			DeviceId;
	u16			SubVendorId;
	u16			SubSystemId;
	char *			DevicePrefix;
	PFSA_MINIPORT_INIT	InitRoutine;
	char *             	DeviceName;
	char *             	Vendor;
	char *             	Model;
} FSA_MINIPORT;
typedef FSA_MINIPORT *PFSA_MINIPORT;


#endif // _PCISUP_
