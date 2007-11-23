/* 3c509.c: A 3c509 EtherLink3 ethernet driver for linux. */
/*
	Written 1993-1998 by Donald Becker.

	Copyright 1994-1998 by Donald Becker.
	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.	 This software may be used and
	distributed according to the terms of the GNU Public License,
	incorporated herein by reference.

	This driver is for the 3Com EtherLinkIII series.

	The author may be reached as becker@cesdis.gsfc.nasa.gov or
	C/O Center of Excellence in Space Data and Information Sciences
		Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	Known limitations:
	Because of the way 3c509 ISA detection works it's difficult to predict
	a priori which of several ISA-mode cards will be detected first.

	This driver does not use predictive interrupt mode, resulting in higher
	packet latency but lower overhead.  If interrupts are disabled for an
	unusually long time it could also result in missed packets, but in
	practice this rarely happens.


	FIXES:
		Alan Cox:       Removed the 'Unexpected interrupt' bug.
		Michael Meskes:	Upgraded to Donald Becker's version 1.07.
		Alan Cox:	Increased the eeprom delay. Regardless of
				what the docs say some people definitely
				get problems with lower (but in card spec)
				delays
		v1.10 4/21/97 Fixed module code so that multiple cards may be detected,
				other cleanups.  -djb
		v1.13 9/8/97 Made 'max_interrupt_work' an insmod-settable variable -djb
		v1.14 10/15/97 Avoided waiting..discard message for fast machines -djb
		v1.15 1/31/98 Faster recovery for Tx errors. -djb
		v1.16 2/3/98 Different ID port handling to avoid sound cards. -djb
*/

static char *version = "3c509.c:1.16C 2/5/98 becker@cesdis.gsfc.nasa.gov\n";
/* A few values that may be tweaked. */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (400*HZ/1000)
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

#include <linux/module.h>

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/config.h>	/* for CONFIG_MCA */
#include <linux/delay.h>	/* for udelay() */

#include <asm/bitops.h>
#include <asm/io.h>

#ifdef EL3_DEBUG
int el3_debug = EL3_DEBUG;
#else
int el3_debug = 2;
#endif

#define test_and_set_bit(val, addr) set_bit(val, addr)

/* To minimize the size of the driver source I only define operating
   constants if they are used several times and make the code clearer.
   You'll need the manual to understand hardware operation. */
/* Offsets from base I/O address. */
#define EL3_DATA 0x00
#define EL3_CMD 0x0e
#define EL3_STATUS 0x0e

#define EL3_IO_EXTENT	16

#define EL3WINDOW(win_num) outw(SelectWindow + (win_num), ioaddr + EL3_CMD)


/* The top five bits written to EL3_CMD are a command, the lower
   11 bits are the parameter, if applicable. */
enum c509cmd {
	TotalReset = 0<<11, SelectWindow = 1<<11, StartCoax = 2<<11,
	RxDisable = 3<<11, RxEnable = 4<<11, RxReset = 5<<11, RxDiscard = 8<<11,
	TxEnable = 9<<11, TxDisable = 10<<11, TxReset = 11<<11,
	FakeIntr = 12<<11, AckIntr = 13<<11, SetIntrEnb = 14<<11,
	SetStatusEnb = 15<<11, SetRxFilter = 16<<11, SetRxThreshold = 17<<11,
	SetTxThreshold = 18<<11, SetTxStart = 19<<11, StatsEnable = 21<<11,
	StatsDisable = 22<<11, StopCoax = 23<<11,};

enum c509status {
	IntLatch = 0x0001, AdapterFailure = 0x0002, TxComplete = 0x0004,
	TxAvailable = 0x0008, RxComplete = 0x0010, RxEarly = 0x0020,
	IntReq = 0x0040, StatsFull = 0x0080, CmdBusy = 0x1000, };

/* The SetRxFilter command accepts the following classes: */
enum RxFilter {
	RxStation = 1, RxMulticast = 2, RxBroadcast = 4, RxProm = 8 };

/* Register window 1 offsets, the window used in normal operation. */
enum Window1 {
	TX_FIFO = 0x10,  RX_FIFO = 0x10,  RxErrors = 0x14,
	RxStatus = 0x18,  Timer=0x1A, TxStatus = 0x1B,
	TxFree = 0x1C, /* Remaining free bytes in Tx buffer. */
};
enum Window0 {
	Wn0_IRQ = 8,			/* Window 0: Set IRQ line in bits 12-15. */
	Wn0EepromCmd = 10,		/* Window 0: EEPROM command register. */
	Wn0EepromData = 12,		/* Window 0: EEPROM results register. */
	IntrStatus=0x0E,		/* Valid in all windows. */
};
enum Win0_EEPROM_bits {
	EEPROM_Read = 0x80, EEPROM_Busy = 0x8000,
};

#define WN4_MEDIA	0x0A		/* Window 4: Various transcvr/media bits. */
#define  MEDIA_TP	0x00C0		/* Enable link beat and jabber for 10baseT. */

struct el3_private {
	struct enet_statistics stats;
	struct device *next_dev;
	/* skb send-queue */
	int head, size;
};

static int id_port = 0x110;		/* Start with 0x110 to avoid new sound cards.*/
static struct device *el3_root_dev = NULL;

static ushort id_read_eeprom(int index);
static ushort read_eeprom(int ioaddr, int index);
static int el3_open(struct device *dev);
static int el3_start_xmit(struct sk_buff *skb, struct device *dev);
static void el3_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void update_stats(int addr, struct device *dev);
static struct enet_statistics *el3_get_stats(struct device *dev);
static int el3_rx(struct device *dev, int max_work);
static int el3_close(struct device *dev);
static void set_rx_mode(struct device *dev);



int el3_probe(struct device *dev)
{
	short lrs_state = 0xff, i;
	int ioaddr, irq, if_port;
	u16 phys_addr[3];
	static int current_tag = 0;

	/* First check all slots of the EISA bus.  The next slot address to
	   probe is kept in 'eisa_addr' to support multiple probe() calls. */
	if (EISA_bus) {
		static int eisa_addr = 0x1000;
		while (eisa_addr < 0x9000) {
			ioaddr = eisa_addr;
			eisa_addr += 0x1000;

			if (check_region(ioaddr, EL3_IO_EXTENT))
				continue;
			/* Check the standard EISA ID register for an encoded '3Com'. */
			if (inw(ioaddr + 0xC80) != 0x6d50)
				continue;
			/* Check for a product that we support, 3c579 any rev. */
			if (inb(ioaddr + 0xC82) != 0x57)
				continue;

			/* Change the register set to the configuration window 0. */
			outw(SelectWindow | 0, ioaddr + 0xC80 + EL3_CMD);

			irq = inw(ioaddr + Wn0_IRQ) >> 12;
			if_port = inw(ioaddr + 6)>>14;
			for (i = 0; i < 3; i++)
				phys_addr[i] = htons(read_eeprom(ioaddr, i));

			/* Restore the "Product ID" to the EEPROM read register. */
			read_eeprom(ioaddr, 3);

			/* Was the EISA code an add-on hack?  Nahhhhh... */
			goto found;
		}
	}

#ifdef CONFIG_MCA
	if (MCA_bus) {
		mca_adaptor_select_mode(1);
		for (i = 0; i < 8; i++)
			if ((mca_adaptor_id(i) | 1) == 0x627c) {
				ioaddr = mca_pos_base_addr(i);
				irq = inw(ioaddr + Wn0_IRQ) >> 12;
				if_port = inw(ioaddr + 6)>>14;
				for (i = 0; i < 3; i++)
					phys_addr[i] = htons(read_eeprom(ioaddr, i));

				mca_adaptor_select_mode(0);
				goto found;
			}
		mca_adaptor_select_mode(0);

	}
#endif

	/* Reset the ISA PnP mechanism on 3c509b. */
	outb(0x02, 0x279);           /* Select PnP config control register. */
	outb(0x02, 0xA79);           /* Return to WaitForKey state. */
	/* Select an open I/O location at 0x1*0 to do contention select. */
	for ( ; id_port < 0x200; id_port += 0x10) {
		if (check_region(id_port, 1))
			continue;
		outb(0x00, id_port);
		outb(0xff, id_port);
		if (inb(id_port) & 0x01)
			break;
	}
	if (id_port >= 0x200) {             /* GCC optimizes this test out. */
		/* Rare -- do we really need a warning? */
		printk(KERN_WARNING " WARNING: No I/O port available for 3c509 "
			   "activation sequence.\n");
		return -ENODEV;
	}
	/* Next check for all ISA bus boards by sending the ID sequence to the
	   ID_PORT.  We find cards past the first by setting the 'current_tag'
	   on cards as they are found.  Cards with their tag set will not
	   respond to subsequent ID sequences. */

	outb(0x00, id_port);
	outb(0x00, id_port);
	for(i = 0; i < 255; i++) {
		outb(lrs_state, id_port);
		lrs_state <<= 1;
		lrs_state = lrs_state & 0x100 ? lrs_state ^ 0xcf : lrs_state;
	}

	/* For the first probe, clear all board's tag registers. */
	if (current_tag == 0)
		outb(0xd0, id_port);
	else				/* Otherwise kill off already-found boards. */
		outb(0xd8, id_port);

	if (id_read_eeprom(7) != 0x6d50) {
		return -ENODEV;
	}

	/* Read in EEPROM data, which does contention-select.
	   Only the lowest address board will stay "on-line".
	   3Com got the byte order backwards. */
	for (i = 0; i < 3; i++) {
		phys_addr[i] = htons(id_read_eeprom(i));
	}

	{
		unsigned int iobase = id_read_eeprom(8);
		if_port = iobase >> 14;
		ioaddr = 0x200 + ((iobase & 0x1f) << 4);
	}
	irq = id_read_eeprom(9) >> 12;

	if (dev) {					/* Set passed-in IRQ or I/O Addr. */
		if (dev->irq > 1  &&  dev->irq < 16)
			irq = dev->irq;

		if (dev->base_addr) {
			if (dev->mem_end == 0x3c509 			/* Magic key */
				&& dev->base_addr >= 0x200  &&  dev->base_addr <= 0x3e0)
				ioaddr = dev->base_addr & 0x3f0;
			else if (dev->base_addr != ioaddr)
				return -ENODEV;
		}
	}

	/* Set the adaptor tag so that the next card can be found. */
	outb(0xd0 + ++current_tag, id_port);

	/* Activate the adaptor at the EEPROM location. */
	outb((ioaddr >> 4) | 0xe0, id_port);

	EL3WINDOW(0);
	if (inw(ioaddr) != 0x6d50)
		return -ENODEV;

	/* Free the interrupt so that some other card can use it. */
	outw(0x0f00, ioaddr + Wn0_IRQ);
 found:
	if (dev == NULL) {
		dev = init_etherdev(dev, sizeof(struct el3_private));
	}
	memcpy(dev->dev_addr, phys_addr, sizeof(phys_addr));
	dev->base_addr = ioaddr;
	dev->irq = irq;
	dev->if_port = (dev->mem_start & 0x1f) ? dev->mem_start & 3 : if_port;

	request_region(dev->base_addr, EL3_IO_EXTENT, "3c509");

	{
		const char *if_names[] = {"10baseT", "AUI", "undefined", "BNC"};
		printk(KERN_INFO"%s: 3c509 at %#3.3lx tag %d, %s port, address ",
			   dev->name, dev->base_addr, current_tag, if_names[dev->if_port]);
	}

	/* Show the station address. */
	for (i = 0; i < 5; i++)
		printk("%2.2x:", dev->dev_addr[i]);
	printk("%2.2x, IRQ %d.\n", dev->dev_addr[i], dev->irq);

	/* Make up a EL3-specific-data structure. */
	if (dev->priv == NULL)
		dev->priv = kmalloc(sizeof(struct el3_private), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct el3_private));

	((struct el3_private *)dev->priv)->next_dev = el3_root_dev;
	el3_root_dev = dev;

	if (el3_debug > 0)
		printk(KERN_INFO"%s", version);

	/* The EL3-specific entries in the device structure. */
	dev->open = &el3_open;
	dev->hard_start_xmit = &el3_start_xmit;
	dev->stop = &el3_close;
	dev->get_stats = &el3_get_stats;
	dev->set_multicast_list = &set_rx_mode;

	/* Fill in the generic fields of the device structure. */
	ether_setup(dev);
	return 0;
}

/* Read a word from the EEPROM using the regular EEPROM access register.
   Assume that we are in register window zero.
 */
static ushort read_eeprom(int ioaddr, int index)
{
	int boguscheck;
	outw(EEPROM_Read + index, ioaddr + Wn0EepromCmd);
	/* Pause for at least 162 us. for the read to take place. */
	for (boguscheck = 20; boguscheck >= 0; boguscheck--) {
		udelay(40);
		if ((inw(ioaddr + Wn0EepromCmd) & EEPROM_Busy) == 0)
			break;
	}
	return inw(ioaddr + Wn0EepromData);
}

/* Read a word from the EEPROM when in the ISA ID probe state. */
static ushort id_read_eeprom(int index)
{
	int bit, word = 0;

	/* Issue read command, and pause for at least 162 us. for it to complete.
	   Assume extra-fast 16Mhz bus. */
	outb(EEPROM_Read + index, id_port);

	/* Pause for at least 162 us. for the read to take place. */
	udelay (500);
	
	for (bit = 15; bit >= 0; bit--)
		word = (word << 1) + (inb(id_port) & 0x01);

	if (el3_debug > 3)
		printk(KERN_DEBUG"  3c509 EEPROM word %d %#4.4x.\n", index, word);

	return word;
}



static int
el3_open(struct device *dev)
{
	int ioaddr = dev->base_addr;
	int i;

	outw(TxReset, ioaddr + EL3_CMD);
	outw(RxReset, ioaddr + EL3_CMD);
	outw(SetStatusEnb | 0x00, ioaddr + EL3_CMD);

	if (request_irq(dev->irq, &el3_interrupt, 0, "3c509", dev))
		return -EAGAIN;

	EL3WINDOW(0);
	if (el3_debug > 3)
		printk(KERN_DEBUG"%s: Opening 3c509, IRQ %d status@%x %4.4x.\n",
			   dev->name, dev->irq, ioaddr + EL3_STATUS,
			   inw(ioaddr + EL3_STATUS));

	/* Activate board: this is usually unnecessary. */
	outw(0x0001, ioaddr + 4);

	/* Set the IRQ line. */
	outw((dev->irq << 12) | 0x0f00, ioaddr + Wn0_IRQ);

	/* Set the station address in window 2 each time opened. */
	EL3WINDOW(2);

	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + i);

	if (dev->if_port == 3)
		/* Start the thinnet transceiver. We should really wait 50ms...*/
		outw(StartCoax, ioaddr + EL3_CMD);
	else if (dev->if_port == 0) {
		/* 10baseT interface, enabled link beat and jabber check. */
		EL3WINDOW(4);
		outw(inw(ioaddr + WN4_MEDIA) | MEDIA_TP, ioaddr + WN4_MEDIA);
	}

	/* Switch to the stats window, and clear all stats by reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	EL3WINDOW(6);
	for (i = 0; i < 9; i++)
		inb(ioaddr + i);
	inw(ioaddr + 10);
	inw(ioaddr + 12);

	/* Switch to register set 1 for normal use. */
	EL3WINDOW(1);

	/* Accept b-case and phys addr only. */
	set_rx_mode(dev);
	outw(StatsEnable, ioaddr + EL3_CMD); /* Turn on statistics. */

	dev->interrupt = 0;
	dev->tbusy = 0;
	dev->start = 1;

	outw(RxEnable, ioaddr + EL3_CMD); /* Enable the receiver. */
	outw(TxEnable, ioaddr + EL3_CMD); /* Enable transmitter. */
	/* Allow status bits to be seen. */
	outw(SetStatusEnb | 0xff, ioaddr + EL3_CMD);
	/* Ack all pending events, and set active indicator mask. */
	outw(AckIntr | IntLatch | TxAvailable | RxEarly | IntReq,
		 ioaddr + EL3_CMD);
	outw(SetIntrEnb | IntLatch|TxAvailable|TxComplete|RxComplete|StatsFull,
		 ioaddr + EL3_CMD);

	if (el3_debug > 3)
		printk(KERN_DEBUG "%s: Opened 3c509  IRQ %d  status %4.4x.\n",
			   dev->name, dev->irq, inw(ioaddr + EL3_STATUS));

	MOD_INC_USE_COUNT;
	return 0;					/* Always succeed */
}

static void el3_tx_timeout(struct device *dev)
{
	struct el3_private *lp = (struct el3_private *)dev->priv;
	int ioaddr = dev->base_addr;
	ushort status = inw(ioaddr + EL3_STATUS);

	printk(KERN_ERR"%s: transmit timed out, Tx_status %2.2x status %4.4x "
		   "Tx FIFO room %d.\n",
		   dev->name, inb(ioaddr + TxStatus), status,
		   inw(ioaddr + TxFree));

	/* Error-checking code for the common IRQ line block. */
	if (status & 0x0001 		/* IRQ line active, missed one. */
		&& inw(ioaddr + EL3_STATUS) & 1) { 			/* Make sure. */
		printk(KERN_CRIT"%s: Interrupt line blocked?  3c509 status %4.4x"
			   "  Tx %2.2x Rx %4.4x.\n", dev->name, status,
			   inb(ioaddr + TxStatus), inw(ioaddr + RxStatus));
	}

	lp->stats.tx_errors++;
	dev->trans_start = jiffies;
	/* Issue TX_RESET and TX_START commands. */
	outw(TxReset, ioaddr + EL3_CMD);
	outw(TxEnable, ioaddr + EL3_CMD);
	dev->tbusy = 0;
}

static int
el3_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct el3_private *lp = (struct el3_private *)dev->priv;
	int ioaddr = dev->base_addr;

	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		if (jiffies - dev->trans_start >= TX_TIMEOUT)
			el3_tx_timeout(dev);
		return 1;
	}

	if (el3_debug > 4) {
		printk(KERN_DEBUG"%s: el3_start_xmit(length = %ld) called, status "
			   "%4.4x.\n", dev->name, skb->len, inw(ioaddr + EL3_STATUS));
	}

	/* Put out the doubleword header... */
	outl(skb->len, ioaddr + TX_FIFO);

	/* ... and the packet rounded to a doubleword. */
#ifdef  __powerpc__
	outsl_unswapped(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);
#else
	outsl(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);
#endif
	dev->trans_start = jiffies;
	if (inw(ioaddr + TxFree) > 1536) {
		dev->tbusy = 0;
	} else
		/* Interrupt us when the FIFO has room for max-sized packet. */
		outw(SetTxThreshold + 1536, ioaddr + EL3_CMD);

	dev_kfree_skb (skb, FREE_WRITE);

	/* Clear the Tx status stack. */
	{
		short tx_status;
		int i = 4;

		while (--i > 0	&&	(tx_status = inb(ioaddr + TxStatus)) > 0) {
			if (tx_status & 0x38) lp->stats.tx_aborted_errors++;
			if (tx_status & 0x30) outw(TxReset, ioaddr + EL3_CMD);
			if (tx_status & 0x3C) outw(TxEnable, ioaddr + EL3_CMD);
			outb(0x00, ioaddr + TxStatus); /* Pop the status stack. */
		}
	}
	return 0;
}

/* The EL3 interrupt handler. */
static void
el3_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	int ioaddr, status;
	int work_budget = max_interrupt_work;

	if (dev->interrupt) {
		printk(KERN_ERR"%s: Re-entering the interrupt handler.\n", dev->name);
		return;
	}
	dev->interrupt = 1;

	ioaddr = dev->base_addr;
	status = inw(ioaddr + EL3_STATUS);

	if (el3_debug > 4)
		printk(KERN_DEBUG"%s: interrupt, status %4.4x.\n", dev->name, status);

	while ((status = inw(ioaddr + EL3_STATUS)) &
		   (IntLatch | RxComplete | StatsFull)) {

		if (status & RxComplete)
			work_budget = el3_rx(dev, work_budget);

		if (status & TxAvailable) {
			if (el3_debug > 5)
				printk(KERN_DEBUG"	TX room bit was handled.\n");
			/* There's room in the FIFO for a full-sized packet. */
			outw(AckIntr | TxAvailable, ioaddr + EL3_CMD);
			dev->tbusy = 0;
			mark_bh(NET_BH);
		}
		if (status & (AdapterFailure | RxEarly | StatsFull | TxComplete)) {
			/* Handle all uncommon interrupts. */
			if (status & StatsFull)				/* Empty statistics. */
				update_stats(ioaddr, dev);
			if (status & RxEarly) {				/* Rx early is unused. */
				work_budget = el3_rx(dev, work_budget);
				outw(AckIntr | RxEarly, ioaddr + EL3_CMD);
			}
			if (status & TxComplete) {			/* Really Tx error. */
				struct el3_private *lp = (struct el3_private *)dev->priv;
				short tx_status;
				int i = 4;

				while (--i>0 && (tx_status = inb(ioaddr + TxStatus)) > 0) {
					if (tx_status & 0x38) lp->stats.tx_aborted_errors++;
					if (tx_status & 0x30) outw(TxReset, ioaddr + EL3_CMD);
					if (tx_status & 0x3C) outw(TxEnable, ioaddr + EL3_CMD);
					outb(0x00, ioaddr + TxStatus); /* Pop the status stack. */
				}
			}
			if (status & AdapterFailure) {
				/* Adapter failure requires Rx reset and reinit. */
				outw(RxReset, ioaddr + EL3_CMD);
				/* Set the Rx filter to the current state. */
				set_rx_mode(dev);
				outw(RxEnable, ioaddr + EL3_CMD); /* Re-enable the receiver. */
				outw(AckIntr | AdapterFailure, ioaddr + EL3_CMD);
			}
		}

		if (--work_budget < 0) {
			printk(KERN_WARNING"%s: Too much work in interrupt, status "
				   "%4.4x.\n", dev->name, status);
			/* Clear all interrupts. */
			outw(AckIntr | 0xFF, ioaddr + EL3_CMD);
			break;
		}
		/* Acknowledge the IRQ. */
		outw(AckIntr | IntReq | IntLatch, ioaddr + EL3_CMD); /* Ack IRQ */
	}

	if (el3_debug > 4) {
		printk(KERN_DEBUG"%s: exiting interrupt, status %4.4x.\n", dev->name,
			   inw(ioaddr + EL3_STATUS));
	}

	dev->interrupt = 0;
	return;
}


static struct enet_statistics *
el3_get_stats(struct device *dev)
{
	struct el3_private *lp = (struct el3_private *)dev->priv;
	unsigned long flags;

	save_flags(flags);
	cli();
	update_stats(dev->base_addr, dev);
	restore_flags(flags);
	return &lp->stats;
}

/*  Update statistics.  We change to register window 6, so this should be run
	single-threaded if the device is active. This is expected to be a rare
	operation, and it's simpler for the rest of the driver to assume that
	window 1 is always valid rather than use a special window-state variable.
	*/
static void update_stats(int ioaddr, struct device *dev)
{
	struct el3_private *lp = (struct el3_private *)dev->priv;

	if (el3_debug > 5)
		printk(KERN_DEBUG"   Updating the statistics.\n");
	/* Turn off statistics updates while reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	/* Switch to the stats window, and read everything. */
	EL3WINDOW(6);
	lp->stats.tx_carrier_errors 	+= inb(ioaddr + 0);
	lp->stats.tx_heartbeat_errors	+= inb(ioaddr + 1);
	/* Multiple collisions. */	   	inb(ioaddr + 2);
	lp->stats.collisions			+= inb(ioaddr + 3);
	lp->stats.tx_window_errors		+= inb(ioaddr + 4);
	lp->stats.rx_fifo_errors		+= inb(ioaddr + 5);
	lp->stats.tx_packets			+= inb(ioaddr + 6);
	/* Rx packets	*/				inb(ioaddr + 7);
	/* Tx deferrals */				inb(ioaddr + 8);
	inw(ioaddr + 10);	/* Total Rx and Tx octets. */
	inw(ioaddr + 12);

	/* Back to window 1, and turn statistics back on. */
	EL3WINDOW(1);
	outw(StatsEnable, ioaddr + EL3_CMD);
	return;
}

static int el3_rx(struct device *dev, int work_budget)
{
	struct el3_private *lp = (struct el3_private *)dev->priv;
	int ioaddr = dev->base_addr;
	short rx_status;

	if (el3_debug > 5)
		printk(KERN_DEBUG"   In rx_packet(), status %4.4x, rx_status %4.4x.\n",
			   inw(ioaddr+EL3_STATUS), inw(ioaddr+RxStatus));
	while ((rx_status = inw(ioaddr + RxStatus)) > 0  && --work_budget >= 0) {
		if (rx_status & 0x4000) { /* Error, update stats. */
			short error = rx_status & 0x3800;

			outw(RxDiscard, ioaddr + EL3_CMD);
			lp->stats.rx_errors++;
			switch (error) {
			case 0x0000:		lp->stats.rx_over_errors++; break;
			case 0x0800:		lp->stats.rx_length_errors++; break;
			case 0x1000:		lp->stats.rx_frame_errors++; break;
			case 0x1800:		lp->stats.rx_length_errors++; break;
			case 0x2000:		lp->stats.rx_frame_errors++; break;
			case 0x2800:		lp->stats.rx_crc_errors++; break;
			}
		} else {
			short pkt_len = rx_status & 0x7ff;
			struct sk_buff *skb;

			skb = dev_alloc_skb(pkt_len+5);
			if (el3_debug > 4)
				printk(KERN_DEBUG"Receiving packet size %d status %4.4x.\n",
					   pkt_len, rx_status);
			if (skb != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);     /* Align IP on 16 byte */

				/* 'skb->data' points to the start of sk_buff data area. */
#ifdef  __powerpc__
				insl_unswapped(ioaddr+RX_FIFO, skb_put(skb,pkt_len),
							   (pkt_len + 3) >> 2);
#else
				insl(ioaddr + RX_FIFO, skb_put(skb,pkt_len),
					 (pkt_len + 3) >> 2);
#endif

				outw(RxDiscard, ioaddr + EL3_CMD); /* Pop top Rx packet. */
				skb->protocol = eth_type_trans(skb,dev);
				netif_rx(skb);
				lp->stats.rx_packets++;
				continue;
			}
			outw(RxDiscard, ioaddr + EL3_CMD);
			lp->stats.rx_dropped++;
			if (el3_debug)
				printk(KERN_NOTICE"%s: Couldn't allocate a sk_buff of size "
					   "%d.\n", dev->name, pkt_len);
		}
		inw(ioaddr + EL3_STATUS); 				/* Delay. */
		while (inw(ioaddr + EL3_STATUS) & 0x1000)
			printk(KERN_NOTICE"	Waiting for 3c509 to discard packet, status %x.\n",
				   inw(ioaddr + EL3_STATUS) );
	}

	return work_budget;
}

/*
 *     Set or clear the multicast filter for this adaptor.
 */
static void
set_rx_mode(struct device *dev)
{
	int ioaddr = dev->base_addr;

	if (dev->flags & IFF_PROMISC) {
		outw(SetRxFilter | RxStation | RxMulticast | RxBroadcast | RxProm,
			 ioaddr + EL3_CMD);
	} else if (dev->mc_count || (dev->flags&IFF_ALLMULTI)) {
		outw(SetRxFilter | RxStation | RxMulticast | RxBroadcast,
			 ioaddr + EL3_CMD);
	} else
		outw(SetRxFilter | RxStation | RxBroadcast, ioaddr + EL3_CMD);
}

static int
el3_close(struct device *dev)
{
	int ioaddr = dev->base_addr;

	if (el3_debug > 2)
		printk(KERN_DEBUG"%s: Shutting down ethercard.\n", dev->name);

	dev->tbusy = 1;
	dev->start = 0;

	/* Turn off statistics ASAP.  We update lp->stats below. */
	outw(StatsDisable, ioaddr + EL3_CMD);

	/* Disable the receiver and transmitter. */
	outw(RxDisable, ioaddr + EL3_CMD);
	outw(TxDisable, ioaddr + EL3_CMD);

	if (dev->if_port == 3)
		/* Turn off thinnet power.  Green! */
		outw(StopCoax, ioaddr + EL3_CMD);
	else if (dev->if_port == 0) {
		/* Disable link beat and jabber, if_port may change ere next open(). */
		EL3WINDOW(4);
		outw(inw(ioaddr + WN4_MEDIA) & ~MEDIA_TP, ioaddr + WN4_MEDIA);
	}

	free_irq(dev->irq, dev);
	/* Switching back to window 0 disables the IRQ. */
	EL3WINDOW(0);
	/* But we explicitly zero the IRQ line select anyway. */
	outw(0x0f00, ioaddr + Wn0_IRQ);

	update_stats(ioaddr, dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
/* Parameters that may be passed into the module. */
static int debug = -1;
static int irq[] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int xcvr[] = {-1, -1, -1, -1, -1, -1, -1, -1};

int
init_module(void)
{
	int el3_cards = 0;

	if (debug >= 0)
		el3_debug = debug;

	el3_root_dev = NULL;
	while (el3_probe(0) == 0) {
		if (irq[el3_cards] > 1)
			el3_root_dev->irq = irq[el3_cards];
		if (xcvr[el3_cards] >= 0)
			el3_root_dev->if_port = xcvr[el3_cards];
		el3_cards++;
	}

	return el3_cards ? 0 : -ENODEV;
}

void
cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (el3_root_dev) {
		next_dev = ((struct el3_private *)el3_root_dev->priv)->next_dev;
		unregister_netdev(el3_root_dev);
		release_region(el3_root_dev->base_addr, EL3_IO_EXTENT);
		kfree(el3_root_dev);
		el3_root_dev = next_dev;
	}
}
#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c 3c509.c"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
