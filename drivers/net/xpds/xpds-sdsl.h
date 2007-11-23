/*
 * Copyright 1999, 2000 Xpeed, Inc.
 * xpds-sdsl.h, $Revision: 1.3 $
 * License to copy and distribute is GNU General Public License, version 2.
 */

#ifndef XPDS_SDSL_H
#define XPDS_SDSL_H 1

int xpds_reset_flash (int card_num);
int xpds_read_flash_byte (int card_num, u32 address, u8 *valuep);
int xpds_write_flash_byte (int card_num, u32 address, u8 value);
int xpds_verify_flash (int card_num, u8 *buffer, u8 expected_byte, int length);
int xpds_erase_flash (int card_num);

typedef enum { /* Maximum 128 types total */
                                       /* BEGIN PCI MAILBOX */
/* Write bootdata from the mailbox */
	XPDS_MBX_WRITE_BOOTDATA,
	XPDS_MBX_WRITE_MACADDR,              /* Write the mac address from the mailbox */
	XPDS_MBX_WRITE_HWVER,                /* Write the hardware version from the mailbox */
	XPDS_MBX_WRITE_FWVER,                /* Write the firmware version from the mailbox */
	XPDS_MBX_WRITE_PHYSMODE,             /* Write the physical layer mode */
	XPDS_MBX_READ_MACADDR,               /* Send the mac address to the mailbox */
	XPDS_MBX_READ_HWVER,                 /* Send the hardware version to the mailbox */
	XPDS_MBX_READ_FWVER,                 /* Send the firmware version to the mailbox */
	XPDS_MBX_READ_PHYSMODE,              /* Write the physical layer mode */
	XPDS_MBX_WRITE_TESTMODE,             /* Put the rs8973 into a test mode */
	XPDS_MBX_CLEAR_IRQ,                  /* Clear the interrupt being generated */
	XPDS_MBX_READ_PHYS_STATE,            /* Read the physical layer state */
	XPDS_MBX_WRITE_MFGDATE,		 /* 4 bytes */
	XPDS_MBX_READ_MFGDATE,
	XPDS_MBX_READ_SERIALNUMBER,		 /* 16 bytes */
	XPDS_MBX_WRITE_SERIALNUMBER,
	XPDS_MBX_READ_STAGE,
	XPDS_MBX_START_BITPUMP,
	XPDS_MBX_START_BITPUMP_FOR_MFG,
	XPDS_MBX_READ_PHYSMODE_IN_FLASH,
	XPDS_MBX_READ_PHYS_QUALITY,
	XPDS_MBX_PCIMASTER_MAXTYPE,          /* Types below this value are for the PCI mailbox only */
				       /* BEGIN NIC MAILBOX (r/w from NIC perspective) */
	XPDS_MBX_WRITE_DEBUGPORT = 64,            /* Get the NIC supplied terminal output from the mailbox */
	XPDS_MBX_WRITE_IRQTYPE,              /* Get the type of irq being generated */
	XPDS_MBX_WRITE_EXITCODE,             /* Write the completion code */        
	XPDS_MBX_NICMASTER_MAXTYPE           /* Types below this value are for NIC mailbox only */  
} xpds_mbx_t;

/*
 * IRQ types for value returned by using XPDS_MBX_WRITE_IRQTYPE 
 * with xpds_mailbox_read().
 */
typedef enum {      /* Codes for physical layer i/o */
	XPDS_SDSL_CMD_FAILED,            /* Physical layer command failed */
	XPDS_SDSL_CMD_COMPLETED,         /* Physical layer command completed successfu
	lly */
	XPDS_SDSL_CMD_TIMEOUT,           /* Physical layer command timed out */
	XPDS_SDSL_FLASH_ERASE_ERROR,     /* Flash could not be erased */
	XPDS_SDSL_FLASH_ZERO_ERROR,      /* Flash could not be zeroed out */
	XPDS_SDSL_FLASH_WRITE_ERROR,     /* Flash could not be written */
	XPDS_SDSL_AUX_MODE_PCI,          /* PCI is the aux bus master */
	XPDS_SDSL_AUX_MODE_NIC,          /* NIC is the aux bus master */
	XPDS_SDSL_AUX_MODE_BOTH,         /* Both can be aux master */
	XPDS_SDSL_RAM_ERROR,             /* RAM failed a self-test */
	XPDS_SDSL_IRQTYPE_STATE_CHANGE,  /* Physical layer has gone into an important 
	state */
	XPDS_SDSL_STATE_BOOTED,          /* PL State: Booted */ 
	XPDS_SDSL_TESTMODE_NORMAL_OPERATION,         /* Return to normal operation */
	XPDS_SDSL_TESTMODE_EXTERNAL_ANALOG_LOOPBACK, /* Transmit then receive echo */
	XPDS_SDSL_TESTMODE_FOUR_LEVEL_SCR,           /* Transmit a continuous stream of 4 level scrambled ones */
	XPDS_SDSL_TESTMODE_TWO_LEVEL_SCR,            /* Transmit a continuous stream of 2 level scrambled ones */
	XPDS_SDSL_TESTMODE_INTERNAL_ANALOG_LOOPBACK, /* Loopback bypassing pins */  
	XPDS_SDSL_TESTMODE_FORCE_LINKUP,             /* Force link up so driver can st
	art */                               
	XPDS_SDSL_MAX                                /* Not a code - Used for bounds c
hecking */
} xpds_sdsl_codes_t;

/*
 * Physical layer states for value returned by mailbox read
 * with XPDS_RETURN_PHYS_STATE.
 */
#define XPDS_SDSL_STATE_LOS                   0x01
#define XPDS_SDSL_STATE_LOST                  0x02
#define XPDS_SDSL_STATE_TIPRING_REVERSED      0x04
#define XPDS_SDSL_STATE_2LEVEL_TIMER_EXPIRED  0x08
#define XPDS_SDSL_STATE_GOOD_NOISE_MARGIN     0x10
#define XPDS_SDSL_STATE_RESERVED              0x20
#define XPDS_SDSL_STATE_4LEVEL                0x40
#define XPDS_SDSL_STATE_LINKUP                0x80

/* 
 * SDSL operating mode definitions
 */
#define XPDS_SDSL_MODE__UNUSED		0xfff80000
#define XPDS_SDSL_MODE__NT		0x00040000
#define XPDS_SDSL_MODE__INVERT		0x00020000
#define XPDS_SDSL_MODE__SWAP		0x00010000
#define XPDS_SDSL_MODE__SPEED_MASK	0x0000ffff

typedef struct {
        int     size;
	u8      *image;
} xpds_flash_image_t;

int xpds_mailbox_write (int card_num, xpds_mbx_t transfer_type, u8 byte);
int xpds_mailbox_read (int card_num, xpds_mbx_t transfer_type, u8 *bytep);
int xpds_get_sdsl_exit_code (int card_num, u8 *bytep);
int xpds_get_sdsl_mode (int card_num, u32 *value);
int xpds_get_flash_sdsl_mode (int card_num, u32 *value);
int xpds_set_sdsl_mode (int card_num, u32 value);
int xpds_reset_sdsl (int card_num);
int xpds_start_sdsl (int card_num);
int xpds_install_flash_image (int card_num, xpds_flash_image_t *fd);
int xpds_sdsl_loopback (int card_num);
int xpds_set_sdsl_info (int card_num);
int xpds_sdsl_allocate (void);
int xpds_sdsl_get_state (int card_num, u8 *state);
int xpds_sdsl_cleanup (void);

#define XPDS_SDSL_MAILBOX_READ_INCOMPLETE	1000

#endif
