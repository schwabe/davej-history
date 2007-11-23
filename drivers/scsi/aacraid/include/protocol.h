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
 *   protocol.h
 *
 * Abstract: Defines the commands and command data which enables the nt
 *    filesystem driver to be the client of the fsa adapter
 *    filesystem. This protocol is largely modeled after the NFS
 *    V3 protocol with modifications allowed due to the unique
 *    client/server model FSA works under.
 *
 *
 *	
 --*/

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_


#include <fsafs.h>      	// definition of FSAFID; includes fsatypes.h
#include <nvramioctl.h> 	// for NVRAMINFO definition

// #define MDL_READ_WRITE

//
// Define the command values
//
typedef enum _FSA_COMMANDS {
        Null = 0,
        GetAttributes,
        SetAttributes,
        Lookup,
        ReadLink,
        Read,
        Write,
        Create,
        MakeDirectory,
        SymbolicLink,
        MakeNode,
        Removex,
        RemoveDirectoryx, 	// bkpfix added x to this because already defined in nt
        Rename,
        Link,
        ReadDirectory,
        ReadDirectoryPlus,
        FileSystemStatus,
        FileSystemInfo,
        PathConfigure,
        Commit,
        Mount,
        UnMount,
        Newfs,
        FsCheck,
        FsSync,
	SimReadWrite,
	SetFileSystemStatus,
	BlockRead,
	BlockWrite,
	NvramIoctl,
	FsSyncWait,
	ClearArchiveBit,
#ifdef MDL_READ_WRITE
	MdlReadComplete,
	MdlWriteComplete,
	MdlRead,		// these are used solely for stats, Mdl really controlled by 
	MdlWrite,		// flags field in Fib.
#endif
	SetAcl,
	GetAcl,
	AssignAcl,
	FaultInsertion,		// Fault Insertion Command
	CrazyCache,		// crazycache
	MAX_FSACOMMAND_NUM	//CJ: used for sizing stats array - leave last
} _E_FSACOMMAND;

#ifdef AAC_32BIT_ENUMS
typedef	_E_FSACOMMAND	FSACOMMAND;
#else
typedef AAC_UINT32	FSACOMMAND;
#endif



//
// Define the status returns
//
// See include/comm/errno.h for adapter kernel errno's
//

typedef enum _FSASTATUS {
	ST_OK = 0,
	ST_PERM = 1,
	ST_NOENT = 2,
	ST_IO = 5,
	ST_NXIO = 6,
	ST_E2BIG = 7,
	ST_ACCES = 13,
	ST_EXIST = 17,
	ST_XDEV = 18,
	ST_NODEV = 19,
	ST_NOTDIR = 20,
	ST_ISDIR = 21,
	ST_INVAL = 22,
	ST_FBIG = 27,
	ST_NOSPC = 28,
	ST_ROFS = 30,
	ST_MLINK = 31,
	ST_WOULDBLOCK = 35,
	ST_NAMETOOLONG = 63,
	ST_NOTEMPTY = 66,
	ST_DQUOT = 69,
	ST_STALE = 70,
	ST_REMOTE = 71,
	ST_BADHANDLE = 10001,
	ST_NOT_SYNC = 10002,
	ST_BAD_COOKIE = 10003,
	ST_NOTSUPP = 10004,
	ST_TOOSMALL = 10005,
	ST_SERVERFAULT = 10006,
	ST_BADTYPE = 10007,
	ST_JUKEBOX = 10008,
	ST_NOTMOUNTED = 10009,
	ST_MAINTMODE = 10010,
	ST_STALEACL = 10011
} _E_FSASTATUS;

#ifdef AAC_32BIT_ENUMS
typedef _E_FSASTATUS	FSASTATUS;
#else
typedef	AAC_UINT32	FSASTATUS;
#endif

//
// On writes how does the client want the data written.
//

typedef enum _CACHELEVEL {
	CSTABLE = 1,
	CUNSTABLE
} _E_CACHELEVEL;

#ifdef AAC_32BIT_ENUMS
typedef _E_CACHELEVEL	CACHELEVEL;
#else
typedef	AAC_UINT32	CACHELEVEL;
#endif

//
// Lets the client know at which level the data was commited on a write request
//

typedef enum _COMMITLEVEL {
	CMFILE_SYNCH_NVRAM = 1,
	CMDATA_SYNCH_NVRAM,
	CMFILE_SYNCH,
	CMDATA_SYNCH,
	CMUNSTABLE
} _E_COMMITLEVEL;

#ifdef AAC_32BIT_ENUMS
typedef _E_COMMITLEVEL	COMMITLEVEL;
#else
typedef AAC_UINT32	COMMITLEVEL;
#endif



//
// The following are all the different commands or FIBs which can be sent to the
// FSA filesystem. We will define a required subset which cannot return STATUS_NOT_IMPLEMENTED,
// but others outside that subset are allowed to return not implemented. The client is then
// responsible for dealing with the fact it is not implemented.
//

typedef AAC_INT8 	FSASTRING[16];
typedef AAC_UINT32	BYTECOUNT;	// only 32 bit-ism

//
// BlockRead
//

typedef struct _BLOCKREAD { // variable size struct
	FSACOMMAND 		Command;
	AAC_UINT32 		ContainerId;
	BYTECOUNT 		BlockNumber;
	BYTECOUNT 		ByteCount;
	SGMAP			SgMap;	// Must be last in struct because it is variable
} BLOCKREAD;

typedef BLOCKREAD *PBLOCKREAD;

typedef struct _BLOCKREADRESPONSE {
	FSASTATUS 		Status;
	BYTECOUNT 		ByteCount;
} BLOCKREADRESPONSE;

typedef BLOCKREADRESPONSE *PBLOCKREADRESPONSE;

//
// BlockWrite
//

typedef struct _BLOCKWRITE {	// variable size struct
	FSACOMMAND 		Command;
	AAC_UINT32 		ContainerId;
	BYTECOUNT 		BlockNumber;
	BYTECOUNT 		ByteCount;
	CACHELEVEL 		Stable;
	SGMAP			SgMap;	// Must be last in struct because it is variable
} BLOCKWRITE;

typedef BLOCKWRITE *PBLOCKWRITE;


typedef struct _BLOCKWRITERESPONSE {
	FSASTATUS 		Status;
	BYTECOUNT 		ByteCount;
	COMMITLEVEL 	Committed;
} BLOCKWRITERESPONSE;

typedef BLOCKWRITERESPONSE *PBLOCKWRITERESPONSE;



#endif // _PROTOCOL_H_

