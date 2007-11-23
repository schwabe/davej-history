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
 *   revision.h
 *
 * Abstract: This module contains all of the revision information for
 *	the FSA product, as well as the support routines for
 *	checking module compatibility.
 *
 *	Before editing anything in this module, make sure that
 *	you read the comments. Some lines are changed automatically
 *	as part of the build, and should never be changed by hand.
 *
 * Routines (all inlines):
 *
 *	RevGetBuildNumber - Retrieve current build number
 *	RevGetExternalRev - Retrieve revision for external use
 *	RevGetFullRevision - Retrieve full revision structure
 *
 *	RevCheckCompatibility - Checks compatibility base on internal table
 *
 * 	RevCheckCompatibilityFullInfo - Check for static component
 *	RevGetCompInfoTableSize - Get size for static component table
 *	RevGetCompInfoTable - Get actual table to place on static component
 *	RevGetBuildNumberFromInfo - Get build number for static component.
 *
 *
 *	
 --*/

#ifndef _REVISION_H
#define _REVISION_H


#include "version.h" // revision numbers kept separate so they can be used by resource compiler as well

typedef int REV_BOOL;

#define REV_TRUE 1
#define REV_FALSE 0

//
//	Define Revision Levels for this product
//
//  IMPORTANT: Do NOT modify BUILD_NUMBER define, this is modified
//			   automatically by the build.
//
//  Version is VMAJOR.MINOR-DASH TYPE (Build BUILD_NUMBER)
//
//	IMPORTANT: Don't access these revisions directly. They can be
//			   accessed via, the RevGetXxxxx rouines.
//

#define REV_AS_LONGWORD \
	((REV_MAJOR << 24) | (REV_MINOR << 16) | (REV_TYPE << 8) | (REV_DASH))



#ifndef BIOS

//
//	Enumerate the types of product levels we can have
//
enum {
	RevType_Devo=1,		// Development mode, testing all of latest
	RevType_Alpha,		// Alpha - Internal field test
	RevType_Beta,		// Beta - External field test
	RevType_Release		// Release - Retail version
};

//
//	Define the basic structure for all revision information. Note
//	that the ordering of the components is such that they should
//	always increase. dash will be updated the most, then the version
//	type, then minor and major.
//
typedef struct {
	union {
		struct {
			unsigned char dash;	// Dash version number
			unsigned char type;	// Type, 1=Devo, 2=Alpha, 3=Beta, 4=Release
			unsigned char minor;// Minor version minor
			unsigned char major;// Major version number
		} comp;				// Components to external viewed rev number
		unsigned long ul;	// External revision as single 32-bit value
	} external;			// External revision number (union)
	unsigned long buildNumber; // Automatically generated build number
} FsaRevision;


//
//	Define simple routines to get basic revision information. The
//	definitions should never be accessed directly. These routines
//	are meant to be used to access all relevant information no matter
//	how simple.
//

static inline unsigned long RevGetBuildNumber(void) {return REV_BUILD_NUMBER;}
static inline unsigned long RevGetExternalRev(void) {return REV_AS_LONGWORD;}

//
//	Enumerate different components that may have to check
//	compatibility. This list of components can be changed
//	at any time.
//
//	IMPORTANT: ONLY add to the END of this enum structure. Otherwise,
//			   incompatibilities between component rev checking will
//			   cause wrong checking results.
//
typedef enum {
	RevApplication = 1,	// Any user End application
	RevDkiCli,			// ADAPTEC proprietary interface (knows FIBs)
	RevNetService,		// Network Service Revision (under API)
	RevApi,				// ADAPTEC User mode API
	RevFileSysDriver,	// FSA File System Driver
	RevMiniportDriver,	// FSA File System Miniport Driver
	RevAdapterSW,		// Adapter Software (or NT Simulator)
	RevMonitor,			// Monitor for adapter hardware (MON960 for now)
	RevRemoteApi		// The remote API.
	// ALWAYS ADD NEW COMPONENTS HERE - AT END
} RevComponent;

//
//	Define a structure so that we can create a compatibility table.
//
typedef struct {
	RevComponent A,B;
	unsigned long BuildNumOfB_RequiredByA;
	unsigned long BuildNumOfA_RequiredByB;
} RevCompareElement;

//
//	Now, define the table. This table should only be included once,
//	in one program. If it is linked from 2 modules, there will likely
//	be a multiply defined symbol error from the linker.
//
//	To fix this problem, REV_REFERENCE_ONLY can be defined. This will
//	allow access to the revision information table without a redefinition
//	of the tables.
//
extern const int			   RevCompareTableLength;

extern const RevCompareElement RevCompareTable[];

/********************************************************************\
* Routine: RevCheckCompatibility(callerComp,compB,compB_BuildNumber)
*
*	The following routine is used to check compatibility between
*	the calling component and a component that has some dependencies
*	on it. If this routine returns REV_FALSE, it is expected that the caller
*	will send an appropriate incompatibility message and stop.
*
*	This routine is only meant to check for compatibility in the
*	absolute sense. If code wishes to execute a different path based
*	on the CompB_BuildNumber, then this routine is not useful. The
*	routine RevGetBuildNumber can be used to get the calling module's
*	current build number for a comparison check.
*
*	The return value is REV_TRUE, if compatibility is possible, and REV_FALSE
*	if the components are definitely not compatible, or there is an
*	error when trying to figure it out. To be more specific:
*
*		1) REV_TRUE if component B is newer than calling component. (In this
*		   case, the revision check done by component B with respect to
*		   this component will give the real compatibility information.
*		   It is the only one with the knowledge, since this component
*		   could not look into the future.)
*		2) REV_TRUE if calling component is more recent and table shows okay
*		3) REV_FALSE if calling component more recent and table show not okay
*		4) REV_FALSE if calling component is more recent and table entry to
*		   check does not exist.
*
*	Note that the CompB_BuildNumber must be attained by the calling
*	routine through some mechanism done by the caller.
*
* Input:
*
*	callerComp - Name of component making this call
*	compB - Name of component to check compatibility with
*	compB_BuildNumber - Build number to component B
*
* Output:
*
*	None
*
* Return Value:
*
*	REV_TRUE - Component compatibility is possible, continue as usual. compB
*		   must give true compatibility information.
*	REV_FALSE - Incompatible components, notify and end
*
\********************************************************************/
static inline REV_BOOL RevCheckCompatibility(
		RevComponent callerComp,
		RevComponent compB,
		unsigned long compB_BuildNumber)
{
	int i;
	unsigned long RevForB;

	//
	//	Compatibility check is possible, so we should continue. When
	//	compB makes this call in its own component, it will get the
	//	true compatibility information, since only it can know.
	//
	if (RevGetBuildNumber() < compB_BuildNumber) return REV_TRUE;

	//
	//	Go through rev table. When the components are found in the
	//	same table entry, return the approprate number.
	//
	for (i=0; i<RevCompareTableLength; i++) {
		if (RevCompareTable[i].A == callerComp) {
			if (RevCompareTable[i].B == compB) {
				RevForB = RevCompareTable[i].BuildNumOfB_RequiredByA;
				return (compB_BuildNumber >= RevForB);
			}
		} else if (RevCompareTable[i].B == callerComp) {
			if (RevCompareTable[i].A == compB) {
				RevForB = RevCompareTable[i].BuildNumOfA_RequiredByB;
				return (compB_BuildNumber >= RevForB);
			}
		}
	}

	//
	//	Uh oh! No relevant table entry was found (this should never
	//	happen).
	//
	return REV_FALSE;
}


//
//	Now create a structure that can be used by a FIB to check
//	compatibility.
//
typedef struct _RevCheck {
	RevComponent callingComponent;
	FsaRevision callingRevision;
} RevCheck;

typedef struct _RevCheckResp {
	REV_BOOL possiblyCompatible;
	FsaRevision adapterSWRevision;
} RevCheckResp;

#endif /* bios */
#endif /* _REVISION_H */

//
//	The following allows for inclusion of revision.h in other h
//	files. when you include this file in another h file, simply
//	define REV_REFERENCE_ONLY. This will be undefined later, so that
//	the single C file inclusion in the module will be used to
//	implement the global structures.
//
#ifndef REV_REFERENCE_ONLY
#ifndef _REVISION_H_GLOBAL
#define _REVISION_H_GLOBAL



//
//	The following array is the table of compatibility. This table
//	can be modified in two ways:
//
//		1) A component which has an incompatible change done to
//		   it, can get a new build number.
//
//		2) A new component can be added, requiring more entries
//		   to be place into this table.
//
//
//	In case (1), you must change the revision number in the appropriate
//	column, based on which component absolutely requires an upgrade.
//
//	Example: A new FIB used by the API, in build number 105
//		{RevApi,	RevAdapterSW,		100,  100}
//			---> would be changed to <---
//		{RevApi,	RevAdapterSW,		105,  100}
//
//	Example: A structure is changed for a FIB that only the API uses
//		{RevApi,	RevAdapterSW,		100,  100}
//			---> would be changed to <---
//		{RevApi,	RevAdapterSW,		105,  105}
//
//
//	In case (2), the less common case, the enumerated list of
//	components must be changed to include the new component. Then
//	entries need to be placed into this table.
//
//	Since the revisions must be communicated between the two
//	components, it is likely that you would need to put in the
//	current build number for both columns. That is the recommended
//	way to start revision test.
//
const RevCompareElement RevCompareTable[] = {
	// Component A		Component B			MinBForA	MinAForB
	// -----------		-----------			--------	--------
	{RevApplication,	RevApi,				2120,		2120	},
	{RevDkiCli,		RevApi,				2120,		2120	},
	{RevDkiCli,		RevFileSysDriver,	        257,		257	},
	{RevDkiCli,		RevMiniportDriver,	        257,		257	},
	{RevDkiCli,		RevAdapterSW,		        257,		257	},
	{RevApi,		RevFileSysDriver,	        2120,		2120	},
	{RevApi,		RevMiniportDriver,	        2120,		2120	},
	{RevApi,		RevAdapterSW,		        2120,		2120	},
	{RevApi,		RevNetService,		        2120,		2120	},
	{RevFileSysDriver,	RevMiniportDriver,	        100,		100	},
	{RevFileSysDriver,	RevAdapterSW,		        257,		257	},
	{RevMiniportDriver,	RevAdapterSW,		        257,		257	},
	{RevMiniportDriver,	RevMonitor,			100,		100	},
	{RevApi,		RevNetService,		        2120,		2120	},
	{RevApi,		RevRemoteApi,		        2120,		2120	},
	{RevNetService,		RevRemoteApi,		        2120,		2120	}
};

const int RevCompareTableLength = sizeof(RevCompareTable)/sizeof(RevCompareElement);

#endif /* _REVISION_H_GLOBAL */
#endif /* REV_REFERENCE_ONLY */
#undef REV_REFERENCE_ONLY







