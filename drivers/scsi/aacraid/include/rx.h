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
 *   rx.h
 *
 * Abstract: Prototypes and data structures unique to the Rx based controller board.
 *
 *	
 --*/


typedef struct _Rx_ADAPTER_EXTENSION {
	//
	// The following must be first.
	//
	PPCI_MINIPORT_COMMON_EXTENSION	Common;
	struct _Rx_ADAPTER_EXTENSION	*Next;		// Next adapter miniport structure
	u16				LocalMaskInterruptControl;
	PRx_DEVICE_REGISTERS		Device;
} Rx_ADAPTER_EXTENSION;
    
typedef Rx_ADAPTER_EXTENSION *PRx_ADAPTER_EXTENSION;

/*
 * 
 */

#define Rx_READ_UCHAR(AEP,  CSR)		*(volatile unsigned char *)  &((AEP)->Device->CSR)
#define Rx_READ_ULONG(AEP,  CSR)		*(volatile unsigned int *)   &((AEP)->Device->CSR)
#define Rx_WRITE_UCHAR(AEP,  CSR, Value)	*(volatile unsigned char *)  &((AEP)->Device->CSR) = (Value)
#define Rx_WRITE_ULONG(AEP, CSR, Value)		*(volatile unsigned int *)   &((AEP)->Device->CSR) = (Value)

void RxInterruptAdapter(void *arg1);
void RxNotifyAdapter(void *arg1, HOST_2_ADAP_EVENT AdapterEvent);
void RxResetDevice(void *arg1);

