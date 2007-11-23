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
 *   Aifstruc.h
 *
 * Abstract:
 *   Define all shared data types relating to
 *   the set of features utilizing Adapter
 *   Initiated Fibs.
 *
 *
 *
 --*/
#ifndef _AIFSTRUC_H
#define _AIFSTRUC_H

#include <protocol.h>

//
//	Progress report structure definitions
//
typedef enum {
	AifJobStsSuccess = 1,
	AifJobStsFinished,
	AifJobStsAborted,
	AifJobStsFailed,
	AifJobStsLastReportMarker = 100, // All before mean last report
	AifJobStsSuspended,
	AifJobStsRunning
} _E_AifJobStatus;

#ifdef AAC_32BIT_ENUMS
typedef	_E_AifJobStatus	AifJobStatus;
#else
typedef	AAC_UINT32	AifJobStatus;
#endif


typedef enum {
	AifJobScsiMin = 1,		// Minimum value for Scsi operation
	AifJobScsiZero,			// SCSI device clear operation
	AifJobScsiVerify,		// SCSI device Verify operation NO REPAIR
	AifJobScsiExercise,		// SCSI device Exercise operation
	AifJobScsiVerifyRepair,		// SCSI device Verify operation WITH repair
	// Add new SCSI task types above this line
	AifJobScsiMax = 99,		// Max Scsi value
	AifJobCtrMin,			// Min Ctr op value
	AifJobCtrZero,			// Container clear operation
	AifJobCtrCopy,			// Container copy operation
	AifJobCtrCreateMirror,		// Container Create Mirror operation
	AifJobCtrMergeMirror,		// Container Merge Mirror operation
	AifJobCtrScrubMirror,		// Container Scrub Mirror operation
	AifJobCtrRebuildRaid5,		// Container Rebuild Raid5 operation
	AifJobCtrScrubRaid5,		// Container Scrub Raid5 operation
	AifJobCtrMorph,			// Container morph operation
	AifJobCtrPartCopy,		// Container Partition copy operation
	AifJobCtrRebuildMirror,	// Container Rebuild Mirror operation
	AifJobCtrCrazyCache,		// crazy cache
	// Add new container task types above this line
	AifJobCtrMax = 199,		// Max Ctr type operation
	AifJobFsMin,			// Min Fs type operation
	AifJobFsCreate,			// File System Create operation
	AifJobFsVerify,			// File System Verify operation
	AifJobFsExtend,			// File System Extend operation
	// Add new file system task types above this line
	AifJobFsMax = 299,		// Max Fs type operation
	// Add new API task types here
	AifJobApiFormatNTFS,		// Format a drive to NTFS
	AifJobApiFormatFAT,		// Format a drive to FAT
	AifJobApiUpdateSnapshot,	// update the read/write half of a snapshot
	AifJobApiFormatFAT32,		// Format a drive to FAT32
	AifJobApiMax = 399,		// Max API type operation
	AifJobCtlContinuousCtrVerify,	// Controller operation
	AifJobCtlMax = 499		// Max Controller type operation
} _E_AifJobType;

#ifdef AAC_32BIT_ENUMS
typedef	_E_AifJobType	AifJobType;
#else
typedef	AAC_UINT32	AifJobType;
#endif

union SrcContainer {
	AAC_UINT32 from;
	AAC_UINT32 master;
	AAC_UINT32 container;
};

union DstContainer {
	AAC_UINT32 to;
	AAC_UINT32 slave;
	AAC_UINT32 container;
};


struct AifContainers {
	union SrcContainer src;
	union DstContainer dst;
};

union AifJobClient {
	
	struct AifContainers container;	// For Container nd file system progress ops;
	AAC_INT32 scsi_dh;			// For SCSI progress ops
};

struct AifJobDesc {
	AAC_UINT32 jobID;			// DO NOT FILL IN! Will be filled in by AIF
	AifJobType type;		// Operation that is being performed
	union AifJobClient client; // Details
};

struct AifJobProgressReport {
	struct AifJobDesc jd;
	AifJobStatus status;
	AAC_UINT32 finalTick;
	AAC_UINT32 currentTick;
	AAC_UINT32 jobSpecificData1;
	AAC_UINT32 jobSpecificData2;
};

//
//	Notification of events structure definition starts here
//
typedef enum {
	// General application notifies start here
	AifEnGeneric = 1,		// Generic notification
	AifEnTaskComplete,		// Task has completed
	AifEnConfigChange,		// Adapter configuration change occurred
	AifEnContainerChange,		// Adapter specific container configuration change
	AifEnDeviceFailure,		// SCSI device failed
	AifEnMirrorFailover,		// Mirror failover started
	AifEnContainerEvent,		// Significant container event
	AifEnFileSystemChange,		// File system changed
	AifEnConfigPause,		// Container pause event
	AifEnConfigResume,		// Container resume event
	AifEnFailoverChange,		// Failover space assignment changed
	AifEnRAID5RebuildDone,		// RAID5 rebuild finished
	AifEnEnclosureManagement,	// Enclosure management event
	AifEnBatteryEvent,		// Significant NV battery event
	AifEnAddContainer,		// A new container was created.
	AifEnDeleteContainer,		// A container was deleted.
	AifEnSMARTEvent,          	// SMART Event
	AifEnBatteryNeedsRecond,	// The battery needs reconditioning
	AifEnClusterEvent,		// Some cluster event
	AifEnDiskSetEvent,		// A disk set event occured.
	// Add general application notifies above this comment
	AifDriverNotifyStart=199,	// Notifies for host driver go here
	// Host driver notifications start here
	AifDenMorphComplete, 		// A morph operation completed
	AifDenVolumeExtendComplete 	// A volume expand operation completed
	// Add host driver notifications above this comment
} _E_AifEventNotifyType;

#ifdef AAC_32BIT_ENUMS
typedef _E_AifEventNotifyType	AifEventNotifyType;
#else
typedef	AAC_UINT32		AifEventNotifyType;
#endif

struct AifEnsGeneric {
	AAC_INT8 text[132];		// Generic text
};

struct AifEnsDeviceFailure {
	AAC_INT32 deviceHandle;		// SCSI device handle
};

struct AifEnsMirrorFailover {
	AAC_UINT32 container;		// Container with failed element
	AAC_UINT32 failedSlice;		// Old slice which failed
	AAC_UINT32 creatingSlice;	// New slice used for auto-create
};

struct AifEnsContainerChange {
	AAC_UINT32 container[2];	// container that changed, -1 if no container
};

struct AifEnsContainerEvent {
	AAC_UINT32 container;		// container number 
	AAC_UINT32 eventType;		// event type
};

struct AifEnsEnclosureEvent {
	AAC_UINT32 empID;		// enclosure management processor number 
	AAC_UINT32 unitID;		// unitId, fan id, power supply id, slot id, tempsensor id. 
	AAC_UINT32 eventType;		// event type
};


struct AifEnsBatteryEvent {
	NVBATT_TRANSITION transition_type;	// e.g. from low to ok
	NVBATTSTATUS current_state;		// current battery state
	NVBATTSTATUS prior_state;		// previous battery state
};

struct AifEnsDiskSetEvent {
	AAC_UINT32	eventType;
	AAC_UINT32	DsNum[2];
	AAC_UINT32	CreatorId[2];
};



typedef enum _CLUSTER_AIF_EVENT {
	CLUSTER_NULL_EVENT = 0,
	CLUSTER_PARTNER_NAME_EVENT,		// change in partner hostname or adaptername from NULL to non-NULL
						// (partner's agent may be up)
	CLUSTER_PARTNER_NULL_NAME_EVENT		// change in partner hostname or adaptername from non-null to NULL
						// (partner has rebooted)
} _E_CLUSTER_AIF_EVENT;

#ifdef AAC_32BIT_ENUMS
typedef	_E_CLUSTER_AIF_EVENT	CLUSTER_AIF_EVENT;
#else
typedef	AAC_UINT32		CLUSTER_AIF_EVENT;
#endif

struct AifEnsClusterEvent {
	CLUSTER_AIF_EVENT   eventType;
};

struct AifEventNotify {
	AifEventNotifyType			type;
	union {
		struct AifEnsGeneric		EG;
		struct AifEnsDeviceFailure	EDF;
		struct AifEnsMirrorFailover 	EMF;
		struct AifEnsContainerChange 	ECC;
		struct AifEnsContainerEvent 	ECE;
		struct AifEnsEnclosureEvent 	EEE;
		struct AifEnsBatteryEvent	EBE;
		struct AifEnsDiskSetEvent	EDS;
#ifdef BRIDGE
		struct AifEnsSMARTEvent 	ES;
#endif
		struct AifEnsClusterEvent	ECLE;
	} data;
};

//
//	Generic API structure
//
#define AIF_API_REPORT_MAX_SIZE 64
typedef AAC_INT8 AifApiReport[AIF_API_REPORT_MAX_SIZE];

//
//	For FIB communication, we need all of the following things
//	to send back to the user.
//

typedef enum {
	AifCmdEventNotify = 1,		// Notify of event
	AifCmdJobProgress,		// Progress report
	AifCmdAPIReport,		// Report from other user of API
	AifCmdDriverNotify,		// Notify host driver of event
	AifReqJobList = 100,		// Gets back complete job list
	AifReqJobsForCtr,		// Gets back jobs for specific container
	AifReqJobsForScsi,		// Gets back jobs for specific SCSI device
	AifReqJobReport,		// Gets back a specific job report or list of them
	AifReqTerminateJob,		// Terminates job
	AifReqSuspendJob,		// Suspends a job
	AifReqResumeJob,		// Resumes a job
	AifReqSendAPIReport,		// API generic report requests
	AifReqAPIJobStart,		// Start a job from the API
	AifReqAPIJobUpdate,		// Update a job report from the API
	AifReqAPIJobFinish		// Finish a job from the API
} _E_AIFCOMMAND;

#ifdef AAC_32BIT_ENUMS
typedef _E_AIFCOMMAND	AIFCOMMAND;
#else
typedef	AAC_UINT32	AIFCOMMAND;
#endif



//
//	Adapter Initiated FIB command structures. Start with the adapter
//	initiated FIBs that really come from the adapter, and get responded
//	to by the host.
//
typedef struct _AIFCOMMANDTOHOST {
	AIFCOMMAND command;				// Tell host what type of notify this is
	AAC_UINT32 seqNumber;				// To allow ordering of reports (if necessary)
	union {
		// First define data going to the adapter
		struct AifEventNotify EN;		// Event notify structure
		struct AifJobProgressReport PR[1];	// Progress report
		AifApiReport AR;
	} data;
} AIFCOMMANDTOHOST, *PAIFCOMMANDTOHOST;


#endif // _AIFSTRUC_H



