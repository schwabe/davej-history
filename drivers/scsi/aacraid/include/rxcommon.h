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
 *   rxcommon.h
 *
 * Abstract: Structures and defines for the i960 Rx chip.
 *
 *	
 --*/

#ifndef _Rx_COMMON_H_
#define _Rx_COMMON_H_

//
// Rx Message Unit Registers
//

typedef volatile struct _StructRxMURegisters {
				//	 Local	|   PCI*	|	Name
				//		|		|
	u32	ARSR;		//	1300h	|	00h	|	APIC Register Select Register
	u32	reserved0;	//	1304h	|	04h	|	Reserved
	u32	AWR;		//	1308h	|	08h	|	APIC Window Register
	u32	reserved1;	//	130Ch	|	0Ch	|	Reserved
	u32	IMRx[2];	//	1310h	|	10h	|	Inbound Message Registers
	u32	OMRx[2];	//	1318h	|	18h	|	Outbound Message Registers
	u32	IDR;		//	1320h	|	20h	|	Inbound Doorbell Register
	u32	IISR;		//	1324h	|	24h	|	Inbound Interrupt Status Register
	u32	IIMR;		//	1328h	|	28h	|	Inbound Interrupt Mask Register
	u32	ODR;		//	132Ch	|	2Ch	|	Outbound Doorbell Register
	u32	OISR;		//	1330h	|	30h	|	Outbound Interrupt Status Register
	u32	OIMR;		//	1334h	|	34h	|	Outbound Interrupt Mask Register
				// * Must access trhough ATU Inbound Translation Window
}Rx_MU_CONFIG;

typedef Rx_MU_CONFIG *PRx_MU_CONFIG;

typedef volatile struct _Rx_Inbound {
	u32	Mailbox[8];
}Rx_Inbound;

typedef Rx_Inbound *PRx_Inbound;

#define	InboundMailbox0		IndexRegs.Mailbox[0]
#define	InboundMailbox1		IndexRegs.Mailbox[1]
#define	InboundMailbox2		IndexRegs.Mailbox[2]
#define	InboundMailbox3		IndexRegs.Mailbox[3]
#define	InboundMailbox4		IndexRegs.Mailbox[4]

#define	INBOUNDDOORBELL_0	0x00000001
#define INBOUNDDOORBELL_1	0x00000002
#define INBOUNDDOORBELL_2	0x00000004
#define INBOUNDDOORBELL_3	0x00000008
#define INBOUNDDOORBELL_4	0x00000010
#define INBOUNDDOORBELL_5	0x00000020
#define INBOUNDDOORBELL_6	0x00000040

#define	OUTBOUNDDOORBELL_0	0x00000001
#define OUTBOUNDDOORBELL_1	0x00000002
#define OUTBOUNDDOORBELL_2	0x00000004
#define OUTBOUNDDOORBELL_3	0x00000008
#define OUTBOUNDDOORBELL_4	0x00000010

#define InboundDoorbellReg	MUnit.IDR
#define OutboundDoorbellReg	MUnit.ODR

typedef struct _Rx_DEVICE_REGISTERS {
	Rx_MU_CONFIG		MUnit;		// 1300h - 1334h
	u32			reserved1[6];	// 1338h - 134ch
	Rx_Inbound		IndexRegs;
} Rx_DEVICE_REGISTERS;

typedef Rx_DEVICE_REGISTERS *PRx_DEVICE_REGISTERS;

#endif // _Rx_COMMON_H_


