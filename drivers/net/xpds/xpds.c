/*
 * Copyright 1998, 1999, 2000 Xpeed, Inc.
 * xpds.c, $Revision: 1.33 $
 * License to copy and distribute is GNU General Public License, version 2.
 */
#ifndef VERSION_STRING
#define VERSION_STRING	"release-20001009k"
#endif

#define LT_IND_AI	0xc
#define NT_IND_AI	0xc

#ifndef __KERNEL__
#define __KERNEL__ 1
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/ctype.h>

#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/time.h>

#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>

#include <linux/tqueue.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#if ! defined (CONFIG_PCI)
#error "CONFIG_PCI is not defined"
#endif

#include "xpds-softnet.h"
#include "xpds-reg.h"
#include "xpds-fsm.h"
#include <linux/xpds-ioctl.h>
#include "xpds-sdsl.h"
#include "xpds-encap-fr.h"
#include "xpds.h"

MODULE_AUTHOR("Timothy J. Lee <linux@xpeed.com>");
MODULE_DESCRIPTION("Xpeed 200 IDSL and 300 SDSL NIC driver (frame relay)");

/*
 * Debugging stuff.
 */
#define DEBUG_MAIN	1
#define DEBUG_FSM	2
#define DEBUG_PACKET	64
#define DEBUG_DETAILED	128
int	xpds_debug_level = 0 /* DEBUG_MAIN | DEBUG_FSM */;
MODULE_PARM(xpds_debug_level, "i");

#ifdef DEBUG
#define	dprintk		if (xpds_debug_level & DEBUG_MAIN) printk
#define	dpprintk	if ((xpds_debug_level & (DEBUG_MAIN | DEBUG_PACKET)) == (DEBUG_MAIN | DEBUG_PACKET)) printk
#define	ddprintk	if ((xpds_debug_level & (DEBUG_MAIN | DEBUG_DETAILED)) == (DEBUG_MAIN | DEBUG_DETAILED)) printk
#else
#define	dprintk		if (0) printk
#define	dpprintk	if (0) printk
#define	ddprintk	if (0) printk
#endif

#define nrprintk	if (net_ratelimit()) printk

/*
 * If we are loaded module for flashing, ignore error in detection.
 */
int xpds_load_for_flash = 0;
MODULE_PARM(xpds_load_for_flash, "i");

/*
 * For loopback testing only.
 */
int xpds_asic_loopback = 0;
MODULE_PARM(xpds_asic_loopback, "i");
int xpds_external_loopback = 0;
MODULE_PARM(xpds_external_loopback, "i");

/* some motherboards have 6 PCI slots... */
#define XPDS_DEFAULT_MAX	6
int xpds_max_cards = XPDS_DEFAULT_MAX;
MODULE_PARM(xpds_max_cards, "i");

/* time to wait for SDSL physical layer to come up */
#define XPDS_SDSL_TIMEOUT	300
int xpds_sdsl_timeout = XPDS_SDSL_TIMEOUT;
MODULE_PARM(xpds_sdsl_timeout, "i");

int xpds_default_dlci = XPDS_DEFAULT_DLCI;
MODULE_PARM(xpds_default_dlci, "i");

int xpds_default_dlci_cr = XPDS_DEFAULT_DLCI_CR;
MODULE_PARM(xpds_default_dlci_cr, "i");

int xpds_default_dlci_lmi = XPDS_DLCI_LMI_LT_OR_NT;
MODULE_PARM(xpds_default_dlci_lmi, "i");

int xpds_default_bridged = 0;
MODULE_PARM(xpds_default_bridged, "i");

static char *xpds_dev_name = "dsl";
MODULE_PARM(xpds_dev_name, "s");

/*
 * At rates below 400 Kbps, the SDSL TX DMA does not work on some
 * computers.
 */
#define LOW_BIT_RATE		404
#define TX_DMA_LOW_RATE_BUG(card_num) \
	(xpds_data[card_num].is_sdsl && \
	xpds_data[card_num].has_tx_dma_low_rate_bug && \
	xpds_data[card_num].sdsl_speed < LOW_BIT_RATE)
#define RX_DMA_LOW_RATE_BUG(card_num) \
	(xpds_data[card_num].is_sdsl && \
	xpds_data[card_num].has_rx_dma_low_rate_bug && \
	xpds_data[card_num].sdsl_speed < LOW_BIT_RATE)

#define PCI_VENDOR_ID_XPDS      0x14b3
#define PCI_DEVICE_ID_XPDS_1    0x0000

#define ALLOW_OLD_PCI_VENDOR_ID	0
#define PCI_VENDOR_ID_XPDS_OLD	0xeeee
 
/*
 * Set xpds_mode by insmod.
 */
#define XPDS_MODE_B1		1
#define XPDS_MODE_B2		2
#define XPDS_MODE_D		4
#define XPDS_MODE_DEFAULT	7

int	xpds_mode = XPDS_MODE_DEFAULT;
MODULE_PARM(xpds_mode, "i");

/*
 * Guard time for the state machines.
 */
#define DEFAULT_GUARD_TIME	15
int	xpds_guard_time = DEFAULT_GUARD_TIME;
MODULE_PARM(xpds_guard_time, "i");

#define RXTX_BUFFER_SIZE        2048

/*
 * Maximum packet length that is put into a hardware register.
 * Packets received by the hardware are dropped if they are
 * equal to or greater in length.
 */
#define DEFAULT_MAX_PACKET_LENGTH	0x600
u16	xpds_max_packet_length = DEFAULT_MAX_PACKET_LENGTH;
MODULE_PARM(xpds_max_packet_length, "i");

#define RXTX_CONTROL__SWGO		0x80000000
#define RXTX_CONTROL__HWGO		0x40000000
#define RXTX_CONTROL__NEXT_VALID	0x20000000
#define RXTX_CONTROL__PACKET_TAG_OFFSET	16
#define RXTX_CONTROL__PACKET_TAG_MASK	0xf
#define RXTX_CONTROL__PACKET_LENGTH_MASK	0xffff

#define NUM_MAIN_CONTROL_REGISTERS      16
#define NUM_FIFO_CONTROL_REGISTERS      16
#define NUM_DMA_CONTROL_REGISTERS       48
#define NUM_FIFO_DATA_REGISTERS		32
#define NUM_AUX_REGISTERS		4

#define NUM_FIFO			4
#define FIFO_SIZE			(NUM_FIFO_DATA_REGISTERS / NUM_FIFO)

xpds_data_t		*xpds_data = NULL;
static int		num_xpds_found = 0;

#define XPDS_MAIN    		0
#define XPDS_RX_FIFO		2
#define XPDS_TX_FIFO		3
#define XPDS_RX_DMA		4
#define XPDS_TX_DMA  		5
#define XPDS_RX_FIFO_DATA	6
#define XPDS_TX_FIFO_DATA	7
#define XPDS_AUX		8

#define MAIN    	XPDS_MAIN
#define RX_FIFO		XPDS_RX_FIFO
#define TX_FIFO		XPDS_TX_FIFO
#define RX_DMA		XPDS_RX_DMA
#define TX_DMA  	XPDS_TX_DMA
#define RX_FIFO_DATA	XPDS_RX_FIFO_DATA
#define TX_FIFO_DATA	XPDS_TX_FIFO_DATA
#define AUX		XPDS_AUX

#define NAME_SIZE	16

#define NUM_DESC	14

static int xpds_init (struct net_device *dev);

static char *xpds_names = NULL;
struct net_device *xpds_devs = NULL;

__inline__ static int
xpds_read_control_register_internal (int xpds_num, int register_number,
	u32 *value, int which, int verbose)
{
	register_number >>= 2;

	if (verbose) dprintk (KERN_DEBUG "%s: ", xpds_devs[xpds_num].name);
	switch (which) {
		case MAIN:
			if (register_number > NUM_MAIN_CONTROL_REGISTERS ||
				xpds_data[xpds_num].main_control_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].main_control_registers[register_number];
			if (verbose) {
				dprintk ("main_ctl[%02x]->%08x ",
					register_number, *value);
			}
			break;
		case RX_FIFO:
			if (register_number > NUM_FIFO_CONTROL_REGISTERS ||
				xpds_data[xpds_num].rx_fifo_control_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].rx_fifo_control_registers[register_number];
			if (verbose) dprintk ("rx_fifo_ctl[%02x]->%08x ",
				register_number, *value);
			break;
		case TX_FIFO:
			if (register_number > NUM_FIFO_CONTROL_REGISTERS ||
				xpds_data[xpds_num].tx_fifo_control_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].tx_fifo_control_registers[register_number];
			if (verbose) dprintk ("tx_fifo_ctl[%02x]->%08x ",
				register_number, *value);
			break;
		case RX_DMA:
			if (register_number > NUM_DMA_CONTROL_REGISTERS ||
				xpds_data[xpds_num].rx_dma_control_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].rx_dma_control_registers[register_number];
			if (verbose) dprintk ("rx_dma_ctl[%02x]->%08x ",
				register_number, *value);
			break;
		case TX_DMA:
			if (register_number > NUM_DMA_CONTROL_REGISTERS ||
				xpds_data[xpds_num].tx_dma_control_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].tx_dma_control_registers[register_number];
			if (verbose) dprintk ("tx_dma_ctl[%02x]->%08x ",
				register_number, *value);
			break;
		case RX_FIFO_DATA:
			if (register_number > NUM_FIFO_DATA_REGISTERS ||
				xpds_data[xpds_num].rx_fifo_data_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].rx_fifo_data_registers[register_number];
			if (verbose) dprintk ("rx_fifo_data[%02x]->%08x ",
				register_number, *value);
			break;
		case TX_FIFO_DATA:
			if (register_number > NUM_FIFO_DATA_REGISTERS ||
				xpds_data[xpds_num].tx_fifo_data_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].tx_fifo_data_registers[register_number];
			if (verbose) dprintk ("tx_fifo_data[%02x]->%08x ",
				register_number, *value);
			break;
		case AUX:
			if (register_number > NUM_AUX_REGISTERS ||
				xpds_data[xpds_num].aux_registers == NULL) {
				return 4;
			}
			*value = xpds_data[xpds_num].aux_registers[register_number];
			if (verbose) dprintk ("aux[%02x]->%08x ",
				register_number, *value);
			break;
		default:
			if (verbose) dprintk ("\n");
			return 4;
	}
	if (verbose) dprintk ("\n");
	return 0;
}

__inline__ int
xpds_read_control_register (int xpds_num, int register_number,
	u32 *value, int which)
{
	return xpds_read_control_register_internal (xpds_num, register_number,
		value, which, 0);
}

__inline__ int
xpds_read_control_register_verbose (int xpds_num, int register_number,
	u32 *value, int which)
{
	return xpds_read_control_register_internal (xpds_num, register_number,
		value, which, 1);
}

__inline__ int
xpds_read_control_register_quiet (int xpds_num, int register_number,
	u32 *value, int which)
{
	return xpds_read_control_register_internal (xpds_num, register_number,
		value, which, 0);
}

__inline__ static int
xpds_write_control_register_internal (int xpds_num, int register_number,
	u32 value, int which, int verbose)
{
	register_number >>= 2;

	if (verbose) dprintk (KERN_DEBUG "%s: ", xpds_devs[xpds_num].name);

	switch (which) {
		case MAIN:
			if (register_number > NUM_MAIN_CONTROL_REGISTERS) {
				return 4;
			}
			xpds_data[xpds_num].main_control_registers[register_number] = value;
			if (verbose) {
				dprintk ("%08x->main_ctl[%02x] ",
					value, register_number);
			}
			break;
		case RX_FIFO:
			if (register_number > NUM_FIFO_CONTROL_REGISTERS) {
				return 4;
			}
			xpds_data[xpds_num].rx_fifo_control_registers[register_number] = value;
			if (verbose) dprintk ("%08x->rx_fifo_ctl[%02x] ",
				value, register_number);
			break;
		case TX_FIFO:
			if (register_number > NUM_FIFO_CONTROL_REGISTERS) {
				return 4;
			}
			xpds_data[xpds_num].tx_fifo_control_registers[register_number] = value;
			if (verbose) dprintk ("%08x->tx_fifo_ctl[%02x] ",
				value, register_number);
			break;
		case RX_DMA:
			if (register_number > NUM_DMA_CONTROL_REGISTERS) {
				return 4;
			}
			xpds_data[xpds_num].rx_dma_control_registers[register_number] = value;
			if (verbose) dprintk ("%08x->rx_dma_ctl[%02x] ",
				value, register_number);
			break;
		case TX_DMA:
			if (register_number > NUM_DMA_CONTROL_REGISTERS) {
				return 4;
			}
			xpds_data[xpds_num].tx_dma_control_registers[register_number] = value;
			if (verbose) dprintk ("%08x->tx_dma_ctl[%02x] ",
				value, register_number);
			break;
		case RX_FIFO_DATA:
			if (register_number > NUM_FIFO_DATA_REGISTERS) {
				return 4;
			}
			xpds_data[xpds_num].rx_fifo_data_registers[register_number] = value;
			if (verbose) dprintk ("%08x->rx_fifo_data[%02x] ",
				value, register_number);
			break;
		case TX_FIFO_DATA:
			if (register_number > NUM_FIFO_DATA_REGISTERS) {
				if (verbose) dprintk ("\n");
				return 4;
			}
			xpds_data[xpds_num].tx_fifo_data_registers[register_number] = value;
			if (verbose) dprintk ("%08x->tx_fifo_data[%02x] ",
				value, register_number);
			break;
		case AUX:
			if (register_number > NUM_AUX_REGISTERS) {
				if (verbose) dprintk ("\n");
				return 4;
			}
			xpds_data[xpds_num].aux_registers[register_number] = value;
			if (verbose) dprintk ("%08x->aux[%02x] ",
				value, register_number);
			break;
		default:
			if (verbose) dprintk ("\n");
			return 4;
	}
	if (verbose) dprintk ("\n");
	return 0;
}

__inline__ int
xpds_write_control_register (int xpds_num, int register_number,
	u32 value, int which)
{
	return xpds_write_control_register_internal (xpds_num, register_number,
		value, which, 0);
}

__inline__ int
xpds_write_control_register_verbose (int xpds_num, int register_number,
	u32 value, int which)
{
	return xpds_write_control_register_internal (xpds_num, register_number,
		value, which, 1);
}

__inline__ int
xpds_write_control_register_quiet (int xpds_num, int register_number,
	u32 value, int which)
{
	return xpds_write_control_register_internal (xpds_num, register_number,
		value, which, 0);
}

/*
 * Return 1 if the hardware version is >= major.minor, unless
 * the hardware version is the uninitialized 0xff.0xff value,
 * which is assumed to be the lowest revision.  Return 0 otherwise.
 */
static int
xpds_is_hardware_version (int card_num, int major, int minor)
{
	if (xpds_data[card_num].serial_data.hardware_version[0] == 0xff &&
		xpds_data[card_num].serial_data.hardware_version[1] == 0xff) {
		return 0;
	}
	if (xpds_data[card_num].serial_data.hardware_version[0] > major) {
		return 1;
	}
	if (xpds_data[card_num].serial_data.hardware_version[0] == major &&
		xpds_data[card_num].serial_data.hardware_version[1] >= minor) {
		return 1;
	}
	return 0;
}

static int
xpds_init_descriptors (int card_num)
{
	u32	bus_addr;

	dprintk (KERN_DEBUG "xpds_init_descriptors (%d)\n", card_num);
	/*
	 * lock?
	 */

	/*
	 * Set maximum packet length
	 */
	dprintk (KERN_DEBUG "setting maximum packet length to %u (0x%x)\n",
		xpds_max_packet_length, xpds_max_packet_length);
	xpds_write_control_register (card_num, XPDS_MCR_PACKH,
		(xpds_max_packet_length >> 8) & 0xff, MAIN);
	xpds_write_control_register (card_num, XPDS_MCR_PACKL,
		xpds_max_packet_length & 0xff, MAIN);

	/*
	 * Initialize descriptor
	 */
	{
		volatile xpds_rxtx_list_t	*rx_ptr, *tx_ptr;
		int			i;

		rx_ptr = xpds_data[card_num].rx_dma_list;
		tx_ptr = xpds_data[card_num].tx_dma_list;
		for (i = 0; i < NUM_DESC; i ++) {
			rx_ptr->control =
				RXTX_CONTROL__NEXT_VALID | RXTX_CONTROL__HWGO;
			rx_ptr = rx_ptr->next;
			tx_ptr->control =
				RXTX_CONTROL__NEXT_VALID;
			tx_ptr = tx_ptr->next;
		}
	}

	/*
	 * Initialize pointers to receive and transmit buffers
	 * that were allocated on module initialization (by
	 * allocate_rxtx_buffers() ).
	 */
	bus_addr = virt_to_bus (xpds_data[card_num].rx_dma_list);
	dprintk (KERN_DEBUG "%s: writing RX DMA status address %p (bus address %08x)\n", xpds_devs[card_num].name, xpds_data[card_num].rx_dma_list, bus_addr);
	xpds_write_control_register (card_num, XPDS_DMA_STAT_ADDR,
		bus_addr, RX_DMA);
	bus_addr = virt_to_bus (xpds_data[card_num].tx_dma_list);
	dprintk (KERN_DEBUG "%s: writing TX DMA status address %p (bus address %08x)\n", xpds_devs[card_num].name, xpds_data[card_num].tx_dma_list, bus_addr);
	xpds_write_control_register (card_num, XPDS_DMA_STAT_ADDR,
		bus_addr, TX_DMA);
	xpds_data[card_num].current_rx_dma = xpds_data[card_num].rx_dma_list;
	xpds_data[card_num].current_tx_dma = xpds_data[card_num].tx_dma_list;
	xpds_data[card_num].current_hw_tx_dma = xpds_data[card_num].tx_dma_list;
	xpds_data[card_num].current_hw_rx_dma = xpds_data[card_num].rx_dma_list;

	/*
	 * release lock?
	 */

	return 0;
}

static int
xpds_dma_enable (int card_num)
{
	u32	value;

	dprintk (KERN_DEBUG "xpds_dma_enable (%d)\n", card_num);

	/*
	 * Check SDSL speed.  If speed mode had been set to 0
	 * (autonegotiation mode), then the speed needs to be
	 * read before the DMA is enabled, in order to determine
	 * DMA low rate bug is applicable.
	 */
	if (xpds_data[card_num].is_sdsl) {
		u32	waituntil = jiffies + 90 * HZ;

		do {
			u32	sdsl_mode, speed_mode;
			int	rc;

			rc = xpds_get_sdsl_mode (card_num, &sdsl_mode);
			dprintk ("%s: SDSL mode is %08x\n",
				xpds_devs[card_num].name, sdsl_mode);
			speed_mode = sdsl_mode & XPDS_SDSL_MODE__SPEED_MASK;
			dprintk ("%s: SDSL speed is %d\n",
				xpds_devs[card_num].name, speed_mode << 3);
			xpds_data[card_num].sdsl_speed = speed_mode << 3;
			schedule_if_no_interrupt (card_num);
		} while (xpds_data[card_num].sdsl_speed == 0 &&
			jiffies < waituntil);
		printk ("%s: SDSL speed is %d kbps.\n",
			xpds_devs[card_num].name,
			xpds_data[card_num].sdsl_speed);
	}

	/*
	 * Disable all interrupts
	 */
	xpds_write_control_register (card_num, XPDS_DMA_CLR_MASK,
		XPDS_DMA_MASK__DONE | XPDS_DMA_MASK__FLOW |
		XPDS_DMA_MASK__LONG | XPDS_DMA_MASK__ABORT |
		XPDS_DMA_MASK__CRC, RX_DMA);
	xpds_write_control_register (card_num, XPDS_DMA_CLR_MASK,
		XPDS_DMA_MASK__DONE | XPDS_DMA_MASK__FLOW |
		XPDS_DMA_MASK__LONG | XPDS_DMA_MASK__ABORT |
		XPDS_DMA_MASK__CRC, TX_DMA);
	xpds_write_control_register (card_num, XPDS_MCR_MASK_CLR,
		0xff /* XPDS_MCR_MASK__RX_FIFO | XPDS_MCR_MASK__TX_FIFO */, MAIN);

	xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
		0xff, MAIN);

	xpds_read_control_register (card_num, XPDS_MCR_MASK_CLR,
		&value, MAIN);
	dprintk (KERN_DEBUG "%s: interrupt mask is %02x\n", xpds_devs[card_num].name, value);

	/*
	 * Enable receive and transmit DMAs.  The transmit DMA is only
	 * enabled if we do not encounter the low bit rate bug.
	 */
	if (! RX_DMA_LOW_RATE_BUG (card_num) ) {
		if (! xpds_data[card_num].has_rx_dma_burst_bug) {
			dprintk (KERN_INFO "%s: enabling burst on RX DMA\n",
				xpds_devs[card_num].name);
			xpds_write_control_register (card_num, XPDS_DMA_CONFIG,
				XPDS_DMA_CONFIG__BURST_ENABLE | XPDS_DMA_CONFIG__ENABLE,
				RX_DMA);
		} else {
			xpds_write_control_register (card_num, XPDS_DMA_CONFIG,
				XPDS_DMA_CONFIG__ENABLE, RX_DMA);
		}
	}
	if (! TX_DMA_LOW_RATE_BUG (card_num) ) {
		if (! xpds_data[card_num].has_tx_dma_burst_bug) {
			dprintk (KERN_INFO "%s: enabling burst on TX DMA\n",
				xpds_devs[card_num].name);
			xpds_write_control_register (card_num, XPDS_DMA_CONFIG,
				XPDS_DMA_CONFIG__BURST_ENABLE | XPDS_DMA_CONFIG__ENABLE,
				TX_DMA);
		} else {
			xpds_write_control_register (card_num, XPDS_DMA_CONFIG,
				XPDS_DMA_CONFIG__ENABLE, TX_DMA);
		}
	}

	/*
	 * Enable DMA interrupts.  The transmit DMA interrupt is only
	 * enabled if the low bit rate bug is not encountered.
	 */
	if (! RX_DMA_LOW_RATE_BUG (card_num) ) {
		xpds_write_control_register (card_num, XPDS_DMA_SET_MASK,
			XPDS_DMA_MASK__DONE, RX_DMA);
		xpds_read_control_register (card_num, XPDS_DMA_SET_STAT,
			&value, RX_DMA);
		dprintk (KERN_DEBUG "%s: RX DMA status is %02x\n",
			xpds_devs[card_num].name, value);
	}

	if (! TX_DMA_LOW_RATE_BUG (card_num) ) {
		xpds_write_control_register (card_num, XPDS_DMA_SET_MASK,
			XPDS_DMA_MASK__DONE, TX_DMA);
		xpds_read_control_register (card_num, XPDS_DMA_SET_STAT,
			&value, TX_DMA);
		dprintk (KERN_DEBUG "%s: TX DMA status is %02x\n",
			xpds_devs[card_num].name, value);
	}

	if (xpds_data[card_num].is_sdsl) {
		u32	mask;

		mask = XPDS_MCR_MASK__EXT;
		if (! TX_DMA_LOW_RATE_BUG (card_num)) {
			mask |= XPDS_MCR_MASK__TX_DMA;
		}
		if (! RX_DMA_LOW_RATE_BUG (card_num)) {
			mask |= XPDS_MCR_MASK__RX_DMA;
		} else {
			mask |= XPDS_MCR_MASK__RX_FIFO;
		}
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_SET, mask, MAIN);
	} else {
		xpds_write_control_register (card_num, XPDS_MCR_MASK_SET,
			XPDS_MCR_MASK__RX_DMA | XPDS_MCR_MASK__TX_DMA |
			XPDS_MCR_MASK__RXCI, MAIN);
	}
	xpds_read_control_register (card_num, XPDS_MCR_MASK_CLR,
		&value, MAIN);
#if DEBUG
	dprintk (KERN_DEBUG "%s: interrupt mask is %02x", xpds_devs[card_num].name, value);
	if (value & XPDS_MCR_MASK__RX_FIFO) dprintk (" RX_FIFO");
	if (value & XPDS_MCR_MASK__TX_FIFO) dprintk (" TX_FIFO");
	if (value & XPDS_MCR_MASK__RX_DMA) dprintk (" RX_DMA");
	if (value & XPDS_MCR_MASK__TX_DMA) dprintk (" TX_DMA");
	if (value & XPDS_MCR_MASK__PCI_ERROR) dprintk (" PCI_ERROR");
	if (value & XPDS_MCR_MASK__EXT) dprintk (" EXT");
	if (value & XPDS_MCR_MASK__RXCI) dprintk (" RXCI");
	dprintk ("\n");
#endif

	/*
	 * Let the RX DMA begin (write to hunt bit).
	 * If RX DMA bug, reset and unreset the RX FIFO instead.
	 */
	if (RX_DMA_LOW_RATE_BUG (card_num)) {
		xpds_data[card_num].current_rx_fifo = 0;
		xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
			XPDS_FCR_CONFIG__RESET, RX_FIFO);
		udelay (20);
		xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
			0, RX_FIFO);
	} else {
		xpds_write_control_register (card_num, XPDS_DMA_GO,
			XPDS_DMA_GO__HUNT, RX_DMA);
	}

	/*
	 * Reset and unreset to enable TX FIFO mode instead of
	 * TX DMA to work around the TX DMA bug at < 400 Kbps.
	 */
	if (TX_DMA_LOW_RATE_BUG (card_num)) {
		xpds_data[card_num].current_tx_fifo = 0;
		xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
			XPDS_FCR_CONFIG__RESET, TX_FIFO);
		udelay (20);
		xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
			0, TX_FIFO);
	}

	return 0;
}

static int
xpds_dma_disable (int card_num)
{
	u32	value;

	dprintk (KERN_DEBUG "xpds_dma_disable (%d)\n", card_num);

	/*
	 * Reset the DMAs
	 */
	xpds_write_control_register (card_num, XPDS_DMA_CONFIG,
		XPDS_DMA_CONFIG__RESET, RX_DMA);
	xpds_write_control_register (card_num, XPDS_DMA_CONFIG,
		XPDS_DMA_CONFIG__RESET, TX_DMA);

	/*
	 * Disable the DMAs
	 */
	xpds_write_control_register (card_num, XPDS_DMA_CONFIG, 0x0, RX_DMA);
	xpds_write_control_register (card_num, XPDS_DMA_CONFIG, 0x0, TX_DMA);
	/*
	 * Disable the DMA interrupts
	 */
	xpds_write_control_register (card_num, XPDS_DMA_CLR_MASK,
		XPDS_DMA_MASK__DONE | XPDS_DMA_MASK__FLOW, RX_DMA);
	xpds_write_control_register (card_num, XPDS_DMA_CLR_MASK,
		XPDS_DMA_MASK__DONE | XPDS_DMA_MASK__FLOW, TX_DMA);
	xpds_write_control_register (card_num, XPDS_MCR_MASK_CLR,
		XPDS_MCR_MASK__RX_DMA | XPDS_MCR_MASK__TX_DMA, MAIN);
	xpds_read_control_register (card_num, XPDS_MCR_MASK_CLR,
		&value, MAIN);
	dprintk (KERN_DEBUG "%s: interrupt mask is %02x\n", xpds_devs[card_num].name, value);

	return 0;
}

#define LED_OFF	0
#define LED_ON	1
/*
 * Turn on/off the TX or activity LED
 */
__inline__ static void
xpds_tx_led (int card_num, int on)
{
	if (xpds_data[card_num].is_fpga) {
		if (on) {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__OE_FPGA_TX_LED, MAIN );
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_CLR,
				XPDS_MCR_GPIO__GP_FPGA_TX_LED, MAIN );
		} else {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__GP_FPGA_TX_LED |
				XPDS_MCR_GPIO__OE_FPGA_TX_LED, MAIN );
		}
	} else if (xpds_data[card_num].is_sdsl) {
		if (on) {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__OE_SDSL_ACT_LED, MAIN);
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_CLR,
				XPDS_MCR_GPIO__GP_SDSL_ACT_LED, MAIN);
		} else {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__GP_SDSL_ACT_LED |
				XPDS_MCR_GPIO__OE_SDSL_ACT_LED, MAIN);
		}
	} else {
		if (on) {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__OE_ASIC_ACT_LED, MAIN);
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_CLR,
				XPDS_MCR_GPIO__GP_ASIC_ACT_LED, MAIN);
		} else {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__GP_ASIC_ACT_LED |
				XPDS_MCR_GPIO__OE_ASIC_ACT_LED, MAIN);
		}
	}
}

/*
 * Turn on/off the RX or activity LED
 */
__inline__ static void
xpds_rx_led (int card_num, int on)
{
	if (xpds_data[card_num].is_fpga) {
		if (on) {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__OE_FPGA_RX_LED, MAIN );
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_CLR,
				XPDS_MCR_GPIO__GP_FPGA_RX_LED, MAIN );
		} else {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__GP_FPGA_RX_LED |
				XPDS_MCR_GPIO__OE_FPGA_RX_LED, MAIN );
		}
	} else if (xpds_data[card_num].is_sdsl) {
		if (on) {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__OE_SDSL_ACT_LED, MAIN);
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_CLR,
				XPDS_MCR_GPIO__GP_SDSL_ACT_LED, MAIN);
		} else {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__GP_SDSL_ACT_LED |
				XPDS_MCR_GPIO__OE_SDSL_ACT_LED, MAIN);
		}
	} else {
		if (on) {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__OE_ASIC_ACT_LED, MAIN);
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_CLR,
				XPDS_MCR_GPIO__GP_ASIC_ACT_LED, MAIN);
		} else {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__GP_ASIC_ACT_LED |
				XPDS_MCR_GPIO__OE_ASIC_ACT_LED, MAIN);
		}
	}
}


/*
 * Turn on the link LED
 */
__inline__ static void
xpds_link_led (int card_num, int on)
{
	if (! xpds_data[card_num].is_fpga) {
		if (on) {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__OE_ASIC_LINK_LED, MAIN);
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_CLR,
				XPDS_MCR_GPIO__GP_ASIC_LINK_LED, MAIN);
		} else {
			xpds_write_control_register (card_num,
				XPDS_MCR_GPIO_SET,
				XPDS_MCR_GPIO__GP_ASIC_LINK_LED |
				XPDS_MCR_GPIO__OE_ASIC_LINK_LED, MAIN);
		}
	}
}

/*
 * Resets the XPDS card, including running the PEB 2091 state machine
 * into transparent mode by calling xpds_fsm().
 */
static int
xpds_reset (int card_num)
{
	int	rc;
	int	i;
	int	speed_mode;
	u32	mode;
	u32	value;

	dprintk (KERN_DEBUG "%s: xpds_reset\n", xpds_devs[card_num].name);

	rc = xpds_dma_disable (card_num);
	if (rc != 0) return rc;

	/*
	 * Reset the TX FIFO, then unreset it.
	 */
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
		XPDS_FCR_CONFIG__RESET, TX_FIFO);
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG, 0, TX_FIFO);

	/*
	 * For each FIFO, zero the status.
	 */
	for (i = 0; i < NUM_FIFO; i ++ ) {
		xpds_write_control_register (card_num, XPDS_FCR_STAT,
			0, TX_FIFO);
		xpds_write_control_register (card_num, XPDS_FCR_INC,
			XPDS_FCR_INC__NEXT, TX_FIFO);
	}

	/*
	 * Reset the TX FIFO, then unreset it.
	 */
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
		XPDS_FCR_CONFIG__RESET, TX_FIFO);
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG, 0, TX_FIFO);

	/*
	 * Reset the RX FIFO, then unreset it.
	 */
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
		XPDS_FCR_CONFIG__RESET, RX_FIFO);
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG, 0, RX_FIFO);

	/*
	 * For each FIFO, zero the status.
	 */
	for (i = 0; i < NUM_FIFO; i ++ ) {
		xpds_write_control_register (card_num, XPDS_FCR_STAT,
			0, RX_FIFO);
		xpds_write_control_register (card_num, XPDS_FCR_INC,
			XPDS_FCR_INC__NEXT, RX_FIFO);
	}

	/*
	 * Reset the RX FIFO, then unreset it.
	 */
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
		XPDS_FCR_CONFIG__RESET, RX_FIFO);
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG, 0, RX_FIFO);

	/*
	 * Enable TX and RX HDLC
	 */
	xpds_write_control_register (card_num, XPDS_MCR_TXCFG, 
		XPDS_MCR_TXCFG__ENABLE, MAIN);
	if (xpds_data[card_num].has_last_byte_bug) {
		xpds_write_control_register (card_num, XPDS_MCR_RXCFG, 
			(xpds_data[card_num].is_fpga ?
				XPDS_MCR_RXCFG__ENABLE :
				XPDS_MCR_RXCFG__ENABLE | XPDS_MCR_RXCFG__NO_CRC),
			MAIN);
	} else {
		xpds_write_control_register (card_num, XPDS_MCR_RXCFG, 
			XPDS_MCR_RXCFG__ENABLE, MAIN);
	}

	/* 
	 * Set mode register to normal mode.
	 */
	if (xpds_asic_loopback) {
		dprintk (KERN_INFO "%s: setting ASIC loopback\n", xpds_devs[card_num].name);
		xpds_write_control_register (card_num, XPDS_MCR_CONFIG,
			XPDS_MCR_CONFIG__MODE_LOOPBACK, MAIN);
	} else {
		xpds_write_control_register (card_num, XPDS_MCR_CONFIG,
			XPDS_MCR_CONFIG__MODE_NORMAL, MAIN);
	}

	/*
	 * Turn on activation LED
	 */
	xpds_write_control_register (card_num, XPDS_MCR_TEST, 1, MAIN);

	/*
	 * Clear all bits in GPIO
	 */
	xpds_write_control_register (card_num, XPDS_MCR_GPIO_CLR, 0xff, MAIN);

	/*
	 * Enable GPIO outputs
	 */
	xpds_write_control_register (card_num, XPDS_MCR_GPIO_SET,
		XPDS_MCR_GPIO__OE_MASK, MAIN);

	/*
	 * Enable NT or LT mode
	 */
	if (xpds_data[card_num].is_fpga) {
		xpds_write_control_register (card_num, XPDS_MCR_GPIO_SET,
			(xpds_data[card_num].is_lt ?
				XPDS_MCR_GPIO__GP_FPGA_LT :
				XPDS_MCR_GPIO__GP_FPGA_NT) |
			XPDS_MCR_GPIO__OE_MASK,
			MAIN);
	}

	/*
	 * Turn off the LEDs
	 */
	if (xpds_data[card_num].is_fpga) {
		xpds_write_control_register (card_num, XPDS_MCR_GPIO_SET,
			XPDS_MCR_GPIO__GP_FPGA_TX_LED |
			XPDS_MCR_GPIO__GP_FPGA_RX_LED |
			XPDS_MCR_GPIO__OE_FPGA_TX_LED |
			XPDS_MCR_GPIO__OE_FPGA_RX_LED, MAIN);
	} else {
		xpds_write_control_register (card_num, XPDS_MCR_GPIO_SET,
			XPDS_MCR_GPIO__GP_ASIC_ACT_LED |
			XPDS_MCR_GPIO__GP_ASIC_LINK_LED |
			XPDS_MCR_GPIO__OE_ASIC_ACT_LED |
			XPDS_MCR_GPIO__OE_ASIC_LINK_LED, MAIN);
	}

	/*
	 * Enable interrupt which notifies a change in the indication
	 * code of the PEB 2091 (RXCI) or the interrupt which indicates
	 * a change in the status of the SDSL (EXT).
	 */
	if (xpds_data[card_num].is_sdsl) {
		xpds_write_control_register (card_num, XPDS_MCR_MASK_SET,
			XPDS_MCR_MASK__EXT, MAIN);
	} else {
		xpds_write_control_register (card_num, XPDS_MCR_MASK_SET,
			XPDS_MCR_MASK__RXCI, MAIN);
	}
	xpds_read_control_register (card_num, XPDS_MCR_MASK_SET,
		&value, MAIN);
	dprintk (KERN_DEBUG "%s: interrupt mask is %02x\n",
		xpds_devs[card_num].name, value);

	rc = xpds_init_descriptors (card_num);
	if (rc != 0) return rc;

	mode = 0;
	if (xpds_data[card_num].is_sdsl) {
		u32	sdsl_mode;

		/*
		 * Reset and unreset the SDSL, then get the SDSL mode.
		 */
		rc = xpds_reset_sdsl (card_num);
		if (rc != 0) return rc;
		rc = xpds_start_sdsl (card_num);
		if (rc != 0) return rc;

		rc = xpds_get_sdsl_mode (card_num, &sdsl_mode);
		dprintk ("%s: SDSL mode is %08x\n", xpds_devs[card_num].name, sdsl_mode);
		speed_mode = sdsl_mode & XPDS_SDSL_MODE__SPEED_MASK;
		dprintk ("%s: SDSL speed is %d\n", xpds_devs[card_num].name, speed_mode << 3);
		xpds_data[card_num].sdsl_speed = speed_mode << 3;
	} else {
		/*
		 * Take the PEB 2091 out of reset (B1, B2, and D channels for
		 * 144k operation).  Only for IDSL card.
		 */
		speed_mode = xpds_data[card_num].speed_mode;
		if (speed_mode & XPDS_MODE_B1) {
			if (speed_mode & XPDS_MODE_B2) {
				if (speed_mode & XPDS_MODE_D) {
					dprintk ("%s: selecting B1B2D (144k)\n", xpds_devs[card_num].name);
					mode |= XPDS_MCR_TXCI__PMD_CONFIG_B1B2D;
				} else {
					dprintk ("%s: selecting B1B2 (128k)\n", xpds_devs[card_num].name);
					mode |= XPDS_MCR_TXCI__PMD_CONFIG_B1B2;
				}
			} else {
				dprintk ("%s: selecting B1 (64k)\n", xpds_devs[card_num].name);
				mode |= XPDS_MCR_TXCI__PMD_CONFIG_B1;
			}
		} else if (speed_mode & XPDS_MODE_B2) {
			dprintk ("%s: selecting B2 (64k)\n", xpds_devs[card_num].name);
			mode |= XPDS_MCR_TXCI__PMD_CONFIG_B2;
		} else {
			dprintk (KERN_ERR "%s: invalid mode %d\n", xpds_devs[card_num].name, speed_mode);
			dprintk ("%s: selecting B1B2D (144k)\n", xpds_devs[card_num].name);
			mode |= XPDS_MCR_TXCI__PMD_CONFIG_B1B2D;
		}
		xpds_write_control_register (card_num, XPDS_MCR_TXCI, mode, MAIN);
	}

	/*
	 * Set the PMD enable bit.
	 */
	if (! xpds_data[card_num].is_sdsl) {
		xpds_read_control_register (card_num, XPDS_MCR_TXCI, &mode, MAIN);
	}
	mode |= XPDS_MCR_TXCI__PMD_ENABLE | XPDS_MCR_TXCI__PMD_RESQ;
	xpds_write_control_register (card_num, XPDS_MCR_TXCI, mode, MAIN);

	if (xpds_data[card_num].is_sdsl) {
		rc = xpds_mailbox_write (card_num, XPDS_MBX_START_BITPUMP, 0);
		DELAY_HZ (3 * HZ / 2, card_num);
		if (xpds_external_loopback) {
			xpds_sdsl_loopback (card_num);
			DELAY (2, card_num);
		}
	} else {
		/*
		 * need to control state machine for non-SDSL cards
		 */
		if (xpds_data[card_num].is_lt) {
			rc = xpds_fsm_lt (card_num, mode, xpds_guard_time);
		} else {
			rc = xpds_fsm_nt (card_num, mode, xpds_guard_time);
		}
		if (rc != 0) return rc;
	}

	/*
	 * Turn on the link LED
	 */
	if (! xpds_data[card_num].is_sdsl) xpds_link_led (card_num, 1);

	/*
	 * Reset the FIFOs.
	 */
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
		XPDS_FCR_CONFIG__RESET, TX_FIFO);
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG,
		XPDS_FCR_CONFIG__RESET, RX_FIFO);

	DELAY_HZ (1, card_num);

	/*
	 * For each FIFO, zero the status.
	 */
	for (i = 0; i < NUM_FIFO; i ++ ) {
		xpds_write_control_register (card_num, XPDS_FCR_STAT,
			0, TX_FIFO);
		xpds_write_control_register (card_num, XPDS_FCR_INC,
			XPDS_FCR_INC__NEXT, TX_FIFO);
		xpds_write_control_register (card_num, XPDS_FCR_STAT,
			0, RX_FIFO);
		xpds_write_control_register (card_num, XPDS_FCR_INC,
			XPDS_FCR_INC__NEXT, RX_FIFO);
	}

	/*
	 * Unreset the FIFOs.
	 */
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG, 0, TX_FIFO);
	xpds_write_control_register (card_num, XPDS_FCR_CONFIG, 0, RX_FIFO);

	xpds_read_control_register (card_num, XPDS_FCR_CONFIG, &value, RX_FIFO);
	dprintk (KERN_DEBUG "%s: RX FIFO config register is %08x\n",
		xpds_devs[card_num].name, value);

	/*
	 * For IDSL, physical layer is up.
	 * For SDSL, wait until interrupt occurs to let us know.
	 */
	if (! xpds_data[card_num].is_sdsl) {
		struct net_device	*dev;

		printk ("%s: physical link came up\n", xpds_devs[card_num].name);
		xpds_data[card_num].physical_up = 1;
		rc = xpds_dma_enable (card_num);
		dev = &(xpds_devs[card_num]);
		netif_start_queue(dev);
		if (rc != 0) return rc;
	}

	return 0;
}

/*
 * Called from interrupt handler to receive a packet.
 */
static void
xpds_rx (struct net_device *dev, int len, volatile u8 *buf)
{
	struct sk_buff	*skb;
	int		card_num;
	struct frad_local	*fp;
	short		dlci;
	xpds_data_t	*data_ptr;
	int		i;

	card_num = dev - xpds_devs;
	dprintk (KERN_DEBUG "xpds_rx (%p (%d), %d, %p)\n",
		dev, card_num, len, buf);
	xpds_rx_led (card_num, LED_ON);

	data_ptr = dev->priv;
	fp = &(data_ptr->frad_data);
	dlci = data_ptr->dlci;

	ddprintk (KERN_DEBUG "data_ptr = %p, fp = %p\n", data_ptr, fp);

#if DEBUG
	{
		int	i, debuglen;

		debuglen = len;
		if (debuglen > sizeof (struct frhdr)) {
			debuglen = sizeof (struct frhdr);
		}
		dpprintk (KERN_DEBUG "frame header (%p) received:", buf);
		for (i = 0; i < debuglen; i ++) {
			dpprintk (" %02x", buf[i]);
		}
		dpprintk ("\n");
	}
	{
		int	i, debuglen;

		debuglen = len;
		if (debuglen > 256) debuglen = 256;
		dpprintk (KERN_DEBUG "data (%p) received:",
			buf + sizeof (struct frhdr));
		for (i = sizeof (struct frhdr); i < debuglen ; i ++) {
			dpprintk (" %02x", buf[i]);
		}
		if (len > debuglen) dpprintk (" ...");
		dpprintk ("\n");
	}
#endif

	if (dlci == 0) {
		data_ptr->stats.rx_errors ++;
		nrprintk (KERN_ERR "xpds_rx failed -- DLCI 0???\n");
		xpds_rx_led (card_num, LED_OFF);
		return;
	}

	for (i = 0; i < CONFIG_DLCI_MAX; i ++) {
		if (fp->dlci[i] == dlci) break;
	}
	if (i == CONFIG_DLCI_MAX) {
		data_ptr->stats.rx_errors ++;
		nrprintk (KERN_ERR "xpds_rx failed -- invalid DLCI %d\n", dlci);
		xpds_rx_led (card_num, LED_OFF);
		return;
	}
	ddprintk (KERN_DEBUG "dlci = %d, i = %d\n", dlci, i);

	skb = dev_alloc_skb (len);
	if (skb == NULL) {
		printk (KERN_ERR "%s: unable to allocate skb for packet reception\n", dev->name);
		return;
	}
	memcpy (skb_put (skb, len), (void *)buf, len);
	skb->dev = dev;
	/* skb->protocol = eth_type_trans (skb, dev); */
	skb->ip_summed = CHECKSUM_NONE; /* software will do checksum */
	xpds_dlci_receive (skb, dev);
	/* netif_rx (skb); */

	data_ptr->stats.rx_packets ++;
	data_ptr->stats.rx_bytes ++;

	dprintk (KERN_DEBUG "xpds_rx done\n");

	dev->last_rx = jiffies;
	xpds_rx_led (card_num, LED_OFF);
}

static struct tq_struct *xpds_reset_bh_tasks;

/*
 * A bottom half function that is meant to be put on a task queue
 * if the link goes down (i.e. PEB 2091 falls out of transparent mode)
 * and either a transmit is attempted or an RXCI interrupt (indicating
 * that the other side may be back up) is received.
 */
static void
xpds_reset_bh (void *p)
{
	int	rc;
	int	card_num;
	struct net_device *dev;

	dev = (struct net_device *)p;

	card_num = dev - xpds_devs;

	dprintk (KERN_DEBUG "%s: retrying physical link / state machine, calling xpds_reset\n", xpds_devs[card_num].name);

	/*
	 * The guard times should be different on LT vs. NT
	 * when retrying.  Should the guard times change every
	 * retry?
	 */
	xpds_guard_time = xpds_data[card_num].is_lt ? 3 : 2;
	rc = xpds_reset (card_num);
	if (rc == 0) {
		printk ("%s: physical link is up\n", xpds_devs[card_num].name);
		if (! xpds_data[card_num].is_sdsl) xpds_link_led (card_num, 1);
		netif_start_queue(dev);
	}
	xpds_data[card_num].physical_retrying = 0;
}

#if DEBUG
static void
xpds_print_interrupt_type (int card_num, u32 interrupt)
{
	u32	value;

	xpds_read_control_register (card_num, XPDS_MCR_MASK_CLR,
		&value, MAIN);
	dprintk (KERN_DEBUG "%s: interrupt mask is %02x", xpds_devs[card_num].name, value);
	if (value & XPDS_MCR_MASK__RX_FIFO) dprintk (" RX_FIFO");
	if (value & XPDS_MCR_MASK__TX_FIFO) dprintk (" TX_FIFO");
	if (value & XPDS_MCR_MASK__RX_DMA) dprintk (" RX_DMA");
	if (value & XPDS_MCR_MASK__TX_DMA) dprintk (" TX_DMA");
	if (value & XPDS_MCR_MASK__PCI_ERROR) dprintk (" PCI_ERROR");
	if (value & XPDS_MCR_MASK__EXT) dprintk (" EXT");
	if (value & XPDS_MCR_MASK__RXCI) dprintk (" RXCI");
	dprintk ("\n");
	dprintk (KERN_DEBUG "%s: received interrupt %02x",
		xpds_devs[card_num].name, interrupt);
	if (interrupt & XPDS_MCR_MASK__RX_FIFO) dprintk (" RX_FIFO");
	if (interrupt & XPDS_MCR_MASK__TX_FIFO) dprintk (" TX_FIFO");
	if (interrupt & XPDS_MCR_MASK__RX_DMA) dprintk (" RX_DMA");
	if (interrupt & XPDS_MCR_MASK__TX_DMA) dprintk (" TX_DMA");
	if (interrupt & XPDS_MCR_MASK__PCI_ERROR) dprintk (" PCI_ERROR");
	if (interrupt & XPDS_MCR_MASK__EXT) dprintk (" EXT");
	if (interrupt & XPDS_MCR_MASK__RXCI) dprintk (" RXCI");
	dprintk ("\n");
	xpds_read_control_register (card_num, XPDS_DMA_SET_MASK,
		&value, RX_DMA);
	dprintk (KERN_DEBUG "%s: RX DMA mask is %02x\n", xpds_devs[card_num].name, value);
	xpds_read_control_register (card_num, XPDS_DMA_SET_MASK,
		&value, TX_DMA);
	dprintk (KERN_DEBUG "%s: TX DMA mask is %02x\n", xpds_devs[card_num].name, value);
}

static void
xpds_print_fifo_data (int card_num, u32 interrupt)
{
	u32	value;

	if ((xpds_debug_level & (DEBUG_MAIN | DEBUG_DETAILED)) != (DEBUG_MAIN | DEBUG_DETAILED)) return;

	if (interrupt & XPDS_MCR_INT__RX_FIFO) {
		int	i, rc;
		u8	*vptr;
		const volatile xpds_rxtx_list_t	*rxtx;

		ddprintk (KERN_DEBUG "RX FIFO control registers:");
		for (i = 0; i < NUM_FIFO_CONTROL_REGISTERS; i += 4) {
			if (i % 16 == 0) {
				ddprintk ("\n");
				ddprintk (KERN_DEBUG "%08x:", i);
			}
			rc = xpds_read_control_register (card_num, i, &value, RX_FIFO);
			if (rc > 0) {
				ddprintk (" E%d E%d E%d E%d", rc, rc, rc, rc);
			} else {
				vptr = (u8 *) &value;
				ddprintk (" %02x %02x %02x %02x",
					vptr[0], vptr[1], vptr[2], vptr[3]);
			}
		}
		ddprintk ("\n");

		ddprintk (KERN_DEBUG "RX FIFO data registers:");
		for (i = 0; i < NUM_FIFO_DATA_REGISTERS; i += 4) {
			if (i % 16 == 0) {
				ddprintk ("\n");
				ddprintk (KERN_DEBUG "%08x:", i);
			}
			rc = xpds_read_control_register (card_num, i, &value, RX_FIFO_DATA);
			if (rc > 0) {
				ddprintk (" E%d E%d E%d E%d", rc, rc, rc, rc);
			} else {
				vptr = (u8 *) &value;
				ddprintk (" %02x %02x %02x %02x",
					vptr[0], vptr[1], vptr[2], vptr[3]);
			}
		}
		ddprintk ("\n");

		ddprintk (KERN_DEBUG "TX FIFO control registers:");
		for (i = 0; i < NUM_FIFO_CONTROL_REGISTERS; i += 4) {
			if (i % 16 == 0) {
				ddprintk ("\n");
				ddprintk (KERN_DEBUG "%08x:", i);
			}
			rc = xpds_read_control_register (card_num, i, &value, TX_FIFO);
			if (rc > 0) {
				ddprintk (" E%d E%d E%d E%d", rc, rc, rc, rc);
			} else {
				vptr = (u8 *) &value;
				ddprintk (" %02x %02x %02x %02x",
					vptr[0], vptr[1], vptr[2], vptr[3]);
			}
		}
		ddprintk ("\n");

		ddprintk (KERN_DEBUG "TX FIFO data registers:");
		for (i = 0; i < NUM_FIFO_DATA_REGISTERS; i += 4) {
			if (i % 16 == 0) {
				ddprintk ("\n");
				ddprintk (KERN_DEBUG "%08x:", i);
			}
			rc = xpds_read_control_register (card_num, i, &value, TX_FIFO_DATA);
			if (rc > 0) {
				ddprintk (" E%d E%d E%d E%d", rc, rc, rc, rc);
			} else {
				vptr = (u8 *) &value;
				ddprintk (" %02x %02x %02x %02x",
					vptr[0], vptr[1], vptr[2], vptr[3]);
			}
		}
		ddprintk ("\n");

		ddprintk (KERN_DEBUG "RX DMA control registers:");
		for (i = 0; i < NUM_DMA_CONTROL_REGISTERS; i += 4) {
			if (i % 16 == 0) {
				ddprintk ("\n");
				ddprintk (KERN_DEBUG "%08x:", i);
			}
			rc = xpds_read_control_register (card_num, i, &value, RX_DMA);
			if (rc > 0) {
				ddprintk (" E%d E%d E%d E%d", rc, rc, rc, rc);
			} else {
				vptr = (u8 *) &value;
				ddprintk (" %02x %02x %02x %02x",
					vptr[0], vptr[1], vptr[2], vptr[3]);
			}
		}
		ddprintk ("\n");

		ddprintk (KERN_DEBUG "TX DMA control registers:");
		for (i = 0; i < NUM_DMA_CONTROL_REGISTERS; i += 4) {
			if (i % 16 == 0) {
				ddprintk ("\n");
				ddprintk (KERN_DEBUG "%08x:", i);
			}
			rc = xpds_read_control_register (card_num, i, &value, TX_DMA);
			if (rc > 0) {
				ddprintk (" E%d E%d E%d E%d", rc, rc, rc, rc);
			} else {
				vptr = (u8 *) &value;
				ddprintk (" %02x %02x %02x %02x",
					vptr[0], vptr[1], vptr[2], vptr[3]);
			}
		}
		ddprintk ("\n");

		for (i = 0, rxtx = xpds_data[card_num].current_rx_dma;
			i < NUM_DESC;
			i ++, rxtx = rxtx->next) {
			ddprintk (KERN_DEBUG "rx[%d](%p)->control = %08x",
				i, rxtx, rxtx->control);
			if (rxtx->control & RXTX_CONTROL__SWGO) {
				ddprintk (" SWGO");
			}
			if (rxtx->control & RXTX_CONTROL__HWGO) {
				ddprintk (" HWGO");
			}
			if (rxtx->control & RXTX_CONTROL__NEXT_VALID) {
				ddprintk (" NEXT_VALID");
			}
			ddprintk ("\n");
			if (! (rxtx->control & RXTX_CONTROL__NEXT_VALID)) {
				break;
			}
		}
		for (i = 0, rxtx = xpds_data[card_num].current_tx_dma;
			i < NUM_DESC;
			i ++, rxtx = rxtx->next) {
			ddprintk (KERN_DEBUG "tx[%d](%p)->control = %08x",
				i, rxtx, rxtx->control);
			if (rxtx->control & RXTX_CONTROL__SWGO) {
				ddprintk (" SWGO");
			}
			if (rxtx->control & RXTX_CONTROL__HWGO) {
				ddprintk (" HWGO");
			}
			if (rxtx->control & RXTX_CONTROL__NEXT_VALID) {
				ddprintk (" NEXT_VALID");
			}
			ddprintk ("\n");
			if (! (rxtx->control & RXTX_CONTROL__NEXT_VALID)) {
				break;
			}
		}
	}
	xpds_read_control_register (card_num, XPDS_MCR_RXCFG, &value, MAIN);
	ddprintk ("%s: RX HDLC config is %08x\n", xpds_devs[card_num].name, value);
	xpds_read_control_register (card_num, XPDS_MCR_TXCFG, &value, MAIN);
	ddprintk ("%s: TX HDLC config is %08x\n", xpds_devs[card_num].name, value);
}
#endif

/*
 * For SDSL cards with the TX DMA low rate bug, do the transfer from the
 * TX DMA descriptor to the TX FIFO.  The TX FIFO interrupt indicates that
 * the TX FIFO is ready to receive up to 32 bytes of data; this function
 * simulates the hardware's method of copying data from the TX DMA to the
 * TX FIFO.
 *
 * Note that this function keep stuffing the TX FIFO as long as it is
 * ready (i.e. the TX FIFO interrupt bit shows 1), since if there is
 * too long a delay for the next piece of the packet to be put in the
 * TX FIFO, the packet will be aborted.
 */
#define TX_FIFO_DO_MORE	1
#define TX_FIFO_DONE	2
__inline__ static int
xpds_tx_fifo_interrupt (int card_num)
{
	volatile u32	*tx_fifo_data_pointer;
	volatile u8	*data;
	int		tx_len, copy_len, i;
	u32		control, length, offset;
	u32		value;
	struct timeval	tv1, tv2;
	int		tvdiff;

	/*
	 * If there is no data in TX DMA (i.e. no more packets to
	 * send), disallow the TX FIFO interrupt (which will be
	 * enabled the next time a packet is transmitted by
	 * xpds_hw_tx() ) and be done.
	 */
	control = xpds_data[card_num].current_hw_tx_dma->control;
	if (! (control & RXTX_CONTROL__HWGO) ) {
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_CLR, XPDS_MCR_INT__TX_FIFO, MAIN);
		return TX_FIFO_DONE;
	}

	/*
	 * Check TX FIFO interrupt to see if hardware is
	 * ready for the next TX FIFO to be filled.
	 * If not, allow TX FIFO interrupts (so that when the
	 * TX FIFO is ready, we come back here) and be done.
	 */
	xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
		XPDS_MCR_INT__TX_FIFO, MAIN);
	xpds_read_control_register (card_num, XPDS_MCR_INT_CLR,
		&value, MAIN);
	if (! (value & XPDS_MCR_INT__TX_FIFO)) {
		dprintk (KERN_DEBUG "%s: TX FIFO interrupt not asserted\n", xpds_devs[card_num].name);
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_SET, XPDS_MCR_INT__TX_FIFO, MAIN);
		xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
			XPDS_MCR_INT__TX_FIFO, MAIN);
		return TX_FIFO_DONE;
	}
	do_gettimeofday (&tv1);

	/*
	 * Get needed information about the length, offset,
	 * and data pointer in the TX DMA descriptor.  Note
	 * that the length in the control field is one less
	 * than the actual length; we add 1 here to get the
	 * true length.
	 */
	length = (control & RXTX_CONTROL__PACKET_LENGTH_MASK) + 1;
	offset = xpds_data[card_num].current_hw_tx_dma->offset;
	data = xpds_data[card_num].current_hw_tx_dma->buffer + offset;

#if DEBUG
	xpds_read_control_register (card_num, XPDS_FCR_CONFIG,
		&value, TX_FIFO);
	dprintk (KERN_DEBUG "%s: TX FIFO config is %02x\n",
		xpds_devs[card_num].name, value);
#endif

	/*
	 * Put data in the appropriate TX FIFO.  Data must be
	 * set / copied into the TX FIFO data registers in 32
	 * bit chunks, not 8 bit chunks.
	 */
	tx_fifo_data_pointer = (volatile u32 *) (xpds_data[card_num].tx_fifo_data_registers + FIFO_SIZE * xpds_data[card_num].current_tx_fifo);
	dprintk (KERN_DEBUG "%s: current_tx_fifo=%d, tx_fifo_data_pointer=%p\n", xpds_devs[card_num].name, xpds_data[card_num].current_tx_fifo, tx_fifo_data_pointer);
	
	if (length - offset > FIFO_SIZE * sizeof (u32)) {
		tx_len = FIFO_SIZE * sizeof (u32);
	} else {
		tx_len = length - offset;
	}

	copy_len = tx_len % 4 == 0 ? tx_len : (tx_len + 4) / 4 * 4;
	for (i = 0; i < copy_len / 4; i ++) {
		tx_fifo_data_pointer[i] = *((u32 *)data + i);
	}

	dprintk (KERN_DEBUG "%s: transmitting %d bytes through FIFO (%d left)\n", xpds_devs[card_num].name, tx_len, length - offset);
#if DEBUG
	dprintk (KERN_DEBUG "TX FIFO data (%p):", tx_fifo_data_pointer);
	for (i = 0; i < tx_len; i ++) {
		dprintk (" %02x", ((volatile u8 *)tx_fifo_data_pointer)[i]);
	}
	dprintk ("\n");
#endif

	/*
	 * Increment the current TX FIFO number and data pointer.
	 * Decrement the length.
	 */
	xpds_data[card_num].current_tx_fifo ++;
	xpds_data[card_num].current_tx_fifo %= NUM_FIFO;
	offset += tx_len;
	xpds_data[card_num].current_hw_tx_dma->offset = offset;

	/*
	 * Tell the hardware that the data is ready.  Note that
	 * the length is reduced by 1 for the hardware. 
	 */
	tx_len -= 1;
	if (length - offset <= 0) {
		tx_len |= XPDS_FCR_STAT__LAST;
		xpds_data[card_num].current_hw_tx_dma->control &=
			~ RXTX_CONTROL__HWGO;
		xpds_data[card_num].current_hw_tx_dma->control |=
			RXTX_CONTROL__SWGO;
		xpds_data[card_num].current_hw_tx_dma->offset = 0;
		xpds_data[card_num].current_hw_tx_dma =
			xpds_data[card_num].current_hw_tx_dma->next;
	}

	/*
	 * Note that due to the bug, we cannot use the FIFO registers
	 * until at least 10 microseconds after the FIFO ready (interrupt)
	 * is set.
	 */
	do_gettimeofday (&tv2);
	tvdiff = tv2.tv_usec - tv1.tv_usec +
		(tv2.tv_sec - tv1.tv_sec) * 1000000;
	if (tvdiff >= 0 && tvdiff < 10) {
		udelay (10 - tvdiff);
	}

	dprintk (KERN_DEBUG "%s: writing %02x to FIFO stat\n",
		xpds_devs[card_num].name, tx_len);
	xpds_write_control_register (card_num, XPDS_FCR_STAT,
		tx_len, TX_FIFO);
	xpds_write_control_register (card_num, XPDS_FCR_INC,
		XPDS_FCR_INC__NEXT, TX_FIFO);

#if DEBUG
	xpds_read_control_register (card_num, XPDS_FCR_CONFIG,
		&value, TX_FIFO);
	dprintk (KERN_DEBUG "%s: TX FIFO config is %02x after FCR_INC\n", xpds_devs[card_num].name, value);
	xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
		XPDS_MCR_INT__TX_FIFO, MAIN);
	xpds_read_control_register (card_num, XPDS_MCR_INT_CLR,
		&value, MAIN);
	dprintk (KERN_DEBUG "%s: interrupt value is %02x\n",
		xpds_devs[card_num].name, value);
#endif
	return TX_FIFO_DO_MORE;
}

/*
 * For SDSL cards with the RX DMA low rate bug, do the transfer from the
 * RX FIFO descriptor to the RX DMA.  The RX FIFO interrupt indicates that
 * there is data in the RX FIFO that should be copied to the RX DMA.
 *
 * Note that this function keep taking from the RX FIFO as long as it is
 * ready (i.e. the RX FIFO interrupt bit shows 1), since if there is
 * too long a delay, pieces of the packet may be lost.
 */
#define RX_FIFO_DO_MORE	1
#define RX_FIFO_DONE	2
__inline__ static int
xpds_rx_fifo_interrupt (int card_num)
{
	volatile u32	*rx_fifo_data_pointer;
	volatile u8	*data;
	int		rx_len, rx_last, copy_len, i;
	u32		control, length, offset;
	u32		value;
	struct timeval	tv1, tv2;
	int		tvdiff;

	/*
	 * If the RX DMA buffer is not ready for the hardware,
	 * stop.  The packet is likely to drop in this case...
	 */
	control = xpds_data[card_num].current_hw_rx_dma->control;
	if (! (control & RXTX_CONTROL__HWGO) ) {
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_CLR, XPDS_MCR_INT__RX_FIFO, MAIN);
		return RX_FIFO_DONE;
	}

	/*
	 * Check RX FIFO interrupt to see if hardware hass
	 * filled the next RX FIFO.
	 * If not, allow RX FIFO interrupts (so that when the
	 * RX FIFO is ready, we come back here) and be done.
	 */
	xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
		XPDS_MCR_INT__RX_FIFO, MAIN);
	xpds_read_control_register (card_num, XPDS_MCR_INT_CLR,
		&value, MAIN);
	if (! (value & XPDS_MCR_INT__RX_FIFO)) {
		dprintk (KERN_DEBUG "%s: RX FIFO interrupt not asserted\n", xpds_devs[card_num].name);
		/*
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_SET, XPDS_MCR_INT__RX_FIFO, MAIN);
		xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
			XPDS_MCR_INT__RX_FIFO, MAIN);
		*/
		return RX_FIFO_DONE;
	}

	do_gettimeofday (&tv1);

	/*
	 * Get needed information about the length, offset,
	 * and data pointer in the RX DMA descriptor.  Note
	 * that the length in the control field is one less
	 * than the actual length; we add 1 here to get the
	 * true length.
	 */
	length = (control & RXTX_CONTROL__PACKET_LENGTH_MASK);
	offset = xpds_data[card_num].current_hw_rx_dma->offset;
	data = xpds_data[card_num].current_hw_rx_dma->buffer + offset;

	dprintk (KERN_DEBUG "%s: current RX DMA buffer = %p\n",
		xpds_devs[card_num].name,
		xpds_data[card_num].current_hw_rx_dma->buffer);
	dprintk (KERN_DEBUG "%s: current RX DMA length = %d\n",
		xpds_devs[card_num].name, length);
	dprintk (KERN_DEBUG "%s: current RX DMA offset = %d\n",
		xpds_devs[card_num].name, offset);
	dprintk (KERN_DEBUG "%s: current RX DMA data pointer = %p\n",
		xpds_devs[card_num].name, data);

	/*
	 * Copy data from the RX FIFO into the DMA buffer.  Data
	 * must be set / copied in 32 bit chunks, not 8 bit chunks.
	 */
	rx_fifo_data_pointer = (volatile u32 *) (xpds_data[card_num].rx_fifo_data_registers + FIFO_SIZE * xpds_data[card_num].current_rx_fifo);
	dprintk (KERN_DEBUG "%s: current_rx_fifo=%d, rx_fifo_data_pointer=%p\n", xpds_devs[card_num].name, xpds_data[card_num].current_rx_fifo, rx_fifo_data_pointer);

	/*
	 * Note that due to the bug, we cannot read the FIFO registers
	 * until at least 10 microseconds after the FIFO ready (interrupt)
	 * is set.
	 */
	do_gettimeofday (&tv2);
	tvdiff = tv2.tv_usec - tv1.tv_usec +
		(tv2.tv_sec - tv1.tv_sec) * 1000000;
	if (tvdiff >= 0 && tvdiff < 10) {
		udelay (10 - tvdiff);
	}

	xpds_read_control_register (card_num, XPDS_FCR_STAT,
		&value, RX_FIFO);
	rx_len = value & XPDS_FCR_STAT__SIZE_MASK;
	rx_len += 1;
	rx_last = value & XPDS_FCR_STAT__LAST;

	if (length + rx_len > RXTX_BUFFER_SIZE) {
		dprintk (KERN_ERR "%s: packet too long (length = %d, rx_len = %d)\n", xpds_devs[card_num].name, length, rx_len);
		if (rx_last) {
			control &= ~ RXTX_CONTROL__PACKET_LENGTH_MASK;
			control |= (length - 1);
			control &= ~ RXTX_CONTROL__HWGO;
			control |= RXTX_CONTROL__SWGO;
			control &= ~ XPDS_DMA_DESC__PACKET_TAG_MASK;
			control |= XPDS_DMA_DESC__PACKET_TAG_LONG;
			xpds_data[card_num].current_hw_rx_dma->control = control;
			xpds_data[card_num].current_hw_rx_dma->offset = 0;
		}
		xpds_data[card_num].current_rx_fifo ++;
		xpds_data[card_num].current_rx_fifo %= NUM_FIFO;
		xpds_write_control_register (card_num, XPDS_FCR_INC, 
			XPDS_FCR_INC__NEXT, RX_FIFO);
		return RX_FIFO_DO_MORE;
	}
	dprintk (KERN_DEBUG "%s: receiving %d bytes through FIFO (rx_last=%x)\n", xpds_devs[card_num].name, rx_len, rx_last);
#if DEBUG
	dprintk (KERN_DEBUG "RX FIFO data (%p):", rx_fifo_data_pointer);
	for (i = 0; i < rx_len; i ++) {
		dprintk (" %02x", ((volatile u8 *)rx_fifo_data_pointer)[i]);
	}
	dprintk ("\n");
#endif

	copy_len = (rx_len % 4 == 0) ? rx_len : (rx_len + 4) / 4 * 4;
	dprintk (KERN_DEBUG "%s: RX DMA offset = %d, data = %p, copy_len = %d\n", xpds_devs[card_num].name, offset, data, copy_len);
	for (i = 0; i < copy_len / 4; i ++) {
		*((u32 *)data + i) = rx_fifo_data_pointer[i];
	}

	/*
	 * Increment the RX FIFO number and data pointer.
	 */
	xpds_data[card_num].current_rx_fifo ++;
	xpds_data[card_num].current_rx_fifo %= NUM_FIFO;
	xpds_write_control_register (card_num, XPDS_FCR_INC, 
		XPDS_FCR_INC__NEXT, RX_FIFO);

	/*
	 * Increment the length and offset in the RX DMA buffer.
	 * If this is the last piece of the packet, turn on
	 * SW_GO and turn off HW_GO in the descriptor.
	 */
	length += rx_len;
	if (rx_last) {
		u32	tag;
		offset = 0;
		/*
		 * Check for tags.
		 */
		xpds_read_control_register (card_num, XPDS_FCR_TAG,
			&value, RX_FIFO);
		tag = value & XPDS_FCR_TAG__MASK;
		if (tag) {
			dprintk (KERN_DEBUG "%s: RX FIFO received tag %x\n",
				xpds_devs[card_num].name, tag);
		}

		control &= ~ RXTX_CONTROL__PACKET_LENGTH_MASK;
		control |= (length - 1);
		control &= ~ RXTX_CONTROL__HWGO;
		control |= RXTX_CONTROL__SWGO;
		control &= ~ XPDS_DMA_DESC__PACKET_TAG_MASK;
		control |= ((tag << 16) & XPDS_DMA_DESC__PACKET_TAG_MASK);
		dprintk (KERN_DEBUG "%s: packet length is %d\n",
			xpds_devs[card_num].name, length);
	} else {
		control &= ~ RXTX_CONTROL__PACKET_LENGTH_MASK;
		control |= length;
		offset += rx_len;
	}
	xpds_data[card_num].current_hw_rx_dma->control = control;
	xpds_data[card_num].current_hw_rx_dma->offset = offset;

	if (rx_last) {
		xpds_data[card_num].current_hw_rx_dma = xpds_data[card_num].current_hw_rx_dma->next;
		if (! (control & RXTX_CONTROL__NEXT_VALID)) {
			dprintk (KERN_ERR "%s: RX DMA next valid is not set\n", xpds_devs[card_num].name);
		}
	}
	return RX_FIFO_DO_MORE;
}

/*
 * If the RX or TX low rate bugs exist and the
 * RX or TX FIFOs are ready, do the FIFO manipulations.
 */
__inline__ static void
xpds_fifo_interrupts (int card_num, u32 interrupt)
{
	int	rx_rc = RX_FIFO_DONE, tx_rc = TX_FIFO_DONE, do_more;

	dprintk (KERN_DEBUG "%s: xpds_fifo_interrupts(%d,%02x) called\n",
		xpds_devs[card_num].name, card_num, interrupt);
	if ((interrupt & XPDS_MCR_INT__RX_FIFO) &&
		RX_DMA_LOW_RATE_BUG (card_num)) {
		rx_rc = xpds_rx_fifo_interrupt (card_num);
	}
	if ((interrupt & XPDS_MCR_INT__TX_FIFO) &&
		TX_DMA_LOW_RATE_BUG (card_num)) {
		tx_rc = xpds_tx_fifo_interrupt (card_num);
	}
	do_more = rx_rc == RX_FIFO_DO_MORE || tx_rc == TX_FIFO_DO_MORE;
	while (do_more) {
		int	i, rc;

		do_more = 0;
		for (i = 0; i < xpds_max_cards; i ++) {
			if (xpds_data[i].rxtx_mem_allocated == NULL) continue;
			if (RX_DMA_LOW_RATE_BUG (i)) {
				rc = xpds_rx_fifo_interrupt (i);
				if (rc == RX_FIFO_DO_MORE) do_more = 1;
			}
			if (TX_DMA_LOW_RATE_BUG (i)) {
				rc = xpds_tx_fifo_interrupt (i);
				if (rc == TX_FIFO_DO_MORE) do_more = 1;
			}
		}
	}
}

/*
 * Handle RXCI interrupt for IDSL, indicating possible physical
 * layer state change.
 */
__inline__ static void
xpds_rxci_interrupt (int card_num, struct net_device *dev)
{
	int		ind_ai;
	u32		value;

	/*
	 * Get correct IND_AI to look for.
	 */
	ind_ai = xpds_data[card_num].is_lt ? LT_IND_AI : NT_IND_AI;

	xpds_data[card_num].rxci_interrupt_received = 1;
	xpds_read_control_register (card_num, XPDS_MCR_RXCI,
		&value, MAIN);

	/*
	 * If network interface is up, check to see if PEB 2091
	 * fell out of transparent mode.
	 */
	if (xpds_if_busy(dev) && (value & XPDS_MCR_RXCI__MASK) != ind_ai) {
		dprintk (KERN_DEBUG "%s: RXCI status change to 0x%02x\n", xpds_devs[card_num].name, value);
		printk (KERN_NOTICE "%s: physical link went down\n", xpds_devs[card_num].name);
		if (! xpds_data[card_num].is_sdsl) xpds_link_led (card_num, 0);
		netif_stop_queue(dev);
		xpds_if_down(dev);
		xpds_data[card_num].physical_up = 0;
		xpds_data[card_num].physical_retrying = 0;
		memset (xpds_data[card_num].frad_data.pvc_active, 0,
			sizeof (xpds_data[card_num].frad_data.pvc_active));
		memset (xpds_data[card_num].frad_data.pvc_new, 0,
			sizeof (xpds_data[card_num].frad_data.pvc_new));
	} else {
		dprintk (KERN_DEBUG "%s: RXCI status change to 0x%02x\n", xpds_devs[card_num].name, value);
	}
}

/*
 * Handle external interrupt for SDSL, indicating possible physical
 * layer state change.
 */
__inline__ static void
xpds_ext_interrupt (int card_num, struct net_device *dev)
{
	u8	irq_type;
	int	rc;

	/* unmask external interrupt */
	xpds_write_control_register (card_num, XPDS_MCR_MASK_SET,
		XPDS_MCR_INT__EXT, MAIN);

	/* get IRQ type */
	rc = xpds_mailbox_read (card_num, XPDS_MBX_WRITE_IRQTYPE,
		&irq_type);
	if (rc > 0) {
		printk (KERN_ERR "%s: mailbox read (WRITE_IRQTYPE) failed (%d)\n", xpds_devs[card_num].name, rc);
	} else if (irq_type == XPDS_SDSL_IRQTYPE_STATE_CHANGE) {
		u8	state;

		rc = xpds_mailbox_read (card_num,
			XPDS_MBX_READ_PHYS_STATE, &state);
		if (rc > 0) {
			printk (KERN_ERR "%s: mailbox read (READ_PHYS_STATE) failed (%d)\n", xpds_devs[card_num].name, rc);
		} else {
			if (state & XPDS_SDSL_STATE_LINKUP) {
				int	rc;

				printk (KERN_NOTICE "%s: physical link came up (state=%02x)\n", xpds_devs[card_num].name, state);
				netif_start_queue(dev);
				if (! xpds_data[card_num].physical_up) {
					xpds_data[card_num].physical_up = 1;
					xpds_data[card_num].physical_retrying = 0;

					/*
					 * Need to re-enable the DMA which was
					 * disabled when the link went down.
					 */
					rc = xpds_init_descriptors (card_num);
					if (rc) printk (KERN_ERR "%s: xpds_init_descriptors(%d) failed\n", xpds_devs[card_num].name, card_num);
					rc = xpds_dma_enable (card_num);
					if (rc) printk (KERN_ERR "%s: xpds_dma_enable(%d) failed\n", xpds_devs[card_num].name, card_num);
				}

			} else {
				printk (KERN_NOTICE "%s: physical link went down (state=%02x)\n", xpds_devs[card_num].name, state);
				netif_stop_queue(dev);
				xpds_if_down(dev);
				if (xpds_data[card_num].physical_up) {
					u32	sdsl_mode, speed_mode;
					int	rc;

					xpds_data[card_num].physical_up = 0;
					xpds_data[card_num].physical_retrying = 1;

					/*
					 * DMA can get jammed up with junk
					 * packets when link goes down and
					 * up.  Need to disable the DMA here
					 * to get the ASIC unstuck.
					 */
					rc = xpds_dma_disable (card_num);
					if (rc) printk (KERN_ERR "%s: xpds_dma_disable(%d) failed\n", xpds_devs[card_num].name, card_num);

					/*
					 * Reset speed mode (in case it is 0,
					 * need it set that way for mailbox
					 * timeouts).
					 */
					rc = xpds_get_sdsl_mode (card_num,
						&sdsl_mode);
					speed_mode = sdsl_mode & XPDS_SDSL_MODE__SPEED_MASK;
					xpds_data[card_num].sdsl_speed =
						speed_mode << 3;

				}
			}
		}
	}
}

#define PACKET_READ		0
#define RX_DMA_NOT_READY	1

/*
 * Handle an RX DMA interrupt.  Return PACKET_READ if a packet was
 * read (erroneous or otherwise), and RX_DMA_NOT_READY if SW_GO is
 * not set in the next descriptor.
 */
__inline__ static int
xpds_rx_dma_interrupt (struct net_device *dev, int card_num)
{
	volatile xpds_rxtx_list_t	*current_rx;
	int		len;
	int		packet_good;
#if DEBUG
	u32		value;
#endif

	current_rx = xpds_data[card_num].current_rx_dma;

	dprintk (KERN_DEBUG "%s: xpds_rx_dma_interrupt() called\n", xpds_devs[card_num].name);
	dprintk (KERN_DEBUG "%s: current_rx = %p\n", xpds_devs[card_num].name, current_rx);
	dprintk (KERN_DEBUG "%s: current_rx->control = %08x\n", xpds_devs[card_num].name, current_rx->control);
	dprintk (KERN_DEBUG "%s: current_rx->buffer = %p\n", xpds_devs[card_num].name, current_rx->buffer);
	dprintk (KERN_DEBUG "%s: current_rx->next = %p\n", xpds_devs[card_num].name, current_rx->next);
	dprintk (KERN_DEBUG "%s: current_rx->prev = %p\n", xpds_devs[card_num].name, current_rx->prev);
	{
		volatile xpds_rxtx_list_t	*rx;

		for (rx = current_rx->next; rx != current_rx; rx = rx->next) {
			ddprintk (KERN_DEBUG "rx = %p\n", rx);
			ddprintk (KERN_DEBUG "rx->control = %08x\n", rx->control);
			ddprintk (KERN_DEBUG "rx->buffer = %p\n", rx->buffer);
			ddprintk (KERN_DEBUG "rx->next = %p\n", rx->next);
			ddprintk (KERN_DEBUG "rx->prev = %p\n", rx->prev);
		}
	}

#if DEBUG
	xpds_read_control_register (card_num, XPDS_DMA_CLR_STAT,
		&value, RX_DMA);
	ddprintk (KERN_DEBUG "%s: RX DMA status is %02x\n",
		xpds_devs[card_num].name, value);
#endif

#if DEBUG
	xpds_read_control_register (card_num, XPDS_MCR_MASK_CLR,
		&value, MAIN);
	ddprintk (KERN_DEBUG "%s: interrupt mask is %02x\n", xpds_devs[card_num].name, value);
	xpds_read_control_register (card_num, XPDS_MCR_INT_CLR,
		&value, MAIN);
	ddprintk (KERN_DEBUG "%s: interrupt is %02x\n", xpds_devs[card_num].name, value);
#endif

	/*
	 * Hardware should have set the software-go bit.
	 */
	if (! (current_rx->control & RXTX_CONTROL__SWGO) ) {
		dprintk (KERN_DEBUG "%s: xpds_rx_dma_interrupt() done, not ready\n", xpds_devs[card_num].name);
		return RX_DMA_NOT_READY;
	}

	/*
	 * Packet is good unless proven otherwise...
	 */
	packet_good = 1;

	/*
	 * Check the packet tag.  If not OK, mark the packet as not good.
	 */
	{
		u32		packet_tag;
		const char	*tag_str;

		packet_tag = current_rx->control & XPDS_DMA_DESC__PACKET_TAG_MASK;
		if (packet_tag == XPDS_DMA_DESC__PACKET_TAG_CRC) {
			tag_str = "CRC error";
		} else if (packet_tag == XPDS_DMA_DESC__PACKET_TAG_BOUNDARY_VIOLATION) {
			tag_str = "boundary violation";
		} else if (packet_tag == XPDS_DMA_DESC__PACKET_TAG_LONG) {
			tag_str = "long frame";
		} else if (packet_tag == XPDS_DMA_DESC__PACKET_TAG_ABORT) {
			tag_str = "abort received";
		} else if (packet_tag == XPDS_DMA_DESC__PACKET_TAG_OK) {
			tag_str = "OK";
		} else if (packet_tag & 0x80000) {
			tag_str = "overflow/underflow";
		} else {
			tag_str = "unlisted error";
		}
		if (packet_tag != XPDS_DMA_DESC__PACKET_TAG_OK) {
			len = current_rx->control &
				RXTX_CONTROL__PACKET_LENGTH_MASK;
			len ++;
			dprintk (KERN_ERR "%s: received packet of length %d with tag %05x (%s)\n", xpds_devs[card_num].name, len, packet_tag, tag_str);
			packet_good = 0;
		}
	}

	/*
	 * Get and check length of packet.
	 */
	len = current_rx->control & RXTX_CONTROL__PACKET_LENGTH_MASK;
	len ++;
	/* buggy ASIC has packet length 2 too large */
	if (xpds_data[card_num].has_last_byte_bug) {
		len -= 2;
		if (len < 0) len = 0;
	}
	if (len > RXTX_BUFFER_SIZE) {
		dprintk (KERN_ERR "%s: receiving packet of length %d -- too large\n", xpds_devs[card_num].name, len);
		packet_good = 0;
	} else if (len < 1) {
		dprintk (KERN_ERR "%s: receiving packet of length %d -- too small\n", xpds_devs[card_num].name, len);
		packet_good = 0;
	} else {
		dprintk (KERN_DEBUG "%s: receiving packet of length %d\n",
			xpds_devs[card_num].name, len);
	}

	/*
	 * Receive the packet (i.e. copy it to where the kernel
	 * deals with it) if the packet is ok.
	 */
	if (packet_good) {
		xpds_rx (dev, len, current_rx->buffer);
	}

	/*
	 * Zero the packet length.
	 */
	ddprintk (KERN_DEBUG "%s: zeroing packet length\n", xpds_devs[card_num].name);
	current_rx->control &= ~ (RXTX_CONTROL__PACKET_LENGTH_MASK | (RXTX_CONTROL__PACKET_TAG_MASK << RXTX_CONTROL__PACKET_TAG_OFFSET));

	/*
	 * Turn off software-go, then turn on hardware-go to let the
	 * hardware know that it is ok to reuse the buffer.
	 */
	current_rx->control &= ~ RXTX_CONTROL__SWGO;
	current_rx->control |= RXTX_CONTROL__HWGO;

	/*
	 * Advance the current to the next descriptor.
	 */
	xpds_data[card_num].current_rx_dma = current_rx->next;

	/*
	 * Increment the counters.
	 */
	if (packet_good) {
		xpds_data[card_num].stats.rx_packets ++;
	} else {
		xpds_data[card_num].stats.rx_errors ++;
	}

	dprintk (KERN_DEBUG "%s: xpds_rx_dma_interrupt() done, packet read\n", xpds_devs[card_num].name);

	return PACKET_READ;
}

typedef struct {
	struct net_device	*dev;
	u32		interrupt;
} interrupt_bh_data_t;

static struct tq_struct *xpds_interrupt_bh_tasks;

static void xpds_interrupt_bh (void *p);

/*
 * The interrupt handler for the card.  Interrupts may happen due
 * to receiving a packet, or due to an RXCI status change (which
 * occurs while the state machines on the LT and NT sides are
 * negotiating each other into transparent mode).
 */
static void
xpds_interrupt (int irq __attribute__((unused)), void *dev_instance,
	struct pt_regs *regs __attribute__((unused)))
{
	struct net_device	*dev;
	int		card_num;
	u32		interrupt, bh_interrupts;

	dprintk (KERN_DEBUG "xpds_interrupt (%d, %p, %p)\n", irq, dev_instance, regs);

	dev = (struct net_device *)dev_instance;

	if (dev == NULL) return;

	/*
	 * Lock.
	 */
#ifdef __SMP__
        if (test_and_set_bit(0, (void*)&dev->interrupt)) {
                dprintk (KERN_DEBUG "%s: Duplicate entry of the interrupt handler by processor %d.\n",
                        dev->name, hard_smp_processor_id());
                dev->interrupt = 0;
                return;   
        }
#else
/*        if (dev->interrupt) {
                dprintk (KERN_DEBUG "%s: Re-entering the interrupt handler.\n", dev->name);
                return;
        }
	dev->interrupt = 1; */
#endif

	card_num = dev - xpds_devs;

	/*
	 * Get the interrupt type.
	 */
	xpds_read_control_register (card_num, XPDS_MCR_INT_SET,
		&interrupt, MAIN);
#if DEBUG
	xpds_print_interrupt_type (card_num, interrupt);
#endif

	if (RX_DMA_LOW_RATE_BUG (card_num)) {
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_CLR, XPDS_MCR_INT__RX_FIFO, MAIN);
	}
	if (TX_DMA_LOW_RATE_BUG (card_num)) {
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_CLR, XPDS_MCR_INT__TX_FIFO, MAIN);
	}

	/*
	 * Tell the hardware to stop generating this interrupt.
	 * For SDSL, clear the physical layer interrupt first.
	 * For all cards, clear the interrupt in the MCR.
	 */
	if ((interrupt & XPDS_MCR_INT__EXT) && xpds_data[card_num].is_sdsl) {
		int	rc;

		rc = xpds_mailbox_write (card_num, XPDS_MBX_CLEAR_IRQ, 0);
		if (rc > 0) printk (KERN_ERR "%s: mailbox write (CLEAR_IRQ, 0) failed\n", xpds_devs[card_num].name);

		/* mask external interrupt */
		xpds_write_control_register (card_num, XPDS_MCR_MASK_CLR,
			XPDS_MCR_INT__EXT, MAIN);
	}

	if (interrupt & XPDS_MCR_INT__RX_DMA) {
		xpds_write_control_register (card_num, XPDS_DMA_CLR_STAT,
			0xff /* XPDS_DMA_STAT__DONE */, RX_DMA);
	}

	/* clear the interrupt in the MCR */
	xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
		0xff, MAIN);

	/* read the interrupt in the MCR */
	if (xpds_debug_level) {
		u32	value;
		xpds_read_control_register (card_num, XPDS_MCR_INT_SET,
			&value, MAIN);
		dprintk (KERN_DEBUG "%s: interrupt register is %02x after clearing\n", xpds_devs[card_num].name, value);
	}

	/*
	 * If the RX or TX low rate bugs exist and the
	 * RX or TX FIFOs are ready, do the FIFO manipulations.
	 */
	xpds_fifo_interrupts (card_num, interrupt);

#if DEBUG
	xpds_print_fifo_data (card_num, interrupt);
#endif

	/*
	 * If it is a TX DMA interrupt, clear the HW_GO and SW_GO bits
	 * in any descriptors where both are set, and clear the interrupt.
	 */
	if (interrupt & XPDS_MCR_INT__TX_DMA) {
		volatile xpds_rxtx_list_t	*tx_desc;
		int				i;

		tx_desc = xpds_data[card_num].current_tx_dma;
		for (i = 0; i < NUM_DESC; i ++) {
			if ((tx_desc->control & (XPDS_DMA_DESC__HW_GO | XPDS_DMA_DESC__SW_GO)) == (XPDS_DMA_DESC__HW_GO | XPDS_DMA_DESC__SW_GO)) {
				tx_desc->control &= ~ (XPDS_DMA_DESC__HW_GO | XPDS_DMA_DESC__SW_GO);
			}
			tx_desc = tx_desc->next;
		}

		ddprintk (KERN_DEBUG "%s: clearing TX DMA interrupt\n", xpds_devs[card_num].name);
		xpds_write_control_register (card_num,
			XPDS_DMA_CLR_STAT,
			0xff /* XPDS_DMA_STAT__DONE */, TX_DMA);
		xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
			XPDS_MCR_INT__TX_DMA, MAIN);

		xpds_tx_led (card_num, LED_OFF);
	}

	/*
	 * Check to see if the hardware has caught up with the
	 * software.  If it has the software-go bit set, then
	 * reset and start the DMA again.
	 */
	if (xpds_data[card_num].current_rx_dma->prev->control & RXTX_CONTROL__SWGO) {
		if (! RX_DMA_LOW_RATE_BUG (card_num)) {
			printk (KERN_INFO "%s: RX DMA overrun -- restarting RX DMA\n", xpds_devs[card_num].name);
			xpds_dma_disable (card_num);
			xpds_init_descriptors (card_num);
			xpds_dma_enable (card_num);
		}
	}

	mark_bh (NET_BH);

	bh_interrupts = XPDS_MCR_INT__EXT | XPDS_MCR_INT__RXCI | XPDS_MCR_INT__RX_DMA;
	if (RX_DMA_LOW_RATE_BUG (card_num) || TX_DMA_LOW_RATE_BUG (card_num)) {
		bh_interrupts |= XPDS_MCR_INT__RX_FIFO | XPDS_MCR_INT__TX_FIFO;
	}

	if (interrupt & bh_interrupts) {
		dprintk (KERN_DEBUG "queuing a bottom half task (dev = %p (%d), interrupt = %02x)\n", dev, card_num, interrupt);
		((interrupt_bh_data_t *)(xpds_interrupt_bh_tasks[card_num].data))->dev = dev;
		((interrupt_bh_data_t *)(xpds_interrupt_bh_tasks[card_num].data))->interrupt = interrupt;
		queue_task (&(xpds_interrupt_bh_tasks[card_num]), &tq_immediate);
		mark_bh (IMMEDIATE_BH);
	}
#if defined(__i386__)
/*        clear_bit(0, (void*)&dev->interrupt); */
#else
/*	dev->interrupt = 0; */
#endif
}

static void
xpds_interrupt_bh (void *p)
{
	interrupt_bh_data_t	*data;
	struct net_device	*dev;
	int			card_num;
	u32			interrupt;

	dprintk (KERN_DEBUG "xpds_interrupt_bh(%p) started\n", p);

	data = p;
	dev = data->dev;
	card_num = dev - xpds_devs;
	interrupt = data->interrupt;

	dprintk (KERN_DEBUG "card_num = %d, interrupt = %02x\n",
		card_num, interrupt);

	/*
	 * If it is an RX DMA interrupt, need to get the packet
	 * from the buffer that hardware wrote into.  Keep doing
	 * this until there are no more descriptors to read,
	 * because there may be multiple descriptors filled
	 * during one interrupt.
	 *
	 * Note that if we have the TX DMA low bit rate bug, we
	 * need to check see if we can push more data into the TX
	 * FIFO each time we receive a packet; if this isn't done,
	 * the TX FIFOs may get starved.
	 */
	if ((interrupt & XPDS_MCR_INT__RX_DMA) ||
		((interrupt & XPDS_MCR_INT__RX_FIFO) &&
			RX_DMA_LOW_RATE_BUG (card_num)) ) {
		int	rc;
		do {
			xpds_fifo_interrupts (card_num, interrupt);
			rc = xpds_rx_dma_interrupt (dev, card_num);
		} while (rc == PACKET_READ);
	}

	/*
	 * Turn on RX FIFO interrupts if necessary.
	 */
	if (RX_DMA_LOW_RATE_BUG (card_num)) {
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_SET, XPDS_MCR_INT__RX_FIFO, MAIN);
	}

	/*
	 * If it is an RXCI interrupt, clear it, handle it, and go on.
	 * The RXCI interrupt is only used for IDSL cards.
	 */
	if ((interrupt & XPDS_MCR_INT__RXCI) && ! xpds_data[card_num].is_sdsl) {
		xpds_rxci_interrupt (card_num, dev);
	}

	/*
	 * If it is an external interrupt, clear it, handle it, and go on.
	 * The external interrupt is only used for SDSL cards.
	 */
	if ((interrupt & XPDS_MCR_INT__EXT) && xpds_data[card_num].is_sdsl) {
		xpds_ext_interrupt (card_num, dev);
	}

	dprintk (KERN_DEBUG "xpds_interrupt_bh() finished\n");
}


/*
 * Called when "ifconfig xpds0 up" or some such is done.
 * Tries to bring up and reset the device.  Needs to have
 * the other side also trying to get to transparent mode.
 */
static int
xpds_open (struct net_device *dev)
{
	int	card_num, rc, flags;
	u32	value;

	dprintk (KERN_DEBUG "xpds_open (%p (%d))\n", dev, dev - xpds_devs);

	card_num = dev - xpds_devs;

	/*
	 * Disable all interrupts.
	 */
	xpds_write_control_register (card_num, XPDS_MCR_MASK_CLR,
		0xff, MAIN);
	xpds_write_control_register (card_num, XPDS_MCR_INT_CLR,
		0xff, MAIN);
	xpds_read_control_register (card_num, XPDS_MCR_MASK_CLR,
		&value, MAIN);
	dprintk (KERN_DEBUG "%s: interrupt mask is %08x\n", xpds_devs[card_num].name, value);

	/*
	 * Request the IRQ.
	 */
	dev->irq = xpds_data[card_num].pci_dev->irq;

	flags = SA_SHIRQ;

	dprintk (KERN_DEBUG "%s: request_irq (%d, %p, %08x, %s, %p)\n",
		dev->name, dev->irq, xpds_interrupt, flags, dev->name, dev);
	/* DELAY (2, card_num); */
	rc = request_irq (dev->irq, &xpds_interrupt, flags, dev->name, dev);
	if (rc != 0) {
		dprintk (KERN_DEBUG "%s: unable to get IRQ %d (rc=%d)\n",
			dev->name, dev->irq, rc);
		free_irq (dev->irq, dev);
		return -EAGAIN;
	}

	dprintk (KERN_DEBUG "%s: %s got interrupt %d\n", dev->name, dev->name, dev->irq);

	/* dev->dev_addr = ??? ; */

	rc = xpds_reset (card_num);

	xpds_dlci_install_lmi_timer (0, dev);

	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * Called when "ifconfig xpds0 down" or some such is done.
 * Brings down the device.
 */
static int
xpds_stop (struct net_device *dev)
{
	int	card_num;
	int	rc;
	u32	value;

	card_num = dev - xpds_devs;

	dprintk (KERN_DEBUG "%s: xpds_stop(%p)\n", xpds_devs[card_num].name, dev);
	printk (KERN_DEBUG "%s: going down\n", xpds_devs[card_num].name);

	xpds_dlci_remove_lmi_timer (0, dev);

	rc = xpds_dma_disable (card_num);
	if (rc != 0) return rc;

	/*
	 * Turn off link LED, or put the SDSL in reset.
	 */
	if (! xpds_data[card_num].is_sdsl) {
		xpds_link_led (card_num, 0);
	} else {
		xpds_reset_sdsl (card_num);
	}

	/*
	 * Bring physical layer down.
	 */
	if (xpds_data[card_num].is_sdsl) {
		xpds_reset_sdsl (card_num);
	} else {
		xpds_read_control_register (card_num, XPDS_MCR_TXCI, &value,
			MAIN);
		value &= (XPDS_MCR_TXCI__MASK | XPDS_MCR_TXCI__PMD_CONFIG_B1B2D);
		xpds_write_control_register (card_num, XPDS_MCR_TXCI, value,
			MAIN);
	}
	xpds_data[card_num].physical_up = 0;
	xpds_data[card_num].physical_retrying = 0;

	/*
	 * Disable interrupts
	 */
	xpds_write_control_register (card_num, XPDS_MCR_MASK_CLR, 0xff, MAIN);

	/*
	 * Mark the device as off and busy.
	 */
	netif_stop_queue(dev);
	xpds_if_down(dev);

	MOD_DEC_USE_COUNT;

	free_irq (dev->irq, dev);

	return 0;
}

/*
 * Does the hardware control part of the packet transmission.
 */
static int
xpds_hw_tx (char *data, int len, struct net_device *dev)
{
	int	card_num;
	volatile xpds_rxtx_list_t    *current_tx;
	u32	control;

	card_num = dev - xpds_devs;
#if DEBUG
	{
		u32	value;

		xpds_read_control_register (card_num, XPDS_MCR_MASK_CLR,
			&value, MAIN);
		dprintk (KERN_DEBUG "%s: interrupt mask is %02x", xpds_devs[card_num].name, value);
		if (value & XPDS_MCR_MASK__RX_FIFO) dprintk (" RX_FIFO");
		if (value & XPDS_MCR_MASK__TX_FIFO) dprintk (" TX_FIFO");
		if (value & XPDS_MCR_MASK__RX_DMA) dprintk (" RX_DMA");
		if (value & XPDS_MCR_MASK__TX_DMA) dprintk (" TX_DMA");
		if (value & XPDS_MCR_MASK__PCI_ERROR) dprintk (" PCI_ERROR");
		if (value & XPDS_MCR_MASK__EXT) dprintk (" EXT");
		if (value & XPDS_MCR_MASK__RXCI) dprintk (" RXCI");
		dprintk ("\n");
	}
#endif

	dprintk (KERN_DEBUG "%s: transmitting packet of length %d\n",
		xpds_devs[card_num].name, len);

	/*
	 * Make sure data is not too big.  We are assuming that the
	 * other end has the same maximum packet length limitation.
	 * Is this necessarily correct?
	 */
	if (len > xpds_max_packet_length) {
		xpds_data[card_num].stats.tx_errors ++;
		printk (KERN_ERR "packet size %d is too large (maximum %d)\n",
			len, xpds_max_packet_length);
		return -E2BIG;
	}

	/*
	 * Get control information for the descriptor.
	 */
	control = xpds_data[card_num].current_tx_dma->control;

	/*
	 * Check current descriptor to see if it is available.
	 * If software-go flag is not set, then the buffer is still
	 * waiting for the hardware to empty it.
	 */
	if (! (control & RXTX_CONTROL__SWGO) && (control & RXTX_CONTROL__HWGO) ) {
		dprintk (KERN_DEBUG "%s: unable to transmit (SWGO == 0 && HWGO == 1)\n", xpds_devs[card_num].name);
		/* xpds_data[card_num].stats.tx_errors ++; */
		return -EBUSY;
	}

	/*
	 * Indicate the time a transmission started.
	 */
	dev->trans_start = jiffies;

	/*
	 * Fill in current transmit buffer.
	 */
	ddprintk (KERN_DEBUG "%s: filling current transmit buffer\n", xpds_devs[card_num].name);
	memcpy ((void *)(xpds_data[card_num].current_tx_dma->buffer), data, len);

	/*
	 * Set up the length in the current descriptor.
	 */
	ddprintk (KERN_DEBUG "%s: setting length in descriptor\n", xpds_devs[card_num].name);
	control &= ~ RXTX_CONTROL__PACKET_LENGTH_MASK;
	control |= ((len - 1) & RXTX_CONTROL__PACKET_LENGTH_MASK);

	/*
	 * Clear the software-go flag and set the hardware-go flag
	 * in the current descriptor.
	 */
	ddprintk (KERN_DEBUG "%s: clearing SWGO and setting HWGO\n", xpds_devs[card_num].name);
	control &= ~ RXTX_CONTROL__SWGO;
	control |= RXTX_CONTROL__HWGO;

	/*
	 * Put changed control back in the TX descriptor.
	 */
	xpds_data[card_num].current_tx_dma->control = control;

	/*
	 * Print out the current TX descriptor.
	 */
	current_tx =  xpds_data[card_num].current_tx_dma;

	/*
	 * Write a 1 to the hunt bit of the DMA go register for
	 * the transmit DMA to cause it to fetch the next descriptor.
	 */
	ddprintk (KERN_DEBUG "%s: setting hunt bit\n", xpds_devs[card_num].name);
	xpds_write_control_register (card_num, XPDS_DMA_GO,
		XPDS_DMA_GO__HUNT, TX_DMA);

	/*
	 * Set current transmit buffer to the next one in the
	 * circular list, after setting the offset to 0 in case
	 * of the TX DMA bug.
	 */
	xpds_data[card_num].current_tx_dma->offset = 0;
	ddprintk (KERN_DEBUG "%s: setting current transmit buffer to next\n", xpds_devs[card_num].name);
	xpds_data[card_num].current_tx_dma =
		xpds_data[card_num].current_tx_dma->next;

	/*
	 * Increment the counters.
	 */
	xpds_data[card_num].stats.tx_packets ++;
	xpds_data[card_num].stats.tx_bytes += len;

	dprintk (KERN_DEBUG "%s: transmitted packet %ld\n",
		xpds_devs[card_num].name, (long) (xpds_data[card_num].stats.tx_packets));

	/*
	 * If we have the TX DMA bug we need to enable the TX FIFO
	 * interrupt so that when the TX FIFO is ready, we can simulate
	 * the hardware DMA->FIFO transfer in the driver.
	 */
	if (TX_DMA_LOW_RATE_BUG (card_num)) {
		xpds_write_control_register (card_num,
			XPDS_MCR_MASK_SET, XPDS_MCR_INT__TX_FIFO, MAIN);
	}

	dprintk (KERN_DEBUG "%s: xpds_hw_tx () done\n", xpds_devs[card_num].name);

	return 0;
}

/*
 * Called whenever a packet needs to be transmitted.
 * Note that if the physical link is down (not in transparent
 * mode), a bottom half to reset the device is placed on the
 * scheduler queue.
 */
int
xpds_tx (u8 *data, unsigned int len, struct net_device *dev)
{
	int	card_num;
	int	rc;
	u32	control;

	card_num = dev - xpds_devs;

	xpds_tx_led (card_num, LED_ON);

	dprintk (KERN_DEBUG "%s: xpds_tx (%p, %u, %p)\n", xpds_devs[card_num].name,
		data, len, dev);

#if DEBUG
	{
		int	i, debuglen;

		debuglen = len;
		if (debuglen > sizeof (struct frhdr)) {
			debuglen = sizeof (struct frhdr);
		}
		dpprintk (KERN_DEBUG "frame header sent:");
		for (i = 0; i < sizeof (struct frhdr); i ++) {
			dpprintk (" %02x", data[i]);
		}
		dpprintk ("\n");
	}
	{
		int	i, debuglen;

		/* debuglen = ETH_ZLEN < len ? len : ETH_ZLEN; */
		debuglen = len;
		if (debuglen > 256) debuglen = 256;
		dpprintk (KERN_DEBUG "data sent:");
		for (i = sizeof (struct frhdr); i < debuglen ; i ++) {
			dpprintk (" %02x", data[i]);
		}
		if (len > debuglen) dpprintk (" ...");
		dpprintk ("\n");
	}
#endif
	if (! xpds_data[card_num].physical_up) {
		dprintk (KERN_DEBUG "%s: physical link is down\n", xpds_devs[card_num].name);
		if (xpds_data[card_num].is_sdsl) {
			return -EBUSY;
		}
		if (! xpds_data[card_num].physical_retrying) {
			xpds_data[card_num].physical_retrying = 1;

			xpds_reset_bh_tasks[card_num].next = NULL;
			xpds_reset_bh_tasks[card_num].sync = 0;
			xpds_reset_bh_tasks[card_num].routine = xpds_reset_bh;
			xpds_reset_bh_tasks[card_num].data = dev;

			queue_task (&(xpds_reset_bh_tasks[card_num]),
				&tq_scheduler);
		}
		xpds_tx_led (card_num, LED_OFF);
		return -EBUSY;
	}

	/*
	 * Atomically test and set (bit 0 of) the busy flag.
	 * If busy, return BUSY.
	 */
	if (xpds_if_busy(dev)) {
		dprintk (KERN_DEBUG "%s: busy\n", xpds_devs[card_num].name);
		xpds_tx_led (card_num, LED_OFF);
		/* xpds_data[card_num].stats.tx_errors ++; */

		return -EBUSY;
	}
	netif_stop_queue(dev);

	/* len = ETH_ZLEN < len ? len : ETH_ZLEN; */
	dev->trans_start = jiffies;

	rc = xpds_hw_tx (data, len, dev);

	if (rc == 0) {
		/* dev_kfree_skb (skb, FREE_WRITE); */
	} else {
		xpds_tx_led (card_num, LED_OFF);
	}

	/*
	 * Get control information for the descriptor (after
	 * the one that we just wrote into).
	 */
	control = xpds_data[card_num].current_tx_dma->control;

	/*
	 * Atomically clear the busy bit.
	 */
/*	if (test_and_clear_bit (0, (void *)&(dev->tbusy)) == 0) {
		printk (KERN_ERR "%s: test_and_clear_bit(0, &(dev->tbusy)) in xpds_tx() found bit already clear\n", xpds_devs[card_num].name);
	} */
	netif_wake_queue(dev);

	/*
	 * Return OK if xpds_tx returned 0 (OK), BUSY if -EBUSY,
	 * ERR otherwise.
	 */
	return rc;
}

struct net_device_stats *
xpds_get_stats (struct net_device *dev)
{
	xpds_data_t	*data_ptr;

	data_ptr = dev->priv;
	return &(data_ptr->stats);
}

static int
xpds_set_config (struct net_device *dev,
	struct ifmap *map __attribute__ ((unused)) )
{
	dprintk (KERN_DEBUG "%s: xpds_set_config() called\n",
		dev->name);

	if (dev->flags & IFF_UP) return -EBUSY;

	/* nothing */

	return 0;
}

static void
xpds_reboot_sdsl_physical_layer (int card_num)
{
	if (xpds_data[card_num].physical_up ||
		xpds_data[card_num].physical_retrying) {
		xpds_reset_sdsl (card_num);
		xpds_start_sdsl (card_num);
	}
}

static int xpds_init_frad_data (struct frad_local *fp, short dlci);

static int
xpds_ioctl (struct net_device *dev, struct ifreq *rq,
	int cmd)
{
	xpds_data_t	*data_ptr;
	int		val, card_num, rc = 0;

	if (! suser ()) return -EACCES;

	data_ptr = dev->priv;
	card_num = data_ptr - xpds_data;
	val = (int)(rq->ifr_data);
	if (cmd == XPDS_IOCTL_SET_DEBUG) {
#if DEBUG
		int	i;

		printk ("XPDS driver debug level set to %d\n", val);
		xpds_debug_level = val;
		for (i = 0; i < xpds_max_cards; i ++) {
			xpds_devs[i].flags &= ~ IFF_DEBUG;
			xpds_devs[i].flags |= val ? IFF_DEBUG : 0;
		}
#else
		printk (KERN_ERR "This version of the XPDS driver does not have debug capability.\n");
#endif
	} else if (cmd == XPDS_IOCTL_SET_LT) {
		printk ("%s: set to %s\n",
			xpds_devs[card_num].name,
			val ? "LT (line termination)" :
				"NT (network termination)");
		val = (val != 0);
		data_ptr->is_lt = val;
		data_ptr->frad_data.no_initiate_lmi = val;
		if (data_ptr->is_sdsl) {
			u32	sdsl_mode;
			u8	byte;

			xpds_get_flash_sdsl_mode (card_num, &sdsl_mode);
			printk (KERN_DEBUG "%s: sdsl_mode = %08x\n",
				xpds_devs[card_num].name, sdsl_mode);
			sdsl_mode &= ~ XPDS_SDSL_MODE__UNUSED;
			sdsl_mode &= ~ XPDS_SDSL_MODE__NT;
			sdsl_mode |= data_ptr->is_lt ? 0 : XPDS_SDSL_MODE__NT;
			printk (KERN_DEBUG "%s: changed sdsl_mode = %08x\n", xpds_devs[card_num].name, sdsl_mode);
			rc = xpds_set_sdsl_mode (card_num, sdsl_mode);
			if (rc > 0) printk (KERN_ERR "%s: xpds_set_sdsl_mode() failed\n", xpds_devs[card_num].name);
			rc = xpds_get_sdsl_exit_code (card_num, &byte);
			if (rc > 0 || byte != XPDS_SDSL_CMD_COMPLETED) {
				printk (KERN_ERR "%s: SDSL exit code indicated failure (rc=%d, ec=%d)\n", xpds_devs[card_num].name, rc, byte);
				rc = -EBUSY;
			}
		}
	} else if (cmd == XPDS_IOCTL_SET_IDSL_MODE) {
		if (! data_ptr->is_sdsl) {
			printk ("%s: set to mode %d\n", xpds_devs[card_num].name, val);
			data_ptr->speed_mode = val;
		} else {
			printk (KERN_ERR "Setting the IDSL mode is only applicable to IDSL.\n");
			rc = -EINVAL;
		}
	} else if (cmd == XPDS_IOCTL_SET_SDSL_SPEED) {
		if (data_ptr->is_sdsl) {
			u32	sdsl_mode;
			u8	byte;

			printk ("%s: SDSL speed set to %d\n",
				xpds_devs[card_num].name,
				((val >> 3) & XPDS_SDSL_MODE__SPEED_MASK) << 3);
			rc = xpds_get_flash_sdsl_mode (card_num, &sdsl_mode);
			if (rc > 0) printk (KERN_ERR "%s: xpds_get_flash_sdsl_mode() failed\n", xpds_devs[card_num].name);
			printk (KERN_DEBUG "%s: sdsl_mode = %08x\n", xpds_devs[card_num].name, sdsl_mode);
			sdsl_mode &= ~ XPDS_SDSL_MODE__UNUSED;
			sdsl_mode &= ~ XPDS_SDSL_MODE__SPEED_MASK;
			sdsl_mode |= (val >> 3) & XPDS_SDSL_MODE__SPEED_MASK;
			printk (KERN_DEBUG "%s: changed sdsl_mode = %08x\n", xpds_devs[card_num].name, sdsl_mode);
			rc = xpds_set_sdsl_mode (card_num, sdsl_mode);
			if (rc > 0) printk (KERN_ERR "%s: xpds_set_sdsl_mode() failed\n", xpds_devs[card_num].name);
			rc = xpds_get_sdsl_exit_code (card_num, &byte);
			if (rc > 0 || byte != XPDS_SDSL_CMD_COMPLETED) {
				printk (KERN_ERR "%s: SDSL exit code indicated failure (rc=%d, ec=%d)\n", xpds_devs[card_num].name, rc, byte);
				rc = -EBUSY;
			}

			xpds_reboot_sdsl_physical_layer (card_num);

			if ((xpds_data[card_num].has_tx_dma_low_rate_bug ||
				xpds_data[card_num].has_rx_dma_low_rate_bug) &&
				xpds_data[card_num].physical_up) {
				if ((xpds_data[card_num].sdsl_speed < LOW_BIT_RATE && val >= LOW_BIT_RATE) || (xpds_data[card_num].sdsl_speed >= LOW_BIT_RATE && val < LOW_BIT_RATE)) {
					/*
					 * If card has the low bit rate bug,
					 * and the speed change moved across
					 * the value that triggers the bug,
					 * need to reset the DMA / FIFO.
					 */
					xpds_dma_disable (card_num);
					xpds_dma_enable (card_num);
				}
			}
			xpds_data[card_num].sdsl_speed = val;
		} else {
			printk (KERN_ERR "Setting the SDSL speed is only applicable to SDSL.\n");
			rc = -EINVAL;
		}
	} else if (cmd == XPDS_IOCTL_SET_SDSL_INVERT) {
		if (data_ptr->is_sdsl) {
			u32	sdsl_mode;
			u8	byte;

			printk ("%s: SDSL invert set to %d\n",
				xpds_devs[card_num].name, val ? 1 : 0);
			rc = xpds_get_flash_sdsl_mode (card_num, &sdsl_mode);
			if (rc > 0) printk (KERN_ERR "%s: xpds_get_flash_sdsl_mode() failed\n", xpds_devs[card_num].name);
			printk (KERN_DEBUG "%s: sdsl_mode = %08x\n", xpds_devs[card_num].name, sdsl_mode);
			sdsl_mode &= ~ XPDS_SDSL_MODE__UNUSED;
			sdsl_mode &= ~ XPDS_SDSL_MODE__INVERT;
			sdsl_mode |= (val ? XPDS_SDSL_MODE__INVERT : 0);
			printk (KERN_DEBUG "%s: changed sdsl_mode = %08x\n", xpds_devs[card_num].name, sdsl_mode);
			xpds_set_sdsl_mode (card_num, sdsl_mode);
			if (rc > 0) printk (KERN_ERR "%s: xpds_set_sdsl_mode() failed\n", xpds_devs[card_num].name);
			rc = xpds_get_sdsl_exit_code (card_num, &byte);
			if (rc > 0 || byte != XPDS_SDSL_CMD_COMPLETED) {
				printk (KERN_ERR "%s: SDSL exit code indicated failure (rc=%d, ec=%d)\n", xpds_devs[card_num].name, rc, byte);
				rc = -EBUSY;
			}
			xpds_reboot_sdsl_physical_layer (card_num);
		} else {
			printk (KERN_ERR "Setting the invert is only applicable to SDSL.\n");
			rc = -EINVAL;
		}
	} else if (cmd == XPDS_IOCTL_SET_SDSL_SWAP) {
		if (data_ptr->is_sdsl) {
			u32	sdsl_mode;
			u8	byte;

			printk ("%s: SDSL swap set to %d\n",
				xpds_devs[card_num].name, val ? 1 : 0);
			rc = xpds_get_flash_sdsl_mode (card_num, &sdsl_mode);
			if (rc > 0) printk (KERN_ERR "%s: xpds_get_flash_sdsl_mode() failed\n", xpds_devs[card_num].name);
			printk (KERN_DEBUG "%s: sdsl_mode = %08x\n",
				xpds_devs[card_num].name, sdsl_mode);
			sdsl_mode &= ~ XPDS_SDSL_MODE__UNUSED;
			sdsl_mode &= ~ XPDS_SDSL_MODE__SWAP;
			sdsl_mode |= (val ? XPDS_SDSL_MODE__SWAP : 0);
			printk (KERN_DEBUG "%s: changed sdsl_mode = %08x\n", xpds_devs[card_num].name, sdsl_mode);
			rc = xpds_set_sdsl_mode (card_num, sdsl_mode);
			if (rc > 0) printk (KERN_ERR "%s: xpds_set_sdsl_mode() failed\n", xpds_devs[card_num].name);
			rc = xpds_get_sdsl_exit_code (card_num, &byte);
			if (rc > 0 || byte != XPDS_SDSL_CMD_COMPLETED) {
				printk (KERN_ERR "%s: SDSL exit code indicated failure (rc=%d, ec=%d)\n", xpds_devs[card_num].name, rc, byte);
				rc = -EBUSY;
			}
			xpds_reboot_sdsl_physical_layer (card_num);
		} else {
			printk (KERN_ERR "Setting the swap is only applicable to SDSL.\n");
			rc = -EINVAL;
		}
	} else if (cmd == XPDS_IOCTL_INSTALL_FLASH) {
		rc = xpds_install_flash_image (card_num,
			(xpds_flash_image_t *)(rq->ifr_data));
		if (rc > 0) rc = -EBUSY;
	} else if (cmd == XPDS_IOCTL_GET_SDSL_INFO) {
		if (data_ptr->is_sdsl) {
			printk ("%s: getting SDSL serial info for user process\n", xpds_devs[card_num].name);
			copy_to_user ((void *)(rq->ifr_data),
				&(data_ptr->serial_data),
				sizeof (data_ptr->serial_data));
		} else {
			printk (KERN_ERR "%s: cannot get SDSL info on IDSL card\n", xpds_devs[card_num].name);
			rc = -EINVAL;
		}
	} else if (cmd == XPDS_IOCTL_SET_SDSL_INFO) {
		if (data_ptr->is_sdsl) {
			printk ("%s: setting SDSL serial info from user process\n", xpds_devs[card_num].name);
			copy_from_user (&(data_ptr->serial_data),
				(void *)(rq->ifr_data),
				sizeof (data_ptr->serial_data));
			rc = xpds_set_sdsl_info (card_num);
			if (rc > 0) {
				printk (KERN_ERR "%s: unable to set SDSL info\n", xpds_devs[card_num].name);
				rc = -EBUSY;
			}
		} else {
			printk (KERN_ERR "%s: cannot set SDSL info on IDSL card\n", xpds_devs[card_num].name);
			rc = -EINVAL;
		}
	} else if (cmd == XPDS_IOCTL_GET_SDSL_STATE) {
		if (data_ptr->is_sdsl) {
			u8	state;
			rc = xpds_sdsl_get_state (card_num, &state);
			if (rc != 0) {
				printk (KERN_ERR "%s: get SDSL state failed, rc = %d\n", xpds_devs[card_num].name, rc);
				state = -1;
				rc = -EBUSY;
			} else {
				printk (KERN_NOTICE "%s: SDSL state is 0x%02x\n", xpds_devs[card_num].name, state);
			}
			copy_to_user (rq->ifr_data, &state, 1);
		} else {
			printk (KERN_ERR "%s: cannot set SDSL state on IDSL card\n", xpds_devs[card_num].name);
			rc = -EINVAL;
		}
	} else if (cmd == XPDS_IOCTL_SET_LOOPBACK) {
		xpds_loopback_parameters_t	loopback_params;
		copy_from_user (&loopback_params,
			(void *)(rq->ifr_data),
			sizeof (loopback_params));
		if (loopback_params.loopback_type == XPDS_ASIC_LOOPBACK) {
			if (loopback_params.on) {
				printk (KERN_NOTICE "%s: setting ASIC loopback\n", xpds_devs[card_num].name);
				xpds_write_control_register (card_num, XPDS_MCR_CONFIG,
					XPDS_MCR_CONFIG__MODE_LOOPBACK, MAIN);
				netif_start_queue(dev);
				xpds_data[card_num].physical_up = 1;
			} else {
				printk (KERN_NOTICE "%s: unsetting ASIC loopback\n", xpds_devs[card_num].name);
				xpds_write_control_register (card_num, XPDS_MCR_CONFIG,
					XPDS_MCR_CONFIG__MODE_NORMAL, MAIN);
				netif_stop_queue(dev);
				xpds_if_down(dev);
				xpds_data[card_num].physical_up = 0;
			}
		} else if (loopback_params.loopback_type == XPDS_SDSL_LOOPBACK) {
			if (data_ptr->is_sdsl) {
				printk (KERN_NOTICE "%s: setting SDSL external loopback\n", xpds_devs[card_num].name);
				rc = xpds_sdsl_loopback (card_num);
				if (rc != 0) {
					printk (KERN_ERR "%s: setting SDSL external loopback failed, rc = %d\n", xpds_devs[card_num].name, rc);
					rc = -EBUSY;
				}
			} else {
				printk (KERN_ERR "%s: cannot set SDSL external loopback on IDSL card\n", xpds_devs[card_num].name);
				rc = -EINVAL;
			}
		} else {
			printk (KERN_ERR "%s: invalid loopback type %d\n",
				xpds_devs[card_num].name, loopback_params.loopback_type);
		}
	} else if (cmd == XPDS_IOCTL_SET_BRIDGED_ETHERNET) {
		/*
		 * 1 for bridged ethernet mode, 0 otherwise.
		 */
		printk ("%s: bridged ethernet set to %d (%s)\n", xpds_devs[card_num].name,
			val, val ? "on" : "off");
		xpds_data[card_num].bridged_ethernet = val;
	} else if (cmd == XPDS_IOCTL_SET_DLCI_CR) {
		/*
		 * Should be 2, except for buggy Ascends which need 0.
		 */
		printk ("%s: DLCI CR set to %d\n", xpds_devs[card_num].name, val);
		xpds_data[card_num].dlci_cr = val;
	} else if (cmd == XPDS_IOCTL_SET_DLCI_LMI) {
		/*
		 * 0 LMI off
		 * 1 LMI in LT mode (responds to LMI messages)
		 * 2 LMI in NT mode (generates LMI messages)
		 * 3 LMI in NT bidirectional mode (generates and
		 *     responds to LMI messages)
		 */
		printk ("%s: DLCI LMI set to %d\n", xpds_devs[card_num].name, val);
		xpds_data[card_num].dlci_lmi = val;
	} else if (cmd == XPDS_IOCTL_SET_DLCI) {
		printk ("%s: DLCI set to %d\n", xpds_devs[card_num].name, val);
		xpds_data[card_num].dlci = val;
		xpds_init_frad_data (&(xpds_data[card_num].frad_data), val);
	} else {
		printk (KERN_ERR "%s: received cmd = %d, data = %d\n",
			xpds_devs[card_num].name, cmd, val);
		rc = -EINVAL;
	}
	return rc;
}

static int
xpds_init_frad_data (struct frad_local *fp, short dlci)
{
	memset (&(fp->dlci), 0, sizeof (fp->dlci));
	fp->dlci[0] = dlci;

	return 0;
}

static int
xpds_init (struct net_device *dev)
{
	xpds_data_t	*data_ptr;
	int		card_num;

	card_num = dev - xpds_devs;

	dprintk (KERN_DEBUG "xpds_init (%p (%d))\n", dev, card_num);

	netif_stop_queue(dev);
	xpds_if_down(dev);

	ether_setup (dev);
	dev->open = xpds_open;
	dev->stop = xpds_stop;
	dev->hard_start_xmit = /* xpds_tx */ xpds_dlci_transmit;
	dev->get_stats = xpds_get_stats;
	dev->set_config = xpds_set_config;
	/* dev->set_mac_address = xpds_set_mac_addr; */
	/* dev->rebuild_header = xpds_rebuild_header; */
	dev->do_ioctl = xpds_ioctl;
	/* dev->change_mtu = xpds_change_mtu; */
	dev->priv = &(xpds_data[card_num]);
#if DEBUG
	if (xpds_debug_level) dev->flags |= IFF_DEBUG;
#endif
	dev->flags &= ~ (IFF_BROADCAST | IFF_MULTICAST);

	data_ptr = dev->priv;
	memset (&(data_ptr->stats), 0, sizeof (data_ptr->stats));

	/* memset(dev->dev_addr, 0, sizeof(dev->dev_addr)); */
	memcpy(dev->dev_addr,
		xpds_data[dev - xpds_devs].serial_data.mac_address,
		sizeof(dev->dev_addr));
	/* dev->hard_header_len = sizeof (struct frhdr); */

	/*
	 * Make sure that the payload cannot be equal to or
	 * greater than the maximum packet length minus headers.
	 * For some reason, it must be 2 less, not 1 less???
	 * If dev->mtu from ether_setup() is already smaller,
	 * leave it alone.
	 */
	if (dev->mtu > xpds_max_packet_length - sizeof (struct frhdr) - 2) {
		dev->mtu             = xpds_max_packet_length - sizeof (struct frhdr) - 2;
		dprintk (KERN_DEBUG "dev->mtu set to %d\n", dev->mtu);
	}

	xpds_init_frad_data (&(xpds_data[card_num].frad_data),
		xpds_default_dlci);

	/*
	 * Should be set by ioctls...
	 */
	xpds_data[card_num].bridged_ethernet = xpds_default_bridged;
	xpds_data[card_num].dlci = xpds_default_dlci;
	/* dlci_cr needs to be 0 for buggy Ascends */
	xpds_data[card_num].dlci_cr = xpds_default_dlci_cr;
	if (xpds_default_dlci_lmi == XPDS_DLCI_LMI_LT_OR_NT) {
		if (xpds_data[card_num].is_lt) {
			xpds_data[card_num].dlci_lmi = XPDS_DLCI_LMI_LT;
		} else {
			xpds_data[card_num].dlci_lmi = XPDS_DLCI_LMI_NT;
		}
	} else {
		xpds_data[card_num].dlci_lmi = xpds_default_dlci_lmi;
	}

	dprintk (KERN_INFO "%s: bridged ethernet mode is %d (%s)\n",
		xpds_devs[card_num].name, xpds_data[card_num].bridged_ethernet,
		xpds_data[card_num].bridged_ethernet ? "on" : "off");
	dprintk (KERN_INFO "%s: DLCI = %d\n",
		xpds_devs[card_num].name, xpds_data[card_num].dlci);
	dprintk (KERN_INFO "%s: DLCI CR = %d\n",
		xpds_devs[card_num].name, xpds_data[card_num].dlci_cr);
	dprintk (KERN_INFO "%s: DLCI LMI = %d\n",
		xpds_devs[card_num].name, xpds_data[card_num].dlci_lmi);

	dprintk (KERN_DEBUG "xpds_init done\n");

	return 0;
}

#define	SEPROM_SIZE		17

#define SEPROM_WRITE_OPCODE	0x5
#define SEPROM_READ_OPCODE	0x6
#define SEPROM_ERASE_OPCODE	0x7

#define SEPROM_READ_OPERAND_LEN	9
#define SEPROM_READ_DATA_LEN	17

/*
 * Read the given address out of the serial EPROM from an IDSL card.
 */
static u16
read_seprom_data (int card_num, int address)
{
	int	i;
	u16	value = 0;
	int	opcode;

	if (address < 0 || address >= SEPROM_SIZE) {
		printk (KERN_ERR "%s: read_seprom_data (%d (invalid))\n",
			xpds_devs[card_num].name, address);
		return 0;
	}

	opcode = (SEPROM_READ_OPCODE << 6) | address;

	/*
	 * Clear the GPIO.
	 */
	xpds_write_control_register (card_num,
		XPDS_MCR_GPIO_CLR, 0xff, MAIN );

	/*
	 * Enable the SK, DI, and CS bits in the GPIO,
	 * and set the CS.
	 */
	xpds_write_control_register (card_num,
		XPDS_MCR_GPIO_SET,
		XPDS_MCR_GPIO__OE_SEPROM_SK |
			XPDS_MCR_GPIO__OE_SEPROM_DI |
			XPDS_MCR_GPIO__GP_SEPROM_CS |
			XPDS_MCR_GPIO__OE_SEPROM_CS,
		MAIN );
	/*
	 * Give the address to the serial SEPROM.
	 */
	for (i = 0; i < SEPROM_READ_OPERAND_LEN; i ++) {
		int	bit;

		bit = opcode >> ((SEPROM_READ_OPERAND_LEN - 1) - i);
		bit &= 0x1;
		bit = bit ? XPDS_MCR_GPIO__GP_SEPROM_DI : 0;
		/* 0 -> SK, DI */
		xpds_write_control_register (card_num,
			XPDS_MCR_GPIO_CLR,
			XPDS_MCR_GPIO__GP_SEPROM_SK |
				XPDS_MCR_GPIO__GP_SEPROM_DI,
			MAIN);
		udelay (3);
		/* bit -> DI */
		xpds_write_control_register (card_num,
			XPDS_MCR_GPIO_SET, bit, MAIN);
		/* setup time */
		udelay (3);
		/* 1 -> SK */
		xpds_write_control_register (card_num,
			XPDS_MCR_GPIO_SET, XPDS_MCR_GPIO__GP_SEPROM_SK, MAIN);
		/* hold time */
		udelay (3);
	}
	/*
	 * Get the data from the serial SEPROM.
	 */
	for (i = 0; i < SEPROM_READ_DATA_LEN; i ++) {
		u32	bit;

		/* 0 -> SK */
		xpds_write_control_register (card_num,
			XPDS_MCR_GPIO_CLR, XPDS_MCR_GPIO__GP_SEPROM_SK, MAIN);
		/* setup time */
		udelay (3);
		/* 1 -> SK */
		xpds_write_control_register (card_num,
			XPDS_MCR_GPIO_SET, XPDS_MCR_GPIO__GP_SEPROM_SK, MAIN);
		/* hold time */
		udelay (3);
		
		/* read bit */
		xpds_read_control_register (card_num, XPDS_MCR_GPIO_SET,
			&bit, MAIN);
		bit &= XPDS_MCR_GPIO__GP_SEPROM_DO;
		bit = bit ? 1 : 0;
		value <<= 1;
		value |= bit;
	}
	value >>= 1;

	/*
	dprintk (KERN_DEBUG "%s: read_seprom (%d) -> %x\n",
		xpds_devs[card_num].name, address, value);
	*/

	return value;
}

#define SEPROM_REVISION		0
#define SEPROM_HARDWARE_VERSION	1
#define SEPROM_SOFTWARE_VERSION	2
#define SEPROM_FIRMWARE_VERSION	2
#define SEPROM_MFG_DATE_HI	4
#define SEPROM_MFG_DATE_LO	5
#define SEPROM_MAC_ADDR_0_1	6
#define SEPROM_MAC_ADDR_2_3	7
#define SEPROM_MAC_ADDR_4_5	8
#define SEPROM_SERIAL_0		9
#define SEPROM_SERIAL_1		10
#define SEPROM_SERIAL_2		11
#define SEPROM_SERIAL_3		12
#define SEPROM_SERIAL_4		13
#define SEPROM_SERIAL_5		14
#define SEPROM_SERIAL_6		15
#define SEPROM_SERIAL_7		16

/*
 * Determine if the card is an IDSL or SDSL card.  Only the IDSL
 * card has the serial EPROM, so garbage indicates an SDSL card.
 * Note:  an FPGA card will have is_sdsl set to 1; read_sdsl_info()
 * will set is_sdsl back to 0 if it appears to be an IDSL FPGA card.
 * If IDSL, determine if it has the last byte bug.
 */
static void
read_seprom_info (int card_num)
{
	u16	value1, value2, value3, value4;
	xpds_serial_data_t	*sdata;

	if (xpds_data[card_num].is_fpga) {
		/*
		 * FPGA card may be SDSL, so set is_sdsl so that
		 * read_sdsl_info() is called.  read_sdsl_info()
		 * will unset if unable to get SDSL information.
		 */
		xpds_data[card_num].is_sdsl = 1;
		dprintk (KERN_INFO "No serial EPROM for FPGA device.\n");
		return;
	}

	sdata = &(xpds_data[card_num].serial_data);

	value1 = read_seprom_data (card_num, SEPROM_SERIAL_0);
	value2 = read_seprom_data (card_num, SEPROM_SERIAL_1);
	value3 = read_seprom_data (card_num, SEPROM_SERIAL_2);
	value4 = read_seprom_data (card_num, SEPROM_SERIAL_3);
	if (value1 != 0x5854 /* 'XT' */ || value2 != 0x414e /* 'AN' */ ||
		value3 != 0x3230 /* '20' */ || (value4 >> 8) != '0') {
		dprintk (KERN_DEBUG "Serial EPROM serial number does not begin with XTAN200; not an IDSL card.\n");    
		xpds_data[card_num].is_sdsl = 1;
		return; 
	}

	value1 = read_seprom_data (card_num, SEPROM_REVISION);
	dprintk (KERN_DEBUG "Serial EPROM revision %d\n", value1);
	sdata->seprom_revision = value1;

	value1 = read_seprom_data (card_num, SEPROM_HARDWARE_VERSION);
	dprintk (KERN_DEBUG "Hardware version %d.%d\n",
		value1 >> 8, value1 & 0xff);
	sdata->hardware_version[0] = value1 >> 8;
	sdata->hardware_version[1] = value1 & 0xff;

	value1 = read_seprom_data (card_num, SEPROM_SOFTWARE_VERSION);
	dprintk (KERN_DEBUG "Software version %d.%d\n",
		value1 >> 8, value1 & 0xff);
	sdata->software_version[0] = value1 >> 8;
	sdata->software_version[1] = value1 & 0xff;

	value1 = read_seprom_data (card_num, SEPROM_FIRMWARE_VERSION);
	dprintk (KERN_DEBUG "Firmware version %d.%d\n",
		value1 >> 8, value1 & 0xff);
	sdata->firmware_version[0] = value1 >> 8;
	sdata->firmware_version[1] = value1 & 0xff;

	value1 = read_seprom_data (card_num, SEPROM_MFG_DATE_HI);
	value2 = read_seprom_data (card_num, SEPROM_MFG_DATE_LO);
	dprintk (KERN_DEBUG "Manufacturing date %d.%d.%d\n",
		value1, value2 >> 8, value2 & 0xff);
	sdata->mfg_date[0] = value1 >> 8;
	sdata->mfg_date[1] = value1 & 0xff;
	sdata->mfg_date[2] = value2 >> 8;
	sdata->mfg_date[3] = value2 & 0xff;

	value1 = read_seprom_data (card_num, SEPROM_MAC_ADDR_0_1);
	value2 = read_seprom_data (card_num, SEPROM_MAC_ADDR_2_3);
	value3 = read_seprom_data (card_num, SEPROM_MAC_ADDR_4_5);
	dprintk (KERN_DEBUG "MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
		value1 >> 8, value1 & 0xff, value2 >> 8, value2 & 0xff,
		value3 >> 8, value3 & 0xff);
	sdata->mac_address[0] = value1 >> 8;
	sdata->mac_address[1] = value1 & 0xff;
	sdata->mac_address[2] = value2 >> 8;
	sdata->mac_address[3] = value2 & 0xff;
	sdata->mac_address[4] = value3 >> 8;
	sdata->mac_address[5] = value3 & 0xff;

	dprintk (KERN_DEBUG "Serial number ");
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_0);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[0] = value1 >> 8;
	sdata->serial_number[1] = value1 & 0xff;
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_1);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[2] = value1 >> 8;
	sdata->serial_number[3] = value1 & 0xff;
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_2);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[4] = value1 >> 8;
	sdata->serial_number[5] = value1 & 0xff;
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_3);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[6] = value1 >> 8;
	sdata->serial_number[7] = value1 & 0xff;
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_4);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[8] = value1 >> 8;
	sdata->serial_number[9] = value1 & 0xff;
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_5);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[10] = value1 >> 8;
	sdata->serial_number[11] = value1 & 0xff;
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_6);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[12] = value1 >> 8;
	sdata->serial_number[13] = value1 & 0xff;
	value1 = read_seprom_data (card_num, SEPROM_SERIAL_7);
	dprintk ("%c%c", value1 >> 8, value1 & 0xff);
	sdata->serial_number[14] = value1 >> 8;
	sdata->serial_number[15] = value1 & 0xff;
	dprintk ("\n");
}

/*
 * Read information from the SDSL card (previously identified
 * because the serial EPROM did not exist).
 * Note that if the mailbox reads fail, it is probably an IDSL
 * FPGA card.
 */
static int
read_sdsl_info (int card_num)
{
	u8	byte, byte1, byte2;
	int	i, rc, rval = 0;
	xpds_serial_data_t	*sdata;

	if (! xpds_data[card_num].is_sdsl) return 1;

	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_HWVER, &byte1);
	if (rc) rval = 1;

	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_HWVER, &byte2);
	if (rc) rval = 1;

	if (rval) {
		/* IDSL FPGA card */
		xpds_data[card_num].is_sdsl = 0;
		return rval;
	}

	sdata = &(xpds_data[card_num].serial_data);

	dprintk (KERN_INFO "Hardware version ");
	dprintk ("%d.", byte1);
	dprintk ("%d\n", byte2);

	sdata->hardware_version[0] = byte1;
	sdata->hardware_version[1] = byte2;

	dprintk (KERN_INFO "Firmware version ");
	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_FWVER, &byte);
	dprintk ("%d.", byte);
	if (rc) rval = 1;
	sdata->firmware_version[0] = byte;

	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_FWVER, &byte);
	dprintk ("%d\n", byte);
	if (rc) rval = 1;
	sdata->firmware_version[1] = byte;

	if (rval) return rval;

	dprintk (KERN_INFO "Manufacturing date ");
	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_MFGDATE, &byte1);
	if (rc) rval = 1;
	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_MFGDATE, &byte2);
	if (rc) rval = 1;
	dprintk ("%d.", (byte1 << 8) + byte2);
	sdata->mfg_date[0] = byte1;
	sdata->mfg_date[1] = byte2;

	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_MFGDATE, &byte);
	if (rc) rval = 1;
	dprintk ("%d.", byte);
	sdata->mfg_date[2] = byte;

	rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_MFGDATE, &byte);
	if (rc) rval = 1;
	dprintk ("%d\n", byte);
	sdata->mfg_date[3] = byte;

	if (rval) return rval;

	dprintk (KERN_INFO "MAC address ");
	for (i = 0; i < 6; i ++) {
		if (i != 0) dprintk (":");
		rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_MACADDR, &byte);
		dprintk ("%02x", byte);
		if (rc) rval = 1;
		sdata->mac_address[i] = byte;
	}
	dprintk ("\n");

	if (rval) return rval;

	dprintk (KERN_INFO "Serial number ");
	for (i = 0; i < 16; i ++) {
		rc = xpds_mailbox_read (card_num, XPDS_MBX_READ_SERIALNUMBER, &byte);
		dprintk ("%c", byte);
		if (rc) rval = 1;
		sdata->serial_number[i] = byte;
	}
	dprintk ("\n");

	return rval;
}

static int
get_pci_config (struct pci_dev *dev, volatile void **config_mem)
{
	u32	value;
	int	rc;

	rc = pci_read_config_dword (dev, PCI_BASE_ADDRESS_0, &value);

	if (rc != PCIBIOS_SUCCESSFUL) {
		printk (KERN_ERR "unable to read PCI configuration register 4, error = %d\n", rc);
		return -EIO;
	}

	dprintk (KERN_DEBUG "PCI config register 4 value=%08x\n", value);

	value &= PCI_BASE_ADDRESS_IO_MASK;
 
        *config_mem = ioremap (value, 32768);
 
        dprintk (KERN_DEBUG "remapped hw_config_mem=%08x\n", (u32) *config_mem);
#if (LINUX_VERSION_CODE < 0x02030d)
        dprintk (KERN_DEBUG "dev->base_address[0]=%08x\n", (u32) dev->base_address[0]);
        dprintk (KERN_DEBUG "dev->base_address[1]=%08x\n", (u32) dev->base_address[1]);
        dprintk (KERN_DEBUG "dev->base_address[2]=%08x\n", (u32) dev->base_address[2]);
        dprintk (KERN_DEBUG "dev->base_address[3]=%08x\n", (u32) dev->base_address[3]);
        dprintk (KERN_DEBUG "dev->base_address[4]=%08x\n", (u32) dev->base_address[4]);
        dprintk (KERN_DEBUG "dev->base_address[5]=%08x\n", (u32) dev->base_address[5]);
#else
        dprintk (KERN_DEBUG "dev->base_address[0]=%08x\n", (u32) dev->resource[0].start);
        dprintk (KERN_DEBUG "dev->base_address[1]=%08x\n", (u32) dev->resource[1].start);
        dprintk (KERN_DEBUG "dev->base_address[2]=%08x\n", (u32) dev->resource[2].start);
        dprintk (KERN_DEBUG "dev->base_address[3]=%08x\n", (u32) dev->resource[3].start);
        dprintk (KERN_DEBUG "dev->base_address[4]=%08x\n", (u32) dev->resource[4].start);
        dprintk (KERN_DEBUG "dev->base_address[5]=%08x\n", (u32) dev->resource[5].start);
#endif

        return config_mem == NULL ? -ENOMEM : 0;
}

#define TRIES	8

static int
allocate_rxtx_buffers (int xpds_num, int num_rxtx, volatile void **rxtx_mem,
	volatile xpds_rxtx_list_t **rx_list,
	volatile xpds_rxtx_list_t **tx_list)
{
	int	size, i, j;
	volatile u8	*rx_buffer, *tx_buffer;
	volatile xpds_rxtx_list_t	*rx_ptr, *tx_ptr;
	volatile void	*old_mem[TRIES];

	size = (num_rxtx + num_rxtx) * sizeof (xpds_rxtx_list_t) +
		(num_rxtx + num_rxtx) * RXTX_BUFFER_SIZE;

	/*
	 * Prevent RX/TX memory from crossing a 16 bit page (64k
	 * byte) boundary.  This is due to a hardware limitation.
	 */
	memset (old_mem, 0, sizeof (old_mem));
	for (i = 0; i < TRIES; i ++) {
		u32	start, end;

		*rxtx_mem = kmalloc (size + 16, GFP_KERNEL /* | GFP_DMA */);
		if (*rxtx_mem == NULL) break;
		
		start = virt_to_bus (*rxtx_mem);
		end = start + size + 16;

		start &= 0xffff;
		end &= 0xffff;

		if (start < end) break;

		printk (KERN_DEBUG "%s: RX/TX buffer allocation crossed 16 bit boundary, reallocating.\n", xpds_devs[xpds_num].name);
		old_mem[i] = *rxtx_mem;
	}
	for (j = 0; j < i; j ++) {
		if (old_mem[j] != NULL) kfree ((void *)(old_mem[j]));
	}
	if (i == TRIES || *rxtx_mem == NULL) return -ENOMEM;

	xpds_data[xpds_num].rxtx_mem_allocated = *rxtx_mem;

	if ((u32)*rxtx_mem & 0xf) {
		*rxtx_mem = (volatile void *) (((u32) *rxtx_mem + 16) & 0xfffffff0);
	}

	*rx_list = (volatile xpds_rxtx_list_t *) *rxtx_mem;
	*tx_list = (volatile xpds_rxtx_list_t *) ((u32) *rxtx_mem +
		sizeof (xpds_rxtx_list_t) * num_rxtx);

	rx_buffer = (volatile u8 *) ((u32) *tx_list +
		sizeof (xpds_rxtx_list_t) * num_rxtx);
	tx_buffer = (volatile u8 *) ((u32) rx_buffer +
		RXTX_BUFFER_SIZE * num_rxtx);

	rx_ptr = *rx_list;
	tx_ptr = *tx_list;

	for (i = 0; i < num_rxtx; i ++) {
		rx_ptr->control = RXTX_CONTROL__NEXT_VALID | RXTX_CONTROL__HWGO;
		rx_ptr->buffer = rx_buffer + i * RXTX_BUFFER_SIZE;
		rx_ptr->buffer_bus_addr = virt_to_bus (rx_ptr->buffer);
		rx_ptr->next = (i == num_rxtx - 1) ?
			*rx_list : 
			(volatile xpds_rxtx_list_t *) ((u32) rx_ptr +
				sizeof (xpds_rxtx_list_t));
		rx_ptr->next_bus_addr = virt_to_bus (rx_ptr->next);
		rx_ptr->unused1 = 0;
		rx_ptr->prev = (i == 0) ?
			(volatile xpds_rxtx_list_t *) ((u32) *rx_list +
				sizeof (xpds_rxtx_list_t) * (num_rxtx - 1)) :
			(volatile xpds_rxtx_list_t *) ((u32) rx_ptr -
				sizeof (xpds_rxtx_list_t));
		rx_ptr->offset = 0;
		rx_ptr = rx_ptr->next;

		tx_ptr->control = RXTX_CONTROL__NEXT_VALID;
		tx_ptr->buffer = tx_buffer + i * RXTX_BUFFER_SIZE;
		tx_ptr->buffer_bus_addr = virt_to_bus (tx_ptr->buffer);
		tx_ptr->next = (i == num_rxtx - 1) ?
			*tx_list : 
			(volatile xpds_rxtx_list_t *) ((u32) tx_ptr +
				sizeof (xpds_rxtx_list_t));
		tx_ptr->next_bus_addr = virt_to_bus (tx_ptr->next);
		tx_ptr->unused1 = 0;
		tx_ptr->prev = (i == 0) ?
			(volatile xpds_rxtx_list_t *) ((u32) *tx_list +
				sizeof (xpds_rxtx_list_t) * (num_rxtx - 1)) :
			(volatile xpds_rxtx_list_t *) ((u32) tx_ptr -
				sizeof (xpds_rxtx_list_t));
		tx_ptr->offset = 0;
		tx_ptr = tx_ptr->next;
	}

	dprintk (KERN_DEBUG "rxtx_mem = %08x, rx_list = %08x, tx_list = %08x,\n",
		(u32) *rxtx_mem, (u32) *rx_list, (u32) *tx_list);
	dprintk (KERN_DEBUG "rx_buffer = %08x, tx_buffer = %08x\n",
		(u32) rx_buffer, (u32) tx_buffer);
	return 0;
}

/*
 * Given a pointer to memory, set the control register pointers.
 */
static int
set_control_register_pointers (int xpds_num, volatile void *mem,
	volatile xpds_rxtx_list_t *rx_list, volatile xpds_rxtx_list_t *tx_list)
{
	int	rc;

	dprintk (KERN_DEBUG "setting %s: control register pointers\n", xpds_devs[xpds_num].name);
	xpds_data[xpds_num].main_control_registers =
		(volatile u32 *) ( (volatile u8 *) mem + 0x0000 );
	xpds_data[xpds_num].rx_fifo_control_registers =
		(volatile u32 *) ( (volatile u8 *) mem + 0x0050 );
	xpds_data[xpds_num].tx_fifo_control_registers =
		(volatile u32 *) ( (volatile u8 *) mem + 0x0040 );
	xpds_data[xpds_num].rx_dma_control_registers =
		(volatile u32 *) ( (volatile u8 *) mem + 0x00c0 );
	xpds_data[xpds_num].tx_dma_control_registers =
		(volatile u32 *) ( (volatile u8 *) mem + 0x0080 );
	xpds_data[xpds_num].rx_fifo_data_registers =
		(volatile u32 *) ( (volatile u8 *) mem +
		(xpds_data[xpds_num].is_fpga ? 0x0600 : 0x0500) );
	xpds_data[xpds_num].tx_fifo_data_registers =
		(volatile u32 *) ( (volatile u8 *) mem + 0x0400 );
	xpds_data[xpds_num].aux_registers =
		(volatile u32 *) ( (volatile u8 *) mem + 0x0060 );
	dprintk (KERN_DEBUG "memory=%08x\n", (u32)mem);
	dprintk (KERN_DEBUG "main_control_registers=%08x\n",
		(u32)(xpds_data[xpds_num].main_control_registers));
	dprintk (KERN_DEBUG "rx_fifo_control_registers=%08x\n",
		(u32)(xpds_data[xpds_num].rx_fifo_control_registers));
	dprintk (KERN_DEBUG "tx_fifo_control_registers=%08x\n",
		(u32)(xpds_data[xpds_num].tx_fifo_control_registers));
	dprintk (KERN_DEBUG "rx_dma_control_registers=%08x\n",
		(u32)(xpds_data[xpds_num].rx_dma_control_registers));
	dprintk (KERN_DEBUG "tx_dma_control_registers=%08x\n",
		(u32)(xpds_data[xpds_num].tx_dma_control_registers));
	dprintk (KERN_DEBUG "rx_fifo_data_registers=%08x\n",
		(u32)(xpds_data[xpds_num].rx_fifo_data_registers));
	dprintk (KERN_DEBUG "tx_fifo_data_registers=%08x\n",
		(u32)(xpds_data[xpds_num].tx_fifo_data_registers));
	dprintk (KERN_DEBUG "aux_registers=%08x\n",
		(u32)(xpds_data[xpds_num].aux_registers));

	/*
	 * Install the buffer lists for RX and TX DMA.
	 */
	xpds_data[xpds_num].rx_dma_list = rx_list;
	dprintk (KERN_DEBUG "initializing %s: RX DMA Status Address:\n", xpds_devs[xpds_num].name);
	rc = xpds_write_control_register (xpds_num, XPDS_DMA_STAT_ADDR,
		(u32) rx_list, RX_DMA);
	if (rc != 0) return rc;
	dprintk ("\n");
	xpds_data[xpds_num].tx_dma_list = tx_list;
	dprintk (KERN_DEBUG "initializing %s: TX DMA Status Address:\n", xpds_devs[xpds_num].name);
	rc = xpds_write_control_register (xpds_num, XPDS_DMA_STAT_ADDR,
		(u32) tx_list, TX_DMA);
	dprintk ("\n");
	if (rc != 0) return rc;

	return 0;
}

static int
xpds_alloc_data (void)
{
	int	rc;

	if (xpds_data == NULL) {
		xpds_data = kmalloc (sizeof (*xpds_data) * xpds_max_cards, GFP_KERNEL);
		if (xpds_data == NULL) return -ENOMEM;
		memset (xpds_data, 0, sizeof (*xpds_data) * xpds_max_cards);
	}
	if (xpds_names == NULL) {
		xpds_names = kmalloc (NAME_SIZE * xpds_max_cards, GFP_KERNEL);
		if (xpds_names == NULL) return -ENOMEM;
		memset (xpds_names, 0, NAME_SIZE * xpds_max_cards);
	}
	if (xpds_reset_bh_tasks == NULL) {
		xpds_reset_bh_tasks = kmalloc (sizeof (*xpds_reset_bh_tasks) * xpds_max_cards, GFP_KERNEL);
		if (xpds_reset_bh_tasks == NULL) return -ENOMEM;
		memset (xpds_reset_bh_tasks, 0, sizeof (*xpds_reset_bh_tasks) * xpds_max_cards);
	}
	if (xpds_interrupt_bh_tasks == NULL) {
		int	i;
		xpds_interrupt_bh_tasks = kmalloc (sizeof (*xpds_interrupt_bh_tasks) * xpds_max_cards, GFP_KERNEL);
		if (xpds_interrupt_bh_tasks == NULL) return -ENOMEM;
		memset (xpds_interrupt_bh_tasks, 0, sizeof (*xpds_interrupt_bh_tasks) * xpds_max_cards);
		for (i = 0; i < xpds_max_cards; i ++) {
			xpds_interrupt_bh_tasks[i].data = kmalloc (sizeof (interrupt_bh_data_t), GFP_KERNEL);
			xpds_interrupt_bh_tasks[i].next = NULL;
			xpds_interrupt_bh_tasks[i].sync = 0;
			xpds_interrupt_bh_tasks[i].routine = xpds_interrupt_bh;
		}
	}
	if (xpds_devs == NULL) {
		int	i;
		xpds_devs = kmalloc (sizeof (*xpds_devs) * xpds_max_cards, GFP_KERNEL);
		if (xpds_devs == NULL) return -ENOMEM;
		memset (xpds_devs, 0, sizeof (*xpds_devs) * xpds_max_cards);

		for (i = 0; i < xpds_max_cards; i ++) {
#if (LINUX_VERSION_CODE < HAS_SOFT_NET)
                        xpds_devs[i].name = xpds_names + NAME_SIZE * i;
#endif
			xpds_devs[i].init = xpds_init;
		}
	}
	rc = xpds_sdsl_allocate ();
	if (rc) return rc;
	return 0;
}

static void
xpds_free_data (void)
{
	if (xpds_data != NULL) kfree (xpds_data);
	if (xpds_names != NULL) kfree (xpds_names);
	if (xpds_reset_bh_tasks != NULL) kfree (xpds_reset_bh_tasks);
	if (xpds_devs != NULL) kfree (xpds_devs);
	xpds_sdsl_cleanup ();
}

#ifdef MODULE
int init_module (void)
#else
int xpdsl_init(void)
#endif
{
	int		i;
	int		rc;
	volatile xpds_rxtx_list_t	*rx_list, *tx_list;
	volatile void	*rxtx_mem;
	int		found_sdsl = 0;
	struct pci_dev	*dev = NULL;
#if ALLOW_OLD_PCI_VENDOR_ID
	int		old_offset = -1;
#endif

	printk (KERN_NOTICE "xpds.c: Xpeed XPDS frame relay driver %s, linux@xpeed.com, Copyright 1999, 2000 Xpeed, Inc.\n", VERSION_STRING);
	rc = xpds_alloc_data ();
	if (rc != 0) {
		printk (KERN_ERR "Unable to allocate memory for XPDS data.\n");
		xpds_free_data ();
		return rc;
	}

	dprintk (KERN_DEBUG "xpds_data = %p, &(xpds_data[0].rxci_interrupt_received) = %p\n", xpds_data, &(xpds_data[0].rxci_interrupt_received));

	/*
	 * Make sure that there is a PCI present, and the
	 * desired device is there.
	 */
	if ( ! pci_present ()) {
		printk (KERN_ERR "No PCI present.\n");
		xpds_free_data ();
		return -ENODEV;
	}

	for (i = 0; i < xpds_max_cards; i ++) {
		char	devname[NAME_SIZE], name[NAME_SIZE];
		int	n;

		sprintf (devname, "%.*s%%d", NAME_SIZE - 4, xpds_dev_name);
		n = dev_alloc_name (&(xpds_devs[i]), devname);
		if (n < 0) {
			printk(KERN_ERR "%s: dev_alloc_name failed (%d)\n", xpds_dev_name, n);
			xpds_free_data ();
			return -ENODEV;
		}
		sprintf (name, "%.*s%d", NAME_SIZE - 4, xpds_dev_name, n);
		strcpy (xpds_devs[i].name, name);
	}

	for (i = 0; i < xpds_max_cards; i ++) {
		memset (&(xpds_data[i]), 0, sizeof (xpds_data[i]));
		xpds_data[i].speed_mode = xpds_mode;

#if ALLOW_OLD_PCI_VENDOR_ID
		if (old_offset < 0) {
			dev = pci_find_device (PCI_VENDOR_ID_XPDS,
				PCI_DEVICE_ID_XPDS_1, dev);
			if (dev == NULL) {
				old_offset = i;
				dev = pci_find_device (PCI_VENDOR_ID_XPDS_OLD,
					PCI_DEVICE_ID_XPDS_1, dev);
			}
		} else {
			dev = pci_find_device (PCI_VENDOR_ID_XPDS_OLD,
				PCI_DEVICE_ID_XPDS_1, dev);
		}
#else
		dev = pci_find_device (PCI_VENDOR_ID_XPDS,
			PCI_DEVICE_ID_XPDS_1, dev);
#endif
		if (dev == NULL) break;
		dprintk (KERN_DEBUG "dev = %p\n", dev);

		xpds_data[i].pci_dev = dev;

		pci_set_master (dev);

#if ALLOW_OLD_PCI_VENDOR_ID
		if (old_offset >= 0) {
			dprintk (KERN_DEBUG "XPDS FPGA device %d at pci_bus=0x%02x, pci_dev_fn=0x%02x, pci_irq_line=0x%02x.\n",
				i,
				xpds_data[i].pci_dev->bus->number,
				xpds_data[i].pci_dev->devfn,
				xpds_data[i].pci_dev->irq);
			xpds_data[i].is_fpga = 1;
		} else {
			dprintk (KERN_DEBUG "XPDS device %d found at pci_bus=0x%02x, pci_dev_fn=0x%02x, pci_irq_line=0x%02x.\n",
				i,
				xpds_data[i].pci_dev->bus->number,
				xpds_data[i].pci_dev->devfn,
				xpds_data[i].pci_dev->irq);
			xpds_data[i].is_fpga = 0;
		}
#else
		dprintk (KERN_DEBUG "XPDS device %d found at pci_bus=0x%02x, pci_dev_fn=0x%02x, pci_irq_line=0x%02x.\n",
			i,
			xpds_data[i].pci_dev->bus->number,
			xpds_data[i].pci_dev->devfn,
			xpds_data[i].pci_dev->irq);
		xpds_data[i].is_fpga = 0;
#endif
		
		/*
		 * Get the 32k of memory from hardware.
		 */
		dprintk (KERN_DEBUG "%s: setting up PCI configuration\n", xpds_devs[i].name);
		rc = get_pci_config (xpds_data[i].pci_dev,
			&(xpds_data[i].config_mem_remapped));
		if (rc != 0) {
			xpds_free_data ();
			return rc;
		}

		/*
		 * Allocate the RX and TX lists and buffers.
		 */
		dprintk (KERN_DEBUG "%s: allocating RX/TX buffers\n", xpds_devs[i].name);
		rc = allocate_rxtx_buffers (i, NUM_DESC, &rxtx_mem,
			&rx_list, &tx_list);
		if (rc != 0) {
			xpds_free_data ();
			return rc;
		}

		/*
		 * Set up the control register pointers to the RX/TX buffers.
		 */
		rc = set_control_register_pointers (i,
			xpds_data[i].config_mem_remapped, rx_list, tx_list);
		if (rc != 0) {
			xpds_free_data ();
			return rc;
		}

		/*
		 * Read the serial EPROM for some more information
		 * about the device.  May set xpds_data[i].is_sdsl or
		 * xpds_data[i].has_last_byte_bug .
		 */
		read_seprom_info (i);

		/*
		 * If the serial EPROM read failed, then it should be
		 * an SDSL device.  Read the information from the SDSL
		 * device.  If an error, then something is wrong...
		 */
		if (xpds_data[i].is_sdsl) {
			xpds_reset_sdsl (i);
			xpds_start_sdsl (i);
			DELAY_HZ (3 * HZ / 2, i);
			rc = read_sdsl_info (i);
			if (rc != 0) {
				if (xpds_data[i].is_sdsl) {
					printk (KERN_ERR "%s: Unable to determine if %s is an IDSL or SDSL card.\n", xpds_devs[i].name, xpds_devs[i].name);
					printk (KERN_ERR "%s: %s may be defective\n", xpds_devs[i].name, xpds_devs[i].name);
					if (! xpds_load_for_flash) {
						xpds_free_data ();
						return -ENODEV;
					}
				} else {
					/* FPGA IDSL board */
					printk (KERN_DEBUG "%s: %s has no SDSL infomation.\n", xpds_devs[i].name, xpds_devs[i].name);
					printk (KERN_DEBUG "%s: is assumed to be an FPGA IDSL card.\n", xpds_devs[i].name);
				}
			}
		}

		/*
		 * Hardware bugs:
		 * ASIC < 1.1:  last byte corruption bug
		 * hardware < 1.1:  RX DMA burst bug
		 * hardware < 1.2:  TX DMA burst bug
		 */
		if (! xpds_data[i].is_fpga && ! xpds_is_hardware_version (i, 1, 1)) {
			dprintk (KERN_DEBUG "Has last byte bug, using workaround.\n");
			xpds_data[i].has_last_byte_bug = 1;
		}

		if (! xpds_is_hardware_version (i, 1, 1)) {
			dprintk (KERN_DEBUG "Has RX DMA burst bug, not using RX DMA burst mode.\n");
			xpds_data[i].has_rx_dma_burst_bug = 1;
		}

		if (! xpds_is_hardware_version (i, 1, 2)) {
			dprintk (KERN_DEBUG "Has TX DMA burst bug, not using TX DMA burst mode.\n");
			xpds_data[i].has_tx_dma_burst_bug = 1;
			dprintk (KERN_DEBUG "Has TX DMA low rate (<%d Kbps) bug.\n", LOW_BIT_RATE);
			xpds_data[i].has_tx_dma_low_rate_bug = 1;
		}

		printk (KERN_DEBUG "%s: Xpeed %c00 %cDSL NIC, %02X %02X %02X %02X %02X %02X, IRQ %d.\n",
			xpds_devs[i].name,
			xpds_data[i].is_sdsl ? '3' : '2',
			xpds_data[i].is_sdsl ? 'S' : 'I',
			xpds_data[i].serial_data.mac_address[0],
			xpds_data[i].serial_data.mac_address[1],
			xpds_data[i].serial_data.mac_address[2],
			xpds_data[i].serial_data.mac_address[3],
			xpds_data[i].serial_data.mac_address[4],
			xpds_data[i].serial_data.mac_address[5],
			xpds_data[i].pci_dev->irq);

		if (xpds_data[i].is_sdsl) {
			int	rc;
			u32	mode, speed, swap, invert, nt;

			rc = xpds_get_flash_sdsl_mode (i, &mode);

			if (rc) {
				printk (KERN_ERR "%s: unable to get speed, swap, and invert\n", xpds_devs[i].name);
			} else {
				speed = (mode & XPDS_SDSL_MODE__SPEED_MASK) << 3;
				nt = (mode & XPDS_SDSL_MODE__NT);
				swap = (mode & XPDS_SDSL_MODE__SWAP);
				invert = (mode & XPDS_SDSL_MODE__INVERT);
				printk (KERN_DEBUG "%s: speed = %d%s, swap %s, invert %s, %s mode\n", xpds_devs[i].name, speed, (speed == 0) ? " (CM auto)" : "", swap ? "on" : "off", invert ? "on" : "off", nt ? "NT" : "LT");
			}
		}

		if (xpds_data[i].is_sdsl) found_sdsl = 1;

		/*
		 * Register network device.
		 */
		rc = register_netdev (&(xpds_devs[i]));
		if (rc != 0) {
			xpds_free_data ();
			return rc;
		}
	}

	num_xpds_found = i;

	if (num_xpds_found < 1) {
		printk (KERN_ERR "PCI error:  XPDS device not found.\n");
		xpds_free_data ();
		return -ENODEV;
	}

	return 0;
}

void cleanup_module (void)
{
	int	i;

	for (i = 0; i < num_xpds_found; i ++) {
		if (xpds_data[i].rxtx_mem_allocated != NULL) {
			kfree ((void *)(xpds_data[i].rxtx_mem_allocated));
		}
		if (xpds_data[i].config_mem_remapped != NULL) {
			iounmap ((void *)(xpds_data[i].config_mem_remapped));
		}
		dprintk (KERN_DEBUG "unregistering XPDS network device %d\n", i);
		unregister_netdev (&(xpds_devs[i]));
	}
	xpds_free_data ();
	dprintk (KERN_DEBUG "removing Xpeed XPDS frame relay driver\n");
}
