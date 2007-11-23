 /*
  * Copyright (c) 1997-2000 LAN Media Corporation (LMC)
  * All rights reserved.  www.lanmedia.com
  *
  * This code is written by:
  * Andrew Stanley-Jones (asj@cban.com)
  * Rob Braun (bbraun@vix.com),
  * Michael Graff (explorer@vix.com) and
  * Matt Thomas (matt@3am-software.com).
  *
  * With Help By:
  * David Boggs
  * Ron Crane
  * Allan Cox
  *
  * This software may be used and distributed according to the terms
  * of the GNU Public License version 2, incorporated herein by reference.
  *
  * Driver for the LanMedia LMC5200, LMC5245, LMC1000, LMC1200 cards.
  *
  * To control link specific options lmcctl is required.
  * It can be obtained from ftp.lanmedia.com.
  *
  * Linux driver notes:
  * Linux uses the device struct lmc_private to pass private information
  * arround.
  *
  * The initialization portion of this driver (the lmc_reset() and the
  * lmc_dec_reset() functions, as well as the led controls and the
  * lmc_initcsrs() functions.
  *
  * The watchdog function runs every second and checks to see if
  * we still have link, and that the timing source is what we expected
  * it to be.  If link is lost, the interface is marked down, and
  * we no longer can transmit.
  *
  */

/* $Id: lmc_main.c,v 1.24 2000/01/21 13:29:48 asj Exp $ */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <asm/segment.h>
#include <asm/smp.h>

#if LINUX_VERSION_CODE < 0x20155
#include <linux/bios32.h>
#endif

#include <linux/in.h>
#include <linux/if_arp.h>
#include <asm/processor.h>             /* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "../syncppp.h"
#include <linux/inet.h>

#if LINUX_VERSION_CODE >= 0x20200
#include <asm/uaccess.h>
#include <asm/spinlock.h>
#endif

#ifdef MODULE
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#define DRIVER_MAJOR_VERSION     1
#define DRIVER_MINOR_VERSION    33
#define DRIVER_SUB_VERSION       1

#define DRIVER_VERSION  ((DRIVER_MAJOR_VERSION << 8) + DRIVER_MINOR_VERSION)

#include "lmc.h"
#include "lmc_ver.h"
#include "lmc_var.h"
#include "lmc_ioctl.h"
#include "lmc_debug.h"

static int Lmc_Count = 0;
static struct device *Lmc_root_dev = NULL;
static u8 cards_found = 0;

static int lmc_first_load = 0;

int lmc_probe_fake(struct device *dev);
static struct device *lmc_probe1(struct device *dev, int ioaddr, int irq,
				 int chip_id, int subdevice, int board_idx);
static int lmc_start_xmit(struct sk_buff *skb, struct device *dev);
static int lmc_rx(struct device *dev);
static int lmc_open(struct device *dev);
static int lmc_close(struct device *dev);
static struct enet_statistics *lmc_get_stats(struct device *dev);
static void lmc_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int lmc_ioctl(struct device *dev, struct ifreq *ifr, int cmd);
static int lmc_set_config(struct device *dev, struct ifmap *map);
static void lmc_initcsrs(lmc_softc_t * const, u32, size_t);
static void lmc_softreset(lmc_softc_t * const);
static void lmc_running_reset(struct device *dev);
static int lmc_ifdown(struct device * const);
static void lmc_watchdog(unsigned long data);
static int lmc_init(struct device * const);
static void lmc_reset(lmc_softc_t * const sc);
static void lmc_dec_reset(lmc_softc_t * const sc);
static void lmc_initcsrs(lmc_softc_t * const sc, lmc_csrptr_t csr_base,size_t csr_size);

/*
 * linux reserves 16 device specific IOCTLs.  We call them
 * LMCIOC* to control various bits of our world.
 */
static int lmc_ioctl (struct device *dev, struct ifreq *ifr, int cmd) /*fold00*/
{
    lmc_softc_t *sc;
    lmc_ctl_t ctl;
    int ret;
    u_int16_t regVal;
    unsigned long flags;

    struct sppp *sp;

    ret = -EOPNOTSUPP;

    sc = dev->priv;

    /*
     * Most functions mess with the structure
     * Disable interupts while we do the polling
     */
    spin_lock_irqsave(&sc->lmc_lock, flags);

    switch (cmd) {
        /*
         * Return current driver state.  Since we keep this up
         * To date internally, just copy this out to the user.
         */
    case LMCIOCGINFO:
        LMC_COPY_TO_USER(ifr->ifr_data, &sc->ictl, sizeof (lmc_ctl_t));
        ret = 0;
        break;

    case LMCIOCSINFO:
        sp = &((struct ppp_device *) dev)->sppp;
        if (!suser ()) {
            ret = -EPERM;
            break;
        }

        if(dev->flags & IFF_UP){
            ret = -EBUSY;
            break;
        }

        LMC_COPY_FROM_USER(&ctl, ifr->ifr_data, sizeof (lmc_ctl_t));

        sc->lmc_media->set_status (sc, &ctl);

        if(ctl.crc_length != sc->ictl.crc_length) {
            sc->lmc_media->set_crc_length(sc, ctl.crc_length);
        }

        if (ctl.keepalive_onoff == LMC_CTL_OFF)
            sp->pp_flags &= ~PP_KEEPALIVE;	/* Turn off */
        else
            sp->pp_flags |= PP_KEEPALIVE;	/* Turn on */

        ret = 0;
        break;
    case LMCIOCGETXINFO:
        sc->lmc_xinfo.Magic0 = 0xBEEFCAFE;

        sc->lmc_xinfo.PciCardType = sc->lmc_cardtype;
        sc->lmc_xinfo.PciSlotNumber = 0;
        sc->lmc_xinfo.DriverMajorVersion = DRIVER_MAJOR_VERSION;
        sc->lmc_xinfo.DriverMinorVersion = DRIVER_MINOR_VERSION;
        sc->lmc_xinfo.DriverSubVersion = DRIVER_SUB_VERSION;
        sc->lmc_xinfo.XilinxRevisionNumber =
            lmc_mii_readreg (sc, 0, 3) & 0xf;
        sc->lmc_xinfo.MaxFrameSize = PKT_BUF_SZ;
        sc->lmc_xinfo.link_status = sc->lmc_media->get_link_status (sc);
        sc->lmc_xinfo.mii_reg16 = lmc_mii_readreg (sc, 0, 16);

        sc->lmc_xinfo.Magic1 = 0xDEADBEEF;

        LMC_COPY_TO_USER(ifr->ifr_data, &sc->lmc_xinfo,
                         sizeof (struct lmc_xinfo));
        ret = 0;

        break;

    case LMCIOCGETLMCSTATS:
        if (sc->lmc_cardtype == LMC_CARDTYPE_T1){
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_FERR_LSB);
            sc->stats.framingBitErrorCount +=
                lmc_mii_readreg (sc, 0, 18) & 0xff;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_FERR_MSB);
            sc->stats.framingBitErrorCount +=
                (lmc_mii_readreg (sc, 0, 18) & 0xff) << 8;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_LCV_LSB);
            sc->stats.lineCodeViolationCount +=
                lmc_mii_readreg (sc, 0, 18) & 0xff;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_LCV_MSB);
            sc->stats.lineCodeViolationCount +=
                (lmc_mii_readreg (sc, 0, 18) & 0xff) << 8;
            lmc_mii_writereg (sc, 0, 17, T1FRAMER_AERR);
            regVal = lmc_mii_readreg (sc, 0, 18) & 0xff;

            sc->stats.lossOfFrameCount +=
                (regVal & T1FRAMER_LOF_MASK) >> 4;
            sc->stats.changeOfFrameAlignmentCount +=
                (regVal & T1FRAMER_COFA_MASK) >> 2;
            sc->stats.severelyErroredFrameCount +=
                regVal & T1FRAMER_SEF_MASK;
        }

        LMC_COPY_TO_USER(ifr->ifr_data, &sc->stats,
                         sizeof (struct lmc_statistics));

        ret = 0;
        break;

    case LMCIOCCLEARLMCSTATS:
        if (!suser ()){
            ret = -EPERM;
            break;
        }

        memset (&sc->stats, 0, sizeof (struct lmc_statistics));
        sc->stats.check = STATCHECK;
        sc->stats.version_size = (DRIVER_VERSION << 16) +
            sizeof (struct lmc_statistics);
        sc->stats.lmc_cardtype = sc->lmc_cardtype;
        ret = 0;
        break;

    case LMCIOCSETCIRCUIT:
        if (!suser ()){
            ret = -EPERM;
            break;
        }

        if(dev->flags & IFF_UP){
            ret = -EBUSY;
            break;
        }

        LMC_COPY_FROM_USER(&ctl, ifr->ifr_data, sizeof (lmc_ctl_t));
        sc->lmc_media->set_circuit_type(sc, ctl.circuit_type);
        sc->ictl.circuit_type = ctl.circuit_type;
        ret = 0;

        break;

    case LMCIOCRESET:
        if (!suser ()){
            ret = -EPERM;
            break;
        }

        /* Reset driver and bring back to current state */
        printk (" REG16 before reset +%04x\n", lmc_mii_readreg (sc, 0, 16));
        lmc_running_reset (dev);
        printk (" REG16 after reset +%04x\n", lmc_mii_readreg (sc, 0, 16));

        LMC_EVENT_LOG(LMC_EVENT_FORCEDRESET, LMC_CSR_READ (sc, csr_status), lmc_mii_readreg (sc, 0, 16));

        ret = 0;
        break;

#ifdef DEBUG
    case LMCIOCDUMPEVENTLOG:
        LMC_COPY_TO_USER(ifr->ifr_data, &lmcEventLogIndex, sizeof (u32));
        LMC_COPY_TO_USER(ifr->ifr_data + sizeof (u32), lmcEventLogBuf, sizeof (lmcEventLogBuf));

        ret = 0;
        break;
#endif /* end ifdef _DBG_EVENTLOG */
    case LMCIOCT1CONTROL:
        if (sc->lmc_cardtype != LMC_CARDTYPE_T1){
            ret = -EOPNOTSUPP;
            break;
        }
        break;
    default:
        /* If we don't know what to do, give syncppp a shot. */
        ret = sppp_do_ioctl (dev, ifr, cmd);
    }

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    return ret;
}


/* the watchdog process that cruises around */
static void lmc_watchdog (unsigned long data) /*FOLD00*/
{
    struct device *dev = (struct device *) data;
    lmc_softc_t *sc;
    int link_status;
    u_int32_t ticks;
    LMC_SPIN_FLAGS

    sc = dev->priv;

    spin_lock_irqsave(&sc->lmc_lock, flags);

    /* Make sure the tx jabber and rx watchdog are off,
     * and the transmit and recieve processes are running.
     */

    LMC_CSR_WRITE (sc, csr_15, 0x00000011);
    sc->lmc_cmdmode |= TULIP_CMD_TXRUN | TULIP_CMD_RXRUN;
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    if (sc->lmc_ok == 0)
        goto kick_timer;

    LMC_EVENT_LOG(LMC_EVENT_WATCHDOG, LMC_CSR_READ (sc, csr_status), lmc_mii_readreg (sc, 0, 16));

    /* --- begin time out check -----------------------------------
     * check for a transmit interrupt timeout
     * Has the packet xmt vs xmt serviced threshold been exceeded */
    if (sc->lmc_taint_tx == sc->lastlmc_taint_tx &&
        sc->stats.tx_packets > sc->lasttx_packets &&
        sc->tx_TimeoutInd == 0)
    {

        /* wait for the watchdog to come around again */
        sc->tx_TimeoutInd = 1;
    }
    else if (sc->lmc_taint_tx == sc->lastlmc_taint_tx &&
             sc->stats.tx_packets > sc->lasttx_packets &&
             sc->tx_TimeoutInd)
    {

        LMC_EVENT_LOG(LMC_EVENT_XMTINTTMO, LMC_CSR_READ (sc, csr_status), 0);

        sc->tx_TimeoutDisplay = 1;
        sc->stats.tx_TimeoutCnt++;

        /* DEC chip is stuck, hit it with a RESET!!!! */
        lmc_running_reset (dev);


        /* look at receive & transmit process state to make sure they are running */
        LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);

        /* look at: DSR - 02  for Reg 16
         *                  CTS - 08
         *                  DCD - 10
         *                  RI  - 20
         * for Reg 17
         */
        LMC_EVENT_LOG(LMC_EVENT_RESET2, lmc_mii_readreg (sc, 0, 16), lmc_mii_readreg (sc, 0, 17));

        /* reset the transmit timeout detection flag */
        sc->tx_TimeoutInd = 0;
        sc->lastlmc_taint_tx = sc->lmc_taint_tx;
        sc->lasttx_packets = sc->stats.tx_packets;
    }
    else
    {
        sc->tx_TimeoutInd = 0;
        sc->lastlmc_taint_tx = sc->lmc_taint_tx;
        sc->lasttx_packets = sc->stats.tx_packets;
    }

    /* --- end time out check ----------------------------------- */


    link_status = sc->lmc_media->get_link_status (sc);

    /*
     * hardware level link lost, but the interface is marked as up.
     * Mark it as down.
     */
    if ((link_status == 0) && (sc->last_link_status != 0)) {
        printk(KERN_WARNING "%s: link down\n", dev->name);
        sc->last_link_status = 0;
        /* lmc_reset (sc); Why reset??? The link can go down ok */

        /* Inform the world that link has been lost */
        dev->flags &= ~IFF_RUNNING;
    }

    /*
     * hardware link is up, but the interface is marked as down.
     * Bring it back up again.
     */
     if (link_status != 0 && sc->last_link_status == 0) {
         printk(KERN_WARNING "%s: link up\n", dev->name);
         sc->last_link_status = 1;
         /* lmc_reset (sc); Again why reset??? */

         /* Inform the world that link protocol is back up. */
         dev->flags |= IFF_RUNNING;

         /* Now we have to tell the syncppp that we had an outage
          * and that it should deal.  Calling sppp_reopen here
          * should do the trick, but we may have to call sppp_close
          * when the link goes down, and call sppp_open here.
          * Subject to more testing.
          * --bbraun
          */

         sppp_reopen (dev);

     }

    /* Call media specific watchdog functions */
    sc->lmc_media->watchdog(sc);

    /*
     * Poke the transmitter to make sure it
     * never stops, even if we run out of mem
     */
    LMC_CSR_WRITE(sc, csr_rxpoll, 0);


    /*
     * remember the timer value
     */
kick_timer:

    ticks = LMC_CSR_READ (sc, csr_gp_timer);
    LMC_CSR_WRITE (sc, csr_gp_timer, 0xffffffffUL);
    sc->ictl.ticks = 0x0000ffff - (ticks & 0x0000ffff);

    /*
     * restart this timer.
     */
    sc->timer.expires = jiffies + (HZ);
    add_timer (&sc->timer);

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

}

static int lmc_init(struct device * const dev) /*fold00*/
{
    return 0;
}

/* This initializes each card from lmc_probe() */
static struct device *lmc_probe1 (struct device *dev, int ioaddr, int irq, /*fold00*/
                                  int chip_id, int subdevice, int board_idx)
{
    lmc_softc_t *sc = NULL;
    u_int16_t AdapModelNum;

    /*
     * Allocate our own device structure
     */

    dev = kmalloc (sizeof (struct ppp_device)+8, GFP_KERNEL);
    if (dev == NULL){
        printk (KERN_ERR "lmc: kmalloc for device failed\n");
        return NULL;
    }
    memset (dev, 0, sizeof (struct ppp_device));

	/*
	 *	Switch to common hdlc%d naming. We name by type not by vendor
	 */
    dev->name = (char *)(dev+1);
    dev_alloc_name(dev, "hdlc%d");
    
    Lmc_Count++;

    if(lmc_first_load == 0){
        printk(KERN_INFO "Lan Media Corporation WAN Driver Version %d.%d.%d\n",DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,DRIVER_SUB_VERSION);
#ifndef MODULE
        sync_ppp_init();
#endif
        lmc_first_load = 1;
    }


    /* Initialize the sppp layer */
    sppp_attach ((struct ppp_device *) dev);

    /*
     * Allocate space for the private data structure
     */

    /* FIXME: we adjust to 8 byte align, but we then kfree the adjusted value
       BUG BUG BUG - fortunately kmalloc will be 8 byte aligned.. */
       
    sc = (void *) (((long) kmalloc (sizeof (lmc_softc_t), GFP_KERNEL) + 7) & ~7);
    if (sc == NULL) {
        printk (KERN_WARNING "%s: Cannot allocate memory for device state\n",
                dev->name);
        return (NULL);
    }
    memset (sc, 0, sizeof (lmc_softc_t));
    dev->priv = sc;
    sc->lmc_device = dev;
    sc->name = dev->name;

    /* Just fill in the entries for the device */

    dev->init = lmc_init;
    dev->hard_start_xmit = lmc_start_xmit;
    dev->open = lmc_open;
    dev->stop = lmc_close;
    dev->get_stats = lmc_get_stats;
    dev->do_ioctl = lmc_ioctl;
    dev->set_config = lmc_set_config;
    /*
     * Why were we changing this???
     dev->tx_queue_len = 100;
     */

    /* Init the spin lock so can call it latter */

    spin_lock_init(&sc->lmc_lock);

    LMC_SETUP_20_DEV;

    printk ("%s: detected at %#3x, irq %d\n", dev->name, ioaddr, irq);

    dev->base_addr = ioaddr;
    dev->irq = irq;

    if (register_netdev (dev) != 0) {
        printk (KERN_ERR "%s: register_netdev failed.\n", dev->name);
        sppp_detach (dev);
        kfree (dev->priv);
        kfree (dev);
        return NULL;
    }

    /*
     * Request the region of registers we need, so that
     * later on, no one else will take our card away from
     * us.
     */
    request_region (ioaddr, LMC_REG_RANGE, dev->name);

    sc->lmc_cardtype = LMC_CARDTYPE_UNKNOWN;
    sc->lmc_timing = LMC_CTL_CLOCK_SOURCE_EXT;

    switch (subdevice) {
    case PCI_PRODUCT_LMC_HSSI:
        printk ("%s: LMC HSSI\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_HSSI;
        sc->lmc_media = &lmc_hssi_media;
        break;
    case PCI_PRODUCT_LMC_DS3:
        printk ("%s: LMC DS3\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_DS3;
        sc->lmc_media = &lmc_ds3_media;
        break;
    case PCI_PRODUCT_LMC_SSI:
        printk ("%s: LMC SSI\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_SSI;
        sc->lmc_media = &lmc_ssi_media;
        break;
    case PCI_PRODUCT_LMC_T1:
        printk ("%s: LMC T1\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_T1;
        sc->lmc_media = &lmc_t1_media;
        break;
    default:
        printk (KERN_WARNING "%s: LMC UNKOWN CARD!\n", dev->name);
        break;
    }

    lmc_initcsrs (sc, dev->base_addr, 8);

    lmc_gpio_mkinput (sc, 0xff);
    sc->lmc_gpio = 0;		/* drive no signals yet */

    sc->lmc_media->defaults (sc);

    sc->lmc_media->set_link_status (sc, LMC_LINK_UP);

    /* verify that the PCI Sub System ID matches the Adapter Model number
     * from the MII register
     */
    AdapModelNum = (lmc_mii_readreg (sc, 0, 3) & 0x3f0) >> 4;

    if ((AdapModelNum == LMC_ADAP_T1
         && subdevice == PCI_PRODUCT_LMC_T1) ||		/* detect LMC1200 */
        (AdapModelNum == LMC_ADAP_SSI
         && subdevice == PCI_PRODUCT_LMC_SSI) ||	/* detect LMC1000 */
        (AdapModelNum == LMC_ADAP_DS3
         && subdevice == PCI_PRODUCT_LMC_DS3) ||	/* detect LMC5245 */
        (AdapModelNum == LMC_ADAP_HSSI
         && subdevice == PCI_PRODUCT_LMC_HSSI))
    {				/* detect LMC5200 */

    }
    else {
        printk ("%s: Model number (%d) miscompare for PCI Subsystem ID = 0x%04x\n",
                dev->name, AdapModelNum, subdevice);
        return (NULL);
    }
    /*
     * reset clock
     */
    LMC_CSR_WRITE (sc, csr_gp_timer, 0xFFFFFFFFUL);

    sc->board_idx = board_idx;

    memset (&sc->stats, 0, sizeof (struct lmc_statistics));

    sc->stats.check = STATCHECK;
    sc->stats.version_size = (DRIVER_VERSION << 16) +
        sizeof (struct lmc_statistics);
    sc->stats.lmc_cardtype = sc->lmc_cardtype;

    sc->lmc_ok = 0;
    sc->last_link_status = 0;

    return dev;
}


/* This is the entry point.  This is what is called immediatly. */
/* This goes out and finds the card */

int lmc_probe_fake(struct device *dev)
{
    lmc_probe(NULL);
    /* Return 1 to unloaded bogus device */
    return 1;
}

int lmc_probe (struct device *dev) /*fold00*/
{
    int pci_index = 0;
    unsigned char pci_irq_line;
    u16 vendor, subvendor, device, subdevice;
    u32 pci_ioaddr, foundaddr = 0;
    unsigned char pci_bus, pci_device_fn;
    u8 intcf = 0;

    /* The card is only available on PCI, so if we don't have a
     * PCI bus, we are in trouble.
     */

    if (!LMC_PCI_PRESENT()) {
/*        printk ("%s: We really want a pci bios!\n", dev->name);*/
        return -1;
    }
    /* Loop basically until we don't find anymore. */
    while (pci_index < 0xff){
        /* The tulip is considered an ethernet class of card... */
        if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8,
                                pci_index, &pci_bus,
                                &pci_device_fn) != PCIBIOS_SUCCESSFUL) {
            /* No card found on this pass */
            break;
        }
        /* Read the info we need to determine if this is
         * our card or not
         */
#if LINUX_VERSION_CODE >= 0x20155
        vendor = pci_find_slot (pci_bus, pci_device_fn)->vendor;
        device = pci_find_slot (pci_bus, pci_device_fn)->device;
        pci_irq_line = pci_find_slot (pci_bus, pci_device_fn)->irq;
        pci_ioaddr = pci_find_slot (pci_bus, pci_device_fn)->base_address[0];
        pci_read_config_word (pci_find_slot (pci_bus, pci_device_fn),
                              PCI_SUBSYSTEM_VENDOR_ID, &subvendor);
        pci_read_config_word (pci_find_slot (pci_bus, pci_device_fn),
                              PCI_SUBSYSTEM_ID, &subdevice);
#else
        pcibios_read_config_word (pci_bus, pci_device_fn,
                                  PCI_VENDOR_ID, &vendor);
        pcibios_read_config_word (pci_bus, pci_device_fn,
                                  PCI_DEVICE_ID, &device);
        pcibios_read_config_byte (pci_bus, pci_device_fn,
                                  PCI_INTERRUPT_LINE, &pci_irq_line);
        pcibios_read_config_dword(pci_bus, pci_device_fn,
                                  PCI_BASE_ADDRESS_0, &pci_ioaddr);
        pcibios_read_config_word (pci_bus, pci_device_fn,
                                  PCI_SUBSYSTEM_VENDOR_ID, &subvendor);
        pcibios_read_config_word (pci_bus, pci_device_fn,
                                  PCI_SUBSYSTEM_ID, &subdevice);
#endif

        /* Align the io address on the 32 bit boundry just in case */
        pci_ioaddr &= ~3;

        /*
         * Make sure it's the correct card.  CHECK SUBVENDOR ID!
         * There are lots of tulip's out there.
         * Also check the region of registers we will soon be
         * poking, to make sure no one else has reserved them.
         * This prevents taking someone else's device.
         *
         * Check either the subvendor or the subdevice, some systems reverse
         * the setting in the bois, seems to be version and arch dependant?
         * Fix the two variables
         *
         */
        if (!(check_region (pci_ioaddr, LMC_REG_RANGE)) &&
            (vendor == CORRECT_VENDOR_ID) &&
            (device == CORRECT_DEV_ID) &&
            ((subvendor == PCI_VENDOR_LMC)  || (subdevice == PCI_VENDOR_LMC))){
            struct device *cur, *prev = NULL;

            /* Fix the error, exchange the two values */
            if(subdevice == PCI_VENDOR_LMC){
                subdevice = subvendor;
                subvendor = PCI_VENDOR_LMC ;
            }

            /* Make the call to actually setup this card */
            dev = lmc_probe1 (dev, pci_ioaddr, pci_irq_line,
                              device, subdevice, cards_found);
            if (dev == NULL) {
                printk ("lmc_probe: lmc_probe1 failed\n");
                goto lmc_probe_next_card;
            }
            /* insert the device into the chain of lmc devices */
            for (cur = Lmc_root_dev;
                 cur != NULL;
                 cur = ((lmc_softc_t *) cur->priv)->next_module) {
                prev = cur;
            }

            if (prev == NULL)
                Lmc_root_dev = dev;
            else
                ((lmc_softc_t *) prev->priv)->next_module = dev;

            ((lmc_softc_t *) dev->priv)->next_module = NULL;
            /* end insert */

            foundaddr = dev->base_addr;

            cards_found++;
            intcf++;
        }
    lmc_probe_next_card:
        pci_index++;
    }

    if (cards_found < 1)
        return -1;

#if LINUX_VERSION_CODE >= 0x20200
    return foundaddr;
#else
    return 0;
#endif
}

/* After this is called, packets can be sent.
 * Does not initialize the addresses
 */
static int lmc_open (struct device *dev) /*fold00*/
{
    lmc_softc_t *sc = dev->priv;
    int err;

    lmc_dec_reset (sc);
    lmc_reset (sc);

    LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
    LMC_EVENT_LOG(LMC_EVENT_RESET2,
                  lmc_mii_readreg (sc, 0, 16),
                  lmc_mii_readreg (sc, 0, 17));


    if (sc->lmc_ok)
        return (0);

    lmc_softreset (sc);

    /* Since we have to use PCI bus, this should work on x86,alpha,ppc */
    if (request_irq (dev->irq, &lmc_interrupt, SA_SHIRQ, dev->name, dev)){
        printk(KERN_WARNING "%s: could not get irq: %d\n", dev->name, dev->irq);
        return -EAGAIN;
    }
    sc->got_irq = 1;

    /* Assert Terminal Active */
    sc->lmc_miireg16 |= LMC_MII16_LED_ALL;
    sc->lmc_media->set_link_status (sc, LMC_LINK_UP);

    /*
     * reset to last state.
     */
    sc->lmc_media->set_status (sc, NULL);

    /* setup default bits to be used in tulip_desc_t transmit descriptor
     * -baz */
    sc->TxDescriptControlInit = (
                                 LMC_TDES_INTERRUPT_ON_COMPLETION
                                 | LMC_TDES_FIRST_SEGMENT
                                 | LMC_TDES_LAST_SEGMENT
                                 | LMC_TDES_SECOND_ADDR_CHAINED
                                 | LMC_TDES_DISABLE_PADDING
                                );

    if (sc->ictl.crc_length == LMC_CTL_CRC_LENGTH_16) {
        /* disable 32 bit CRC generated by ASIC */
        sc->TxDescriptControlInit |= LMC_TDES_ADD_CRC_DISABLE;
    }
    /* Acknoledge the Terminal Active and light LEDs */

    /* dev->flags |= IFF_UP; */

    err = sppp_open (dev);
    if (err){
        return err;
    }
    dev->do_ioctl = lmc_ioctl;
    dev->tbusy = 0;
    dev->start = 1;

    MOD_INC_USE_COUNT;

    /*
     * select what interrupts we want to get
     */
    sc->lmc_intrmask = 0;
    /* Should be using the default interrupt mask defined in the .h file. */
    sc->lmc_intrmask |= (TULIP_STS_NORMALINTR
                         | TULIP_STS_RXINTR
                         | TULIP_STS_TXINTR
                         | TULIP_STS_ABNRMLINTR
                         | TULIP_STS_SYSERROR
                         | TULIP_STS_TXSTOPPED
                         | TULIP_STS_TXUNDERFLOW
                         | TULIP_STS_RXSTOPPED
                        );
    LMC_CSR_WRITE (sc, csr_intr, sc->lmc_intrmask);

    sc->lmc_cmdmode |= TULIP_CMD_TXRUN;
    sc->lmc_cmdmode |= TULIP_CMD_RXRUN;
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    sc->lmc_ok = 1; /* Run watchdog */

    /*
     * Set the if up now - pfb
     */

    sc->last_link_status = 1;

    /*
     * Setup a timer for the watchdog on probe, and start it running.
     * Since lmc_ok == 0, it will be a NOP for now.
     */
    init_timer (&sc->timer);
    sc->timer.expires = jiffies + HZ;
    sc->timer.data = (unsigned long) dev;
    sc->timer.function = &lmc_watchdog;
    add_timer (&sc->timer);

    return (0);
}

/* Total reset to compensate for the AdTran DSU doing bad things
 *  under heavy load
 */

static void lmc_running_reset (struct device *dev) /*fold00*/
{

    lmc_softc_t *sc = (lmc_softc_t *) dev->priv;

    /* stop interrupts */
    /* Clear the interrupt mask */
    LMC_CSR_WRITE (sc, csr_intr, 0x00000000);

    lmc_dec_reset (sc);
    lmc_reset (sc);
    lmc_softreset (sc);
    /* sc->lmc_miireg16 |= LMC_MII16_LED_ALL; */
    sc->lmc_media->set_link_status (sc, 1);
    sc->lmc_media->set_status (sc, NULL);

    dev->flags |= IFF_RUNNING;
    dev->tbusy = 0;
    sc->lmc_txfull = 0;

    sc->lmc_intrmask = TULIP_DEFAULT_INTR_MASK;
    LMC_CSR_WRITE (sc, csr_intr, sc->lmc_intrmask);

    sc->lmc_cmdmode |= (TULIP_CMD_TXRUN | TULIP_CMD_RXRUN);
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);
}


/* This is what is called when you ifconfig down a device.
 * This disables the timer for the watchdog and keepalives,
 * and disables the irq for dev.
 */
static int lmc_close (struct device *dev) /*fold00*/
{
    /* not calling release_region() as we should */
    lmc_softc_t *sc;

    sc = dev->priv;
    sc->lmc_ok = 0;
    sc->lmc_media->set_link_status (sc, 0);
    del_timer (&sc->timer);
    sppp_close (dev);
    lmc_ifdown (dev);
    printk(KERN_DEBUG "%s: Close ran\n", dev->name);

    return 0;
}

/* Ends the transfer of packets */
/* When the interface goes down, this is called */
static int lmc_ifdown (struct device *dev) /*fold00*/
{
    lmc_softc_t *sc = dev->priv;
    u32 csr6;
    int i;

    /* Don't let anything else go on right now */
    dev->start = 0;
    dev->tbusy = 1;

    /* stop interrupts */
    /* Clear the interrupt mask */
    LMC_CSR_WRITE (sc, csr_intr, 0x00000000);

    /* Stop Tx and Rx on the chip */
    csr6 = LMC_CSR_READ (sc, csr_command);
    csr6 &= ~LMC_DEC_ST;		/* Turn off the Transmission bit */
    csr6 &= ~LMC_DEC_SR;		/* Turn off the Recieve bit */
    LMC_CSR_WRITE (sc, csr_command, csr6);

    dev->flags &= ~IFF_RUNNING;

    sc->stats.rx_missed_errors +=
        LMC_CSR_READ (sc, csr_missed_frames) & 0xffff;

    /* release the interrupt */
    if(sc->got_irq == 1){
        free_irq (dev->irq, dev);
        sc->got_irq = 0;
    }

    /* free skbuffs in the Rx queue */
    for (i = 0; i < LMC_RXDESCS; i++)
    {
        struct sk_buff *skb = sc->lmc_rxq[i];
        sc->lmc_rxq[i] = 0;
        sc->lmc_rxring[i].status = 0;
        sc->lmc_rxring[i].length = 0;
        sc->lmc_rxring[i].buffer1 = 0xDEADBEEF;
        if (skb != NULL)
        {
            LMC_SKB_FREE(skb, 1);
            LMC_DEV_KFREE_SKB (skb);
        }
        sc->lmc_rxq[i] = NULL;
    }

    for (i = 0; i < LMC_TXDESCS; i++)
    {
        if (sc->lmc_txq[i] != NULL)
            LMC_DEV_KFREE_SKB (sc->lmc_txq[i]);
        sc->lmc_txq[i] = NULL;
    }

    lmc_led_off (sc, LMC_MII16_LED_ALL);

    dev->tbusy = 0;

    MOD_DEC_USE_COUNT;
    return 0;
}

/* Interrupt handling routine.  This will take an incoming packet, or clean
 * up after a trasmit.
 */
static void lmc_interrupt (int irq, void *dev_instance, struct pt_regs *regs) /*fold00*/
{
    struct device *dev = (struct device *) dev_instance;
    lmc_softc_t *sc;
    u32 csr;
    int i;
    s32 stat;
    unsigned int badtx;
    u32 firstcsr;

    sc = dev->priv;

    spin_lock(&sc->lmc_lock);

    if (test_and_set_bit (0, (void *) &dev->interrupt)) {
        goto lmc_int_fail_out;
    }

    /*
     * Read the csr to find what interupts we have (if any)
     */
    csr = LMC_CSR_READ (sc, csr_status);

    /*
     * Make sure this is our interrupt
     */
    if ( ! (csr & sc->lmc_intrmask)) {
        goto lmc_int_fail_out;
    }

    firstcsr = csr;

    /* always go through this loop at least once */
    while (csr & sc->lmc_intrmask) {

        /*
         * Clear interupt bits, we handle all case below
         */
        LMC_CSR_WRITE (sc, csr_status, csr);

        /*
         * One of
         *  - Transmit process timed out CSR5<1>
         *  - Transmit jabber timeout    CSR5<3>
         *  - Transmit underflow         CSR5<5>
         *  - Transmit Receiver buffer unavailable CSR5<7>
         *  - Receive process stopped    CSR5<8>
         *  - Receive watchdog timeout   CSR5<9>
         *  - Early transmit interrupt   CSR5<10>
         *
         * Is this really right? Should we do a running reset for jabber?
         * (being a WAN card and all)
         */
        if (csr & TULIP_STS_ABNRMLINTR){
            lmc_running_reset (dev);
            break;
        }

        
        if (csr & (TULIP_STS_RXINTR | TULIP_STS_RXNOBUF)) {
	  lmc_rx (dev);
        }
        
        if (csr & (TULIP_STS_TXINTR | TULIP_STS_TXNOBUF | TULIP_STS_TXSTOPPED)) {

            /* reset the transmit timeout detection flag -baz */
            sc->stats.tx_NoCompleteCnt = 0;

            badtx = sc->lmc_taint_tx;
            i = badtx % LMC_TXDESCS;

            while ((badtx < sc->lmc_next_tx)) {
                stat = sc->lmc_txring[i].status;

                LMC_EVENT_LOG (LMC_EVENT_XMTINT, stat, 0);
                /*
                 * If bit 31 is 1 the tulip owns it break out of the loop
                 */
                if (stat < 0)
                    break;

                /*
                 * If we have no skbuff or have cleared it
                 * Already continue to the next buffer
                 */
                if (sc->lmc_txq[i] == NULL)
                    continue;

                /*
                 * Check the total error summary to look for any errors
                 */
                if (stat & 0x8000) {
                    sc->stats.tx_errors++;
                    if (stat & 0x4104)
                        sc->stats.tx_aborted_errors++;
                    if (stat & 0x0C00)
                        sc->stats.tx_carrier_errors++;
                    if (stat & 0x0200)
                        sc->stats.tx_window_errors++;
                    if (stat & 0x0002)
                        sc->stats.tx_fifo_errors++;
                }
                else {
                    
#if LINUX_VERSION_CODE >= 0x20200
                    sc->stats.tx_bytes += sc->lmc_txring[i].length & 0x7ff;
#endif
                    
                    sc->stats.tx_packets++;
                }
                
                LMC_DEV_KFREE_SKB (sc->lmc_txq[i]);
                sc->lmc_txq[i] = 0;

                badtx++;
                i = badtx % LMC_TXDESCS;
            }

            if (sc->lmc_next_tx - badtx > LMC_TXDESCS)
            {
                printk ("%s: out of sync pointer\n", dev->name);
                badtx += LMC_TXDESCS;
            }
            sc->lmc_txfull = 0;
            dev->tbusy = 0;
            mark_bh (NET_BH);  /* Tell Linux to give me more packets */

#ifdef DEBUG
            sc->stats.dirtyTx = badtx;
            sc->stats.lmc_next_tx = sc->lmc_next_tx;
            sc->stats.lmc_txfull = sc->lmc_txfull;
            sc->stats.tbusy = dev->tbusy;
#endif
            sc->lmc_taint_tx = badtx;

            /*
             * Why was there a break here???
             */
        }			/* end handle transmit interrupt */

        if (csr & TULIP_STS_SYSERROR) {
            u32 error;
            printk (KERN_WARNING "%s: system bus error csr: %#8.8x\n", dev->name, csr);
            error = csr>>23 & 0x7;
            switch(error){
            case 0x000:
                printk(KERN_WARNING "%s: Parity Fault (bad)\n", dev->name);
                break;
            case 0x001:
                printk(KERN_WARNING "%s: Master Abort (naughty)\n", dev->name);
                break;
            case 0x010:
                printk(KERN_WARNING "%s: Target Abort (not so naughty)\n", dev->name);
                break;
            default:
                printk(KERN_WARNING "%s: This bus error code was supposed to be reserved!\n", dev->name);
            }
            lmc_dec_reset (sc);
            lmc_reset (sc);
            LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
            LMC_EVENT_LOG(LMC_EVENT_RESET2,
                          lmc_mii_readreg (sc, 0, 16),
                          lmc_mii_readreg (sc, 0, 17));

        }
        /*
         * Get current csr status to make sure
         * we've cleared all interupts
         */
        csr = LMC_CSR_READ (sc, csr_status);
    }				/* end interrupt loop */
    LMC_EVENT_LOG(LMC_EVENT_INT, firstcsr, csr);

    dev->interrupt = 0;

lmc_int_fail_out:

    spin_unlock(&sc->lmc_lock);
    return;
}

static int lmc_start_xmit (struct sk_buff *skb, struct device *dev) /*fold00*/
{
    lmc_softc_t *sc;
    u32 flag;
    int entry;
    int ret = 0;
    LMC_SPIN_FLAGS;

    sc = dev->priv;

    /* the interface better be up */
    if ( ! (dev->flags & IFF_UP))
        return 1;

    spin_lock_irqsave(&sc->lmc_lock, flags);

    /*
     * If the transmitter is busy
     * this must be the 5 second polling
     * from the kernel which called us.
     * Poke the chip and try to get it running
     *
     */
    if (dev->tbusy) {
        u32 csr6;

        if (jiffies - dev->trans_start < TX_TIMEOUT) {
            ret = 1;
            goto lmc_start_xmit_bug_out;
        }

        /*
         * Chip seems to have locked up
         * Reset it
         * This whips out all our decriptor
         * table and starts from scartch
         */

        LMC_EVENT_LOG(LMC_EVENT_XMTPRCTMO,
                      LMC_CSR_READ (sc, csr_status),
                      sc->stats.tx_ProcTimeout);

        lmc_running_reset (dev);

        LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
        LMC_EVENT_LOG(LMC_EVENT_RESET2,
                      lmc_mii_readreg (sc, 0, 16),
                      lmc_mii_readreg (sc, 0, 17));

        /* restart the tx processes */
        csr6 = LMC_CSR_READ (sc, csr_command);
        LMC_CSR_WRITE (sc, csr_command, csr6 | 0x0002);
        LMC_CSR_WRITE (sc, csr_command, csr6 | 0x2002);

        /* immediate transmit */
        LMC_CSR_WRITE (sc, csr_txpoll, 0);

        sc->stats.tx_errors++;
        sc->stats.tx_ProcTimeout++;	/* -baz */

        dev->trans_start = jiffies;

        ret = 1;
        goto lmc_start_xmit_bug_out;
    }
    /* normal path */

    entry = sc->lmc_next_tx % LMC_TXDESCS;

    sc->lmc_txq[entry] = skb;
    sc->lmc_txring[entry].buffer1 = virt_to_bus (skb->data);

    LMC_CONSOLE_LOG("xmit", skb->data, skb->len);

    /* If the queue is less than half full, don't interrupt */
    if (sc->lmc_next_tx - sc->lmc_taint_tx < LMC_TXDESCS / 2)
    {
        /* Do not interrupt on completion of this packet */
        flag = 0x60000000;
        dev->tbusy = 0;
    }
    else if (sc->lmc_next_tx - sc->lmc_taint_tx == LMC_TXDESCS / 2)
    {
        /* This generates an interrupt on completion of this packet */
        flag = 0xe0000000;
        dev->tbusy = 0;
    }
    else if (sc->lmc_next_tx - sc->lmc_taint_tx < LMC_TXDESCS - 1)
    {
        /* Do not interrupt on completion of this packet */
        flag = 0x60000000;
        dev->tbusy = 0;
    }
    else
    {
        /* This generates an interrupt on completion of this packet */
        flag = 0xe0000000;
        sc->lmc_txfull = 1;
        dev->tbusy = 1;
    }

    if ((entry == LMC_TXDESCS - 1))
    {
        flag |= 0xe2000000;
        dev->tbusy = 1;
    }
    /* don't pad small packets either */
    sc->lmc_txring[entry].length = (skb->len) | flag | sc->TxDescriptControlInit;

    /* Done above through TxDescControlInit */
    /*
     sc->lmc_txring[entry].length = (skb->len) | flag | 0x00800000;
     if(sc->ictl.crc_length == LMC_CTL_CRC_LENGTH_16){
     sc->lmc_txring[entry].length |= 0x04000000;
     }
     */

    /* set the transmit timeout flag to be checked in
     * the watchdog timer handler. -baz
     */

    sc->stats.tx_NoCompleteCnt++;
    sc->lmc_next_tx++;

    /* give ownership to the chip */
    sc->lmc_txring[entry].status = 0x80000000;

    /* send now! */
    LMC_CSR_WRITE (sc, csr_txpoll, 0);

    dev->trans_start = jiffies;

lmc_start_xmit_bug_out:

    spin_unlock_irqrestore(&sc->lmc_lock, flags);
    return ret;
}


static int lmc_rx (struct device *dev) /*FOLD00*/
{
    lmc_softc_t *sc;
    int i;
    int rx_work_limit = LMC_RXDESCS;
    unsigned int next_rx;
    int rxIntLoopCnt;		/* debug -baz */
    int localLengthErrCnt = 0;
    long stat;
    struct sk_buff *skb, *nsb;
    u16 len;

    sc = dev->priv;

    if ( ! (dev->flags & IFF_UP))
        return 1;

    rxIntLoopCnt = 0;		/* debug -baz */

    i = sc->lmc_next_rx % LMC_RXDESCS;
    next_rx = sc->lmc_next_rx;

    while (((stat = sc->lmc_rxring[i].status) & LMC_RDES_OWN_BIT) != DESC_OWNED_BY_DC21X4)
    {
        rxIntLoopCnt++;		/* debug -baz */
        LMC_EVENT_LOG(LMC_EVENT_RCVINT, stat, 0);

        if ((stat & 0x0300) != 0x0300) {  /* Check first segment and last segment */
            if ((stat & 0x0000ffff) != 0x7fff) {
                /* Oversized frame */
                sc->stats.rx_length_errors++;
                goto skip_packet;
            }
        }

        if(stat & 0x00000008){ /* Catch a dribbling bit error */
            sc->stats.rx_errors++;
            sc->stats.rx_frame_errors++;
            goto skip_packet;
        }


        if(stat & 0x00000004){ /* Catch a CRC error by the Xilinx */
            sc->stats.rx_errors++;
            sc->stats.rx_crc_errors++;
            goto skip_packet;
        }


        len = ((stat & LMC_RDES_FRAME_LENGTH) >> RDES_FRAME_LENGTH_BIT_NUMBER);
        if (len > PKT_BUF_SZ){
            sc->stats.rx_length_errors++;
            localLengthErrCnt++;
            goto skip_packet;
        }

        if (len < sc->lmc_crcSize + 2) {
            sc->stats.rx_length_errors++;
            sc->stats.rx_SmallPktCnt++;
            localLengthErrCnt++;
            goto skip_packet;
        }

        if(stat & 0x00004000){
            printk(KERN_WARNING "%s: Receiver descriptor error, receiver out of sync?\n", dev->name);
        }

        len -= sc->lmc_crcSize;

        skb = sc->lmc_rxq[i];

        /*
         * We ran out of memory at some point
         * just allocate an skb buff and continue.
         */
        
        if(skb == 0x0){
            nsb = dev_alloc_skb (PKT_BUF_SZ + 2);
            if (nsb) {
                LMC_SKB_FREE(nsb, 1);
                sc->lmc_rxq[i] = nsb;
                nsb->dev = dev;
                sc->lmc_rxring[i].buffer1 = virt_to_bus (nsb->tail);
            }
            goto skip_packet;
        }
        
        dev->last_rx = jiffies;
        sc->stats.rx_packets++;

        LMC_CONSOLE_LOG("recv", skb->data, len);

        /*
         * I'm not sure of the sanity of this
         * Packets could be arriving at a constant
         * 44.210mbits/sec and we're going to copy
         * them into a new buffer??
         */
        
        if(len > LMC_MTU * 0.75){
            /*
             * If it's a large packet don't copy it just hand it up
             */
        give_it_anyways:

            sc->lmc_rxq[i] = 0x0;
            sc->lmc_rxring[i].buffer1 = 0x0;

            skb_put (skb, len);
            skb->protocol=htons(ETH_P_WAN_PPP);
            skb->mac.raw = skb->data;
            skb->nh.raw = skb->data;
            skb->dev = dev;
            netif_rx(skb);

            /*
             * This skb will be destroyed by the upper layers, make a new one
             */
            nsb = dev_alloc_skb (PKT_BUF_SZ + 2);
            if (nsb) {
                LMC_SKB_FREE(nsb, 1);
                sc->lmc_rxq[i] = nsb;
                nsb->dev = dev;
                sc->lmc_rxring[i].buffer1 = virt_to_bus (nsb->tail);
                /* Transfered to 21140 below */
            }
            else {
                /*
                 * We've run out of memory, stop trying to allocate
                 * memory and exit the interupt handler
                 *
                 * The chip may run out of receivers and stop
                 * in which care we'll try to allocate the buffer
                 * again.  (once a second)
                 */
                sc->stats.rx_BuffAllocErr++;
                goto skip_out_of_mem;

            }

        }
        else {
            nsb = dev_alloc_skb(len);
            if(!nsb) {
                goto give_it_anyways;
            }
            memcpy(skb_put(nsb, len), skb->data, len);
            nsb->protocol=htons(ETH_P_WAN_PPP);
            nsb->mac.raw = nsb->data;
            nsb->nh.raw = nsb->data;
            nsb->dev = dev;
            netif_rx(nsb);
        }

    skip_packet:
        sc->lmc_rxring[i].status = DESC_OWNED_BY_DC21X4;

        sc->lmc_next_rx++;
        i = sc->lmc_next_rx % LMC_RXDESCS;
        rx_work_limit--;
        if (rx_work_limit < 0)
            break;
    }
    
    /* detect condition for LMC1000 where DSU cable attaches and fills
     * descriptors with bogus packets
     */
    if (localLengthErrCnt > LMC_RXDESCS - 3) {
        sc->stats.rx_BadPktSurgeCnt++;
        LMC_EVENT_LOG(LMC_EVENT_BADPKTSURGE,
                      localLengthErrCnt,
                      sc->stats.rx_BadPktSurgeCnt);
    }

    /* save max count of receive descriptors serviced */
    if (rxIntLoopCnt > sc->stats.rxIntLoopCnt) {
        sc->stats.rxIntLoopCnt = rxIntLoopCnt;	/* debug -baz */
    }

#ifdef DEBUG
    if (rxIntLoopCnt == 0)
    {
        for (i = 0; i < LMC_RXDESCS; i++)
        {
            if ((sc->lmc_rxring[i].status & LMC_RDES_OWN_BIT)
                != DESC_OWNED_BY_DC21X4)
            {
                rxIntLoopCnt++;
            }
        }
        LMC_EVENT_LOG(LMC_EVENT_RCVEND, rxIntLoopCnt, 0);
    }
#endif

skip_out_of_mem:

    return 0;
}

static struct enet_statistics *lmc_get_stats (struct device *dev) /*fold00*/
{
    lmc_softc_t *sc;
    LMC_SPIN_FLAGS;

    sc = dev->priv;

    spin_lock_irqsave(&sc->lmc_lock, flags);

    if (dev->start)
        sc->stats.rx_missed_errors += LMC_CSR_READ (sc, csr_missed_frames) & 0xffff;

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    return (struct enet_statistics *) &sc->stats;
}

#ifdef MODULE

int init_module (void) /*fold00*/
{
    printk ("lmc: module loaded\n");

    /* Have lmc_probe search for all the cards, and allocate devices */
    if (lmc_probe (NULL) < 0)
        return -EIO;

    return 0;
}

void cleanup_module (void) /*fold00*/
{
    struct device *dev, *next;

    /* we have no pointer to our devices, since they are all dynamically
     * allocated.  So, here we loop through all the network devices
     * looking for ours.  When found, dispose of them properly.
     */

    for (dev = Lmc_root_dev;
         dev != NULL;
         dev = next )
    {

        next = ((lmc_softc_t *) dev->priv)->next_module; /* get it now before we deallocate it */
        printk ("%s: removing...\n", dev->name);

        /* close the syncppp stuff, and release irq. Close is run on unreg net */
        lmc_close (dev);
        sppp_detach (dev);

        /* Remove the device from the linked list */
        unregister_netdev (dev);

        /* Let go of the io region */;
        release_region (dev->base_addr, LMC_REG_RANGE);

        /* free our allocated structures. */
        kfree (dev->priv);
        dev->priv = NULL;

        kfree ((struct ppp_device *) dev);
        dev = NULL;
    }


    Lmc_root_dev = NULL;
    printk ("lmc module unloaded\n");
}
#endif

unsigned lmc_mii_readreg (lmc_softc_t * const sc, unsigned devaddr, unsigned regno) /*fold00*/
{
    int i;
    int command = (0xf6 << 10) | (devaddr << 5) | regno;
    int retval = 0;

    LMC_MII_SYNC (sc);

    for (i = 15; i >= 0; i--)
    {
        int dataval = (command & (1 << i)) ? 0x20000 : 0;

        LMC_CSR_WRITE (sc, csr_9, dataval);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        LMC_CSR_WRITE (sc, csr_9, dataval | 0x10000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
    }

    for (i = 19; i > 0; i--)
    {
        LMC_CSR_WRITE (sc, csr_9, 0x40000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        retval = (retval << 1) | ((LMC_CSR_READ (sc, csr_9) & 0x80000) ? 1 : 0);
        LMC_CSR_WRITE (sc, csr_9, 0x40000 | 0x10000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
    }

    return (retval >> 1) & 0xffff;
}

void lmc_mii_writereg (lmc_softc_t * const sc, unsigned devaddr, /*fold00*/
                       unsigned regno, unsigned data)
{
    int i = 32;
    int command = (0x5002 << 16) | (devaddr << 23) | (regno << 18) | data;

    LMC_MII_SYNC (sc);

    i = 31;
    while (i >= 0)
    {
        int datav;

        if (command & (1 << i))
            datav = 0x20000;
        else
            datav = 0x00000;

        LMC_CSR_WRITE (sc, csr_9, datav);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        LMC_CSR_WRITE (sc, csr_9, (datav | 0x10000));
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        i--;
    }

    i = 2;
    while (i > 0)
    {
        LMC_CSR_WRITE (sc, csr_9, 0x40000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        LMC_CSR_WRITE (sc, csr_9, 0x50000);
        lmc_delay ();
        /* __SLOW_DOWN_IO; */
        i--;
    }
}

static void lmc_softreset (lmc_softc_t * const sc) /*fold00*/
{
    int i;

    /* Initialize the recieve rings and buffers. */
    sc->lmc_txfull = 0;
    sc->lmc_next_rx = 0;
    sc->lmc_next_tx = 0;
    sc->lmc_taint_rx = 0;
    sc->lmc_taint_tx = 0;

    /*
     * Setup each one of the receiver buffers
     * allocate an skbuff for each one, setup the the descriptor table
     * and point each buffer at the next one
     */

    for (i = 0; i < LMC_RXDESCS; i++)
    {
        struct sk_buff *skb;

        if (sc->lmc_rxq[i] == NULL)
        {
            skb = dev_alloc_skb (PKT_BUF_SZ + 2);
            sc->lmc_rxq[i] = skb;
        }
        else
        {
            skb = sc->lmc_rxq[i];
        }

        skb->dev = sc->lmc_device;
        LMC_SKB_FREE(skb, 1);

        /* owned by 21140 */
        sc->lmc_rxring[i].status = 0x80000000;

        /* used to be PKT_BUF_SZ now uses skb since we loose some to head room */
        sc->lmc_rxring[i].length = skb->end - skb->data;

        /* use to be tail which is dumb since you're thinking why write
         * to the end of the packj,et but since there's nothing there tail == data
         */
        sc->lmc_rxring[i].buffer1 = virt_to_bus (skb->data);

        /* This is fair since the structure is static and we have the next address */
        sc->lmc_rxring[i].buffer2 = virt_to_bus (&sc->lmc_rxring[i + 1]);

    }

    /*
     * Sets end of ring
     */
    sc->lmc_rxring[i - 1].length |= 0x02000000; /* Set end of buffers flag */
    sc->lmc_rxring[i - 1].buffer2 = virt_to_bus (&sc->lmc_rxring[0]); /* Point back to the start */
    LMC_CSR_WRITE (sc, csr_rxlist, virt_to_bus (sc->lmc_rxring)); /* write base address */


    /* Initialize the transmit rings and buffers */
    for (i = 0; i < LMC_TXDESCS; i++)
    {
        sc->lmc_txq[i] = 0;
        sc->lmc_txring[i].status = 0x00000000;
        sc->lmc_txring[i].buffer2 = virt_to_bus (&sc->lmc_txring[i + 1]);
    }
    sc->lmc_txring[i - 1].buffer2 = virt_to_bus (&sc->lmc_txring[0]);
    LMC_CSR_WRITE (sc, csr_txlist, virt_to_bus (sc->lmc_txring));
}

static int lmc_set_config(struct device *dev, struct ifmap *map) /*fold00*/
{
    return -EOPNOTSUPP;
}

void lmc_gpio_mkinput(lmc_softc_t * const sc, u_int32_t bits)
{
    sc->lmc_gpio_io &= ~bits;
    LMC_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET | (sc->lmc_gpio_io));
}

void lmc_gpio_mkoutput(lmc_softc_t * const sc, u_int32_t bits)
{
    sc->lmc_gpio_io |= bits;
    LMC_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET | (sc->lmc_gpio_io));
}

void lmc_led_on(lmc_softc_t * const sc, u_int32_t led)
{
    if((~sc->lmc_miireg16) & led) /* Already on! */
        return;
    
    sc->lmc_miireg16 &= ~led;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);
}

void lmc_led_off(lmc_softc_t * const sc, u_int32_t led)
{
    if(sc->lmc_miireg16 & led) /* Already set don't do anything */
        return;
    
    sc->lmc_miireg16 |= led;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);
}

static void lmc_reset(lmc_softc_t * const sc)
{
    sc->lmc_miireg16 |= LMC_MII16_FIFO_RESET;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

    sc->lmc_miireg16 &= ~LMC_MII16_FIFO_RESET;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

    /*
     * make some of the GPIO pins be outputs
     */
    lmc_gpio_mkoutput(sc, LMC_GEP_DP | LMC_GEP_RESET);

    /*
     * drive DP and RESET low to force configuration.  This also forces
     * the transmitter clock to be internal, but we expect to reset
     * that later anyway.
     */
    sc->lmc_gpio &= ~(LMC_GEP_DP | LMC_GEP_RESET);
    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);

    /*
     * hold for more than 10 microseconds
     */
    udelay(50);

    /*
     * stop driving Xilinx-related signals
     */
    lmc_gpio_mkinput(sc, LMC_GEP_DP | LMC_GEP_RESET);

    /*
     * busy wait for the chip to reset
     */
    while ((LMC_CSR_READ(sc, csr_gp) & LMC_GEP_DP) == 0)
        ;

    /*
     * Call media specific init routine
     */
    sc->lmc_media->init(sc);

    sc->stats.resetCount++;
}

static void lmc_dec_reset(lmc_softc_t * const sc)
{
    u_int32_t val;

    /*
     * disable all interrupts
     */
    sc->lmc_intrmask = 0;
    LMC_CSR_WRITE(sc, csr_intr, sc->lmc_intrmask);

    /*
     * Reset the chip with a software reset command.
     * Wait 10 microseconds (actually 50 PCI cycles but at
     * 33MHz that comes to two microseconds but wait a
     * bit longer anyways)
     */
    LMC_CSR_WRITE(sc, csr_busmode, TULIP_BUSMODE_SWRESET);
    udelay(25);
    sc->lmc_cmdmode = LMC_CSR_READ(sc, csr_command);

    /*
     * We want:
     *   no ethernet address in frames we write
     *   disable padding (txdesc, padding disable)
     *   ignore runt frames (rdes0 bit 15)
     *   no receiver watchdog or transmitter jabber timer
     *       (csr15 bit 0,14 == 1)
     *   if using 16-bit CRC, turn off CRC (trans desc, crc disable)
     */

    sc->lmc_cmdmode |= ( TULIP_CMD_PROMISCUOUS
                         | TULIP_CMD_FULLDUPLEX
                         | TULIP_CMD_PASSBADPKT
                         | TULIP_CMD_NOHEARTBEAT
                         | TULIP_CMD_PORTSELECT
                         | TULIP_CMD_RECEIVEALL
                         | TULIP_CMD_MUSTBEONE
                       );
    sc->lmc_cmdmode &= ~( TULIP_CMD_OPERMODE
                          | TULIP_CMD_THRESHOLDCTL
                          | TULIP_CMD_STOREFWD
                          | TULIP_CMD_TXTHRSHLDCTL
                        );

    LMC_CSR_WRITE(sc, csr_command, sc->lmc_cmdmode);

    /*
     * disable receiver watchdog and transmit jabber
     */
    val = LMC_CSR_READ(sc, csr_sia_general);
    val |= (TULIP_WATCHDOG_TXDISABLE | TULIP_WATCHDOG_RXDISABLE);
    LMC_CSR_WRITE(sc, csr_sia_general, val);

}

static void lmc_initcsrs(lmc_softc_t * const sc, lmc_csrptr_t csr_base,
                         size_t csr_size)
{
    sc->lmc_csrs.csr_busmode	        = csr_base +  0 * csr_size;
    sc->lmc_csrs.csr_txpoll		= csr_base +  1 * csr_size;
    sc->lmc_csrs.csr_rxpoll		= csr_base +  2 * csr_size;
    sc->lmc_csrs.csr_rxlist		= csr_base +  3 * csr_size;
    sc->lmc_csrs.csr_txlist		= csr_base +  4 * csr_size;
    sc->lmc_csrs.csr_status		= csr_base +  5 * csr_size;
    sc->lmc_csrs.csr_command	        = csr_base +  6 * csr_size;
    sc->lmc_csrs.csr_intr		= csr_base +  7 * csr_size;
    sc->lmc_csrs.csr_missed_frames	= csr_base +  8 * csr_size;
    sc->lmc_csrs.csr_9		        = csr_base +  9 * csr_size;
    sc->lmc_csrs.csr_10		        = csr_base + 10 * csr_size;
    sc->lmc_csrs.csr_11		        = csr_base + 11 * csr_size;
    sc->lmc_csrs.csr_12		        = csr_base + 12 * csr_size;
    sc->lmc_csrs.csr_13		        = csr_base + 13 * csr_size;
    sc->lmc_csrs.csr_14		        = csr_base + 14 * csr_size;
    sc->lmc_csrs.csr_15		        = csr_base + 15 * csr_size;
}

