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


#define DAC960_DriverVersion		"2.0.0 Beta3"
#define DAC960_DriverDate		"29 November 1998"


#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/segment.h>
#include "DAC960.h"


/*
  DAC960_ControllerCount is the number of DAC960 Controllers.
*/

static int
  DAC960_ControllerCount =			0;


/*
  DAC960_Controllers is an array of pointers to the DAC960 Controller
  structures.
*/

static DAC960_Controller_T
  *DAC960_Controllers[DAC960_MaxControllers] =	{ NULL };


/*
  DAC960_FileOperations is the File Operations structure for DAC960 Logical
  Disk Devices.
*/

static FileOperations_T
  DAC960_FileOperations =
    { lseek:		    NULL,
      read:		    block_read,
      write:		    block_write,
      readdir:		    NULL,
      select:		    NULL,
      ioctl:		    DAC960_Ioctl,
      mmap:		    NULL,
      open:		    DAC960_Open,
      release:		    DAC960_Release,
      fsync:		    block_fsync,
      fasync:		    NULL,
      check_media_change:   NULL,
      revalidate:	    NULL };


/*
  DAC960_AnnounceDriver announces the Driver Version and Date, Author's Name,
  Copyright Notice, and Electronic Mail Address.
*/

static void DAC960_AnnounceDriver(DAC960_Controller_T *Controller)
{
  DAC960_Announce("***** DAC960 RAID Driver Version "
		  DAC960_DriverVersion " of "
		  DAC960_DriverDate " *****\n", Controller);
  DAC960_Announce("Copyright 1998 by Leonard N. Zubkoff "
		  "<lnz@dandelion.com>\n", Controller);
}


/*
  DAC960_Failure prints a standardized error message, and then returns false.
*/

static boolean DAC960_Failure(DAC960_Controller_T *Controller,
			      char *ErrorMessage)
{
  DAC960_Error("While configuring DAC960 PCI RAID Controller at\n",
	       Controller);
  if (Controller->IO_Address == 0)
    DAC960_Error("PCI Bus %d Device %d Function %d I/O Address N/A "
		 "PCI Address 0x%X\n", Controller,
		 Controller->Bus, Controller->Device,
		 Controller->Function, Controller->PCI_Address);
  else DAC960_Error("PCI Bus %d Device %d Function %d I/O Address "
		    "0x%X PCI Address 0x%X\n", Controller,
		    Controller->Bus, Controller->Device,
		    Controller->Function, Controller->IO_Address,
		    Controller->PCI_Address);
  DAC960_Error("%s FAILED - DETACHING\n", Controller, ErrorMessage);
  return false;
}


/*
  DAC960_ClearCommand clears critical fields of Command.
*/

static inline void DAC960_ClearCommand(DAC960_Command_T *Command)
{
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  CommandMailbox->Words[0] = 0;
  CommandMailbox->Words[1] = 0;
  CommandMailbox->Words[2] = 0;
  CommandMailbox->Words[3] = 0;
  Command->CommandStatus = 0;
}


/*
  DAC960_AllocateCommand allocates a Command structure from Controller's
  free list.
*/

static inline DAC960_Command_T *DAC960_AllocateCommand(DAC960_Controller_T
						       *Controller)
{
  DAC960_Command_T *Command = Controller->FreeCommands;
  if (Command == NULL) return NULL;
  Controller->FreeCommands = Command->Next;
  Command->Next = NULL;
  return Command;
}


/*
  DAC960_DeallocateCommand deallocates Command, returning it to Controller's
  free list.
*/

static inline void DAC960_DeallocateCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  Command->Next = Controller->FreeCommands;
  Controller->FreeCommands = Command;
}


/*
  DAC960_QueueCommand queues Command.
*/

static void DAC960_QueueCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  void *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V4_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  DAC960_V4_CommandMailbox_T *NextCommandMailbox;
  CommandMailbox->Common.CommandIdentifier = Command - Controller->Commands;
  switch (Controller->ControllerType)
    {
    case DAC960_V4_Controller:
      NextCommandMailbox = Controller->NextCommandMailbox;
      DAC960_V4_WriteCommandMailbox(NextCommandMailbox, CommandMailbox);
      if (Controller->PreviousCommandMailbox->Words[0] == 0)
	DAC960_V4_NewCommand(ControllerBaseAddress);
      Controller->PreviousCommandMailbox = NextCommandMailbox;
      if (++NextCommandMailbox > Controller->LastCommandMailbox)
	NextCommandMailbox = Controller->FirstCommandMailbox;
      Controller->NextCommandMailbox = NextCommandMailbox;
      break;
    case DAC960_V3_Controller:
      while (DAC960_V3_MailboxFullP(ControllerBaseAddress))
	udelay(1);
      DAC960_V3_WriteCommandMailbox(ControllerBaseAddress, CommandMailbox);
      DAC960_V3_NewCommand(ControllerBaseAddress);
      break;
    }
}


/*
  DAC960_ExecuteCommand executes Command and waits for completion.  It
  returns true on success and false on failure.
*/

static boolean DAC960_ExecuteCommand(DAC960_Command_T *Command)
{
  Semaphore_T Semaphore = MUTEX_LOCKED;
  Command->Semaphore = &Semaphore;
  DAC960_QueueCommand(Command);
  down(&Semaphore);
  return Command->CommandStatus == DAC960_NormalCompletion;
}


/*
  DAC960_ExecuteType3 executes a DAC960 Type 3 Command and waits for
  completion.  It returns true on success and false on failure.
*/

static boolean DAC960_ExecuteType3(DAC960_Controller_T *Controller,
				   DAC960_CommandOpcode_T CommandOpcode,
				   void *DataPointer)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  boolean Result;
  DAC960_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Type3.CommandOpcode = CommandOpcode;
  CommandMailbox->Type3.BusAddress = Virtual_to_Bus(DataPointer);
  Result = DAC960_ExecuteCommand(Command);
  DAC960_DeallocateCommand(Command);
  return Result;
}


/*
  DAC960_ExecuteType3D executes a DAC960 Type 3D Command and waits for
  completion.  It returns true on success and false on failure.
*/

static boolean DAC960_ExecuteType3D(DAC960_Controller_T *Controller,
				    DAC960_CommandOpcode_T CommandOpcode,
				    unsigned char Channel,
				    unsigned char TargetID,
				    void *DataPointer)
{
  DAC960_Command_T *Command = DAC960_AllocateCommand(Controller);
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  boolean Result;
  DAC960_ClearCommand(Command);
  Command->CommandType = DAC960_ImmediateCommand;
  CommandMailbox->Type3D.CommandOpcode = CommandOpcode;
  CommandMailbox->Type3D.Channel = Channel;
  CommandMailbox->Type3D.TargetID = TargetID;
  CommandMailbox->Type3D.BusAddress = Virtual_to_Bus(DataPointer);
  Result = DAC960_ExecuteCommand(Command);
  DAC960_DeallocateCommand(Command);
  return Result;
}


/*
  DAC960_V4_EnableMemoryMailboxInterface enables the V4 Memory Mailbox
  Interface.
*/

static boolean DAC960_V4_EnableMemoryMailboxInterface(DAC960_Controller_T
						      *Controller)
{
  void *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V4_CommandMailbox_T *CommandMailboxesMemory;
  DAC960_V4_StatusMailbox_T *StatusMailboxesMemory;
  DAC960_CommandMailbox_T CommandMailbox;
  DAC960_CommandStatus_T CommandStatus;
  CommandMailboxesMemory =
    (DAC960_V4_CommandMailbox_T *) __get_free_pages(GFP_KERNEL, 1, 0);
  memset(CommandMailboxesMemory, 0, PAGE_SIZE << 1);
  Controller->FirstCommandMailbox = CommandMailboxesMemory;
  CommandMailboxesMemory += DAC960_CommandMailboxCount - 1;
  Controller->LastCommandMailbox = CommandMailboxesMemory;
  Controller->NextCommandMailbox = Controller->FirstCommandMailbox;
  Controller->PreviousCommandMailbox = Controller->LastCommandMailbox;
  StatusMailboxesMemory =
    (DAC960_V4_StatusMailbox_T *) (CommandMailboxesMemory + 1);
  Controller->FirstStatusMailbox = StatusMailboxesMemory;
  StatusMailboxesMemory += DAC960_StatusMailboxCount - 1;
  Controller->LastStatusMailbox = StatusMailboxesMemory;
  Controller->NextStatusMailbox = Controller->FirstStatusMailbox;
  /* Enable the Memory Mailbox Interface. */
  CommandMailbox.TypeX.CommandOpcode = 0x2B;
  CommandMailbox.TypeX.CommandIdentifier = 0;
  CommandMailbox.TypeX.CommandOpcode2 = 0x10;
  CommandMailbox.TypeX.CommandMailboxesBusAddress =
    Virtual_to_Bus(Controller->FirstCommandMailbox);
  CommandMailbox.TypeX.StatusMailboxesBusAddress =
    Virtual_to_Bus(Controller->FirstStatusMailbox);
  while (DAC960_V4_MailboxFullP(ControllerBaseAddress))
    udelay(1);
  DAC960_V4_WriteLegacyCommand(ControllerBaseAddress, &CommandMailbox);
  DAC960_V4_NewCommand(ControllerBaseAddress);
  while (!DAC960_V4_StatusAvailableP(ControllerBaseAddress))
    udelay(1);
  CommandStatus = DAC960_V4_ReadStatusRegister(ControllerBaseAddress);
  DAC960_V4_AcknowledgeInterrupt(ControllerBaseAddress);
  DAC960_V4_AcknowledgeStatus(ControllerBaseAddress);
  return CommandStatus == DAC960_NormalCompletion;
}


/*
  DAC960_DetectControllers detects DAC960 PCI RAID Controllers by interrogating
  the PCI Configuration Space for DeviceID.
*/

static void DAC960_DetectControllers(unsigned short DeviceID)
{
  unsigned char Bus, DeviceFunction, IRQ_Channel;
  unsigned int BaseAddress0, BaseAddress1;
  unsigned int MemoryWindowSize = 0;
  unsigned short Index = 0;
  while (pcibios_find_device(PCI_VENDOR_ID_MYLEX, DeviceID,
			     Index++, &Bus, &DeviceFunction) == 0)
    {
      DAC960_Controller_T *Controller = (DAC960_Controller_T *)
	kmalloc(sizeof(DAC960_Controller_T), GFP_ATOMIC);
      DAC960_ControllerType_T ControllerType = 0;
      DAC960_IO_Address_T IO_Address = 0;
      DAC960_PCI_Address_T PCI_Address = 0;
      unsigned char Device = DeviceFunction >> 3;
      unsigned char Function = DeviceFunction & 0x7;
      pcibios_read_config_dword(Bus, DeviceFunction,
				PCI_BASE_ADDRESS_0, &BaseAddress0);
      pcibios_read_config_dword(Bus, DeviceFunction,
				PCI_BASE_ADDRESS_1, &BaseAddress1);
      pcibios_read_config_byte(Bus, DeviceFunction,
			       PCI_INTERRUPT_LINE, &IRQ_Channel);
      switch (DeviceID)
	{
	case PCI_DEVICE_ID_MYLEX_DAC960P_V4:
	  ControllerType = DAC960_V4_Controller;
	  PCI_Address = BaseAddress0 & PCI_BASE_ADDRESS_MEM_MASK;
	  MemoryWindowSize = DAC960_V4_RegisterWindowSize;
	  break;
	case PCI_DEVICE_ID_MYLEX_DAC960P_V3:
	  ControllerType = DAC960_V3_Controller;
	  IO_Address = BaseAddress0 & PCI_BASE_ADDRESS_IO_MASK;
	  PCI_Address = BaseAddress1 & PCI_BASE_ADDRESS_MEM_MASK;
	  MemoryWindowSize = DAC960_V3_RegisterWindowSize;
	  break;
	}
      if (DAC960_ControllerCount == DAC960_MaxControllers)
	{
	  DAC960_Error("More than %d DAC960 Controllers detected - "
		       "ignoring from Controller at\n",
		       NULL, DAC960_MaxControllers);
	  goto Failure;
	}
      if (Controller == NULL)
	{
	  DAC960_Error("Unable to allocate Controller structure for "
		       "Controller at\n", NULL);
	  goto Failure;
	}
      memset(Controller, 0, sizeof(DAC960_Controller_T));
      Controller->ControllerNumber = DAC960_ControllerCount;
      DAC960_Controllers[DAC960_ControllerCount++] = Controller;
      if (IRQ_Channel == 0 || IRQ_Channel >= NR_IRQS)
	{
	  DAC960_Error("IRQ Channel %d illegal for Controller at\n",
		       NULL, IRQ_Channel);
	  goto Failure;
	}
      Controller->ControllerType = ControllerType;
      Controller->IO_Address = IO_Address;
      Controller->PCI_Address = PCI_Address;
      Controller->Bus = Bus;
      Controller->Device = Device;
      Controller->Function = Function;
      /*
	Acquire shared access to the IRQ Channel.
      */
      strcpy(Controller->FullModelName, "DAC960");
      if (request_irq(IRQ_Channel, DAC960_InterruptHandler,
		      SA_INTERRUPT | SA_SHIRQ, Controller->FullModelName,
		      Controller) < 0)
	{
	  DAC960_Error("Unable to acquire IRQ Channel %d for Controller at\n",
		       NULL, IRQ_Channel);
	  goto Failure;
	}
      Controller->IRQ_Channel = IRQ_Channel;
      /*
	Map the Controller Register Window.
      */
      if (MemoryWindowSize < PAGE_SIZE)
	MemoryWindowSize = PAGE_SIZE;
      Controller->MemoryMappedAddress =
	ioremap_nocache(PCI_Address & PAGE_MASK, MemoryWindowSize);
      Controller->BaseAddress =
	Controller->MemoryMappedAddress + (PCI_Address & ~PAGE_MASK);
      if (Controller->MemoryMappedAddress == NULL)
	{
	  DAC960_Error("Unable to map Controller Register Window for "
		       "Controller at\n", NULL);
	  goto Failure;
	}
      switch (DeviceID)
	{
	case PCI_DEVICE_ID_MYLEX_DAC960P_V4:
	  DAC960_V4_DisableInterrupts(Controller->BaseAddress);
	  if (!DAC960_V4_EnableMemoryMailboxInterface(Controller))
	    {
	      DAC960_Error("Unable to Enable V4 Memory Mailbox Interface "
			   "for Controller at\n", NULL);
	      goto Failure;
	    }
	  DAC960_V4_EnableInterrupts(Controller->BaseAddress);
	  break;
	case PCI_DEVICE_ID_MYLEX_DAC960P_V3:
	  request_region(Controller->IO_Address, 0x80,
			 Controller->FullModelName);
	  DAC960_V3_EnableInterrupts(Controller->BaseAddress);
	  break;
	}
      Controller->Commands[0].Controller = Controller;
      Controller->Commands[0].Next = NULL;
      Controller->FreeCommands = &Controller->Commands[0];
      continue;
    Failure:
      if (IO_Address == 0)
	DAC960_Error("PCI Bus %d Device %d Function %d I/O Address N/A "
		     "PCI Address 0x%X\n", NULL,
		     Bus, Device, Function, PCI_Address);
      else DAC960_Error("PCI Bus %d Device %d Function %d I/O Address "
			"0x%X PCI Address 0x%X\n", NULL,
			Bus, Device, Function, IO_Address, PCI_Address);
      if (Controller == NULL) break;
      if (Controller->IRQ_Channel > 0)
	free_irq(IRQ_Channel, Controller);
      if (Controller->MemoryMappedAddress != NULL)
	iounmap(Controller->MemoryMappedAddress);
      kfree(Controller);
      break;
    }
}


/*
  DAC960_ReadControllerConfiguration reads the Configuration Information
  from Controller and initializes the Controller structure.
*/

static boolean DAC960_ReadControllerConfiguration(DAC960_Controller_T
						  *Controller)
{
  DAC960_Enquiry2_T Enquiry2;
  DAC960_Config2_T Config2;
  int Channel, TargetID;
  if (!DAC960_ExecuteType3(Controller, DAC960_Enquiry,
			   &Controller->Enquiry[0]))
    return DAC960_Failure(Controller, "ENQUIRY");
  if (!DAC960_ExecuteType3(Controller, DAC960_Enquiry2, &Enquiry2))
    return DAC960_Failure(Controller, "ENQUIRY2");
  if (!DAC960_ExecuteType3(Controller, DAC960_ReadConfig2, &Config2))
    return DAC960_Failure(Controller, "READ CONFIG2");
  if (!DAC960_ExecuteType3(Controller, DAC960_GetLogicalDriveInformation,
			   &Controller->LogicalDriveInformation[0]))
    return DAC960_Failure(Controller, "GET LOGICAL DRIVE INFORMATION");
  for (Channel = 0; Channel < Enquiry2.ActualChannels; Channel++)
    for (TargetID = 0; TargetID < DAC960_MaxTargets; TargetID++)
      if (!DAC960_ExecuteType3D(Controller, DAC960_GetDeviceState,
				Channel, TargetID,
				&Controller->DeviceState[0][Channel][TargetID]))
	  return DAC960_Failure(Controller, "GET DEVICE STATE");
  /*
    Initialize the Controller Model Name and Full Model Name fields.
  */
  switch (Enquiry2.HardwareID.SubModel)
    {
    case DAC960_P_PD_PU:
      if (Enquiry2.SCSICapability.BusSpeed == DAC960_Ultra)
	strcpy(Controller->ModelName, "DAC960PU");
      else strcpy(Controller->ModelName, "DAC960PD");
      break;
    case DAC960_PL:
      strcpy(Controller->ModelName, "DAC960PL");
      break;
    case DAC960_PG:
      strcpy(Controller->ModelName, "DAC960PG");
      break;
    case DAC960_PJ:
      strcpy(Controller->ModelName, "DAC960PJ");
      break;
    case DAC960_PTL_0:
      strcpy(Controller->ModelName, "DAC960PTL-0");
      break;
    case DAC960_PTL_1:
      strcpy(Controller->ModelName, "DAC960PTL-1");
      break;
    default:
      return DAC960_Failure(Controller, "MODEL VERIFICATION");
    }
  strcpy(Controller->FullModelName, "Mylex ");
  strcat(Controller->FullModelName, Controller->ModelName);
  /*
    Initialize the Controller Firmware Version field and verify that it
    is a supported firmware version.  The supported firmware versions are:

    DAC960PTL/PJ/PG	4.06 and above
    DAC960PU/PD/PL	3.51 and above
  */
  sprintf(Controller->FirmwareVersion, "%d.%02d-%c-%02d",
	  Enquiry2.FirmwareID.MajorVersion, Enquiry2.FirmwareID.MinorVersion,
	  Enquiry2.FirmwareID.FirmwareType, Enquiry2.FirmwareID.TurnID);
  if (!((Controller->FirmwareVersion[0] == '4' &&
	 strcmp(Controller->FirmwareVersion, "4.06") >= 0) ||
	(Controller->FirmwareVersion[0] == '3' &&
	 strcmp(Controller->FirmwareVersion, "3.51") >= 0)))
    {
      DAC960_Failure(Controller, "FIRMWARE VERSION VERIFICATION");
      DAC960_Error("Firmware Version = '%s'\n", Controller,
		   Controller->FirmwareVersion);
      return false;
    }
  /*
    Initialize the Controller Channels, Memory Size, and SAF-TE Fault
    Management Enabled fields.
  */
  Controller->Channels = Enquiry2.ActualChannels;
  Controller->MemorySize = Enquiry2.MemorySize >> 20;
  Controller->SAFTE_FaultManagementEnabled =
    Enquiry2.FaultManagementType == DAC960_SAFTE;
  /*
    Initialize the Controller Queue Depth, Driver Queue Depth, Logical Drive
    Count, Maximum Blocks per Command, and Maximum Scatter/Gather Segments.
    The Driver Queue Depth must be at most one less than the Controller Queue
    Depth to allow for an automatic drive rebuild operation.
  */
  Controller->ControllerQueueDepth = Controller->Enquiry[0].MaxCommands;
  Controller->DriverQueueDepth = Controller->ControllerQueueDepth - 1;
  Controller->LogicalDriveCount = Controller->Enquiry[0].NumberOfLogicalDrives;
  Controller->MaxBlocksPerCommand = Enquiry2.MaxBlocksPerCommand;
  Controller->MaxScatterGatherSegments = Enquiry2.MaxScatterGatherEntries;
  /*
    Initialize the Stripe Size, Segment Size, and Geometry Translation.
  */
  Controller->StripeSize = Config2.BlocksPerStripe * Config2.BlockFactor
			   >> (10 - DAC960_BlockSizeBits);
  Controller->SegmentSize = Config2.BlocksPerCacheLine * Config2.BlockFactor
			    >> (10 - DAC960_BlockSizeBits);
  switch (Config2.DriveGeometry)
    {
    case DAC960_Geometry_128_32:
      Controller->GeometryTranslationHeads = 128;
      Controller->GeometryTranslationSectors = 32;
      break;
    case DAC960_Geometry_255_63:
      Controller->GeometryTranslationHeads = 255;
      Controller->GeometryTranslationSectors = 63;
      break;
    default:
      return DAC960_Failure(Controller, "CONFIG2 DRIVE GEOMETRY");
    }
  return true;
}


/*
  DAC960_ReportControllerConfiguration reports the configuration of
  Controller.
*/

static boolean DAC960_ReportControllerConfiguration(DAC960_Controller_T
						    *Controller)
{
  int LogicalDriveNumber, Channel, TargetID;
  DAC960_Info("Configuring Mylex %s PCI RAID Controller\n",
	      Controller, Controller->ModelName);
  DAC960_Info("  Firmware Version: %s, Channels: %d, Memory Size: %dMB\n",
	      Controller, Controller->FirmwareVersion,
	      Controller->Channels, Controller->MemorySize);
  DAC960_Info("  PCI Bus: %d, Device: %d, Function: %d, I/O Address: ",
	      Controller, Controller->Bus,
	      Controller->Device, Controller->Function);
  if (Controller->IO_Address == 0)
    DAC960_Info("Unassigned\n", Controller);
  else DAC960_Info("0x%X\n", Controller, Controller->IO_Address);
  DAC960_Info("  PCI Address: 0x%X mapped at 0x%lX, IRQ Channel: %d\n",
	      Controller, Controller->PCI_Address,
	      (unsigned long) Controller->BaseAddress,
	      Controller->IRQ_Channel);
  DAC960_Info("  Controller Queue Depth: %d, "
	      "Maximum Blocks per Command: %d\n",
	      Controller, Controller->ControllerQueueDepth,
	      Controller->MaxBlocksPerCommand);
  DAC960_Info("  Driver Queue Depth: %d, "
	      "Maximum Scatter/Gather Segments: %d\n",
	      Controller, Controller->DriverQueueDepth,
	      Controller->MaxScatterGatherSegments);
  DAC960_Info("  Stripe Size: %dKB, Segment Size: %dKB, "
	      "BIOS Geometry: %d/%d\n", Controller,
	      Controller->StripeSize,
	      Controller->SegmentSize,
	      Controller->GeometryTranslationHeads,
	      Controller->GeometryTranslationSectors);
  if (Controller->SAFTE_FaultManagementEnabled)
    DAC960_Info("  SAF-TE Fault Management Enabled\n", Controller);
  DAC960_Info("  Physical Devices:\n", Controller);
  for (Channel = 0; Channel < Controller->Channels; Channel++)
    for (TargetID = 0; TargetID < DAC960_MaxTargets; TargetID++)
      {
	DAC960_DeviceState_T *DeviceState =
	  &Controller->DeviceState[0][Channel][TargetID];
	if (!DeviceState->Present) continue;
	switch (DeviceState->DeviceType)
	  {
	  case DAC960_OtherType:
	    DAC960_Info("    %d:%d - Other\n", Controller, Channel, TargetID);
	    break;
	  case DAC960_DiskType:
	    DAC960_Info("    %d:%d - Disk: %s, %d blocks\n", Controller,
			Channel, TargetID,
			(DeviceState->DeviceState == DAC960_Device_Dead
			 ? "Dead"
			 : DeviceState->DeviceState == DAC960_Device_WriteOnly
			   ? "Write-Only"
			   : DeviceState->DeviceState == DAC960_Device_Online
			     ? "Online" : "Standby"),
			DeviceState->DiskSize);
	    break;
	  case DAC960_SequentialType:
	    DAC960_Info("    %d:%d - Sequential\n", Controller,
			Channel, TargetID);
	    break;
	  case DAC960_CDROM_or_WORM_Type:
	    DAC960_Info("    %d:%d - CD-ROM or WORM\n", Controller,
			Channel, TargetID);
	    break;
	  }

      }
  DAC960_Info("  Logical Drives:\n", Controller);
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < Controller->LogicalDriveCount;
       LogicalDriveNumber++)
    {
      DAC960_LogicalDriveInformation_T *LogicalDriveInformation =
	&Controller->LogicalDriveInformation[0][LogicalDriveNumber];
      DAC960_Info("    /dev/rd/c%dd%d: RAID-%d, %s, %d blocks, %s\n",
		  Controller, Controller->ControllerNumber, LogicalDriveNumber,
		  LogicalDriveInformation->RAIDLevel,
		  (LogicalDriveInformation->LogicalDriveState ==
		     DAC960_LogicalDrive_Online
		   ? "Online"
		   : LogicalDriveInformation->LogicalDriveState ==
		     DAC960_LogicalDrive_Critical
		     ? "Critical" : "Offline"),
		  LogicalDriveInformation->LogicalDriveSize,
		  (LogicalDriveInformation->WriteBack
		   ? "Write Back" : "Write Thru"));
    }
  DAC960_Info("\n", Controller);
  return true;
}


/*
  DAC960_RegisterBlockDevice registers the Block Device structures
  associated with Controller.
*/

static boolean DAC960_RegisterBlockDevice(DAC960_Controller_T *Controller)
{
  static void (*RequestFunctions[DAC960_MaxControllers])(void) =
    { DAC960_RequestFunction0, DAC960_RequestFunction1,
      DAC960_RequestFunction2, DAC960_RequestFunction3,
      DAC960_RequestFunction4, DAC960_RequestFunction5,
      DAC960_RequestFunction6, DAC960_RequestFunction7 };
  int MajorNumber = DAC960_MAJOR + Controller->ControllerNumber;
  GenericDiskInfo_T *GenericDiskInfo;
  int MinorNumber;
  /*
    Register the Block Device Major Number for this DAC960 Controller.
  */
  if (register_blkdev(MajorNumber, "rd", &DAC960_FileOperations) < 0)
    {
      DAC960_Error("UNABLE TO ACQUIRE MAJOR NUMBER %d - DETACHING\n",
		   Controller, MajorNumber);
      return false;
    }
  /*
    Initialize the I/O Request Function.
  */
  blk_dev[MajorNumber].request_fn =
    RequestFunctions[Controller->ControllerNumber];
  /*
    Initialize the Disk Partitions array, Partition Sizes array, Block Sizes
    array, Max Sectors per Request array, and Max Segments per Request array.
  */
  for (MinorNumber = 0; MinorNumber < DAC960_MinorCount; MinorNumber++)
    {
      Controller->BlockSizes[MinorNumber] = BLOCK_SIZE;
      Controller->MaxSectorsPerRequest[MinorNumber] =
	Controller->MaxBlocksPerCommand;
      Controller->MaxSegmentsPerRequest[MinorNumber] =
	Controller->MaxScatterGatherSegments;
    }
  Controller->GenericDiskInfo.part = Controller->DiskPartitions;
  Controller->GenericDiskInfo.sizes = Controller->PartitionSizes;
  blksize_size[MajorNumber] = Controller->BlockSizes;
  max_sectors[MajorNumber] = Controller->MaxSectorsPerRequest;
  max_segments[MajorNumber] = Controller->MaxSegmentsPerRequest;
  /*
    Initialize Read Ahead to 128 sectors.
  */
  read_ahead[MajorNumber] = 128;
  /*
    Complete initialization of the Generic Disk Information structure.
  */
  Controller->GenericDiskInfo.major = MajorNumber;
  Controller->GenericDiskInfo.major_name = "rd";
  Controller->GenericDiskInfo.minor_shift = DAC960_MaxPartitionsBits;
  Controller->GenericDiskInfo.max_p = DAC960_MaxPartitions;
  Controller->GenericDiskInfo.max_nr = DAC960_MaxLogicalDrives;
  Controller->GenericDiskInfo.init = DAC960_InitializeGenericDiskInfo;
  Controller->GenericDiskInfo.nr_real = Controller->LogicalDriveCount;
  Controller->GenericDiskInfo.real_devices = Controller;
  Controller->GenericDiskInfo.next = NULL;
  /*
    Install the Generic Disk Information structure at the end of the list.
  */
  if ((GenericDiskInfo = gendisk_head) != NULL)
    {
      while (GenericDiskInfo->next != NULL)
	GenericDiskInfo = GenericDiskInfo->next;
      GenericDiskInfo->next = &Controller->GenericDiskInfo;
    }
  else gendisk_head = &Controller->GenericDiskInfo;
  /*
    Indicate the Block Device Registration completed successfully,
  */
  return true;
}


/*
  DAC960_UnregisterBlockDevice unregisters the Block Device structures
  associated with Controller.
*/

static void DAC960_UnregisterBlockDevice(DAC960_Controller_T *Controller)
{
  int MajorNumber = DAC960_MAJOR + Controller->ControllerNumber;
  /*
    Unregister the Block Device Major Number for this DAC960 Controller.
  */
  unregister_blkdev(MajorNumber, "rd");
  /*
    Remove the I/O Request Function.
  */
  blk_dev[MajorNumber].request_fn = NULL;
  /*
    Remove the Disk Partitions array, Partition Sizes array, Block Sizes
    array, Max Sectors per Request array, and Max Segments per Request array.
  */
  Controller->GenericDiskInfo.part = NULL;
  Controller->GenericDiskInfo.sizes = NULL;
  blk_size[MajorNumber] = NULL;
  blksize_size[MajorNumber] = NULL;
  max_sectors[MajorNumber] = NULL;
  max_segments[MajorNumber] = NULL;
  /*
    Remove the Generic Disk Information structure from the list.
  */
  if (gendisk_head != &Controller->GenericDiskInfo)
    {
      GenericDiskInfo_T *GenericDiskInfo = gendisk_head;
      while (GenericDiskInfo != NULL &&
	     GenericDiskInfo->next != &Controller->GenericDiskInfo)
	GenericDiskInfo = GenericDiskInfo->next;
      if (GenericDiskInfo != NULL)
	GenericDiskInfo->next = GenericDiskInfo->next->next;
    }
  else gendisk_head = Controller->GenericDiskInfo.next;
}


/*
  DAC960_InitializeController initializes Controller.
*/

static void DAC960_InitializeController(DAC960_Controller_T *Controller)
{
  DAC960_AnnounceDriver(Controller);
  if (DAC960_ReadControllerConfiguration(Controller) &&
      DAC960_ReportControllerConfiguration(Controller) &&
      DAC960_RegisterBlockDevice(Controller))
    {
      /*
	Initialize the Command structures.
      */
      DAC960_Command_T *Commands = Controller->Commands;
      int CommandIdentifier;
      Controller->FreeCommands = NULL;
      for (CommandIdentifier = 0;
	   CommandIdentifier < Controller->DriverQueueDepth;
	   CommandIdentifier++)
	{
	  Commands[CommandIdentifier].Controller = Controller;
	  Commands[CommandIdentifier].Next = Controller->FreeCommands;
	  Controller->FreeCommands = &Commands[CommandIdentifier];
	}
      /*
	Initialize the Monitoring Timer.
      */
      init_timer(&Controller->MonitoringTimer);
      Controller->MonitoringTimer.expires =
	jiffies + DAC960_MonitoringTimerInterval;
      Controller->MonitoringTimer.data = (unsigned long) Controller;
      Controller->MonitoringTimer.function = DAC960_MonitoringTimerFunction;
      add_timer(&Controller->MonitoringTimer);
    }
  else
    {
      free_irq(Controller->IRQ_Channel, Controller);
      iounmap(Controller->MemoryMappedAddress);
      DAC960_UnregisterBlockDevice(Controller);
      DAC960_Controllers[Controller->ControllerNumber] = NULL;
      kfree(Controller);
    }
}


/*
  DAC960_Initialize initializes the DAC960 Driver.
*/

void DAC960_Initialize(void)
{
  int ControllerNumber;
  DAC960_DetectControllers(PCI_DEVICE_ID_MYLEX_DAC960P_V4);
  DAC960_DetectControllers(PCI_DEVICE_ID_MYLEX_DAC960P_V3);
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    if (DAC960_Controllers[ControllerNumber] != NULL)
      DAC960_InitializeController(DAC960_Controllers[ControllerNumber]);
}


/*
  DAC960_Finalize flushes all DAC960 caches before the system halts.
*/

void DAC960_Finalize(void)
{
  int ControllerNumber;
  for (ControllerNumber = 0;
       ControllerNumber < DAC960_ControllerCount;
       ControllerNumber++)
    {
      DAC960_Controller_T *Controller = DAC960_Controllers[ControllerNumber];
      if (Controller == NULL) continue;
      DAC960_Notice("Flushing Cache...", Controller);
      DAC960_ExecuteType3(Controller, DAC960_Flush, NULL);
      DAC960_Notice("done\n", Controller);
    }
}


/*
  DAC960_ProcessRequest attempts to remove one I/O Request from Controller's
  I/O Request Queue and queues it to the Controller.  Command is either a
  previously allocated Command to be reused, or NULL if a new Command is to
  be allocated for this I/O Request.  It returns true if an I/O Request was
  queued and false otherwise.
*/

static boolean DAC960_ProcessRequest(DAC960_Controller_T *Controller,
				     DAC960_Command_T *Command)
{
  int MajorNumber = DAC960_MAJOR + Controller->ControllerNumber;
  IO_Request_T *Request = blk_dev[MajorNumber].current_request;
  DAC960_CommandMailbox_T *CommandMailbox;
  char *RequestBuffer;
  if (Request == NULL || Request->rq_status == RQ_INACTIVE) return false;
  if (Command == NULL)
    Command = DAC960_AllocateCommand(Controller);
  if (Command == NULL) return false;
  DAC960_ClearCommand(Command);
  if (Request->cmd == READ)
    Command->CommandType = DAC960_ReadCommand;
  else Command->CommandType = DAC960_WriteCommand;
  Command->Semaphore = Request->sem;
  Command->LogicalDriveNumber = DAC960_LogicalDriveNumber(Request->rq_dev);
  Command->BlockNumber =
    Request->sector
    + Controller->GenericDiskInfo.part[MINOR(Request->rq_dev)].start_sect;
  Command->BlockCount = Request->nr_sectors;
  Command->SegmentCount = Request->nr_segments;
  Command->BufferHeader = Request->bh;
  RequestBuffer = Request->buffer;
  Request->rq_status = RQ_INACTIVE;
  blk_dev[MajorNumber].current_request = Request->next;
  wake_up(&wait_for_request);
  CommandMailbox = &Command->CommandMailbox;
  if (Command->SegmentCount == 1)
    {
      if (Command->CommandType == DAC960_ReadCommand)
	CommandMailbox->Type5.CommandOpcode = DAC960_Read;
      else CommandMailbox->Type5.CommandOpcode = DAC960_Write;
      CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
      CommandMailbox->Type5.LD.LogicalDriveNumber = Command->LogicalDriveNumber;
      CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
      CommandMailbox->Type5.BusAddress = Virtual_to_Bus(RequestBuffer);
    }
  else
    {
      DAC960_ScatterGatherSegment_T
	*ScatterGatherList = Command->ScatterGatherList;
      BufferHeader_T *BufferHeader = Command->BufferHeader;
      char *LastDataEndPointer = NULL;
      int SegmentNumber = 0;
      if (Command->CommandType == DAC960_ReadCommand)
	CommandMailbox->Type5.CommandOpcode = DAC960_ReadWithOldScatterGather;
      else
	CommandMailbox->Type5.CommandOpcode = DAC960_WriteWithOldScatterGather;
      CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
      CommandMailbox->Type5.LD.LogicalDriveNumber = Command->LogicalDriveNumber;
      CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
      CommandMailbox->Type5.BusAddress = Virtual_to_Bus(ScatterGatherList);
      CommandMailbox->Type5.ScatterGatherCount = Command->SegmentCount;
      while (BufferHeader != NULL)
	{
	  if (BufferHeader->b_data == LastDataEndPointer)
	    {
	      ScatterGatherList[SegmentNumber-1].SegmentByteCount +=
		BufferHeader->b_size;
	      LastDataEndPointer += BufferHeader->b_size;
	    }
	  else
	    {
	      ScatterGatherList[SegmentNumber].SegmentDataPointer =
		Virtual_to_Bus(BufferHeader->b_data);
	      ScatterGatherList[SegmentNumber].SegmentByteCount =
		BufferHeader->b_size;
	      LastDataEndPointer = BufferHeader->b_data + BufferHeader->b_size;
	      if (SegmentNumber++ > Controller->MaxScatterGatherSegments)
		panic("DAC960: Scatter/Gather Segment Overflow\n");
	    }
	  BufferHeader = BufferHeader->b_reqnext;
	}
      if (SegmentNumber != Command->SegmentCount)
	panic("DAC960: SegmentNumber != SegmentCount\n");
    }
  DAC960_QueueCommand(Command);
  return true;
}


/*
  DAC960_ProcessRequests attempts to remove as many I/O Requests as possible
  from Controller's I/O Request Queue and queue them to the Controller.
*/

static inline void DAC960_ProcessRequests(DAC960_Controller_T *Controller)
{
  while (Controller->FreeCommands != NULL)
    if (!DAC960_ProcessRequest(Controller, NULL)) break;
}


/*
  DAC960_RequestFunction0 is the I/O Request Function for DAC960 Controller 0.
*/

static void DAC960_RequestFunction0(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[0];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction1 is the I/O Request Function for DAC960 Controller 1.
*/

static void DAC960_RequestFunction1(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[1];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction2 is the I/O Request Function for DAC960 Controller 2.
*/

static void DAC960_RequestFunction2(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[2];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction3 is the I/O Request Function for DAC960 Controller 3.
*/

static void DAC960_RequestFunction3(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[3];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction4 is the I/O Request Function for DAC960 Controller 4.
*/

static void DAC960_RequestFunction4(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[4];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction5 is the I/O Request Function for DAC960 Controller 5.
*/

static void DAC960_RequestFunction5(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[5];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction6 is the I/O Request Function for DAC960 Controller 6.
*/

static void DAC960_RequestFunction6(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[6];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_RequestFunction7 is the I/O Request Function for DAC960 Controller 7.
*/

static void DAC960_RequestFunction7(void)
{
  DAC960_Controller_T *Controller = DAC960_Controllers[7];
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockRF(Controller, &ProcessorFlags);
  /*
    Process I/O Requests for Controller.
   */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockRF(Controller, &ProcessorFlags);
}


/*
  DAC960_ReadWriteError prints an appropriate error message for Command when
  an error occurs on a read or write operation.
*/

static void DAC960_ReadWriteError(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  char *CommandName = "UNKNOWN";
  switch (Command->CommandType)
    {
    case DAC960_ReadCommand:
    case DAC960_ReadRetryCommand:
      CommandName = "READ";
      break;
    case DAC960_WriteCommand:
    case DAC960_WriteRetryCommand:
      CommandName = "WRITE";
      break;
    case DAC960_MonitoringCommand:
    case DAC960_ImmediateCommand:
      break;
    }
  switch (Command->CommandStatus)
    {
    case DAC960_IrrecoverableDataError:
      DAC960_Error("Irrecoverable Data Error on %s:\n",
		   Controller, CommandName);
      break;
    case DAC960_LogicalDriveNonexistentOrOffline:
      break;
    case DAC960_AccessBeyondEndOfLogicalDrive:
      DAC960_Error("Attempt to Access Beyond End of Logical Drive "
		   "on %s:\n", Controller, CommandName);
      break;
    case DAC960_BadDataEncountered:
      DAC960_Error("Bad Data Encountered on %s:\n", Controller, CommandName);
      break;
    default:
      DAC960_Error("Unexpected Error Status %04X on %s:\n",
		   Controller, Command->CommandStatus, CommandName);
      break;
    }
  DAC960_Error("  /dev/rd/c%dd%d:   absolute blocks %d..%d\n",
	       Controller, Controller->ControllerNumber,
	       Command->LogicalDriveNumber, Command->BlockNumber,
	       Command->BlockNumber + Command->BlockCount - 1);
  if (DAC960_PartitionNumber(Command->BufferHeader->b_rdev) > 0)
    DAC960_Error("  /dev/rd/c%dd%dp%d: relative blocks %d..%d\n",
		 Controller, Controller->ControllerNumber,
		 Command->LogicalDriveNumber,
		 DAC960_PartitionNumber(Command->BufferHeader->b_rdev),
		 Command->BufferHeader->b_rsector,
		 Command->BufferHeader->b_rsector + Command->BlockCount - 1);
}


/*
  DAC960_ProcessCompletedBuffer performs completion processing for an
  individual Buffer.
*/

static inline void DAC960_ProcessCompletedBuffer(BufferHeader_T *BufferHeader,
						 boolean SuccessfulIO)
{
  mark_buffer_uptodate(BufferHeader, SuccessfulIO);
  unlock_buffer(BufferHeader);
}


/*
  DAC960_ProcessCompletedCommand performs completion processing for Command.
*/

static void DAC960_ProcessCompletedCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_CommandType_T CommandType = Command->CommandType;
  DAC960_CommandStatus_T CommandStatus = Command->CommandStatus;
  BufferHeader_T *BufferHeader = Command->BufferHeader;
  if (CommandType == DAC960_ReadCommand ||
      CommandType == DAC960_WriteCommand)
    {
      if (CommandStatus == DAC960_NormalCompletion)
	{
	  /*
	    Perform completion processing for all buffers in this I/O Request.
	  */
	  while (BufferHeader != NULL)
	    {
	      BufferHeader_T *NextBufferHeader = BufferHeader->b_reqnext;
	      BufferHeader->b_reqnext = NULL;
	      DAC960_ProcessCompletedBuffer(BufferHeader, true);
	      BufferHeader = NextBufferHeader;
	    }
	  /*
	    Wake up requestor for swap file paging requests.
	  */
	  if (Command->Semaphore != NULL)
	    {
	      up(Command->Semaphore);
	      Command->Semaphore = NULL;
	    }
	}
      else if ((CommandStatus == DAC960_IrrecoverableDataError ||
		CommandStatus == DAC960_BadDataEncountered) &&
	       BufferHeader != NULL &&
	       BufferHeader->b_reqnext != NULL)
	{
	  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
	  if (CommandType == DAC960_ReadCommand)
	    {
	      Command->CommandType = DAC960_ReadRetryCommand;
	      CommandMailbox->Type5.CommandOpcode = DAC960_Read;
	    }
	  else
	    {
	      Command->CommandType = DAC960_WriteRetryCommand;
	      CommandMailbox->Type5.CommandOpcode = DAC960_Write;
	    }
	  Command->BlockCount = BufferHeader->b_size >> DAC960_BlockSizeBits;
	  CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
	  CommandMailbox->Type5.BusAddress =
	    Virtual_to_Bus(BufferHeader->b_data);
	  DAC960_QueueCommand(Command);
	  return;
	}
      else
	{
	  DAC960_ReadWriteError(Command);
	  /*
	    Perform completion processing for all buffers in this I/O Request.
	  */
	  while (BufferHeader != NULL)
	    {
	      BufferHeader_T *NextBufferHeader = BufferHeader->b_reqnext;
	      BufferHeader->b_reqnext = NULL;
	      DAC960_ProcessCompletedBuffer(BufferHeader, false);
	      BufferHeader = NextBufferHeader;
	    }
	  /*
	    Wake up requestor for swap file paging requests.
	  */
	  if (Command->Semaphore != NULL)
	    {
	      up(Command->Semaphore);
	      Command->Semaphore = NULL;
	    }
	}
    }
  else if (CommandType == DAC960_ReadRetryCommand ||
	   CommandType == DAC960_WriteRetryCommand)
    {
      BufferHeader_T *NextBufferHeader = BufferHeader->b_reqnext;
      BufferHeader->b_reqnext = NULL;
      /*
	Perform completion processing for this single buffer.
      */
      if (CommandStatus == DAC960_NormalCompletion)
	DAC960_ProcessCompletedBuffer(BufferHeader, true);
      else
	{
	  DAC960_ReadWriteError(Command);
	  DAC960_ProcessCompletedBuffer(BufferHeader, false);
	}
      if (NextBufferHeader != NULL)
	{
	  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
	  Command->BlockNumber +=
	    BufferHeader->b_size >> DAC960_BlockSizeBits;
	  Command->BlockCount =
	    NextBufferHeader->b_size >> DAC960_BlockSizeBits;
	  Command->BufferHeader = NextBufferHeader;
	  CommandMailbox->Type5.LD.TransferLength = Command->BlockCount;
	  CommandMailbox->Type5.LogicalBlockAddress = Command->BlockNumber;
	  CommandMailbox->Type5.BusAddress =
	    Virtual_to_Bus(NextBufferHeader->b_data);
	  DAC960_QueueCommand(Command);
	  return;
	}
    }
  else if (CommandType == DAC960_MonitoringCommand)
    {
      DAC960_CommandOpcode_T CommandOpcode =
	Command->CommandMailbox.Common.CommandOpcode;
      unsigned int OldCriticalLogicalDriveCount = 0;
      unsigned int NewCriticalLogicalDriveCount = 0;
      if (CommandOpcode == DAC960_Enquiry)
	{
	  DAC960_Enquiry_T *OldEnquiry =
	    &Controller->Enquiry[Controller->EnquiryIndex];
	  DAC960_Enquiry_T *NewEnquiry =
	    &Controller->Enquiry[Controller->EnquiryIndex ^= 1];
	  OldCriticalLogicalDriveCount = OldEnquiry->CriticalLogicalDriveCount;
	  NewCriticalLogicalDriveCount = NewEnquiry->CriticalLogicalDriveCount;
	  if (NewEnquiry->StatusFlags.DeferredWriteError !=
	      OldEnquiry->StatusFlags.DeferredWriteError)
	    DAC960_Critical("Deferred Write Error Flag is now %s\n", Controller,
			    (NewEnquiry->StatusFlags.DeferredWriteError
			     ? "TRUE" : "FALSE"));
	  if ((NewCriticalLogicalDriveCount > 0 ||
	       NewCriticalLogicalDriveCount != OldCriticalLogicalDriveCount) ||
	      (NewEnquiry->OfflineLogicalDriveCount > 0 ||
	       NewEnquiry->OfflineLogicalDriveCount !=
	       OldEnquiry->OfflineLogicalDriveCount) ||
	      (NewEnquiry->DeadDriveCount > 0 ||
	       NewEnquiry->DeadDriveCount !=
	       OldEnquiry->DeadDriveCount) ||
	      (NewEnquiry->EventLogSequenceNumber !=
	       OldEnquiry->EventLogSequenceNumber) ||
	      (jiffies - Controller->SecondaryMonitoringTime
		 >= DAC960_SecondaryMonitoringInterval))
	    {
	      Controller->NeedLogicalDriveInformation = true;
	      Controller->NewEventLogSequenceNumber =
		NewEnquiry->EventLogSequenceNumber;
	      Controller->NeedDeviceStateInformation = true;
	      Controller->DeviceStateChannel = 0;
	      Controller->DeviceStateTargetID = 0;
	      Controller->SecondaryMonitoringTime = jiffies;
	    }
	  if ((NewEnquiry->RebuildCount > 0 &&
	      jiffies - Controller->RebuildLastReportTime
		>= DAC960_RebuildStatusReportingInterval) ||
	      NewEnquiry->RebuildCount != OldEnquiry->RebuildCount)
	    Controller->NeedRebuildProgress = true;
	}
      else if (CommandOpcode == DAC960_GetLogicalDriveInformation)
	{
	  int LogicalDriveNumber;
	  for (LogicalDriveNumber = 0;
	       LogicalDriveNumber < Controller->LogicalDriveCount;
	       LogicalDriveNumber++)
	    {
	      DAC960_LogicalDriveInformation_T *OldLogicalDriveInformation =
		&Controller->LogicalDriveInformation
			     [Controller->LogicalDriveInformationIndex]
			     [LogicalDriveNumber];
	      DAC960_LogicalDriveInformation_T *NewLogicalDriveInformation =
		&Controller->LogicalDriveInformation
			     [Controller->LogicalDriveInformationIndex ^ 1]
			     [LogicalDriveNumber];
	      if (NewLogicalDriveInformation->LogicalDriveState !=
		  OldLogicalDriveInformation->LogicalDriveState)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"is now %s\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber,
				(NewLogicalDriveInformation->LogicalDriveState
				 == DAC960_LogicalDrive_Online
				 ? "ONLINE"
				 : NewLogicalDriveInformation->LogicalDriveState
				 == DAC960_LogicalDrive_Critical
				 ? "CRITICAL" : "OFFLINE"));
	      if (NewLogicalDriveInformation->WriteBack !=
		  OldLogicalDriveInformation->WriteBack)
		DAC960_Critical("Logical Drive %d (/dev/rd/c%dd%d) "
				"is now %s\n", Controller,
				LogicalDriveNumber,
				Controller->ControllerNumber,
				LogicalDriveNumber,
				(NewLogicalDriveInformation->WriteBack
				 ? "WRITE BACK" : "WRITE THRU"));
	    }
	  Controller->LogicalDriveInformationIndex ^= 1;
	}
      else if (CommandOpcode == DAC960_PerformEventLogOperation)
	{
	  DAC960_EventLogEntry_T *EventLogEntry = &Controller->EventLogEntry;
	  if (EventLogEntry->SequenceNumber ==
	      Controller->OldEventLogSequenceNumber)
	    {
	      unsigned char SenseKey = EventLogEntry->SenseKey;
	      unsigned char AdditionalSenseCode =
		EventLogEntry->AdditionalSenseCode;
	      unsigned char AdditionalSenseCodeQualifier =
		EventLogEntry->AdditionalSenseCodeQualifier;
	      if (SenseKey == 9 &&
		  AdditionalSenseCode == 0x80 &&
		  AdditionalSenseCodeQualifier < DAC960_EventMessagesCount)
		DAC960_Critical("Physical Drive %d:%d %s\n", Controller,
				EventLogEntry->Channel,
				EventLogEntry->TargetID,
				DAC960_EventMessages[
				  AdditionalSenseCodeQualifier]);
	      else if (!((SenseKey == 2 &&
			  AdditionalSenseCode == 0x04 &&
			  (AdditionalSenseCodeQualifier == 0x01 ||
			   AdditionalSenseCodeQualifier == 0x02)) ||
			 (SenseKey == 6 && AdditionalSenseCode == 0x29)))
		DAC960_Critical("Physical Drive %d:%d Error Log: "
				"Sense Key = %d, ASC = %02X, ASCQ = %02X\n",
				Controller,
				EventLogEntry->Channel,
				EventLogEntry->TargetID,
				SenseKey,
				AdditionalSenseCode,
				AdditionalSenseCodeQualifier);
	    }
	  Controller->OldEventLogSequenceNumber++;
	}
      else if (CommandOpcode == DAC960_GetDeviceState)
	{
	  DAC960_DeviceState_T *OldDeviceState =
	    &Controller->DeviceState[Controller->DeviceStateIndex]
				    [Controller->DeviceStateChannel]
				    [Controller->DeviceStateTargetID];
	  DAC960_DeviceState_T *NewDeviceState =
	    &Controller->DeviceState[Controller->DeviceStateIndex ^ 1]
				    [Controller->DeviceStateChannel]
				    [Controller->DeviceStateTargetID];
	  if (NewDeviceState->DeviceState != OldDeviceState->DeviceState)
	    DAC960_Critical("Physical Drive %d:%d is now %s\n", Controller,
			    Controller->DeviceStateChannel,
			    Controller->DeviceStateTargetID,
			    (NewDeviceState->DeviceState == DAC960_Device_Dead
			     ? "DEAD"
			     : NewDeviceState->DeviceState
			       == DAC960_Device_WriteOnly
			       ? "WRITE-ONLY"
			       : NewDeviceState->DeviceState
				 == DAC960_Device_Online
				 ? "ONLINE" : "STANDBY"));
	  if (++Controller->DeviceStateTargetID == DAC960_MaxTargets)
	    {
	      Controller->DeviceStateChannel++;
	      Controller->DeviceStateTargetID = 0;
	    }
	}
      else if (CommandOpcode == DAC960_GetRebuildProgress)
	{
	  unsigned int LogicalDriveNumber =
	    Controller->RebuildProgress.LogicalDriveNumber;
	  unsigned int LogicalDriveSize =
	    Controller->RebuildProgress.LogicalDriveSize;
	  unsigned int BlocksCompleted =
	    LogicalDriveSize - Controller->RebuildProgress.RemainingBlocks;
	  switch (CommandStatus)
	    {
	    case DAC960_NormalCompletion:
	      DAC960_Critical("REBUILD IN PROGRESS: "
			      "Logical Drive %d (/dev/rd/c%dd%d) "
			      "%d%% completed\n",
			      Controller, LogicalDriveNumber,
			      Controller->ControllerNumber,
			      LogicalDriveNumber,
			      (100 * (BlocksCompleted >> 7))
			      / (LogicalDriveSize >> 7));
	      break;
	    case DAC960_RebuildFailed_LogicalDriveFailure:
	      DAC960_Critical("REBUILD FAILED due to "
			      "LOGICAL DRIVE FAILURE\n", Controller);
	      break;
	    case DAC960_RebuildFailed_BadBlocksOnOther:
	      DAC960_Critical("REBUILD FAILED due to "
			      "BAD BLOCKS ON OTHER DRIVES\n", Controller);
	      break;
	    case DAC960_RebuildFailed_NewDriveFailed:
	      DAC960_Critical("REBUILD FAILED due to "
			      "FAILURE OF DRIVE BEING REBUILT\n", Controller);
	      break;
	    case DAC960_RebuildSuccessful:
	      DAC960_Critical("REBUILD COMPLETED SUCCESSFULLY\n", Controller);
	      break;
	    case DAC960_NoRebuildOrCheckInProgress:
	      break;
	    }
	  Controller->RebuildLastReportTime = jiffies;
	}
      if (Controller->NeedLogicalDriveInformation &&
	  NewCriticalLogicalDriveCount >= OldCriticalLogicalDriveCount)
	{
	  Controller->NeedLogicalDriveInformation = false;
	  Command->CommandMailbox.Type3.CommandOpcode =
	    DAC960_GetLogicalDriveInformation;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(
	      &Controller->LogicalDriveInformation
			   [Controller->LogicalDriveInformationIndex ^ 1]);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NewEventLogSequenceNumber
	  - Controller->OldEventLogSequenceNumber > 0)
	{
	  Command->CommandMailbox.Type3E.CommandOpcode =
	    DAC960_PerformEventLogOperation;
	  Command->CommandMailbox.Type3E.OperationType =
	    DAC960_GetEventLogEntry;
	  Command->CommandMailbox.Type3E.OperationQualifier = 1;
	  Command->CommandMailbox.Type3E.SequenceNumber =
	    Controller->OldEventLogSequenceNumber;
	  Command->CommandMailbox.Type3E.BusAddress =
	    Virtual_to_Bus(&Controller->EventLogEntry);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NeedDeviceStateInformation)
	{
	  while (Controller->DeviceStateChannel < Controller->Channels)
	    {
	      DAC960_DeviceState_T *OldDeviceState =
		&Controller->DeviceState[Controller->DeviceStateIndex]
					[Controller->DeviceStateChannel]
					[Controller->DeviceStateTargetID];
	      if (OldDeviceState->Present &&
		  OldDeviceState->DeviceType == DAC960_DiskType)
		{
		  Command->CommandMailbox.Type3D.CommandOpcode =
		    DAC960_GetDeviceState;
		  Command->CommandMailbox.Type3D.Channel =
		    Controller->DeviceStateChannel;
		  Command->CommandMailbox.Type3D.TargetID =
		    Controller->DeviceStateTargetID;
		  Command->CommandMailbox.Type3D.BusAddress =
		    Virtual_to_Bus(&Controller->DeviceState
				      [Controller->DeviceStateIndex ^ 1]
				      [Controller->DeviceStateChannel]
				      [Controller->DeviceStateTargetID]);
		  DAC960_QueueCommand(Command);
		  return;
		}
	      if (++Controller->DeviceStateTargetID == DAC960_MaxTargets)
		{
		  Controller->DeviceStateChannel++;
		  Controller->DeviceStateTargetID = 0;
		}
	    }
	  Controller->NeedDeviceStateInformation = false;
	  Controller->DeviceStateIndex ^= 1;
	}
      if (Controller->NeedRebuildProgress)
	{
	  Controller->NeedRebuildProgress = false;
	  Command->CommandMailbox.Type3.CommandOpcode =
	    DAC960_GetRebuildProgress;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(&Controller->RebuildProgress);
	  DAC960_QueueCommand(Command);
	  return;
	}
      if (Controller->NeedLogicalDriveInformation &&
	  NewCriticalLogicalDriveCount < OldCriticalLogicalDriveCount)
	{
	  Controller->NeedLogicalDriveInformation = false;
	  Command->CommandMailbox.Type3.CommandOpcode =
	    DAC960_GetLogicalDriveInformation;
	  Command->CommandMailbox.Type3.BusAddress =
	    Virtual_to_Bus(
	      &Controller->LogicalDriveInformation
			   [Controller->LogicalDriveInformationIndex ^ 1]);
	  DAC960_QueueCommand(Command);
	  return;
	}
      Controller->MonitoringTimer.expires =
	jiffies + DAC960_MonitoringTimerInterval;
      add_timer(&Controller->MonitoringTimer);
    }
  else if (CommandType == DAC960_ImmediateCommand)
    {
      up(Command->Semaphore);
      Command->Semaphore = NULL;
      return;
    }
  else panic("DAC960: Unknown Command Type %d\n", CommandType);
  /*
    Queue a Monitoring Command to the Controller using the just completed
    Command if one was deferred previously due to lack of a free Command when
    the Monitoring Timer Function was called.
  */
  if (Controller->MonitoringCommandDeferred)
    {
      Controller->MonitoringCommandDeferred = false;
      DAC960_QueueMonitoringCommand(Command);
      return;
    }
  /*
    Attempt to remove a new I/O Request from the Controller's I/O Request
    Queue and queue it to the Controller using the just completed Command.
    If there is no I/O Request to be queued, deallocate the Command.
  */
  if (!DAC960_ProcessRequest(Controller, Command))
    DAC960_DeallocateCommand(Command);
}


/*
  DAC960_InterruptHandler handles hardware interrupts from DAC960 Controllers.
*/

static void DAC960_InterruptHandler(int IRQ_Channel,
				    void *DeviceIdentifier,
				    Registers_T *InterruptRegisters)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) DeviceIdentifier;
  void *ControllerBaseAddress = Controller->BaseAddress;
  DAC960_V4_StatusMailbox_T *NextStatusMailbox;
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLockIH(Controller, &ProcessorFlags);
  /*
    Process Hardware Interrupts for Controller.
  */
  switch (Controller->ControllerType)
    {
    case DAC960_V4_Controller:
      DAC960_V4_AcknowledgeInterrupt(ControllerBaseAddress);
      NextStatusMailbox = Controller->NextStatusMailbox;
      while (NextStatusMailbox->Fields.Valid)
	{
	  DAC960_CommandIdentifier_T CommandIdentifier =
	    NextStatusMailbox->Fields.CommandIdentifier;
	  DAC960_Command_T *Command = &Controller->Commands[CommandIdentifier];
	  Command->CommandStatus = NextStatusMailbox->Fields.CommandStatus;
	  NextStatusMailbox->Word = 0;
	  if (++NextStatusMailbox > Controller->LastStatusMailbox)
	    NextStatusMailbox = Controller->FirstStatusMailbox;
	  DAC960_ProcessCompletedCommand(Command);
	}
      Controller->NextStatusMailbox = NextStatusMailbox;
      break;
    case DAC960_V3_Controller:
      while (DAC960_V3_StatusAvailableP(ControllerBaseAddress))
	{
	  DAC960_CommandIdentifier_T CommandIdentifier =
	    DAC960_V3_ReadStatusCommandIdentifier(ControllerBaseAddress);
	  DAC960_Command_T *Command = &Controller->Commands[CommandIdentifier];
	  Command->CommandStatus =
	    DAC960_V3_ReadStatusRegister(ControllerBaseAddress);
	  DAC960_V3_AcknowledgeInterrupt(ControllerBaseAddress);
	  DAC960_V3_AcknowledgeStatus(ControllerBaseAddress);
	  DAC960_ProcessCompletedCommand(Command);
	}
      break;
    }
  /*
    Attempt to remove additional I/O Requests from the Controller's
    I/O Request Queue and queue them to the Controller.
  */
  DAC960_ProcessRequests(Controller);
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLockIH(Controller, &ProcessorFlags);
}


/*
  DAC960_QueueMonitoringCommand queues a Monitoring Command to Controller.
*/

static void DAC960_QueueMonitoringCommand(DAC960_Command_T *Command)
{
  DAC960_Controller_T *Controller = Command->Controller;
  DAC960_CommandMailbox_T *CommandMailbox = &Command->CommandMailbox;
  DAC960_ClearCommand(Command);
  Command->CommandType = DAC960_MonitoringCommand;
  CommandMailbox->Type3.CommandOpcode = DAC960_Enquiry;
  CommandMailbox->Type3.BusAddress =
    Virtual_to_Bus(&Controller->Enquiry[Controller->EnquiryIndex ^ 1]);
  DAC960_QueueCommand(Command);
}


/*
  DAC960_MonitoringTimerFunction is the timer function for monitoring
  the status of DAC960 Controllers.
*/

static void DAC960_MonitoringTimerFunction(unsigned long TimerData)
{
  DAC960_Controller_T *Controller = (DAC960_Controller_T *) TimerData;
  DAC960_Command_T *Command;
  ProcessorFlags_T ProcessorFlags;
  /*
    Acquire exclusive access to Controller.
  */
  DAC960_AcquireControllerLock(Controller, &ProcessorFlags);
  /*
    Queue a Status Monitoring Command for Controller;
  */
  Command = DAC960_AllocateCommand(Controller);
  if (Command != NULL)
    DAC960_QueueMonitoringCommand(Command);
  else Controller->MonitoringCommandDeferred = true;
  /*
    Release exclusive access to Controller.
  */
  DAC960_ReleaseControllerLock(Controller, &ProcessorFlags);
}


/*
  DAC960_Open is the Device Open Function for the DAC960 Driver.
*/

static int DAC960_Open(Inode_T *Inode, File_T *File)
{
  int ControllerNumber = DAC960_ControllerNumber(Inode->i_rdev);
  int LogicalDriveNumber = DAC960_LogicalDriveNumber(Inode->i_rdev);
  DAC960_Controller_T *Controller;
  if (ControllerNumber < 0 || ControllerNumber > DAC960_ControllerCount - 1)
    return -ENXIO;
  Controller = DAC960_Controllers[ControllerNumber];
  if (Controller == NULL ||
      LogicalDriveNumber > Controller->LogicalDriveCount - 1)
    return -ENXIO;
  if (Controller->LogicalDriveInformation
		  [Controller->LogicalDriveInformationIndex]
		  [LogicalDriveNumber] .LogicalDriveState
      == DAC960_LogicalDrive_Offline)
    return -ENXIO;
  if (Controller->GenericDiskInfo.sizes[MINOR(Inode->i_rdev)] == 0)
    return -ENXIO;
  /*
    Increment Controller and Logical Drive Usage Counts.
  */
  Controller->ControllerUsageCount++;
  Controller->LogicalDriveUsageCount[LogicalDriveNumber]++;
  return 0;
}


/*
  DAC960_Release is the Device Release Function for the DAC960 Driver.
*/

static void DAC960_Release(Inode_T *Inode, File_T *File)
{
  int ControllerNumber = DAC960_ControllerNumber(Inode->i_rdev);
  int LogicalDriveNumber = DAC960_LogicalDriveNumber(Inode->i_rdev);
  DAC960_Controller_T *Controller = DAC960_Controllers[ControllerNumber];
  /*
    Force any buffered data to be written.
  */
  fsync_dev(Inode->i_rdev);
  /*
    Decrement the Logical Drive and Controller Usage Counts.
  */
  Controller->LogicalDriveUsageCount[LogicalDriveNumber]--;
  Controller->ControllerUsageCount--;
}


/*
  DAC960_Ioctl is the Device Ioctl Function for the DAC960 Driver.
*/

static int DAC960_Ioctl(Inode_T *Inode, File_T *File,
			unsigned int Request, unsigned long Argument)
{
  int ControllerNumber = DAC960_ControllerNumber(Inode->i_rdev);
  int LogicalDriveNumber = DAC960_LogicalDriveNumber(Inode->i_rdev);
  int PartitionNumber, ErrorCode;
  unsigned short Cylinders;
  DiskGeometry_T *Geometry;
  DAC960_Controller_T *Controller;
  if (ControllerNumber < 0 || ControllerNumber > DAC960_ControllerCount - 1)
    return -ENXIO;
  Controller = DAC960_Controllers[ControllerNumber];
  if (Controller == NULL ||
      LogicalDriveNumber > Controller->LogicalDriveCount - 1)
    return -ENXIO;
  switch (Request)
    {
    case HDIO_GETGEO:
      /* Get BIOS Disk Geometry. */
      Geometry = (DiskGeometry_T *) Argument;
      if (Geometry == NULL) return -EINVAL;
      ErrorCode = verify_area(VERIFY_WRITE, Geometry, sizeof(DiskGeometry_T));
      if (ErrorCode != 0) return ErrorCode;
      Cylinders =
	Controller->LogicalDriveInformation
		    [Controller->LogicalDriveInformationIndex]
		    [LogicalDriveNumber].LogicalDriveSize
	/ (Controller->GeometryTranslationHeads *
	   Controller->GeometryTranslationSectors);
      put_user(Controller->GeometryTranslationHeads, &Geometry->heads);
      put_user(Controller->GeometryTranslationSectors, &Geometry->sectors);
      put_user(Cylinders, &Geometry->cylinders);
      put_user(Controller->GenericDiskInfo.part[MINOR(Inode->i_rdev)]
					  .start_sect, &Geometry->start);
      return 0;
    case BLKGETSIZE:
      /* Get Device Size. */
      if ((long *) Argument == NULL) return -EINVAL;
      ErrorCode = verify_area(VERIFY_WRITE, (long *) Argument, sizeof(long));
      if (ErrorCode != 0) return ErrorCode;
      put_user(Controller->GenericDiskInfo.part[MINOR(Inode->i_rdev)].nr_sects,
	       (long *) Argument);
      return 0;
    case BLKRAGET:
      /* Get Read-Ahead. */
      if ((int *) Argument == NULL) return -EINVAL;
      ErrorCode = verify_area(VERIFY_WRITE, (int *) Argument, sizeof(int));
      if (ErrorCode != 0) return ErrorCode;
      put_user(read_ahead[MAJOR(Inode->i_rdev)], (int *) Argument);
      return 0;
    case BLKRASET:
      /* Set Read-Ahead. */
      if (!suser()) return -EACCES;
      if (Argument > 256) return -EINVAL;
      read_ahead[MAJOR(Inode->i_rdev)] = Argument;
      return 0;
    case BLKFLSBUF:
      /* Flush Buffers. */
      if (!suser()) return -EACCES;
      fsync_dev(Inode->i_rdev);
      invalidate_buffers(Inode->i_rdev);
      return 0;
    case BLKRRPART:
      /* Re-Read Partition Table. */
      if (!suser()) return -EACCES;
      if (Controller->LogicalDriveUsageCount[LogicalDriveNumber] > 1)
	return -EBUSY;
      for (PartitionNumber = 0;
	   PartitionNumber < DAC960_MaxPartitions;
	   PartitionNumber++)
	{
	  KernelDevice_T Device = DAC960_KernelDevice(ControllerNumber,
						      LogicalDriveNumber,
						      PartitionNumber);
	  int MinorNumber = DAC960_MinorNumber(LogicalDriveNumber,
					       PartitionNumber);
	  if (Controller->GenericDiskInfo.part[MinorNumber].nr_sects == 0)
	    continue;
	  /*
	    Flush all changes and invalidate buffered state.
	  */
	  sync_dev(Device);
	  invalidate_inodes(Device);
	  invalidate_buffers(Device);
	  /*
	    Clear existing partition sizes.
	  */
	  if (PartitionNumber > 0)
	    {
	      Controller->GenericDiskInfo.part[MinorNumber].start_sect = 0;
	      Controller->GenericDiskInfo.part[MinorNumber].nr_sects = 0;
	    }
	  /*
	    Reset the Block Size so that the partition table can be read.
	  */
	  set_blocksize(Device, BLOCK_SIZE);
	}
      resetup_one_dev(&Controller->GenericDiskInfo, LogicalDriveNumber);
      return 0;
    }
  return -EINVAL;
}


/*
  DAC960_GenericDiskInit is the Generic Disk Information Initialization
  Function for the DAC960 Driver.
*/

static void DAC960_InitializeGenericDiskInfo(GenericDiskInfo_T *GenericDiskInfo)
{
  DAC960_Controller_T *Controller =
    (DAC960_Controller_T *) GenericDiskInfo->real_devices;
  DAC960_LogicalDriveInformation_T *LogicalDriveInformation =
    Controller->LogicalDriveInformation
		[Controller->LogicalDriveInformationIndex];
  int LogicalDriveNumber;
  for (LogicalDriveNumber = 0;
       LogicalDriveNumber < Controller->LogicalDriveCount;
       LogicalDriveNumber++)
    GenericDiskInfo->part[DAC960_MinorNumber(LogicalDriveNumber, 0)].nr_sects =
      LogicalDriveInformation[LogicalDriveNumber].LogicalDriveSize;
}


/*
  DAC960_Message prints Driver Messages.
*/

static void DAC960_Message(DAC960_MessageLevel_T MessageLevel,
			   char *Format,
			   DAC960_Controller_T *Controller,
			   ...)
{
  static char Buffer[DAC960_LineBufferSize];
  static boolean BeginningOfLine = true;
  va_list Arguments;
  int Length = 0;
  va_start(Arguments, Controller);
  Length = vsprintf(Buffer, Format, Arguments);
  va_end(Arguments);
  if (MessageLevel == DAC960_AnnounceLevel)
    {
      static int AnnouncementLines = 0;
      strcpy(&Controller->MessageBuffer[Controller->MessageBufferLength],
	     Buffer);
      Controller->MessageBufferLength += Length;
      if (++AnnouncementLines <= 2)
	printk("%sDAC960: %s", DAC960_MessageLevelMap[MessageLevel], Buffer);
    }
  else if (MessageLevel == DAC960_InfoLevel)
    {
      strcpy(&Controller->MessageBuffer[Controller->MessageBufferLength],
	     Buffer);
      Controller->MessageBufferLength += Length;
      if (BeginningOfLine)
	{
	  if (Buffer[0] != '\n' || Length > 1)
	    printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
		   Controller->ControllerNumber, Buffer);
	}
      else printk("%s", Buffer);
    }
  else
    {
      if (BeginningOfLine)
	printk("%sDAC960#%d: %s", DAC960_MessageLevelMap[MessageLevel],
	       Controller->ControllerNumber, Buffer);
      else printk("%s", Buffer);
    }
  BeginningOfLine = (Buffer[Length-1] == '\n');
}
