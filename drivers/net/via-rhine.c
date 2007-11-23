/* via-rhine.c: A Linux Ethernet device driver for VIA Rhine family chips. */
/*
	Written 1998-2000 by Donald Becker.

	This software may be used and distributed according to the terms of
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice.  This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	This driver is designed for the VIA VT86c100A Rhine-II PCI Fast Ethernet
	controller.  It also works with the older 3043 Rhine-I chip.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support and updates available at
	http://www.scyld.com/network/via-rhine.html


	Linux kernel version history:

	LK1.0.0:
	- Urban Widmark: merges from Beckers 1.08b version and 2.4.0 (VT6102)

	LK1.0.1
	- Dennis Björklund: backport tx_timeout from 2.4.x to reset on timeout
                        instead of stop working...
*/

/* These identify the driver base version and may not be removed. */
static const char version1[] =
"via-rhine.c:v1.08b-LK1.0.1 12/14/2000  Written by Donald Becker\n";
static const char version2[] =
"  http://www.scyld.com/network/via-rhine.html\n";

/* The user-configurable values.
   These may be modified when a driver module is loaded.*/

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */
static int max_interrupt_work = 20;
static int min_pci_latency = 32;

/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1518 effectively disables this feature. */
static int rx_copybreak = 0;

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' should exist for driver
   interoperability.
   The media type is usually passed in 'options[]'.
*/
#define MAX_UNITS 8		/* More are supported, limit only on options */
static int options[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* Maximum number of multicast addresses to filter (vs. rx-all-multicast).
   The Rhine has a 64 element 8390-like hash table.  */
static const int multicast_filter_limit = 32;

/* Operational parameters that are set at compile time. */

/* Keep the ring sizes a power of two for compile efficiency.
   The compiler will convert <unsigned>'%'<2^N> into a bit mask.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define TX_QUEUE_LEN	10		/* Limit ring entries actually used.  */
#define RX_RING_SIZE	16

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/


#if !defined(__OPTIMIZE__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error  You must compile this driver with "-O".
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>

/* Condensed bus+endian portability operations. */
#define virt_to_le32desc(addr) cpu_to_le32(virt_to_bus(addr))
#define le32desc_to_virt(addr) bus_to_virt(le32_to_cpu(addr))

/* This driver was written to use PCI memory space, however most versions
   of the Rhine only work correctly with I/O space accesses. */
#if defined(VIA_USE_MEMORY)
#warning Many adapters using the VIA Rhine chip are not configured to work
#warning with PCI memory space accesses.
#else
#define USE_IO_OPS
#undef readb
#undef readw
#undef readl
#undef writeb
#undef writew
#undef writel
#define readb inb
#define readw inw
#define readl inl
#define writeb outb
#define writew outw
#define writel outl
#endif

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("VIA Rhine PCI Fast Ethernet driver");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(min_pci_latency, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");

#define NETSTATS_VER2

/*
				Theory of Operation

I. Board Compatibility

This driver is designed for the VIA 86c100A Rhine-II PCI Fast Ethernet
controller.

II. Board-specific settings

Boards with this chip are functional only in a bus-master PCI slot.

Many operational settings are loaded from the EEPROM to the Config word at
offset 0x78.  This driver assumes that they are correct.
If this driver is compiled to use PCI memory space operations the EEPROM
must be configured to enable memory ops.

III. Driver operation

IIIa. Ring buffers

This driver uses two statically allocated fixed-size descriptor lists
formed into rings by a branch from the final descriptor to the beginning of
the list.  The ring sizes are set at compile time by RX/TX_RING_SIZE.

IIIb/c. Transmit/Receive Structure

This driver attempts to use a zero-copy receive and transmit scheme.

Alas, all data buffers are required to start on a 32 bit boundary, so
the driver must often copy transmit packets into bounce buffers.

The driver allocates full frame size skbuffs for the Rx ring buffers at
open() time and passes the skb->data field to the chip as receive data
buffers.  When an incoming frame is less than RX_COPYBREAK bytes long,
a fresh skbuff is allocated and the frame is copied to the new skbuff.
When the incoming frame is larger, the skbuff is passed directly up the
protocol stack.  Buffers consumed this way are replaced by newly allocated
skbuffs in the last phase of netdev_rx().

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  New boards are typically used in generously configured machines
and the underfilled buffers have negligible impact compared to the benefit of
a single allocation size, so the default value of zero results in never
copying packets.  When copying is done, the cost is usually mitigated by using
a combined copy/checksum routine.  Copying also preloads the cache, which is
most useful with small frames.

Since the VIA chips are only able to transfer data to buffers on 32 bit
boundaries, the the IP header at offset 14 in an ethernet frame isn't
longword aligned for further processing.  Copying these unaligned buffers
has the beneficial effect of 16-byte aligning the IP header.

IIId. Synchronization

The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and interrupt handling software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'lp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  After reaping the stats, it marks the Tx queue entry as
empty by incrementing the dirty_tx mark. Iff the 'lp->tx_full' flag is set, it
clears both the tx_full and tbusy flags.

IV. Notes

IVb. References

Preliminary VT86C100A manual from http://www.via.com.tw/
http://www.scyld.com/expert/100mbps.html
http://www.scyld.com/expert/NWay.html

IVc. Errata

The VT86C100A manual is not reliable information.
The 3043 chip does not handle unaligned transmit or receive buffers, resulting
in significant performance degradation for bounce buffer copies on transmit
and unaligned IP headers on receive.
The chip does not pad to minimum transmit length.

*/



/* This table drives the PCI probe routines.  It's mostly boilerplate in all
   of the drivers, and will likely be provided by some future kernel.
   Note the matching code -- the first table entry matchs all 56** cards but
   second only the 1234 card.
*/
enum pci_flags_bit {
	PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
	PCI_ADDR0=0x10<<0, PCI_ADDR1=0x10<<1, PCI_ADDR2=0x10<<2, PCI_ADDR3=0x10<<3,
};

#if defined(VIA_USE_MEMORY)
#define RHINE_IOTYPE (PCI_USES_MEM | PCI_USES_MASTER | PCI_ADDR0)
#define RHINEII_IOSIZE 4096
#else
#define RHINE_IOTYPE (PCI_USES_IO  | PCI_USES_MASTER | PCI_ADDR1)
#define RHINEII_IOSIZE 256
#endif

struct pci_id_info {
	const char *name;
	u16	vendor_id, device_id, device_id_mask, flags;
	int io_size;
	struct device *(*probe1)(int pci_bus, int pci_devfn, struct device *dev,
							 long ioaddr, int irq, int chip_idx, int fnd_cnt);
};

static struct device *via_probe1(int pci_bus, int pci_devfn,
								 struct device *dev, long ioaddr, int irq,
								 int chp_idx, int fnd_cnt);

enum via_rhine_chips {
	VT86C100A = 0,
	VT6102,
	VT3043,
};

/* directly indexed by enum via_rhine_chips, above */
static struct pci_id_info pci_tbl[] __initdata = {
	{ "VIA VT86C100A Rhine-II", 0x1106, 0x6100, 0xffff,
	  RHINE_IOTYPE, 128, via_probe1},
	{ "VIA VT6102 Rhine-II", 0x1106, 0x3065, 0xffff,
	  RHINE_IOTYPE, RHINEII_IOSIZE, via_probe1},
	{ "VIA VT3043 Rhine", 0x1106, 0x3043, 0xffff,
	  RHINE_IOTYPE, 128, via_probe1},
	{0,},						/* 0 terminated list. */
};


/* A chip capabilities table, matching the entries in pci_tbl[] above. */
enum chip_capability_flags {
	CanHaveMII=1, HasESIPhy=2, HasDavicomPhy=4,
	ReqTxAlign=0x10, HasWOL=0x20,
};

struct chip_info {
	int io_size;
	int flags;
} static cap_tbl[] __initdata = {
	{128, CanHaveMII | ReqTxAlign, },
	{128, CanHaveMII | HasWOL, },
	{128, CanHaveMII | ReqTxAlign, },
};


/* Offsets to the device registers. */
enum register_offsets {
	StationAddr=0x00, RxConfig=0x06, TxConfig=0x07, ChipCmd=0x08,
	IntrStatus=0x0C, IntrEnable=0x0E,
	MulticastFilter0=0x10, MulticastFilter1=0x14,
	RxRingPtr=0x18, TxRingPtr=0x1C,
	MIIPhyAddr=0x6C, MIIStatus=0x6D, PCIBusConfig=0x6E,
	MIICmd=0x70, MIIRegAddr=0x71, MIIData=0x72,
	Config=0x78, ConfigA=0x7A, RxMissed=0x7C, RxCRCErrs=0x7E,
	StickyHW=0x83, WOLcrClr=0xA4, WOLcgClr=0xA7, PwrcsrClr=0xAC,
};

/* Bits in the interrupt status/mask registers. */
enum intr_status_bits {
	IntrRxDone=0x0001, IntrRxErr=0x0004, IntrRxEmpty=0x0020,
	IntrTxDone=0x0002, IntrTxAbort=0x0008, IntrTxUnderrun=0x0010,
	IntrPCIErr=0x0040,
	IntrStatsMax=0x0080, IntrRxEarly=0x0100, IntrMIIChange=0x0200,
	IntrRxOverflow=0x0400, IntrRxDropped=0x0800, IntrRxNoBuf=0x1000,
	IntrTxAborted=0x2000, IntrLinkChange=0x4000,
	IntrRxWakeUp=0x8000,
	IntrNormalSummary=0x0003, IntrAbnormalSummary=0xC260,
};

/* The Rx and Tx buffer descriptors. */
struct rx_desc {
	s32 rx_status;
	u32 desc_length;
	u32 addr;
	u32 next_desc;
};
struct tx_desc {
	s32 tx_status;
	u32 desc_length;
	u32 addr;
	u32 next_desc;
};

/* Bits in *_desc.status */
enum rx_status_bits {
	RxOK=0x8000, RxWholePkt=0x0300, RxErr=0x008F
};
enum desc_status_bits {
	DescOwn=0x80000000, DescEndPacket=0x4000, DescIntr=0x1000,
};

/* Bits in ChipCmd. */
enum chip_cmd_bits {
	CmdInit=0x0001, CmdStart=0x0002, CmdStop=0x0004, CmdRxOn=0x0008,
	CmdTxOn=0x0010, CmdTxDemand=0x0020, CmdRxDemand=0x0040,
	CmdEarlyRx=0x0100, CmdEarlyTx=0x0200, CmdFDuplex=0x0400,
	CmdNoTxPoll=0x0800, CmdReset=0x8000,
};

#define PRIV_ALIGN	15	/* Required alignment mask */
struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct rx_desc rx_ring[RX_RING_SIZE];
	struct tx_desc tx_ring[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for later free(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	unsigned char *tx_buf[TX_RING_SIZE];	/* Tx bounce buffers */
	unsigned char *tx_bufs;				/* Tx bounce buffer region. */
	struct device *next_module;			/* Link for devices of this type. */
	void *priv_addr;					/* Unaligned address for kfree */
	struct net_device_stats stats;
	struct timer_list timer;	/* Media monitoring timer. */
	unsigned char pci_bus, pci_devfn;
	/* Frequently used values: keep some adjacent for cache effect. */
	int chip_id, drv_flags;
	struct rx_desc *rx_head_desc;
	unsigned int cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	unsigned int cur_tx, dirty_tx;
	unsigned int rx_buf_sz;				/* Based on MTU+slack. */
	u16 chip_cmd;						/* Current setting for ChipCmd */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	/* These values are keep track of the transceiver/media in use. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int duplex_lock:1;
	unsigned int medialock:1;			/* Do not sense media. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	u8 tx_thresh, rx_thresh;
	/* MII transceiver section. */
	int mii_cnt;						/* MII device addresses. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[2];				/* MII device addresses. */
};

static int  mdio_read(struct device *dev, int phy_id, int location);
static void mdio_write(struct device *dev, int phy_id, int location, int value);
static int  netdev_open(struct device *dev);
static void check_duplex(struct device *dev);
static void netdev_timer(unsigned long data);
static void tx_timeout(struct device *dev);
static int  start_tx(struct sk_buff *skb, struct device *dev);
static void intr_handler(int irq, void *dev_instance, struct pt_regs *regs);
static int  netdev_rx(struct device *dev);
static void netdev_error(struct device *dev, int intr_status);
static void set_rx_mode(struct device *dev);
static struct net_device_stats *get_stats(struct device *dev);
static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd);
static int  netdev_close(struct device *dev);
static inline void clear_tally_counters(long ioaddr);



/* A list of our installed devices, for removing the driver module. */
static struct device *root_net_dev = NULL;

static void wait_for_reset(struct device *dev, char *name)
{
	struct netdev_private *np = dev->priv;
	long ioaddr = dev->base_addr;
	int chip_id = np->chip_id;
	int i;

	/* 3043 may need long delay after reset (dlink) */
	if (chip_id == VT3043 || chip_id == VT86C100A)
		udelay(100);

	i = 0;
	do {
		udelay(5);
		i++;
		if(i > 2000) {
			printk(KERN_ERR "%s: reset did not complete in 10 ms.\n", name);
			break;
		}
	} while(readw(ioaddr + ChipCmd) & CmdReset);
	if (debug > 1)
		printk(KERN_INFO "%s: reset finished after %d microseconds.\n",
			   name, 5*i);
}

/* Ideally we would detect all network cards in slot order.  That would
   be best done a central PCI probe dispatch, which wouldn't work
   well when dynamically adding drivers.  So instead we detect just the
   cards we know about in slot order. */

static int __init pci_etherdev_probe(struct device *dev, struct pci_id_info pci_tbl[])
{
	int cards_found = 0;
	int pci_index = 0;
	unsigned char pci_bus, pci_device_fn;

	if ( ! pcibios_present())
		return -ENODEV;

	for (;pci_index < 0xff; pci_index++) {
		u16 vendor, device, pci_command, new_command;
		int chip_idx, irq;
		long pciaddr;
		long ioaddr;

		if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8, pci_index,
								&pci_bus, &pci_device_fn)
			!= PCIBIOS_SUCCESSFUL)
			break;
		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_VENDOR_ID, &vendor);
		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_DEVICE_ID, &device);

		for (chip_idx = 0; pci_tbl[chip_idx].vendor_id; chip_idx++)
			if (vendor == pci_tbl[chip_idx].vendor_id
				&& (device & pci_tbl[chip_idx].device_id_mask) ==
				pci_tbl[chip_idx].device_id)
				break;
		if (pci_tbl[chip_idx].vendor_id == 0) 		/* Compiled out! */
			continue;

		{
			struct pci_dev *pdev = pci_find_slot(pci_bus, pci_device_fn);
#ifdef USE_IO_OPS
			pciaddr = pdev->base_address[0];
#else
			pciaddr = pdev->base_address[1];
#endif
			irq = pdev->irq;
		}

		if (debug > 2)
			printk(KERN_INFO "Found %s at PCI address %#lx, IRQ %d.\n",
				   pci_tbl[chip_idx].name, pciaddr, irq);

		if (pci_tbl[chip_idx].flags & PCI_USES_IO) {
			ioaddr = pciaddr & ~3;
			if (check_region(ioaddr, pci_tbl[chip_idx].io_size))
				continue;
		} else if ((ioaddr = (long)ioremap(pciaddr & ~0xf,
										 pci_tbl[chip_idx].io_size)) == 0) {
			printk(KERN_INFO "Failed to map PCI address %#lx.\n",
				   pciaddr);
			continue;
		}

		pcibios_read_config_word(pci_bus, pci_device_fn,
								 PCI_COMMAND, &pci_command);
		new_command = pci_command | (pci_tbl[chip_idx].flags & 7);
		if (pci_command != new_command) {
			printk(KERN_INFO "  The PCI BIOS has not enabled the"
				   " device at %d/%d!  Updating PCI command %4.4x->%4.4x.\n",
				   pci_bus, pci_device_fn, pci_command, new_command);
			pcibios_write_config_word(pci_bus, pci_device_fn,
									  PCI_COMMAND, new_command);
		}

		dev = pci_tbl[chip_idx].probe1(pci_bus, pci_device_fn, dev, ioaddr,
									   irq, chip_idx, cards_found);

		if (dev  && (pci_tbl[chip_idx].flags & PCI_COMMAND_MASTER)) {
			u8 pci_latency;
			pcibios_read_config_byte(pci_bus, pci_device_fn,
									 PCI_LATENCY_TIMER, &pci_latency);
			if (pci_latency < min_pci_latency) {
				printk(KERN_INFO "  PCI latency timer (CFLT) is "
					   "unreasonably low at %d.  Setting to %d clocks.\n",
					   pci_latency, min_pci_latency);
				pcibios_write_config_byte(pci_bus, pci_device_fn,
										  PCI_LATENCY_TIMER, min_pci_latency);
			}
		}
		dev = 0;
		cards_found++;
	}

	return cards_found ? 0 : -ENODEV;
}

#ifndef MODULE
int __init via_rhine_probe(struct device *dev)
{
	static int did_version = 0;
	if (!did_version++)
		printk(KERN_INFO "%s" KERN_INFO "%s", version1, version2);
	return pci_etherdev_probe(dev, pci_tbl);
}
#endif

static struct device * __init via_probe1(int pci_bus, int pci_devfn,
								 struct device *dev, long ioaddr, int irq,
								 int chip_id, int card_idx)
{
	struct netdev_private *np;
	void *priv_mem;
	int i, option = card_idx < MAX_UNITS ? options[card_idx] : 0;

	dev = init_etherdev(dev, 0);

	printk(KERN_INFO "%s: %s at 0x%lx, ",
		   dev->name, pci_tbl[chip_id].name, ioaddr);

	/* We would prefer to read the EEPROM but access may be locked. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = readb(ioaddr + StationAddr + i);
	for (i = 0; i < 5; i++)
			printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], irq);

	/* Allocate driver private memory for descriptor lists.
	   Check for the very unlikely case of no memory. */
	priv_mem = kmalloc(sizeof(*np) + PRIV_ALIGN, GFP_KERNEL);
	if (priv_mem == NULL)
		return NULL;

#ifdef USE_IO_OPS
	request_region(ioaddr, pci_tbl[chip_id].io_size, dev->name);
#endif

	/* Reset the chip to erase previous misconfiguration. */
	writew(CmdReset, ioaddr + ChipCmd);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Make certain the descriptor lists are cache-aligned. */
	dev->priv = np = (void *)(((long)priv_mem + PRIV_ALIGN) & ~PRIV_ALIGN);
	memset(np, 0, sizeof(*np));
	np->priv_addr = priv_mem;

	np->next_module = root_net_dev;
	root_net_dev = dev;

	np->pci_bus = pci_bus;
	np->pci_devfn = pci_devfn;
	np->chip_id = chip_id;
	np->drv_flags = cap_tbl[chip_id].flags;

	if (dev->mem_start)
		option = dev->mem_start;

	/* The lower four bits are the media type. */
	if (option > 0) {
		if (option & 0x200)
			np->full_duplex = 1;
		np->default_port = option & 15;
		if (np->default_port)
			np->medialock = 1;
	}
	if (card_idx < MAX_UNITS  &&  full_duplex[card_idx] > 0)
		np->full_duplex = 1;

	if (np->full_duplex)
		np->duplex_lock = 1;

	/* The chip-specific entries in the device structure. */
	dev->open = &netdev_open;
	dev->hard_start_xmit = &start_tx;
	dev->stop = &netdev_close;
	dev->get_stats = &get_stats;
	dev->set_multicast_list = &set_rx_mode;
	dev->do_ioctl = &mii_ioctl;

	if (np->drv_flags & CanHaveMII) {
		int phy, phy_idx = 0;
		np->phys[0] = 1;		/* Standard for this chip. */
		for (phy = 1; phy < 32 && phy_idx < 4; phy++) {
			int mii_status = mdio_read(dev, phy, 1);
			if (mii_status != 0xffff  &&  mii_status != 0x0000) {
				np->phys[phy_idx++] = phy;
				np->advertising = mdio_read(dev, phy, 4);
				printk(KERN_INFO "%s: MII PHY found at address %d, status "
					   "0x%4.4x advertising %4.4x Link %4.4x.\n",
					   dev->name, phy, mii_status, np->advertising,
					   mdio_read(dev, phy, 5));
			}
		}
		np->mii_cnt = phy_idx;
	}

	return dev;
}

static void alloc_rbufs(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	np->cur_rx = 0;
	np->dirty_rx = 0;

	np->rx_buf_sz = (dev->mtu <= 1500 ? PKT_BUF_SZ : dev->mtu + 32);
	np->rx_head_desc = &np->rx_ring[0];

	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].desc_length = cpu_to_le32(np->rx_buf_sz);
		np->rx_ring[i].next_desc = virt_to_le32desc(&np->rx_ring[i+1]);
		np->rx_skbuff[i] = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	np->rx_ring[i-1].next_desc = virt_to_le32desc(&np->rx_ring[0]);

	/* Fill in the Rx buffers.  Handle allocation failure gracefully. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = dev_alloc_skb(np->rx_buf_sz);
		np->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		np->rx_ring[i].addr = virt_to_le32desc(skb->tail);
		np->rx_ring[i].rx_status = cpu_to_le32(DescOwn);
	}
	np->dirty_rx = (unsigned int)(i - RX_RING_SIZE);
}

static void free_rbufs(struct device* dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		np->rx_ring[i].rx_status = 0;
		np->rx_ring[i].addr = 0xBADF00D0; /* An invalid address. */
		if (np->rx_skbuff[i]) {
#if LINUX_VERSION_CODE < 0x20100
			np->rx_skbuff[i]->free = 1;
#endif
			dev_kfree_skb(np->rx_skbuff[i]);
		}
		np->rx_skbuff[i] = 0;
	}
}

static void alloc_tbufs(struct device* dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	np->tx_full = 0;
	np->cur_tx = 0;
	np->dirty_tx = 0;

	for (i = 0; i < TX_RING_SIZE; i++) {
		np->tx_skbuff[i] = 0;
		np->tx_ring[i].tx_status = 0;
		np->tx_ring[i].desc_length = cpu_to_le32(0x00e08000);
		np->tx_ring[i].next_desc = virt_to_le32desc(&np->tx_ring[i+1]);
		np->tx_buf[i] = kmalloc(PKT_BUF_SZ, GFP_KERNEL);
	}
	np->tx_ring[i-1].next_desc = virt_to_le32desc(&np->tx_ring[0]);
}

static void free_tbufs(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int i;

	for (i = 0; i < TX_RING_SIZE; i++) {
		if (np->tx_skbuff[i])
			dev_kfree_skb(np->tx_skbuff[i]);
		np->tx_skbuff[i] = 0;
		if (np->tx_buf[i]) {
			kfree(np->tx_buf[i]);
			np->tx_buf[i] = 0;
		}
	}
}

static void init_registers(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	for (i = 0; i < 6; i++)
		writeb(dev->dev_addr[i], ioaddr + StationAddr + i);

	/* Initialize other registers. */
	writew(0x0006, ioaddr + PCIBusConfig);	/* Tune configuration??? */
	/* Configure the FIFO thresholds. */
	writeb(0x20, ioaddr + TxConfig);	/* Initial threshold 32 bytes */
	np->tx_thresh = 0x20;
	np->rx_thresh = 0x60;				/* Written in set_rx_mode(). */

	if (dev->if_port == 0)
		dev->if_port = np->default_port;

	dev->tbusy = 0;
	dev->interrupt = 0;

	writel(virt_to_bus(np->rx_ring), ioaddr + RxRingPtr);
	writel(virt_to_bus(np->tx_ring), ioaddr + TxRingPtr);

	set_rx_mode(dev);

	dev->start = 1;

	/* Enable interrupts by setting the interrupt mask. */
	writew(IntrRxDone | IntrRxErr | IntrRxEmpty| IntrRxOverflow| IntrRxDropped|
		   IntrTxDone | IntrTxAbort | IntrTxUnderrun |
		   IntrPCIErr | IntrStatsMax | IntrLinkChange | IntrMIIChange,
		   ioaddr + IntrEnable);

	np->chip_cmd = CmdStart|CmdTxOn|CmdRxOn|CmdNoTxPoll;
	if (np->duplex_lock)
		np->chip_cmd |= CmdFDuplex;
	writew(np->chip_cmd, ioaddr + ChipCmd);

	check_duplex(dev);
	/* The LED outputs of various MII xcvrs should be configured.  */
	/* For NS or Mison phys, turn on bit 1 in register 0x17 */
	/* For ESI phys, turn on bit 7 in register 0x17. */
	mdio_write(dev, np->phys[0], 0x17, mdio_read(dev, np->phys[0], 0x17) |
			   (np->drv_flags & HasESIPhy) ? 0x0080 : 0x0001);
}


/* Read and write over the MII Management Data I/O (MDIO) interface. */

static int mdio_read(struct device *dev, int phy_id, int regnum)
{
	long ioaddr = dev->base_addr;
	int boguscnt = 1024;

	/* Wait for a previous command to complete. */
	while ((readb(ioaddr + MIICmd) & 0x60) && --boguscnt > 0)
		;
	writeb(0x00, ioaddr + MIICmd);
	writeb(phy_id, ioaddr + MIIPhyAddr);
	writeb(regnum, ioaddr + MIIRegAddr);
	writeb(0x40, ioaddr + MIICmd);			/* Trigger read */
	boguscnt = 1024;
	while ((readb(ioaddr + MIICmd) & 0x40) && --boguscnt > 0)
		;
	return readw(ioaddr + MIIData);
}

static void mdio_write(struct device *dev, int phy_id, int regnum, int value)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int boguscnt = 1024;

	if (phy_id == np->phys[0]) {
		switch (regnum) {
		case 0:					/* Is user forcing speed/duplex? */
			if (value & 0x9000)	/* Autonegotiation. */
				np->duplex_lock = 0;
			else
				np->full_duplex = (value & 0x0100) ? 1 : 0;
			break;
		case 4:
			np->advertising = value;
			break;
		}
	}
	/* Wait for a previous command to complete. */
	while ((readb(ioaddr + MIICmd) & 0x60) && --boguscnt > 0)
		;
	writeb(0x00, ioaddr + MIICmd);
	writeb(phy_id, ioaddr + MIIPhyAddr);
	writeb(regnum, ioaddr + MIIRegAddr);
	writew(value, ioaddr + MIIData);
	writeb(0x20, ioaddr + MIICmd);			/* Trigger write. */
	return;
}


static int netdev_open(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	/* Reset the chip. */
	writew(CmdReset, ioaddr + ChipCmd);

	MOD_INC_USE_COUNT;

	if (request_irq(dev->irq, &intr_handler, SA_SHIRQ, dev->name, dev)) {
		MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}

	if (debug > 1)
		printk(KERN_DEBUG "%s: netdev_open() irq %d.\n",
			   dev->name, dev->irq);

	alloc_rbufs(dev);
	alloc_tbufs(dev);

	init_registers(dev);

	if (debug > 2)
		printk(KERN_DEBUG "%s: Done netdev_open(), status %4.4x "
			   "MII status: %4.4x.\n",
			   dev->name, readw(ioaddr + ChipCmd),
			   mdio_read(dev, np->phys[0], 1));

	/* Set the timer to check for link beat. */
	init_timer(&np->timer);
	np->timer.expires = jiffies + 2;
	np->timer.data = (unsigned long)dev;
	np->timer.function = &netdev_timer;				/* timer handler */
	add_timer(&np->timer);

	return 0;
}

static void check_duplex(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int mii_reg5 = mdio_read(dev, np->phys[0], 5);
	int negotiated = mii_reg5 & np->advertising;
	int duplex;

	if (np->duplex_lock  ||  mii_reg5 == 0xffff)
		return;
	duplex = (negotiated & 0x0100) || (negotiated & 0x01C0) == 0x0040;
	if (np->full_duplex != duplex) {
		np->full_duplex = duplex;
		if (debug)
			printk(KERN_INFO "%s: Setting %s-duplex based on MII #%d link"
				   " partner capability of %4.4x.\n", dev->name,
				   duplex ? "full" : "half", np->phys[0], mii_reg5);
		if (duplex)
			np->chip_cmd |= CmdFDuplex;
		else
			np->chip_cmd &= ~CmdFDuplex;
		writew(np->chip_cmd, ioaddr + ChipCmd);
	}
}

static void netdev_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 10*HZ;

	if (debug > 3) {
		printk(KERN_DEBUG "%s: VIA Rhine monitor tick, status %4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));
	}
	if (test_bit(0, (void*)&dev->tbusy) != 0
		&& np->cur_tx - np->dirty_tx > 1
		&& jiffies - dev->trans_start > TX_TIMEOUT)
		tx_timeout(dev);

	check_duplex(dev);

	np->timer.expires = jiffies + next_tick;
	add_timer(&np->timer);
}

static void tx_timeout(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	struct pci_dev *pdev = pci_find_slot(np->pci_bus, np->pci_devfn);
	long ioaddr = dev->base_addr;

	printk(KERN_WARNING "%s: Transmit timed out, status %4.4x, PHY status "
		   "%4.4x, resetting...\n",
		   dev->name, readw(ioaddr + IntrStatus),
		   mdio_read(dev, np->phys[0], 1));

	dev->if_port = 0;

	/* protect against concurrent rx interrupts */
	disable_irq(pdev->irq);

	/* Reset the chip. */
	writew(CmdReset, ioaddr + ChipCmd);

	/* clear all descriptors */
	free_tbufs(dev);
	free_rbufs(dev);
	alloc_tbufs(dev);
	alloc_rbufs(dev);

	/* Reinitialize the hardware. */
	wait_for_reset(dev, dev->name);
	init_registers(dev);

	enable_irq(pdev->irq);

	dev->trans_start = jiffies;
	np->stats.tx_errors++;

	/* wake queue */
	clear_bit(0, (void*)&dev->tbusy);
	mark_bh(NET_BH);

	return;
}

static int start_tx(struct sk_buff *skb, struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	unsigned entry;

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		/* This watchdog code is redundant with the media monitor timer. */
		if (jiffies - dev->trans_start > TX_TIMEOUT)
			tx_timeout(dev);
		return 1;
	}

	/* Explicitly flush packet data cache lines here. */

	/* Caution: the write order is important here, set the descriptor word
	   with the "ownership" bit last.  No SMP locking is needed if the
	   cur_tx is incremented after the descriptor is consistent.  */

	/* Calculate the next Tx descriptor entry. */
	entry = np->cur_tx % TX_RING_SIZE;

	if (skb->len < ETH_ZLEN) {
		skb = skb_padto(skb, ETH_ZLEN);
		if(skb == NULL)
		{
			dev->tbusy = 0;
			return 0;
		}
	}

	np->tx_skbuff[entry] = skb;

	if ((np->drv_flags & ReqTxAlign)  && ((long)skb->data & 3)) {
		/* Must use alignment buffer. */
		if (np->tx_buf[entry] == NULL &&
			(np->tx_buf[entry] = kmalloc(PKT_BUF_SZ, GFP_KERNEL)) == NULL)
			return 1;
		memcpy(np->tx_buf[entry], skb->data, skb->len);
		np->tx_ring[entry].addr = virt_to_le32desc(np->tx_buf[entry]);
	} else
		np->tx_ring[entry].addr = virt_to_le32desc(skb->data);

	np->tx_ring[entry].desc_length =
		cpu_to_le32(0x00E08000 | (skb->len >= ETH_ZLEN ? skb->len : ETH_ZLEN));
	np->tx_ring[entry].tx_status = cpu_to_le32(DescOwn);

	np->cur_tx++;

	/* Explicitly flush descriptor cache lines here. */

	/* Wake the potentially-idle transmit channel. */
	writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);

	if (np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 1)
		clear_bit(0, (void*)&dev->tbusy);		/* Typical path */
	else
		np->tx_full = 1;
	dev->trans_start = jiffies;

	if (debug > 4) {
		printk(KERN_DEBUG "%s: Transmit frame #%d queued in slot %d.\n",
			   dev->name, np->cur_tx, entry);
	}
	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void intr_handler(int irq, void *dev_instance, struct pt_regs *rgs)
{
	struct device *dev = (struct device *)dev_instance;
	struct netdev_private *np = (void *)dev->priv;
	long ioaddr = dev->base_addr;
	int boguscnt = max_interrupt_work;

	do {
		u32 intr_status = readw(ioaddr + IntrStatus);

		/* Acknowledge all of the current interrupt sources ASAP. */
		writew(intr_status & 0xffff, ioaddr + IntrStatus);

		if (debug > 4)
			printk(KERN_DEBUG "%s: Interrupt, status %4.4x.\n",
				   dev->name, intr_status);

		if (intr_status == 0)
			break;

		if (intr_status & (IntrRxDone | IntrRxErr | IntrRxDropped |
						   IntrRxWakeUp | IntrRxEmpty | IntrRxNoBuf))
			netdev_rx(dev);

		for (; np->cur_tx - np->dirty_tx > 0; np->dirty_tx++) {
			int entry = np->dirty_tx % TX_RING_SIZE;
			int txstatus = le32_to_cpu(np->tx_ring[entry].tx_status);
			if (txstatus & DescOwn)
				break;
			if (debug > 6)
				printk(KERN_DEBUG " Tx scavenge %d status %4.4x.\n",
					   entry, txstatus);
			if (txstatus & 0x8000) {
				if (debug > 1)
					printk(KERN_DEBUG "%s: Transmit error, Tx status %4.4x.\n",
						   dev->name, txstatus);
				np->stats.tx_errors++;
				if (txstatus & 0x0400) np->stats.tx_carrier_errors++;
				if (txstatus & 0x0200) np->stats.tx_window_errors++;
				if (txstatus & 0x0100) np->stats.tx_aborted_errors++;
				if (txstatus & 0x0080) np->stats.tx_heartbeat_errors++;
				if (txstatus & 0x0002) np->stats.tx_fifo_errors++;
#ifdef ETHER_STATS
				if (txstatus & 0x0100) np->stats.collisions16++;
#endif
				/* Transmitter restarted in 'abnormal' handler. */
			} else {
#ifdef ETHER_STATS
				if (txstatus & 0x0001) np->stats.tx_deferred++;
#endif
				np->stats.collisions += (txstatus >> 3) & 15;
#if defined(NETSTATS_VER2)
				np->stats.tx_bytes += np->tx_skbuff[entry]->len;
#endif
				np->stats.tx_packets++;
			}
			/* Free the original skb. */
			dev_kfree_skb(np->tx_skbuff[entry]);
			np->tx_skbuff[entry] = 0;
		}
		if (np->tx_full && dev->tbusy
			&& np->cur_tx - np->dirty_tx < TX_QUEUE_LEN - 4) {
			/* The ring is no longer full, clear tbusy. */
			np->tx_full = 0;
			clear_bit(0, (void*)&dev->tbusy);
			mark_bh(NET_BH);
		}

		/* Abnormal error summary/uncommon events handlers. */
		if (intr_status & (IntrPCIErr | IntrLinkChange | IntrMIIChange |
						   IntrStatsMax | IntrTxAbort | IntrTxUnderrun))
			netdev_error(dev, intr_status);

		if (--boguscnt < 0) {
			printk(KERN_WARNING "%s: Too much work at interrupt, "
				   "status=0x%4.4x.\n",
				   dev->name, intr_status);
			break;
		}
	} while (1);

	if (debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, status=%#4.4x.\n",
			   dev->name, readw(ioaddr + IntrStatus));

	return;
}

/* This routine is logically part of the interrupt handler, but isolated
   for clarity and better register allocation. */
static int netdev_rx(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	int entry = np->cur_rx % RX_RING_SIZE;
	int boguscnt = np->dirty_rx + RX_RING_SIZE - np->cur_rx;

	if (debug > 4) {
		printk(KERN_DEBUG " In netdev_rx(), entry %d status %8.8x.\n",
			   entry, np->rx_head_desc->rx_status);
	}

	/* If EOP is set on the next entry, it's a new packet. Send it up. */
	while ( ! (np->rx_head_desc->rx_status & cpu_to_le32(DescOwn))) {
		struct rx_desc *desc = np->rx_head_desc;
		u32 desc_status = le32_to_cpu(desc->rx_status);
		int data_size = desc_status >> 16;

		if (debug > 4)
			printk(KERN_DEBUG "  netdev_rx() status is %4.4x.\n",
				   desc_status);
		if (--boguscnt < 0)
			break;
		if ( (desc_status & (RxWholePkt | RxErr)) !=  RxWholePkt) {
			if ((desc_status & RxWholePkt) !=  RxWholePkt) {
				printk(KERN_WARNING "%s: Oversized Ethernet frame spanned "
					   "multiple buffers, entry %#x length %d status %4.4x!\n",
					   dev->name, np->cur_rx, data_size, desc_status);
				printk(KERN_WARNING "%s: Oversized Ethernet frame %p vs %p.\n",
					   dev->name, np->rx_head_desc,
					   &np->rx_ring[np->cur_rx % RX_RING_SIZE]);
				np->stats.rx_length_errors++;
			} else if (desc_status & RxErr) {
				/* There was a error. */
				if (debug > 2)
					printk(KERN_DEBUG "  netdev_rx() Rx error was %8.8x.\n",
						   desc_status);
				np->stats.rx_errors++;
				if (desc_status & 0x0030) np->stats.rx_length_errors++;
				if (desc_status & 0x0048) np->stats.rx_fifo_errors++;
				if (desc_status & 0x0004) np->stats.rx_frame_errors++;
				if (desc_status & 0x0002) np->stats.rx_crc_errors++;
			}
		} else {
			struct sk_buff *skb;
			/* Length should omit the CRC */
			int pkt_len = data_size - 4;

			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
#if HAS_IP_COPYSUM			/* Call copy + cksum if available. */
				eth_copy_and_sum(skb, np->rx_skbuff[entry]->tail, pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len), np->rx_skbuff[entry]->tail,
					   pkt_len);
#endif
			} else {
				skb_put(skb = np->rx_skbuff[entry], pkt_len);
				np->rx_skbuff[entry] = NULL;
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
#if defined(NETSTATS_VER2)
			np->stats.rx_bytes += skb->len;
#endif
			np->stats.rx_packets++;
		}
		entry = (++np->cur_rx) % RX_RING_SIZE;
		np->rx_head_desc = &np->rx_ring[entry];
	}

	/* Refill the Rx ring buffers. */
	for (; np->cur_rx - np->dirty_rx > 0; np->dirty_rx++) {
		struct sk_buff *skb;
		entry = np->dirty_rx % RX_RING_SIZE;
		if (np->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(np->rx_buf_sz);
			np->rx_skbuff[entry] = skb;
			if (skb == NULL)
				break;			/* Better luck next round. */
			skb->dev = dev;			/* Mark as being used by this device. */
			np->rx_ring[entry].addr = virt_to_le32desc(skb->tail);
		}
		np->rx_ring[entry].rx_status = cpu_to_le32(DescOwn);
	}

	/* Pre-emptively restart Rx engine. */
	writew(CmdRxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	return 0;
}

static void netdev_error(struct device *dev, int intr_status)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (intr_status & (IntrMIIChange | IntrLinkChange)) {
		if (readb(ioaddr + MIIStatus) & 0x02) {
			/* Link failed, restart autonegotiation. */
			if (np->drv_flags & HasDavicomPhy)
				mdio_write(dev, np->phys[0], 0, 0x3300);
		} else
			check_duplex(dev);
		if (debug)
			printk(KERN_ERR "%s: MII status changed: Autonegotiation "
				   "advertising %4.4x  partner %4.4x.\n", dev->name,
			   mdio_read(dev, np->phys[0], 4),
			   mdio_read(dev, np->phys[0], 5));
	}
	if (intr_status & IntrStatsMax) {
		np->stats.rx_crc_errors	+= readw(ioaddr + RxCRCErrs);
		np->stats.rx_missed_errors	+= readw(ioaddr + RxMissed);
		clear_tally_counters(ioaddr);
	}
	if (intr_status & IntrTxAbort) {
		/* Stats counted in Tx-done handler, just restart Tx. */
		writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	}
	if (intr_status & IntrTxUnderrun) {
		if (np->tx_thresh < 0xE0)
			writeb(np->tx_thresh += 0x20, ioaddr + TxConfig);
		if (debug > 1)
			printk(KERN_INFO "%s: Transmitter underrun, increasing Tx "
				   "threshold setting to %2.2x.\n", dev->name, np->tx_thresh);
	}
	if ((intr_status & ~(IntrLinkChange | IntrStatsMax |
						 IntrTxAbort|IntrTxAborted))  &&  debug) {
		printk(KERN_ERR "%s: Something Wicked happened! %4.4x.\n",
			   dev->name, intr_status);
		/* Recovery for other fault sources not known. */
		writew(CmdTxDemand | np->chip_cmd, dev->base_addr + ChipCmd);
	}
}

static struct enet_statistics *get_stats(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;

	/* Nominally we should lock this segment of code for SMP, although
	   the vulnerability window is very small and statistics are
	   non-critical. */
	np->stats.rx_crc_errors	+= readw(ioaddr + RxCRCErrs);
	np->stats.rx_missed_errors	+= readw(ioaddr + RxMissed);
	clear_tally_counters(ioaddr);

	return &np->stats;
}

/* Clears the "tally counters" for CRC errors and missed frames(?).
   It has been reported that some chips need a write of 0 to clear
   these, for others the counters are set to 1 when written to and
   instead cleared when read. So we clear them both ways ... */
static inline void clear_tally_counters(const long ioaddr)
{
	writel(0, ioaddr + RxMissed);
	readw(ioaddr + RxCRCErrs);
	readw(ioaddr + RxMissed);
}

/* The big-endian AUTODIN II ethernet CRC calculation.
   N.B. Do not use for bulk data, use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c */
static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc(int length, unsigned char *data)
{
	int crc = -1;

	while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 0; bit < 8; bit++, current_octet >>= 1) {
			crc = (crc << 1) ^
				((crc < 0) ^ (current_octet & 1) ? ethernet_polynomial : 0);
		}
	}
	return crc;
}

static void set_rx_mode(struct device *dev)
{
	struct netdev_private *np = (struct netdev_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u32 mc_filter[2];			/* Multicast hash filter */
	u8 rx_mode;					/* Note: 0x02=accept runt, 0x01=accept errs */

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		/* Unconditionally log net taps. */
		printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n", dev->name);
		rx_mode = 0x1C;
	} else if ((dev->mc_count > multicast_filter_limit)
			   ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to match, or accept all multicasts. */
		writel(0xffffffff, ioaddr + MulticastFilter0);
		writel(0xffffffff, ioaddr + MulticastFilter1);
		rx_mode = 0x0C;
	} else {
		struct dev_mc_list *mclist;
		int i;
		memset(mc_filter, 0, sizeof(mc_filter));
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26,
					mc_filter);
		}
		writel(mc_filter[0], ioaddr + MulticastFilter0);
		writel(mc_filter[1], ioaddr + MulticastFilter1);
		rx_mode = 0x0C;
	}
	writeb(np->rx_thresh | rx_mode, ioaddr + RxConfig);
}

static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	u16 *data = (u16 *)&rq->ifr_data;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = ((struct netdev_private *)dev->priv)->phys[0] & 0x1f;
		/* Fall Through */
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int netdev_close(struct device *dev)
{
	long ioaddr = dev->base_addr;
	struct netdev_private *np = (struct netdev_private *)dev->priv;

	dev->start = 0;
	dev->tbusy = 1;

	if (debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %4.4x.\n",
			   dev->name, readw(ioaddr + ChipCmd));

	del_timer(&np->timer);

	/* Switch to loopback mode to avoid hardware races. */
	writeb(np->tx_thresh | 0x01, ioaddr + TxConfig);

	/* Disable interrupts by clearing the interrupt mask. */
	writew(0x0000, ioaddr + IntrEnable);

	/* Stop the chip's Tx and Rx processes. */
	writew(CmdStop, ioaddr + ChipCmd);

	free_irq(dev->irq, dev);

	free_rbufs(dev);
	free_tbufs(dev);

	MOD_DEC_USE_COUNT;

	return 0;
}


#ifdef MODULE
int init_module(void)
{
	if (debug)					/* Emit version even if no cards detected. */
		printk(KERN_INFO "%s" KERN_INFO "%s", version1, version2);
	return pci_etherdev_probe(NULL, pci_tbl);
}

void cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_net_dev) {
		struct netdev_private *np = (void *)(root_net_dev->priv);
		unregister_netdev(root_net_dev);
#ifdef USE_IO_OPS
		release_region(root_net_dev->base_addr, pci_tbl[np->chip_id].io_size);
#else
		iounmap((char *)(root_net_dev->base_addr));
#endif
		next_dev = np->next_module;
		if (np->priv_addr)
			kfree(np->priv_addr);
		kfree(root_net_dev);
		root_net_dev = next_dev;
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c via-rhine.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  SMP-compile-command: "gcc -D__SMP__ -DMODULE -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -c via-rhine.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
