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
 *   linit.h
 *
 * Abstract: Header file for Linux Driver for Adaptec RAID Array Controller
 *
 --*/
/*------------------------------------------------------------------------------
 *              I N C L U D E S
 *----------------------------------------------------------------------------*/

#ifndef _LINIT_H_
#define _LINIT_H_

#include <linux/config.h>

/*------------------------------------------------------------------------------
 *              D E F I N E S
 *----------------------------------------------------------------------------*/
/* Define the AAC SCSI Host Template structure. */
#define AAC_HOST_TEMPLATE_ENTRY	\
{\
	proc_dir:       &AAC_ProcDirectoryEntry, /* ProcFS Directory Entry */ \
	proc_info:      AAC_ProcDirectoryInfo,   /* ProcFS Info Function   */ \
	name:           "AAC",                   /* Driver Name            */ \
	detect:         AAC_DetectHostAdapter,   /* Detect Host Adapter    */ \
	release:        AAC_ReleaseHostAdapter,  /* Release Host Adapter   */ \
	info:           AAC_DriverInfo,          /* Driver Info Function   */ \
	ioctl:          AAC_Ioctl,               /* ioctl Interface        */ \
	command:        AAC_Command,             /* unqueued command       */ \
	queuecommand:   AAC_QueueCommand,        /* Queue Command Function */ \
	abort:          AAC_AbortCommand,        /* Abort Command Function */ \
	reset:          AAC_ResetCommand,        /* Reset Command Function */ \
	bios_param:     AAC_BIOSDiskParameters,  /* BIOS Disk Parameters   */ \
	can_queue:      1,                       /* Default initial value  */ \
	this_id:        0,                       /* Default initial value  */ \
	sg_tablesize:   0,                       /* Default initial value  */ \
	cmd_per_lun:    0,                       /* Default initial value  */ \
	present:        0,                       /* Default initial value  */ \
	unchecked_isa_dma: 0,                    /* Default Initial Value  */ \
	use_new_eh_code:         0,              /* Default initial value      */ \
	eh_abort_handler:        AAC_AbortCommand,   /* New Abort Command func     */ \
	eh_strategy_handler:     NULL,           /* New Strategy Error Handler */ \
	eh_device_reset_handler: NULL,           /* New Device Reset Handler   */ \
	eh_bus_reset_handler:    NULL,           /* New Bus Reset Handler      */ \
	eh_host_reset_handler:   NULL,           /* New Host reset Handler     */ \
	use_clustering: ENABLE_CLUSTERING        /* Disable Clustering      */ \
}


/*------------------------------------------------------------------------------
 *              T Y P E D E F S / S T R U C T S
 *----------------------------------------------------------------------------*/

typedef struct AAC_BIOS_DiskParameters
{
	int heads;
	int sectors;
	int cylinders;
} AAC_BIOS_DiskParameters_T;


/*------------------------------------------------------------------------------
 *              P R O G R A M   G L O B A L S
 *----------------------------------------------------------------------------*/

const char *AAC_DriverInfo( struct Scsi_Host * );
extern struct proc_dir_entry AAC_ProcDirectoryEntry;



/*------------------------------------------------------------------------------
 *              F U N C T I O N   P R O T O T Y P E S
 *----------------------------------------------------------------------------*/
/* Define prototypes for the AAC Driver Interface Functions. */
int AAC_DetectHostAdapter(Scsi_Host_Template *);
int AAC_ReleaseHostAdapter(struct Scsi_Host *);
int AAC_QueueCommand(Scsi_Cmnd *, void ( *CompletionRoutine)(Scsi_Cmnd *));
int AAC_Command( Scsi_Cmnd *);
int AAC_ResetCommand(Scsi_Cmnd *, unsigned int);
int AAC_BIOSDiskParameters(Disk *, kdev_t, int * );
int AAC_ProcDirectoryInfo(char *, char **, off_t, int, int, int);
int AAC_Ioctl(Scsi_Device *, int, void *);
void AAC_SelectQueueDepths(struct Scsi_Host *, Scsi_Device *);
int AAC_AbortCommand( Scsi_Cmnd *scsi_cmnd_ptr );

#endif /* _LINIT_H_ */
