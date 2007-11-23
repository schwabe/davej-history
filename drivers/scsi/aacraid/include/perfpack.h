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
 *   perfpack.h
 *
 * Abstract: This file defines the layout of the performance data that is passed
 *           back from the FSA filesystem driver.
 *
 *	
 --*/

#ifndef _FSA_PERFPACK_H_
#define _FSA_PERFPACK_H_	1


//#define FSA_DO_PERF		1		/* enable the engineering counters */

#ifdef FSA_DO_PERF
//
// engineering counters
//
typedef struct _FSA_PERF_DATA {
		u32 FibsSent;
		u32 ReadDirs;
		u32 GetAttrs;
		u32 SetAttrs;
		u32 Lookups;
		u32 ReadFibs;
		u32 WriteFibs;
		u32 CreateFibs;
		u32 MakeDirs;
		u32 RemoveFibs;
		u32 RemoveDirs;
		u32 RenameFibs;
		u32 ReadDirPlus;
		u32 FsStat;
		u32 WriteBytes;
		u32 ReadBytes;
// NT FSA entry points
		u32 FsaFsdCreateCount;
		u32 FsaFsdCloseCount;
		u32 FsaFsdReadCount;
		u32 FsaFsdWriteCount;
		u32 FsaFsdQueryInformationCount;
		struct _FsaFsdSetInfomation{
			u32 FsaSetAllocationInfoCount;
			u32 FsaSetBasicInfoCount;
			u32 FsaSetDispositionInfoCount;
			u32 FsaSetEndOfFileInfoCount;
			u32 FsaSetPositionInfoCount;
			u32 FsaSetRenameInfoCount;
			u32 FsaClearArchiveBitCount;
		};
		u32 FsaFsdFlushBuffersCount;
		u32 FsaFsdQueryVolumeInfoCount;
		u32 FsaFsdSetVolumeInfoCount;
		u32 FsaFsdCleanupCount;
		u32 FsaFsdDirectoryControlCount;
		u32 FsaFsdFileSystemControlCount;
		u32 FsaFsdLockControlCount;
		u32 FsaFsdDeviceControlCount;
		u32 FsaFsdShutdownCount;
		u32 FsaFsdQuerySecurityInfo;
		u32 FsaFsdSetSecurityInfo;
		u32 FastIoCheckIfPossibleCount;
		u32 FastIoReadCount;
		u32 FastIoWriteCount;
		u32 FastIoQueryBasicInfoCount;
		u32 FastIoQueryStandardInfoCount;
		u32 FastIoLockCount;
		u32 FastIoUnlockSingleCount;
		u32 FastIoUnlockAllCount;
		u32 FastIoUnlockAllByKeyCount;
		u32 FastIoDeviceControlCount;
} FSA_PERF_DATA;

typedef FSA_PERF_DATA *PFSA_PERF_DATA;


#else /* FSA_DO_PERF */

//
// engineering performance counters are disabled
//
#define FSA_DO_PERF_INC(Counter)		/* */
#define FSA_DO_FSP_PERF_INC(Counter)	/* */

#endif /* FSA_DO_PERF */

#endif // _FSA_PERFPACK_H_
