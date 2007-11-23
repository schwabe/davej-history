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
 *   nodetype.h
 *
 * Abstract:     This module defines all of the node type codes used in this development
 *  shell.  Every major data structure in the file system is assigned a node
 *  type code that is.  This code is the first CSHORT in the structure and is
 *  followed by a CSHORT containing the size, in bytes, of the structure.
 *
 --*/
#ifndef _NODETYPE_
#define _NODETYPE_

typedef u16 NODE_TYPE_CODE;


#define FSAFS_NTC_GET_ADAPTER_FIB_CONTEXT ((NODE_TYPE_CODE)0x030b)
#define FSAFS_NTC_FIB_CONTEXT            ((NODE_TYPE_CODE)0x030c)


typedef u16 NODE_BYTE_SIZE;


//
//  The following definitions are used to generate meaningful blue bugcheck
//  screens.  On a bugcheck the file system can output 4 ulongs of useful
//  information.  The first ulong will have encoded in it a source file id
//  (in the high word) and the line number of the bugcheck (in the low word).
//  The other values can be whatever the caller of the bugcheck routine deems
//  necessary.
//
//  Each individual file that calls bugcheck needs to have defined at the
//  start of the file a constant called BugCheckFileId with one of the
//  FSAFS_BUG_CHECK_ values defined below and then use FsaBugCheck to bugcheck
//  the system.
//


#define FSAFS_BUG_CHECK_COMMSUP           (0X001e0000)
#define FSAFS_BUG_CHECK_DPCSUP            (0X001f0000)


#define FsaBugCheck(A,B,C) { cmn_err( CE_PANIC, "aacdisk: module %x, line %x, 0x%x, 0x%x, 0x%x ", BugCheckFileId, __LINE__, A, B, C); }


#endif // _NODETYPE_
