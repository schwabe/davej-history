/*
 * Copyright 1999, 2000 Xpeed, Inc.
 * xpds-sdsl.c, $Revision: 1.7 $
 * License to copy and distribute is GNU General Public License, version 2.
 */
#ifdef DEBUG
#define	dprintk		if (xpds_debug_level & DEBUG_MAIN) printk
#define	ddprintk	if ((xpds_debug_level & (DEBUG_MAIN | DEBUG_DETAILED)) == (DEBUG_MAIN | DEBUG_DETAILED)) printk
#else
#define	dprintk		if (0) printk
#define	ddprintk	if (0) printk
#endif

#ifndef __KERNEL__
#define __KERNEL__ 1
#endif
#ifndef MODULE
#define MODULE 1
#endif

#define __NO_VERSION__ 1
#include <linux/module.h>
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>

#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include "xpds-encap-fr.h"
#include <linux/if_arp.h>

#include <linux/tqueue.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>  

#if ! defined (CONFIG_PCI)
#error "CONFIG_PCI is not defined"
#endif

#include "xpds.h"
#include "xpds-reg.h"

#define	AUX_CONTROL	0x0
#define	AUX_WRITE_ADDR	0x4
#define	AUX_WRITE_DATA	0x8
#define	AUX_READ_DATA	0x4
#define	AUX_READ_ADDR	0x8
#define	AUX_MAILBOX	0xc

#define AUX_CONTROL__BUSY	0x08

#define ASIC_FLASHSIZE	0x8000
#define FPGA_FLASHSIZE	0x10000

/*
 * Using __inline__ produces bugs.
 */
#define __inline__

#define	aux_busy_wait(card_num,n) \
	do { \
		u32	wait_until_abw = jiffies + (n) * HZ; \
		for (;;) { \
			u32	val_d; \
			int	rc_abw; \
			rc_abw = xpds_read_control_register_quiet (card_num, AUX_CONTROL, &val_d, AUX); \
			if (rc_abw > 0) return rc_abw; \
			if (! (val_d & AUX_CONTROL__BUSY) ) break; \
			schedule_if_no_interrupt (card_num); \
			if (jiffies > wait_until_abw) return 5; \
		} \
	} while (0)

#define flash_addr_control(card_num,addr,ctrl) \
	do { \
		int	rc_fac; \
		rc_fac = xpds_write_control_register_quiet (card_num, AUX_WRITE_ADDR, (addr), AUX); \
		if (rc_fac > 0) return rc_fac; \
		rc_fac = xpds_write_control_register_quiet (card_num, AUX_CONTROL, (ctrl), AUX); \
		if (rc_fac > 0) return rc_fac; \
		aux_busy_wait (card_num, 1); \
	} while (0)

#define flash_data_control(card_num,data,ctrl) \
	do { \
		int	rc_fdc; \
		rc_fdc = xpds_write_control_register_quiet (card_num, AUX_WRITE_DATA, (data), AUX); \
		if (rc_fdc > 0) return rc_fdc; \
		rc_fdc = xpds_write_control_register_quiet (card_num, AUX_CONTROL, (ctrl), AUX); \
		if (rc_fdc > 0) return rc_fdc; \
		aux_busy_wait (card_num, 1); \
	} while (0)

__inline__ int
xpds_reset_flash (int card_num)
{
	flash_addr_control (card_num, 0, 0x106);
	flash_data_control (card_num, 0xff, 0x105);
	return 0;
}

__inline__ static int
set_flash_read_mode (int card_num)
{
	flash_addr_control (card_num, 0, 0x106);
	flash_data_control (card_num, 0, 0x105);
	return 0;
}

__inline__ int
xpds_read_flash_byte (int card_num, u32 address, u8 *valuep)
{
	u32	val;
	int	rc;

	rc = set_flash_read_mode (card_num);
	if (rc > 0) return rc;

	address &= 0xffff;

	flash_addr_control (card_num, address, 0x106);
	flash_data_control (card_num, 0xaa, 0x104);

	/* will this work on a big-endian computer? */
	rc = xpds_read_control_register_quiet (card_num,
		AUX_READ_DATA, &val, AUX);
	if (rc > 0) return rc;

	*valuep = val;

	ddprintk ("flash[%04x]->%02x\n", address, *valuep);
	return 0;
}

__inline__ int
xpds_write_flash_byte (int card_num, u32 address, u8 value)
{
	int	rc, done, count;
	u8	rval;

	rc = xpds_reset_flash (card_num);
	if (rc > 0) return rc;

	address &= 0xffff;

	for (count = 0, done = 0; ! done && count < 100; ) {

		/* program setup command */
		flash_addr_control (card_num, address, 0x106);
		flash_data_control (card_num, 0x40, 0x105);

		/* program command */
		flash_addr_control (card_num, address, 0x106);
		flash_data_control (card_num, value, 0x105);

		udelay (10);

		/* program verify command */
		flash_addr_control (card_num, address, 0x106);
		flash_data_control (card_num, 0xc0, 0x105);

		udelay (6);

		/* read back */
		rc = xpds_read_flash_byte (card_num, address, &rval);

		if (rval == value) {
			done = 1;
		} else {
			count ++;
		}
	}
	if (! done) {
		printk (KERN_ERR "write_flash_byte (%d, %04lx, %02x) failed (rval = %02x)\n", card_num, (unsigned long) address, value, rval);
		return 6;
	}

	ddprintk ("%02x->flash[%04x]\n", value, address);

	return 0;
}

__inline__ int
xpds_verify_flash (int card_num, u8 *buffer, u8 expected_byte, int length)
{
	int	i;
	int	rc;

	for (i = 0; i < length; i ++) {
		u8	byte, ex_byte;

		rc = xpds_read_flash_byte (card_num, i, &byte);
		ex_byte = ((buffer != NULL) ? buffer[i] : expected_byte);
		if (byte != ex_byte) {
			printk (KERN_ERR "xpds_verify_flash() failed at %04x (got %02x, expected %02x)\n", i, byte, ex_byte);
			return 6;
		}
	}
	return -1;
}

__inline__ int
xpds_erase_flash (int card_num)
{
	int	i, verified = 0, done = 0, rc;
	int	flashsize;

	/* write all zeros */
	flashsize = xpds_data[card_num].is_fpga ?
		FPGA_FLASHSIZE : ASIC_FLASHSIZE;
	for (i = 0; i < flashsize; i ++) {
		rc = xpds_write_flash_byte (card_num, i, 0);
		if (rc > 0) {
			printk (KERN_ERR "%s: writing 0 to flash byte %04x during erase failed\n", xpds_devs[card_num].name, i);
			return rc;
		}
		schedule ();
	}

	rc = xpds_verify_flash (card_num, NULL, 0, flashsize);
	if (rc > 0) return rc;

	/* bulk erase 1000 times */

	for (i = 0; i < 1000; ) {
		u32	wait_until;
		int	j;

		/* erase setup */
		flash_addr_control (card_num, 0, 0x106);
		flash_data_control (card_num, 0x20, 0x105);

		/* erase */
		flash_addr_control (card_num, 0, 0x106);
		flash_data_control (card_num, 0x20, 0x105);

		/* delay 10ms */
		wait_until = jiffies + 10 * HZ / 1000;
		while (jiffies < wait_until) {
			schedule ();
		}

		/* verify that each byte is 0xff */
		for (j = verified; j < flashsize; j ++) {
			u32	val;
			u8	byte;

			/* erase verify */
			flash_addr_control (card_num, (j & 0xffff), 0x106);
			flash_data_control (card_num, 0xa0, 0x105);
			udelay (6);

			/* read data */
			flash_addr_control (card_num, (j & 0xffff), 0x106);
			flash_data_control (card_num, 0, 0x104);

			rc = xpds_read_control_register (card_num, AUX_READ_DATA, &val, AUX);
			byte = val;		
			if (rc > 0 || byte != 0xff) {
				done = 0;
				verified = j;
				i ++;
				break;
			} else {
				done = 1;
			}
		}
		/*
		printk ("i = %d, verified = %04x, done = %d\n",
			i, verified, done);
		*/

		if (done) break;
	}
	if (i >= 1000) {
		ddprintk ("xpds_erase_flash() failed\n");
		return 6;
	}

	xpds_reset_flash (card_num);
	ddprintk ("xpds_erase_flash() succeeded\n");

	rc = xpds_verify_flash (card_num, NULL, 0xff, flashsize);
	if (rc > 0) {
		printk (KERN_ERR "xpds_verify_flash () during erase failed\n");
		return rc;
	}

	return 0;
}

/*
 * IRQ types for value returned by using XPDS_MBX_WRITE_IRQTYPE 
 * with xpds_mailbox_read().
 */
typedef enum {      /* Codes for physical layer i/o */
	XPDS_SDSL_CMD_FAILED,            /* Physical layer command failed */
	XPDS_SDSL_CMD_COMPLETED,         /* Physical layer command completed successfully */
	XPDS_SDSL_CMD_TIMEOUT,           /* Physical layer command timed out */
	XPDS_SDSL_FLASH_ERASE_ERROR,     /* Flash could not be erased */
	XPDS_SDSL_FLASH_ZERO_ERROR,      /* Flash could not be zeroed out */
	XPDS_SDSL_FLASH_WRITE_ERROR,     /* Flash could not be written */
	XPDS_SDSL_AUX_MODE_PCI,          /* PCI is the aux bus master */
	XPDS_SDSL_AUX_MODE_NIC,          /* NIC is the aux bus master */
	XPDS_SDSL_AUX_MODE_BOTH,         /* Both can be aux master */
	XPDS_SDSL_RAM_ERROR,             /* RAM failed a self-test */
	XPDS_SDSL_IRQTYPE_STATE_CHANGE,  /* Physical layer has gone into an important state */
	XPDS_SDSL_STATE_BOOTED,          /* PL State: Booted */ 
	XPDS_SDSL_TESTMODE_NORMAL_OPERATION,         /* Return to normal operation */
	XPDS_SDSL_TESTMODE_EXTERNAL_ANALOG_LOOPBACK, /* Transmit then receive echo */
	XPDS_SDSL_TESTMODE_FOUR_LEVEL_SCR,           /* Transmit a continuous stream of 4 level scrambled ones */
	XPDS_SDSL_TESTMODE_TWO_LEVEL_SCR,            /* Transmit a continuous stream of 2 level scrambled ones */
	XPDS_SDSL_TESTMODE_INTERNAL_ANALOG_LOOPBACK, /* Loopback bypassing pins */  
	XPDS_SDSL_TESTMODE_FORCE_LINKUP,             /* Force link up so driver can start */                               
	XPDS_SDSL_MAX                                /* Not a code - Used for bounds checking */
} xpds_sdsl_codes_t;

typedef enum { /* Maximum 128 types total */
                                       /* BEGIN PCI MAILBOX */
    XPDS_MBX_WRITE_BOOTDATA,             /* Write bootdata from the mailbox */
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
    XPDS_MBX_WRITE_DEBUGPORT = 64,       /* Get the NIC supplied terminal output from the mailbox */
    XPDS_MBX_WRITE_IRQTYPE,              /* Get the type of irq being generated */
    XPDS_MBX_WRITE_EXITCODE,             /* Write the completion code */        
    XPDS_MBX_NICMASTER_MAXTYPE           /* Types below this value are for NIC mailbox only */  
} xpds_mbx_t;

/* MAILBOX BITMASKS */
#define  XPDS_MBX_PCI_HANDSHAKE_BIT         0x00000080
#define  XPDS_MBX_NIC_HANDSHAKE_BIT         0x00800000
#define  XPDS_MBX_NIC_HANDSHAKE_BIT_SHIFTED 0x00000080

typedef union {
	u8	bytes[4];
	u32	word;
} mailbox_t;

#define XPDS_MBX_PCI_FLAGS	0
#define XPDS_MBX_PCI_DATA	1
#define XPDS_MBX_NIC_FLAGS	2
#define XPDS_MBX_NIC_DATA	3

#define mailbox_busy_wait(card_num,n,bit) \
	do { \
		u32	wait_until_mbw = jiffies + (n) * HZ / 1000; \
		for (;;) { \
			u32	val_mbw; \
			xpds_read_control_register_quiet (card_num, AUX_MAILBOX, &val_mbw, AUX); \
			if (val_mbw & (bit)) break; \
			schedule_if_no_interrupt (card_num); \
			if (jiffies > wait_until_mbw) { \
				printk (KERN_ERR "(val_mbw=%x,bit=%x)", val_mbw, bit);\
				return 5; \
			} \
		} \
	} while (0)

#define mailbox_busy_wait_not(card_num,n,bit) \
	do { \
		u32	wait_until_mbw = jiffies + (n) * HZ / 1000; \
		for (;;) { \
			u32	val_mbw; \
			xpds_read_control_register_quiet (card_num, AUX_MAILBOX, &val_mbw, AUX); \
			if (! (val_mbw & (bit))) break; \
			schedule_if_no_interrupt (card_num); \
			if (jiffies > wait_until_mbw) { \
				printk (KERN_ERR "(val_mbw=%x,!bit=%x)", val_mbw, bit);\
				return 5; \
			} \
		} \
	} while (0)


static mailbox_t	*mailbox = NULL;
/*
 * Timeouts in milliseconds.
 * Need a longer timeout for use with Copper Mountain when getting the
 * speed assigned by the DSLAM.
 */
#define MAILBOX_WRITE_TIMEOUT	(xpds_data[card_num].sdsl_speed == 0 ? 1100 :50)
#define MAILBOX_READ_TIMEOUT	(xpds_data[card_num].sdsl_speed == 0 ? 1100 :50)

__inline__ int
xpds_mailbox_write (int card_num, xpds_mbx_t transfer_type, u8 byte)
{
	if (transfer_type < XPDS_MBX_PCIMASTER_MAXTYPE) {
		/* PCI mailbox flag */

		if (mailbox == NULL || card_num >= xpds_max_cards) {
			printk (KERN_ERR "mailbox problem:  mailbox = %p, xpds_max_cards = %d, card_num = %d\n", mailbox, xpds_max_cards, card_num);
			return 2;
		}

		/* write data into data mailbox */
		mailbox[card_num].bytes[XPDS_MBX_PCI_DATA] = byte;
		xpds_write_control_register (card_num, AUX_MAILBOX, mailbox[card_num].word, AUX);

		/* change transfer type and set handshake bit */
		mailbox[card_num].bytes[XPDS_MBX_PCI_FLAGS] =
			transfer_type | XPDS_MBX_PCI_HANDSHAKE_BIT;
		xpds_write_control_register (card_num, AUX_MAILBOX, mailbox[card_num].word, AUX);

		/* wait until data has been received */
		mailbox_busy_wait (card_num, MAILBOX_WRITE_TIMEOUT, XPDS_MBX_PCI_HANDSHAKE_BIT);

		/* clear handshake bit */
		mailbox[card_num].bytes[XPDS_MBX_PCI_FLAGS] &=
			~ XPDS_MBX_PCI_HANDSHAKE_BIT;
		xpds_write_control_register (card_num, AUX_MAILBOX, mailbox[card_num].word, AUX);

		/* wait until data has been received */
		mailbox_busy_wait_not (card_num, MAILBOX_WRITE_TIMEOUT, XPDS_MBX_PCI_HANDSHAKE_BIT);

		return 0;
	} else if (transfer_type < XPDS_MBX_NICMASTER_MAXTYPE) {
		/* 8032 mailbox flag */
		return 2;
	} else {
		return 2;
	}
}

typedef struct {
	u8	*bytes;
	u32	size;
	u32	head;
	u32	tail;
} queue_t;

static queue_t	*debug_queue = NULL;
static queue_t	*irq_queue = NULL;
static queue_t	*exitcode_queue = NULL;

static int
expand_queue (queue_t *q)
{
	u32	new_size;
	u8	*new_bytes;

	if (q->size == 0) {
		new_size = 100;
	} else {
		new_size = q->size * 2;
	}
	dprintk (KERN_DEBUG "expanding queue %p from %d bytes to %d bytes\n",
		q, q->size, new_size);
	new_bytes = kmalloc (new_size, GFP_ATOMIC);
	if (new_bytes == NULL) {
		printk (KERN_ERR "failed to increase size of queue to %d\n", new_size);
		return 1;
	}
	if (q->size > 0) {
		memcpy (new_bytes, q->bytes + q->head, q->size - q->head);
		memcpy (new_bytes + q->size - q->head, q->bytes, q->head);
	}
	if (q->bytes != NULL) {
		dprintk (KERN_DEBUG "freeing q->bytes (%p)\n", q->bytes);
		kfree (q->bytes);
	}
	q->head = 0;
	q->tail = q->size;
	q->size = new_size;
	q->bytes = new_bytes;
	return 0;
}

__inline__ static int
add_to_queue (queue_t *q, u8 byte)
{
	if (q == NULL) return 1;
	if (q->size == 0 || (q->tail + 1) % q->size == q->head) {
		int	rc;
		rc = expand_queue (q);
		if (rc) return rc;
	}
	dprintk (KERN_DEBUG "adding %02x in queue %p position %d\n",
		byte, q, q->tail);
	q->bytes[q->tail] = byte;
	q->tail ++;
	q->tail %= q->size;
	return 0;
}

__inline__ static int
get_from_queue (queue_t *q, u8 *bytep)
{
	if (q == NULL) return 1;
	if (q->head == q->tail) return 1;
	dprintk (KERN_DEBUG "getting %02x from queue %p position %d\n",
		q->bytes[q->head], q, q->head);
	*bytep = q->bytes[q->head];
	q->head ++;
	q->head %= q->size;
	return 0;
}

__inline__ int
xpds_mailbox_read (int card_num, xpds_mbx_t transfer_type, u8 *bytep)
{
	u32	val;

	/* check to see if NIC is the master? */

	if (transfer_type < XPDS_MBX_PCIMASTER_MAXTYPE) {
		/* change transfer type and set handshake bit */
		mailbox[card_num].bytes[XPDS_MBX_PCI_FLAGS] =
			transfer_type | XPDS_MBX_PCI_HANDSHAKE_BIT;
		xpds_write_control_register (card_num, AUX_MAILBOX, mailbox[card_num].word, AUX);

		/* wait until data has been received */
		mailbox_busy_wait (card_num, MAILBOX_READ_TIMEOUT, XPDS_MBX_PCI_HANDSHAKE_BIT);

		/* get data */
		xpds_read_control_register (card_num, AUX_MAILBOX, &val, AUX);
		*bytep = (val & 0xff00) >> 8;

		/* clear handshake bit */
		mailbox[card_num].bytes[XPDS_MBX_PCI_FLAGS] &=
			~ XPDS_MBX_PCI_HANDSHAKE_BIT;
		xpds_write_control_register (card_num, AUX_MAILBOX, mailbox[card_num].word, AUX);

		/* wait until handshake clear is received */
		mailbox_busy_wait_not (card_num, MAILBOX_READ_TIMEOUT, XPDS_MBX_PCI_HANDSHAKE_BIT);

		return 0;
	} else if (transfer_type < XPDS_MBX_NICMASTER_MAXTYPE) {
		queue_t		*q;
		int		rc;
		u32		wait_until;

		/* first check the appropriate queue */
		switch (transfer_type) {
			case XPDS_MBX_WRITE_DEBUGPORT:
				q = &(debug_queue[card_num]); break;
			case XPDS_MBX_WRITE_IRQTYPE:
				q = &(irq_queue[card_num]); break;
			case XPDS_MBX_WRITE_EXITCODE:
				q = &(exitcode_queue[card_num]); break;
			default:
				return 2;
		}
		rc = get_from_queue (q, bytep);
		dprintk (KERN_DEBUG "%s: got %02x from queue (%p/%02x) (rc = %d)\n", xpds_devs[card_num].name, *bytep, q, transfer_type, rc);
		if (rc == 0) return 0;

		/* not in queue, get from NIC */
		wait_until = jiffies + (MAILBOX_READ_TIMEOUT * 2) * HZ / 1000;
		for (;;) {
			u32		val_nic;
			queue_t		*q_nic;
			int		done = 0;

			xpds_read_control_register_quiet (card_num, AUX_MAILBOX, &val_nic, AUX);
			if (val_nic & XPDS_MBX_NIC_HANDSHAKE_BIT) {
				xpds_mbx_t	nic_transfer_type;
				u8	byte;

				nic_transfer_type = (val_nic & 0x007f0000) >> 16;

				switch (nic_transfer_type) {
					case XPDS_MBX_WRITE_DEBUGPORT:
						q_nic = &(debug_queue[card_num]); break;
					case XPDS_MBX_WRITE_IRQTYPE:
						q_nic = &(irq_queue[card_num]); break;
					case XPDS_MBX_WRITE_EXITCODE:
						q_nic = &(exitcode_queue[card_num]); break;
					default:
						q_nic = &(debug_queue[card_num]); break;
				}
				byte = (val_nic & 0xff000000) >> 24;
				dprintk (KERN_DEBUG "%s: adding %02x to queue (%p/%02x)\n", xpds_devs[card_num].name, byte, q_nic, nic_transfer_type);
				add_to_queue (q_nic, byte);

				mailbox[card_num].bytes[XPDS_MBX_NIC_FLAGS] = 
					transfer_type |
					XPDS_MBX_NIC_HANDSHAKE_BIT_SHIFTED;
				xpds_write_control_register (card_num, AUX_MAILBOX, mailbox[card_num].word, AUX);

				mailbox_busy_wait_not (card_num, MAILBOX_READ_TIMEOUT, XPDS_MBX_NIC_HANDSHAKE_BIT);
				mailbox[card_num].bytes[XPDS_MBX_NIC_FLAGS] &=
					~ XPDS_MBX_NIC_HANDSHAKE_BIT_SHIFTED;
				xpds_write_control_register (card_num, AUX_MAILBOX, mailbox[card_num].word, AUX);

				schedule_if_no_interrupt (card_num);

				if (nic_transfer_type == transfer_type) {
					done = 1;
				}
			}
			if (done || jiffies > wait_until) break;
		}
		rc = get_from_queue (q, bytep);
		dprintk (KERN_DEBUG "%s: got %02x from queue (%p/%02x) (rc = %d)\n", xpds_devs[card_num].name, *bytep, q, transfer_type, rc);
		if (rc == 0) return 0;

		return 1;
	} else {
		return 2;
	}
}

int
xpds_get_sdsl_exit_code (int card_num, u8 *bytep)
{
	int	rc;

	rc = xpds_mailbox_read (card_num, XPDS_MBX_WRITE_EXITCODE, bytep);
	return rc;
}

/*
 * This gets the mode in the flash memory, but the speed section of
 * the mode may be a default speed instead of the actual value if the
 * speed section is invalid (the SDSL card will operate at the default
 * speed in this case).
 */
int
xpds_get_sdsl_mode (int card_num, u32 *val)
{
	int	i, rc, rval = 0;
	u8	byte = 0;

	*val = 0;
	for (i = 0; i < 4; i ++) {
		rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_PHYSMODE, &byte);
		if (rc > 0) rval = 1;
		*val |= byte << ((3 - i) * 8);
	}
	return rval;
}

/*
 * This gets the actual mode in the flash memory.
 */
int
xpds_get_flash_sdsl_mode (int card_num, u32 *val)
{
	int	i, rc, rval = 0;
	u8	byte = 0;

	*val = 0;
	for (i = 0; i < 4; i ++) {
		rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_PHYSMODE_IN_FLASH, &byte);
		if (rc > 0) rval = 1;
		*val |= byte << ((3 - i) * 8);
	}
	return rval;
}

int
xpds_set_sdsl_mode (int card_num, u32 val)
{
	int	i, rc, rval = 0;
	u8	byte;

	for (i = 0; i < 4; i ++) {
		byte = (val >> ((3 - i) * 8)) & 0xff;
		rc = xpds_mailbox_write (card_num, XPDS_MBX_WRITE_PHYSMODE, byte);
		if (rc > 0) rval = 1;
	}
	return rval;
}

int
xpds_reset_sdsl (int card_num)
{
	u32	wait_until, val;

	/* reset 8032 and Rockwell */
	dprintk (KERN_DEBUG "%s: resetting the 8032\n", xpds_devs[card_num].name);
	xpds_read_control_register (card_num, XPDS_MCR_TXCI, &val, MAIN);
	xpds_write_control_register (card_num, XPDS_MCR_TXCI, val & 0x6f, MAIN);
	xpds_read_control_register (card_num, AUX_CONTROL, &val, AUX);
	xpds_write_control_register (card_num, AUX_CONTROL, val & ~0x80, AUX);
	xpds_read_control_register (card_num, AUX_CONTROL, &val, AUX);
	xpds_write_control_register (card_num, AUX_CONTROL, val | 0x100, AUX);

	wait_until = jiffies + 1 * HZ;
	while (jiffies < wait_until) {
		schedule ();
	}
	return 0;
}

int
xpds_start_sdsl (int card_num)
{
	u32	wait_until, val;

	/* unreset 8032 and Rockwell */
	dprintk (KERN_DEBUG "%s: unresetting the 8032\n", xpds_devs[card_num].name);

	xpds_read_control_register (card_num, AUX_CONTROL, &val, AUX);
	xpds_write_control_register (card_num, AUX_CONTROL, val & ~0x100, AUX);

	xpds_read_control_register (card_num, XPDS_MCR_TXCI, &val, MAIN);
	xpds_write_control_register (card_num, XPDS_MCR_TXCI, val | 0x90, MAIN);

	xpds_read_control_register (card_num, AUX_CONTROL, &val, AUX);
	xpds_write_control_register (card_num, AUX_CONTROL, val | 0x80, AUX);

	wait_until = jiffies + HZ * 1;
	while (jiffies < wait_until) {
		schedule ();
	}

	return 0;
}

typedef struct {
	int	size;
	u8	*image;
} xpds_flash_image_t;

int
xpds_install_flash_image (int card_num, xpds_flash_image_t *fd)
{
	int			i, rc;
	xpds_flash_image_t	kfd;
	u8			*image;
	int			flashsize;

	flashsize = xpds_data[card_num].is_fpga ?
		FPGA_FLASHSIZE : ASIC_FLASHSIZE;

	printk (KERN_NOTICE "%s: resetting SDSL\n", xpds_devs[card_num].name);
	rc = xpds_reset_sdsl (card_num);
	if (rc > 0) {
		printk (KERN_ERR "%s: SDSL reset failed\n", xpds_devs[card_num].name);
		return rc;
	}
	printk (KERN_NOTICE "%s: resetting flash\n", xpds_devs[card_num].name);
	rc = xpds_reset_flash (card_num);
	if (rc > 0) {
		printk (KERN_ERR "%s: flash reset failed\n", xpds_devs[card_num].name);
		return rc;
	}

	printk (KERN_NOTICE "%s: erasing flash\n", xpds_devs[card_num].name);
	rc = xpds_erase_flash (card_num);
	if (rc > 0) {
		printk (KERN_ERR "%s: flash erase failed\n", xpds_devs[card_num].name);
		return rc;
	}

	printk (KERN_NOTICE "%s: writing flash image\n", xpds_devs[card_num].name);
	copy_from_user (&kfd, fd, sizeof (kfd));
	if (kfd.size > flashsize) kfd.size = flashsize;
	image = kmalloc (kfd.size, GFP_KERNEL);
	if (image == NULL) return 1;
	copy_from_user (image, kfd.image, kfd.size);

	for (i = 0; i < kfd.size; i ++) {
		rc = xpds_write_flash_byte (card_num, i, image[i]);
		if (rc > 0) {
			printk (KERN_ERR "%s: flash write failed, address %04x, data %02x\n", xpds_devs[card_num].name, i, image[i]);
			kfree (image);
			return rc;
		}
		schedule ();
	}
	printk (KERN_NOTICE "%s: verifying flash image\n", xpds_devs[card_num].name);
	rc = xpds_verify_flash (card_num, image, 0, kfd.size);
	if (rc > 0) {
		printk (KERN_ERR "%s: flash verify failed\n", xpds_devs[card_num].name);
		kfree (image);
		return rc;
	}

	xpds_reset_sdsl (card_num);
	xpds_start_sdsl (card_num);

	kfree (image);
	return 0;
}

int
xpds_sdsl_loopback (int card_num)
{
	int	rc;

	dprintk (KERN_DEBUG "%s: setting SDSL loopback mode\n", xpds_devs[card_num].name);
	rc = xpds_mailbox_write (card_num, XPDS_MBX_WRITE_TESTMODE, 0x10);
	return rc;
}

/*
 * Set data like hardware version, serial number, etc.
 * Only one exit code is generated for the whole thing, except for
 * bytes which were unwritable (which cause additional exit codes).
 */
static int
xpds_set_sdsl_serial_data (int card_num, xpds_mbx_t transfer_type, u8 *data,
	int size)
{
	int			i, rc, rval = 0;
	u8			ec;

	for (i = 0; i < size; i ++) {
		rc = xpds_mailbox_write (card_num, transfer_type, data[i]);
		if (rc > 0) {
			rval = 1;
			xpds_get_sdsl_exit_code (card_num, &ec);
		}
	}
	rc = xpds_get_sdsl_exit_code (card_num, &ec);
	if (rc > 0 || ec != XPDS_SDSL_CMD_COMPLETED) rval = 1;
	return rval;
}

int
xpds_set_sdsl_info (int card_num)
{
	xpds_serial_data_t	*sdata;
	int	rc, rval = 0;

	sdata = &(xpds_data[card_num].serial_data);

	rc = xpds_set_sdsl_serial_data (card_num, XPDS_MBX_WRITE_HWVER,
		sdata->hardware_version, sizeof (sdata->hardware_version));
	if (rc > 0) {
		printk (KERN_ERR "%s: failed to write hardware version\n", xpds_devs[card_num].name);
		rval = 1;
	}

#if REWRITE_FWVER
	rc = xpds_set_sdsl_serial_data (card_num, XPDS_MBX_WRITE_FWVER,
		sdata->firmware_version, sizeof (sdata->firmware_version));
	if (rc > 0) {
		printk (KERN_ERR "%s: failed to write firmware version\n", xpds_devs[card_num].name);
		rval = 1;
	}
#else
	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_FWVER,
		&(sdata->firmware_version[0]));
	if (rc) rval = 1;

	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_FWVER,
		&(sdata->firmware_version[1]));
	if (rc) rval = 1;

	if (rval) {
		printk (KERN_ERR "%s: failed to read new firmware version\n", xpds_devs[card_num].name);
		sdata->firmware_version[0] = 0;
		sdata->firmware_version[1] = 0;
	}

	printk (KERN_INFO "%s: new firmware version is %d.%d\n",
		xpds_devs[card_num].name, sdata->firmware_version[0],
		sdata->firmware_version[1]);
#endif

	rc = xpds_set_sdsl_serial_data (card_num, XPDS_MBX_WRITE_MFGDATE,
		sdata->mfg_date, sizeof (sdata->mfg_date));
	if (rc > 0) {
		printk (KERN_ERR "%s: failed to write manufacturing date\n", xpds_devs[card_num].name);
		rval = 1;
	}

	rc = xpds_set_sdsl_serial_data (card_num, XPDS_MBX_WRITE_MACADDR,
		sdata->mac_address, sizeof (sdata->mac_address));
	if (rc > 0) {
		printk (KERN_ERR "%s: failed to write MAC address\n", xpds_devs[card_num].name);
		rval = 1;
	}

	rc = xpds_set_sdsl_serial_data (card_num, XPDS_MBX_WRITE_SERIALNUMBER,
		sdata->serial_number, sizeof (sdata->serial_number));
	if (rc > 0) {
		printk (KERN_ERR "%s: failed to write serial number\n", xpds_devs[card_num].name);
		rval = 1;
	}

	return rval;
}

int
xpds_sdsl_allocate (void)
{
	if (debug_queue == NULL) {
		debug_queue = kmalloc (xpds_max_cards * sizeof (*debug_queue), GFP_KERNEL);
		if (debug_queue == NULL) return -ENOMEM;
		memset (debug_queue, 0, xpds_max_cards * sizeof (*debug_queue));
	}
	if (irq_queue == NULL) {
		irq_queue = kmalloc (xpds_max_cards * sizeof (*irq_queue), GFP_KERNEL);
		if (irq_queue == NULL) return -ENOMEM;
		memset (irq_queue, 0, xpds_max_cards * sizeof (*irq_queue));
	}
	if (exitcode_queue == NULL) {
		exitcode_queue = kmalloc (xpds_max_cards * sizeof (*exitcode_queue), GFP_KERNEL);
		if (exitcode_queue == NULL) return -ENOMEM;
		memset (exitcode_queue, 0, xpds_max_cards * sizeof (*exitcode_queue));
	}
	if (mailbox == NULL) {
		dprintk (KERN_DEBUG "allocating mailbox (%d * %d)\n",
			xpds_max_cards, sizeof (*mailbox));
		mailbox = kmalloc (xpds_max_cards * sizeof (*mailbox), GFP_KERNEL);
		if (mailbox == NULL) return -ENOMEM;
		memset (mailbox, 0, xpds_max_cards * sizeof (*mailbox));
	}
	return 0;
}

int
xpds_sdsl_get_state (int card_num, u8 *state)
{
	return xpds_mailbox_read (card_num, XPDS_MBX_READ_STAGE, state);
}

int
xpds_sdsl_cleanup (void)
{
	int	i;

	for (i = 0; i < xpds_max_cards; i ++) {
		if (debug_queue != NULL && debug_queue[i].bytes != NULL) kfree (debug_queue[i].bytes);
		if (irq_queue != NULL && irq_queue[i].bytes != NULL) kfree (irq_queue[i].bytes);
		if (exitcode_queue != NULL && exitcode_queue[i].bytes != NULL) kfree (exitcode_queue[i].bytes);
	}
	if (debug_queue != NULL) kfree (debug_queue);
	if (irq_queue != NULL) kfree (irq_queue);
	if (exitcode_queue != NULL) kfree (exitcode_queue);
	if (mailbox != NULL) kfree (mailbox);
	return 0;
}
