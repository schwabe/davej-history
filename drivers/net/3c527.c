/* 3c527.c: 3Com Etherlink/MC32 driver for Linux
 *
 *	(c) Copyright 1998 Red Hat Software Inc
 *	Written by Alan Cox.
 *
 *	Based on skeleton.c written 1993-94 by Donald Becker and ne2.c
 *	(for the MCA stuff) written by Wim Dumon.
 *
 *	Thanks to 3Com for making this possible by providing me with the
 *	documentation.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 */

static const char *version =
	"3c527.c:v0.06 1999/09/16 Alan Cox (alan@redhat.com)\n";

/* Modified by Richard Procter (rprocter@mcs.vuw.ac.nz, rnp@netlink.co.nz) */

/*
 *	Things you need
 *	o	The databook.
 *
 *	Traps for the unwary
 *
 *	The diagram (Figure 1-1) and the POS summary disagree with the
 *	"Interrupt Level" section in the manual.
 *
 *	The documentation in places seems to miss things. In actual fact
 *	I've always eventually found everything is documented, it just
 *	requires careful study.
 *
 *      The manual contradicts itself when describing the minimum number 
 *      buffers in the 'configure lists' command. 
 *      My card accepts a buffer config of 4/4. 
 *
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/mca.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/proc_fs.h>

#include "3c527.h"

/*
 * The name of the card. Is used for messages and in the requests for
 * io regions, irqs and dma channels
 */
static const char* cardname = "3c527";

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 2
#endif

#undef DEBUG_IRQ

static unsigned int mc32_debug = NET_DEBUG;

/* The number of low I/O ports used by the ethercard. */
#define NETCARD_IO_EXTENT	8

/* As implemented, values must be a power-of-2 -- 4/8/16/32 */ 

#define TX_RING_LEN     16       /* Typically the card supports 37  */
#define RX_RING_LEN     8        /*     "       "        "          */

#define RX_COPYBREAK    200      /* Value from 3c59x.c */


/* Pointers to buffers and their on-card records */
struct mc32_ring_desc 
{
  volatile struct skb_header *p;                    
  struct sk_buff *skb;          
};


/* Information that needs to be kept for each board. */
struct mc32_local 
{
	struct net_device_stats net_stats;
	int slot;
	volatile struct mc32_mailbox *rx_box;
	volatile struct mc32_mailbox *tx_box;
	volatile struct mc32_mailbox *exec_box;
        volatile struct mc32_stats *stats;    /* Start of on-card statistics */
        u16 tx_chain;           /* Transmit list start offset */
	u16 rx_chain;           /* Receive list start offset */
        u16 tx_len;             /* Transmit list count */ 
        u16 rx_len;             /* Receive list count */
	u32 base;
	u16 rx_halted;
	u16 tx_halted;
	u16 exec_pending;
	u16 mc_reload_wait;	/* a multicast load request is pending */
	atomic_t tx_count;	/* buffers left */
	struct wait_queue *event;
	
	struct mc32_ring_desc tx_ring[TX_RING_LEN];	/* Host Transmit ring */
	struct mc32_ring_desc rx_ring[RX_RING_LEN];	/* Host Receive ring */

	u16 tx_ring_tail;       /* index to tx de-queue end */
	u16 tx_ring_head;       /* index to tx en-queue end */

	u16 rx_ring_tail;       /* index to rx de-queue end */ 

	u32 mc_list_valid;	/* True when the mclist is set */
};

/* The station (ethernet) address prefix, used for a sanity check. */
#define SA_ADDR0 0x02
#define SA_ADDR1 0x60
#define SA_ADDR2 0xAC

struct mca_adapters_t {
	unsigned int	id;
	char		*name;
};

const struct mca_adapters_t mc32_adapters[] = {
	{ 0x0041, "3COM EtherLink MC/32" },
	{ 0x8EF5, "IBM High Performance Lan Adapter" },
	{ 0x0000, NULL }
};


/* Index to functions, as function prototypes. */

extern int mc32_probe(struct device *dev);

static int	mc32_probe1(struct device *dev, int ioaddr);
static int      mc32_command(struct device *dev, u16 cmd, void *data, int len);
static int	mc32_open(struct device *dev);
static int	mc32_send_packet(struct sk_buff *skb, struct device *dev);
static void	mc32_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int	mc32_close(struct device *dev);
static struct	net_device_stats *mc32_get_stats(struct device *dev);
static void	mc32_set_multicast_list(struct device *dev);
static void	mc32_reset_multicast_list(struct device *dev);
static void     mc32_flush_tx_ring(struct mc32_local *lp);

/*
 * Check for a network adaptor of this type, and return '0' iff one exists.
 * If dev->base_addr == 0, probe all likely locations.
 * If dev->base_addr == 1, always return failure.
 * If dev->base_addr == 2, allocate space for the device and return success
 * (detachable devices only).
 */

__initfunc(int mc32_probe(struct device *dev))
{
	static int current_mca_slot = -1;
	int i;
	int adapter_found = 0;

	/* Do not check any supplied i/o locations. 
	   POS registers usually don't fail :) */

	/* MCA cards have POS registers.  
	   Autodetecting MCA cards is extremely simple. 
	   Just search for the card. */

	for(i = 0; (mc32_adapters[i].name != NULL) && !adapter_found; i++) {
		current_mca_slot = 
			mca_find_unused_adapter(mc32_adapters[i].id, 0);

		if((current_mca_slot != MCA_NOTFOUND) && !adapter_found) {
			if(!mc32_probe1(dev, current_mca_slot))
			{
				mca_set_adapter_name(current_mca_slot, 
						mc32_adapters[i].name);
				mca_mark_as_used(current_mca_slot);
				return 0;
			}
			
		}
	}
	return -ENODEV;
}

/*
 * This is the real probe routine. Linux has a history of friendly device
 * probes on the ISA bus. A good device probes avoids doing writes, and
 * verifies that the correct device exists and functions.
 */
__initfunc(static int mc32_probe1(struct device *dev, int slot))
{
	static unsigned version_printed = 0;
	int i;
	u8 POS;
	u32 base;
	struct mc32_local *lp;
	static u16 mca_io_bases[]={
		0x7280,0x7290,
		0x7680,0x7690,
		0x7A80,0x7A90,
		0x7E80,0x7E90
	};
	static u32 mca_mem_bases[]={
		0x00C0000,
		0x00C4000,
		0x00C8000,
		0x00CC000,
		0x00D0000,
		0x00D4000,
		0x00D8000,
		0x00DC000
	};
	static char *failures[]={
		"Processor instruction",
		"Processor data bus",
		"Processor data bus",
		"Processor data bus",
		"Adapter bus",
		"ROM checksum",
		"Base RAM",
		"Extended RAM",
		"82586 internal loopback",
		"82586 initialisation failure",
		"Adapter list configuration error"
	};

	/* Time to play MCA games */

	if (mc32_debug  &&  version_printed++ == 0)
		printk(KERN_DEBUG "%s", version);

	printk(KERN_INFO "%s: %s found in slot %d:", dev->name, cardname, slot);

	POS = mca_read_stored_pos(slot, 2);
	
	if(!(POS&1))
	{
		printk(" disabled.\n");
		return -ENODEV;
	}

	/* Allocate a new 'dev' if needed. */
	if (dev == NULL) {
		/*
		 * Don't allocate the private data here, it is done later
		 * This makes it easier to free the memory when this driver
		 * is used as a module.
		 */
		dev = init_etherdev(0, 0);
		if (dev == NULL)
			return -ENOMEM;
	}

	/* Fill in the 'dev' fields. */
	dev->base_addr = mca_io_bases[(POS>>1)&7];
	dev->mem_start = mca_mem_bases[(POS>>4)&7];
	
	POS = mca_read_stored_pos(slot, 4);
	if(!(POS&1))
	{
		printk("memory window disabled.\n");
		return -ENODEV;
	}

	POS = mca_read_stored_pos(slot, 5);
	
	i=(POS>>4)&3;
	if(i==3)
	{
		printk("invalid memory window.\n");
		return -ENODEV;
	}
	
	i*=16384;
	i+=16384;
	
	dev->mem_end=dev->mem_start + i;
	
	dev->irq = ((POS>>2)&3)+9;
	
	printk("io 0x%3lX irq %d mem 0x%lX (%dK)\n",
		dev->base_addr, dev->irq, dev->mem_start, i/1024);
	
	
	/* We ought to set the cache line size here.. */
	
	
	/*
	 *	Go PROM browsing
	 */
	 
	printk("%s: Address ", dev->name);
	 
	/* Retrieve and print the ethernet address. */
	for (i = 0; i < 6; i++)
	{
		mca_write_pos(slot, 6, i+12);
		mca_write_pos(slot, 7, 0);
	
		printk(" %2.2x", dev->dev_addr[i] = mca_read_pos(slot,3));
	}

	mca_write_pos(slot, 6, 0);
	mca_write_pos(slot, 7, 0);

	POS = mca_read_stored_pos(slot, 4);
	
	if(POS&2)
		printk(" : BNC port selected.\n");
	else 
		printk(" : AUI port selected.\n");
		
	POS=inb(dev->base_addr+HOST_CTRL);
	POS|=HOST_CTRL_ATTN|HOST_CTRL_RESET;
	POS&=~HOST_CTRL_INTE;
	outb(POS, dev->base_addr+HOST_CTRL);
	/* Reset adapter */
	udelay(100);
	/* Reset off */
	POS&=~(HOST_CTRL_ATTN|HOST_CTRL_RESET);
	outb(POS, dev->base_addr+HOST_CTRL);
	
	udelay(300);
	
	/*
	 *	Grab the IRQ
	 */

	if(request_irq(dev->irq, &mc32_interrupt, 0, cardname, dev))
	{
		printk("%s: unable to get IRQ %d.\n",
				   dev->name, dev->irq);
		return -EAGAIN;
	}

	/* Initialize the device structure. */
	if (dev->priv == NULL) {
		dev->priv = kmalloc(sizeof(struct mc32_local), GFP_KERNEL);
		if (dev->priv == NULL)
		{
			free_irq(dev->irq, dev);
			return -ENOMEM;
		}
	}

	memset(dev->priv, 0, sizeof(struct mc32_local));
	lp = (struct mc32_local *)dev->priv;
	lp->slot = slot;

	i=0;

	base = inb(dev->base_addr);
	
	while(base==0xFF)
	{
		i++;
		if(i==1000)
		{
			printk("%s: failed to boot adapter.\n", dev->name);
			free_irq(dev->irq, dev);
			return -ENODEV;
		}
		udelay(1000);
		if(inb(dev->base_addr+2)&(1<<5))
			base = inb(dev->base_addr);
	}

	if(base>0)
	{
		if(base < 0x0C)
			printk("%s: %s%s.\n", dev->name, failures[base-1],
				base<0x0A?" test failure":"");
		else
			printk("%s: unknown failure %d.\n", dev->name, base);
		free_irq(dev->irq, dev);
		return -ENODEV;
	}
	
	base=0;
	for(i=0;i<4;i++)
	{
		int n=0;
	
		while(!(inb(dev->base_addr+2)&(1<<5)))
		{
			n++;
			udelay(50);
			if(n>100)
			{
				printk(KERN_ERR "%s: mailbox read fail (%d).\n", dev->name, i);
				free_irq(dev->irq, dev);
				return -ENODEV;
			}
		}

		base|=(inb(dev->base_addr)<<(8*i));
	}
	
	lp->exec_box=bus_to_virt(dev->mem_start+base);
	
	base=lp->exec_box->data[1]<<16|lp->exec_box->data[0];  
	
	lp->base = dev->mem_start+base;
	
	lp->rx_box=bus_to_virt(lp->base + lp->exec_box->data[2]); 
	lp->tx_box=bus_to_virt(lp->base + lp->exec_box->data[3]);
	
	lp->stats = bus_to_virt(lp->base + lp->exec_box->data[5]);

	/*
	 *	Descriptor chains (card relative)
	 */
	 
	lp->tx_chain 		= lp->exec_box->data[8];   /* Transmit list start offset */
	lp->rx_chain 		= lp->exec_box->data[10];  /* Receive list start offset */
	lp->tx_len 		= lp->exec_box->data[9];   /* Transmit list count */ 
	lp->rx_len 		= lp->exec_box->data[11];  /* Receive list count */
	
	printk("%s: Firmware Rev %d. %d RX buffers, %d TX buffers. Base of 0x%08X.\n",
		dev->name, lp->exec_box->data[12], lp->rx_len, lp->tx_len, lp->base);

	dev->open		= mc32_open;
	dev->stop		= mc32_close;
	dev->hard_start_xmit	= mc32_send_packet;
	dev->get_stats		= mc32_get_stats;
	dev->set_multicast_list = mc32_set_multicast_list;
	
	lp->rx_halted		= 1;
	lp->tx_halted		= 1;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);
	
	return 0;
}

/*
 *	Polled command stuff 
 */
 
static void mc32_ring_poll(struct device *dev)
{
	int ioaddr = dev->base_addr;
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
}


/*
 *	Send exec commands. This requires a bit of explaining.
 *
 *	You feed the card a command, you wait, it interrupts you get a 
 *	reply. All well and good. The complication arises because you use
 *	commands for filter list changes which come in at bh level from things
 *	like IPV6 group stuff.
 *
 *	We have a simple state machine
 *
 *	0	- nothing issued
 *	1	- command issued, wait reply
 *	2	- reply waiting - reader then goes to state 0
 *	3	- command issued, trash reply. In which case the irq
 *		  takes it back to state 0
 */
 

/*
 *	Send command from interrupt state
 */

static int mc32_command_nowait(struct device *dev, u16 cmd, void *data, int len)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	
	if(lp->exec_pending)
		return -1;
		
	lp->exec_pending=3;
	lp->exec_box->mbox=0;
	lp->exec_box->mbox=cmd;
	memcpy((void *)lp->exec_box->data, data, len);
	barrier();	/* the memcpy forgot the volatile so be sure */

	/* Send the command */
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	outb(1<<6, ioaddr+HOST_CMD);	
	return 0;
}


/*
 *	Send command and block for results. On completion spot and reissue
 *	multicasts
 */
  
static int mc32_command(struct device *dev, u16 cmd, void *data, int len)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	unsigned long flags;
	int ret = 0;
	
	/*
	 *	Wait for a command
	 */
	 
	save_flags(flags);
	cli();
	 
	while(lp->exec_pending)
		sleep_on(&lp->event);
		
	/*
	 *	Issue mine
	 */

	lp->exec_pending=1;
	
	restore_flags(flags);
	
	lp->exec_box->mbox=0;
	lp->exec_box->mbox=cmd;
	memcpy((void *)lp->exec_box->data, data, len);
	barrier();	/* the memcpy forgot the volatile so be sure */

	/* Send the command */
	while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR));
	outb(1<<6, ioaddr+HOST_CMD);	

	save_flags(flags);
	cli();

	while(lp->exec_pending!=2)
	  sleep_on(&lp->event);
	lp->exec_pending=0;
	restore_flags(flags);
	
	if(lp->exec_box->mbox&(1<<13))
		ret = -1;

	/*
	 *	A multicast set got blocked - do it now
	 */
		
	if(lp->mc_reload_wait)
		mc32_reset_multicast_list(dev);

	return ret;
}


/*
 *	RX abort
 */
 
static void mc32_rx_abort(struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;

	mc32_ring_poll(dev);	
	
	lp->rx_box->mbox=0;
	outb(HOST_CMD_SUSPND_RX, ioaddr+HOST_CMD);	/* Suspend reception */
	
	mc32_ring_poll(dev);	
}

 
/*
 *	RX enable
 */
 
static void mc32_rx_begin(struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;

	/* Tell the card start reception at the first descriptor */ 
	lp->rx_box->data[0]=lp->rx_chain; 
 
	mc32_ring_poll(dev);	
	
	lp->rx_box->mbox=0;
	outb(HOST_CMD_START_RX, ioaddr+HOST_CMD);	/* GO */

	mc32_ring_poll(dev);
	lp->rx_halted=0;
}

static void mc32_tx_abort(struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
		
	mc32_ring_poll(dev);
	
	lp->tx_box->mbox=0;
	outb(HOST_CMD_SUSPND_TX, ioaddr+HOST_CMD);	/* Suspend */

	mc32_flush_tx_ring(lp); 

	mc32_ring_poll(dev);
}

/*
 *	TX enable
 */
 
static void mc32_tx_begin(struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	
	mc32_ring_poll(dev);

	lp->tx_box->mbox=0;
	outb(HOST_CMD_RESTRT_TX, ioaddr+HOST_CMD);	/* GO */

	mc32_ring_poll(dev);	
	lp->tx_halted=0;
}

	
/*
 *	Load the rx ring.
 */
 
static int mc32_load_rx_ring(struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int i;
	u16 rx_base;
	volatile struct skb_header *p;
	
	rx_base=lp->rx_chain;

	for(i=0;i<RX_RING_LEN;i++)
	{
		lp->rx_ring[i].skb=alloc_skb(1532, GFP_KERNEL);
		skb_reserve(lp->rx_ring[i].skb, 18);  

		if(lp->rx_ring[i].skb==NULL)
		{
			for(;i>=0;i--)
				kfree_skb(lp->rx_ring[i].skb);
			return -ENOBUFS;
		}
		
		p=bus_to_virt(lp->base+rx_base);
				
		p->control=0;
		p->data=virt_to_bus(lp->rx_ring[i].skb->data);
		p->status=0;
		p->length=1532;
	
		lp->rx_ring[i].p=p; 
		rx_base=p->next; 
	}

	lp->rx_ring[i-1].p->control |= CONTROL_EOL;

	lp->rx_ring_tail=0; 

	lp->rx_box->mbox=0;   /* check: needed ? */
	return 0;
}	

static void mc32_flush_rx_ring(struct mc32_local *lp)
{
	int i; 
	for(i=0; i < RX_RING_LEN; i++) { 
		kfree_skb(lp->rx_ring[i].skb);
		lp->rx_ring[i].p=NULL; 
	} 
}


/* Load the addresses of the on-card buffer descriptors into main memory */ 
static void mc32_load_tx_ring(struct device *dev)
{ 
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	volatile struct skb_header *p;
	int i; 
	u16 tx_base;

	tx_base=lp->tx_box->data[0]; 

	/* Read the 'next' pointers from the on-card list into      */
	/* our tx_ring array so we can reduce slow shared-mem reads */ 

	for(i=0;i<lp->tx_len;i++) 
	{
		p=bus_to_virt(lp->base+tx_base);
		lp->tx_ring[i].p=p; 
		lp->tx_ring[i].skb=NULL;

		tx_base=p->next;
	}

	lp->tx_ring_head=lp->tx_ring_tail=0; 
} 

static void mc32_flush_tx_ring(struct mc32_local *lp)
{
	
	if(lp->tx_ring_tail!=lp->tx_ring_head)
	{
		int i;
	
		if(lp->tx_ring_tail < lp->tx_ring_head)
		{
			for(i=lp->tx_ring_tail;i<lp->tx_ring_head;i++)
			{
				dev_kfree_skb(lp->tx_ring[i].skb);
				lp->tx_ring[i].skb=NULL;
				lp->tx_ring[i].p=NULL; 
			}
		}
		else
		{
			for(i=lp->tx_ring_tail; i<TX_RING_LEN; i++) 
			{
				dev_kfree_skb(lp->tx_ring[i].skb);
				lp->tx_ring[i].skb=NULL;
				lp->tx_ring[i].p=NULL; 
			}
			for(i=0; i<lp->tx_ring_head; i++) 
			{
				dev_kfree_skb(lp->tx_ring[i].skb);
				lp->tx_ring[i].skb=NULL;
				lp->tx_ring[i].p=NULL; 
			}
		}
	}
	
	/* -1 so that tx_ring_head cannot "lap" tx_ring_tail, */
	/* which would be bad news for mc32_tx_ring as cur. implemented */ 
	atomic_set(&lp->tx_count, TX_RING_LEN-1); 
	lp->tx_ring_tail=lp->tx_ring_head=0;
}
 	
/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 */

static int mc32_open(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	u16 zero_word=0;
	u8 one=1;
	u8 regs;
	u16 descnumbuffs[2] = {TX_RING_LEN, RX_RING_LEN};
	
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/*
	 *	Interrupts enabled
	 */

	regs=inb(ioaddr+HOST_CTRL);
	regs|=HOST_CTRL_INTE;
	outb(regs, ioaddr+HOST_CTRL);
	

	/*
	 *	Send the indications on command
	 */

	mc32_command(dev, 4, &one, 2);

	/*
	 *	Send the command sequence "abort, resume" for RX and TX.
	 *	The abort cleans up the buffer chains if needed.
	 */

	mc32_rx_abort(dev);
	mc32_tx_abort(dev);
	
	/* Ask card to set up on-card descriptors to our spec */ 

	if(mc32_command(dev, 8, descnumbuffs, 4)) { 
		printk("%s: %s rejected our buffer configuration!\n",
	 	       dev->name, cardname);
		return -ENOBUFS; 
	}
	
	/* Report new configuration */ 
	mc32_command(dev, 6, NULL, 0); 

	lp->tx_chain 		= lp->exec_box->data[8];   /* Transmit list start offset */
	lp->rx_chain 		= lp->exec_box->data[10];  /* Receive list start offset */
	lp->tx_len 		= lp->exec_box->data[9];   /* Transmit list count */ 
	lp->rx_len 		= lp->exec_box->data[11];  /* Receive list count */
 
	/* Set Network Address */
	mc32_command(dev, 1, dev->dev_addr, 6);
	
	/* Set the filters */
	mc32_set_multicast_list(dev);
	
	/* Issue the 82586 workaround command - this is for "busy lans",
	   but basically means for all lans now days - has a performance
	   cost but best set */
	   
	/* mc32_command(dev, 0x0D, &zero_word, 2); */   /* 82586 bug workaround on  */

	/* Load the ring we just initialised */

	mc32_load_tx_ring(dev);
	
	if(mc32_load_rx_ring(dev))
	{
		mc32_close(dev);
		return -ENOBUFS;
	}
	
	/* And the resume command goes last */

	mc32_rx_begin(dev);
	mc32_tx_begin(dev);
	
	MOD_INC_USE_COUNT;

	return 0;
}


static int mc32_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;

	if (dev->tbusy) {
		/*
		 * If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead.
		 */
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5)
			return 1;
		printk(KERN_WARNING "%s: transmit timed out?\n", dev->name);
		/* Try to restart the adaptor. */
		dev->tbusy=0;
		dev->trans_start = jiffies;
	}

	/*
	 * Block a timer-based transmit from overlapping. This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0)
	{
		printk(KERN_WARNING "%s: Transmitter access conflict.\n", dev->name);
		dev_kfree_skb(skb);
	}
	else 
	{
		unsigned long flags;	       
		volatile struct skb_header *p, *np;

		save_flags(flags);
		cli();
		
		if(atomic_read(&lp->tx_count)==0)
		{
			dev->tbusy=1;
			restore_flags(flags);
			return 1;
		}

		atomic_dec(&lp->tx_count); 

		/* P is the last sending/sent buffer as a pointer */
		p=lp->tx_ring[lp->tx_ring_head].p; 
		
		lp->tx_ring_head++;
		lp->tx_ring_head&=(TX_RING_LEN-1); 

		/* NP is the buffer we will be loading */
		np=lp->tx_ring[lp->tx_ring_head].p; 

	        /* We will need this to flush the buffer out */
		lp->tx_ring[lp->tx_ring_head].skb=skb;
   	   
		np->length = (skb->len < 60) ? 60 : skb->len; 
			
		np->data	= virt_to_bus(skb->data);
   		np->status	= 0;
		np->control     = CONTROL_EOP | CONTROL_EOL;     
	        wmb();
		
		p->control     &= ~CONTROL_EOL;     /* Clear EOL on p */ 
	   
       	        dev->tbusy	= 0;	       /* Keep feeding me */		
	
	        restore_flags(flags);
	}
	return 0;
}

static void mc32_update_stats(struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	volatile struct mc32_stats *st = lp->stats; 
	
        u32 rx_errors=0; 

	/* The databook isn't at all clear about whether      */ 
        /* the host should suspend rx/tx here to avoid races. */
        /* Assuming that it should, my code here is probably  */
        /* a bit dodgey - RP                                  */

	/* TO DO: count rx_errors, tx_errors, figure out how to measure colisions */     

	/* RX */ 

    	rx_errors += lp->net_stats.rx_crc_errors += st->rx_crc_errors;
	st->rx_crc_errors = 0; 

    	rx_errors += lp->net_stats.rx_length_errors += st->rx_tooshort_errors;
	st->rx_tooshort_errors = 0; 

	rx_errors += lp->net_stats.rx_length_errors += st->rx_toolong_errors;
	st->rx_toolong_errors = 0; 

	rx_errors += lp->net_stats.rx_fifo_errors += st->rx_overrun_errors; 
	st->rx_overrun_errors = 0; 

	rx_errors += lp->net_stats.rx_frame_errors += st->rx_alignment_errors;
	st->rx_alignment_errors = 0; 

	rx_errors += lp->net_stats.rx_missed_errors += st->rx_outofresource_errors;
	st->rx_outofresource_errors = 0; 

	lp->net_stats.rx_errors = rx_errors;
	
        /* TX */ 

	/* How to count collisions when you're only 
           told frames which had 1 or 2-15? - RP */
	
	lp->net_stats.collisions += (st->tx_max_collisions * 16); 
	st->tx_max_collisions = 0;  

	lp->net_stats.tx_carrier_errors += st->tx_carrier_errors; 
	st->tx_carrier_errors = 0; 

	lp->net_stats.tx_fifo_errors += st->tx_underrun_errors; 
	st->tx_underrun_errors = 0; 
	
}


static void mc32_rx_ring(struct device *dev)
{
	struct mc32_local *lp=dev->priv;		
	volatile struct skb_header *p;
	u16 rx_ring_tail = lp->rx_ring_tail;
	u16 rx_old_tail = rx_ring_tail; 

	int x=0;
	
	do
	{ 
		p=lp->rx_ring[rx_ring_tail].p; 

		if(!(p->status & (1<<7))) { /* Not COMPLETED */ 
			break;
		} 
		if(p->status & (1<<6)) /* COMPLETED_OK */
		{		        

			u16 length=p->length;
			struct sk_buff *skb; 
			struct sk_buff *newskb; 

#ifdef DEBUG_IRQ 
 			printk("skb_header %p has frame, x==%d\n", p, x); 
#endif 

			/* Try to save time by avoiding a copy on big frames */

			if ((length > RX_COPYBREAK) 
			    && ((newskb=dev_alloc_skb(1532)) != NULL)) 
			{ 
				skb=lp->rx_ring[rx_ring_tail].skb;
				skb_put(skb, length);
				
				skb_reserve(newskb,18); 
				lp->rx_ring[rx_ring_tail].skb=newskb;  
				p->data=virt_to_bus(newskb->data);  
			} 
			else 
			{
				skb=dev_alloc_skb(length+2);  

				if(skb==NULL) 
				{ 
					lp->net_stats.rx_dropped++; 

					p->length = 1532; 
					p->status = 0;
					p->control = 0; 	        
					
					rx_ring_tail++;
					rx_ring_tail&=(RX_RING_LEN-1); 

					continue; /* better to use a goto? */ 
				}

				skb_reserve(skb,2);
				memcpy(skb_put(skb, length),
				       lp->rx_ring[rx_ring_tail].skb->data, length);
			}
			
			skb->protocol=eth_type_trans(skb,dev); 
			skb->dev=dev; 
 			lp->net_stats.rx_packets++; 
 			lp->net_stats.rx_bytes+=skb->len; 
			netif_rx(skb); 
		}
		else    /* NOT COMPLETED_OK */
		{
		        /* There was some sort of reception error.     */
                        /* This case should never occur unless         */
			/* the card is asked to upload damaged frames.  */
  
			/* do nothing */ 
		}

		p->length = 1532; 
		p->status = 0;
		p->control = 0; 	        

		rx_ring_tail++;
		rx_ring_tail&=(RX_RING_LEN-1); 
	}
        while(x++<48);  

	/* If there was actually a frame to be processed, */ 
	/* place the EL bit at the descriptor prior to the one to be filled next */ 

	if (rx_ring_tail != rx_old_tail) { 
		lp->rx_ring[(rx_ring_tail-1)&(RX_RING_LEN-1)].p->control |=  CONTROL_EOL; 
		lp->rx_ring[(rx_old_tail-1)&(RX_RING_LEN-1)].p->control  &= ~CONTROL_EOL; 

		lp->rx_ring_tail=rx_ring_tail; 
	}
}


static void mc32_tx_ring(struct device *dev) {

  struct mc32_local *lp=(struct mc32_local *)dev->priv;
  volatile struct skb_header *np;

  /* NB: lp->tx_count=TX_RING_LEN-1 so that tx_ring_head cannot "lap" tail here */

  while (lp->tx_ring_tail != lp->tx_ring_head)  
  {   
	  u16 t; 

	  t=(lp->tx_ring_tail+1)&(TX_RING_LEN-1); 
	  np=lp->tx_ring[t].p; 

	  if(!(np->status & (1<<7))) { /* Not COMPLETED */ 
		  break; 
	  } 

	  lp->net_stats.tx_packets++;

	  if(!(np->status & (1<<6))) /* Not COMPLETED_OK */
	  {
		  lp->net_stats.tx_errors++;   
		  
                  /* Error stats are stored on-card to be picked up by
	             mc32_update_stats() - RP */
	  }
	  
	  
	  /* Packets are sent in order - this is
	     basically a FIFO queue of buffers matching
	     the card ring */
	  lp->net_stats.tx_bytes+=lp->tx_ring[t].skb->len;
	  dev_kfree_skb(lp->tx_ring[t].skb);
	  lp->tx_ring[t].skb=NULL;
	  atomic_inc(&lp->tx_count);
	  dev->tbusy=0;
	  mark_bh(NET_BH);

	  lp->tx_ring_tail=t; 
  }
} 

/*
 * The typical workload of the driver:
 *   Handle the network interface interrupts.
 */
static void mc32_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct device *dev = dev_id;
	struct mc32_local *lp;
	int ioaddr, status, boguscount = 0;
	int rx_event = 0;
	int tx_event = 0; 
	int must_restart = 0;
	
	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", cardname, irq);
		return;
	}
	dev->interrupt = 1;
 
	ioaddr = dev->base_addr;
	lp = (struct mc32_local *)dev->priv;

	/* See whats cooking */

	while((inb(ioaddr+HOST_STATUS)&HOST_STATUS_CWR) && boguscount++<2000)
	{
		status=inb(ioaddr+HOST_CMD);

#ifdef DEBUG_IRQ		
		printk("Status TX%d RX%d EX%d OV%d BC%d\n",
			(status&7), (status>>3)&7, (status>>6)&1,
			(status>>7)&1, boguscount);
#endif
			
		switch(status&7)
		{
			case 0:
				break;
			case 6: /* TX fail */
			case 2:	/* TX ok */
				tx_event = 1; 
				break;
			case 3: /* Halt */
			case 4: /* Abort */
				lp->tx_halted=1;
				wake_up(&lp->event);
				break;
			case 5:
				lp->tx_halted=0;
				wake_up(&lp->event);
				break;
			default:
				printk("%s: strange tx ack %d\n", dev->name, status&7);
		}
		status>>=3;
		switch(status&7)
		{
			case 0:
				break;
			case 2:	/* RX */
				rx_event=1; 
				break;
			case 3:
			case 4:
				lp->rx_halted=1;
				wake_up(&lp->event);
				break;
			case 5:
				lp->rx_halted=0;
				wake_up(&lp->event);
				break;
			case 6:
				/* Out of RX buffers stat */
				/* Must restart */
				lp->net_stats.rx_dropped++;
				must_restart = 1; 	/* To restart */
				rx_event = 1; 

				break;
			default:
				printk("%s: strange rx ack %d\n", 
					dev->name, status&7);			
		}
		status>>=3;
		if(status&1)
		{
			
			/* 0=no 1=yes 2=replied, get cmd, 3 = wait reply & dump it */
			
			if(lp->exec_pending!=3) 
			{
				lp->exec_pending=2;
				wake_up(&lp->event);
			}
			else 
			{
				lp->exec_pending=0;
				wake_up(&lp->event);
			}
		}
		if(status&2)
		{
			/*
			 *	Update the stats as soon as
			 *	we have it flagged and can 
			 *	send an immediate reply (CRR set)
			 */
	
			if(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR)
			{
			 	mc32_update_stats(dev);
			        outb(0, ioaddr+HOST_CMD);
			}
			
		}
	}


	if(tx_event) { 
		mc32_tx_ring(dev);
	}

	/*
	 *	Process the receive ring and restart if the card 
	 *      stopped due to a shortage of free buffers.
         */
	 
	if(rx_event) {
		mc32_rx_ring(dev);
		if (must_restart) { 
			u16 rx_prev_tail=(lp->rx_ring_tail-1)&(RX_RING_LEN-1);

			/* Restart at the current rx_ring_tail */ 
			lp->rx_box->data[0]=lp->rx_ring[rx_prev_tail].p->next; 
			lp->rx_box->mbox=0; 

			while(!(inb(ioaddr+HOST_STATUS)&HOST_STATUS_CRR)); 
			outb(HOST_CMD_START_RX, ioaddr+HOST_CMD); /* Re-enable receives */
		}
	}

	dev->interrupt = 0;	

	return;
}


/* The inverse routine to mc32_open(). */

static int mc32_close(struct device *dev)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	int ioaddr = dev->base_addr;
	u8 regs;
	u16 one=1;

	/*
	 *	Send the indications on command (handy debug check)
	 */

	mc32_command(dev, 4, &one, 2);

	/* Abort RX and Abort TX */
	
	mc32_rx_abort(dev);	
	mc32_tx_abort(dev);
	
	/* Catch any waiting commands */
	
	while(lp->exec_pending==1)
		sleep_on(&lp->event);
		
	/* Ok the card is now stopping */	
	
	regs=inb(ioaddr+HOST_CTRL);
	regs&=~HOST_CTRL_INTE;
	outb(regs, ioaddr+HOST_CTRL);

	mc32_flush_rx_ring(lp);
	mc32_flush_tx_ring(lp);
	
	dev->tbusy = 1;
	dev->start = 0;

	/* Update the statistics here. */

	MOD_DEC_USE_COUNT;

	return 0;

}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */

static struct net_device_stats *mc32_get_stats(struct device *dev)
{
	struct mc32_local *lp;
	
	mc32_update_stats(dev); 

	lp = (struct mc32_local *)dev->priv;

	return &lp->net_stats;
}

/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */
static void do_mc32_set_multicast_list(struct device *dev, int retry)
{
	struct mc32_local *lp = (struct mc32_local *)dev->priv;
	u16 filt;

	if (dev->flags&IFF_PROMISC)
		/* Enable promiscuous mode */
		filt = 1;
	else if((dev->flags&IFF_ALLMULTI) || dev->mc_count > 10)
	{
		dev->flags|=IFF_PROMISC;
		filt = 1;
	}
	else if(dev->mc_count)
	{
		unsigned char block[62];
		unsigned char *bp;
		struct dev_mc_list *dmc=dev->mc_list;
		
		int i;
		
		filt = 0;
		
		if(retry==0)
			lp->mc_list_valid = 0;
		if(!lp->mc_list_valid)
		{
			block[1]=0;
			block[0]=dev->mc_count;
			bp=block+2;
		
			for(i=0;i<dev->mc_count;i++)
			{
				memcpy(bp, dmc->dmi_addr, 6);
				bp+=6;
				dmc=dmc->next;
			}
			if(mc32_command_nowait(dev, 2, block, 2+6*dev->mc_count)==-1)
			{
				lp->mc_reload_wait = 1;
				return;
			}
			lp->mc_list_valid=1;
		}
	}
	else 
	{
		filt = 0;
	}
	if(mc32_command_nowait(dev, 0, &filt, 2)==-1)
	{
		lp->mc_reload_wait = 1;
	}
}

static void mc32_set_multicast_list(struct device *dev)
{
	do_mc32_set_multicast_list(dev,0);
}

static void mc32_reset_multicast_list(struct device *dev)
{
	do_mc32_set_multicast_list(dev,1);
}

#ifdef MODULE

static char devicename[9] = { 0, };
static struct device this_device = {
	devicename, /* will be inserted by linux/drivers/net/mc32_init.c */
	0, 0, 0, 0,
	0, 0,  /* I/O address, IRQ */
	0, 0, 0, NULL, mc32_probe };

int init_module(void)
{
	int result;

	if ((result = register_netdev(&this_device)) != 0)
		return result;

	return 0;
}

void cleanup_module(void)
{
	int slot;
	
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	unregister_netdev(&this_device);

	/*
	 * If we don't do this, we can't re-insmod it later.
	 * Release irq/dma here, when you have jumpered versions and
	 * allocate them in mc32_probe1().
	 */
	 
	if (this_device.priv)
	{
		struct mc32_local *lp=this_device.priv;
		slot = lp->slot;
		mca_mark_as_unused(slot);
		mca_set_adapter_name(slot, NULL);
		kfree_s(this_device.priv, sizeof(struct mc32_local));
	}
	free_irq(this_device.irq, &this_device);
}

#endif /* MODULE */
