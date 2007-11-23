/*

  Linux Driver for Mylex DAC960 PCI RAID Controllers

  Copyright 1998 by Leonard N. Zubkoff <lnz@dandelion.com>

  This program is free software; you may redistribute and/or modify it under
  the terms of the GNU General Public License Version 2 as published by the
  Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for complete details.

  The author respectfully requests that any modifications to this software be
  sent directly to him for evaluation and testing.

*/


/*
  DAC960_DriverVersion protects the private portion of this file.
*/

#ifdef DAC960_DriverVersion


/*
  Define the maximum number of DAC960 Controllers supported by this driver.
*/

#define DAC960_MaxControllers			8


/*
  Define the maximum number of Controller Channels supported by this driver.
*/

#define DAC960_MaxChannels			3


/*
  Define the maximum number of Targets per Channel supported by this driver.
*/

#define DAC960_MaxTargets			16


/*
  Define the maximum number of Logical Drives supported by any DAC960 model.
*/

#define DAC960_MaxLogicalDrives			32


/*
  Define the maximum number of Partitions allowed for each Logical Drive.
*/

#define DAC960_MaxPartitions			8
#define DAC960_MaxPartitionsBits		3


/*
  Define the maximum Driver Queue Depth and Controller Queue Depth supported
  by any DAC960 model.
*/

#define DAC960_MaxDriverQueueDepth		127
#define DAC960_MaxControllerQueueDepth		128


/*
  Define the maximum number of Scatter/Gather Segments supported by any
  DAC960 model.
*/

#define DAC960_MaxScatterGatherSegments		33


/*
  Define the DAC960 Controller Monitoring Timer Interval.
*/

#define DAC960_MonitoringTimerInterval		(7 * HZ)


/*
  Define the DAC960 Controller Secondary Monitoring Interval.
*/

#define DAC960_SecondaryMonitoringInterval	(60 * HZ)


/*
  Define the DAC960 Controller Rebuild Status Reporting Interval.
*/

#define DAC960_RebuildStatusReportingInterval	(60 * HZ)


/*
  Define the number of Command Mailboxes and Status Mailboxes used by the
  V4 Memory Mailbox Interface.
*/

#define DAC960_CommandMailboxCount		256
#define DAC960_StatusMailboxCount		1024


/*
  Define macros to extract the Controller Number, Logical Drive Number, and
  Partition Number from a Kernel Device, and to construct a Major Number, Minor
  Number, and Kernel Device from the Controller Number, Logical Drive Number,
  and Partition Number.  There is one Major Number assigned to each Controller.
  The associated Minor Number is divided into the Logical Drive Number and
  Partition Number.
*/

#define DAC960_ControllerNumber(Device) \
  (MAJOR(Device) - DAC960_MAJOR)

#define DAC960_LogicalDriveNumber(Device) \
  (MINOR(Device) >> DAC960_MaxPartitionsBits)

#define DAC960_PartitionNumber(Device) \
  (MINOR(Device) & (DAC960_MaxPartitions - 1))

#define DAC960_MajorNumber(ControllerNumber) \
  (DAC960_MAJOR + (ControllerNumber))

#define DAC960_MinorNumber(LogicalDriveNumber, PartitionNumber) \
   (((LogicalDriveNumber) << DAC960_MaxPartitionsBits) | (PartitionNumber))

#define DAC960_MinorCount			(DAC960_MaxLogicalDrives \
						 * DAC960_MaxPartitions)

#define DAC960_KernelDevice(ControllerNumber,				       \
			    LogicalDriveNumber,				       \
			    PartitionNumber)				       \
   MKDEV(DAC960_MajorNumber(ControllerNumber),				       \
	 DAC960_MinorNumber(LogicalDriveNumber, PartitionNumber))


/*
  Define the DAC960 Controller fixed Block Size and Block Size Bits.
*/

#define DAC960_BlockSize			512
#define DAC960_BlockSizeBits			9


/*
  Define the Controller Line and Message Buffer Sizes.
*/

#define DAC960_LineBufferSize			100
#define DAC960_MessageBufferSize		2048


/*
  Define the Driver Message Levels.
*/

typedef enum DAC960_MessageLevel
{
  DAC960_AnnounceLevel =			0,
  DAC960_InfoLevel =				1,
  DAC960_NoticeLevel =				2,
  DAC960_WarningLevel =				3,
  DAC960_ErrorLevel =				4,
  DAC960_CriticalLevel =			5
}
DAC960_MessageLevel_T;

static char
  *DAC960_MessageLevelMap[] =
    { KERN_NOTICE, KERN_NOTICE, KERN_NOTICE,
      KERN_WARNING, KERN_ERR, KERN_CRIT };


/*
  Define Driver Message macros.
*/

#define DAC960_Announce(Format, Arguments...) \
  DAC960_Message(DAC960_AnnounceLevel, Format, ##Arguments)

#define DAC960_Info(Format, Arguments...) \
  DAC960_Message(DAC960_InfoLevel, Format, ##Arguments)

#define DAC960_Notice(Format, Arguments...) \
  DAC960_Message(DAC960_NoticeLevel, Format, ##Arguments)

#define DAC960_Warning(Format, Arguments...) \
  DAC960_Message(DAC960_WarningLevel, Format, ##Arguments)

#define DAC960_Error(Format, Arguments...) \
  DAC960_Message(DAC960_ErrorLevel, Format, ##Arguments)

#define DAC960_Critical(Format, Arguments...) \
  DAC960_Message(DAC960_CriticalLevel, Format, ##Arguments)


/*
  Define the types of DAC960 Controllers that are supported.
*/

typedef enum
{
  DAC960_V4_Controller =			1,	/* DAC960PTL/PJ/PG */
  DAC960_V3_Controller =			2	/* DAC960PU/PD/PL */
}
DAC960_ControllerType_T;


/*
  Define a Boolean data type.
*/

typedef enum { false, true } __attribute__ ((packed)) boolean;


/*
  Define a 32 bit I/O Address data type.
*/

typedef unsigned int DAC960_IO_Address_T;


/*
  Define a 32 bit PCI Bus Address data type.
*/

typedef unsigned int DAC960_PCI_Address_T;


/*
  Define a 32 bit Bus Address data type.
*/

typedef unsigned int DAC960_BusAddress_T;


/*
  Define a 32 bit Byte Count data type.
*/

typedef unsigned int DAC960_ByteCount_T;


/*
  Define types for some of the structures that interface with the rest
  of the Linux Kernel and I/O Subsystem.
*/

typedef struct buffer_head BufferHeader_T;
typedef struct file File_T;
typedef struct file_operations FileOperations_T;
typedef struct gendisk GenericDiskInfo_T;
typedef struct hd_geometry DiskGeometry_T;
typedef struct hd_struct DiskPartition_T;
typedef struct inode Inode_T;
typedef kdev_t KernelDevice_T;
typedef unsigned long ProcessorFlags_T;
typedef struct pt_regs Registers_T;
typedef struct request IO_Request_T;
typedef struct semaphore Semaphore_T;
typedef struct timer_list Timer_T;


/*
  Define the DAC960 V4 Controller Interface Register Offsets.
*/

#define DAC960_V4_RegisterWindowSize		0x2000

typedef enum
{
  DAC960_V4_InboundDoorBellRegisterOffset =	0x0020,
  DAC960_V4_OutboundDoorBellRegisterOffset =	0x002C,
  DAC960_V4_InterruptMaskRegisterOffset =	0x0034,
  DAC960_V4_CommandOpcodeRegisterOffset =	0x1000,
  DAC960_V4_CommandIdentifierRegisterOffset =	0x1001,
  DAC960_V4_MailboxRegister2Offset =		0x1002,
  DAC960_V4_MailboxRegister3Offset =		0x1003,
  DAC960_V4_MailboxRegister4Offset =		0x1004,
  DAC960_V4_MailboxRegister5Offset =		0x1005,
  DAC960_V4_MailboxRegister6Offset =		0x1006,
  DAC960_V4_MailboxRegister7Offset =		0x1007,
  DAC960_V4_MailboxRegister8Offset =		0x1008,
  DAC960_V4_MailboxRegister9Offset =		0x1009,
  DAC960_V4_MailboxRegister10Offset =		0x100A,
  DAC960_V4_MailboxRegister11Offset =		0x100B,
  DAC960_V4_MailboxRegister12Offset =		0x100C,
  DAC960_V4_StatusCommandIdentifierRegOffset =	0x1018,
  DAC960_V4_StatusRegisterOffset =		0x101A
}
DAC960_V4_RegisterOffsets_T;


/*
  Define the structure of the DAC960 V4 Inbound Door Bell Register.
*/

typedef union DAC960_V4_InboundDoorBellRegister
{
  unsigned int All;
  struct {
    boolean NewCommand:1;				/* Bit 0 */
    boolean AcknowledgeStatus:1;			/* Bit 1 */
    boolean SoftReset:1;				/* Bit 2 */
    unsigned int :29;					/* Bits 3-31 */
  } Write;
  struct {
    boolean MailboxFull:1;				/* Bit 0 */
    unsigned int :31;					/* Bits 1-31 */
  } Read;
}
DAC960_V4_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 V4 Outbound Door Bell Register.
*/

typedef union DAC960_V4_OutboundDoorBellRegister
{
  unsigned int All;
  struct {
    boolean AcknowledgeInterrupt:1;			/* Bit 0 */
    unsigned int :31;					/* Bits 1-31 */
  } Write;
  struct {
    boolean StatusAvailable:1;				/* Bit 0 */
    unsigned int :31;					/* Bits 1-31 */
  } Read;
}
DAC960_V4_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 V4 Interrupt Mask Register.
*/

typedef union DAC960_V4_InterruptMaskRegister
{
  unsigned int All;
  struct {
    unsigned int MessageUnitInterruptMask1:2;		/* Bits 0-1 */
    boolean DisableInterrupts:1;			/* Bit 2 */
    unsigned int MessageUnitInterruptMask2:5;		/* Bits 3-7 */
    unsigned int Reserved0:24;				/* Bits 8-31 */
  } Bits;
}
DAC960_V4_InterruptMaskRegister_T;


/*
  Define the DAC960 V3 Controller Interface Register Offsets.
*/

#define DAC960_V3_RegisterWindowSize		0x80

typedef enum
{
  DAC960_V3_CommandOpcodeRegisterOffset =	0x00,
  DAC960_V3_CommandIdentifierRegisterOffset =	0x01,
  DAC960_V3_MailboxRegister2Offset =		0x02,
  DAC960_V3_MailboxRegister3Offset =		0x03,
  DAC960_V3_MailboxRegister4Offset =		0x04,
  DAC960_V3_MailboxRegister5Offset =		0x05,
  DAC960_V3_MailboxRegister6Offset =		0x06,
  DAC960_V3_MailboxRegister7Offset =		0x07,
  DAC960_V3_MailboxRegister8Offset =		0x08,
  DAC960_V3_MailboxRegister9Offset =		0x09,
  DAC960_V3_MailboxRegister10Offset =		0x0A,
  DAC960_V3_MailboxRegister11Offset =		0x0B,
  DAC960_V3_MailboxRegister12Offset =		0x0C,
  DAC960_V3_StatusCommandIdentifierRegOffset =	0x0D,
  DAC960_V3_StatusRegisterOffset =		0x0E,
  DAC960_V3_InboundDoorBellRegisterOffset =	0x40,
  DAC960_V3_OutboundDoorBellRegisterOffset =	0x41,
  DAC960_V3_InterruptEnableRegisterOffset =	0x43
}
DAC960_V3_RegisterOffsets_T;


/*
  Define the structure of the DAC960 V3 Inbound Door Bell Register.
*/

typedef union DAC960_V3_InboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean NewCommand:1;				/* Bit 0 */
    boolean AcknowledgeStatus:1;			/* Bit 1 */
    unsigned char :1;					/* Bit 2 */
    boolean SoftReset:1;				/* Bit 3 */
    unsigned char :4;					/* Bits 4-7 */
  } Write;
  struct {
    boolean MailboxFull:1;				/* Bit 0 */
    unsigned char :7;					/* Bits 1-7 */
  } Read;
}
DAC960_V3_InboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 V3 Outbound Door Bell Register.
*/

typedef union DAC960_V3_OutboundDoorBellRegister
{
  unsigned char All;
  struct {
    boolean AcknowledgeInterrupt:1;			/* Bit 0 */
    unsigned char :7;					/* Bits 1-7 */
  } Write;
  struct {
    boolean StatusAvailable:1;				/* Bit 0 */
    unsigned char :7;					/* Bits 1-7 */
  } Read;
}
DAC960_V3_OutboundDoorBellRegister_T;


/*
  Define the structure of the DAC960 V3 Interrupt Enable Register.
*/

typedef union DAC960_V3_InterruptEnableRegister
{
  unsigned char All;
  struct {
    boolean EnableInterrupts:1;				/* Bit 0 */
    unsigned char :7;					/* Bits 1-7 */
  } Bits;
}
DAC960_V3_InterruptEnableRegister_T;


/*
  Define the DAC960 Command Identifier type.
*/

typedef unsigned char DAC960_CommandIdentifier_T;


/*
  Define the DAC960 Command Opcodes.
*/

typedef enum
{
  /* I/O Commands */
  DAC960_ReadExtended =				0x33,
  DAC960_WriteExtended =			0x34,
  DAC960_ReadAheadExtended =			0x35,
  DAC960_ReadExtendedWithScatterGather =	0xB3,
  DAC960_WriteExtendedWithScatterGather =	0xB4,
  DAC960_Read =					0x36,
  DAC960_ReadWithOldScatterGather =		0xB6,
  DAC960_Write =				0x37,
  DAC960_WriteWithOldScatterGather =		0xB7,
  DAC960_DCDB =					0x04,
  DAC960_DCDBWithScatterGather =		0x84,
  DAC960_Flush =				0x0A,
  /* Controller Status Related Commands */
  DAC960_Enquiry =				0x53,
  DAC960_Enquiry2 =				0x1C,
  DAC960_GetLogicalDriveElement =		0x55,
  DAC960_GetLogicalDriveInformation =		0x19,
  DAC960_IOPortRead =				0x39,
  DAC960_IOPortWrite =				0x3A,
  DAC960_GetSDStats =				0x3E,
  DAC960_GetPDStats =				0x3F,
  DAC960_PerformEventLogOperation =		0x72,
  /* Device Related Commands */
  DAC960_StartDevice =				0x10,
  DAC960_GetDeviceState =			0x50,
  DAC960_StopChannel =				0x13,
  DAC960_StartChannel =				0x12,
  DAC960_ResetChannel =				0x1A,
  /* Commands Associated with Data Consistency and Errors */
  DAC960_Rebuild =				0x09,
  DAC960_RebuildAsync =				0x16,
  DAC960_CheckConsistency =			0x0F,
  DAC960_CheckConsistencyAsync =		0x1E,
  DAC960_RebuildStat =				0x0C,
  DAC960_GetRebuildProgress =			0x27,
  DAC960_RebuildControl =			0x1F,
  DAC960_ReadBadBlockTable =			0x0B,
  DAC960_ReadBadDataTable =			0x25,
  DAC960_ClearBadDataTable =			0x26,
  DAC960_GetErrorTable =			0x17,
  DAC960_AddCapacityAsync =			0x2A,
  /* Configuration Related Commands */
  DAC960_ReadConfig2 =				0x3D,
  DAC960_WriteConfig2 =				0x3C,
  DAC960_ReadConfigurationOnDisk =		0x4A,
  DAC960_WriteConfigurationOnDisk =		0x4B,
  DAC960_ReadConfiguration =			0x4E,
  DAC960_ReadBackupConfiguration =		0x4D,
  DAC960_WriteConfiguration =			0x4F,
  DAC960_AddConfiguration =			0x4C,
  DAC960_ReadConfigurationLabel =		0x48,
  DAC960_WriteConfigurationLabel =		0x49,
  /* Firmware Upgrade Related Commands */
  DAC960_LoadImage =				0x20,
  DAC960_StoreImage =				0x21,
  DAC960_ProgramImage =				0x22,
  /* Diagnostic Commands */
  DAC960_SetDiagnosticMode =			0x31,
  DAC960_RunDiagnostic =			0x32,
  /* Subsystem Service Commands */
  DAC960_GetSubsystemData =			0x70,
  DAC960_SetSubsystemParameters =		0x71
}
__attribute__ ((packed))
DAC960_CommandOpcode_T;


/*
  Define the DAC960 Command Status Codes.
*/

#define DAC960_NormalCompletion			0x0000	/* Common */
#define DAC960_CheckConditionReceived		0x0002	/* Common */
#define DAC960_NoDeviceAtAddress		0x0102	/* Common */
#define DAC960_InvalidDeviceAddress		0x0105	/* Common */
#define DAC960_InvalidParameter			0x0105	/* Common */
#define DAC960_IrrecoverableDataError		0x0001	/* I/O */
#define DAC960_LogicalDriveNonexistentOrOffline	0x0002	/* I/O */
#define DAC960_AccessBeyondEndOfLogicalDrive	0x0105	/* I/O */
#define DAC960_BadDataEncountered		0x010C	/* I/O */
#define DAC960_DeviceBusy			0x0008	/* DCDB */
#define DAC960_DeviceNonresponsive		0x000E	/* DCDB */
#define DAC960_CommandTerminatedAbnormally	0x000F	/* DCDB */
#define DAC960_UnableToStartDevice		0x0002	/* Device */
#define DAC960_InvalidChannelOrTarget		0x0105	/* Device */
#define DAC960_ChannelBusy			0x0106	/* Device */
#define DAC960_ChannelNotStopped		0x0002	/* Device */
#define DAC960_AttemptToRebuildOnlineDrive	0x0002	/* Consistency */
#define DAC960_RebuildBadBlocksEncountered	0x0003	/* Consistency */
#define DAC960_NewDiskFailedDuringRebuild	0x0004	/* Consistency */
#define DAC960_RebuildOrCheckAlreadyInProgress	0x0106	/* Consistency */
#define DAC960_DependentDiskIsDead		0x0002	/* Consistency */
#define DAC960_InconsistentBlocksFound		0x0003	/* Consistency */
#define DAC960_InvalidOrNonredundantLogicalDrive 0x0105	/* Consistency */
#define DAC960_NoRebuildOrCheckInProgress	0x0105	/* Consistency */
#define DAC960_RebuildInProgress_DataValid	0x0000	/* Consistency */
#define DAC960_RebuildFailed_LogicalDriveFailure 0x0002	/* Consistency */
#define DAC960_RebuildFailed_BadBlocksOnOther	0x0003	/* Consistency */
#define DAC960_RebuildFailed_NewDriveFailed	0x0004	/* Consistency */
#define DAC960_RebuildSuccessful		0x0100	/* Consistency */
#define DAC960_AddCapacityInProgress		0x0004	/* Consistency */
#define DAC960_AddCapacityFailedOrSuspended	0x00F4	/* Consistency */
#define DAC960_Config2ChecksumError		0x0002	/* Configuration */
#define DAC960_ConfigurationSuspended		0x0106	/* Configuration */
#define DAC960_FailedToConfigureNVRAM		0x0105	/* Configuration */
#define DAC960_ConfigurationNotSavedStateChange	0x0106	/* Configuration */
#define DAC960_SubsystemNotInstalled		0x0001	/* Subsystem */
#define DAC960_SubsystemFailed			0x0002	/* Subsystem */
#define DAC960_SubsystemBusy			0x0106	/* Subsystem */

typedef unsigned short DAC960_CommandStatus_T;


/*
  Define the Enquiry reply structure.
*/

typedef struct DAC960_Enquiry
{
  unsigned char NumberOfLogicalDrives;			/* Byte 0 */
  unsigned int :24;					/* Bytes 1-3 */
  unsigned int LogicalDriveSizes[32];			/* Bytes 4-131 */
  unsigned short FlashAge;				/* Bytes 132-133 */
  struct {
    boolean DeferredWriteError:1;			/* Byte 134 Bit 0 */
    boolean BatteryLow:1;				/* Byte 134 Bit 1 */
    unsigned char :6;					/* Byte 134 Bits 2-7 */
  } StatusFlags;
  unsigned char :8;					/* Byte 135 */
  unsigned char MinorFirmwareVersion;			/* Byte 136 */
  unsigned char MajorFirmwareVersion;			/* Byte 137 */
  enum {
    DAC960_NoStandbyRebuildOrCheckInProgress =			0x00,
    DAC960_StandbyRebuildInProgress =				0x01,
    DAC960_BackgroundRebuildInProgress =			0x02,
    DAC960_BackgroundCheckInProgress =				0x03,
    DAC960_StandbyRebuildCOmpletedWithError =			0xFF,
    DAC960_BackgroundRebuildOrCheckFailed_DriveFailed =		0xF0,
    DAC960_BackgroundRebuildOrCheckFailed_LogicalDriveFailed =	0xF1,
    DAC960_BackgroundRebuildOrCheckFailed_OtherCauses =		0xF2,
    DAC960_BackgroundRebuildOrCheckSuccessfullyTerminated =	0xF3
  } __attribute__ ((packed)) RebuildFlag;		/* Byte 138 */
  unsigned char MaxCommands;				/* Byte 139 */
  unsigned char OfflineLogicalDriveCount;		/* Byte 140 */
  unsigned char :8;					/* Byte 141 */
  unsigned short EventLogSequenceNumber;		/* Bytes 142-143 */
  unsigned char CriticalLogicalDriveCount;		/* Byte 144 */
  unsigned int :24;					/* Bytes 145-147 */
  unsigned char DeadDriveCount;				/* Byte 148 */
  unsigned char :8;					/* Byte 149 */
  unsigned char RebuildCount;				/* Byte 150 */
  struct {
    unsigned char :3;					/* Byte 151 Bits 0-2 */
    boolean BatteryBackupUnitPresent:1;			/* Byte 151 Bit 3 */
    unsigned char :3;					/* Byte 151 Bits 4-6 */
    unsigned char :1;					/* Byte 151 Bit 7 */
  } MiscFlags;
  struct {
    unsigned char TargetID;
    unsigned char Channel;
  } DeadDrives[21];					/* Bytes 152-194 */
  unsigned char Reserved[62];				/* Bytes 195-255 */
}
__attribute__ ((packed))
DAC960_Enquiry_T;


/*
  Define the Enquiry2 reply structure.
*/

typedef struct DAC960_Enquiry2
{
  struct {
    enum {
      DAC960_P_PD_PU =				0x01,
      DAC960_PL =				0x02,
      DAC960_PG =				0x10,
      DAC960_PJ =				0x11,
      DAC960_PTL_0 =				0x14,
      DAC960_PTL_1 =				0x16
    } __attribute__ ((packed)) SubModel;		/* Byte 0 */
    unsigned char ActualChannels;			/* Byte 1 */
    enum {
      DAC960_FiveChannelBoard =			0x01,
      DAC960_ThreeChannelBoard =		0x02,
      DAC960_TwoChannelBoard =			0x03,
      DAC960_ThreeChannelASIC_DAC =		0x04
    } __attribute__ ((packed)) Model;			/* Byte 2 */
    enum {
      DAC960_EISA_Controller =			0x01,
      DAC960_MicroChannel_Controller =		0x02,
      DAC960_PCI_Controller =			0x03,
      DAC960_SCSItoSCSI_Controller =		0x08
    } __attribute__ ((packed)) ProductFamily;		/* Byte 3 */
  } HardwareID;						/* Bytes 0-3 */
  /* MajorVersion.MinorVersion-FirmwareType-TurnID */
  struct {
    unsigned char MajorVersion;				/* Byte 4 */
    unsigned char MinorVersion;				/* Byte 5 */
    unsigned char TurnID;				/* Byte 6 */
    char FirmwareType;					/* Byte 7 */
  } FirmwareID;						/* Bytes 4-7 */
  unsigned char :8;					/* Byte 8 */
  unsigned int :24;					/* Bytes 9-11 */
  unsigned char ConfiguredChannels;			/* Byte 12 */
  unsigned char ActualChannels;				/* Byte 13 */
  unsigned char MaxTargets;				/* Byte 14 */
  unsigned char MaxTags;				/* Byte 15 */
  unsigned char MaxLogicalDrives;			/* Byte 16 */
  unsigned char MaxArms;				/* Byte 17 */
  unsigned char MaxSpans;				/* Byte 18 */
  unsigned char :8;					/* Byte 19 */
  unsigned int :32;					/* Bytes 20-23 */
  unsigned int MemorySize;				/* Bytes 24-27 */
  unsigned int CacheSize;				/* Bytes 28-31 */
  unsigned int FlashMemorySize;				/* Bytes 32-35 */
  unsigned int NonVolatileMemorySize;			/* Bytes 36-39 */
  struct {
    enum {
      DAC960_DRAM =				0x00,
      DAC960_EDO =				0x01
    } __attribute__ ((packed)) RamType:3;		/* Byte 40 Bits 0-2 */
    enum {
      DAC960_None =				0x00,
      DAC960_Parity =				0x01,
      DAC960_ECC =				0x02
    } __attribute__ ((packed)) ErrorCorrection:3;	/* Byte 40 Bits 3-5 */
    boolean FastPageMode:1;				/* Byte 40 Bit 6 */
    boolean LowPowerMemory:1;				/* Byte 40 Bit 7 */
    unsigned char :8;					/* Bytes 41 */
  } MemoryType;
  unsigned short ClockSpeed;				/* Bytes 42-43 */
  unsigned short MemorySpeed;				/* Bytes 44-45 */
  unsigned short HardwareSpeed;				/* Bytes 46-47 */
  unsigned int :32;					/* Bytes 48-51 */
  unsigned int :32;					/* Bytes 52-55 */
  unsigned char :8;					/* Byte 56 */
  unsigned char :8;					/* Byte 57 */
  unsigned short :16;					/* Bytes 58-59 */
  unsigned short MaxCommands;				/* Bytes 60-61 */
  unsigned short MaxScatterGatherEntries;		/* Bytes 62-63 */
  unsigned short MaxDriveCommands;			/* Bytes 64-65 */
  unsigned short MaxIODescriptors;			/* Bytes 66-67 */
  unsigned short MaxCombinedSectors;			/* Bytes 68-69 */
  unsigned char Latency;				/* Byte 70 */
  unsigned char :8;					/* Byte 71 */
  unsigned char SCSITimeout;				/* Byte 72 */
  unsigned char :8;					/* Byte 73 */
  unsigned short MinFreeLines;				/* Bytes 74-75 */
  unsigned int :32;					/* Bytes 76-79 */
  unsigned int :32;					/* Bytes 80-83 */
  unsigned char RebuildRateConstant;			/* Byte 84 */
  unsigned char :8;					/* Byte 85 */
  unsigned char :8;					/* Byte 86 */
  unsigned char :8;					/* Byte 87 */
  unsigned int :32;					/* Bytes 88-91 */
  unsigned int :32;					/* Bytes 92-95 */
  unsigned short PhysicalDriveBlockSize;		/* Bytes 96-97 */
  unsigned short LogicalDriveBlockSize;			/* Bytes 98-99 */
  unsigned short MaxBlocksPerCommand;			/* Bytes 100-101 */
  unsigned short BlockFactor;				/* Bytes 102-103 */
  unsigned short CacheLineSize;				/* Bytes 104-105 */
  struct {
    enum {
      DAC960_Narrow_8bit =			0x00,
      DAC960_Wide_16bit =			0x01,
      DAC960_Wide_32bit =			0x02
    } __attribute__ ((packed)) BusWidth:2;		/* Byte 106 Bits 0-1 */
    enum {
      DAC960_Fast =				0x00,
      DAC960_Ultra =				0x01,
    } __attribute__ ((packed)) BusSpeed:2;		/* Byte 106 Bits 2-3 */
    boolean Differential:1;				/* Byte 106 Bit 4 */
    unsigned char :3;					/* Byte 106 Bits 5-7 */
  } SCSICapability;
  unsigned char :8;					/* Byte 107 */
  unsigned int :32;					/* Bytes 108-111 */
  unsigned short FirmwareBuildNumber;			/* Bytes 112-113 */
  enum {
    DAC960_AEMI =				0x01,
    DAC960_OEM1 =				0x02,
    DAC960_OEM2 =				0x04,
    DAC960_OEM3 =				0x08,
    DAC960_Conner =				0x10,
    DAC960_SAFTE =				0x20
  } __attribute__ ((packed)) FaultManagementType;	/* Byte 114 */
  unsigned char :8;					/* Byte 115 */
  struct {
    boolean Clustering:1;				/* Byte 116 Bit 0 */
    boolean MylexOnlineRAIDExpansion:1;			/* Byte 116 Bit 1 */
    unsigned int :30;					/* Bytes 116-119 */
  } FirmwareFeatures;
  unsigned int :32;					/* Bytes 120-123 */
  unsigned int :32;					/* Bytes 124-127 */
}
DAC960_Enquiry2_T;


/*
  Define the Get Logical Drive Information reply structure.
*/

typedef struct DAC960_LogicalDriveInformation
{
  unsigned int LogicalDriveSize;			/* Bytes 0-3 */
  enum {
    DAC960_LogicalDrive_Online =		0x03,
    DAC960_LogicalDrive_Critical =		0x04,
    DAC960_LogicalDrive_Offline =		0xFF
  } __attribute__ ((packed)) LogicalDriveState;		/* Byte 4 */
  unsigned char RAIDLevel:7;				/* Byte 5 Bits 0-6 */
  boolean WriteBack:1;					/* Byte 5 Bit 7 */
  unsigned int :16;					/* Bytes 6-7 */
}
DAC960_LogicalDriveInformation_T;


/*
  Define the Perform Event Log Operation Types.
*/

typedef enum
{
  DAC960_GetEventLogEntry =				0x00
}
__attribute__ ((packed))
DAC960_PerformEventLogOpType_T;


/*
  Define the Get Event Log Entry reply structure.
*/

typedef struct DAC960_EventLogEntry
{
  unsigned char MessageType;				/* Byte 0 */
  unsigned char MessageLength;				/* Byte 1 */
  unsigned char TargetID:5;				/* Byte 2 Bits 0-4 */
  unsigned char Channel:3;				/* Byte 2 Bits 5-7 */
  unsigned char LogicalUnit:6;				/* Byte 3 Bits 0-5 */
  unsigned char :2;					/* Byte 3 Bits 6-7 */
  unsigned short SequenceNumber;			/* Bytes 4-5 */
  unsigned char ErrorCode:7;				/* Byte 6 Bits 0-6 */
  boolean Valid:1;					/* Byte 6 Bit 7 */
  unsigned char SegmentNumber;				/* Byte 7 */
  unsigned char SenseKey:4;				/* Byte 8 Bits 0-3 */
  unsigned char :1;					/* Byte 8 Bit 4 */
  boolean ILI:1;					/* Byte 8 Bit 5 */
  boolean EOM:1;					/* Byte 8 Bit 6 */
  boolean Filemark:1;					/* Byte 8 Bit 7 */
  unsigned char Information[4];				/* Bytes 9-12 */
  unsigned char AdditionalSenseLength;			/* Byte 13 */
  unsigned char CommandSpecificInformation[4];		/* Bytes 14-17 */
  unsigned char AdditionalSenseCode;			/* Byte 18 */
  unsigned char AdditionalSenseCodeQualifier;		/* Byte 19 */
  unsigned char Dummy[12];				/* Bytes 20-31 */
}
DAC960_EventLogEntry_T;

#define DAC960_EventMessagesCount		13

static char
  *DAC960_EventMessages[DAC960_EventMessagesCount] =
     { "killed because write recovery failed",
       "killed because of SCSI bus reset failure",
       "killed because of double check condition",
       "killed because it was removed",
       "killed because of gross error on SCSI chip",
       "killed because of bad tag returned from drive",
       "killed because of timeout on SCSI command",
       "killed because of reset SCSI command issued from system",
       "killed because busy or parity error count exceeded limit",
       "killed because of 'kill drive' command from system",
       "killed because of selection timeout",
       "killed due to SCSI phase sequence error",
       "killed due to unknown status" };


/*
  Define the Get Device State reply structure.
*/

typedef struct DAC960_DeviceState
{
  boolean Present:1;					/* Byte 0 Bit 0 */
  unsigned char :7;					/* Byte 0 Bits 1-7 */
  enum {
    DAC960_OtherType =				0x00,
    DAC960_DiskType =				0x01,
    DAC960_SequentialType =			0x02,
    DAC960_CDROM_or_WORM_Type =			0x03
    } __attribute__ ((packed)) DeviceType:2;		/* Byte 1 Bits 0-1 */
  boolean :1;						/* Byte 1 Bit 2 */
  boolean Fast20:1;					/* Byte 1 Bit 3 */
  boolean Sync:1;					/* Byte 1 Bit 4 */
  boolean Fast:1;					/* Byte 1 Bit 5 */
  boolean Wide:1;					/* Byte 1 Bit 6 */
  boolean TaggedQueuingSupported:1;			/* Byte 1 Bit 7 */
  enum {
    DAC960_Device_Dead =			0x00,
    DAC960_Device_WriteOnly =			0x02,
    DAC960_Device_Online =			0x03,
    DAC960_Device_Standby =			0x10
  } __attribute__ ((packed)) DeviceState;		/* Byte 2 */
  unsigned char :8;					/* Byte 3 */
  unsigned char SynchronousMultiplier;			/* Byte 4 */
  unsigned char SynchronousOffset:5;			/* Byte 5 Bits 0-4 */
  unsigned char :3;					/* Byte 5 Bits 5-7 */
  unsigned long DiskSize __attribute__ ((packed));	/* Bytes 6-9 */
}
DAC960_DeviceState_T;


/*
  Define the Get Rebuild Progress reply structure.
*/

typedef struct DAC960_RebuildProgress
{
  unsigned int LogicalDriveNumber;			/* Bytes 0-3 */
  unsigned int LogicalDriveSize;			/* Bytes 4-7 */
  unsigned int RemainingBlocks;				/* Bytes 8-11 */
}
DAC960_RebuildProgress_T;


/*
  Define the Config2 reply structure.
*/

typedef struct DAC960_Config2
{
  unsigned char :1;					/* Byte 0 Bit 0 */
  boolean ActiveNegationEnabled:1;			/* Byte 0 Bit 1 */
  unsigned char :5;					/* Byte 0 Bits 2-6 */
  boolean NoRescanIfResetReceivedDuringScan:1;		/* Byte 0 Bit 7 */
  boolean StorageWorksSupportEnabled:1;			/* Byte 1 Bit 0 */
  boolean HewlettPackardSupportEnabled:1;		/* Byte 1 Bit 1 */
  boolean NoDisconnectOnFirstCommand:1;			/* Byte 1 Bit 2 */
  unsigned char :2;					/* Byte 1 Bits 3-4 */
  boolean AEMI_ARM:1;					/* Byte 1 Bit 5 */
  boolean AEMI_OFM:1;					/* Byte 1 Bit 6 */
  unsigned char :1;					/* Byte 1 Bit 7 */
  enum {
    DAC960_OEMID_Mylex =			0x00,
    DAC960_OEMID_IBM =				0x08,
    DAC960_OEMID_HP =				0x0A,
    DAC960_OEMID_DEC =				0x0C,
    DAC960_OEMID_Siemens =			0x10,
    DAC960_OEMID_Intel =			0x12
  } __attribute__ ((packed)) OEMID;			/* Byte 2 */
  unsigned char OEMModelNumber;				/* Byte 3 */
  unsigned char PhysicalSector;				/* Byte 4 */
  unsigned char LogicalSector;				/* Byte 5 */
  unsigned char BlockFactor;				/* Byte 6 */
  boolean ReadAheadEnabled:1;				/* Byte 7 Bit 0 */
  boolean LowBIOSDelay:1;				/* Byte 7 Bit 1 */
  unsigned char :2;					/* Byte 7 Bits 2-3 */
  boolean ReassignRestrictedToOneSector:1;		/* Byte 7 Bit 4 */
  unsigned char :1;					/* Byte 7 Bit 5 */
  boolean ForceUnitAccessDuringWriteRecovery:1;		/* Byte 7 Bit 6 */
  boolean EnableLeftSymmetricRAID5Algorithm:1;		/* Byte 7 Bit 7 */
  unsigned char DefaultRebuildRate;			/* Byte 8 */
  unsigned char :8;					/* Byte 9 */
  unsigned char BlocksPerCacheLine;			/* Byte 10 */
  unsigned char BlocksPerStripe;			/* Byte 11 */
  struct {
    enum {
      DAC960_Async =				0x00,
      DAC960_Sync_8MHz =			0x01,
      DAC960_Sync_5MHz =			0x02,
      DAC960_Sync_10or20MHz =			0x03	/* Bits 0-1 */
    } __attribute__ ((packed)) Speed:2;
    boolean Force8Bit:1;				/* Bit 2 */
    boolean DisableFast20:1;				/* Bit 3 */
    unsigned char :3;					/* Bits 4-6 */
    boolean EnableTaggedQueuing:1;			/* Bit 7 */
  } __attribute__ ((packed)) ChannelParameters[6];	/* Bytes 12-17 */
  unsigned char SCSIInitiatorID;			/* Byte 18 */
  unsigned char :8;					/* Byte 19 */
  enum {
    DAC960_StartupMode_ControllerSpinUp =	0x00,
    DAC960_StartupMode_PowerOnSpinUp =		0x01
  } __attribute__ ((packed)) StartupMode;		/* Byte 20 */
  unsigned char SimultaneousDeviceSpinUpCount;		/* Byte 21 */
  unsigned char SecondsDelayBetweenSpinUps;		/* Byte 22 */
  unsigned char Reserved1[29];				/* Bytes 23-51 */
  boolean BIOSDisabled:1;				/* Byte 52 Bit 0 */
  boolean CDROMBootEnabled:1;				/* Byte 52 Bit 1 */
  unsigned char :3;					/* Byte 52 Bits 2-4 */
  enum {
    DAC960_Geometry_128_32 =			0x00,
    DAC960_Geometry_255_63 =			0x01,
    DAC960_Geometry_Reserved1 =			0x02,
    DAC960_Geometry_Reserved2 =			0x03
  } __attribute__ ((packed)) DriveGeometry:2;		/* Byte 52 Bits 5-6 */
  unsigned char :1;					/* Byte 52 Bit 7 */
  unsigned char Reserved2[9];				/* Bytes 53-61 */
  unsigned short Checksum;				/* Bytes 62-63 */
}
DAC960_Config2_T;


/*
  Define the Scatter/Gather List Type 1 32 Bit Address 32 Bit Byte Count
  structure.
*/

typedef struct DAC960_ScatterGatherSegment
{
  DAC960_BusAddress_T SegmentDataPointer;		/* Bytes 0-3 */
  DAC960_ByteCount_T SegmentByteCount;			/* Bytes 4-7 */
}
DAC960_ScatterGatherSegment_T;


/*
  Define the 13 Byte DAC960 Command Mailbox structure.  Bytes 13-15 are
  not used.  The Command Mailbox structure is padded to 16 bytes for
  efficient access.
*/

typedef union DAC960_CommandMailbox
{
  unsigned int Words[4];				/* Words 0-3 */
  unsigned char Bytes[16];				/* Bytes 0-15 */
  struct {
    DAC960_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Dummy[14];				/* Bytes 2-15 */
  } __attribute__ ((packed)) Common;
  struct {
    DAC960_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Dummy1[6];				/* Bytes 2-7 */
    DAC960_BusAddress_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy2[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3;
  struct {
    DAC960_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char Channel;				/* Byte 2 */
    unsigned char TargetID;				/* Byte 3 */
    unsigned char Dummy1[4];				/* Bytes 4-7 */
    DAC960_BusAddress_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy2[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3D;
  struct {
    DAC960_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    DAC960_PerformEventLogOpType_T OperationType;	/* Byte 2 */
    unsigned char OperationQualifier;			/* Byte 3 */
    unsigned short SequenceNumber;			/* Bytes 4-5 */
    unsigned char Dummy1[2];				/* Bytes 6-7 */
    DAC960_BusAddress_T BusAddress;			/* Bytes 8-11 */
    unsigned char Dummy2[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) Type3E;
  struct {
    DAC960_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    struct {
      unsigned short TransferLength:11;			/* Bytes 2-3 */
      unsigned char LogicalDriveNumber:5;		/* Byte 3 Bits 3-7 */
    } __attribute__ ((packed)) LD;
    unsigned int LogicalBlockAddress;			/* Bytes 4-7 */
    DAC960_BusAddress_T BusAddress;			/* Bytes 8-11 */
    unsigned char ScatterGatherCount:6;			/* Byte 12 Bits 0-5 */
    enum {
      DAC960_ScatterGather_32BitAddress_32BitByteCount =	0x0,
      DAC960_ScatterGather_32BitAddress_16BitByteCount =	0x1,
      DAC960_ScatterGather_32BitByteCount_32BitAddress =	0x2,
      DAC960_ScatterGather_16BitByteCount_32BitAddress =	0x3
    } __attribute__ ((packed)) ScatterGatherType:2;	/* Byte 12 Bits 6-7 */
    unsigned char Dummy[3];				/* Bytes 13-15 */
  } __attribute__ ((packed)) Type5;
  struct {
    DAC960_CommandOpcode_T CommandOpcode;		/* Byte 0 */
    DAC960_CommandIdentifier_T CommandIdentifier;	/* Byte 1 */
    unsigned char CommandOpcode2;			/* Byte 2 */
    unsigned char :8;					/* Byte 3 */
    DAC960_BusAddress_T CommandMailboxesBusAddress;	/* Bytes 4-7 */
    DAC960_BusAddress_T StatusMailboxesBusAddress;	/* Bytes 8-11 */
    unsigned char Dummy[4];				/* Bytes 12-15 */
  } __attribute__ ((packed)) TypeX;
}
DAC960_CommandMailbox_T;


/*
  Define the DAC960 V4 Controller Command Mailbox structure.
*/

typedef DAC960_CommandMailbox_T DAC960_V4_CommandMailbox_T;


/*
  Define the DAC960 V4 Controller Status Mailbox structure.
*/

typedef union DAC960_V4_StatusMailbox
{
  unsigned int Word;					/* Bytes 0-3 */
  struct {
    DAC960_CommandIdentifier_T CommandIdentifier;	/* Byte 0 */
    unsigned char :7;					/* Byte 1 Bits 0-6 */
    boolean Valid:1;					/* Byte 1 Bit 7 */
    DAC960_CommandStatus_T CommandStatus;		/* Bytes 2-3 */
  } Fields;
}
DAC960_V4_StatusMailbox_T;


/*
  Define the DAC960 Driver Command Types.
*/

typedef enum
{
  DAC960_ReadCommand =				1,
  DAC960_WriteCommand =				2,
  DAC960_ReadRetryCommand =			3,
  DAC960_WriteRetryCommand =			4,
  DAC960_MonitoringCommand =			5,
  DAC960_ImmediateCommand =			6
}
DAC960_CommandType_T;


/*
  Define the DAC960 Driver Command structure.
*/

typedef struct DAC960_Command
{
  DAC960_CommandType_T CommandType;
  DAC960_CommandMailbox_T CommandMailbox;
  DAC960_CommandStatus_T CommandStatus;
  struct DAC960_Controller *Controller;
  struct DAC960_Command *Next;
  Semaphore_T *Semaphore;
  unsigned int LogicalDriveNumber;
  unsigned int BlockNumber;
  unsigned int BlockCount;
  unsigned int SegmentCount;
  BufferHeader_T *BufferHeader;
  DAC960_ScatterGatherSegment_T
    ScatterGatherList[DAC960_MaxScatterGatherSegments];
}
DAC960_Command_T;


/*
  Define the DAC960 Driver Controller structure.
*/

typedef struct DAC960_Controller
{
  void *BaseAddress;
  void *MemoryMappedAddress;
  DAC960_ControllerType_T ControllerType;
  DAC960_IO_Address_T IO_Address;
  DAC960_PCI_Address_T PCI_Address;
  unsigned char ControllerNumber;
  unsigned char ModelName[12];
  unsigned char FullModelName[18];
  unsigned char FirmwareVersion[14];
  unsigned char Bus;
  unsigned char Device;
  unsigned char Function;
  unsigned char IRQ_Channel;
  unsigned char Channels;
  unsigned char MemorySize;
  unsigned char LogicalDriveCount;
  unsigned char GeometryTranslationHeads;
  unsigned char GeometryTranslationSectors;
  unsigned short ControllerQueueDepth;
  unsigned short DriverQueueDepth;
  unsigned short MaxBlocksPerCommand;
  unsigned short MaxScatterGatherSegments;
  unsigned short StripeSize;
  unsigned short SegmentSize;
  unsigned short NewEventLogSequenceNumber;
  unsigned short OldEventLogSequenceNumber;
  unsigned short MessageBufferLength;
  unsigned int ControllerUsageCount;
  unsigned int EnquiryIndex;
  unsigned int LogicalDriveInformationIndex;
  unsigned int DeviceStateIndex;
  unsigned int DeviceStateChannel;
  unsigned int DeviceStateTargetID;
  unsigned long SecondaryMonitoringTime;
  unsigned long RebuildLastReportTime;
  boolean SAFTE_FaultManagementEnabled;
  boolean MonitoringCommandDeferred;
  boolean NeedLogicalDriveInformation;
  boolean NeedDeviceStateInformation;
  boolean NeedRebuildProgress;
  GenericDiskInfo_T GenericDiskInfo;
  Timer_T MonitoringTimer;
  DAC960_Command_T *FreeCommands;
  DAC960_V4_CommandMailbox_T *FirstCommandMailbox;
  DAC960_V4_CommandMailbox_T *LastCommandMailbox;
  DAC960_V4_CommandMailbox_T *NextCommandMailbox;
  DAC960_V4_CommandMailbox_T *PreviousCommandMailbox;
  DAC960_V4_StatusMailbox_T *FirstStatusMailbox;
  DAC960_V4_StatusMailbox_T *LastStatusMailbox;
  DAC960_V4_StatusMailbox_T *NextStatusMailbox;
  DAC960_Enquiry_T Enquiry[2];
  DAC960_LogicalDriveInformation_T
    LogicalDriveInformation[2][DAC960_MaxLogicalDrives];
  DAC960_DeviceState_T DeviceState[2][DAC960_MaxChannels][DAC960_MaxTargets];
  DAC960_EventLogEntry_T EventLogEntry;
  DAC960_RebuildProgress_T RebuildProgress;
  DAC960_Command_T Commands[DAC960_MaxDriverQueueDepth];
  DiskPartition_T DiskPartitions[DAC960_MinorCount];
  int LogicalDriveUsageCount[DAC960_MaxLogicalDrives];
  int PartitionSizes[DAC960_MinorCount];
  int BlockSizes[DAC960_MinorCount];
  int MaxSectorsPerRequest[DAC960_MinorCount];
  int MaxSegmentsPerRequest[DAC960_MinorCount];
  char MessageBuffer[DAC960_MessageBufferSize];
}
DAC960_Controller_T;


/*
  DAC960_AcquireControllerLock acquires exclusive access to Controller.
*/

static inline
void DAC960_AcquireControllerLock(DAC960_Controller_T *Controller,
				  ProcessorFlags_T *ProcessorFlags)
{
  save_flags(*ProcessorFlags);
  cli();
}


/*
  DAC960_ReleaseControllerLock releases exclusive access to Controller.
*/

static inline
void DAC960_ReleaseControllerLock(DAC960_Controller_T *Controller,
				  ProcessorFlags_T *ProcessorFlags)
{
  restore_flags(*ProcessorFlags);
}


/*
  DAC960_AcquireControllerLockRF acquires exclusive access to Controller,
  but is only called from the request function when interrupts are disabled.
*/

static inline
void DAC960_AcquireControllerLockRF(DAC960_Controller_T *Controller,
				    ProcessorFlags_T *ProcessorFlags)
{
}


/*
  DAC960_ReleaseControllerLockRF releases exclusive access to Controller,
  but is only called from the request function when interrupts are disabled.
*/

static inline
void DAC960_ReleaseControllerLockRF(DAC960_Controller_T *Controller,
				    ProcessorFlags_T *ProcessorFlags)
{
}


/*
  DAC960_AcquireControllerLockIH acquires exclusive access to Controller,
  but is only called from the interrupt handler when interrupts are disabled.
*/

static inline
void DAC960_AcquireControllerLockIH(DAC960_Controller_T *Controller,
				    ProcessorFlags_T *ProcessorFlags)
{
}


/*
  DAC960_ReleaseControllerLockIH releases exclusive access to Controller,
  but is only called from the interrupt handler when interrupts are disabled.
*/

static inline
void DAC960_ReleaseControllerLockIH(DAC960_Controller_T *Controller,
				    ProcessorFlags_T *ProcessorFlags)
{
}


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 V4 Controller Interface Registers.
*/

static inline
void DAC960_V4_NewCommand(void *ControllerBaseAddress)
{
  DAC960_V4_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.NewCommand = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V4_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_V4_AcknowledgeStatus(void *ControllerBaseAddress)
{
  DAC960_V4_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeStatus = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V4_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_V4_SoftReset(void *ControllerBaseAddress)
{
  DAC960_V4_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.SoftReset = true;
  writel(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V4_InboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_V4_MailboxFullP(void *ControllerBaseAddress)
{
  DAC960_V4_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readl(ControllerBaseAddress + DAC960_V4_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.MailboxFull;
}

static inline
void DAC960_V4_AcknowledgeInterrupt(void *ControllerBaseAddress)
{
  DAC960_V4_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeInterrupt = true;
  writel(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V4_OutboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_V4_StatusAvailableP(void *ControllerBaseAddress)
{
  DAC960_V4_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readl(ControllerBaseAddress + DAC960_V4_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.StatusAvailable;
}

static inline
void DAC960_V4_EnableInterrupts(void *ControllerBaseAddress)
{
  DAC960_V4_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask1 = 0x3;
  InterruptMaskRegister.Bits.DisableInterrupts = false;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask2 = 0x1F;
  writel(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_V4_InterruptMaskRegisterOffset);
}

static inline
void DAC960_V4_DisableInterrupts(void *ControllerBaseAddress)
{
  DAC960_V4_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All = 0;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask1 = 0x3;
  InterruptMaskRegister.Bits.DisableInterrupts = true;
  InterruptMaskRegister.Bits.MessageUnitInterruptMask2 = 0x1F;
  writel(InterruptMaskRegister.All,
	 ControllerBaseAddress + DAC960_V4_InterruptMaskRegisterOffset);
}

static inline
boolean DAC960_V4_InterruptsEnabledP(void *ControllerBaseAddress)
{
  DAC960_V4_InterruptMaskRegister_T InterruptMaskRegister;
  InterruptMaskRegister.All =
    readl(ControllerBaseAddress + DAC960_V4_InterruptMaskRegisterOffset);
  return !InterruptMaskRegister.Bits.DisableInterrupts;
}

static inline
void DAC960_V4_WriteCommandMailbox(DAC960_CommandMailbox_T *NextCommandMailbox,
				   DAC960_CommandMailbox_T *CommandMailbox)
{
  NextCommandMailbox->Words[1] = CommandMailbox->Words[1];
  NextCommandMailbox->Words[2] = CommandMailbox->Words[2];
  NextCommandMailbox->Words[3] = CommandMailbox->Words[3];
  NextCommandMailbox->Words[0] = CommandMailbox->Words[0];
}

static inline
void DAC960_V4_WriteLegacyCommand(void *ControllerBaseAddress,
				  DAC960_CommandMailbox_T *CommandMailbox)
{
  writel(CommandMailbox->Words[0],
	 ControllerBaseAddress + DAC960_V4_CommandOpcodeRegisterOffset);
  writel(CommandMailbox->Words[1],
	 ControllerBaseAddress + DAC960_V4_MailboxRegister4Offset);
  writel(CommandMailbox->Words[2],
	 ControllerBaseAddress + DAC960_V4_MailboxRegister8Offset);
  writeb(CommandMailbox->Bytes[12],
	 ControllerBaseAddress + DAC960_V4_MailboxRegister12Offset);
}

static inline DAC960_CommandIdentifier_T
DAC960_V4_ReadStatusCommandIdentifier(void *ControllerBaseAddress)
{
  return readb(ControllerBaseAddress
	       + DAC960_V4_StatusCommandIdentifierRegOffset);
}

static inline DAC960_CommandStatus_T
DAC960_V4_ReadStatusRegister(void *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_V4_StatusRegisterOffset);
}


/*
  Define inline functions to provide an abstraction for reading and writing the
  DAC960 V3 Controller Interface Registers.
*/

static inline
void DAC960_V3_NewCommand(void *ControllerBaseAddress)
{
  DAC960_V3_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.NewCommand = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V3_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_V3_AcknowledgeStatus(void *ControllerBaseAddress)
{
  DAC960_V3_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.AcknowledgeStatus = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V3_InboundDoorBellRegisterOffset);
}

static inline
void DAC960_V3_SoftReset(void *ControllerBaseAddress)
{
  DAC960_V3_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All = 0;
  InboundDoorBellRegister.Write.SoftReset = true;
  writeb(InboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V3_InboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_V3_MailboxFullP(void *ControllerBaseAddress)
{
  DAC960_V3_InboundDoorBellRegister_T InboundDoorBellRegister;
  InboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_V3_InboundDoorBellRegisterOffset);
  return InboundDoorBellRegister.Read.MailboxFull;
}

static inline
void DAC960_V3_AcknowledgeInterrupt(void *ControllerBaseAddress)
{
  DAC960_V3_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All = 0;
  OutboundDoorBellRegister.Write.AcknowledgeInterrupt = true;
  writeb(OutboundDoorBellRegister.All,
	 ControllerBaseAddress + DAC960_V3_OutboundDoorBellRegisterOffset);
}

static inline
boolean DAC960_V3_StatusAvailableP(void *ControllerBaseAddress)
{
  DAC960_V3_OutboundDoorBellRegister_T OutboundDoorBellRegister;
  OutboundDoorBellRegister.All =
    readb(ControllerBaseAddress + DAC960_V3_OutboundDoorBellRegisterOffset);
  return OutboundDoorBellRegister.Read.StatusAvailable;
}

static inline
void DAC960_V3_EnableInterrupts(void *ControllerBaseAddress)
{
  DAC960_V3_InterruptEnableRegister_T InterruptEnableRegister;
  InterruptEnableRegister.All = 0;
  InterruptEnableRegister.Bits.EnableInterrupts = true;
  writeb(InterruptEnableRegister.All,
	 ControllerBaseAddress + DAC960_V3_InterruptEnableRegisterOffset);
}

static inline
void DAC960_V3_DisableInterrupts(void *ControllerBaseAddress)
{
  DAC960_V3_InterruptEnableRegister_T InterruptEnableRegister;
  InterruptEnableRegister.All = 0;
  InterruptEnableRegister.Bits.EnableInterrupts = false;
  writeb(InterruptEnableRegister.All,
	 ControllerBaseAddress + DAC960_V3_InterruptEnableRegisterOffset);
}

static inline
boolean DAC960_V3_InterruptsEnabledP(void *ControllerBaseAddress)
{
  DAC960_V3_InterruptEnableRegister_T InterruptEnableRegister;
  InterruptEnableRegister.All =
    readb(ControllerBaseAddress + DAC960_V3_InterruptEnableRegisterOffset);
  return InterruptEnableRegister.Bits.EnableInterrupts;
}

static inline
void DAC960_V3_WriteCommandMailbox(void *ControllerBaseAddress,
				   DAC960_CommandMailbox_T *CommandMailbox)
{
  writel(CommandMailbox->Words[0],
	 ControllerBaseAddress + DAC960_V3_CommandOpcodeRegisterOffset);
  writel(CommandMailbox->Words[1],
	 ControllerBaseAddress + DAC960_V3_MailboxRegister4Offset);
  writel(CommandMailbox->Words[2],
	 ControllerBaseAddress + DAC960_V3_MailboxRegister8Offset);
  writeb(CommandMailbox->Bytes[12],
	 ControllerBaseAddress + DAC960_V3_MailboxRegister12Offset);
}

static inline DAC960_CommandIdentifier_T
DAC960_V3_ReadStatusCommandIdentifier(void *ControllerBaseAddress)
{
  return readb(ControllerBaseAddress
	       + DAC960_V3_StatusCommandIdentifierRegOffset);
}

static inline DAC960_CommandStatus_T
DAC960_V3_ReadStatusRegister(void *ControllerBaseAddress)
{
  return readw(ControllerBaseAddress + DAC960_V3_StatusRegisterOffset);
}


/*
  Virtual_to_Bus and Bus_to_Virtual map between Kernel Virtual Addresses
  and PCI Bus Addresses.
*/

static inline DAC960_BusAddress_T Virtual_to_Bus(void *VirtualAddress)
{
  return (DAC960_BusAddress_T) virt_to_bus(VirtualAddress);
}

static inline void *Bus_to_Virtual(DAC960_BusAddress_T BusAddress)
{
  return (void *) bus_to_virt(BusAddress);
}


/*
  Define compatibility macros between Linux 2.0 and Linux 2.1.
*/

#if LINUX_VERSION_CODE < 0x20100

#define MODULE_PARM(Variable, Type)
#define ioremap_nocache(Offset, Size)	vremap(Offset, Size)
#define iounmap(Address)		vfree(Address)

#endif


/*
  Define prototypes for the forward referenced DAC960 Driver Internal Functions.
*/

static void DAC960_RequestFunction0(void);
static void DAC960_RequestFunction1(void);
static void DAC960_RequestFunction2(void);
static void DAC960_RequestFunction3(void);
static void DAC960_RequestFunction4(void);
static void DAC960_RequestFunction5(void);
static void DAC960_RequestFunction6(void);
static void DAC960_RequestFunction7(void);
static void DAC960_InterruptHandler(int, void *, Registers_T *);
static void DAC960_QueueMonitoringCommand(DAC960_Command_T *);
static void DAC960_MonitoringTimerFunction(unsigned long);
static int DAC960_Open(Inode_T *, File_T *);
static void DAC960_Release(Inode_T *, File_T *);
static int DAC960_Ioctl(Inode_T *, File_T *, unsigned int, unsigned long);
static void DAC960_InitializeGenericDiskInfo(GenericDiskInfo_T *);
static void DAC960_Message(DAC960_MessageLevel_T, char *,
			   DAC960_Controller_T *, ...);


#endif /* DAC960_DriverVersion */
