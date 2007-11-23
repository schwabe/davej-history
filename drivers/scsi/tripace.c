/* tc2550.c -- Tripace TC-2550x based PCI SCSI Adapter  SCSI driver
 * Created:D.Ravi jun 8 1998 at chennai lab of Tripace 
 * Copyright 1998 Tripace Europe BV
 *
 * Driver version 1.00.000 (904)

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.

 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.

 **************************************************************************

 DESCRIPTION:




 REFERENCES USED:

 1.0 Design Kit for the Tripace TC-2550x based PCI SCSI  Adapter

 2.0 Tripace IOLAYER document.

 3.0 LINUX driver sources in /usr/linux/drivers/scsi directory.


 ALPHA TESTERS:

 1.0 so far no external testers,only developer testing has been done.

 2.0 Testing with IBMHDD,Quantum HDD,zip drive and sony CDROM has been 
 done.


 NOTES ON USER DEFINABLE OPTIONS:

 1.0 The following are the command line options that are possible for the
 TRIPACE TC-2550 PCI SCSI controller without BIOS.

 tripace=fast clock,discon,sync,tag


 The values for these four options can be either 0 or 1.
 If fast clock is set to 1 ,then the chip uses a 60mhz clock.If ultra
 scsi devices are used this should be set and the controller should have a
 60mhz crystal.

 if disconnect is 1 ,then disconnect/reconnect is allowed for all scsi
 devices connected to the controller.if it is 0 ,it is off.

 sync = 1 means that synchronous negotiation will be done with scsi
 devices.currently,though the flag is set ,the function is not implemented.

 Tag = 1 means that tagged queue commands can be sent to the scsi devices.
 This is not implemnted as yet in the driver. 

 The default values are 0,1,1,1

 **************************************************************************/

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/head.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <asm/io.h>
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include "tripace.h"
#include <asm/system.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include "tripace_mcode.h"

#define VERSION          "$Revision: 1.00 $"

/* #define DEBUG */


#define   MAXTARGET               8
#define   MAXSRBS		  16

#define   RISC_ENTRY1             0

#define   DEV_PARAM_REG_BASE      CFG_BASE+0x20

#define   SCSI_DMA_POINTER        CFG_BASE+0x40
#define   SCSI_DMA_COUNTER        CFG_BASE+0x44
#define   HOST_DMA_POINTER        CFG_BASE+0x48
#define   HOST_DMA_COUNTER        CFG_BASE+0x4c
#define   MBX_IN_BASE             CFG_BASE+0x50
#define   MBX_OUT_BASE            CFG_BASE+0x54
#define   DEV_HDR_BASE            CFG_BASE+0x58
#define   TASK_Q_BASE             CFG_BASE+0x5c
#define   CURRENT_DEV_PTR         CFG_BASE+0x60
#define   CURRENT_TASK_PTR        CFG_BASE+0x64
#define   SG_LIST_PTR             CFG_BASE+0x68
#define   ACC                     CFG_BASE+0x6c
#define   SCRATCHB                CFG_BASE+0x70
#define   CURRENT_INST            CFG_BASE+0x74
#define   FLAG                    CFG_BASE+0x78
#define   SCRATCHC                CFG_BASE+0x7a
#define   MBX_IN_INDEX            CFG_BASE+0x7b
#define   PC                      CFG_BASE+0x7c
#define   MBX_OUT_INDEX           CFG_BASE+0x7f
#define   PROGRAM_DATA_REG        CFG_BASE+0x80
#define   CONTROL_REG             CFG_BASE+0x84
#define   STATUS_INT_REG          CFG_BASE+0x86
#define   SCSI_DATA               CFG_BASE+0x88
#define   SCSI_CONTROL            CFG_BASE+0x8A
#define   SCSI_ID_REG             CFG_BASE+0x8b

#define   INFO_BASE               0x780
#define   SIG_BASE                0x7D4

#define   Q_FILE_SIZE             8192
#define   DEVHDR_SIZE              512
#define   MBOX_SIZE                 48
#define   BIOS_SIGNATURE          0x55AAC731

#define   PCI_1                   1
#define   PCI_2                   2
#define   MAX                     28

#define   PCI_BUS                 1
#define   PCI_INDEX_PORT          0xCF8
#define   PCI_INDEX               0xF0
#define   PCI_CONFIG              0xC000  /* this needs to include slot number */

#define   FAIL                   -1
/* #define   GOOD                 0 */
#define   ERR                     1
#define   DONE                    2
#define   INT_HALT             0xfe000000	/* interrupt code */

#define   SRB_DONE                0
#define   SRB_ACTIVE              2
#define   SRB_READY               1
#define   SRB_CLEAN               4
#define   SRB_ASSIGNED            8
#define   SRB_STOP                2

#define   STOP_RISC               2
#define   FIRST_CMD               1
#define   SPECIAL_CMD             1
#define   MAX_OFFSET             30
/* #define   MAX_PERIOD             12 */
#define   MAX_PERIOD             12
#define   W_MAX_OFFSET           14

/* msg */
#define   CMD_ABORT              0x6
#define   DEVICE_RESET           0xC


#define   CHECK_SYNC             0xA
#define   CHECK_WIDE             0xD
#define   SYNC_NORESPONSE        0x8
#define   SYNC_REJECTED          0x9
#define   SEL_TIME_OUT	         0x11
#define   ERR_OVERRUN		 0x12
#define   ERR_BUSFREE		 0x13
#define   ERR_PHASE		 0x14
#define   CHECK_COND		 0x04
#define   ERR_PARITY  		 0x05
#define   ERR_MESSAGE 		 0x07
#define   WIDE_NORESPONSE        0x0b
#define   WIDE_REJECTED          0x0c


#define   DO_WIDE_NEGO           0x02
#define   DO_SYNC_NEGO           0x04
#define   FIRST_COMMAND          0x01

#define   ENABLE_WIDE_BUS        0x100
#define   ENABLE_TAG_MSG         0x200

/*---------------------------------------------------------*
**  parameter in register definitions                      *
**                                                         *
**---------------------------------------------------------*/
/*
   status register (read only)
 */
#define      INTR_PENDING         0x01
#define      INTR_DIS             0x02
#define      SCAM_INTR_DIS        0x04
#define      POWER_SAVE_ON        0x08
#define      SCSI_RESET_OUT       0x10
#define      SCAM_INTR            0x20
#define      RISC_HALT            0x40
#define      SELECTION_TIMEOUT    0x80
#define      SCSI_PARITY_ERR      0x100
#define      SCSI_RESET_LAT       0x200
#define      STS_SCSI_RESET       0x400
#define      EEPROM_IN            0x800
#define      SCAM_ARB_WIN         0x1000
#define      EN_DMA               0x2000
#define      EN_ARB_SEL           0x4000
/*
   **   interrupt control register (write only)
 */
#define      RESET_INTR_LATCH     0x01
#define      DIS_INTR             0x02
#define      DIS_SCAM_INTR        0x04
#define      POWER_SAVE           0x08
#define      RESET_SCSI_BUS       0x10
/*ravi 10/3/98 changed for term power disable/enable in command line */
/*#define      EN_TERM_PWR          0x20 */
#define      TERM_PWR_EN            0x20
/*
   **   control register ( read/write )
 */
#define      RISC_CHIP_RESET      0x01
#define      RISC_SINGLE_SETP     0x02
#define      HALT_RISC            0x04
#define      EN_SCSI_SCAM         0x08
#define      DIS_SCSI_PARITY      0x10
#define      EN_TARGET_MODE       0x20
#define      FAST_CLOCK           0x40
#define      EN_MEMORY_WRITE      0x80
#define      DIS_PCI_BURST        0x100
#define      DIS_PCI_CACHE_LINE   0x200
#define      DIS_MUL_CACHE_LINE   0x400
#define      EEPROM_CLOCK         0x800
#define      EEPROM_DATA_OUT      0x1000
#define      EEPROM_CHIP_SEL      0x2000
#define      FAST_SELECTION       0x4000
#define      EN_SCAM_ARB          0x8000

#define      TASK_DONE            0x80


/***************************************************************************
**	 filename: newtypes.h
**	 usage   : type definition 
****************************************************************************/


/***************************************************************************
**	 filename: newtypes.h
**	 usage   : type definition 
****************************************************************************/

#define   MAX_Adapter   4	/* maximu adapters allow                */

/*
   **   command related structure (7 bytes)
 */
typedef struct _task_cmd
{
	u8 CmdInProcess;
	u8 * REQ_Header;
	void (*complete) (void);
}
TaskCmd, *PTaskCmd;

/*
   ** total 128 bytes
 */
typedef struct _HIM
{
	u8 status;		/* device status, 0xFF not installed    */
	u8 Target_ID;		/* target ID                            */
	u8 type;		/* target type mapped to device name    */
	u16 attrib;		/* lo 4bit: 1-Wide, 2-Sync, 4-Tag,      */
	/*          8-removable,                */
	/* hi 4bit: 10h do wide, 20h do sync,   */
	/*          40h do tag, 0x80h - disc    */ 
	/* bit 15: under BIOS, bit 8: > 1GB     */
	 u8 op_param;		/* bit 7-5:period, bit4-0:offset        */
	u8 drv_num;		/* current ID --> 8x for BIOS used only */
	u8 sect_per_track;	/* sector per track                     */
	u8 head_per_cyl;	/* sector per track                     */
	u8 byte_per_sect;	/* byte per sector (BIOS is 512)        */
	u8 link_count;	/* SRB link count:                      */
	u8 * ASPICMDLink;	/* SRB link starting pointer            */
	TaskCmd task[16];	/* array structure for each task        */
	
}
DEVStruct;

/*
   ** total (4K+ 36) bytes for each adapter 
 */
typedef struct _Adapter
{
	u16 IoPort;		/* I/O Port address, 0 notsupport       */
	u8 AdapterID;		/* Adapter scsi ID, default = 7         */
	u8 first_disk_num;	/* first disk number under BIOS (82->2) */
	u8 last_disk_num;	/* last disk number under BIOS          */
	u8 time_factor;	/* used for device scam                 */

	u8 IntrNum;		/* -1 NotSupport, otherwise IRQ         */
	u16 hw_attr;		/* see eeprom def                       */
	u16 sw_attr;		/* see eeprom def                       */
	DEVStruct dev[16];	/* target device structure              */
	u16 mbx_out_ptr;	/* mail box out pointer                 */
	u8 HAParam[16];	/* host adapter parameters              */
	u8 bios_install;	/* adapter has BIOS installed           */
	u16 scam_type;		/* scam use              */
	u8 scam_assigned;	/* scam use              */
	u8 * Signature;	/* BIOS signature & new manager address */
	u32 mbx_in_base;	/* mbx in base  logical address    */
	u32 mbx_out_base;	/* mbx out base logical address    */
	u32 devhdr_base;	/* devhdr base logical address     */
	u32 taskq_base;	/* taskq base  logical address     */
	u32 ptaskq_base;	/* taskq base physical address     */
	u32 pdev_base;		/* taskq base physical address     */
	u32 pmbi_base;		/* taskq base physical address     */
	u32 pmbo_base;		/* taskq base physical address     */
}
Adapter, *PAdapter;

/*
   **  adapter information structure reserved for BIOS usage
 */

typedef struct _dev_parm
{
	u8 header;		/* header             */
	u8 sect_per_track;	/* sector per tarck   */
}
target_parm, *ptarget_parm;
/*
   **  This structure is for BIOS used and read by driver
 */
typedef struct _Ada_data
{
	u8 drv_start;		/* first drive           */
	u8 drv_end;		/* last drive            */
	u16 timfact;		/* timing factor         */
	u32 old_int13;		/* old int13 addr        */
	u8 drive_id[8];	/* index= (8x-drv_start) */
	u8 drive_num[16];	/* device number         */
	target_parm dev[16];	/* device parameters     */
	u8 allow_discon[16];	/* disconnect record     */
	u16 scam_type;		/* scam use              */
	u8 scam_assigned;	/* scam use              */
	u8 adapter_id;	/* scam use              */
	u32 signature;		/* BIOS signature        */
}
HA_data, *PHA_data;


typedef struct _ScatGath
{
	u32 sg_address;	/* must be physical address */
	u32 sg_length;		/* sg length     */
}
ScatGath, *PScatGath;

/* length must be 32 bytes */
typedef struct _risc_srb
{
	u8 Tag_info;		/* Tag information */
	u8 SRB_flag;		/* SRB flag */
	u8 DEV_Status;	/* SRB status */
	u8 ScsiStatus;	/* scsi command status */
	u32 CDB;		/* SCSI Command Block */
	u32 CDBLength;		/* SCSI Command Length */
	u32 SenseDataPtr;	/* auto sense pointer */
	u32 Sense_Cmd_Ptr;	/* auto sense pointer */
	u32 SG_ListPtr;	/* SG list  */
	u8 SGNum;		/* S/G number */
	u8 Identify;		/* Identify  message */
	u8 Sense_LUN;		/* lun               */
	u8 Sense_len;		/* sense len         */
	u32 Cmd_sg_addr;	/* cmr sg ptr point to cmd */
}
RISC_SRB, *PRISC_SRB;


typedef struct _SRB
{				/* for SCSI */
	RISC_SRB *risc_srb;	/* structure of SRB in RISC */
	PAdapter AdapterPtr;	/* a pointer to adapter structure */
	u8 LUN;		/* lun number */
	u8 Tag_type;		/* tag type */
	u8 Request_type;	/* request type */
}
SRB, *PSRB;

typedef struct _sync_tbl
{
	int period;		/* parameter setting  */
	int f_factor;		/* fast clock factor  */
	int s_factor;		/* slow clock factor  */
}
sync_tbl;

/*
   **  total 12 bytes of sg header 
 */
typedef struct _dma_hrd
{
	u32 size;		/* region size        */
	u32 offset;		/* offste             */
	u16 segment;		/* segment            */
	u16 revsed;		/* reserved           */
	u16 num_avail;		/* number available   */
	u16 num_used;		/* number used        */
}
DMA_HDR;

/*
   **  total 60 bytes (for each task in windows)
 */
typedef struct _dma_desc
{
	DMA_HDR dma_hdr;
	ScatGath sg_list[6];
}
dma_desc, *Pdma_desc;

/* Flags */
#define SRB_TOHOST    8
#define SRB_TOTARGET  0x10
#define SRB_NEEDSDT   0x20
#define SRB_SENSE     0x40	/* only for OSD      */
#define SIZE_OF_SG    0x3C	/* size of sg table (60 bytes)  */

/* 2. Target Error == SCSI Status */

/* ============ function definiton ============ 
   ** 1. INPUT:  PSRB
   **    Start a scsi command. 
   * */
static void StartScsiCmd(PRISC_SRB);

#define  CMD_INPROGRESS      0x01
#define  CMD_DISCONCTED      0x02
#define  CMD_DATAXFERED      0x04

#define  TAR_TRUESG          0x01
#define  NEEDSDTN            0x10

#define  MSG_ABORT           0x06
#define  MSG_RESET           0x0C
#define  MSG_ALLOWDISC       0x40
#define  MSG_IDENTIFY        0x80
#define  MSG_EXTENDED        0x01
#define  MSG_NOMSG           0x08
#define  MSG_CMDCOMP         0x00
#define  MSG_DISC            0x04
#define  MSG_IGNWR           0x23
#define  MSG_RESTPTR         0x03
#define  MSG_SAVEPTR         0x02
#define  MSG_REJECTED        0x07
#define  MSG_INITRECVY       0x0f
#define  MSG_LNKCMD          0x0a
#define  MSG_LNKCMDTAG       0x0b
#define  MSG_SIMPLEQUE       0x20
#define  MSG_MDFDATPTR       0x00
#define  MSG_SDTREQ          0x01
#define  MSG_WDTREQ          0x03

#define  SECOND              18	//ticks per second
#define  DEV_EMPTY           0xFF

typedef struct _DEVHDR
{
	u32 Updatedmap;	/* updated bitmap      */
	u32 Startedmap;	/* start bitmap        */
	u32 Currentmap;	/* current bitmap      */
	u8 Task_Index;	/* current task index  */
	u8 Request_type;	/* request type        */
	u8 SpCmddone;		/* special commad done */
	u8 Srb_loc;		/* location            */
	u8 TagCmdCnt;		/* Pending task count  */
	u8 ScsiID;		/* scsi ID             */
	u8 DeviceNum;		/* device number       */
	u8 WideMsg;		/* wide bus message    */
	u8 SyncPeriod;	/* sync period         */
	u8 SyncOffset;	/* sync offset         */
	u8 RtnWideMsg;	/* return wide bus msg */
	u8 RtnSyncPeriod;	/* Rtn sync period     */
	u8 RtnSyncOffset;	/* rtn sync offset     */
	u8 ChkSenseTask;	/* check sense task    */
	u8 SenseCmd[6];	/* sense command       */
}
DevHdr, *PDevHdr;

typedef struct _E2Prom
{
	u16 hw_parm;
	u16 sw_parm;
	u16 dev_parm[16];
}
E2prom, *PE2prom;

/* parameter setting for parameter */
#define		HW_ADAPTER_ID            0x0f
#define		HW_PARITY_DISABLE        0x10
#define		HW_TERMPWR_ENABLE        0x20
#define		HW_FAST_CLOCK            0x40
#define		HW_DIAG_ENABLE           0x80
#define		HW_BURST_DISABLE         0x100
#define		HW_CACHE_LINE_DISABLE    0x200
#define		HW_MULTI_CACHE_DISABLE   0x400
#define		HW_SCAM_ENABLE           0x800
#define		HW_CACHE_LINE_SIZE_4	  0x1000
#define		HW_CACHE_LINE_SIZE_8	  0x2000
#define		HW_CACHE_LINE_SIZE_16	  0x3000
#define		HW_FAST_SELECTION        0x4000
#define		HW_BIOS_DISABLE          0x8000


/* software setting      */
#define		SW_REMOVABLE_SUPPORT     0x1
#define		SW_OVER_1GB              0x2
#define		SW_8_DRIVE_SUPPORT       0x4
#define		SW_MAX_ID		 0x8
#define		SW_DEVICE_CHANGED        0x10

/* parameter setting     */
#define		PM_UNDER_BIOS            0x8000
#define		PM_BIOSSCAN_DISABLE      0x2000
#define		PM_NEED_STARTCMD         0x1000
#define		PM_DISCON_ENABLE         0x800
#define		PM_SYNC_ENABLE           0x400
#define		PM_TAG_ENABLE            0x200
#define		PM_WIDEBUS_ENABLE        0x100
#define		PM_TRANSFER_RATE         0xE0
#define		PM_TRANSFER_OFFSET       0x1F

/* ScsiFunc  */
#define		SUPPORT_MORETHAN16M  0x40
#define		SUPPORT_RESELECT     0x20
#define		SUPPORT_SYNC         0x10
#define		SUPPORT_LINKED       0x08
#define		SUPPORT_CMDQUEUE     0x02
#define		SUPPORT_SFTRE        0x01
#define		SUPPORT_WIDEHOST     0x04
#define		SUPPORT_PARCHK       0x80


static unsigned int port_base = 0;
static unsigned int CFG_BASE = 0;
static unsigned int interrupt_level = 0;

/* period table 

   static sync_tbl  period_tbl[8]= {
   { 0, 12, 12,},
   { 1, 16, 18,},
   { 2, 20, 25,},
   { 3, 25, 31,},
   { 4, 29, 37,},
   { 5, 33, 43,},
   { 6, 50, 50,},
   { 7, 58, 75,},
   };
 */

/* pointer to scsi host struc for each HA */
/* needs change for multiple HA support */

static struct Scsi_Host *tripace_host;

/* Tc2550 mailbox data structures */
static unsigned char tcmbdata[Q_FILE_SIZE + DEVHDR_SIZE + MBOX_SIZE + 64];
/* static alloc of sg table for all tasks */
/* dynamic memory is giving problems */
static unsigned char table[8 * MAXSGENT * MAXSRBS * MAXTARGET + 4];


/* global variables */
/* logical addresses of mailbox in/out,dev header base,task que base */

static unsigned char *startsgptr;
static unsigned long pstartsgptr;


static u32 mbx_in_base, mbx_out_base, devhdr_base, taskq_base;
static u8 TargetID;


/* hostadapter structure-as of now we have a single adapter */
Adapter HostAdapter[1];
unsigned char ReqType, targetID, HANumber, Index, HostID;

/* variable for term power in case of /T option */
static unsigned short int EN_TERM_PWR = TERM_PWR_EN;
static unsigned short fast_clk = 0;
static unsigned short par_off = 0;
static unsigned short discon = 1;
static unsigned short syncflag = 1;
static unsigned short tagflag = 0;


static int makecode(unsigned, unsigned);
static void Init_struc(int);
static int download_RISC_code(void);
static void init_chip_reg(void);

static PRISC_SRB search(PAdapter);
u32 insert_bit(u32, short int);
static void Get_Base(Adapter *);

void tc2550_intr(int, void *, struct pt_regs *);

static void internal_done(Scsi_Cmnd *);

int tc2550_command(Scsi_Cmnd *);

static int tc2550_pci_bios_detect(int *irq, int *iobase)
{
	int error;
	unsigned char pci_bus, pci_dev_fn;	/* PCI bus & device function */
	unsigned char pci_irq;	/* PCI interrupt line */
	unsigned int pci_base;	/* PCI I/O base address */
	unsigned short pci_vendor, pci_device;	/* PCI vendor & device IDs */


	/* We will have to change this if more than 1 PCI bus is present and the
	   tripace scsi host is not on the first bus (i.e., a PCI to PCI bridge,
	   which is not supported by bios32 right now anyway). */

	pci_bus = 0;

	for (pci_dev_fn = 0x0; pci_dev_fn < 0xff; pci_dev_fn++)
	{
		pcibios_read_config_word(pci_bus,
					 pci_dev_fn,
					 PCI_VENDOR_ID,
					 &pci_vendor);

		if (pci_vendor == 0x1190)
		{
			pcibios_read_config_word(pci_bus,
						 pci_dev_fn,
						 PCI_DEVICE_ID,
						 &pci_device);

			if (pci_device == 0xc731)
			{
				/* Break out once we have the correct device.  If othertrip 
				   PCI devices are added to this driver we will need to add
				   an or of the other PCI_DEVICE_ID here. */
				printk(KERN_INFO "Tripace TC-2550x based PCI SCSI Adapter detected\n");
				break;
			} else
			{
				/* If we can't finl an tripace scsi card we give up. */
				return 0;
			}
		}
	}

/* vendor id not found */

	if (pci_device != 0xc731)
	{
		printk(KERN_INFO "Tripace TC-2550x - No Host Adapter Detected \n");
		return (0);
	}
	/* We now have the appropriate device function for the tripace board so we
	   just read the PCI config info from the registers.  */

	if ((error = pcibios_read_config_dword(pci_bus,
					       pci_dev_fn,
					       PCI_BASE_ADDRESS_0,
					       &pci_base))
	    || (error = pcibios_read_config_byte(pci_bus,
						 pci_dev_fn,
						 PCI_INTERRUPT_LINE,
						 &pci_irq)))
	{
		printk(KERN_ERR "Tripace TC-2550x not initializing"
		       " due to error reading configuration space\n");
		return 0;
	} else
	{
		printk(KERN_INFO "TC-2550x PCI: IRQ = %u, I/O base = %X\n",
		       pci_irq, pci_base);

		/* Now we have the I/O base address and interrupt from the PCI
		   configuration registers.  
		 */

		*irq = pci_irq;
		*iobase = (pci_base & 0xfff8);
		CFG_BASE = *iobase;

		printk(KERN_INFO "TC-2550x Driver version 1.00.000 (904)\n");
		printk(KERN_INFO "TC-2550x: IRQ = %d, I/O base = 0x%X\n", *irq, *iobase);
		return 1;
	}
	return 0;
}

static void init_chip_reg(void)
{
	int i, val, base;
	unsigned long tick;

	outw(HALT_RISC, CONTROL_REG);
	base = CFG_BASE + 0x20;
	for (i = 0; i < 16; i++)
		outw(0, base + i * 2);
	outw(EN_TERM_PWR, STATUS_INT_REG);
	outw(RESET_SCSI_BUS, STATUS_INT_REG);

	udelay(50);		/*wait for 50 micro secs */

	outw(EN_TERM_PWR, STATUS_INT_REG);
	val = (HALT_RISC | RISC_CHIP_RESET);
	outw(val, CONTROL_REG);
	outw(HALT_RISC, CONTROL_REG);
	val = inw(STATUS_INT_REG);
	tick = 0;
	while ((val & 0x40) == 0 && tick < 0x3fff)
	{
		val = inw(STATUS_INT_REG);
		tick += 1;
	};
	if (tick == 0x3fff)
		printk(KERN_DEBUG "val= %x  \n\r", val);
	outw(EN_TERM_PWR, STATUS_INT_REG);
	outw(0, SCSI_CONTROL);

/* delay for 50 micro secs */
	udelay(50);
}

static int download_RISC_code(void)
{
	unsigned short i, j, fast = 0;
	unsigned short hi, low, base;
	long tmp;
	unsigned long start_time;

	i = inw(CONTROL_REG);
	if (i & 0x40)
		fast = 1;
	outw(HALT_RISC, CONTROL_REG);

/* Ravi modified for sanity check dec 17 1998 */
	start_time = jiffies;

	do
	{
		if ((jiffies - start_time) > 5 * HZ)
		{
			printk(KERN_ERR "tc2550: Download failure.\n");
			return 1;
		}
		i = inw(STATUS_INT_REG);
	}
	while ((i & 0x40) == 0);

	outw(HALT_RISC + EN_MEMORY_WRITE, CONTROL_REG);
	outw(EN_TERM_PWR + DIS_INTR, STATUS_INT_REG);

	/*  download load RISC code
	 */
	outw(0, PC);
	for (i = 0; i < ucode_size; i++)
		outl(ucode_instruction[i], CURRENT_INST);
	/*
	   //  checksum checking (word)
	 */
	base = 0;
	for (i = 0; i < ucode_size; i++)
	{
		outw(i * 4, PC);
		tmp = inl(PROGRAM_DATA_REG);
		hi = tmp >> 16;
		low = (tmp & 0xffff);
		base = base + (hi + low);
	}
	outw(HALT_RISC + RISC_CHIP_RESET, CONTROL_REG);
	outw(HALT_RISC, CONTROL_REG);
	if (fast_clk)
		outw((HALT_RISC | FAST_CLOCK), CONTROL_REG);
	if (par_off)
	{
		i = inw(CONTROL_REG);
		outw((i | DIS_SCSI_PARITY), CONTROL_REG);
	}
	outw(EN_TERM_PWR, STATUS_INT_REG);
	/*ravi
	 */
	if ((unsigned short) (ucode_checksum + base) != 0)
	{
		printk(KERN_ERR "tc2550: Checksum Error During Code Download\n");
		return (1);
	};
	/*
	   load vector table
	 */
	j = 0;
	base = CFG_BASE;
	for (i = 0, j = 0; i < 15; i++, j = j + 2)
		outw(ucode_vector[i], (base + j));
	outw(0, SCSI_CONTROL);
	return (0);
}

int tc2550_detect(Scsi_Host_Template * tpnt)
{
	int flag = 0;
	int retcode;
	struct Scsi_Host *shpnt;
	unsigned long flags;
	unsigned int mod4;


	flag = tc2550_pci_bios_detect(&interrupt_level, &port_base);
	if (!flag)
		return (0);

	init_chip_reg();	/* chip Tc-2550 initialize */
	flag = download_RISC_code();
	if (flag == 0)
	{
		printk(KERN_INFO "tc2550: Successful F/W download on TC-2550x\n");
	}
/* now do a scsi register and get scsi host ptr */

	shpnt = scsi_register(tpnt, 0);

	save_flags(flags);
	cli();
	retcode = request_irq(interrupt_level,
			      tc2550_intr, SA_INTERRUPT, "tripace", NULL);
	if (retcode)
	{
		printk(KERN_ERR "tc2550: Unable to allocate IRQ for Tripace TC-2550x based SCSI Host Adapter.\n");
		goto unregister;
	}
	/* For multiple HA we need to change all this */


	tripace_host = shpnt;
	shpnt->io_port = CFG_BASE;
	shpnt->n_io_port = 0xfc;	/* Number of bytes of I/O space used */
	shpnt->dma_channel = 0;
	shpnt->irq = interrupt_level;

	restore_flags(flags);


	/* log i/o ports with the kernel */
	request_region(port_base, 0xfc, "tripace");

/* when we support multiple HA ,we need to modify */
	Init_struc(0);		/* init mailboxes for one adapter */
/* sg table init */

	/*  get physical address */
	startsgptr = (unsigned char *) table;
	pstartsgptr = virt_to_phys((unsigned char *) table);
	mod4 = pstartsgptr % 4;
	if (mod4)
	{
		pstartsgptr += (4 - mod4);
		startsgptr += (4 - mod4);
	}
	return (0);


unregister:
	scsi_unregister(shpnt);
	return (0);
}

/****************************************************************************
**   Init chip registers and allocate required memory space
****************************************************************************/

static void Init_struc(int id)
{
	u32 pmbx_in_base, pmbx_out_base, pdevhdr_base, ptaskq_base;
	unsigned long paddr;
	char *laddr;
	unsigned short modulo;

/* setup ioport address and irq */

	HostAdapter[id].IoPort = (u16) CFG_BASE;
	HostAdapter[id].IntrNum = (u8) interrupt_level;

	laddr = tcmbdata;
	paddr = virt_to_phys(tcmbdata);
/* adjust phys address to 32 byte boundary */
	modulo = paddr % 32;
	if (modulo)
	{
		paddr = paddr + 32 - modulo;
		laddr = laddr + 32 - modulo;
	}
	/*  logical address  */
	mbx_in_base = (u32) laddr;
	pmbx_in_base = paddr;
	HostAdapter[id].mbx_in_base = mbx_in_base;

	laddr += 32;
	paddr += 32;
	mbx_out_base = (u32) laddr;
	pmbx_out_base = paddr;
	HostAdapter[id].mbx_out_base = mbx_out_base;
	memset((char *) mbx_in_base, 0, 48);

	laddr += 32;
	paddr += 32;
	devhdr_base = (u32) laddr;
	pdevhdr_base = paddr;
	HostAdapter[id].devhdr_base = devhdr_base;
	memset((char *) devhdr_base, 0, 512);


	laddr += 512;
	paddr += 512;

	taskq_base = (u32) laddr;
	ptaskq_base = paddr;
	HostAdapter[id].taskq_base = taskq_base;
	memset((char *) taskq_base, 0, Q_FILE_SIZE);

	HostAdapter[id].ptaskq_base = (u32) ptaskq_base;
	HostAdapter[id].pdev_base = (u32) pdevhdr_base;
	HostAdapter[id].pmbi_base = (u32) pmbx_in_base;
	HostAdapter[id].pmbo_base = (u32) pmbx_out_base;

	outl(pmbx_in_base, MBX_IN_BASE);
	outl(pmbx_out_base, MBX_OUT_BASE);
	outl(pdevhdr_base, DEV_HDR_BASE);
	outl(ptaskq_base, TASK_Q_BASE);
	outb(0, MBX_IN_INDEX);
	outb(0, MBX_OUT_INDEX);
	/* clear mailbox out pointer */
	HostAdapter[id].mbx_out_ptr = 0;
}


/***************************************************************************
**   adjust bitmap position
**   Input : old bitmap,  new bit location 
**   Output: return with new  bitmap layout
****************************************************************************/

extern __inline__ u32 insert_bit(u32 bits, short int loc)
{
	u32 lo = 0;

	lo = 1;
	lo <<= loc;


	return ((lo | bits));
}


/***************************************************************************
**   Get the base address and structures of current host adapter
**   Input  : adapter structure pointer
**   Output : none
****************************************************************************/

static void Get_Base(Adapter * padapter)
{

	CFG_BASE = padapter->IoPort;
	interrupt_level = (u16) padapter->IntrNum;
	mbx_in_base = padapter->mbx_in_base;
	mbx_out_base = padapter->mbx_out_base;
	devhdr_base = padapter->devhdr_base;
	taskq_base = padapter->taskq_base;

}



/***************************************************************************
**  name   : search()
**  Desc   : search an available SRB from taskQ
**  Input  : adapter structure pointer
**  Output : 1. a risc structure space pointer or 0 for non available
**           2. index of task location
****************************************************************************/
static PRISC_SRB search(PAdapter pa)
{
	PRISC_SRB rsrb;
	short int i;
	unsigned long flags;

	Get_Base(pa);
	rsrb = (PRISC_SRB) (taskq_base + TargetID * 16 * sizeof(RISC_SRB));
	/* check attrib in case drive doesn't support SYNC xfer */

	if ((pa->dev[TargetID].attrib & DO_SYNC_NEGO) == 0)
		ReqType = 0;
	save_flags(flags);
	cli();

	Index = 0;
	for (i = 0; i < 16; i++, rsrb++)
		if (rsrb->SRB_flag == SRB_DONE)
			break;
	if (i == 16)
	{
		restore_flags(flags);
		return (0);
	}
	Index = i;
	restore_flags(flags);
	memset((char *) rsrb, 0, 32);
	rsrb->SRB_flag = SRB_ASSIGNED;	/* mark for use */
	if ((pa->dev[TargetID].attrib & 0x44) == 0x44)
		rsrb->Tag_info = 0x20;
	return (rsrb);
}

/***************************************************************************
**  Name   : StartSCSICmd()
**  func   : 1. all commands passed through here are regular
**           2. fill in device structure bitmap && start RISC
**  Input  : risc srb structure, Index, ReqType
**  Output : none
****************************************************************************/
static void StartScsiCmd(PRISC_SRB rsrb)
{
	DevHdr *dev;
	u16 status;
	PAdapter pa;
	unsigned short val;
	u8 t, find_id, find_last, ch = 0;
	char *mptr;
/* Request sense CDB */
	char RequestSense[6] =
	{0x03, 0x00, 0x00, 0x00, 0x0e, 0};

	pa = (PAdapter) & HostAdapter[HANumber];
	pa->dev[TargetID].task[Index].CmdInProcess = 1;		/* command in process */
	dev = (DevHdr *) (devhdr_base + TargetID * sizeof(DevHdr));
	dev->Srb_loc = Index;
	dev->Request_type = ReqType;	/* set request type */
	dev->Updatedmap = insert_bit(dev->Updatedmap, Index);
	for (val = 0; val < 6; val++)
		dev->SenseCmd[val] = RequestSense[val];
	rsrb->Sense_Cmd_Ptr = virt_to_phys(&dev->SenseCmd[0]);

	if (ReqType >= 2)
	{
		if (ReqType & 0x2)
			dev->WideMsg = 0x1;
		else
		{
/*        printf("firing sync nego");
   //ravi 10/3/98 -ultra support in parse 

   if(fast_clk)
   dev->SyncPeriod =   period_tbl[(ultra[TargetID])].f_factor;
   else
   dev->SyncPeriod =   period_tbl[(ultra[TargetID])].s_factor;
 */

			if (pa->dev[TargetID].attrib & 0x1)
				dev->SyncOffset = W_MAX_OFFSET;
			else
				dev->SyncOffset = MAX_OFFSET;
		}
	}
	mptr = (char *) mbx_in_base;
	find_id = 0xff;
	find_last = 0xff;
	for (t = 0; t < 15; t++)
	{
		ch = *mptr++;
		if ((ch & 0xf) == TargetID)
		{
			find_id = t;
			break;
		}
	}

	mptr = (char *) mbx_in_base;
	for (t = 0; t < 15; t++)
	{
		ch = *mptr++;
		if (ch & 0x80)
		{
			find_last = t;
			break;
		}
	}
	val = inw(STATUS_INT_REG);
	mptr = (char *) mbx_in_base;
	if (find_id != 0xff && find_last != 0xff)
	{
		if (find_id < find_last)
			*(char *) (mptr + find_id) |= 0x10;
		else
		{
			*(char *) (mptr + find_last) &= 0x7f;
			*(char *) (mptr + find_id) |= 0x90;
		}
	} else if (find_last != 0xff && find_id == 0xff)
	{
		*(char *) (mptr + find_last) &= 0x7f;
		find_last = find_last + 1;
		*(char *) (mptr + find_last) = (0x90 | TargetID);
	} else if (find_last == 0xff)
		*(char *) mptr = (0x90 | TargetID);


	ReqType = 0;
	/*
	   //  restart the RISC if it is halted before
	 */

	rsrb->SRB_flag = SRB_READY;


	if (val & RISC_HALT)
	{
		outw(ucode_start, PC);	/* set pc counter */
		status = inw(CONTROL_REG);	/* clear halt status */
		outw((status & ~HALT_RISC), CONTROL_REG);
	}
#ifdef DEBUG
	printk(KERN_DEBUG " start scsi issued \n");
#endif

}

static void internal_done(Scsi_Cmnd * SCpnt)
{
	SCpnt->SCp.Status++;
}

int tc2550_command(Scsi_Cmnd * SCpnt)
{
	tc2550_queue(SCpnt, internal_done);

	SCpnt->SCp.Status = 0;
	while (!SCpnt->SCp.Status)
		barrier();
	return SCpnt->result;
}

int tc2550_queue(Scsi_Cmnd * SCpnt, void (*done) (Scsi_Cmnd *))
{

	PAdapter pa;
	PRISC_SRB rsrb;
	int val;
	struct scatterlist *sgpnt;


	ScatGath *riscsgptr;


	int i;
	unsigned int nentries;

	pa = (PAdapter) & HostAdapter[HANumber];
	Get_Base(pa);
	val = 0xaa;
	TargetID = SCpnt->target;
	HostID = 7;
	/* the following code is for corel compatibility */
	if (SCpnt->lun != 0 ||
	    (TargetID == HostID))
	{
		SCpnt->result = DID_BAD_TARGET << 16;
		done(SCpnt);
		return (0);
	};
	/* Error if more than 16 tasks /target if no space is available  */

	if ((rsrb = search(pa)) == 0)
	{
		SCpnt->result = DID_ERROR << 16;
		done(SCpnt);
		return (0);
	};

/*Ravi -modified for hostid 16/12/98 */
	outb((u8) ((TargetID << 4) | HostID), SCSI_ID_REG);


	rsrb->CDBLength = SCpnt->cmd_len;
/* get the physical address of CDB */
	rsrb->CDB = virt_to_phys((unsigned char *) SCpnt->cmnd);


	if (discon)

		rsrb->Identify = 0xC0;
	else
		rsrb->Identify = 0x80;

/* scatter gather processing */

	nentries = SCpnt->use_sg;
	if (nentries == 0)
		nentries = 1;
	rsrb->SGNum = nentries;

#ifdef DEBUG
	printk(KERN_DEBUG "sgentries = %d\n", nentries);
#endif

/*allocate mem for scatter gather table at 32bit boundary */
	SCpnt->host_scribble = startsgptr + 8 * MAXSGENT * MAXSRBS * TargetID;

	/*(unsigned char *)scsi_malloc(4096); */

	sgpnt = (struct scatterlist *) SCpnt->request_buffer;

	riscsgptr = (PScatGath) (SCpnt->host_scribble);
	if (riscsgptr == NULL)
		panic("tripace: unable to allocate DMA memory\n");


	/* fill physical address of scatter-gather list */
	rsrb->SG_ListPtr = virt_to_phys(SCpnt->host_scribble);
	rsrb->Cmd_sg_addr = virt_to_phys(rsrb + 4);

	if (SCpnt->use_sg)
	{
		for (i = 0; i < SCpnt->use_sg; i++)
		{
			if (sgpnt[i].length == 0 || SCpnt->use_sg > 255)
			{
				unsigned char *ptr;
				printk(KERN_ERR "tc2550: Bad segment list supplied to Tripace.c (%d, %d)\n", SCpnt->use_sg, i);
				for (i = 0; i < SCpnt->use_sg; i++)
				{
					printk(KERN_ERR "%d: %x %x %d\n", i, (unsigned int) sgpnt[i].address, (unsigned int) sgpnt[i].alt_address,
					       sgpnt[i].length);
				};
				printk(KERN_ERR "RISCGPTR %x: ", (unsigned int) riscsgptr);
				ptr = (unsigned char *) &riscsgptr[i];
				for (i = 0; i < 18; i++)
					printk("%02x ", ptr[i]);
				panic("Tripace tc-2550x driver!");
			};

			riscsgptr[i].sg_address = (u32) sgpnt[i].address;
			riscsgptr[i].sg_length = sgpnt[i].length;
		};
	} else
	{
		riscsgptr[0].sg_address = (u32) SCpnt->request_buffer;
		riscsgptr[0].sg_length = SCpnt->request_bufflen;
	};


/* fill sense data pointer and len */

	rsrb->Sense_len = sizeof(SCpnt->sense_buffer);
	rsrb->SenseDataPtr = virt_to_phys(SCpnt->sense_buffer);

/* store scsi command pointer for use in intr routine */
	pa->dev[TargetID].task[Index].REQ_Header = (u8 *) SCpnt;
	SCpnt->scsi_done = done;

	/* pa->dev[TargetID].task[Index].complete = CompleteIORequest; */
	ReqType = 0;
	StartScsiCmd(rsrb);
	return 0;
}

int tc2550_reset(Scsi_Cmnd * SCpnt)
{
	return 0;

}

#include "sd.h"

int tc2550_biosparam(Scsi_Disk * disk, int dev, int *info_array)
{
	return 0;

}

int tc2550_abort(Scsi_Cmnd * SCpnt)
{

	return 0;
}


const char *tc2550_info(struct Scsi_Host *ignore)
{

	return 0;
}

void tc2550_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	void (*my_done) (Scsi_Cmnd *) = NULL;

	int val, id, map = 0, tmap, mbx_out_ptr;
	u8 loc = 0;
	PRISC_SRB rsrb;
	DevHdr *dev;
	char *ptr0;
	PAdapter padapter;
/* 
   int i ;
   unsigned long flags ;
 */
	unsigned int memsize;
	Scsi_Cmnd *SCtmp;
	unsigned devstat = 0;
	unsigned scsistat = 0;
	long start_time;

#ifdef DEBUG
	printk("interrupt registered \n");
#endif

/*
   save_flags(flags);
   cli();
 */

	/* multiple HA? not supported now! */
	HANumber = 0;
	padapter = (PAdapter) & HostAdapter[HANumber];
	Get_Base(padapter);

/* disable interrupts */
	val = inw(STATUS_INT_REG);

	udelay(10);

	val |= 0x20;		/* clear interrupt pending */
	outw(val, STATUS_INT_REG);

	udelay(10);

	val |= 0x22;		/* disable interrupt */

	outw(val, STATUS_INT_REG);
	/*               
	   // if RISC is in halt state then find out why ?
	 */
	tmap = inw(STATUS_INT_REG);
/* The following code needs to be added when we intro sync /wide nego */
/*
   if(tmap & RISC_HALT) {
   tmap= risc_halt_check();
   if(tmap) return(0xff);
   };
 */


	mbx_out_ptr = padapter->mbx_out_ptr;

	val = *(u16 *) (mbx_out_base + mbx_out_ptr);

	while (val & 0x80)
	{
		loc = (char) (val & 0x7f);
		id = val & 0xff00;
		id >>= 8;
		TargetID = id;
		dev = (DevHdr *) (devhdr_base + TargetID * sizeof(DevHdr));
		/*(u16  *)(mbx_out_base+ mbx_out_ptr)= (val& 0xff7f); */
		rsrb = (PRISC_SRB) (taskq_base + (id * 16 + loc) * sizeof(RISC_SRB));
		Index = loc;
		devstat = rsrb->DEV_Status;
		scsistat = rsrb->ScsiStatus;

/*      (*padapter->dev[TargetID].task[loc].complete)(rsrb); */

		*(u16 *) (mbx_out_base + mbx_out_ptr) = (val & 0xff7f);

		padapter->dev[TargetID].task[loc].CmdInProcess = 0;
		mbx_out_ptr += 2;
		if (mbx_out_ptr == 32)
			mbx_out_ptr = 0;
		padapter->mbx_out_ptr = mbx_out_ptr;
		/*
		 *	Clear init bimap if no more tasks are waiting
		 */
		map = 1;
		dev->Updatedmap = (dev->Updatedmap ^ (map << loc));
		if ((dev->Updatedmap & 0x0000ffff) == 0)
		{
			ptr0 = (char *) mbx_in_base;
			for (map = 0; map < 16; map++)
				if ((*ptr0 & 0xf) == (char) id)
				{
					*ptr0 = (*ptr0 & 0xef);
					break;
				} else
					ptr0++;
		}
		val = *(u16 *) (mbx_out_base + mbx_out_ptr);

		rsrb->SRB_flag = SRB_DONE;	/* mark done */
	}

	/* stop RISC if mailbox is empty  */

	ptr0 = (char *) mbx_in_base;
	for (map = 0; map < 16; map++)
		if (*ptr0 & 0x10)
			break;
	if (map == 16)
	{
		tmap = inw(CONTROL_REG);
		outw((tmap | HALT_RISC), CONTROL_REG);

/*Ravi modified to introduce sanity check&time out dec 16 1998 */

		start_time = jiffies;
		
		do
		{
			if ((start_time - jiffies) > 5 * HZ)
			{
				printk(KERN_ERR "tc2550: TC-2550x Controller Failure\n");
				return;
			}
			tmap = inw(STATUS_INT_REG);
		}
		while((tmap & RISC_HALT) == 0);
	}
	SCtmp = (Scsi_Cmnd *) padapter->dev[TargetID].task[loc].REQ_Header;

	if (!SCtmp || !SCtmp->scsi_done)
	{
		printk(KERN_ERR "tc2550: Tripace_Intr_Handle: Unexpected Interrupt\n");
		return;
	}
	memsize = 255 * sizeof(struct _ScatGath) + 4;
	my_done = SCtmp->scsi_done;
	/*if (SCtmp->host_scribble)
	   scsi_free(SCtmp->host_scribble,4096); */


	padapter->dev[TargetID].task[loc].REQ_Header = NULL;
	SCtmp->result = makecode(devstat, scsistat);
/*enable chip interrupt signal */
	val = inw(STATUS_INT_REG);
	udelay(25);		/* delay for 25 micros */

	val &= 0xfd;		/* enable interrupt-bit1=0 in status-int reg */
	outw(val, STATUS_INT_REG);

	my_done(SCtmp);		/* inform mid layer that scsi command is over */
/*
   restore_flags(flags);
 */

}

/* called from init/main.c */
void tripace_setup(char *str, int *ints)
{
	switch (ints[0])
	{

	case 0:

		printk(KERN_INFO "tc2550: No Arguments In Command Line:Assuming Defaults\n");
		break;

	case 1:
		fast_clk = ints[1];
		break;

	case 2:
		fast_clk = ints[1];
		discon = ints[2];
		break;

	case 3:
		fast_clk = ints[1];
		discon = ints[2];
		syncflag = ints[3];
		break;

	case 4:
		fast_clk = ints[1];
		discon = ints[2];
		syncflag = ints[3];
		tagflag = ints[4];
	}

#ifdef DEBUG
	printk("fast_clk = %d,discon = %d,syncflag =%d,tagflag=%d\n",
	       fast_clk, discon, syncflag, tagflag);
	printk("fast_clk = %d,discon = %d,syncflag =%d,tagflag=%d\n",
	       fast_clk, discon, syncflag, tagflag);
	printk("fast_clk = %d,discon = %d,syncflag =%d,tagflag=%d\n",
	       fast_clk, discon, syncflag, tagflag);
	printk("fast_clk = %d,discon = %d,syncflag =%d,tagflag=%d\n",
	       fast_clk, discon, syncflag, tagflag);
#endif

}

static int makecode(unsigned hosterr, unsigned scsierr)
{
	switch (hosterr)
	{
	case 0x0:
		hosterr = 0;
		break;

	case SEL_TIME_OUT:	/* Selection time out-The initiator selection or target
				   reselection was not complete within the SCSI Time out period */
		hosterr = DID_TIME_OUT;
		break;

	case ERR_PARITY:	/* parity error */

		hosterr = DID_PARITY;
		break;

	case ERR_OVERRUN:	/* Data overrun/underrun-The target attempted to transfer more data
				   than was allocated by the Data Length field or the sum of the
				   Scatter / Gather Data Length fields. */

	case ERR_BUSFREE:	/* Unexpected bus free-The target dropped the SCSI BSY at an unexpected time. */


	case ERR_PHASE:	/* Target bus phase sequence failure-An invalid bus phase or bus
				   phase sequence was requested by the target. */

		hosterr = DID_ERROR;	/* Couldn't find any better */
		break;

	default:
		hosterr = DID_ERROR;
		printk(KERN_ERR "tc2550: Makecode: Unknown Hoststatus %x\n", hosterr);
		break;
	}
	return scsierr | (hosterr << 16);
}
