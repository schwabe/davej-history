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
   
   Rev 1.05.05 Oct. 29 1999 Ollie Lho (ollie@sis.com.tw) Single buffer Tx/Rx  
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
"sis900.c:v1.05.05  10/29/99\n";

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

typedef struct _BufferDesc {
        u32     link;
        u32     cmdsts;
        u32     bufptr;
} BufferDesc;

struct sis900_private {
        struct device *next_module;
        struct net_device_stats stats;
	struct pci_dev * pci_dev;

	struct mac_chip_info * mac;
	struct mii_phy * mii;

        struct timer_list timer;        		/* Link status detection timer. */
        unsigned int cur_rx, dirty_rx;		
        unsigned int cur_tx, dirty_tx;

        /* The saved address of a sent/receive-in-place packet/buffer */
        struct sk_buff *tx_skbuff[NUM_TX_DESC];
	struct sk_buff *rx_skbuff[NUM_RX_DESC];
	BufferDesc tx_ring[NUM_TX_DESC];	
        BufferDesc rx_ring[NUM_RX_DESC];
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
static void sis900_init_tx_ring(struct device *dev);
static void sis900_init_rx_ring(struct device *dev);
static int sis900_start_xmit(struct sk_buff *skb, struct device *dev);
static int sis900_rx(struct device *dev);
static void sis900_finish_xmit (struct device *dev);
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

	/* check to see if we have sane EEPROM */
	signature = (u16) read_eeprom(ioaddr, EEPROMSignature);    
	if (signature == 0xffff || signature == 0x0000) {
		printk (KERN_INFO "%s: Error EERPOM read %x\n", 
			net_dev->name, signature);
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

	if ((net_dev->priv = kmalloc(sizeof(struct sis900_private), GFP_KERNEL)) == NULL) {
		unregister_netdevice(net_dev);
		return NULL;
	}

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
		unregister_netdev(net_dev);
		kfree(sis_priv);
		release_region(ioaddr, mac->io_size);
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
	
	/* search for total of 32 possible mii phy addresses */
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
				/* the current mii is on our mii_info_table, 
				   quit searching (table) */
				break;
			}
	}
	
	if (sis_priv->mii == NULL) {
		printk(KERN_INFO "%s: No MII transceivers found!\n", 
		       net_dev->name);
		return 0;
	}

	/* FIXME: AMD stuff should be added */
	/* auto negotiate FIXME: not completed */
	elSetCapability(net_dev, sis_priv->mii->phy_addr, 1, 100);
	sis_priv->mii->status = elAutoNegotiate(net_dev, 
						sis_priv->mii->phy_addr,
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

        /* Soft reset the chip. */
	sis900_reset(dev);

        if (request_irq(dev->irq, &sis900_interrupt, SA_SHIRQ, dev->name, dev)) {
                return -EAGAIN;
        }

        MOD_INC_USE_COUNT;

	/* FIXME: should this be move to set_rx_mode() ? */
	sis900_init_rxfilter(dev);

	sis900_init_tx_ring(dev);
	sis900_init_rx_ring(dev);

	/* FIXME: should be removed, and replaced by AutoNeogotiate stuff */
        outl((RX_DMA_BURST << RxMXDMA_shift) | (RxDRNT_10 << RxDRNT_shift), 
	     ioaddr + rxcfg);
        outl(TxATP | (TX_DMA_BURST << TxMXDMA_shift) | (TX_FILL_THRESH << TxFILLT_shift) | TxDRNT_10,
	     ioaddr + txcfg);

        set_rx_mode(dev);

        dev->tbusy = 0;
        dev->interrupt = 0;
        dev->start = 1;

        /* Enable all known interrupts by setting the interrupt mask. */
	outl((RxSOVR|RxORN|RxERR|RxOK|TxURN|TxERR|TxOK), ioaddr + imr);
        outl(RxENA, ioaddr + cr);
        outl(IE, ioaddr + ier);

        /* Set the timer to switch to check for link beat and perhaps switch
           to an alternate media type. */
        init_timer(&sis_priv->timer);
        sis_priv->timer.expires = jiffies + HZ;
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

	/* load MAC addr to filter data register */
	for (i = 0 ; i < 3 ; i++) {
		u32 w;

		w = (u32) *((u16 *)(net_dev->dev_addr)+i);
		outl((i << RFADDR_shift), ioaddr + rfcr);
		outl(w, ioaddr + rfdr);

		if (sis900_debug > 2) {
			printk(KERN_INFO "%s: Receive Filter Addrss[%d]=%x\n",
			       net_dev->name, i, inl(ioaddr + rfdr));
		}
	}

	/* enable packet filitering */
	outl(rfcrSave | RFEN, rfcr + ioaddr);
}

/* Initialize the Tx ring. */
static void
sis900_init_tx_ring(struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr; 
        int i;
	
        tp->tx_full = 0;
        tp->dirty_tx = tp->cur_tx = 0;
	
        for (i = 0; i < NUM_TX_DESC; i++) {
		tp->tx_skbuff[i] = NULL;

		tp->tx_ring[i].link = (u32) virt_to_bus(&tp->tx_ring[i+1]);
                tp->tx_ring[i].cmdsts = 0;
		tp->tx_ring[i].bufptr = 0;
        }
	tp->tx_ring[i-1].link = (u32) virt_to_bus(&tp->tx_ring[0]);

	/* load Transmit Descriptor Register */
        outl(virt_to_bus(&tp->tx_ring[0]), ioaddr + txdp); 
	if (sis900_debug > 2)
                printk(KERN_INFO "%s: TX descriptor register loaded with: %8.8x\n", 
		       dev->name, inl(ioaddr + txdp));
}

/* Initialize the Rx descriptor ring, pre-allocate recevie buffers */ 
static void 
sis900_init_rx_ring(struct device *dev) 
{ 
        struct sis900_private *tp = (struct sis900_private *)dev->priv; 
        long ioaddr = dev->base_addr; 
        int i;
 
        tp->cur_rx = 0; 
	tp->dirty_rx = 0;

	/* init RX descriptor */
	for (i = 0; i < NUM_RX_DESC; i++) {
		tp->rx_skbuff[i] = NULL;

		tp->rx_ring[i].link = (u32) virt_to_bus(&tp->rx_ring[i+1]);
		tp->rx_ring[i].cmdsts = 0;
		tp->rx_ring[i].bufptr = 0;
	}
	tp->rx_ring[i-1].link = (u32) virt_to_bus(&tp->rx_ring[0]);

        /* allocate sock buffers */
	for (i = 0; i < NUM_RX_DESC; i++) {
		struct sk_buff *skb;

		if ((skb = dev_alloc_skb(RX_BUF_SIZE)) == NULL) {
			/* not enough memory for skbuff, this makes a "hole"
			   on the buffer ring, it is not clear how the 
			   hardware will react to this kind of degenerated 
			   buffer */
			break;
		}
		skb->dev = dev;
		tp->rx_skbuff[i] = skb;
		tp->rx_ring[i].cmdsts = RX_BUF_SIZE;
		tp->rx_ring[i].bufptr = virt_to_bus(skb->tail);
	}
	tp->dirty_rx = (unsigned int) (i - NUM_RX_DESC);

	/* load Receive Descriptor Register */
        outl(virt_to_bus(&tp->rx_ring[0]), ioaddr + rxdp);
	if (sis900_debug > 2)
                printk(KERN_INFO "%s: RX descriptor register loaded with: %8.8x\n", 
		       dev->name, inl(ioaddr + rxdp));
}

static void sis900_timer(unsigned long data)
{
        struct device *dev = (struct device *)data;
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int next_tick = 2*HZ;
        u16 status;

	/* FIXME: Should we check transmission time out here ? */
	/* FIXME: call auto negotiate routine to detect link status */
        if (!tp->LinkOn) {
                status = mdio_read(dev, tp->mii->phy_addr, MII_STATUS);
		if (status & MIISTAT_LINK) {
                	elPMDreadMode(dev, tp->mii->phy_addr,
				      &tp->speeds, &tp->full_duplex);
			tp->LinkOn = TRUE;
                        printk(KERN_INFO "%s: Media Link On %s%s-duplex \n", 
			       dev->name,
			       tp->speeds == HW_SPEED_100_MBPS ? 
			       "100mbps " : "10mbps ",
			       tp->full_duplex == FDX_CAPABLE_FULL_SELECTED ?
			       "full" : "half");
		}
        } else { // previous link on
                status = mdio_read(dev, tp->mii->phy_addr, MII_STATUS);
		if (!(status & MIISTAT_LINK)) {
			tp->LinkOn = FALSE;
                        printk(KERN_INFO "%s: Media Link Off\n", dev->name);
		}
        }

	tp->timer.expires = jiffies + next_tick;
	add_timer(&tp->timer);
}

static void sis900_tx_timeout(struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;

        if (sis900_debug > 2)
                printk(KERN_INFO "%s: Transmit timeout, status %2.2x %4.4x \n",
		       dev->name, inl(ioaddr + cr), inl(ioaddr + isr));
	
        /* Disable interrupts by clearing the interrupt mask. */
        outl(0x0000, ioaddr + imr);

        tp->cur_rx = 0;
        dev->trans_start = jiffies;
        tp->stats.tx_errors++;

	/* FIXME: Should we restart the transmission thread here  ?? */

        /* Enable all known interrupts by setting the interrupt mask. */
        outl((RxSOVR|RxORN|RxERR|RxOK|TxURN|TxERR|TxOK), ioaddr + imr);
        return;
}

static int
sis900_start_xmit(struct sk_buff *skb, struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
        unsigned int  entry;

	/* test tbusy to see if we have timeout situation then set it */
        if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
                if (jiffies - dev->trans_start > TX_TIMEOUT)
			sis900_tx_timeout(dev);
                return 1;
        }

        /* Calculate the next Tx descriptor entry. */
        entry = tp->cur_tx % NUM_TX_DESC;
        tp->tx_skbuff[entry] = skb;

	/* set the transmit buffer descriptor and enable Transmit State Machine */
	tp->tx_ring[entry].bufptr = virt_to_bus(skb->data);
        tp->tx_ring[entry].cmdsts = (OWN | skb->len);
        outl(TxENA, ioaddr + cr);

        if (++tp->cur_tx - tp->dirty_tx < NUM_TX_DESC) {
		/* Typical path, clear tbusy to indicate more 
		   transmission is possible */
                clear_bit(0, (void*)&dev->tbusy);
        } else {
		/* no more transmit descriptor avaiable, tbusy remain set */
                tp->tx_full = 1;
        }

        dev->trans_start = jiffies;

        if (sis900_debug > 3)
                printk(KERN_INFO "%s: Queued Tx packet at %p size %d "
		       "to slot %d.\n",
		       dev->name, skb->data, (int)skb->len, entry);

        return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void sis900_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
        struct device *dev = (struct device *)dev_instance;
        int boguscnt = max_interrupt_work;
	long ioaddr = dev->base_addr;
	u32 status;

#if defined(__i386__)
        /* A lock to prevent simultaneous entry bug on Intel SMP machines. */
        if (test_and_set_bit(0, (void*)&dev->interrupt)) {
                printk(KERN_INFO "%s: SMP simultaneous entry of "
		       "an interrupt handler.\n", dev->name);
                dev->interrupt = 0;     /* Avoid halting machine. */
                return;
        }
#else
        if (dev->interrupt) {
                printk(KERN_INFO "%s: Re-entering the interrupt handler.\n", 
		       dev->name);
                return;
        }
        dev->interrupt = 1;
#endif

        do {
                status = inl(ioaddr + isr);
		
                if (sis900_debug > 3)
			printk(KERN_INFO "%s: entering interrupt, "
			       "original status = %#8.8x, "
			       "new status = %#8.8x.\n",
			       dev->name, status, inl(ioaddr + isr));

                if ((status & (HIBERR|TxURN|TxERR|TxOK|RxORN|RxERR|RxOK)) == 0)
			/* nothing intresting happened */
                        break;

		/* why dow't we break after Tx/Rx case ?? keyword: full-duplex */
                if (status & (RxORN | RxERR | RxOK))
			/* Rx interrupt */
                        sis900_rx(dev);

                if (status & (TxURN | TxERR | TxOK))
			/* Tx interrupt */
			sis900_finish_xmit(dev);

                /* something strange happened !!! */
                if (status & HIBERR) {
			printk(KERN_INFO "%s: Abnormal interrupt,"
			       "status %#8.8x.\n", dev->name, status);
			break;
		}
                if (--boguscnt < 0) {
                        printk(KERN_INFO "%s: Too much work at interrupt, "
			       "interrupt Status = %#8.8x.\n",
			       dev->name, status);
                        break;
                }
        } while (1);
	
        if (sis900_debug > 3)
                printk(KERN_INFO "%s: exiting interrupt, "
		       "interrupt status = 0x%#8.8x.\n",
		       dev->name, inl(ioaddr + isr));
	
#if defined(__i386__)
        clear_bit(0, (void*)&dev->interrupt);
#else
        dev->interrupt = 0;
#endif
        return;
}

/* Process receive interrupt events, put buffer to higher layer and refill buffer pool 
   Note: This fucntion is called by interrupt handler, don't do "too much" work here */
static int sis900_rx(struct device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
	unsigned int entry = tp->cur_rx % NUM_RX_DESC;
        u32 rx_status = tp->rx_ring[entry].cmdsts;

        if (sis900_debug > 3)
                printk(KERN_INFO "sis900_rx, cur_rx:%4.4d, dirty_rx:%4.4d "
                	"status:0x%8.8x\n",
		        tp->cur_rx, tp->dirty_rx,rx_status);
	
        while (rx_status & OWN) {
                unsigned int rx_size;
		
		rx_size = (rx_status & DSIZE) - CRC_SIZE;

		if (rx_status & (ABORT|OVERRUN|TOOLONG|RUNT|RXISERR|CRCERR|FAERR)) {
			/* corrupted packet received */
                        if (sis900_debug > 3)
                                printk(KERN_INFO "%s: Corrupted packet "
				       "received, buffer status = 0x%8.8x.\n",
				       dev->name, rx_status);
                        tp->stats.rx_errors++;
			if (rx_status & OVERRUN)
				tp->stats.rx_over_errors++;
			if (rx_status & (TOOLONG|RUNT))
                                tp->stats.rx_length_errors++;
                        if (rx_status & (RXISERR | FAERR))
                                tp->stats.rx_frame_errors++;
			if (rx_status & CRCERR) 
				tp->stats.rx_crc_errors++;
			/* reset buffer descriptor state */
			tp->rx_ring[entry].cmdsts = RX_BUF_SIZE;
                } else {
			struct sk_buff * skb;
			
			if (tp->rx_skbuff[entry] == NULL) {
				printk(KERN_INFO "%s: NULL pointer " 
				       "encountered in Rx ring, skipping\n",
				       dev->name);
				break;			
			}
			skb = tp->rx_skbuff[entry];
			tp->rx_skbuff[entry] = NULL;
			/* reset buffer descriptor state */
			tp->rx_ring[entry].cmdsts = 0;
			tp->rx_ring[entry].bufptr = 0;

			skb_put(skb, rx_size);
                        skb->protocol = eth_type_trans(skb, dev);
                        netif_rx(skb);

			if (rx_status & MCAST)
				tp->stats.multicast++;
                        tp->stats.rx_bytes += rx_size;
                        tp->stats.rx_packets++;
                }
                tp->cur_rx++;
		entry = tp->cur_rx % NUM_RX_DESC;
                rx_status = tp->rx_ring[entry].cmdsts;
        } // while

	/* refill the Rx buffer, what if the rate of refilling is slower than 
	   consuming ?? */
	for (;tp->cur_rx - tp->dirty_rx > 0; tp->dirty_rx++) {
		struct sk_buff *skb;

		entry = tp->dirty_rx % NUM_RX_DESC;

		if (tp->rx_skbuff[entry] == NULL) {
			if ((skb = dev_alloc_skb(RX_BUF_SIZE)) == NULL) {
				/* not enough memory for skbuff, this makes a "hole"
				   on the buffer ring, it is not clear how the 
				   hardware will react to this kind of degenerated 
				   buffer */
				printk(KERN_INFO "%s: Memory squeeze,"
				       "deferring packet.\n",
				       dev->name);
				tp->stats.rx_dropped++;
				break;
			}
			skb->dev = dev;
			tp->rx_skbuff[entry] = skb;
			tp->rx_ring[entry].cmdsts = RX_BUF_SIZE;
			tp->rx_ring[entry].bufptr = virt_to_bus(skb->tail);
		}
	}
	/* re-enable the potentially idle receive state matchine */
	outl(RxENA , ioaddr + cr );

        return 0;
}

/* finish up transmission of packets, check for error condition and free skbuff etc.
   Note: This fucntion is called by interrupt handler, don't do "too much" work here */
static void sis900_finish_xmit (struct device *dev)
{
	struct sis900_private *tp = (struct sis900_private *)dev->priv;

	for (; tp->dirty_tx < tp->cur_tx; tp->dirty_tx++) {
		unsigned int entry;
		u32 tx_status;

		entry = tp->dirty_tx % NUM_TX_DESC;
	        tx_status = tp->tx_ring[entry].cmdsts;

		if (tx_status & OWN) {
			/* The packet is not transmited yet (owned by hardware) ! */
			break;
		}

		if (tx_status & (ABORT | UNDERRUN | OWCOLL)) {
			/* packet unsuccessfully transmited */
			if (sis900_debug > 3)
				printk(KERN_INFO "%s: Transmit "
				       "error, Tx status %8.8x.\n",
				       dev->name, tx_status);
			tp->stats.tx_errors++;
			if (tx_status & UNDERRUN)
				tp->stats.tx_fifo_errors++;
			if (tx_status & ABORT)
				tp->stats.tx_aborted_errors++;
			if (tx_status & NOCARRIER)
				tp->stats.tx_carrier_errors++;
			if (tx_status & OWCOLL)
				tp->stats.tx_window_errors++;
		} else {
			/* packet successfully transmited */
			if (sis900_debug > 3)
				printk(KERN_INFO "Tx Transmit OK\n");
			tp->stats.collisions += (tx_status & COLCNT) >> 16;
			tp->stats.tx_bytes += tx_status & DSIZE;
			tp->stats.tx_packets++;
		}
		/* Free the original skb. */
		dev_kfree_skb(tp->tx_skbuff[entry]);
		tp->tx_skbuff[entry] = NULL;
		tp->tx_ring[entry].bufptr = 0;
		tp->tx_ring[entry].cmdsts = 0;
	}

	if (tp->tx_full && dev->tbusy && 
	    tp->cur_tx - tp->dirty_tx < NUM_TX_DESC - 4) {
		/* The ring is no longer full, clear tbusy, tx_full and schedule 
		   more transmission by marking NET_BH */
		tp->tx_full = 0;
		clear_bit(0, (void *)&dev->tbusy);
		mark_bh(NET_BH);
	}
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
	outl(0x0000, ioaddr + ier);

        /* Stop the chip's Tx and Rx Status Machine */
        outl(RxDIS | TxDIS, ioaddr + cr);

        del_timer(&tp->timer);

        free_irq(dev->irq, dev);

	/* Free Tx and RX skbuff */
	for (i = 0; i < NUM_RX_DESC; i++) {
		if (tp->rx_skbuff[i] != NULL)
			dev_kfree_skb(tp->rx_skbuff[i]);
		tp->rx_skbuff[i] = 0;
	}
        for (i = 0; i < NUM_TX_DESC; i++) {
                if (tp->tx_skbuff[i] != NULL)
                        dev_kfree_skb(tp->tx_skbuff[i]);
                tp->tx_skbuff[i] = 0;
        }

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
        OurCap = mdio_read(dev, phy_id, MII_ANADV);
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
                while (( status = mdio_read(dev, phy_id, MII_STSOUT)) & 0x4000) ;
                while (( status = mdio_read(dev, phy_id, MII_STSOUT)) & 0x0020) ;
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

        mdio_write(dev, phy_id, MII_ANADV, cap);
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
	int i = 0;
	u32 status = TxRCMP | RxRCMP;

        outl(0, ioaddr + ier);
        outl(0, ioaddr + imr);
        outl(0, ioaddr + rfcr);

        outl(RxRESET | TxRESET | RESET, ioaddr + cr);
	
	/* Check that the chip has finished the reset. */
	while (status && (i++ < 1000)) {
		status ^= (inl(isr + ioaddr) & status);
        }

        outl(PESEL, ioaddr + cfg);
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
