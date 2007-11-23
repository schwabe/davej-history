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
 *   sap1common.h
 *
 * Abstract: Structures and defines for the Drawbridge and StrongArm110 chip.
 *	
 --*/

#ifndef _Sa_COMMON_H_
#define _Sa_COMMON_H_


//
// SaP1 Message Unit Registers
//

typedef volatile struct _StructSaDrawbridge_CSR_RegisterMap {
							//	 	Offset	|	Name
	u32		reserved[10];			//		00h-27h |   	Reserved
	u8		LUT_Offset;			//		28h	|	Looup Table Offset
	u8		reserved1[3];			// 		29h-2bh	|	Reserved
	u32		LUT_Data;			//		2ch	|	Looup Table Data	
	u32		reserved2[26];			//		30h-97h	|	Reserved
	u16		PRICLEARIRQ;			//		98h	|	Primary Clear Irq
	u16		SECCLEARIRQ;			//		9ah	|	Secondary Clear Irq
	u16		PRISETIRQ;			//		9ch	|	Primary Set Irq
	u16		SECSETIRQ;			//		9eh	|	Secondary Set Irq
	u16		PRICLEARIRQMASK;		//		a0h	|	Primary Clear Irq Mask
	u16		SECCLEARIRQMASK;		//		a2h	|	Secondary Clear Irq Mask
	u16		PRISETIRQMASK;			//		a4h	|	Primary Set Irq Mask
	u16		SECSETIRQMASK;			//		a6h	|	Secondary Set Irq Mask
	u32		MAILBOX0;			//		a8h	|	Scratchpad 0
	u32		MAILBOX1;			//		ach	|	Scratchpad 1
	u32		MAILBOX2;			//		b0h	|	Scratchpad 2
	u32		MAILBOX3;			//		b4h	|	Scratchpad 3
	u32		MAILBOX4;			//		b8h	|	Scratchpad 4
	u32		MAILBOX5;			//		bch	|	Scratchpad 5
	u32		MAILBOX6;			//		c0h	|	Scratchpad 6
	u32		MAILBOX7;			//		c4h	|	Scratchpad 7

	u32		ROM_Setup_Data;			//		c8h	| 	Rom Setup and Data
	u32		ROM_Control_Addr;		//		cch	| 	Rom Control and Address

	u32		reserved3[12];			//		d0h-ffh	| 	reserved
	u32		LUT[64];			// 		100h-1ffh|	Lookup Table Entries

	//
	//  TO DO
	//	need to add DMA, I2O, UART, etc registers form 80h to 364h
	//

}Sa_Drawbridge_CSR;

typedef Sa_Drawbridge_CSR *PSa_Drawbridge_CSR;

#define Mailbox0	SaDbCSR.MAILBOX0
#define Mailbox1	SaDbCSR.MAILBOX1
#define Mailbox2	SaDbCSR.MAILBOX2
#define Mailbox3	SaDbCSR.MAILBOX3
#define Mailbox4	SaDbCSR.MAILBOX4
	
#define Mailbox7	SaDbCSR.MAILBOX7
	
#define DoorbellReg_p SaDbCSR.PRISETIRQ
#define DoorbellReg_s SaDbCSR.SECSETIRQ
#define DoorbellClrReg_p SaDbCSR.PRICLEARIRQ

#define	DOORBELL_0	0x00000001
#define DOORBELL_1	0x00000002
#define DOORBELL_2	0x00000004
#define DOORBELL_3	0x00000008
#define DOORBELL_4	0x00000010
#define DOORBELL_5	0x00000020
#define DOORBELL_6	0x00000040

#define PrintfReady	DOORBELL_5
#define PrintfDone	DOORBELL_5
	
typedef struct _Sa_DEVICE_REGISTERS {
	Sa_Drawbridge_CSR	SaDbCSR;			// 98h - c4h
} Sa_DEVICE_REGISTERS;
	
typedef Sa_DEVICE_REGISTERS *PSa_DEVICE_REGISTERS;

	
#endif // _Sa_COMMON_H_

