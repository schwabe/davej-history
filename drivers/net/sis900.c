/* sis900.c: A SiS 900/7016 PCI Fast Ethernet driver for Linux.
   Silicon Integrated System Corporation 
   Revision:	1.05	Aug 7 1999
   
   Modified from the driver which is originally written by Donald Becker. 
   
   This software may be used and distributed according to the terms
   of the GNU Public License (GPL), incorporated herein by reference.
   Drivers based on this skeleton fall under the GPL and must retain
   the authorship (implicit copyright) notice.
   
   References:
   SiS 7016 Fast Ethernet PCI Bus 10/100 Mbps LAN Controller with OnNow Support,
   preliminary Rev. 1.0 Jan. 14, 1998
   SiS 900 Fast Ethernet PCI Bus 10/100 Mbps LAN Single Chip with OnNow Support,
   preliminary Rev. 1.0 Nov. 10, 1998
   SiS 7014 Single Chip 100BASE-TX/10BASE-T Physical Layer Solution,
   preliminary Rev. 1.0 Jan. 18, 1998
   http://www.sis.com.tw/support/databook.htm
   
   Ollie Lho (ollie@sis.com.tw) 
   Chin-Shan Li (lcs@sis.com.tw) Added AMD Am79c901 HomePNA PHY support
   Rev 1.05 Aug. 7 1999 Jim Huang (cmhuang@sis.com.tw) Initial release
*/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
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
#include <asm/processor.h>      /* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/delay.h>

#include "sis900.h"

static const char *version =
"sis900.c:v1.05  8/07/99\n";

static int max_interrupt_work = 20;
#define sis900_debug debug
static int sis900_debug = 0;

static int multicast_filter_limit = 128;

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (4*HZ)

struct mac_chip_info {
        const char *name;
        u16 	vendor_id, device_id, flags;
        int 	io_size;
        struct device *(*probe) (struct mac_chip_info *mac, struct pci_dev * pci_dev, 
				 struct device * net_dev);
};
static struct device * sis900_mac_probe (struct mac_chip_info * mac, struct pci_dev * pci_dev,
					 struct device * net_dev);

static struct mac_chip_info  mac_chip_table[] = {
	{ "SiS 900 PCI Fast Ethernet", PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_900,
	  PCI_COMMAND_IO|PCI_COMMAND_MASTER, SIS900_TOTAL_SIZE, sis900_mac_probe},
	{ "SiS 7016 PCI Fast Ethernet",PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_7016,
	  PCI_COMMAND_IO|PCI_COMMAND_MASTER, SIS900_TOTAL_SIZE, sis900_mac_probe},
	{0,},                                          /* 0 terminated list. */
};

static struct mii_chip_info {
	const char * name;
	u16 phy_id0;
	u16 phy_id1;
} mii_chip_table[] = {
	{"SiS 900 Internal MII PHY", 0x001d, 0x8000},
	{"SiS 7014 Physical Layer Solution", 0x0016, 0xf830},
	{"AMD 79C901 10BASE-T PHY", 0x0000, 0x35b9},
	{"AMD 79C901 HomePNA PHY",  0x0000, 0x35c8},
	{0,},
};

struct mii_phy {
	struct mii_phy * next;
	struct mii_chip_info * chip_info;
	int phy_addr;
	u16 status;
};

typedef struct _EuphLiteDesc {
        u32     llink;
        unsigned char*  buf;
        u32     physAddr;
        /* Hardware sees the physical address of descriptor */
        u32     plink;
        u32     cmdsts;
        u32     bufPhys;
} EuphLiteDesc;

struct sis900_private {
        struct device *next_module;
        struct net_device_stats stats;
	struct pci_dev * pci_dev;

	struct mac_chip_info * mac;
	struct mii_phy * mii;

        struct timer_list timer;        		/* Media selection timer. */
        unsigned int cur_rx;			/* Index into the Rx buffer of next Rx pkt. */
        unsigned int cur_tx, dirty_tx, tx_flag;

        /* The saved address of a sent-in-place packet/buffer, for skfree(). */
        struct sk_buff* tx_skbuff[NUM_TX_DESC];
        EuphLiteDesc tx_buf[NUM_TX_DESC];       /* Tx bounce buffers */
        EuphLiteDesc rx_buf[NUM_RX_DESC];
        unsigned char *rx_bufs;
	unsigned char *tx_bufs;                 	/* Tx bounce buffer region. */
        unsigned int tx_full;			/* The Tx queue is full.    */
	int MediaSpeed;                         	/* user force speed         */
	int MediaDuplex;                        	/* user force duplex        */
        int full_duplex;                        	/* Full/Half-duplex.        */
        int speeds;                             	/* 100/10 Mbps.             */
        u16 LinkOn;
        u16 LinkChange;
};

#ifdef MODULE
#if LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Jim Huang <cmhuang@sis.com.tw>");
MODULE_DESCRIPTION("SiS 900 PCI Fast Ethernet driver");
MODULE_PARM(speeds, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(multicast_filter_limit, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(debug, "i");
#endif
#endif

static int sis900_open(struct device *dev);
static int sis900_mii_probe (struct device * dev);
static void sis900_init_rxfilter (struct device * dev);
static u16 read_eeprom(long ioaddr, int location);
static u16 mdio_read(struct device *dev, int phy_id, int location);
static void mdio_write(struct device *dev, int phy_id, int location, int val);
static void sis900_timer(unsigned long data);
static void sis900_tx_timeout(struct device *dev);
static void sis900_init_ring(struct device *dev);
static int sis900_start_xmit(struct sk_buff *skb, struct device *dev);
static int sis900_rx(struct device *dev);
static void sis900_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int sis900_close(struct device *dev);
static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd);
static struct enet_statistics *sis900_get_stats(struct device *dev);
static void set_rx_mode(struct device *dev);
static void sis900_reset(struct device *dev);
static u16 elAutoNegotiate(struct device *dev, int phy_id, int *duplex, int *speed);
static void elSetCapability(struct device *dev, int phy_id, int duplex, int speed);
static u16 elPMDreadMode(struct device *dev, int phy_id, int *speed, int *duplex);
static u16 elMIIpollBit(struct device *dev, int phy_id, int location, u16 mask, 
					u16 polarity, u16 *value);
static void elSetMediaType(struct device *dev, int speed, int duplex);

/* A list of all installed SiS900 devices, for removing the driver module. */
static struct device *root_sis900_dev = NULL;

/* walk through every ethernet PCI devices to see if some of them are matched with our card list*/
int sis900_probe (struct device * net_dev)
{
	int found = 0;
	struct pci_dev * pci_dev = NULL;
		
	if (!pci_present())
		return -ENODEV;
		
	while ((pci_dev = pci_find_class (PCI_CLASS_NETWORK_ETHERNET << 8, pci_dev)) != NULL) {
		/* pci_dev contains all ethernet devices */
		u32 pci_io_base;
		struct mac_chip_info * mac;

		for (mac = mac_chip_table; mac->vendor_id; mac++) {
			/* try to match our card list */
			if (pci_dev->vendor == mac->vendor_id &&
			    pci_dev->device == mac->device_id)
				break;
		}
		
		if (mac->vendor_id == 0)
			/* pci_dev does not match any of our cards */
			continue;
		
		/* now, pci_dev should be either 900 or 7016 */
		pci_io_base = pci_dev->base_address[0] & PCI_BASE_ADDRESS_IO_MASK;
		if ((mac->flags & PCI_COMMAND_IO ) && 
		    check_region(pci_io_base, mac->io_size))
			continue;
		
		/* setup various bits in PCI command register */
		pci_set_master(pci_dev);

		/* do the real low level jobs */
		net_dev = mac->probe(mac, pci_dev, net_dev);
		
		if (net_dev != NULL) {
			found++;
		}
		net_dev = NULL;
	}
	return found ? 0 : -ENODEV;
}

static struct device * sis900_mac_probe (struct mac_chip_info * mac, struct pci_dev * pci_dev, 
					 struct device * net_dev)
{
	struct sis900_private *sis_priv;
	long ioaddr = pci_dev->base_address[0] & ~3;
	int irq = pci_dev->irq;
	static int did_version = 0;
	u16 signature;
	int i;

	if (did_version++ == 0)
		printk(KERN_INFO "%s", version);

	/* check to see if  we have sane EEPROM */
	signature = (u16) read_eeprom(ioaddr, EEPROMSignature);    
	if (signature == 0xffff || signature == 0x0000) {
		printk (KERN_INFO "Error EERPOM read %x\n", signature);
		return NULL;
	}

	if ((net_dev = init_etherdev(net_dev, 0)) == NULL)
		return NULL;

	printk(KERN_INFO "%s: %s at %#lx, IRQ %d, ", net_dev->name, mac->name,
	       ioaddr, irq);

	/* get MAC address from EEPROM */
	for (i = 0; i < 3; i++)
		((u16 *)(net_dev->dev_addr))[i] = read_eeprom(ioaddr, i+EEPROMMACAddr);
	for (i = 0; i < 5; i++)
		printk("%2.2x:", (u8)net_dev->dev_addr[i]);
	printk("%2.2x.\n", net_dev->dev_addr[i]);

	if ((net_dev->priv = kmalloc(sizeof(struct sis900_private), GFP_KERNEL)) == NULL)
	    /* FIXME: possible mem leak here */
	    return NULL;

	sis_priv = net_dev->priv;
	memset(sis_priv, 0, sizeof(struct sis900_private));

	/* We do a request_region() to register /proc/ioports info. */
	request_region(ioaddr, mac->io_size, net_dev->name);
	net_dev->base_addr = ioaddr;
	net_dev->irq = irq;
	sis_priv->pci_dev = pci_dev;
	sis_priv->mac = mac;

	/* probe for mii transciver */
	if (sis900_mii_probe(net_dev) == 0) {
		/* FIXME: how to clean up this */
		release_region (ioaddr, mac->io_size);
		return NULL;
	}

	sis_priv->next_module = root_sis900_dev;
	root_sis900_dev = net_dev;

	/* The SiS900-specific entries in the device structure. */
	net_dev->open = &sis900_open;
	net_dev->hard_start_xmit = &sis900_start_xmit;
	net_dev->stop = &sis900_close;
	net_dev->get_stats = &sis900_get_stats;
	net_dev->set_multicast_list = &set_rx_mode;
	net_dev->do_ioctl = &mii_ioctl;

	return net_dev;
}

static int sis900_mii_probe (struct device * net_dev)
{
	struct sis900_private * sis_priv = (struct sis900_private *)net_dev->priv;
	int phy_addr;
	
	sis_priv->mii = NULL;
	
	/* search for total of 32 possible mii phy address */
	for (phy_addr = 0; phy_addr < 32; phy_addr++) {
		u16 mii_status;
		u16 phy_id0, phy_id1;
		int i;
		
		mii_status = mdio_read(net_dev, phy_addr, MII_STATUS);
		if (mii_status == 0xffff || mii_status == 0x0000)
			/* the mii is not accessable, try next one */
			continue;
		
		phy_id0 = mdio_read(net_dev, phy_addr, MII_PHY_ID0);
		phy_id1 = mdio_read(net_dev, phy_addr, MII_PHY_ID1);
		
		/* search our mii table for the current mii */ 
		for (i = 0; mii_chip_table[i].phy_id1; i++)
			if (phy_id0 == mii_chip_table[i].phy_id0) {
				struct mii_phy * mii_phy;
				
				printk(KERN_INFO 
				       "%s: %s transceiver found at address %d.\n",
				       net_dev->name, mii_chip_table[i].name, 
				       phy_addr);;
				if ((mii_phy = kmalloc(sizeof(struct mii_phy), GFP_KERNEL)) != NULL) {
					mii_phy->chip_info = mii_chip_table+i;
					mii_phy->phy_addr = phy_addr;
					mii_phy->status = mdio_read(net_dev, phy_addr, 
								    MII_STATUS);
					mii_phy->next = sis_priv->mii;
					sis_priv->mii = mii_phy;
				}
				/* the current mii is on our mii_info_table, quit searching (table) */
				break;
			}
	}
	
	if (sis_priv->mii == NULL) {
		printk(KERN_INFO "%s: No MII transceivers found!\n", net_dev->name);
		return 0;
	}

	/* FIXME: AMD stuff should be added */
	/* auto negotiate FIXME: not completed */
	elSetCapability(net_dev, sis_priv->mii->phy_addr, 1, 100);
	sis_priv->mii->status = elAutoNegotiate(net_dev, sis_priv->mii->phy_addr,
						&sis_priv->full_duplex, 
						&sis_priv->speeds);
	
	if (sis_priv->mii->status & MIISTAT_LINK) 
		sis_priv->LinkOn = TRUE;
	else
		sis_priv->LinkOn = FALSE;
	
	sis_priv->LinkChange = FALSE;
	
	return 1;
}

/* Delay between EEPROM clock transitions. */
#define eeprom_delay()  inl(ee_addr)

/* Read Serial EEPROM through EEPROM Access Register, Note that location is 
   in word (16 bits) unit */
static u16 read_eeprom(long ioaddr, int location)
{
	int i;
        u16 retval = 0;
        long ee_addr = ioaddr + mear;
        u32 read_cmd = location | EEread;

        outl(0, ee_addr);
        eeprom_delay();
        outl(EECLK, ee_addr);
        eeprom_delay();

        /* Shift the read command (9) bits out. */
        for (i = 8; i >= 0; i--) {
                u32 dataval = (read_cmd & (1 << i)) ? EEDI | EECS : EECS;
                outl(dataval, ee_addr);
                eeprom_delay();
                outl(dataval | EECLK, ee_addr);
                eeprom_delay();
        }
        outb(EECS, ee_addr);
        eeprom_delay();

	/* read the 16-bits data in */
        for (i = 16; i > 0; i--) {
                outl(EECS, ee_addr);
                eeprom_delay();
                outl(EECS | EECLK, ee_addr);
                eeprom_delay();
                retval = (retval << 1) | ((inl(ee_addr) & EEDO) ? 1 : 0);
                eeprom_delay();
        }
		
        /* Terminate the EEPROM access. */
        outl(0, ee_addr);
        eeprom_delay();
        outl(EECLK, ee_addr);

        return (retval);
}

/* Read and write the MII management registers using software-generated
   serial MDIO protocol. Note that the command bits and data bits are
   send out seperately */

#define mdio_delay()    inl(mdio_addr)

static void mdio_idle(long mdio_addr)
{
        outl(MDIO | MDDIR, mdio_addr);
        mdio_delay();
        outl(MDIO | MDDIR | MDC, mdio_addr);
}

/* Syncronize the MII management interface by shifting 32 one bits out. */
static void mdio_reset(long mdio_addr)
{
        int i;

        for (i = 31; i >= 0; i--) {
                outl(MDDIR | MDIO, mdio_addr);
                mdio_delay();
                outl(MDDIR | MDIO | MDC, mdio_addr);
                mdio_delay();
        }
        return;
}

static u16 mdio_read(struct device *dev, int phy_id, int location)
{
        long mdio_addr = dev->base_addr + mear;
        int mii_cmd = MIIread|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
        u16 retval = 0;
        int i;

        mdio_reset(mdio_addr);
        mdio_idle(mdio_addr);

        for (i = 15; i >= 0; i--) {
                int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;
                outl(dataval, mdio_addr);
		mdio_delay();
                outl(dataval | MDC, mdio_addr);
		mdio_delay();
        }

        /* Read the 16 data bits. */
        for (i = 16; i > 0; i--) {
                outl(0, mdio_addr);
                mdio_delay();
                retval = (retval << 1) | ((inl(mdio_addr) & MDIO) ? 1 : 0);
                outl(MDC, mdio_addr);
                mdio_delay();
        }
        return retval;
}

static void mdio_write(struct device *dev, int phy_id, int location, int value)
{
        long mdio_addr = dev->base_addr + mear;
        int mii_cmd = MIIwrite|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
        int i;

        mdio_reset(mdio_addr);
        mdio_idle(mdio_addr);

        /* Shift the command bits out. */
        for (i = 15; i >= 0; i--) {
                int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;
                outb(dataval, mdio_addr);
                mdio_delay();
                outb(dataval | MDC, mdio_addr);
                mdio_delay();
        }
        mdio_delay();

	/* Shift the value bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (value & (1 << i)) ? MDDIR | MDIO : MDDIR;
	        outl(dataval, mdio_addr);
	        mdio_delay();
	        outl(dataval | MDC, mdio_addr);
	       	mdio_delay();
	}
	mdio_delay();
	
        /* Clear out extra bits. */
        for (i = 2; i > 0; i--) {
                outb(0, mdio_addr);
                mdio_delay();
                outb(MDC, mdio_addr);
                mdio_delay();
        }
        return;
}

static int
sis900_open(struct device *dev)
{
        struct sis900_private *sis_priv = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
	int i = 0;
	u32 status = TxRCMP | RxRCMP;

        /* Soft reset the chip. */
	sis900_reset(dev);

        if (request_irq(dev->irq, &sis900_interrupt, SA_SHIRQ, dev->name, dev)) {
                return -EAGAIN;
        }

        MOD_INC_USE_COUNT;      

        if ((sis_priv->tx_bufs = kmalloc(TX_BUF_SIZE * NUM_TX_DESC, GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "%s: Can't allocate a %d byte TX Bufs.\n",
		       dev->name, TX_BUF_SIZE * NUM_TX_DESC);
		return -ENOMEM;
	}
	if ((sis_priv->rx_bufs = kmalloc(RX_BUF_SIZE * NUM_RX_DESC, GFP_KERNEL)) == NULL) {
		kfree (sis_priv->tx_buf);
		printk(KERN_ERR "%s: Can't allocate a %d byte RX Bufs.\n",
		       dev->name, RX_BUF_SIZE * NUM_RX_DESC);
		return -ENOMEM;
	}

	sis900_init_rxfilter(dev);

	sis900_reset_tx_ring(dev);
	sis900_reset_rx_ring(dev);

        if (sis900_debug > 4)
                printk(KERN_INFO "%s: txdp:%8.8x\n", dev->name, inl(ioaddr + txdp));

	/* Check that the chip has finished the reset. */
	while (status && (i++ < 30000)) {
		status ^= (inl(isr + ioaddr) & status);
        }

        outl(PESEL, ioaddr + cfg);

	/* FIXME: should be removed, and replaced by AutoNeogotiate stuff */
        outl((RX_DMA_BURST << RxMXDMA_shift) | (RxDRNT_10 << RxDRNT_shift), 
	     ioaddr + rxcfg);
        outl(TxATP | (TX_DMA_BURST << TxMXDMA_shift) | (TX_FILL_THRESH << TxFILLT_shift) | TxDRNT_10,
	     ioaddr + txcfg);

	if (sis_priv->LinkOn) {
		printk(KERN_INFO "%s: Media Type %s%s-duplex.\n",
		       dev->name, 
		       sis_priv->speeds == HW_SPEED_100_MBPS ? "100mbps " : "10mbps ",
		       sis_priv->full_duplex == FDX_CAPABLE_FULL_SELECTED ? "full" : "half");
	} else {
		printk(KERN_INFO "%s: Media Link Off\n", dev->name);
	}

        set_rx_mode(dev);

        dev->tbusy = 0;
        dev->interrupt = 0;
        dev->start = 1;

        /* Enable all known interrupts by setting the interrupt mask. */
        outl((RxRCMP|RxOK|RxERR|RxORN|RxSOVR|TxOK|TxERR|TxURN), ioaddr + imr);
        outl(RxENA, ioaddr + cr);
        outl(IE, ioaddr + ier);

        /* Set the timer to switch to check for link beat and perhaps switch
           to an alternate media type. */
        init_timer(&sis_priv->timer);
        sis_priv->timer.expires = jiffies + 2*HZ;
        sis_priv->timer.data = (unsigned long)dev;
        sis_priv->timer.function = &sis900_timer;
        add_timer(&sis_priv->timer);

        return 0;
}

/* set receive filter address to our MAC address */
static void
sis900_init_rxfilter (struct device * net_dev)
{
	long ioaddr = net_dev->base_addr;
	u32 rfcrSave;
	u32 i;
	
	rfcrSave = inl(rfcr + ioaddr);

	/* disable packet filtering before setting filter */
	outl(rfcrSave & ~RFEN, rfcr);

	for (i = 0 ; i < 3 ; i++) {
		u32 w;

		w = (u32) *((u16 *)(net_dev->dev_addr)+i);
		outl((i << RFADDR_shift), ioaddr + rfcr);
		outl(w, ioaddr + rfdr);

		if (sis900_debug > 4) {
			printk(KERN_INFO "%s: Receive Filter Addrss[%d]=%x\n",
			       net_dev->name, i, inl(ioaddr + rfdr));
		}
	}
	outl(rfcrSave, rfcr + ioaddr);
}

static void sis900_timer(unsigned long data)
{
        struct device *dev = (struct device *)data;
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int next_tick = 2*HZ;
        u16 status;

	/* FIXME: call auto negotiate routine to detect link status */
        if (!tp->LinkOn) {
                status = mdio_read(dev, tp->mii->phy_addr, MII_STATUS);
		if (status & MIISTAT_LINK) {
                	elPMDreadMode(dev, tp->mii->phy_addr,
				      &tp->speeds, &tp->full_duplex);
			tp->LinkOn = TRUE;
                        printk(KERN_INFO "%s: Media Link On %s%s-duplex \n", dev->name,
			       tp->speeds == HW_SPEED_100_MBPS ? "100mbps " : "10mbps ",
			       tp->full_duplex == FDX_CAPABLE_FULL_SELECTED ? "full" : "half");
		}
        } else { // previous link on
                status = mdio_read(dev, tp->mii->phy_addr, MII_STATUS);
		if (!(status & MIISTAT_LINK)) {
			tp->LinkOn = FALSE;
                        printk(KERN_INFO "%s: Media Link Off\n", dev->name);
		}
        }

        if (next_tick) {
                tp->timer.expires = jiffies + next_tick;
                add_timer(&tp->timer);
        }
}

static void sis900_tx_timeout(struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
        int i;

        if (sis900_debug > 0)
                printk(KERN_INFO "%s: Transmit timeout, status %2.2x %4.4x \n",
                           dev->name, inl(ioaddr + cr), inl(ioaddr + isr));

        /* Disable interrupts by clearing the interrupt mask. */
        outl(0x0000, ioaddr + imr);

        /* Emit info to figure out what went wrong. */
	if (sis900_debug > 1) {
	        printk(KERN_INFO "%s:Tx queue start entry %d dirty entry %d.\n",
                		   dev->name, tp->cur_tx, tp->dirty_tx);
        	for (i = 0; i < NUM_TX_DESC; i++) 
                	printk(KERN_INFO "%s:  Tx descriptor %d is %8.8x.%s\n",
                        	dev->name, i, (unsigned int)&tp->tx_buf[i],
                     		i == tp->dirty_tx % NUM_TX_DESC ?
                      		" (queue head)" : "");
	}


        tp->cur_rx = 0; 

        {       /* Save the unsent Tx packets. */
                struct sk_buff *saved_skb[NUM_TX_DESC], *skb;
                int j;
                for (j = 0; tp->cur_tx - tp->dirty_tx > 0 ; j++, tp->dirty_tx++)
                        saved_skb[j]=tp->tx_skbuff[tp->dirty_tx % NUM_TX_DESC];
                tp->dirty_tx = tp->cur_tx = 0;

                for (i = 0; i < j; i++) {
                        skb = tp->tx_skbuff[i] = saved_skb[i];
                        /* Always alignment */
                        memcpy((unsigned char*)(tp->tx_buf[i].buf),
                                                skb->data, skb->len);
                        tp->tx_buf[i].cmdsts = OWN | skb->len;
                }
                outl(TxENA, ioaddr + cr);
                tp->cur_tx = i;
                while (i < NUM_TX_DESC)
                        tp->tx_skbuff[i++] = 0;
                if (tp->cur_tx - tp->dirty_tx < NUM_TX_DESC) {/* Typical path */
                        dev->tbusy = 0;
                        tp->tx_full = 0;
                } else {
                        tp->tx_full = 1;
                }
        }

        dev->trans_start = jiffies;
        tp->stats.tx_errors++;
        /* Enable all known interrupts by setting the interrupt mask. */
        outl((RxRCMP|RxOK|RxERR|RxORN|RxSOVR|TxOK|TxERR|TxURN), ioaddr + imr);
        return;
}

/* Reset (Initialize) the Tx rings, along with various 'dev' bits. */
static void
sis900_reset_tx_ring(struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr; 
        int i;
	
        tp->tx_full = 0;
        tp->dirty_tx = tp->cur_tx = 0;
	
        /* Tx Buffer */
        for (i = 0; i < NUM_TX_DESC; i++) {
		tp->tx_skbuff[i] = 0;
		tp->tx_buf[i].buf = &tp->tx_bufs[i*TX_BUF_SIZE];
                tp->tx_buf[i].bufPhys =
			virt_to_bus(&tp->tx_bufs[i*TX_BUF_SIZE]);
        }

        /* Tx Descriptor */
        for (i = 0; i< NUM_TX_DESC; i++) {
                tp->tx_buf[i].llink = (u32)
                        &(tp->tx_buf[((i+1) < NUM_TX_DESC) ? (i+1) : 0]);
                tp->tx_buf[i].plink = (u32)
                        virt_to_bus(&(tp->tx_buf[((i+1) < NUM_TX_DESC) ?
                                (i+1) : 0].plink));
                tp->tx_buf[i].physAddr=
                                virt_to_bus(&(tp->tx_buf[i].plink));
                tp->tx_buf[i].cmdsts=0;
        }

        outl((u32)tp->tx_buf[0].physAddr, ioaddr + txdp); 
}

/* Reset (Initialize) the Rx rings, along with various 'dev' bits. */ 
static void 
sis900_reset_rx_ring(struct device *dev) 
{ 
        struct sis900_private *tp = (struct sis900_private *)dev->priv; 
        long ioaddr = dev->base_addr; 
        int i; 
 
        tp->cur_rx = 0; 

        /* Rx Buffer */
        for (i = 0; i < NUM_RX_DESC; i++) {
                tp->rx_buf[i].buf = &tp->rx_bufs[i*RX_BUF_SIZE];
                tp->rx_buf[i].bufPhys =
                                virt_to_bus(&tp->rx_bufs[i*RX_BUF_SIZE]);
        }

        /* Rx Descriptor */
        for (i = 0; i< NUM_RX_DESC; i++) {
                tp->rx_buf[i].llink = (u32)
                        &(tp->rx_buf[((i+1) < NUM_RX_DESC) ? (i+1) : 0]);
                tp->rx_buf[i].plink = (u32)
                        virt_to_bus(&(tp->rx_buf[((i+1) < NUM_RX_DESC) ?
                                (i+1) : 0].plink));
                tp->rx_buf[i].physAddr=
                                virt_to_bus(&(tp->rx_buf[i].plink));
                tp->rx_buf[i].cmdsts=RX_BUF_SIZE;
        }

        outl((u32)tp->rx_buf[0].physAddr, ioaddr + rxdp); 
}

static int
sis900_start_xmit(struct sk_buff *skb, struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
        int entry;

        /* Block a timer-based transmit from overlapping.  This could better be
           done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
        if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
                if (jiffies - dev->trans_start < TX_TIMEOUT)
                        return 1;
                sis900_tx_timeout(dev);
                return 1;
        }

        /* Calculate the next Tx descriptor entry. ????? */
        entry = tp->cur_tx % NUM_TX_DESC;

        tp->tx_skbuff[entry] = skb;

        if (sis900_debug > 5) {
                int i;
                printk(KERN_INFO "%s: SKB Tx Frame contents:(len=%d)",
                                                dev->name,skb->len);

                for (i = 0; i < skb->len; i++) {
                        printk("%2.2x ",
                        (u8)skb->data[i]);
                }
                printk(".\n");
        }

        memcpy(tp->tx_buf[entry].buf,
                                skb->data, skb->len);

        tp->tx_buf[entry].cmdsts=(OWN | skb->len);

        //tp->tx_buf[entry].plink = 0;
        outl(TxENA, ioaddr + cr);
        if (++tp->cur_tx - tp->dirty_tx < NUM_TX_DESC) {/* Typical path */
                clear_bit(0, (void*)&dev->tbusy);
        } else {
                tp->tx_full = 1;
        }

        /* Note: the chip doesn't have auto-pad! */

        dev->trans_start = jiffies;
        if (sis900_debug > 4)
                printk(KERN_INFO "%s: Queued Tx packet at "
                                "%p size %d to slot %d.\n",
                           dev->name, skb->data, (int)skb->len, entry);

        return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void sis900_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
        struct device *dev = (struct device *)dev_instance;
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int boguscnt = max_interrupt_work;
        int status;
        long ioaddr = dev->base_addr;

#if defined(__i386__)
        /* A lock to prevent simultaneous entry bug on Intel SMP machines. */
        if (test_and_set_bit(0, (void*)&dev->interrupt)
) {
                printk(KERN_INFO "%s: SMP simultaneous entry of "
                                "an interrupt handler.\n", dev->name);
                dev->interrupt = 0;     /* Avoid halting machine. */
                return;
        }
#else
        if (dev->interrupt) {
                printk(KERN_INFO "%s: Re-entering the "
                                "interrupt handler.\n", dev->name);
                return;
        }
        dev->interrupt = 1;
#endif

        do {
                status = inl(ioaddr + isr);

                if (sis900_debug > 4)
                        printk(KERN_INFO "%s: interrupt  status=%#4.4x "
                                "new intstat=%#4.4x.\n",
                                dev->name, status, inl(ioaddr + isr));

                if ((status & (TxURN|TxERR|TxOK | RxORN|RxERR|RxOK)) == 0) {
			/* nothing intresting happened */
                        break;
                }

                if (status & (RxOK|RxORN|RxERR)) /* Rx interrupt */
                        sis900_rx(dev);

                if (status & (TxOK | TxERR)) {
                        unsigned int dirty_tx;

                        if (sis900_debug > 5) {
                                printk(KERN_INFO "TxOK:tp->cur_tx:%d,"
                                                "tp->dirty_tx:%x\n",
                                        tp->cur_tx, tp->dirty_tx);
                        }
                        for (dirty_tx = tp->dirty_tx; dirty_tx < tp->cur_tx;
                                dirty_tx++)
                        {
                                int i;
                                int entry = dirty_tx % NUM_TX_DESC;
                                int txstatus = tp->tx_buf[entry].cmdsts;

                                if (sis900_debug > 4) {
                                        printk(KERN_INFO "%s:     Tx Frame contents:"
                                                "(len=%d)",
                                                dev->name, (txstatus & DSIZE));

                                        for (i = 0; i < (txstatus & DSIZE) ;
                                                                        i++) {
                                                printk("%2.2x ",
                                                (u8)(tp->tx_buf[entry].buf[i]));
                                        }
                                        printk(".\n");
                                }
                                if ( ! (txstatus & (OK | UNDERRUN)))
                                {
                                        if (sis900_debug > 1)
                                                printk(KERN_INFO "Tx NOT (OK,"
                                                        "UnderRun)\n");
                                        break;  /* It still hasn't been Txed */
                                }

                                /* Note: TxCarrierLost is always asserted
                                                at 100mbps.                 */
                                if (txstatus & (OWCOLL | ABORT)) {
                                        /* There was an major error, log it. */
                                        if (sis900_debug > 1)
                                                printk(KERN_INFO "Tx Out of "
                                                        " Window,Abort\n");
#ifndef final_version
                                        if (sis900_debug > 1)
                                                printk(KERN_INFO "%s: Transmit "
                                                    "error, Tx status %8.8x.\n",
                                                           dev->name, txstatus);
#endif
                                        tp->stats.tx_errors++;
                                        if (txstatus & ABORT) {
                                                tp->stats.tx_aborted_errors++;
                                        }
                                        if (txstatus & NOCARRIER)
                                                tp->stats.tx_carrier_errors++;
                                        if (txstatus & OWCOLL)
                                                tp->stats.tx_window_errors++;
#ifdef ETHER_STATS
                                        if ((txstatus & COLCNT)==COLCNT)
                                                tp->stats.collisions16++;
#endif
                                } else {
#ifdef ETHER_STATS
                                        /* No count for tp->stats.tx_deferred */
#endif
                                        if (txstatus & UNDERRUN) {
                                           if (sis900_debug > 2)
                                             printk(KERN_INFO "Tx UnderRun\n");
                                        }
                                        tp->stats.collisions +=
                                                        (txstatus >> 16) & 0xF;
#if LINUX_VERSION_CODE > 0x20119
                                        tp->stats.tx_bytes += txstatus & DSIZE;
#endif
                                        if (sis900_debug > 2)
                                           printk(KERN_INFO "Tx Transmit OK\n");
                                        tp->stats.tx_packets++;
                                }

                                /* Free the original skb. */
                                if (sis900_debug > 2)
                                        printk(KERN_INFO "Free original skb\n");
                                dev_kfree_skb(tp->tx_skbuff[entry]);
                                tp->tx_skbuff[entry] = 0;
                        } // for dirty

#ifndef final_version
                        if (tp->cur_tx - dirty_tx > NUM_TX_DESC) {
                                printk(KERN_INFO"%s: Out-of-sync dirty pointer,"
                                                " %d vs. %d, full=%d.\n",
                                                dev->name, dirty_tx,
                                                tp->cur_tx, tp->tx_full);
                                dirty_tx += NUM_TX_DESC;
                        }
#endif

                        if (tp->tx_full && dirty_tx > tp->cur_tx-NUM_TX_DESC) {
                                /* The ring is no longer full, clear tbusy. */
				if (sis900_debug > 3)
                                   printk(KERN_INFO "Tx Ring NO LONGER Full\n");
                                tp->tx_full = 0;
                                dev->tbusy = 0;
                                mark_bh(NET_BH);
                        }

                        tp->dirty_tx = dirty_tx;
                        if (sis900_debug > 2)
                           printk(KERN_INFO "TxOK,tp->cur_tx:%d,tp->dirty:%d\n",
                                                tp->cur_tx, tp->dirty_tx);
                } // if (TxOK | TxERR)

                /* Check uncommon events with one test. */
                if (status & (RxORN | TxERR | RxERR)) {
                        if (sis900_debug > 2)
                                printk(KERN_INFO "%s: Abnormal interrupt,"
                                        "status %8.8x.\n", dev->name, status);

                        if (status == 0xffffffff)
                                break;
                        if (status & (RxORN | RxERR))
                                tp->stats.rx_errors++;


                        if (status & RxORN) {
                                tp->stats.rx_over_errors++;
                        }
                }
                if (--boguscnt < 0) {
                        printk(KERN_INFO "%s: Too much work at interrupt, "
                                   "IntrStatus=0x%4.4x.\n",
                                   dev->name, status);
                        break;
                }
        } while (1);

        if (sis900_debug > 3)
                printk(KERN_INFO "%s: exiting interrupt, intr_status=%#4.4x.\n",
                           dev->name, inl(ioaddr + isr));

#if defined(__i386__)
        clear_bit(0, (void*)&dev->interrupt);
#else
        dev->interrupt = 0;
#endif
        return;
}

static int sis900_rx(struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
        u16 cur_rx = tp->cur_rx % NUM_RX_DESC;
        int rx_status=tp->rx_buf[cur_rx].cmdsts;

        if (sis900_debug > 4)
                printk(KERN_INFO "%s: sis900_rx, current %4.4x,"
                                " rx status=%8.8x\n",
                                dev->name, cur_rx,
                                rx_status);

        while (rx_status & OWN) {
                int rx_size = rx_status & DSIZE;
                rx_size -= CRC_SIZE;

                if (sis900_debug > 4) {
                        int i;
                        printk(KERN_INFO "%s:  sis900_rx, rx status %8.8x,"
                                        " size %4.4x, cur %4.4x.\n",
                                   dev->name, rx_status, rx_size, cur_rx);
                        printk(KERN_INFO "%s: Rx Frame contents:", dev->name);

                        for (i = 0; i < rx_size; i++) {
                                printk("%2.2x ",
                                (u8)(tp->rx_buf[cur_rx].buf[i]));
                        }

                        printk(".\n");
                }
                if (rx_status & TOOLONG) {
                        if (sis900_debug > 1)
                                printk(KERN_INFO "%s: Oversized Ethernet frame,"
                                                " status %4.4x!\n",
                                           dev->name, rx_status);
                        tp->stats.rx_length_errors++;
                } else if (rx_status & (RXISERR | RUNT | CRCERR | FAERR)) {
                        if (sis900_debug > 1)
                                printk(KERN_INFO"%s: Ethernet frame had errors,"
                                        " status %4.4x.\n",
                                        dev->name, rx_status);
                        tp->stats.rx_errors++;
                        if (rx_status & (RXISERR | FAERR))
                                tp->stats.rx_frame_errors++;
                        if (rx_status & (RUNT | TOOLONG))
                                tp->stats.rx_length_errors++;
                        if (rx_status & CRCERR) tp->stats.rx_crc_errors++;
                } else {
                        /* Malloc up new buffer, compatible with net-2e. */
                        /* Omit the four octet CRC from the length. */
                        struct sk_buff *skb;

                        skb = dev_alloc_skb(rx_size + 2);
                        if (skb == NULL) {
                                printk(KERN_INFO "%s: Memory squeeze,"
                                                "deferring packet.\n",
                                                dev->name);
                                /* We should check that some rx space is free.
                                   If not,
                                   free one and mark stats->rx_dropped++. */
                                tp->stats.rx_dropped++;
                                tp->rx_buf[cur_rx].cmdsts = RX_BUF_SIZE;
                                break;
                        }
                        skb->dev = dev;
                        skb_reserve(skb, 2); /* 16 byte align the IP fields. */
                        if (rx_size+CRC_SIZE > RX_BUF_SIZE) {
                                /*
                                int semi_count = RX_BUF_LEN - ring_offset - 4;
                                memcpy(skb_put(skb, semi_count),
                                        &rx_bufs[ring_offset + 4], semi_count);
                                memcpy(skb_put(skb, rx_size-semi_count),
                                        rx_bufs, rx_size - semi_count);
                                if (sis900_debug > 4) {
                                        int i;
                                        printk(KERN_DEBUG"%s:  Frame wrap @%d",
                                                   dev->name, semi_count);
                                        for (i = 0; i < 16; i++)
                                                printk(" %2.2x", rx_bufs[i]);
                                        printk(".\n");
                                        memset(rx_bufs, 0xcc, 16);
                                }
                                */
                        } else {
#if 0  /* USE_IP_COPYSUM */
                                eth_copy_and_sum(skb,
                                   tp->rx_buf[cur_rx].buf, rx_size, 0);
                                skb_put(skb, rx_size);
#else
                                memcpy(skb_put(skb, rx_size),
                                        tp->rx_buf[cur_rx].buf, rx_size);
#endif
                        }
                        skb->protocol = eth_type_trans(skb, dev);
                        netif_rx(skb);
#if LINUX_VERSION_CODE > 0x20119
                        tp->stats.rx_bytes += rx_size;
#endif
                        tp->stats.rx_packets++;
                }
                tp->rx_buf[cur_rx].cmdsts = RX_BUF_SIZE;

                cur_rx = ((cur_rx+1) % NUM_RX_DESC);
                rx_status = tp->rx_buf[cur_rx].cmdsts;
        } // while
        if (sis900_debug > 4)
                printk(KERN_INFO "%s: Done sis900_rx(), current %4.4x "
                                "Cmd %2.2x.\n",
                           dev->name, cur_rx,
                           inb(ioaddr + cr));
        tp->cur_rx = cur_rx;
	outl( RxENA , ioaddr + cr );  /* LCS */
        return 0;
}

static int
sis900_close(struct device *dev)
{
        long ioaddr = dev->base_addr;
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int i;

        dev->start = 0;
        dev->tbusy = 1;

        /* Disable interrupts by clearing the interrupt mask. */
        outl(0x0000, ioaddr + imr);

        /* Stop the chip's Tx and Rx DMA processes. */
        outl(0x00, ioaddr + cr);

        del_timer(&tp->timer);

        free_irq(dev->irq, dev);

        for (i = 0; i < NUM_TX_DESC; i++) {
                if (tp->tx_skbuff[i])
                        dev_kfree_skb(tp->tx_skbuff[i]);
                tp->tx_skbuff[i] = 0;
        }
        kfree(tp->rx_bufs);
        kfree(tp->tx_bufs);

        /* Green! Put the chip in low-power mode. */

        MOD_DEC_USE_COUNT;

        return 0;
}

static int mii_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        u16 *data = (u16 *)&rq->ifr_data;

        switch(cmd) {
        case SIOCDEVPRIVATE:            	/* Get the address of the PHY in use. */
                data[0] = tp->mii->phy_addr;
                /* Fall Through */
        case SIOCDEVPRIVATE+1:          	/* Read the specified MII register. */
                data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
                return 0;
        case SIOCDEVPRIVATE+2:          	/* Write the specified MII register */
                if (!suser())
                        return -EPERM;
                mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
                return 0;
        default:
                return -EOPNOTSUPP;
        }
}

static struct enet_statistics *
sis900_get_stats(struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;

        return &tp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   This routine is not state sensitive and need not be SMP locked. */

static u16 elComputeHashTableIndex(u8 *addr)
{
#define POLYNOMIAL 0x04C11DB6L
    u32      crc = 0xffffffff, msb;
    int      i, j;
    u8       byte;

    for( i=0; i<6; i++ ) {
        byte = *addr++;
        for( j=0; j<8; j++ ) {
            msb = crc >> 31;
            crc <<= 1;
            if( msb ^ ( byte & 1 )) {
                crc ^= POLYNOMIAL;
                crc |= 1;
            }
            byte >>= 1;
        }
    }
    // 7 bit crc for 128 bit hash table
    return( (int)(crc >> 25) );
}

static u16 elMIIpollBit(struct device *dev,
                         int phy_id,
                         int location,
                         u16 mask,
                         u16 polarity,
                         u16 *value)
{
        u32 i;
        i=0;
        while (1) {
                *value = mdio_read(dev, phy_id, location);
                if (polarity) {
                        if (mask & *value) return(TRUE);
                } else {
                        if (mask & ~(*value)) return(TRUE);
                }
                if (++i == 1200) break;
        }
        return(FALSE);
}

static u16 elPMDreadMode(struct device *dev,
                         int phy_id,
                         int *speed,
                         int *duplex)
{
        u16 status, OurCap;

        *speed = HW_SPEED_10_MBPS;
        *duplex = FDX_CAPABLE_HALF_SELECTED;

        status = mdio_read(dev, phy_id, MII_ANLPAR);
        OurCap = mdio_read(dev, phy_id, MII_ANAR);
	if (sis900_debug > 1) {
		printk(KERN_INFO "Link Part Status %4X\n", status);
		printk(KERN_INFO "Our Status %4X\n", OurCap);
		printk(KERN_INFO "Status Reg %4X\n",
					mdio_read(dev, phy_id, MII_STATUS));
	}
	status &= OurCap;

        if ( !( status &
                (MII_NWAY_T|MII_NWAY_T_FDX | MII_NWAY_TX | MII_NWAY_TX_FDX ))) {
		if (sis900_debug > 1) {
			printk(KERN_INFO "The other end NOT support NWAY...\n");
		}
                while (( status = mdio_read(dev, phy_id, 18)) & 0x4000) ;
                while (( status = mdio_read(dev, phy_id, 18)) & 0x0020) ;
                if (status & 0x80)
                        *speed = HW_SPEED_100_MBPS;
                if (status & 0x40)
                        *duplex = FDX_CAPABLE_FULL_SELECTED;
                if (sis900_debug > 3) {
                        printk(KERN_INFO"%s: Setting %s%s-duplex.\n",
                                dev->name,
                                *speed == HW_SPEED_100_MBPS ?
                                        "100mbps " : "10mbps ",
                                *duplex == FDX_CAPABLE_FULL_SELECTED ?
                                        "full" : "half");
                }
        } else {
		if (sis900_debug > 1) {
			printk(KERN_INFO "The other end support NWAY...\n");
		}

                if (status & (MII_NWAY_TX_FDX | MII_NWAY_T_FDX)) {
                        *duplex = FDX_CAPABLE_FULL_SELECTED;
                }
                if (status & (MII_NWAY_TX_FDX | MII_NWAY_TX)) {
                        *speed = HW_SPEED_100_MBPS;
                }
                if (sis900_debug > 3) {
                        printk(KERN_INFO"%s: Setting %s%s-duplex based on"
                                " auto-negotiated partner ability.\n",
                                dev->name,
                                *speed == HW_SPEED_100_MBPS ?
                                        "100mbps " : "10mbps ",
                                *duplex == FDX_CAPABLE_FULL_SELECTED ?
                                        "full" : "half");
                }
        }
        return (status);
}

static u16 elAutoNegotiate(struct device *dev, int phy_id, int *duplex, int *speed)
{
        u16 status, retnVal;

	if (sis900_debug > 1) {
		printk(KERN_INFO "AutoNegotiate...\n");
	}
        mdio_write(dev, phy_id, MII_CONTROL, 0);
        mdio_write(dev, phy_id, MII_CONTROL, MIICNTL_AUTO | MIICNTL_RST_AUTO);
        retnVal = elMIIpollBit(dev, phy_id, MII_CONTROL, MIICNTL_RST_AUTO,
				FALSE,&status);
	if (!retnVal) {
		printk(KERN_INFO "%s: Not wait for Reset Complete\n", dev->name);
	}
        retnVal = elMIIpollBit(dev, phy_id, MII_STATUS, MIISTAT_AUTO_DONE,
				TRUE, &status);
	if (!retnVal) {
		printk(KERN_INFO "%s: Not wait for AutoNego Complete\n", dev->name);
	}
        retnVal = elMIIpollBit(dev, phy_id, MII_STATUS, MIISTAT_LINK,
				TRUE, &status);
	if (!retnVal) {
		printk(KERN_INFO "%s: Not wait for Link Complete\n", dev->name);
	}
        if (status & MIISTAT_LINK) {
                elPMDreadMode(dev, phy_id, speed, duplex);
                elSetMediaType(dev, *speed, *duplex);
        }
        return(status);
}

static void elSetCapability(struct device *dev, int phy_id,
			    int duplex, int speed)
{
        u16 cap = ( MII_NWAY_T  | MII_NWAY_T_FDX  |
		    MII_NWAY_TX | MII_NWAY_TX_FDX | MII_NWAY_CSMA_CD );

	if (speed != 100) {
		cap &= ~( MII_NWAY_TX | MII_NWAY_TX_FDX );
		if (sis900_debug > 1) {
			printk(KERN_INFO "UNSET 100Mbps\n");
		}
	}

	if (!duplex) {
		cap &= ~( MII_NWAY_T_FDX | MII_NWAY_TX_FDX );
		if (sis900_debug > 1) {
			printk(KERN_INFO "UNSET full-duplex\n");
		}
	}

        mdio_write(dev, phy_id, MII_ANAR, cap);
}

static void elSetMediaType(struct device *dev, int speed, int duplex)
{
        long ioaddr = dev->base_addr;
        u32     txCfgOn = 0, txCfgOff = TxDRNT;
        u32     rxCfgOn = 0, rxCfgOff = 0;

        if (speed == HW_SPEED_100_MBPS) {
                txCfgOn |= (TxDRNT_100 | TxHBI);
        } else {
                txCfgOn |= TxDRNT_10;
        }

        if (duplex == FDX_CAPABLE_FULL_SELECTED) {
                txCfgOn |= (TxCSI | TxHBI);
                rxCfgOn |= RxATP;
        } else {
                txCfgOff |= (TxCSI | TxHBI);
                rxCfgOff |= RxATP;
        }
        outl( (inl(ioaddr + txcfg) & ~txCfgOff) | txCfgOn, ioaddr + txcfg);
        outl( (inl(ioaddr + rxcfg) & ~rxCfgOff) | rxCfgOn, ioaddr + rxcfg);
}

static void set_rx_mode(struct device *dev)
{
        long ioaddr = dev->base_addr;
        u16 mc_filter[8];
        int i;
        int rx_mode;
        u32 rxCfgOn = 0, rxCfgOff = 0;
        u32 txCfgOn = 0, txCfgOff = 0;

        if (sis900_debug > 3)
                printk(KERN_INFO "%s: set_rx_mode (%4.4x) done--"
                                "RxCfg %8.8x.\n",
                                dev->name, dev->flags, inl(ioaddr + rxcfg));

        /* Note: do not reorder, GCC is clever about common statements. */
        if (dev->flags & IFF_PROMISC) {
                printk(KERN_NOTICE"%s: Promiscuous mode enabled.\n", dev->name);
                rx_mode = ACCEPT_ALL_BCASTS | ACCEPT_ALL_MCASTS |
                                ACCEPT_CAM_QUALIFIED | ACCEPT_ALL_PHYS;
                for (i=0 ; i<8 ; i++)
                        mc_filter[i]=0xffff;
        } else if ((dev->mc_count > multicast_filter_limit)
				   ||  (dev->flags & IFF_ALLMULTI)) {
                rx_mode = ACCEPT_ALL_BCASTS | ACCEPT_ALL_MCASTS | ACCEPT_CAM_QUALIFIED;
                for (i=0 ; i<8 ; i++)
                        mc_filter[i]=0xffff;
        } else {
                struct dev_mc_list *mclist;
                rx_mode = ACCEPT_ALL_BCASTS | ACCEPT_ALL_MCASTS |
                                ACCEPT_CAM_QUALIFIED;
                for (i=0 ; i<8 ; i++)
                        mc_filter[i]=0;
                for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
                         i++, mclist = mclist->next)
                        set_bit(elComputeHashTableIndex(mclist->dmi_addr),
                                                mc_filter);
        }

        for (i=0 ; i<8 ; i++) {
                outl((u32)(0x00000004+i) << 16, ioaddr + rfcr);
                outl(mc_filter[i], ioaddr + rfdr);
        }
        /* We can safely update without stopping the chip. */
        //rx_mode = ACCEPT_CAM_QUALIFIED | ACCEPT_ALL_BCASTS | ACCEPT_ALL_PHYS;
        //rx_mode = ACCEPT_CAM_QUALIFIED | ACCEPT_ALL_BCASTS;
        outl(RFEN | ((rx_mode & (ACCEPT_ALL_MCASTS | ACCEPT_ALL_BCASTS |
                          ACCEPT_ALL_PHYS)) << RFAA_shift), ioaddr + rfcr);

        if (rx_mode & ACCEPT_ALL_ERRORS) {
                rxCfgOn = RxAEP | RxARP | RxAJAB;
        } else {
                rxCfgOff = RxAEP | RxARP | RxAJAB;
        }
        if (rx_mode & MAC_LOOPBACK) {
                rxCfgOn |= RxATP;
                txCfgOn |= TxMLB;
        } else {
                if (!(( (struct sis900_private *)(dev->priv) )->full_duplex))
                        rxCfgOff |= RxATP;
                txCfgOff |= TxMLB;
        }

        if (sis900_debug > 2) {
                printk(KERN_INFO "Before Set TxCfg=%8.8x\n",inl(ioaddr+txcfg));
                printk(KERN_INFO "Before Set RxCfg=%8.8x\n",inl(ioaddr+rxcfg));
        }

        outl((inl(ioaddr + rxcfg) | rxCfgOn) & ~rxCfgOff, ioaddr + rxcfg);
        outl((inl(ioaddr + txcfg) | txCfgOn) & ~txCfgOff, ioaddr + txcfg);

        if (sis900_debug > 2) {
                printk(KERN_INFO "After Set TxCfg=%8.8x\n",inl(ioaddr+txcfg));
                printk(KERN_INFO "After Set RxCfg=%8.8x\n",inl(ioaddr+rxcfg));
                printk(KERN_INFO "Receive Filter Register:%8.8x\n",
                                                        inl(ioaddr + rfcr));
        }
        return;
}

static void sis900_reset(struct device *dev)
{
        long ioaddr = dev->base_addr;
	
        outl(0, ioaddr + ier);
        outl(0, ioaddr + imr);
        outl(0, ioaddr + rfcr);

        outl(RxRESET | TxRESET | RESET, ioaddr + cr);
        outl(PESEL, ioaddr + cfg);
	
        set_rx_mode(dev);
}

#ifdef MODULE
int init_module(void)
{
        return sis900_probe(NULL);
}

void
cleanup_module(void)
{
	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_sis900_dev) {
		struct sis900_private *tp =
			(struct sis900_private *)root_sis900_dev->priv;
		struct device *next_dev = tp->next_module;
		
		unregister_netdev(root_sis900_dev);
		release_region(root_sis900_dev->base_addr,
			       tp->mac->io_size);
		kfree(tp);
		kfree(root_sis900_dev);

		root_sis900_dev = next_dev;
        }
}

#endif  /* MODULE */
