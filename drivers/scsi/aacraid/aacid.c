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
 *  aac.c
 *
 * Abstract: Data structures for controller specific info.
 *
--*/

#include "osheaders.h"

#include "AacGenericTypes.h"

#include "aac_unix_defs.h"

#include "fsatypes.h"
#include "comstruc.h"
#include "fsaport.h"
#include "pcisup.h"

#include "version.h"


/* Function Prototypes */
void InqStrCopy(char *a, char *b); /* ossup.c */

/* Device name used to register and unregister
   the device in linit.c */
char devicestr[]="aac";

char *container_types[] = {
        "None",
        "Volume",
        "Mirror",
        "Stripe",
        "RAID5",
        "SSRW",
        "SSRO",
        "Morph",
        "Legacy",
        "RAID4",
        "RAID10",             
        "RAID00",             
        "V-MIRRORS",          
        "PSEUDO R4",          
        "Unknown"
};

/* Local Structure to set SCSI inquiry data strings */
typedef struct _INQSTR {
	char vid[8];         /* Vendor ID */
	char pid[16];        /* Product ID */
	char prl[4];         /* Product Revision Level */
} INQSTR, *INQSTRP;

FSA_MINIPORT MiniPorts[];

/* Function: SetInqDataStr
 *
 * Arguments: [1] pointer to void [1] int
 *
 * Purpose: Sets SCSI inquiry data strings for vendor, product
 * and revision level. Allows strings to be set in platform dependant
 * files instead of in OS dependant driver source.
 */

void SetInqDataStr (int MiniPortIndex, void *dataPtr, int tindex)
{
	INQSTRP InqStrPtr;
	char *findit;
	FSA_MINIPORT   *mp;

	mp = &MiniPorts[MiniPortIndex];
   
	InqStrPtr = (INQSTRP)(dataPtr); /* cast dataPtr to type INQSTRP */

	InqStrCopy (mp->Vendor, InqStrPtr->vid); 
	InqStrCopy (mp->Model,  InqStrPtr->pid); /* last six chars reserved for vol type */

	findit = InqStrPtr->pid;

	for ( ; *findit != ' '; findit++); /* walk till we find a space then incr by 1 */
	        findit++;
	
	if (tindex < (sizeof(container_types)/sizeof(char *)))
		InqStrCopy (container_types[tindex], findit);

	InqStrCopy ("0001", InqStrPtr->prl);
}

int SaInitDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, u32 AdapterNumber, u32 PciBus, u32 PciSlot);
int RxInitDevice(PPCI_MINIPORT_COMMON_EXTENSION CommonExtension, u32 AdapterNumber, u32 PciBus, u32 PciSlot);

/*
 * Because of the way Linux names scsi devices, the order in this table has
 * become important.  Check for on-board Raid first, add-in cards second.
 */

FSA_MINIPORT MiniPorts[] = 
{
	{ 0x1028, 0x0001, 0x1028, 0x0001, "afa", RxInitDevice, "percraid", "DELL    ", "PERCRAID        " },	// PowerEdge 2400
	{ 0x1028, 0x0002, 0x1028, 0x0002, "afa", RxInitDevice, "percraid", "DELL    ", "PERCRAID        " },	// PowerEdge 4400
	{ 0x1028, 0x0003, 0x1028, 0x0003, "afa", RxInitDevice, "percraid", "DELL    ", "PERCRAID        " },	// PowerEdge 2450
	{ 0x1011, 0x0046, 0x9005, 0x1364, "afa", SaInitDevice, "percraid", "DELL    ", "PERCRAID        " },	// Dell PERC2 "Quad Channel"
	{ 0x1011, 0x0046, 0x103c, 0x10c2, "hpn", SaInitDevice, "hpnraid",  "HP      ", "NetRAID-4M      " }	// HP NetRAID-4M
};


#define NUM_MINIPORTS	(sizeof(MiniPorts) / sizeof(FSA_MINIPORT))

int NumMiniPorts = NUM_MINIPORTS;

char DescriptionString[] =	"AACxxx Raid Controller" FSA_VERSION_STRING ;
