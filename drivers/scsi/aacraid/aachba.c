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
 *   aachba.c
 *
 * Abstract: driver...
 *
--*/
/*------------------------------------------------------------------------------
 *              I N C L U D E S
 *----------------------------------------------------------------------------*/
#include "osheaders.h"
#include "AacGenericTypes.h"
#include "aac_unix_defs.h"
#include "comstruc.h"
#include "monkerapi.h"
#include "protocol.h"
#include "fsafs.h"
#include "fsact.h"
#include "fsaioctl.h"

#include "sap1common.h"
#include "fsaport.h"
#include "pcisup.h"
#include "sap1.h"
#include "nodetype.h"
#include "comsup.h"
#include "afacomm.h"
#include "adapter.h"

/*------------------------------------------------------------------------------
 *              D E F I N E S
 *----------------------------------------------------------------------------*/
/*	SCSI Commands */
#define	SS_TEST			0x00	/* Test unit ready */
#define SS_REZERO		0x01	/* Rezero unit */
#define	SS_REQSEN		0x03	/* Request Sense */
#define SS_REASGN		0x07	/* Reassign blocks */
#define	SS_READ			0x08	/* Read 6   */
#define	SS_WRITE		0x0A	/* Write 6  */
#define	SS_INQUIR		0x12	/* inquiry */
#define	SS_ST_SP		0x1B	/* Start/Stop unit */
#define	SS_LOCK			0x1E	/* prevent/allow medium removal */
#define SS_RESERV		0x16	/* Reserve */
#define SS_RELES		0x17	/* Release */
#define SS_MODESEN		0x1A	/* Mode Sense 6 */
#define	SS_RDCAP		0x25	/* Read Capacity */
#define	SM_READ			0x28	/* Read 10  */
#define	SM_WRITE		0x2A	/* Write 10 */
#define SS_SEEK			0x2B	/* Seek */

/* values for inqd_pdt: Peripheral device type in plain English */
#define	INQD_PDT_DA	0x00	/* Direct-access (DISK) device */
#define	INQD_PDT_PROC	0x03	/* Processor device */
#define	INQD_PDT_CHNGR	0x08	/* Changer (jukebox, scsi2) */
#define	INQD_PDT_COMM	0x09	/* Communication device (scsi2) */
#define	INQD_PDT_NOLUN2 0x1f	/* Unknown Device (scsi2) */
#define	INQD_PDT_NOLUN	0x7f	/* Logical Unit Not Present */

#define	INQD_PDT_DMASK	0x1F	/* Peripheral Device Type Mask */
#define	INQD_PDT_QMASK	0xE0	/* Peripheral Device Qualifer Mask */

#define	TARGET_LUN_TO_CONTAINER(Target, Lun)    (((Lun) << 4) | Target)
#define CONTAINER_TO_TARGET(Container)          ((Container) & 0xf)
#define CONTAINER_TO_LUN(Container)             ((Container) >> 4)

#define MAX_FIB_DATA (sizeof(FIB) - sizeof(FIB_HEADER))

#define MAX_DRIVER_SG_SEGMENT_COUNT 17

// ------------------------------------------------------
// Sense keys
//
#define SENKEY_NO_SENSE      0x00 //
#define SENKEY_UNDEFINED     0x01 //
#define SENKEY_NOT_READY     0x02 //
#define SENKEY_MEDIUM_ERR    0x03 //
#define SENKEY_HW_ERR        0x04 //
#define SENKEY_ILLEGAL       0x05 //
#define SENKEY_ATTENTION     0x06 //
#define SENKEY_PROTECTED     0x07 //
#define SENKEY_BLANK         0x08 //
#define SENKEY_V_UNIQUE      0x09 //
#define SENKEY_CPY_ABORT     0x0A //
#define SENKEY_ABORT         0x0B //
#define SENKEY_EQUAL         0x0C //
#define SENKEY_VOL_OVERFLOW  0x0D //
#define SENKEY_MISCOMP       0x0E //
#define SENKEY_RESERVED      0x0F //

// ------------------------------------------------------
// Sense codes
//
#define SENCODE_NO_SENSE                        0x00
#define SENCODE_END_OF_DATA                     0x00
#define SENCODE_BECOMING_READY                  0x04
#define SENCODE_INIT_CMD_REQUIRED               0x04
#define SENCODE_PARAM_LIST_LENGTH_ERROR         0x1A
#define SENCODE_INVALID_COMMAND                 0x20
#define SENCODE_LBA_OUT_OF_RANGE                0x21
#define SENCODE_INVALID_CDB_FIELD               0x24
#define SENCODE_LUN_NOT_SUPPORTED               0x25
#define SENCODE_INVALID_PARAM_FIELD             0x26
#define SENCODE_PARAM_NOT_SUPPORTED             0x26
#define SENCODE_PARAM_VALUE_INVALID             0x26
#define SENCODE_RESET_OCCURRED                  0x29
#define SENCODE_LUN_NOT_SELF_CONFIGURED_YET     0x3E
#define SENCODE_INQUIRY_DATA_CHANGED            0x3F
#define SENCODE_SAVING_PARAMS_NOT_SUPPORTED     0x39
#define SENCODE_DIAGNOSTIC_FAILURE              0x40
#define SENCODE_INTERNAL_TARGET_FAILURE         0x44
#define SENCODE_INVALID_MESSAGE_ERROR           0x49
#define SENCODE_LUN_FAILED_SELF_CONFIG          0x4c
#define SENCODE_OVERLAPPED_COMMAND              0x4E

// ------------------------------------------------------
// Additional sense codes
//
#define ASENCODE_NO_SENSE                       0x00
#define ASENCODE_END_OF_DATA                    0x05
#define ASENCODE_BECOMING_READY                 0x01
#define ASENCODE_INIT_CMD_REQUIRED              0x02
#define ASENCODE_PARAM_LIST_LENGTH_ERROR        0x00
#define ASENCODE_INVALID_COMMAND                0x00
#define ASENCODE_LBA_OUT_OF_RANGE               0x00
#define ASENCODE_INVALID_CDB_FIELD              0x00
#define ASENCODE_LUN_NOT_SUPPORTED              0x00
#define ASENCODE_INVALID_PARAM_FIELD            0x00
#define ASENCODE_PARAM_NOT_SUPPORTED            0x01
#define ASENCODE_PARAM_VALUE_INVALID            0x02
#define ASENCODE_RESET_OCCURRED                 0x00
#define ASENCODE_LUN_NOT_SELF_CONFIGURED_YET    0x00
#define ASENCODE_INQUIRY_DATA_CHANGED           0x03
#define ASENCODE_SAVING_PARAMS_NOT_SUPPORTED    0x00
#define ASENCODE_DIAGNOSTIC_FAILURE             0x80
#define ASENCODE_INTERNAL_TARGET_FAILURE        0x00
#define ASENCODE_INVALID_MESSAGE_ERROR          0x00
#define ASENCODE_LUN_FAILED_SELF_CONFIG         0x00
#define ASENCODE_OVERLAPPED_COMMAND             0x00

#define BYTE0( x ) ( unsigned char )( x )
#define BYTE1( x ) ( unsigned char )( x >> 8  )
#define BYTE2( x ) ( unsigned char )( x >> 16 )
#define BYTE3( x ) ( unsigned char )( x >> 24 )

/*------------------------------------------------------------------------------
 *              S T R U C T S / T Y P E D E F S
 *----------------------------------------------------------------------------*/
/* SCSI inquiry data */

struct inquiry_data {
	u8 inqd_pdt;     /* Peripheral qualifier | Peripheral Device Type  */
	u8 inqd_dtq;     /* RMB | Device Type Qualifier  */
	u8 inqd_ver;     /* ISO version | ECMA version | ANSI-approved version */
	u8 inqd_rdf;     /* AENC | TrmIOP | Response data format */
	u8 inqd_len;     /* Additional length (n-4) */
	u8 inqd_pad1[2]; /* Reserved - must be zero */
	u8 inqd_pad2;    /* RelAdr | WBus32 | WBus16 |  Sync  | Linked |Reserved| CmdQue | SftRe */
	u8 inqd_vid[8];  /* Vendor ID */
	u8 inqd_pid[16]; /* Product ID */
	u8 inqd_prl[4];  /* Product Revision Level */
};

struct sense_data {
	u8 error_code;		// 70h (current errors), 71h(deferred errors)
	u8 valid:1;			// A valid bit of one indicates that the information 
					// field contains valid information as defined in the
					// SCSI-2 Standard.
	
	u8 segment_number;	// Only used for COPY, COMPARE, or COPY AND VERIFY 
				// commands
	
	u8 sense_key:4;		// Sense Key
	u8 reserved:1;
	u8 ILI:1;			// Incorrect Length Indicator
	u8 EOM:1;			// End Of Medium - reserved for random access devices
	u8 filemark:1;		// Filemark - reserved for random access devices
	
	u8 information[4];	// for direct-access devices, contains the unsigned 
				// logical block address or residue associated with 
				// the sense key 
	u8 add_sense_len;	// number of additional sense bytes to follow this field
	u8 cmnd_info[4];	// not used
	u8 ASC;		// Additional Sense Code
	u8 ASCQ;		// Additional Sense Code Qualifier
	u8 FRUC;		// Field Replaceable Unit Code - not used
	
	u8 bit_ptr:3;	// indicates which byte of the CDB or parameter data
				// was in error
	u8 BPV:1;		// bit pointer valid (BPV): 1- indicates that 
				// the bit_ptr field has valid value
	u8 reserved2:2;
	u8 CD:1;		// command data bit: 1- illegal parameter in CDB.
				//                   0- illegal parameter in data.
	u8 SKSV:1;
	
	u8 field_ptr[2];	// byte of the CDB or parameter data in error
};

/*------------------------------------------------------------------------------
 *              G L O B A L S
 *----------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
 *              M O D U L E   G L O B A L S
 *----------------------------------------------------------------------------*/

static fsadev_t *g_fsa_dev_array[8];	// SCSI Device Instance Pointers
static struct sense_data g_sense_data[MAXIMUM_NUM_CONTAINERS];

/*------------------------------------------------------------------------------
 *              F U N C T I O N   P R O T O T Y P E S
 *----------------------------------------------------------------------------*/

AAC_STATUS AacHba_OpenAdapter(void * arg);
AAC_STATUS AacHba_CloseAdapter(void * arg);
int AacHba_HandleAif(void * arg, PFIB_CONTEXT FibContext);
int AacHba_AdapterDeviceControl (void * arg, PAFA_IOCTL_CMD IoctlCmdPtr, int * ReturnStatus);
void AacHba_CompleteScsi(Scsi_Cmnd *scsi_cmnd_ptr);
void AacHba_CompleteScsiNoLock(Scsi_Cmnd *scsi_cmnd_ptr);
static void AacHba_ReadCallback(void *context, PFIB_CONTEXT FibContext, int FibStatus );
static void AacHba_WriteCallback(void *context, PFIB_CONTEXT FibContext, int FibStatus );
int AacHba_DoScsiRead(Scsi_Cmnd *scsi_cmnd_ptr, int ContainerId, int wait);
int AacHba_DoScsiWrite(Scsi_Cmnd *scsi_cmnd_ptr, int ContainerId, int wait);
int AacHba_QueryDisk(void * arg, PAFA_IOCTL_CMD IoctlCmdPtr);
int AacHba_ForceDeleteDisk(void * arg, PAFA_IOCTL_CMD IoctlCmdPtr);
int AacHba_DeleteDisk(void * arg, PAFA_IOCTL_CMD IoctlCmdPtr);
void AacHba_DetachAdapter(void * arg);
int AacCommDetachAdapter(PAFA_COMM_ADAPTER adapter);
void AacHba_SetSenseData(char * sense_buf,u8 sense_key,u8 sense_code,
	u8 a_sense_code, u8 incorrect_length, u8 bit_pointer, 
	unsigned field_pointer, unsigned long residue);
static void get_sd_devname(long disknum, char * buffer);

// Keep these here for the time being - #REVIEW#

int AfaCommAdapterDeviceControl (void * arg,PAFA_IOCTL_CMD IoctlCmdPtr);

AAC_STATUS AfaCommRegisterNewClassDriver(PAFA_COMM_ADAPTER adapter, 
	PAFA_NEW_CLASS_DRIVER NewClassDriver,
	PAFA_NEW_CLASS_DRIVER_RESPONSE NewClassDriverResponse
);

void SetInqDataStr(int, void *, int);


/*------------------------------------------------------------------------------
 *              F U N C T I O N S
 *----------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	AacHba_ClassDriverInit()

		Setup 'core' class driver to answer ioctl's
 *----------------------------------------------------------------------------*/

int AacHba_ClassDriverInit(PCI_MINIPORT_COMMON_EXTENSION * CommonExtensionPtr)
{
	AFA_NEW_CLASS_DRIVER		NewClassDriver;
	AFA_NEW_CLASS_DRIVER_RESPONSE	NewClassDriverResponse;
	PAFA_COMM_ADAPTER		adapter;

	adapter = (AFA_COMM_ADAPTER *)CommonExtensionPtr->Adapter;

	RtlZeroMemory( &NewClassDriver, sizeof( AFA_NEW_CLASS_DRIVER ) );
	
	// ClassDriverExtension is the first argument passed to class driver functions below
	NewClassDriver.ClassDriverExtension = CommonExtensionPtr;
	
	NewClassDriver.OpenAdapter = AacHba_OpenAdapter;
	NewClassDriver.CloseAdapter = AacHba_CloseAdapter;
	NewClassDriver.DeviceControl = AacHba_AdapterDeviceControl;
	NewClassDriver.HandleAif = AacHba_HandleAif;
	AfaCommRegisterNewClassDriver( adapter, &NewClassDriver, &NewClassDriverResponse );

	return(0);
}


/*------------------------------------------------------------------------------
	AacHba_ProbeContainers()

		Make a list of all containers in the system.
 *----------------------------------------------------------------------------*/
int AacHba_ProbeContainers( 
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr )
/*----------------------------------------------------------------------------*/
{
	fsadev_t		*fsa_dev_ptr;
	int			Index, Status;
	PMNTINFO		DiskInfo;
	PMNTINFORESPONSE	DiskInfoResponse;
	PFIB_CONTEXT 		FibContext;
	AFA_COMM_ADAPTER	*adapter;
	unsigned		instance;
	
	adapter = ( AFA_COMM_ADAPTER * )CommonExtensionPtr->Adapter;
	fsa_dev_ptr = &( CommonExtensionPtr->OsDep.fsa_dev );
	instance = CommonExtensionPtr->OsDep.scsi_host_ptr->unique_id;

	if( !( FibContext = adapter->CommFuncs.AllocateFib( adapter ) ) )
	{
		cmn_err( CE_WARN, "AacHba_ProbeContainers: AllocateFib failed" );
		return( STATUS_UNSUCCESSFUL );
	}

	for ( Index = 0; Index < MAXIMUM_NUM_CONTAINERS; Index++ ) 
	{
		adapter->CommFuncs.InitializeFib( FibContext );

		DiskInfo = ( PMNTINFO )adapter->CommFuncs.GetFibData( FibContext );

		DiskInfo->Command  = VM_NameServe;
		DiskInfo->MntCount = Index;
		DiskInfo->MntType  = FT_FILESYS;
		
		Status =  adapter->CommFuncs.SendFib(ContainerCommand,
					FibContext,
					sizeof(MNTINFO),
					FsaNormal,
					TRUE,
					NULL,
					TRUE,
					NULL,
					NULL );
		if ( Status ) 
		{
			cmn_err( CE_WARN, "ProbeContainers: SendFIb Failed" );
			break;
		}

		DiskInfoResponse = ( PMNTINFORESPONSE )adapter->CommFuncs.GetFibData( FibContext );

		if ( ( DiskInfoResponse->Status == ST_OK ) &&
			( DiskInfoResponse->MntTable[0].VolType != CT_NONE ) ) 
		{


			fsa_dev_ptr->ContainerValid[Index] = TRUE;
			fsa_dev_ptr->ContainerType[Index]  = DiskInfoResponse->MntTable[0].VolType;
			fsa_dev_ptr->ContainerSize[Index]  = DiskInfoResponse->MntTable[0].Capacity;

			if (DiskInfoResponse->MntTable[0].ContentState & FSCS_READONLY)
				fsa_dev_ptr->ContainerReadOnly[Index] = TRUE;
		}
		
		adapter->CommFuncs.CompleteFib( FibContext );

		// If there are no more containers, then stop asking.
		if ((Index + 1) >= DiskInfoResponse->MntRespCount)
			break;

	} // end for()

	adapter->CommFuncs.FreeFib( FibContext );

	g_fsa_dev_array[instance] = fsa_dev_ptr;

	return( Status );
}


/*------------------------------------------------------------------------------
	AacHba_ProbeContainer()

		Probe a single container.
 *----------------------------------------------------------------------------*/
int AacHba_ProbeContainer( PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr, int ContainerId )
/*----------------------------------------------------------------------------*/
{
	fsadev_t		*fsa_dev_ptr;
	int			Status;
	PMNTINFO		DiskInfo;
	PMNTINFORESPONSE	DiskInfoResponse;
	PFIB_CONTEXT 		FibContext;
	AFA_COMM_ADAPTER				*adapter;
	unsigned						instance;
	
	adapter = ( AFA_COMM_ADAPTER * )CommonExtensionPtr->Adapter;
	fsa_dev_ptr = &( CommonExtensionPtr->OsDep.fsa_dev );
	instance = CommonExtensionPtr->OsDep.scsi_host_ptr->unique_id;

	if( !( FibContext = adapter->CommFuncs.AllocateFib( adapter ) ) )
	{
		cmn_err( CE_WARN, "AacHba_ProbeContainers: AllocateFib failed" );
		return( STATUS_UNSUCCESSFUL );
	}

	adapter->CommFuncs.InitializeFib( FibContext );

	DiskInfo = ( PMNTINFO )adapter->CommFuncs.GetFibData( FibContext );

	DiskInfo->Command  = VM_NameServe;
	DiskInfo->MntCount = ContainerId;
	DiskInfo->MntType  = FT_FILESYS;
		
	Status =  adapter->CommFuncs.SendFib(	ContainerCommand,
				FibContext,
				sizeof(MNTINFO),
				FsaNormal,
				TRUE,
				NULL,
				TRUE,
				NULL,
				NULL );
	if ( Status ) 
	{
		cmn_err( CE_WARN, "ProbeContainers: SendFIb Failed" );
		adapter->CommFuncs.CompleteFib( FibContext );
		adapter->CommFuncs.FreeFib( FibContext );
		return( Status );
	}

	DiskInfoResponse = ( PMNTINFORESPONSE )adapter->CommFuncs.GetFibData( FibContext );

	if ( ( DiskInfoResponse->Status == ST_OK ) &&
		( DiskInfoResponse->MntTable[0].VolType != CT_NONE ) ) 
	{

		fsa_dev_ptr->ContainerValid[ContainerId] = TRUE;
		fsa_dev_ptr->ContainerType[ContainerId]  = DiskInfoResponse->MntTable[0].VolType;
		fsa_dev_ptr->ContainerSize[ContainerId]  = DiskInfoResponse->MntTable[0].Capacity;
		if (DiskInfoResponse->MntTable[0].ContentState & FSCS_READONLY)
			fsa_dev_ptr->ContainerReadOnly[ContainerId] = TRUE;
	}
		
	adapter->CommFuncs.CompleteFib( FibContext );
	adapter->CommFuncs.FreeFib( FibContext );

	return( Status );
}


/*------------------------------------------------------------------------------
	AacHba_CompleteScsi()

		Call SCSI completion routine after acquiring io_request_lock

  	Preconditions:
	Postconditions:
 *----------------------------------------------------------------------------*/

void AacHba_CompleteScsi(Scsi_Cmnd *scsi_cmnd_ptr)
{
	unsigned long cpu_flags;
	spin_lock_irqsave( &io_request_lock, cpu_flags );
	scsi_cmnd_ptr->scsi_done( scsi_cmnd_ptr );
	spin_unlock_irqrestore( &io_request_lock, cpu_flags );
}


/*------------------------------------------------------------------------------
	AacHba_CompleteScsiNoLock()

		Call SCSI completion routine

  	Preconditions:
	Postconditions:
 *----------------------------------------------------------------------------*/

void AacHba_CompleteScsiNoLock(Scsi_Cmnd *scsi_cmnd_ptr)
{
	scsi_cmnd_ptr->scsi_done( scsi_cmnd_ptr );
}

/*------------------------------------------------------------------------------
	AacHba_DoScsiCmd()

		Process SCSI command

  	Preconditions:
	Postconditions:
		Returns 0 on success, -1 on failure
 *----------------------------------------------------------------------------*/

int AacHba_DoScsiCmd(Scsi_Cmnd *scsi_cmnd_ptr, int wait)
{
	int		ContainerId = 0;
	fsadev_t	*fsa_dev_ptr;
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtensionPtr;
	int MiniPortIndex;

	CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	MiniPortIndex = CommonExtensionPtr->OsDep.MiniPortIndex;

	fsa_dev_ptr = g_fsa_dev_array[ scsi_cmnd_ptr->host->unique_id ];

	// If the bus, target or lun is out of range, return fail
	// Test does not apply to ID 16, the pseudo id for the controller itself.
	if ( scsi_cmnd_ptr->target != scsi_cmnd_ptr->host->this_id ) 
	{
		if( ( scsi_cmnd_ptr->channel > 0 ) ||
			( scsi_cmnd_ptr->target > 15 ) || 
			( scsi_cmnd_ptr->lun > 7 ) )
		{
			cmn_err( CE_DEBUG, "The bus, target or lun is out of range = %d, %d, %d",
				scsi_cmnd_ptr->channel,
				scsi_cmnd_ptr->target, 
				scsi_cmnd_ptr->lun );
			scsi_cmnd_ptr->result = DID_BAD_TARGET << 16;

			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

			return ( -1 );
		}

		ContainerId = TARGET_LUN_TO_CONTAINER( scsi_cmnd_ptr->target, scsi_cmnd_ptr->lun );


		// If the target container doesn't exist, it may have been newly created
		if( fsa_dev_ptr->ContainerValid[ContainerId] == 0 )
		{	
			switch( scsi_cmnd_ptr->cmnd[0] )
			{
				case SS_INQUIR:
				case SS_RDCAP:
				case SS_TEST:
					spin_unlock_irq( &io_request_lock );
					AacHba_ProbeContainer( CommonExtensionPtr, ContainerId );		
					spin_lock_irq( &io_request_lock );
				default:
					break;
			}
		}

		// If the target container still doesn't exist, return failure
		if( fsa_dev_ptr->ContainerValid[ContainerId] == 0 )
		{	
			cmn_err( CE_DEBUG, "Target container %d doesn't exist", ContainerId );
			scsi_cmnd_ptr->result = DID_BAD_TARGET << 16;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

			return ( -1 );
		}
	}
	else	// the command is for the controller itself
		if( ( scsi_cmnd_ptr->cmnd[0] != SS_INQUIR )	&& // only INQUIRY & TUR cmnd supported for controller 
			( scsi_cmnd_ptr->cmnd[0] != SS_TEST ) )
		{
			cmn_err( CE_WARN, "Only INQUIRY & TUR command supported for controller, rcvd = 0x%x", 
				scsi_cmnd_ptr->cmnd[0] );

			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;
			
			AacHba_SetSenseData( (char *)&g_sense_data[ ContainerId ],
				SENKEY_ILLEGAL, SENCODE_INVALID_COMMAND, ASENCODE_INVALID_COMMAND, 
				0, 0, 0, 0 );

			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

			return ( -1 );
		}

	// Handle commands here that don't really require going out to the adapter
	switch ( scsi_cmnd_ptr->cmnd[0] ) 
	{
		case SS_INQUIR:
		{
			struct inquiry_data *inq_data_ptr;
		
			cmn_err( CE_DEBUG, "INQUIRY command, ID: %d", scsi_cmnd_ptr->target );
			inq_data_ptr = ( struct inquiry_data * )scsi_cmnd_ptr->request_buffer;
			bzero( inq_data_ptr, sizeof( struct inquiry_data ) );

			inq_data_ptr->inqd_ver = 2;		// claim compliance to SCSI-2

			inq_data_ptr->inqd_dtq = 0x80;	// set RMB bit to one indicating 
											// that the medium is removable
			inq_data_ptr->inqd_rdf = 2;		// A response data format value of
											// two indicates that the data shall 
											// be in the format specified in SCSI-2
			inq_data_ptr->inqd_len = 31;

			// Set the Vendor, Product, and Revision Level see: <vendor>.c i.e. aac.c
			SetInqDataStr(	MiniPortIndex, 
							(void *)(inq_data_ptr->inqd_vid),
							fsa_dev_ptr->ContainerType[ContainerId]);

			if ( scsi_cmnd_ptr->target == scsi_cmnd_ptr->host->this_id )
				inq_data_ptr->inqd_pdt = INQD_PDT_PROC;	// Processor device
			else
				inq_data_ptr->inqd_pdt = INQD_PDT_DA;	// Direct/random access device

			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

			return ( 0 );
		}

		case SS_RDCAP:
		{
			int capacity;
			char *cp;

			cmn_err( CE_DEBUG, "READ CAPACITY command" );
			capacity = fsa_dev_ptr->ContainerSize[ContainerId];
			cp = scsi_cmnd_ptr->request_buffer;
			cp[0] = ( capacity >> 24 ) & 0xff;
			cp[1] = ( capacity >> 16 ) & 0xff;
			cp[2] = ( capacity >>  8 ) & 0xff;
			cp[3] = ( capacity >>  0 ) & 0xff;
			cp[4] = 0;
			cp[5] = 0;
			cp[6] = 2;
			cp[7] = 0;
	
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

			return ( 0 );
		}

		case SS_MODESEN:
		{
			char *mode_buf;

			cmn_err( CE_DEBUG, "MODE SENSE command" );
			mode_buf = scsi_cmnd_ptr->request_buffer;
			mode_buf[0] = 0;	// Mode data length (MSB)
			mode_buf[1] = 6;	// Mode data length (LSB)
			mode_buf[2] = 0;	// Medium type - default
			mode_buf[3] = 0;	// Device-specific param, bit 8: 0/1 = write enabled/protected
			mode_buf[4] = 0;	// reserved
			mode_buf[5] = 0;	// reserved
			mode_buf[6] = 0;	// Block descriptor length (MSB)
			mode_buf[7] = 0;	// Block descriptor length (LSB)
	
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

			return ( 0 );
		}


		// These commands are all No-Ops
		case SS_TEST:
			cmn_err( CE_DEBUG, "TEST UNIT READY command" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_REQSEN:
			cmn_err( CE_DEBUG, "REQUEST SENSE command" );

			memcpy( scsi_cmnd_ptr->sense_buffer, &g_sense_data[ContainerId],
				sizeof( struct sense_data ) );
			bzero( &g_sense_data[ContainerId], sizeof( struct sense_data ) );

			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_LOCK:
			cmn_err(CE_DEBUG, "LOCK command");

			if( scsi_cmnd_ptr->cmnd[4] )
				fsa_dev_ptr->ContainerLocked[ContainerId] = 1;
			else
				fsa_dev_ptr->ContainerLocked[ContainerId] = 0;

			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_RESERV:
			cmn_err( CE_DEBUG, "RESERVE command" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_RELES:
			cmn_err( CE_DEBUG, "RELEASE command" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_REZERO:
			cmn_err( CE_DEBUG, "REZERO command" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_REASGN:
			cmn_err( CE_DEBUG, "REASSIGN command" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_SEEK:
			cmn_err( CE_DEBUG, "SEEK command" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );

		case SS_ST_SP:
			cmn_err( CE_DEBUG, "START/STOP command" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( 0 );
	}

	switch ( scsi_cmnd_ptr->cmnd[0] ) 
	{
		case SS_READ:
		case SM_READ:
			// Hack to keep track of ordinal number of the device that corresponds
			// to a container. Needed to convert containers to /dev/sd device names
			fsa_dev_ptr->ContainerDevNo[ContainerId] = 
				DEVICE_NR( scsi_cmnd_ptr->request.rq_dev );
			
			return( AacHba_DoScsiRead( scsi_cmnd_ptr, ContainerId,  wait ) );
			break;

		case SS_WRITE:
		case SM_WRITE:
			return( AacHba_DoScsiWrite( scsi_cmnd_ptr, ContainerId,  wait ) );
			break;
	}
	//
	// Unhandled commands
	//
	cmn_err( CE_WARN, "Unhandled SCSI Command: 0x%x", scsi_cmnd_ptr->cmnd[0] );
	scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;

	AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
		SENKEY_ILLEGAL, SENCODE_INVALID_COMMAND, ASENCODE_INVALID_COMMAND, 
		0, 0, 0, 0 );

	AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
	return ( -1 );
}


/*------------------------------------------------------------------------------
	AacHba_DoScsiRead()
		
		Handles SCSI READ requests

  	Preconditions:
	Postconditions:
		Returns 0 on success, -1 on failure
 *----------------------------------------------------------------------------*/
int AacHba_DoScsiRead(
	Scsi_Cmnd *scsi_cmnd_ptr,
	int ContainerId,
	int wait )
/*----------------------------------------------------------------------------*/
{
	u_long				lba;
	u_long				count;
	u_long				byte_count;
	int				Status;

	PBLOCKREAD			BlockReadDisk;
	PBLOCKREADRESPONSE	BlockReadResponse;
	u16				FibSize;
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtension;
	AFA_COMM_ADAPTER		*adapter;
	PFIB_CONTEXT			cmd_fibcontext;

	CommonExtension = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	adapter         = ( AFA_COMM_ADAPTER * )CommonExtension->Adapter;

	// Get block address and transfer length
	if ( scsi_cmnd_ptr->cmnd[0] == SS_READ ) // 6 byte command
	{
		cmn_err( CE_DEBUG, "aachba: received a read(6) command on target %d", ContainerId );

		lba = ( ( scsi_cmnd_ptr->cmnd[1] & 0x1F ) << 16 ) | 
			( scsi_cmnd_ptr->cmnd[2] << 8 ) |
			scsi_cmnd_ptr->cmnd[3];
		count = scsi_cmnd_ptr->cmnd[4];

		if ( count == 0 )
			count = 256;
	} 
	else 
	{		
		cmn_err( CE_DEBUG, "aachba: received a read(10) command on target %d", ContainerId );
		
		lba = ( scsi_cmnd_ptr->cmnd[2] << 24 ) | ( scsi_cmnd_ptr->cmnd[3] << 16 ) | 
			( scsi_cmnd_ptr->cmnd[4] << 8 ) | scsi_cmnd_ptr->cmnd[5];

		count = ( scsi_cmnd_ptr->cmnd[7] << 8 ) | scsi_cmnd_ptr->cmnd[8];
	}	
	cmn_err( CE_DEBUG, "AacHba_DoScsiRead[cpu %d]: lba = %lu, t = %ld", smp_processor_id(), lba, jiffies );

	//-------------------------------------------------------------------------
	// Alocate and initialize a Fib
	//  Setup BlockRead command
	if( !( cmd_fibcontext = adapter->CommFuncs.AllocateFib( adapter ) ) )
	{
		cmn_err( CE_WARN, "AacHba_DoScsiRead: AllocateFib failed\n" );
		scsi_cmnd_ptr->result = DID_ERROR << 16;
		AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
		return ( -1 );
	}

	adapter->CommFuncs.InitializeFib( cmd_fibcontext );

	BlockReadDisk = ( PBLOCKREAD )adapter->CommFuncs.GetFibData( cmd_fibcontext );
	BlockReadDisk->Command     = VM_CtBlockRead;
	BlockReadDisk->ContainerId = ContainerId;
	BlockReadDisk->BlockNumber = lba;
	BlockReadDisk->ByteCount   = count * 512;
	BlockReadDisk->SgMap.SgCount = 1;

	if( BlockReadDisk->ByteCount > ( 64 * 1024 ) )
	{
		cmn_err( CE_WARN, "AacHba_DoScsiRead: READ request is larger than 64K" );
		scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;

		AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
				SENKEY_ILLEGAL, SENCODE_INVALID_CDB_FIELD, ASENCODE_INVALID_CDB_FIELD, 
				0, 0, 7, 0 );

		goto err_return;
	}

	//-------------------------------------------------------------------------
	// Build Scatter/Gather list
	//
	if ( scsi_cmnd_ptr->use_sg )	// use scatter/gather list
	{
		struct scatterlist *scatterlist_ptr;
		int segment;
		
		scatterlist_ptr = ( struct scatterlist * )scsi_cmnd_ptr->request_buffer;

		byte_count = 0;
		for( segment = 0; segment< scsi_cmnd_ptr->use_sg; segment++ ) 
		{
			BlockReadDisk->SgMap.SgEntry[segment].SgAddress = 
				( void * )virt_to_bus(scatterlist_ptr[segment].address );
			BlockReadDisk->SgMap.SgEntry[segment].SgByteCount = 
				scatterlist_ptr[segment].length;

#ifdef DEBUG_SGBUFFER
			memset( scatterlist_ptr[segment].address, 0xa5, 
				scatterlist_ptr[segment].length );
#endif

			byte_count += scatterlist_ptr[segment].length;
			
			if( BlockReadDisk->SgMap.SgEntry[segment].SgByteCount > ( 64 * 1024 ) )
			{
				cmn_err( CE_WARN, "AacHba_DoScsiRead: Segment byte count is larger than 64K" );
				scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;

				AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
					SENKEY_ILLEGAL, SENCODE_INVALID_CDB_FIELD, ASENCODE_INVALID_CDB_FIELD, 
					0, 0, 7, 0 );

				goto err_return;
			}
			/*
			cmn_err(CE_DEBUG, "SgEntry[%d].SgAddress = 0x%x, Byte count = 0x%x", 
					segment,
					BlockReadDisk->SgMap.SgEntry[segment].SgAddress,
					BlockReadDisk->SgMap.SgEntry[segment].SgByteCount);
			*/
		}
		BlockReadDisk->SgMap.SgCount = scsi_cmnd_ptr->use_sg;

		if( BlockReadDisk->SgMap.SgCount > MAX_DRIVER_SG_SEGMENT_COUNT ) 
		{
			cmn_err( CE_WARN, "AacHba_DoScsiRead: READ request with SgCount > %d", 
				MAX_DRIVER_SG_SEGMENT_COUNT );
			scsi_cmnd_ptr->result = DID_ERROR << 16;
			goto err_return;
		}
	} 	
	else		// one piece of contiguous phys mem
	{
		BlockReadDisk->SgMap.SgEntry[0].SgAddress = 
			( void * )virt_to_bus( scsi_cmnd_ptr->request_buffer );
		BlockReadDisk->SgMap.SgEntry[0].SgByteCount = scsi_cmnd_ptr->request_bufflen;

		byte_count = scsi_cmnd_ptr->request_bufflen;

		if( BlockReadDisk->SgMap.SgEntry[0].SgByteCount > ( 64 * 1024 ) )
		{
			cmn_err( CE_WARN, "AacHba_DoScsiRead: Single segment byte count is larger than 64K" );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;

			AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
				SENKEY_ILLEGAL, SENCODE_INVALID_CDB_FIELD, ASENCODE_INVALID_CDB_FIELD, 
				0, 0, 7, 0 );

			goto err_return;
		}
	}

	if( byte_count != BlockReadDisk->ByteCount )
		cmn_err( CE_WARN, "AacHba_DoScsiRead: byte_count != BlockReadDisk->ByteCount" );

	//-------------------------------------------------------------------------
	// Now send the Fib to the adapter
	//
	FibSize = sizeof( BLOCKREAD ) + ( ( BlockReadDisk->SgMap.SgCount - 1 ) * sizeof( SGENTRY ) );

	if( wait ) 
	{
		Status = adapter->CommFuncs.SendFib( ContainerCommand,
				 cmd_fibcontext,
				 FibSize,
				 FsaNormal,
				 TRUE,
				 NULL,
				 TRUE,
				 NULL,
				 NULL);

		BlockReadResponse = ( PBLOCKREADRESPONSE )
			adapter->CommFuncs.GetFibData( cmd_fibcontext );	

		adapter->CommFuncs.CompleteFib( cmd_fibcontext );
		adapter->CommFuncs.FreeFib( cmd_fibcontext );
		
		if( BlockReadResponse->Status != ST_OK )
		{
			cmn_err( CE_WARN, "AacHba_DoScsiRead: BlockReadCommand failed with status: %d", 
				BlockReadResponse->Status );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;

			AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
				SENKEY_HW_ERR, SENCODE_INTERNAL_TARGET_FAILURE, ASENCODE_INTERNAL_TARGET_FAILURE, 
				0, 0, 0, 0 );

			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( -1 );
		}
		else
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;

		AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
		return ( 0 );
	} 
	else 
	{
		Status = adapter->CommFuncs.SendFib( ContainerCommand,
				 cmd_fibcontext,
				 FibSize,
				 FsaNormal,
				 FALSE,
				 NULL,
				 TRUE,
				 ( PFIB_CALLBACK )AacHba_ReadCallback,
				 ( void *)scsi_cmnd_ptr );
		// don't call done func here
		return ( 0 );
	}

err_return:
	AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

	adapter->CommFuncs.CompleteFib( cmd_fibcontext );
	adapter->CommFuncs.FreeFib( cmd_fibcontext );

	return ( -1 );
}


/*------------------------------------------------------------------------------
	AacHba_DoScsiWrite()

		Handles SCSI WRITE requests
  	
	Preconditions:
	Postconditions:
		Returns 0 on success, -1 on failure
 *----------------------------------------------------------------------------*/
int AacHba_DoScsiWrite(Scsi_Cmnd *scsi_cmnd_ptr, int ContainerId, int wait )
{
	u_long	lba;
	u_long	count;
	u_long	byte_count;
	int	Status;

	PBLOCKWRITE			BlockWriteDisk;
	PBLOCKWRITERESPONSE		BlockWriteResponse;
	uint16_t			FibSize;
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtension;
	AFA_COMM_ADAPTER		*adapter;
	PFIB_CONTEXT			cmd_fibcontext;

	CommonExtension = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	adapter         = ( AFA_COMM_ADAPTER * )CommonExtension->Adapter;

	// Get block address and transfer length
	if ( scsi_cmnd_ptr->cmnd[0] == SS_WRITE ) // 6 byte command
	{
		lba = ( ( scsi_cmnd_ptr->cmnd[1] & 0x1F ) << 16 ) | 
			( scsi_cmnd_ptr->cmnd[2] << 8 ) |
			scsi_cmnd_ptr->cmnd[3];
		count = scsi_cmnd_ptr->cmnd[4];


		if ( count == 0 )
			count = 256;
	} 
	else 
	{		
		cmn_err( CE_DEBUG, "aachba: received a write(10) command on target %d", ContainerId );
		
		lba = ( scsi_cmnd_ptr->cmnd[2] << 24 ) | ( scsi_cmnd_ptr->cmnd[3] << 16 ) | 
			( scsi_cmnd_ptr->cmnd[4] << 8 ) | scsi_cmnd_ptr->cmnd[5];

		count = ( scsi_cmnd_ptr->cmnd[7] << 8 ) | scsi_cmnd_ptr->cmnd[8];
	}	
	cmn_err( CE_DEBUG, "AacHba_DoScsiWrite[cpu %d]: lba = %lu, t = %ld", smp_processor_id(), lba, jiffies );

	//-------------------------------------------------------------------------
	// Alocate and initialize a Fib
	//  Setup BlockWrite command
	if( !( cmd_fibcontext = adapter->CommFuncs.AllocateFib( adapter ) ) ) 
	{
		cmn_err( CE_WARN, "AacHba_DoScsiWrite: AllocateFib failed\n" );
		scsi_cmnd_ptr->result = DID_ERROR << 16;
		AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
		return ( -1 );
	}

	adapter->CommFuncs.InitializeFib( cmd_fibcontext );

	BlockWriteDisk = (PBLOCKWRITE) adapter->CommFuncs.GetFibData( cmd_fibcontext );
	BlockWriteDisk->Command     = VM_CtBlockWrite;
	BlockWriteDisk->ContainerId = ContainerId;
	BlockWriteDisk->BlockNumber = lba;
	BlockWriteDisk->ByteCount   = count * 512;
	BlockWriteDisk->SgMap.SgCount = 1;

	if ( BlockWriteDisk->ByteCount > ( 64 * 1024 ) )
	{
		cmn_err( CE_WARN, "AacHba_DoScsiWrite: WRITE request is larger than 64K" );
		scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;

		AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
				SENKEY_ILLEGAL, SENCODE_INVALID_CDB_FIELD, ASENCODE_INVALID_CDB_FIELD, 
				0, 0, 7, 0 );

		goto err_return;
	}

	//-------------------------------------------------------------------------
	// Build Scatter/Gather list
	//
	if ( scsi_cmnd_ptr->use_sg )	// use scatter/gather list
	{
		struct scatterlist *scatterlist_ptr;
		int segment;
		
		scatterlist_ptr = ( struct scatterlist * )scsi_cmnd_ptr->request_buffer;

		byte_count = 0;
		for( segment = 0; segment< scsi_cmnd_ptr->use_sg; segment++ ) 
		{
			BlockWriteDisk->SgMap.SgEntry[segment].SgAddress = 
				( HOSTADDRESS )virt_to_bus( scatterlist_ptr[segment].address );
			BlockWriteDisk->SgMap.SgEntry[segment].SgByteCount = 
				scatterlist_ptr[segment].length;
			
			byte_count += scatterlist_ptr[segment].length;

			if ( BlockWriteDisk->SgMap.SgEntry[segment].SgByteCount > ( 64 * 1024 ) )
			{
				cmn_err( CE_WARN, "AacHba_DoScsiWrite: Segment byte count is larger than 64K" );
				scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;

				AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
					SENKEY_ILLEGAL, SENCODE_INVALID_CDB_FIELD, ASENCODE_INVALID_CDB_FIELD, 
					0, 0, 7, 0 );

				goto err_return;
			}

			/*
			cmn_err(CE_DEBUG, "SgEntry[%d].SgAddress = 0x%x, Byte count = 0x%x", 
					segment,
					BlockWriteDisk->SgMap.SgEntry[segment].SgAddress,
					BlockWriteDisk->SgMap.SgEntry[segment].SgByteCount); 
			*/
		}
		BlockWriteDisk->SgMap.SgCount = scsi_cmnd_ptr->use_sg;

		if( BlockWriteDisk->SgMap.SgCount > MAX_DRIVER_SG_SEGMENT_COUNT ) 
		{
			cmn_err( CE_WARN, "AacHba_DoScsiWrite: WRITE request with SgCount > %d", 
				MAX_DRIVER_SG_SEGMENT_COUNT );
			scsi_cmnd_ptr->result = DID_ERROR << 16;
			goto err_return;
		}
	} 
	else		// one piece of contiguous phys mem
	{
		BlockWriteDisk->SgMap.SgEntry[0].SgAddress = 
			( HOSTADDRESS )virt_to_bus( scsi_cmnd_ptr->request_buffer );
		BlockWriteDisk->SgMap.SgEntry[0].SgByteCount = scsi_cmnd_ptr->request_bufflen;

		byte_count = scsi_cmnd_ptr->request_bufflen;

		if ( BlockWriteDisk->SgMap.SgEntry[0].SgByteCount > ( 64 * 1024 ) )
		{
			cmn_err( CE_WARN, "AacHba_DoScsiWrite: Single segment byte count is larger than 64K" );

			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;
			AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
				SENKEY_ILLEGAL, SENCODE_INVALID_CDB_FIELD, ASENCODE_INVALID_CDB_FIELD, 
				0, 0, 7, 0 );

			goto err_return;
		}
	}

	if( byte_count != BlockWriteDisk->ByteCount )
		cmn_err( CE_WARN, "AacHba_DoScsiWrite: byte_count != BlockReadDisk->ByteCount" );

	//-------------------------------------------------------------------------
	// Now send the Fib to the adapter
	//
	FibSize = sizeof( BLOCKWRITE ) + ( ( BlockWriteDisk->SgMap.SgCount - 1 ) * sizeof( SGENTRY ) );

	if( wait ) 
	{
		Status = adapter->CommFuncs.SendFib( ContainerCommand,
				 cmd_fibcontext,
				 FibSize,
				 FsaNormal,
				 TRUE,
				 NULL,
				 TRUE,
				 NULL,
				 NULL );

		BlockWriteResponse = ( PBLOCKWRITERESPONSE ) 
			adapter->CommFuncs.GetFibData( cmd_fibcontext );	

		adapter->CommFuncs.CompleteFib(  cmd_fibcontext );
		adapter->CommFuncs.FreeFib(  cmd_fibcontext );

		if( BlockWriteResponse->Status != ST_OK )
		{
			cmn_err( CE_WARN, "AacHba_DoScsiWrite: BlockWriteCommand failed with status: %d\n", 
				BlockWriteResponse->Status );
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;;
			AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
				SENKEY_HW_ERR, SENCODE_INTERNAL_TARGET_FAILURE, ASENCODE_INTERNAL_TARGET_FAILURE, 
				0, 0, 0, 0 );
			AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
			return ( -1 );
		}
		else
			scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;

		AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );
		return ( 0 );
	} 
	else 
	{
		Status = adapter->CommFuncs.SendFib( ContainerCommand,
			 cmd_fibcontext,
			 FibSize,
			 FsaNormal,
			 FALSE,
			 NULL,
			 TRUE,
			 ( PFIB_CALLBACK )AacHba_WriteCallback,
			 ( void * )scsi_cmnd_ptr );

		// don't call done func here - it should be called by the WriteCallback
		return ( 0 );
	}

err_return:
	AacHba_CompleteScsiNoLock( scsi_cmnd_ptr );

	adapter->CommFuncs.CompleteFib( cmd_fibcontext );
	adapter->CommFuncs.FreeFib( cmd_fibcontext );

	return ( -1 );
}


/*------------------------------------------------------------------------------
	AacHba_ReadCallback()
 *----------------------------------------------------------------------------*/
void AacHba_ReadCallback(void *context, PFIB_CONTEXT FibContext, int FibStatus )
{
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtension;
	AFA_COMM_ADAPTER		*adapter;
	BLOCKREADRESPONSE		*BlockReadResponse;
	Scsi_Cmnd 			*scsi_cmnd_ptr;
	u_long				lba;
	int				ContainerId;

	scsi_cmnd_ptr = ( Scsi_Cmnd * )context;

	CommonExtension = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	adapter         = ( AFA_COMM_ADAPTER * )CommonExtension->Adapter;

	ContainerId = TARGET_LUN_TO_CONTAINER( scsi_cmnd_ptr->target, scsi_cmnd_ptr->lun );
	
	lba = ( ( scsi_cmnd_ptr->cmnd[1] & 0x1F ) << 16 ) | 
			( scsi_cmnd_ptr->cmnd[2] << 8 ) |
			scsi_cmnd_ptr->cmnd[3];
	cmn_err( CE_DEBUG, "AacHba_ReadCallback[cpu %d]: lba = %ld, t = %ld", smp_processor_id(), lba, jiffies );

	if( FibContext == 0 ) 
	{
		cmn_err( CE_WARN, "AacHba_ReadCallback: no fib context" );
		scsi_cmnd_ptr->result = DID_ERROR << 16;
		AacHba_CompleteScsi( scsi_cmnd_ptr );
		return;
	}

	BlockReadResponse = ( PBLOCKREADRESPONSE )adapter->CommFuncs.GetFibData( FibContext );

	if ( BlockReadResponse->Status == ST_OK ) 
		scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
	else 
	{
		cmn_err( CE_WARN, "AacHba_ReadCallback: read failed, status = %d\n", 
			BlockReadResponse->Status );
		scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;
		AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
			SENKEY_HW_ERR, SENCODE_INTERNAL_TARGET_FAILURE, ASENCODE_INTERNAL_TARGET_FAILURE, 
			0, 0, 0, 0 );
	}

#ifdef DEBUG_SGBUFFER
	if ( scsi_cmnd_ptr->use_sg )	// use scatter/gather list
	{
		struct scatterlist *scatterlist_ptr;
		int i, segment, count;
		char *ptr;
		
		scatterlist_ptr = ( struct scatterlist * )scsi_cmnd_ptr->request_buffer;

		for( segment = 0; segment < scsi_cmnd_ptr->use_sg; segment++ ) 
		{
			count = 0;
			ptr = scatterlist_ptr[segment].address;
			for( i = 0; i < scatterlist_ptr[segment].length; i++ )
			{
				if( *( ptr++ ) == 0xa5 )
					count++;
			}
			if( count == scatterlist_ptr[segment].length )
				cmn_err( CE_WARN, "AacHba_ReadCallback: segment %d not filled", segment );

		}
	}
#endif

	adapter->CommFuncs.CompleteFib( FibContext );
	adapter->CommFuncs.FreeFib( FibContext );

	AacHba_CompleteScsi( scsi_cmnd_ptr );
}

/*------------------------------------------------------------------------------
	AacHba_WriteCallback()
 *----------------------------------------------------------------------------*/
void AacHba_WriteCallback(void *context, PFIB_CONTEXT FibContext, int FibStatus )
/*----------------------------------------------------------------------------*/
{
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtension;
	AFA_COMM_ADAPTER		*adapter;
	BLOCKWRITERESPONSE		*BlockWriteResponse;
	Scsi_Cmnd			*scsi_cmnd_ptr;
	u_long				lba;
	int				ContainerId;

	scsi_cmnd_ptr = ( Scsi_Cmnd * )context;

	CommonExtension = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	adapter         = ( AFA_COMM_ADAPTER * )CommonExtension->Adapter;
	
	ContainerId = TARGET_LUN_TO_CONTAINER( scsi_cmnd_ptr->target, scsi_cmnd_ptr->lun );

	lba = ( ( scsi_cmnd_ptr->cmnd[1] & 0x1F ) << 16 ) | 
			( scsi_cmnd_ptr->cmnd[2] << 8 ) |
			scsi_cmnd_ptr->cmnd[3];
	cmn_err( CE_DEBUG, "AacHba_WriteCallback[cpu %d]: lba = %ld, t = %ld", smp_processor_id(), lba, jiffies );
	if( FibContext == 0 ) 
	{
		cmn_err( CE_WARN, "AacHba_WriteCallback: no fib context" );
		scsi_cmnd_ptr->result = DID_ERROR << 16;
		AacHba_CompleteScsi( scsi_cmnd_ptr );
		return;
	}

	BlockWriteResponse = (PBLOCKWRITERESPONSE) adapter->CommFuncs.GetFibData( FibContext );
	if (BlockWriteResponse->Status == ST_OK) 
		scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | GOOD;
	else 
	{
		cmn_err( CE_WARN, "AacHba_WriteCallback: write failed, status = %d\n", 
			BlockWriteResponse->Status );
		scsi_cmnd_ptr->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | CHECK_CONDITION;
		AacHba_SetSenseData( ( char * )&g_sense_data[ ContainerId ],
			SENKEY_HW_ERR, SENCODE_INTERNAL_TARGET_FAILURE, ASENCODE_INTERNAL_TARGET_FAILURE, 
			0, 0, 0, 0 );
	}

	adapter->CommFuncs.CompleteFib( FibContext );
	adapter->CommFuncs.FreeFib( FibContext );

	AacHba_CompleteScsi( scsi_cmnd_ptr );
}


/*------------------------------------------------------------------------------
	AacHba_Ioctl()

		Handle IOCTL requests

	Preconditions:
	Postconditions:
 *----------------------------------------------------------------------------*/
int AacHba_Ioctl(PCI_MINIPORT_COMMON_EXTENSION *CommonExtension, int cmd, void * arg )
/*----------------------------------------------------------------------------*/
{
	Sa_ADAPTER_EXTENSION	*AdapterExtension;
	AFA_IOCTL_CMD 		IoctlCmd;
	int 			status;
	
	AdapterExtension = ( Sa_ADAPTER_EXTENSION * )CommonExtension->MiniPort;

	cmn_err( CE_DEBUG, "AacHba_Ioctl, type = %d", cmd );
	switch( cmd )
	{
	  case FSACTL_SENDFIB:
		  cmn_err( CE_DEBUG, "FSACTL_SENDFIB" );
		break;

	  case FSACTL_AIF_THREAD:
		  cmn_err( CE_DEBUG, "FSACTL_AIF_THREAD" );
		break;

	  case FSACTL_NULL_IO_TEST:
		  cmn_err( CE_DEBUG, "FSACTL_NULL_IO_TEST" );
		break;

	  case FSACTL_SIM_IO_TEST:
		  cmn_err( CE_DEBUG, "FSACTL_SIM_IO_TEST" );
		break;

	  case FSACTL_GET_FIBTIMES:
		  cmn_err( CE_DEBUG, "FSACTL_GET_FIBTIMES" );
		break;

	  case FSACTL_ZERO_FIBTIMES:
		  cmn_err( CE_DEBUG, "FSACTL_ZERO_FIBTIMES");
		break;

	  case FSACTL_GET_VAR:
		  cmn_err( CE_DEBUG, "FSACTL_GET_VAR" );
		break;

	  case FSACTL_SET_VAR:
		  cmn_err( CE_DEBUG, "FSACTL_SET_VAR" );
		break;

	  case FSACTL_OPEN_ADAPTER_CONFIG:
		  cmn_err( CE_DEBUG, "FSACTL_OPEN_ADAPTER_CONFIG" );
		break;	

	  case FSACTL_CLOSE_ADAPTER_CONFIG:
		  cmn_err( CE_DEBUG, "FSACTL_CLOSE_ADAPTER_CONFIG" );
		break;

	  case FSACTL_QUERY_ADAPTER_CONFIG:
		  cmn_err( CE_DEBUG, "FSACTL_QUERY_ADAPTER_CONFIG" );
		break;

	  case FSACTL_OPEN_GET_ADAPTER_FIB:
		  cmn_err( CE_DEBUG, "FSACTL_OPEN_GET_ADAPTER_FIB" );
		break;

	  case FSACTL_GET_NEXT_ADAPTER_FIB:
		  cmn_err( CE_DEBUG, "FSACTL_GET_NEXT_ADAPTER_FIB" );
		break;

	  case FSACTL_CLOSE_GET_ADAPTER_FIB:
		  cmn_err( CE_DEBUG, "FSACTL_CLOSE_GET_ADAPTER_FIB" );
		break;

	  case FSACTL_MINIPORT_REV_CHECK:
		  cmn_err( CE_DEBUG, "FSACTL_MINIPORT_REV_CHECK" );
		break;

	  case FSACTL_OPENCLS_COMM_PERF_DATA:
		  cmn_err( CE_DEBUG, "FSACTL_OPENCLS_COMM_PERF_DATA" );
		break;
	
	  case FSACTL_GET_COMM_PERF_DATA:
		  cmn_err( CE_DEBUG, "FSACTL_GET_COMM_PERF_DATA" );
		break;

	  case FSACTL_QUERY_DISK:
		  cmn_err( CE_DEBUG, "FSACTL_QUERY_DISK" );
		break;
		
	  case FSACTL_DELETE_DISK:
		  cmn_err( CE_DEBUG, "FSACTL_DELETE_DISK" );
		break;

	  default:
		  cmn_err( CE_DEBUG, "Unknown ioctl: 0x%x", cmd );
	}

	IoctlCmd.cmd = cmd;
	IoctlCmd.arg = ( intptr_t )arg;
	IoctlCmd.flag = 0;
	IoctlCmd.cred_p = 0;
	IoctlCmd.rval_p = 0;

	status = AfaCommAdapterDeviceControl( CommonExtension->Adapter, &IoctlCmd );
	cmn_err( CE_DEBUG, "AAC_Ioctl, completion status = %d", status );
	return( status );
}


/*------------------------------------------------------------------------------
	AacHba_AdapterDeviceControl()

	Preconditions:
	Postconditions:
		Returns TRUE if ioctl handled, FALSE otherwise
		*ReturnStatus set to completion status
 *----------------------------------------------------------------------------*/
int AacHba_AdapterDeviceControl (void * adapter, PAFA_IOCTL_CMD IoctlCmdPtr, int *ret)
{
	int handled = TRUE;	// start out handling it.
	int status = EFAULT;

	switch( IoctlCmdPtr->cmd )
	{
		case FSACTL_QUERY_DISK:
			status = AacHba_QueryDisk( adapter, IoctlCmdPtr );
			break;

		case FSACTL_DELETE_DISK:
			status = AacHba_DeleteDisk( adapter, IoctlCmdPtr );
			break;

		case FSACTL_FORCE_DELETE_DISK:
			status = AacHba_ForceDeleteDisk( adapter, IoctlCmdPtr );
			break;

		case 2131:
			if( AacHba_ProbeContainers( ( PCI_MINIPORT_COMMON_EXTENSION * )adapter ) )
				status = -EFAULT;
			break;

		default:
			handled = FALSE;
			break;
	}

	*ret = status;

	return( handled );
}


/*------------------------------------------------------------------------------
	AacHba_QueryDisk()

	Postconditions:
		Return values
		0       = OK
		-EFAULT = Bad address
		-EINVAL = Bad container number
 *----------------------------------------------------------------------------*/

int AacHba_QueryDisk(void * adapter, PAFA_IOCTL_CMD IoctlCmdPtr )
{
	UNIX_QUERY_DISK QueryDisk;
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtensionPtr;
	fsadev_t						*fsa_dev_ptr;

	CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )adapter;
	fsa_dev_ptr = &( CommonExtensionPtr->OsDep.fsa_dev );

	if( copyin( IoctlCmdPtr->arg, &QueryDisk, sizeof( UNIX_QUERY_DISK ) ) )
		return( -EFAULT );

	if (QueryDisk.ContainerNumber == -1)
		QueryDisk.ContainerNumber = TARGET_LUN_TO_CONTAINER( QueryDisk.Target, QueryDisk.Lun );
	else 
		if( ( QueryDisk.Bus == -1 ) && ( QueryDisk.Target == -1 ) && ( QueryDisk.Lun == -1 ) )
		{
			if( QueryDisk.ContainerNumber > MAXIMUM_NUM_CONTAINERS )
				return( -EINVAL );

			QueryDisk.Instance = CommonExtensionPtr->OsDep.scsi_host_ptr->host_no;
			QueryDisk.Bus = 0;
			QueryDisk.Target = CONTAINER_TO_TARGET( QueryDisk.ContainerNumber );
			QueryDisk.Lun = CONTAINER_TO_LUN( QueryDisk.ContainerNumber );
		}
		else 
			return( -EINVAL );

	QueryDisk.Valid = fsa_dev_ptr->ContainerValid[QueryDisk.ContainerNumber];
	QueryDisk.Locked = fsa_dev_ptr->ContainerLocked[QueryDisk.ContainerNumber];
	QueryDisk.Deleted = fsa_dev_ptr->ContainerDeleted[QueryDisk.ContainerNumber];

	if( fsa_dev_ptr->ContainerDevNo[QueryDisk.ContainerNumber] == -1 )
		QueryDisk.UnMapped = TRUE;
	else
		QueryDisk.UnMapped = FALSE;

	get_sd_devname( fsa_dev_ptr->ContainerDevNo[QueryDisk.ContainerNumber], 
		QueryDisk.diskDeviceName );

	if( copyout( &QueryDisk, IoctlCmdPtr->arg, sizeof( UNIX_QUERY_DISK ) ) )
		return( -EFAULT );

	return( 0 );
}


/*------------------------------------------------------------------------------
	get_sd_devname()
 *----------------------------------------------------------------------------*/

static void get_sd_devname(long disknum, char * buffer)
{
 	if( disknum < 0 )
	{
        	sprintf(buffer, "%s", "");
		return;
	}

	if( disknum < 26 )
	        sprintf(buffer, "sd%c", 'a' + disknum);
	else {
        	unsigned int min1;
        	unsigned int min2;
        	/*
        	 * For larger numbers of disks, we need to go to a new
        	 * naming scheme.
        	 */
        	min1 = disknum / 26;
        	min2 = disknum % 26;
        	sprintf(buffer, "sd%c%c", 'a' + min1 - 1, 'a' + min2);
	}
}


/*------------------------------------------------------------------------------
	AacHba_ForceDeleteDisk()

	Postconditions:
		Return values
		0       = OK
		-EFAULT = Bad address
		-EINVAL = Bad container number
 *----------------------------------------------------------------------------*/
int AacHba_ForceDeleteDisk(void * adapter, // CommonExtensionPtr
			   PAFA_IOCTL_CMD IoctlCmdPtr )
{
	DELETE_DISK	DeleteDisk;
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtensionPtr;
	fsadev_t	*fsa_dev_ptr;

	CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )adapter;
	fsa_dev_ptr = &( CommonExtensionPtr->OsDep.fsa_dev );

	if ( copyin( IoctlCmdPtr->arg,  &DeleteDisk, sizeof( DELETE_DISK ) ) )
		return( -EFAULT );

	if ( DeleteDisk.ContainerNumber > MAXIMUM_NUM_CONTAINERS ) 
		return( -EINVAL );
	
	// Mark this container as being deleted.
	fsa_dev_ptr->ContainerDeleted[DeleteDisk.ContainerNumber] = TRUE;

	return( 0 );
}

/*
 * #REVIEW#
 * 1- Why ContainerValid = 0 down below and ContainerDeleted = TRUE above ???
 */

/*------------------------------------------------------------------------------
	AacHba_DeleteDisk()

	Postconditions:
		Return values
		0       = OK
		-EFAULT = Bad address
		-EINVAL = Bad container number
		-EBUSY  = Device locked
 *----------------------------------------------------------------------------*/

int AacHba_DeleteDisk(void * adapter,PAFA_IOCTL_CMD IoctlCmdPtr)
{
	DELETE_DISK DeleteDisk;
	PCI_MINIPORT_COMMON_EXTENSION	*CommonExtensionPtr;
	fsadev_t						*fsa_dev_ptr;

	CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )adapter;
	fsa_dev_ptr = &( CommonExtensionPtr->OsDep.fsa_dev );

	if( copyin( IoctlCmdPtr->arg, &DeleteDisk, sizeof( DELETE_DISK ) ) ) 
		return( -EFAULT );

	if( DeleteDisk.ContainerNumber > MAXIMUM_NUM_CONTAINERS )
		return( -EINVAL );
	
	// If the container is locked, it can not be deleted by the API.
	if( fsa_dev_ptr->ContainerLocked[DeleteDisk.ContainerNumber] )
		return( -EBUSY );
	else 
	{	
		// Mark the container as no longer being valid.
		fsa_dev_ptr->ContainerValid[DeleteDisk.ContainerNumber] = 0;
		fsa_dev_ptr->ContainerDevNo[DeleteDisk.ContainerNumber] = -1;
		return(0);
	}	
}


/*------------------------------------------------------------------------------
	AacHba_OpenAdapter()
 *----------------------------------------------------------------------------*/

AAC_STATUS AacHba_OpenAdapter(void * adapter)
{
	return( STATUS_SUCCESS );
}


/*------------------------------------------------------------------------------
	AacHba_CloseAdapter()
 *----------------------------------------------------------------------------*/

AAC_STATUS AacHba_CloseAdapter(void * adapter )
{
	return( STATUS_SUCCESS );
}


/*------------------------------------------------------------------------------
	AacHba_DetachAdapter()
 *----------------------------------------------------------------------------*/

void AacHba_DetachAdapter(void * adapter )
{
	AacCommDetachAdapter( adapter );
}


/*------------------------------------------------------------------------------
	AacHba_AbortScsiCommand()
 *----------------------------------------------------------------------------*/
void AacHba_AbortScsiCommand(Scsi_Cmnd *scsi_cmnd_ptr )
{
	u16 interrupt_status;
	PCI_MINIPORT_COMMON_EXTENSION *CommonExtensionPtr;

	CommonExtensionPtr = ( PCI_MINIPORT_COMMON_EXTENSION * )( scsi_cmnd_ptr->host->hostdata );
	interrupt_status = Sa_READ_USHORT( ( PSa_ADAPTER_EXTENSION )( CommonExtensionPtr->MiniPort ),
		DoorbellReg_p );
	cmn_err( CE_WARN, "interrupt_status = %d", interrupt_status );
	
	if( interrupt_status & DOORBELL_1) {	// Adapter -> Host Normal Command Ready
		cmn_err( CE_WARN, "DOORBELL_1: Adapter -> Host Normal Command Ready" );
	} 

	if( interrupt_status & DOORBELL_2) {	// Adapter -> Host Normal Response Ready
		cmn_err( CE_WARN, "DOORBELL_2: Adapter -> Host Normal Response Ready" );
	}

	if ( interrupt_status & DOORBELL_3) {	// Adapter -> Host Normal Command Not Full
		cmn_err( CE_WARN, "DOORBELL_3: Adapter -> Host Normal Command Not Full" );
	}

	if ( interrupt_status & DOORBELL_4) {	// Adapter -> Host Normal Response Not Full
		cmn_err( CE_WARN, "DOORBELL_4: Adapter -> Host Normal Response Not Full" );
	}

}


/*------------------------------------------------------------------------------
	AacHba_HandleAif()
 *----------------------------------------------------------------------------*/

int AacHba_HandleAif(void * adapter,PFIB_CONTEXT FibContext )
{
	return( FALSE );
}


/*------------------------------------------------------------------------------
	AacHba_SetSenseData()
		Fill in the sense data.
	Preconditions:
	Postconditions:
 *----------------------------------------------------------------------------*/
void AacHba_SetSenseData(
	char * sense_buf,
	u8 sense_key,
	u8 sense_code,
	u8 a_sense_code,
	u8 incorrect_length,
	u8 bit_pointer,
	unsigned field_pointer,
	unsigned long residue )
{
	sense_buf[0] = 0xF0;                	// Sense data valid, err code 70h (current error)
	sense_buf[1] = 0;								// Segment number, always zero

	if( incorrect_length )
	{
		sense_buf[2] = sense_key | 0x20;		// Set the ILI bit | sense key
		sense_buf[3] = BYTE3(residue);
		sense_buf[4] = BYTE2(residue);
		sense_buf[5] = BYTE1(residue);
		sense_buf[6] = BYTE0(residue);
	}
	else
		sense_buf[2] = sense_key;				// Sense key

	if( sense_key == SENKEY_ILLEGAL )
		sense_buf[7] = 10;						// Additional sense length
	else
		sense_buf[7] = 6;						// Additional sense length

	sense_buf[12] = sense_code; 				// Additional sense code
	sense_buf[13] = a_sense_code;				// Additional sense code qualifier
	if( sense_key == SENKEY_ILLEGAL )
	{
		sense_buf[15] = 0;

		if( sense_code == SENCODE_INVALID_PARAM_FIELD )
			sense_buf[15] = 0x80; 				// Std sense key specific field
												// Illegal parameter is in the parameter block

		if( sense_code == SENCODE_INVALID_CDB_FIELD )
			sense_buf[15] = 0xc0; 				// Std sense key specific field
												// Illegal parameter is in the CDB block
		sense_buf[15] |= bit_pointer;
		sense_buf[16] = field_pointer >> 8;	// MSB
		sense_buf[17] = field_pointer;		// LSB
	}
}

