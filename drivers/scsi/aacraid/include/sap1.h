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
 *   sap1.h
 *
 * Abstract: Prototypes and data structures unique to the Strong Arm based controller board.
 *
 *	
 --*/

#define Sa_MINIPORT_REVISION			1

typedef struct _Sa_ADAPTER_EXTENSION {
	//
	// The following must be first.
	//
	PPCI_MINIPORT_COMMON_EXTENSION	Common;
	struct _Sa_ADAPTER_EXTENSION	*Next;			// Next adapter miniport structure
	u32				LocalMaskInterruptControl;
	PSa_DEVICE_REGISTERS		Device;
} Sa_ADAPTER_EXTENSION;

typedef Sa_ADAPTER_EXTENSION *PSa_ADAPTER_EXTENSION;

 
/*
 * 
 */
 
#define Sa_READ_USHORT(AEP, CSR)		*(volatile unsigned short *) &((AEP)->Device->CSR)
#define Sa_READ_ULONG(AEP,  CSR)		*(volatile unsigned int *)   &((AEP)->Device->CSR)
#define Sa_WRITE_USHORT(AEP, CSR, Value)	*(volatile unsigned short *) &((AEP)->Device->CSR) = (Value)
#define Sa_WRITE_ULONG(AEP, CSR, Value)		*(volatile unsigned int *)   &((AEP)->Device->CSR) = (Value)


void SaInterruptAdapter(void* arg1);
void SaNotifyAdapter(void* arg1,HOST_2_ADAP_EVENT AdapterEvent);
void SaResetDevice(void* arg1);

 
