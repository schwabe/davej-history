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
 *  port.c
 *
 * Abstract: All support routines for FSA communication which are miniport specific.
 *
 --*/

#include "osheaders.h"


#include "AacGenericTypes.h"

#include "aac_unix_defs.h"

#include "fsatypes.h"
#include "comstruc.h"
#include "protocol.h"

#include "fsaport.h"
#include "fsaioctl.h"

#include "pcisup.h"
#include "port.h"

int AfaPortPrinting = 1;

extern AAC_STATUS AfaPort_Err_Adapter_Printf;
extern AAC_STATUS AfaPort_Warn_Adapter_Printf;
extern AAC_STATUS AfaPort_Info_Adapter_Printf;
extern AAC_STATUS AfaPort_Err_FastAfa_Load_Driver;


/*++

Routine Description:
	Does all of the work to log an error log entry
	
Arguments:
	CommonExtension - Pointer to the adapter that caused the error.
	ErrorCode - Which error is being logged.
	StringBuffer - Pointer to optional String for error log entry.
	StringLength - Length of StringBuffer.

Return Value:
    Nothing

--*/

voidAfaPortLogError(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, AAC_STATUS ErrorCode, unsigned char * StringBuffer, u32 StringLength)
{

}

int AfaPortGetNextAdapterNumber(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT *FsaDeviceObject, PFILE_OBJECT *FileObject, u32 *AdapterNumber)
{
}

int AfaPortAllocateAdapterCommArea(void *Arg1, void **CommHeaderAddress, u32 CommAreaSize, u32 CommAreaAlignment)
{
	PPCI_MINIPORT_COMMON_EXTENSION CommonExtension = (PPCI_MINIPORT_COMMON_EXTENSION) Arg1;
	void * BaseAddress;
	PHYSICAL_ADDRESS PhysicalBaseAddress;
	u32 TotalSize, BytesToAlign;
	size_t		RealLength;
	uint_t		Count;
//	u32 SizeOfFastIoComm = sizeof(FASTIO_STRUCT);
//	u32 AdapterFibsSize = PAGE_SIZE;
	u32 AdapterFibsSize = 4096;
	u32 PrintfBufferSize = 256;
	PADAPTER_INIT_STRUCT InitStruct;
	extern int MiniPortRevision;
	u32	PhysAddress;

//	TotalSize = AdapterFibsSize + sizeof(ADAPTER_INIT_STRUCT) + CommAreaSize + CommAreaAlignment +
//		 SizeOfFastIoComm + PrintfBufferSize;
	TotalSize = AdapterFibsSize + sizeof(ADAPTER_INIT_STRUCT) + CommAreaSize + CommAreaAlignment +
		 PrintfBufferSize;


	OsAllocCommPhysMem(CommonExtension->MiniPort, TotalSize, &BaseAddress, &PhysAddress);

	CommonExtension->CommAddress  = BaseAddress;
	CommonExtension->CommPhysAddr = PhysAddress;
	CommonExtension->CommSize 	  = TotalSize;

	PhysicalBaseAddress.HighPart = 0;
	PhysicalBaseAddress.LowPart = PhysAddress;

	CommonExtension->InitStruct = (PADAPTER_INIT_STRUCT)((unsigned char *)(BaseAddress) + AdapterFibsSize);
	CommonExtension->PhysicalInitStruct = (PADAPTER_INIT_STRUCT)((unsigned char *)(PhysicalBaseAddress.LowPart) + AdapterFibsSize);

	InitStruct = CommonExtension->InitStruct;

	InitStruct->InitStructRevision = ADAPTER_INIT_STRUCT_REVISION;
	InitStruct->MiniPortRevision = MiniPortRevision;
	InitStruct->FilesystemRevision = CommonExtension->FilesystemRevision;

	//
	// Adapter Fibs are the first thing allocated so that they start page aligned
	//
	InitStruct->AdapterFibsVirtualAddress = BaseAddress;
	InitStruct->AdapterFibsPhysicalAddress = (void *) PhysicalBaseAddress.LowPart;
	InitStruct->AdapterFibsSize = AdapterFibsSize;
	InitStruct->AdapterFibAlign = sizeof(FIB);

	//
	// Increment the base address by the amount already used
	//
	BaseAddress = (void *)((unsigned char *)(BaseAddress) + AdapterFibsSize + sizeof(ADAPTER_INIT_STRUCT));
	PhysicalBaseAddress.LowPart = (u32)((unsigned char *)(PhysicalBaseAddress.LowPart) + AdapterFibsSize + sizeof(ADAPTER_INIT_STRUCT));

	//
	// Align the beginning of Headers to CommAreaAlignment
	//
	BytesToAlign = (CommAreaAlignment - ((u32)(BaseAddress) & (CommAreaAlignment - 1)));
	BaseAddress = (void *)((unsigned char *)(BaseAddress) + BytesToAlign);
	PhysicalBaseAddress.LowPart = (u32)((unsigned char *)(PhysicalBaseAddress.LowPart) + BytesToAlign);

	//
	// Fill in addresses of the Comm Area Headers and Queues
	//
	*CommHeaderAddress = BaseAddress;
	InitStruct->CommHeaderAddress = (void *)PhysicalBaseAddress.LowPart;

	//
	//	Increment the base address by the size of the CommArea
	//
	BaseAddress = (void *)((unsigned char *)(BaseAddress) + CommAreaSize);
	PhysicalBaseAddress.LowPart = (u32)((unsigned char *)(PhysicalBaseAddress.LowPart) + CommAreaSize);


	//
	// Place the Printf buffer area after the Fast I/O comm area.
	//
	CommonExtension->PrintfBufferAddress = (void *)(BaseAddress);
	InitStruct->PrintfBufferAddress = (void *)PhysicalBaseAddress.LowPart;
	InitStruct->PrintfBufferSize = PrintfBufferSize;

	AfaPortPrint("FsaAllocateAdapterCommArea: allocated a common buffer of 0x%x bytes from address 0x%x to address 0x%x\n",
			 TotalSize, InitStruct->AdapterFibsVirtualAddress,
			 (unsigned char *)(InitStruct->AdapterFibsVirtualAddress) + TotalSize);

	AfaPortPrint("Mapped on to PCI address 0x%x\n", InitStruct->AdapterFibsPhysicalAddress);

	return (TRUE);
}

/*++

Routine Description:
	The routine will get called each time a user issues a CreateFile on the DeviceObject
	for the adapter.

	The main purpose of this routine is to set up any data structures that may be needed
	to handle any requests made on this DeviceObject.

Arguments:
	DeviceObject - Pointer to device object representing adapter
	Irp - Pointer to Irp that caused this open


Return Value:
	Status value returned from File system driver AdapterOpen

--*/

AAC_STATUS AfaPortCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
}

/*++

Routine Description:
	This routine will get called each time a user issues a CloseHandle on the DeviceObject
	for the adapter.

	The main purpose of this routine is to cleanup any data structures that have been set up
	while this FileObject has been opened.

Arguments:
	DeviceObject - Pointer to device object representing adapter
	Irp - Pointer to Irp that caused this close

Return Value:
	Status value returned from File system driver AdapterClose

--*/

AAC_STATUS AfaPortClose (PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
}

AAC_STATUS AfaPortDeviceControl (PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
}

/*++

Routine Description:
	This routine determines the max physical page in host memory.

Arguments:
	AdapterExtension

Return Value:
	Max physical page in host memory.

--*/

u32 AfaPortGetMaxPhysicalPage(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension)
{
}


