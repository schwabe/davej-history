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
 *   port.h
 *
 * Abstract: This module defines functions and structures that are in common among all miniports
 *
 *	
 --*/

#ifndef _PORT_
#define _PORT_


#ifdef DBG
#define AfaPortPrint if (AfaPortPrinting) DbgPrint
extern int AfaPortPrinting;
#else
#define AfaPortPrint 
#endif DBG

extern int AfaPortPrinting;

int AfaPortAllocateAdapterCommArea(void *arg1,void **CommHeaderAddress,	u32 CommAreaSize, u32 CommAreaAlignment);
int AfaPortFreeAdapterCommArea(void *arg1);
int AfaPortAllocateAndMapFibSpace(void *arg1, PMAPFIB_CONTEXT MapFibContext);
int AfaPortUnmapAndFreeFibSpace(void *arg1, PMAPFIB_CONTEXT MapFibContext);

#endif // _PORT_

