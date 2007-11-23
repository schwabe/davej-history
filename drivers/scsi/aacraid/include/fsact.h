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
 *   fsact.h
 *
 * Abstract:  Common container structures that are required to be
 *            known on both the host and adapter.
 *
 *
 --*/
#ifndef _FSACT_H_
#define	_FSACT_H_

//#include <comstruc.h>
//#include <fsatypes.h>
#include <protocol.h> // definitions for FSASTATUS

/*
 * Object-Server / Volume-Manager Dispatch Classes
 */
typedef enum _VM_COMMANDS {
	VM_Null = 0,
	VM_NameServe,
	VM_ContainerConfig,
	VM_Ioctl,
	VM_FilesystemIoctl,
	VM_CloseAll,
	VM_CtBlockRead,		// see protocol.h for BlockRead command layout
	VM_CtBlockWrite,	// see protocol.h for BlockWrite command layout
	VM_SliceBlockRead,	// raw access to configured "storage objects"
	VM_SliceBlockWrite,
	VM_DriveBlockRead,	// raw access to physical devices
	VM_DriveBlockWrite,
	VM_EnclosureMgt,	// enclosure management
	VM_Unused,		// used to be diskset management
	VM_CtBlockVerify,	// see protocol.h for BlockVerify command layout
	VM_CtPerf,		// performance test
	VM_CtBlockRead64,	// see protocol.h for BlockRead64 command layout
	VM_CtBlockWrite64,	// see protocol.h for BlockWrite64 command layout
	VM_CtBlockVerify64,	// see protocol.h for BlockVerify64 command layout   
	MAX_VMCOMMAND_NUM	// used for sizing stats array - leave last
} _E_VMCOMMAND;

#ifdef AAC_32BIT_ENUMS
typedef _E_VMCOMMAND	VMCOMMAND;
#else
typedef	AAC_UINT32	VMCOMMAND;
#endif



//
// Descriptive information (eg, vital stats)
// that a content manager might report.  The
// FileArray filesystem component is one example
// of a content manager.  Raw mode might be
// another.
//

struct FileSysInfo {
/*
	a) DOS usage - THINK ABOUT WHERE THIS MIGHT GO -- THXXX
	b) FSA usage (implemented by ObjType and ContentState fields)
	c) Block size
	d) Frag size
	e) Max file system extension size - (fsMaxExtendSize * fsSpaceUnits)
	f) I-node density - (computed from other fields)
*/
	AAC_UINT32  fsTotalSize;	// consumed by fs, incl. metadata
	AAC_UINT32  fsBlockSize;
	AAC_UINT32  fsFragSize;
	AAC_UINT32  fsMaxExtendSize;
	AAC_UINT32  fsSpaceUnits;
	AAC_UINT32  fsMaxNumFiles;
	AAC_UINT32  fsNumFreeFiles;
	AAC_UINT32  fsInodeDensity;
};	// valid iff ObjType == FT_FILESYS && !(ContentState & FSCS_NOTCLEAN)

union ContentManagerInfo {
	struct FileSysInfo FileSys;	// valid iff ObjType == FT_FILESYS && !(ContentState & FSCS_NOTCLEAN)
};

//
// Query for "mountable" objects, ie, objects that are typically
// associated with a drive letter on the client (host) side.
//

typedef struct _MNTOBJ {
	AAC_UINT32    ObjectId;
	FSASTRING  FileSystemName;   // if applicable
	ContainerCreationInfo   CreateInfo; // if applicable
	AAC_UINT32    Capacity;
	FSAVOLTYPE VolType;          // substrate structure
	FTYPE      ObjType;          // FT_FILESYS, FT_DATABASE, etc.
	AAC_UINT32     ContentState;     // unready for mounting, readonly, etc.

	union ContentManagerInfo
	ObjExtension;     // Info specific to content manager (eg, filesystem)

	AAC_UINT32    AlterEgoId;       // != ObjectId <==> snapshot or broken mirror exists
} MNTOBJ;


#define FSCS_READONLY	0x0002	// possible result of broken mirror

typedef struct _MNTINFO {
	VMCOMMAND  Command;
	FTYPE      MntType;
	AAC_UINT32     MntCount;
} MNTINFO;
typedef MNTINFO *PMNTINFO;

typedef struct _MNTINFORESPONSE {
	FSASTATUS Status;
	FTYPE     MntType;           // should be same as that requested
	AAC_UINT32    MntRespCount;
	MNTOBJ    MntTable[1];
} MNTINFORESPONSE;
typedef MNTINFORESPONSE *PMNTINFORESPONSE;


//
// The following command is sent to shut down each container.
//

typedef struct _CLOSECOMMAND {
	VMCOMMAND  Command;
	AAC_UINT32	  ContainerId;
} CLOSECOMMAND;
typedef CLOSECOMMAND *PCLOSECOMMAND;


#endif /* _FSACT_H_ */


