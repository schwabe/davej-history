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
 *   commdata.h
 *
 * Abstract: Define the communication layer of the adapter
 *
 *
 *
 --*/
#ifndef _COMMDATA_
#define _COMMDATA_

typedef struct _FSA_COMM_DATA {
  //
  //  A pointer to the Driver and Device object we were initialized with
  //
  
  PDRIVER_OBJECT DriverObject;
  PDEVICE_OBJECT DeviceObject;
  
  //
  // A list of all adapters we have configured.
  // 
  
  PAFA_COMM_ADAPTER AdapterList;
  u32		  TotalAdapters;
  
  //
  // Adapter timeout support. This is the default timeout to wait for the
  // adapter to respond(setup in initfs.c), and a boolean to indicate if
  // we should timeout requests to the adapter or not.
  //

  LARGE_INTEGER QueueFreeTimeout;
  LARGE_INTEGER AdapterTimeout;
  int EnableAdapterTimeouts;

  u32	FibTimeoutIncrement;
  
  u32 FibsSent;
  u32 FibRecved;
  u32 NoResponseSent;
  u32 NoResponseRecved;
  u32 AsyncSent;
  u32 AsyncRecved;
  u32 NormalSent;
  u32 NormalRecved;
  
  u32 TimedOutFibs;
  
  KDPC		TimeoutDPC;
  KTIMER	TimeoutTimer;
  
  // 
  // If this value is set to 1 then interrupt moderation will occur 
  // in the base commuication support.
  //

  u32 EnableInterruptModeration;

  int HardInterruptModeration;
  int HardInterruptModeration1;
  int PeakFibsConsumed;
  int ZeroFibsConsumed;
  int EnableFibTimeoutBreak;
  u32 FibTimeoutSeconds;
  
  //
  // The following holds all of the available user settable variables.
  // This includes all for the comm layer as well as any from the class
  // drivers as well.
  //
  
  FSA_USER_VAR	*UserVars;
  u32			NumUserVars;
  u32           MeterFlag;
#ifdef FIB_CHECKSUMS
  int do_fib_checksums;
#endif
} FSA_COMM_DATA;

typedef FSA_COMM_DATA *PFSA_COMM_DATA;
extern FSA_COMM_DATA FsaCommData;

#endif // _COMMDATA_

