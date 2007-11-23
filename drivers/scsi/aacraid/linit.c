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
 *   linit.c
 *
 * Abstract: Linux Driver entry module for Adaptec RAID Array Controller
 *				
 *	Provides the following driver entry points:
 *		AAC_DetectHostAdapter()
 *		AAC_ReleaseHostAdapter()
 *		AAC_QueueCommand()
 *		AAC_ResetCommand()
 *		AAC_BIOSDiskParameters()
 *		AAC_ProcDirectoryInfo()
 *	
 --*/

/*------------------------------------------------------------------------------
 *              D E F I N E S
 *----------------------------------------------------------------------------*/
#define AAC_DRIVER_VERSION				"0.1.1"
#define AAC_DRIVER_BUILD_DATE			__DATE__
#define MAX_DRIVER_QUEUE_DEPTH			500

/*------------------------------------------------------------------------------
 *              I N C L U D E S
 *----------------------------------------------------------------------------*/
#include "osheaders.h"

#include "AacGenericTypes.h"

#ifdef MODULE
#include <linux/module.h>
#endif
#include "sd.h"
#include "linit.h"
#include "aac_unix_defs.h"
#include "fsatypes.h"
#include "comstruc.h"
#include "fsaport.h"
#include "pcisup.h"
#include "port.h"

/*------------------------------------------------------------------------------
 *              G L O B A L S
 *----------------------------------------------------------------------------*/
extern FSA_MINIPORT MiniPorts[];
extern int CommPrinting;
extern char DescriptionString[];
extern char devicestr[];

/*------------------------------------------------------------------------------
 *              M O D U L E   G L O B A L S
 *----------------------------------------------------------------------------*/
aac_options_t g_options = { CMN_ERR_LEVEL, 0 };	// default message_level

char g_DriverName[] = { "aacraid" };
#define module_options aacraid_options
static char * aacraid_options = NULL;

/* AAC_ProcDirectoryEntry is the  /proc/scsi directory entry.*/
static struct proc_dir_entry AAC_ProcDirectoryEntry = {
	PROC_SCSI_SCSI, 3, "aacraid", S_IFDIR | S_IRUGO | S_IXUGO, 2,
	0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL 
};

PCI_MINIPORT_COMMON_EXTENSION *g_CommonExtensionPtrArray[ MAXIMUM_NUM_ADAPTERS ];

unsigned g_HostAdapterCount = 0;
unsigned g_chardev_major = 0;
int g_single_command_done = FALSE;

/*------------------------------------------------------------------------------
 *              F U N C T I O N   P R O T O T Y P E S
 *----------------------------------------------------------------------------*/

int AacHba_Ioctl(PCI_MINIPORT_COMMON_EXTENSION *CommonExtension,int cmd, void *arg);
int AacHba_ProbeContainers(PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr);
int AacHba_DoScsiCmd(Scsi_Cmnd *scsi_cmnd_ptr, int wait);
void AacHba_DetachAdapter(void *AdapterArg );
int AacHba_ClassDriverInit(PCI_MINIPORT_COMMON_EXTENSION * CommonExtensionPtr);
void AacHba_AbortScsiCommand(Scsi_Cmnd *scsi_cmnd_ptr);


/*------------------------------------------------------------------------------
 *              L O C A L   F U N C T I O N   P R O T O T Y P E S
 *----------------------------------------------------------------------------*/
static int parse_keyword(char ** str_ptr, char * keyword );
static void AAC_ParseDriverOptions(char * cmnd_line_options_str);
static void AAC_AnnounceDriver(void);
int AAC_ChardevIoctl(struct inode * inode_ptr, struct file * file_ptr, unsigned int cmd, unsigned long arg);
int AAC_ChardevOpen(struct inode * inode_ptr, struct file * file_ptr );
int AAC_ChardevRelease(struct inode * inode_ptr, struct file * file_ptr );

struct file_operations AAC_fops = {
	NULL,				// lseek
	NULL,				// read
	NULL,				// write
	NULL,				// readdir
	NULL,				// poll
	AAC_ChardevIoctl,	// ioctl
	NULL,				// mmap
	AAC_ChardevOpen,	// open
	NULL,				// flush
	AAC_ChardevRelease,	// release
	NULL,				// fsync
	NULL,				// fasync
	NULL,				// check media change
	NULL,				// revalidate
	NULL				// lock
};

/*------------------------------------------------------------------------------
 *              F U N C T I O N S
 *----------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
	AAC_AnnounceDriver()

		Announce the driver name, version and date.
 *----------------------------------------------------------------------------*/

static void AAC_AnnounceDriver( void )
{
	printk(KERN_INFO "%s, %s\n", "aacraid raid driver version", AAC_DRIVER_BUILD_DATE );
	schedule();	/* ??? !!! */
}


/*------------------------------------------------------------------------------
	AAC_DetectHostAdapter()

		Probe for AAC Host Adapters initialize, register, and report the 
		configuration of each AAC Host Adapter found.

	Preconditions:
	Postconditions:
		- Returns the number of adapters successfully initialized and 
		registered.
		- Initialize all data necessary for this particular SCSI driver.
	Notes:
		The detect routine must not call any of the mid level functions 
		to queue commands because things are not guaranteed to be set 
		up yet. The detect routine can send commands to the host adapter 
		as long as the program control will not be passed to scsi.c in 
		the processing of the command. Note especially that 
		scsi_malloc/scsi_free must not be called.
 *----------------------------------------------------------------------------*/

int AAC_DetectHostAdapter(Scsi_Host_Template *HostTemplate)
{
	int index;
	int ContainerId;
	uint16_t vendor_id, device_id, sub_vendor_id, sub_system_id;
	struct Scsi_Host *host_ptr;
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr;
	struct pci_dev *dev = NULL;
	extern int NumMiniPorts;
	fsadev_t *fsa_dev_ptr;
	char *DeviceName;
	struct pci_dev *devp;
	int	first_index, last_index, increment;
	CommPrinting = TRUE;

	AAC_AnnounceDriver();
	if( module_options != NULL )
		AAC_ParseDriverOptions( module_options );

	// NumMiniPorts & MiniPorts[] defined in aacid.c
	if (g_options.reverse_scan == 0) {
		first_index = 0;
		last_index  = NumMiniPorts;
		increment   = 1;
	} else {
		first_index = NumMiniPorts -1;
		last_index  = -1;
		increment   = -1;
	}

	for( index = first_index; index != last_index; index += increment )
	{
		device_id = MiniPorts[index].DeviceId;
		vendor_id = MiniPorts[index].VendorId;
		DeviceName = MiniPorts[index].DeviceName;
		cmn_err(CE_DEBUG, "Checking %s %x/%x/%x/%x", 
					DeviceName,
					vendor_id, 
					device_id,
					MiniPorts[index].SubVendorId,
					MiniPorts[index].SubSystemId);


		// pci_find_device traverses the pci_devices linked list for devices
		// with matching vendor and device ids.

		dev = NULL;	// start from beginning of list
		while( ( dev = pci_find_device( vendor_id, device_id, dev ) ) )
		{
			if( pci_read_config_word( dev, PCI_SUBSYSTEM_VENDOR_ID, &sub_vendor_id ) ){
				cmn_err(CE_WARN, "pci_read_config_word SUBSYS_VENDOR_ID failed");
				break;
			}
			if( pci_read_config_word( dev, PCI_SUBSYSTEM_ID, &sub_system_id ) ){
				cmn_err(CE_WARN, "pci_read_config_work SUBSYSTEM_ID failed");
				break;
			}

			cmn_err(CE_DEBUG, "     found: %x/%x/%x/%x", vendor_id, device_id, sub_vendor_id, sub_system_id);
			if( ( sub_vendor_id != MiniPorts[index].SubVendorId ) || 
				( sub_system_id != MiniPorts[index].SubSystemId ) ){
					continue;
			}
			
			printk(KERN_INFO "%s device detected\n", DeviceName );
			// cmn_err(CE_WARN, "%x/%x/%x/%x", vendor_id, device_id, sub_vendor_id, sub_system_id);

			// Increment the host adapter count
			g_HostAdapterCount++;

			// scsi_register() allocates memory for a Scsi_Hosts structure and
			// links it into the linked list of host adapters. This linked list
			// contains the data for all possible <supported> scsi hosts.
			// This is similar to the Scsi_Host_Template, except that we have
			// one entry for each actual physical host adapter on the system,
			// stored as a linked list. If there are two AAC boards, then we
			// will need to make two Scsi_Host entries, but there will be only
			// one Scsi_Host_Template entry. The second argument to scsi_register()
			// specifies the size of the extra memory we want to hold any device 
			// specific information.
			host_ptr = scsi_register( HostTemplate, 
				sizeof( PCI_MINIPORT_COMMON_EXTENSION ) );

			// These three parameters can be used to allow for wide SCSI 
			// and for host adapters that support multiple buses.
			host_ptr->max_id = 17;
			host_ptr->max_lun = 8;
			host_ptr->max_channel = 1;

			host_ptr->irq = dev->irq;		// Adapter IRQ number
			host_ptr->base = ( char * )(dev->base_address[0] & ~0xff);

			cmn_err( CE_DEBUG, "Device base address = 0x%lx [0x%lx]", host_ptr->base, dev->base_address[0] );
			cmn_err( CE_DEBUG, "Device irq = 0x%lx", dev->irq );

			// The unique_id field is a unique identifier that must be assigned
			// so that we have some way of identifying each host adapter properly
			// and uniquely. For hosts that do not support more than one card in the
			// system, this does not need to be set. It is initialized to zero in
			// scsi_register(). This is the value returned from OsGetDeviceInstance().
			host_ptr->unique_id = g_HostAdapterCount - 1;

			host_ptr->this_id = 16;			// SCSI Id for the adapter itself

			// Set the maximum number of simultaneous commands supported by the driver.
			host_ptr->can_queue = MAX_DRIVER_QUEUE_DEPTH;

			// Define the maximum number of scatter/gather elements supported by 
			// the driver. 
			host_ptr->sg_tablesize = 17;

			host_ptr->cmd_per_lun = 1;		// untagged queue depth

			// This function is called after the device list has been built to find
			// tagged queueing depth supported for each device.
			host_ptr->select_queue_depths = AAC_SelectQueueDepths;

			CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )host_ptr->hostdata;
			
			// attach a pointer back to Scsi_Host
			CommonExtensionPtr->OsDep.scsi_host_ptr = host_ptr;	
			CommonExtensionPtr->OsDep.MiniPortIndex =  index;

			// Initialize the ordinal number of the device to -1
			fsa_dev_ptr = &( CommonExtensionPtr->OsDep.fsa_dev );
			for( ContainerId = 0; ContainerId < MAXIMUM_NUM_CONTAINERS; ContainerId++ )
				fsa_dev_ptr->ContainerDevNo[ContainerId] = -1;

			// Call initialization routine
			if( ( *MiniPorts[index].InitRoutine )
				( CommonExtensionPtr, host_ptr->unique_id, dev->bus->number, 0 ) != 0 )
			{
				// device initialization failed
				cmn_err( CE_WARN, "%s:%d device initialization failed", DeviceName, host_ptr->unique_id );
				scsi_unregister( host_ptr );
				g_HostAdapterCount--;
			}
			else
			{
				cmn_err( CE_NOTE, "%s:%d device initialization successful", DeviceName, host_ptr->unique_id  );
				AacHba_ClassDriverInit( CommonExtensionPtr );
				cmn_err(CE_NOTE, "%d:%d AacHba_ClassDriverInit complete", DeviceName, host_ptr->unique_id);
				AacHba_ProbeContainers( CommonExtensionPtr );
				cmn_err( CE_DEBUG, "Probe containers completed" );
				g_CommonExtensionPtrArray[ g_HostAdapterCount - 1 ] = CommonExtensionPtr;
				// OsSleep( 1 );
			}
		}
	}

	if( g_HostAdapterCount )
		if( !( g_chardev_major = register_chrdev( 0, devicestr, &AAC_fops ) ) )
			cmn_err( CE_WARN, "%s: unable to register %s device", DeviceName, devicestr);

	HostTemplate->present = g_HostAdapterCount; // # of cards of this type found
	return( g_HostAdapterCount ); 
}


/*------------------------------------------------------------------------------
	AAC_ReleaseHostAdapter()

		Release all resources previously acquired to support a specific Host 
		Adapter and unregister the AAC Host Adapter.
 *----------------------------------------------------------------------------*/
int AAC_ReleaseHostAdapter(struct Scsi_Host *host_ptr)
/*----------------------------------------------------------------------------*/
{
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr;

	cmn_err( CE_DEBUG, "AAC_ReleaseHostAdapter" );
	CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )host_ptr->hostdata;

	// kill any threads we started
	kill_proc(CommonExtensionPtr->OsDep.thread_pid, SIGKILL, 0);

	// Call the comm layer to detach from this adapter
	AacHba_DetachAdapter( CommonExtensionPtr->Adapter );

	// remove interrupt binding
	OsDetachInterrupt( CommonExtensionPtr->MiniPort );
	SaDetachDevice( CommonExtensionPtr );

	// unregister adapter
	scsi_unregister( host_ptr );

	if( g_chardev_major )
	{
		unregister_chrdev( g_chardev_major, devicestr );
		g_chardev_major = 0;
	}
	return( 0 ); // #REVISIT# return code
}


/*------------------------------------------------------------------------------
	AAC_QueueCommand()

		Queues a command for execution by the associated Host Adapter.
 *----------------------------------------------------------------------------*/

int AAC_QueueCommand(Scsi_Cmnd *scsi_cmnd_ptr, void ( *CompletionRoutine )( Scsi_Cmnd * ) )
{
	scsi_cmnd_ptr->scsi_done = CompletionRoutine;
	// AacHba_DoScsiCmd() handles command processing, setting the 
	// result code and calling completion routine. 
#ifdef SYNC_FIB
	if( AacHba_DoScsiCmd( scsi_cmnd_ptr, 1 ) )	// called with wait = TRUE
#else
	if( AacHba_DoScsiCmd( scsi_cmnd_ptr, 0 ) )	// called with wait = FALSE
#endif
		cmn_err( CE_DEBUG, "AacHba_DoScsiCmd failed" );
	return 0;
} 


/*------------------------------------------------------------------------------
	AAC_Done()

		Callback function for a non-queued command.

	Postconditions
		Sets g_single_command done to TRUE
 *----------------------------------------------------------------------------*/

static void AAC_Done( Scsi_Cmnd * scsi_cmnd_ptr ) 
{
	g_single_command_done = TRUE;
}


/*------------------------------------------------------------------------------
	AAC_Command()
	
		Accepts a single command for execution by the associated Host Adapter.

	Postconditions
		Returns an int where:
		Byte 0 = SCSI status code
		Byte 1 = SCSI 1 byte message
		Byte 2 = host error return
		Byte 3 = mid level error return
 *----------------------------------------------------------------------------*/

int AAC_Command(Scsi_Cmnd *scsi_cmnd_ptr )
{
	scsi_cmnd_ptr->scsi_done = AAC_Done;
	cmn_err( CE_DEBUG, "AAC_Command" );

	// AacHba_DoScsiCmd() handles command processing, setting the 
	// result code and calling completion routine.
	g_single_command_done = FALSE;

	AacHba_DoScsiCmd( scsi_cmnd_ptr, 0 );
	while( !g_single_command_done );
	return( scsi_cmnd_ptr->result );
} 


/*------------------------------------------------------------------------------
	AAC_AbortCommand()

		Abort command if possible.
 *----------------------------------------------------------------------------*/

int AAC_AbortCommand(Scsi_Cmnd *scsi_cmnd_ptr)
{
	int target = scsi_cmnd_ptr->target;
	int hba = scsi_cmnd_ptr->host->unique_id;
	int result = 0;
	u16 interrupt_status;
	PCI_MINIPORT_COMMON_EXTENSION *cep;
	char   *name;

	cmn_err( CE_WARN, "%s:%d ABORT", g_DriverName, hba, target );
	AacHba_AbortScsiCommand( scsi_cmnd_ptr );

	cep = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	name = MiniPorts[cep->OsDep.MiniPortIndex].DeviceName; 

	/*
		cmn_err( CE_WARN, "%s:%d Unable to abort command to target %d - "
		  "command already completed", name, hba, target);
		result = SCSI_ABORT_NOT_RUNNING;

		cmn_err(CE_WARN, "%s:%d Unable to abort command to target %d - "
		   "no command found\n", name, hba, target);
		result = SCSI_ABORT_NOT_RUNNING;

		cmn_err(CE_WARN, "%s:%d Unable to abort command to target %d - "
		   "command reset\n", name, hba, target);
		result = SCSI_ABORT_PENDING;

		cmn_err(CE_WARN, "%s:%d Unable to abort command to target %d - "
		   "abort tag not supported\n", name, hba, target);
		result = SCSI_ABORT_SNOOZE;

		cmn_err(CE_WARN, "%s:%d Aborting command to target %d - pending",
		   name, hba, target);
		result = SCSI_ABORT_PENDING;

		cmn_err(CE_WARN, "%s:%d Unable to abort command to target %d",
		   name, hba, target);
		result = SCSI_ABORT_BUSY;

		cmn_err(CE_WARN, "%s:%d Aborted command to target %d\n",
		   name, hba, target);
		result = SCSI_ABORT_SUCCESS;
	*/

	// Abort not supported yet
	result = SCSI_ABORT_BUSY;
	return result;
}


/*------------------------------------------------------------------------------
	AAC_ResetCommand()

		Reset command handling.
 *----------------------------------------------------------------------------*/

int AAC_ResetCommand( struct scsi_cmnd *scsi_cmnd_ptr,  unsigned int reset_flags )
{
	int target = scsi_cmnd_ptr->target;
	int hba = scsi_cmnd_ptr->host->unique_id;
	PCI_MINIPORT_COMMON_EXTENSION *cep;
	char *name;

	cep = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	name = MiniPorts[cep->OsDep.MiniPortIndex].DeviceName;
	cmn_err( CE_WARN, "%s:%d RESET", name, hba, target );
	return SCSI_RESET_PUNT;
}


/*------------------------------------------------------------------------------
	AAC_DriverInfo()

		Returns the host adapter name
 *----------------------------------------------------------------------------*/

const char *AAC_DriverInfo(struct Scsi_Host *host_ptr)
{
	PCI_MINIPORT_COMMON_EXTENSION *cep;
	char *name;

	cep = ( PCI_MINIPORT_COMMON_EXTENSION * )( host_ptr->hostdata );
	name = MiniPorts[cep->OsDep.MiniPortIndex].DeviceName;

	cmn_err( CE_DEBUG, "AAC_DriverInfo" );
	return name;
}


/*------------------------------------------------------------------------------
	AAC_BIOSDiskParameters()

		Return the Heads/Sectors/Cylinders BIOS Disk Parameters for Disk.  
		The default disk geometry is 64 heads, 32 sectors, and the appropriate 
		number of cylinders so as not to exceed drive capacity.  In order for 
		disks equal to or larger than 1 GB to be addressable by the BIOS
		without exceeding the BIOS limitation of 1024 cylinders, Extended 
		Translation should be enabled.   With Extended Translation enabled, 
		drives between 1 GB inclusive and 2 GB exclusive are given a disk 
		geometry of 128 heads and 32 sectors, and drives above 2 GB inclusive 
		are given a disk geometry of 255 heads and 63 sectors.  However, if 
		the BIOS detects that the Extended Translation setting does not match 
		the geometry in the partition table, then the translation inferred 
		from the partition table will be used by the BIOS, and a warning may 
		be displayed.
 *----------------------------------------------------------------------------*/

int AAC_BIOSDiskParameters(Scsi_Disk *scsi_disk_ptr,  kdev_t device, int *parameter_ptr )
{
	AAC_BIOS_DiskParameters_T *disk_parameters = ( AAC_BIOS_DiskParameters_T *)parameter_ptr;
	struct buffer_head * buffer_head_ptr;

	cmn_err( CE_DEBUG, "AAC_BIOSDiskParameters" );

	// Assuming extended translation is enabled - #REVISIT#
	if( scsi_disk_ptr->capacity >= 2 * 1024 * 1024 ) // 1 GB in 512 byte sectors
	{
		if( scsi_disk_ptr->capacity >= 4 * 1024 * 1024 ) // 2 GB in 512 byte sectors
		{
			disk_parameters->heads = 255;
			disk_parameters->sectors = 63;
		}
		else
		{
			disk_parameters->heads = 128;
			disk_parameters->sectors = 32;
		}
	}
	else
	{
		disk_parameters->heads = 64;
		disk_parameters->sectors = 32;
	}

	disk_parameters->cylinders = scsi_disk_ptr->capacity /
		(disk_parameters->heads * disk_parameters->sectors);

	// Read the first 1024 bytes from the disk device
	buffer_head_ptr = bread( MKDEV( MAJOR( device ), 
			MINOR( device ) & ~0x0F ), 
			0, 1024 );

	if( buffer_head_ptr == NULL )
		return( 0 );
	/* 
		If the boot sector partition table is valid, search for a partition 
		table entry whose end_head matches one of the standard geometry 
		translations ( 64/32, 128/32, 255/63 ).
	*/
	if( *( unsigned short * )( buffer_head_ptr->b_data + 0x1fe ) == 0xaa55 )
	{
		struct partition *first_partition_entry = ( struct partition * )( buffer_head_ptr->b_data + 0x1be );
		struct partition *partition_entry = first_partition_entry;
		int saved_cylinders = disk_parameters->cylinders;
		int partition_number;
		unsigned char partition_entry_end_head, partition_entry_end_sector;

		for( partition_number = 0; partition_number < 4; partition_number++ )
		{
			partition_entry_end_head   = partition_entry->end_head;
			partition_entry_end_sector = partition_entry->end_sector & 0x3f;

			if( partition_entry_end_head == ( 64 - 1 ) )
			{
				disk_parameters->heads = 64;
				disk_parameters->sectors = 32;
				break;
			}
			else if( partition_entry_end_head == ( 128 - 1 ) )
			{
				disk_parameters->heads = 128;
				disk_parameters->sectors = 32;
				break;
			}
			else if( partition_entry_end_head == ( 255 - 1 ) ) 
			{
				disk_parameters->heads = 255;
				disk_parameters->sectors = 63;
				break;
			}
			partition_entry++;
		}

		if( partition_number == 4 )
		{
			partition_entry_end_head   = first_partition_entry->end_head;
			partition_entry_end_sector = first_partition_entry->end_sector & 0x3f;
		}

		disk_parameters->cylinders = scsi_disk_ptr->capacity /
			(disk_parameters->heads * disk_parameters->sectors);

		if( partition_number < 4 && partition_entry_end_sector == disk_parameters->sectors)
		{
			if( disk_parameters->cylinders != saved_cylinders )
				cmn_err( CE_NOTE, "Adopting geometry: heads=%d, sectors=%d from partition table %d",
					disk_parameters->heads, disk_parameters->sectors, partition_number );
		}
		else if(partition_entry_end_head > 0 || partition_entry_end_sector > 0)
		{
			cmn_err( CE_NOTE, "Strange geometry: heads=%d, sectors=%d in partition table %d",
				partition_entry_end_head + 1, partition_entry_end_sector, partition_number );
			cmn_err( CE_NOTE, "Using geometry: heads=%d, sectors=%d",
					disk_parameters->heads, disk_parameters->sectors );
		}
	}
	brelse( buffer_head_ptr );
	return( 0 );
}


/*------------------------------------------------------------------------------
	AAC_ProcDirectoryInfo()

		Implement /proc/scsi/<drivername>/<n>.
		Used to export driver statistics and other infos to the world outside 
		the kernel using the proc file system. Also provides an interface to
		feed the driver with information.

	Postconditions
		For reads
			- if offset > 0 return 0
			- if offset == 0 write data to proc_buffer and set the start_ptr to
			beginning of proc_buffer, return the number of characters written.
		For writes
			- writes currently not supported, return 0
 *----------------------------------------------------------------------------*/
int AAC_ProcDirectoryInfo(
	char *proc_buffer,		// read/write buffer
	char **start_ptr,		// start of valid data in the buffer
	off_t offset,			// offset from the beginning of the imaginary file 
	int bytes_available,	// bytes available
	int host_no,			// SCSI host number 
	int write )				// direction of dataflow: TRUE for writes, FALSE for reads	
{
	int length = 0;
	cmn_err( CE_DEBUG, "AAC_ProcDirectoryInfo" );

	if(write || offset > 0)
		return( 0 );
	*start_ptr = proc_buffer;
	return( sprintf(&proc_buffer[length], "%s  %d\n", "Raid Controller, scsi hba number", host_no ) );
}



/*------------------------------------------------------------------------------
	AAC_SelectQueueDepths()

		Selects queue depths for each target device based on the host adapter's
		total capacity and the queue depth supported by the target device.
		A queue depth of one automatically disables tagged queueing.
 *----------------------------------------------------------------------------*/

void AAC_SelectQueueDepths(struct Scsi_Host * host_ptr, Scsi_Device * scsi_device_ptr )
{
	Scsi_Device * device_ptr;

	cmn_err( CE_DEBUG, "AAC_SelectQueueDepths" );
	cmn_err( CE_DEBUG, "Device #   Q Depth   Online" );
	cmn_err( CE_DEBUG, "---------------------------" );
	for( device_ptr = scsi_device_ptr; device_ptr != NULL; device_ptr = device_ptr->next )
		if( device_ptr->host == host_ptr )
		{
			device_ptr->queue_depth = 10;		
			cmn_err( CE_DEBUG, "  %2d         %d        %d", 
				device_ptr->id, device_ptr->queue_depth, device_ptr->online );
		}
}


/*------------------------------------------------------------------------------
	AAC_SearchBiosSignature()

		Locate adapter signature in BIOS
 *----------------------------------------------------------------------------*/
int AAC_SearchBiosSignature( void )
/*----------------------------------------------------------------------------*/
{
	unsigned base;
	unsigned namep;
	int index;
	int val;
	char name_buf[32];
	int result = FALSE;

	for( base = 0xc8000; base < 0xdffff; base += 0x4000 )
	{
		val = readb( base );
		if( val != 0x55 ) 
			continue;

		result = TRUE;
		namep = base + 0x1e;
		memcpy_fromio( name_buf, namep, 32 );
		name_buf[31] = '\0';
	}
	return( result );
}


/*------------------------------------------------------------------------------
	AAC_Ioctl()

		Handle SCSI ioctls
 *----------------------------------------------------------------------------*/

int AAC_Ioctl(Scsi_Device * scsi_dev_ptr, int cmd, void * arg )
{
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr;

	cmn_err( CE_DEBUG, "AAC_Ioctl" );
	CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )scsi_dev_ptr->host->hostdata;
	return( AacHba_Ioctl( CommonExtensionPtr, cmd, arg ) );
}



/*------------------------------------------------------------------------------
	AAC_ChardevOpen()
	
		Handle character device open
	
	Preconditions:
	Postconditions:
		Returns 0 if successful, -ENODEV or -EINVAL otherwise
 *----------------------------------------------------------------------------*/

int AAC_ChardevOpen(struct inode * inode_ptr, struct file * file_ptr )
{
	unsigned minor_number;
	cmn_err( CE_DEBUG, "AAC_ChardevOpen" );

	// extract & check the minor number
	minor_number = MINOR( inode_ptr->i_rdev );
	if( minor_number > ( g_HostAdapterCount - 1 ) )
	{
		cmn_err( CE_WARN, "AAC_ChardevOpen: Minor number %d not supported", minor_number );
		return( -ENODEV );
	}
	MOD_INC_USE_COUNT;
	return( 0 );
}


/*------------------------------------------------------------------------------
	AAC_ChardevRelease()
		Handle character device release.
	
	Preconditions:
	Postconditions:
		Returns 0 if successful, -ENODEV or -EINVAL otherwise
 *----------------------------------------------------------------------------*/

int AAC_ChardevRelease(struct inode * inode_ptr, struct file * file_ptr )
{
	cmn_err( CE_DEBUG, "AAC_ChardevRelease" );
	MOD_DEC_USE_COUNT;
	return( 0 );
}


/*------------------------------------------------------------------------------
	AAC_ChardevIoctl()
	
		Handle character device interface ioctls
	
	Preconditions:
	Postconditions:
		Returns 0 if successful, -ENODEV or -EINVAL otherwise
 *----------------------------------------------------------------------------*/

int AAC_ChardevIoctl(struct inode * inode_ptr,  struct file * file_ptr, unsigned int cmd, unsigned long arg )
{
	unsigned minor_number;
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr;

	cmn_err( CE_DEBUG, "AAC_ChardevIoctl" );

	// check device permissions in file_ptr->f_mode ??

	// extract & check the minor number
	minor_number = MINOR( inode_ptr->i_rdev );
	if( minor_number > ( g_HostAdapterCount - 1 ) )
	{
		cmn_err( CE_WARN, "AAC_ChardevIoctl: Minor number %d not supported", minor_number );
		return( -ENODEV );
	}

	// get device pointer
	CommonExtensionPtr = g_CommonExtensionPtrArray[ minor_number ];

	// dispatch ioctl - AacHba_Ioctl() returns zero on success
	if( AacHba_Ioctl( CommonExtensionPtr, cmd, ( void * )arg ) )
		return( -EINVAL );

	return( 0 );
}


/*------------------------------------------------------------------------------
	parse_keyword()

		Look for the keyword in str_ptr

	Preconditions:
	Postconditions:
		If keyword found
			- return true and update the pointer str_ptr.
		otherwise
			- return false
 *----------------------------------------------------------------------------*/

static int parse_keyword(char ** str_ptr,  char * keyword )
{
	char * ptr = *str_ptr;
	
	while( *keyword != '\0' )
	{
		char string_char = *ptr++;
		char keyword_char = *keyword++;

		if ( ( string_char >= 'A' ) && ( string_char <= 'Z' ) )
			string_char += 'a' - 'Z';
		if( ( keyword_char >= 'A' ) && ( keyword_char <= 'Z' ) ) 
			keyword_char += 'a' - 'Z';
		if( string_char != keyword_char )
			return FALSE;
	}
	*str_ptr = ptr;
	return TRUE;
}


/*------------------------------------------------------------------------------
	AAC_ParseDriverOptions()

	For modules the usage is:
		insmod -f aacraid.o 'aacraid_options="<option_name:argument>"'
 *----------------------------------------------------------------------------*/

static void AAC_ParseDriverOptions(char * cmnd_line_options_str)
{
	int message_level;
	int reverse_scan;
	char *cp;
	char *endp;

	cp = cmnd_line_options_str;

	cmn_err(CE_DEBUG, "AAC_ParseDriverOptions: <%s>", cp);

	while( *cp ) {
		if( parse_keyword( &cp, "message_level:" ) ) {
			message_level = simple_strtoul( cp, 0, 0 );
			if( ( message_level < CE_TAIL ) && ( message_level >= 0 ) ) {
				g_options.message_level = message_level;
				cmn_err( CE_WARN, "%s: new message level = %d", g_DriverName, g_options.message_level );
			}
			else {
				cmn_err( CE_WARN, "%s: invalid message level = %d", g_DriverName, message_level );
			}
		} else if (parse_keyword( &cp, "reverse_scan:" ) ) {
			reverse_scan = simple_strtoul( cp, 0, 0 );
			if (reverse_scan) {
				g_options.reverse_scan = 1;
				cmn_err( CE_WARN, "%s: reversing device discovery order", g_DriverName, g_options.message_level );
			}
		}
		else {
			cmn_err( CE_WARN, "%s: unknown command line option <%s>", g_DriverName, cp );
		}

		/*
		 * skip to next option,  accept " ", ";", and "," as delimiters
		 */
		while ( *cp && (*cp != ' ')  && (*cp != ';')  && (*cp != ','))
			cp++;
		if (*cp)				 /* skip over the delimiter */
			cp++;
	}
}


/*------------------------------------------------------------------------------
	Include Module support if requested.

	To use the low level SCSI driver support using the linux kernel loadable 
	module interface we should initialize the global variable driver_interface  
	(datatype Scsi_Host_Template) and then include the file scsi_module.c.
	This should also be wrapped in a #ifdef MODULE/#endif
 *----------------------------------------------------------------------------*/
#ifdef MODULE

/*
	The Loadable Kernel Module Installation Facility may pass us
	a pointer to a driver specific options string to be parsed, 
	we assign this to options string. 
*/
MODULE_PARM( module_options, "s" );

EXPORT_NO_SYMBOLS;

Scsi_Host_Template driver_template = AAC_HOST_TEMPLATE_ENTRY;

#include "scsi_module.c"

#endif
