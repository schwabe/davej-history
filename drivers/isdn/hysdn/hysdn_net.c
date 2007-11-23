/* $Id: hysdn_net.c,v 1.1.2.1 2001/12/31 13:26:46 kai Exp $
 *
 * Linux driver for HYSDN cards, net (ethernet type) handling routines.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * This net module has been inspired by the skeleton driver from
 * Donald Becker (becker@CESDIS.gsfc.nasa.gov)
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>

#include "hysdn_defs.h"

unsigned int hynet_enable = 0xffffffff; 
MODULE_PARM(hynet_enable, "i");

/* store the actual version for log reporting */
char *hysdn_net_revision = "$Revision: 1.1.2.1 $";

#define MAX_SKB_BUFFERS 20	/* number of buffers for keeping TX-data */

/****************************************************************************/
/* structure containing the complete network data. The structure is aligned */
/* in a way that both, the device and statistics are kept inside it.        */
/* for proper access, the device structure MUST be the first var/struct     */
/* inside the definition.                                                   */
/****************************************************************************/
struct net_local {
	struct device netdev;	/* the network device */
	struct net_device_stats stats;
	/* additional vars may be added here */
	char dev_name[9];	/* our own device name */

	struct sk_buff *tx_skb;	/* buffer for tx operation */

};				/* net_local */


/*****************************************************/
/* Get the current statistics for this card.         */
/* This may be called with the card open or closed ! */
/*****************************************************/
static struct net_device_stats *
net_get_stats(struct device *dev)
{
	return (&((struct net_local *) dev)->stats);
}				/* net_device_stats */

/*********************************************************************/
/* Open/initialize the board. This is called (in the current kernel) */
/* sometime after booting when the 'ifconfig' program is run.        */
/* This routine should set everything up anew at each open, even     */
/* registers that "should" only need to be set once at boot, so that */
/* there is non-reboot way to recover if something goes wrong.       */
/*********************************************************************/
static int
net_open(struct device *dev)
{
	struct in_device *in_dev;
	hysdn_card *card = dev->priv;
	int i;

	dev->tbusy = 0;		/* non busy state */
	dev->interrupt = 0;
	if (!dev->start)
		MOD_INC_USE_COUNT;	/* increment only if device is down */
	dev->start = 1;		/* and started */

	/* Fill in the MAC-level header (if not already set) */
	if (!card->mac_addr[0]) {
		for (i = 0; i < ETH_ALEN - sizeof(ulong); i++)
			dev->dev_addr[i] = 0xfc;
		if ((in_dev = dev->ip_ptr) != NULL) {
			struct in_ifaddr *ifa = in_dev->ifa_list;
			if (ifa != NULL)
				memcpy(dev->dev_addr + (ETH_ALEN - sizeof(ulong)), &ifa->ifa_local, sizeof(ulong));
		}
	} else
		memcpy(dev->dev_addr, card->mac_addr, ETH_ALEN);

	return (0);
}				/* net_open */



/*********************************************************************/
/* close/decativate the device. The device is not removed, but only  */
/* deactivated.                                                      */
/*********************************************************************/
static int
net_close(struct device *dev)
{

	dev->tbusy = 1;		/* we are busy */

	if (dev->start)
		MOD_DEC_USE_COUNT;	/* dec only if device has been active */

	dev->start = 0;		/* and not started */

	return (0);		/* success */
}				/* net_close */

/************************************/
/* send a packet on this interface. */
/* only for kernel versions < 2.3.33 */
/************************************/
static int
net_send_packet(struct sk_buff *skb, struct device *dev)
{
	struct net_local *lp = (struct net_local *) dev;

	if (dev->tbusy) {
		/*
		 * If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead.
		 * As ISDN may have higher timeouts than real ethernet 10s timeout
		 */
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < (10000 * HZ) / 1000)
			return 1;
		printk(KERN_WARNING "%s: transmit timed out. \n", dev->name);
		dev->tbusy = 0;
		dev->trans_start = jiffies;
	}
	/*
	 * Block a timer-based transmit from overlapping. This could better be
	 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
	if (test_and_set_bit(0, (void *) &dev->tbusy) != 0)
		printk(KERN_WARNING "%s: Transmitter access conflict.\n", dev->name);

	else {
		lp->stats.tx_bytes += skb->len;
		dev->trans_start = jiffies;
		lp->tx_skb = skb;	/* remember skb pointer */
		queue_task(&((hysdn_card *) dev->priv)->irq_queue, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}

	return (0);		/* success */
}				/* net_send_packet */



/***********************************************************************/
/* acknowlegde a packet send. The network layer will be informed about */
/* completion                                                          */
/***********************************************************************/
void
hysdn_tx_netack(hysdn_card * card)
{
	struct net_local *lp = card->netif;

	if (!lp)
		return;		/* non existing device */

	if (lp->tx_skb)
		dev_kfree_skb(lp->tx_skb);	/* free tx pointer */
	lp->tx_skb = NULL;	/* reset pointer */

	lp->stats.tx_packets++;
	lp->netdev.tbusy = 0;
	mark_bh(NET_BH);	/* Inform upper layers. */
}				/* hysdn_tx_netack */

/*****************************************************/
/* we got a packet from the network, go and queue it */
/*****************************************************/
void
hysdn_rx_netpkt(hysdn_card * card, uchar * buf, word len)
{
	struct net_local *lp = card->netif;
	struct sk_buff *skb;

	if (!lp)
		return;		/* non existing device */

	lp->stats.rx_bytes += len;

	skb = dev_alloc_skb(len);
	if (skb == NULL) {
		printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
		       lp->netdev.name);
		lp->stats.rx_dropped++;
		return;
	}
	skb->dev = &lp->netdev;

	/* copy the data */
	memcpy(skb_put(skb, len), buf, len);

	/* determine the used protocol */
	skb->protocol = eth_type_trans(skb, &lp->netdev);

	netif_rx(skb);
	lp->stats.rx_packets++;	/* adjust packet count */

}				/* hysdn_rx_netpkt */

/*****************************************************/
/* return the pointer to a network packet to be send */
/*****************************************************/
struct sk_buff *
hysdn_tx_netget(hysdn_card * card)
{
	struct net_local *lp = card->netif;

	if (!lp)
		return (NULL);	/* non existing device */

	return (lp->tx_skb);	/* return packet pointer */

}				/* hysdn_tx_netget */


/*******************************************/
/* init function called by register device */
/*******************************************/
static int
net_init(struct device *dev)
{
	/* setup the function table */
	dev->open = net_open;
	dev->stop = net_close;
	dev->hard_start_xmit = net_send_packet;
	dev->get_stats = net_get_stats;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	return (0);		/* success */
}				/* net_init */

/*****************************************************************************/
/* hysdn_net_create creates a new net device for the given card. If a device */
/* already exists, it will be deleted and created a new one. The return value */
/* 0 announces success, else a negative error code will be returned.         */
/*****************************************************************************/
int
hysdn_net_create(hysdn_card * card)
{
	struct device *dev;
	int i;
	if(!card) {
		printk(KERN_WARNING "No card-pt in hysdn_net_create!\n");
		return (-ENOMEM);
	}
	hysdn_net_release(card);	/* release an existing net device */
	if ((dev = kmalloc(sizeof(struct net_local), GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING "HYSDN: unable to allocate mem\n");
		return (-ENOMEM);
	}
	memset(dev, 0, sizeof(struct net_local));	/* clean the structure */


	/* initialise necessary or informing fields */
	dev->base_addr = card->iobase;	/* IO address */
	dev->irq = card->irq;	/* irq */
	dev->init = net_init;	/* the init function of the device */
	dev->name = ((struct net_local *) dev)->dev_name;	/* device name */
	if ((i = register_netdev(dev))) {
		printk(KERN_WARNING "HYSDN: unable to create network device\n");
		kfree(dev);
		return (i);
	}
	dev->priv = card;	/* remember pointer to own data structure */
	card->netif = dev;	/* setup the local pointer */

	if (card->debug_flags & LOG_NET_INIT)
		hysdn_addlog(card, "network device created");
	return (0);		/* and return success */
}				/* hysdn_net_create */

/***************************************************************************/
/* hysdn_net_release deletes the net device for the given card. The return */
/* value 0 announces success, else a negative error code will be returned. */
/***************************************************************************/
int
hysdn_net_release(hysdn_card * card)
{
	struct device *dev = card->netif;

	if (!dev)
		return (0);	/* non existing */

	card->netif = NULL;	/* clear out pointer */
	dev->stop(dev);		/* close the device */


	unregister_netdev(dev);	/* release the device */
	kfree(dev);		/* release the memory allocated */
	if (card->debug_flags & LOG_NET_INIT)
		hysdn_addlog(card, "network device deleted");

	return (0);		/* always successful */
}				/* hysdn_net_release */

/*****************************************************************************/
/* hysdn_net_getname returns a pointer to the name of the network interface. */
/* if the interface is not existing, a "-" is returned.                      */
/*****************************************************************************/
char *
hysdn_net_getname(hysdn_card * card)
{
	struct device *dev = card->netif;

	if (!dev)
		return ("-");	/* non existing */

	return (dev->name);
}				/* hysdn_net_getname */
