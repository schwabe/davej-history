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
 *   comprocs.h
 *
 * Abstract: This module defines all of the globally used procedures in the Afa comm layer
 *
 *
 *
 --*/
#ifndef _COMPROCS_
#define _COMPROCS_

#include "osheaders.h"

#include "AacGenericTypes.h"

#include "aac_unix_defs.h"

#include "nodetype.h"

// #define GATHER_FIB_TIMES

#include "fsatypes.h"

#include "perfpack.h"

#include "comstruc.h"

//#include "unix_protocol.h"

#include "fsact.h"

#include "protocol.h"

#include "fsaioctl.h"

#undef GATHER_FIB_TIMES

#include "aifstruc.h"

#include "fsaport.h"
#include "comsup.h"
#include "afacomm.h"
#include "adapter.h"

#include "commfibcontext.h"
#include "comproto.h"
#include "commdata.h"
#include "commerr.h"




//
// The following macro is used when sending and receiving FIBs.  It is only used for
// debugging.

#if DBG
#define	FIB_COUNTER_INCREMENT(Counter)		InterlockedIncrement(&(Counter))
#else
#define	FIB_COUNTER_INCREMENT(Counter)		
#endif

int AfaCommAdapterDeviceControl(void * AdapterArg, PAFA_IOCTL_CMD IoctlCmdPtr);

#endif // _COMPROCS_
