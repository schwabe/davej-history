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
 *   fsafs.h
 *
 * Abstract: Common file system structures that are required to be
 *           known on both the host and adapter
 *
 *
 *
 --*/

#ifndef _FSAFS_H_
#define	_FSAFS_H_ 1


#include <fsatypes.h>   // core types, shared by client and server, eg, u_long

/*
 *  Maximum number of filesystems.
 */

#define NFILESYS   24

/*
 * File identifier.
 * These are unique and self validating within a filesystem
 * on a single machine and can persist across reboots.
 * The hint field may be volatile and is not guaranteed to persist
 * across reboots but is used to speed up the FID to file object translation
 * if possible. The opaque f1 and f2 fields are guaranteed to uniquely identify
 * the file object (assuming a filesystem context, i.e. driveno).
 */

typedef struct {
	AAC_UINT32  hint; // last used hint for fast reclaim
	AAC_UINT32  f1;	  // opaque
	AAC_UINT32  f2;   // opaque
} fileid_t;		/* intra-filesystem file ID type */

	
/*
 * Generic file handle
 */
struct fhandle {
	fsid_t	 fh_fsid;	/* File system id of mount point */
	fileid_t fh_fid;	/* File sys specific file id */
};
typedef struct fhandle fhandle_t;

#define	FIDSIZE		sizeof(fhandle_t)

typedef struct {
	union {
		AAC_INT8	fid_data[FIDSIZE];
		struct	fhandle fsafid;
	} fidu;
} FSAFID;					/* FSA File ID type */

						
#endif /* _FSAFS_H_ */

