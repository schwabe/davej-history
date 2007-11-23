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
 *   monkerapi.h
 *
 * Abstract: This module contains the definitions used by the Host Adapter
 *      Communications interface.
 *      This is the interface used for by host programs and the Adapter 
 *      to communicate via synchronous commands via a shared set of registers
 *      on a platform (typically doorbells and mailboxes).
 *
 --*/
//**********************************************************************
//
//	Monitor / Kernel API
//
//	03/24/1998 Bob Peret	Initial creation
//
//**********************************************************************

#ifndef MONKER_H
#define MONKER_H


#define	BREAKPOINT_REQUEST		0x00000004
#define	INIT_STRUCT_BASE_ADDRESS	0x00000005


#define	SEND_SYNCHRONOUS_FIB		0x0000000c


//
//	Adapter Status Register
//
//  Phase Staus mailbox is 32bits:
//	<31:16> = Phase Status
//	<15:0>  = Phase
//
//  The adapter reports is present state through the phase.  Only
//  a single phase should be ever be set.  Each phase can have multiple
//	phase status bits to provide more detailed information about the 
//	state of the board.  Care should be taken to ensure that any phase status 
//  bits that are set when changing the phase are also valid for the new phase
//  or be cleared out.  Adapter software (monitor, iflash, kernel) is responsible
//  for properly maintining the phase status mailbox when it is running.

//											
// MONKER_API Phases							
//
// Phases are bit oriented.  It is NOT valid 
// to have multiple bits set						
//					

#define	SELF_TEST_FAILED		0x00000004
#define	KERNEL_UP_AND_RUNNING		0x00000080
#define	KERNEL_PANIC			0x00000100

//
// Doorbell bit defines
//

#define DoorBellPrintfDone		(1<<5)	// Host -> Adapter
#define DoorBellAdapterNormCmdReady	(1<<1)	// Adapter -> Host
#define DoorBellAdapterNormRespReady	(1<<2)	// Adapter -> Host
#define DoorBellAdapterNormCmdNotFull	(1<<3)	// Adapter -> Host
#define DoorBellAdapterNormRespNotFull	(1<<4)	// Adapter -> Host
#define DoorBellPrintfReady		(1<<5)	// Adapter -> Host

#endif // MONKER_H

