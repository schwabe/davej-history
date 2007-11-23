/*
 * originally based on the dummy device.
 *
 * Copyright 1999, Thomas Davis, tadavis@lbl.gov.  
 * Licensed under the GPL. Based on dummy.c, and eql.c devices.
 *
 * bonding.c: a bonding/etherchannel/sun trunking net driver
 *
 * This is useful to talk to a Cisco 5500, running Etherchannel, aka:
 *	Linux Channel Bonding
 *	Sun Trunking (Solaris)
 *
 * How it works:
 *    ifconfig bond0 ipaddress netmask up
 *      will setup a network device, with an ip address.  No mac address 
 *	will be assigned at this time.  The hw mac address will come from 
 *	the first slave bonded to the channel.  All slaves will then use 
 *	this hw mac address.
 *
 *    ifconfig bond0 down
 *         will release all slaves, marking them as down.
 *
 *    ifenslave bond0 eth0
 *	will attache eth0 to bond0 as a slave.  eth0 hw mac address will either
 *	a: be used as initial mac address
 *	b: if a hw mac address already is there, eth0's hw mac address 
 *	   will then  be set from bond0.
 *
 * v0.1 - first working version.
 * v0.2 - changed stats to be calculated by summing slaves stats.
 *
 * Changes:
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fix leaks on failure at bond_init
 * 
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/if_bonding.h>

static int bond_xmit(struct sk_buff *skb, struct device *dev);
static struct net_device_stats *bond_get_stats(struct device *dev);

static int bond_open(struct device *dev)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int bond_close(struct device *master)
{
	bonding_t *private = (struct bonding *) master->priv;
	slave_queue_t *queue = (struct slave_queue *) private->queue;
	slave_t *slave, *next;
	unsigned long flags;

	save_flags(flags);
	cli();

	for( slave=queue->head; slave != NULL; ) {
#ifdef BONDING_DEBUG
		printk("freeing = %s\n", slave->dev->name);
#endif
		slave->dev->flags &= ~IFF_SLAVE;
		slave->dev->slave = NULL;
		next = slave->next;
		kfree(slave);
		slave=next;
		queue->num_slaves--;
	}
	queue->head = NULL;
	
	restore_flags(flags);

	MOD_DEC_USE_COUNT;
	return 0;
}

/* fake multicast ability */
static void set_multicast_list(struct device *dev)
{
}

static int bond_enslave(struct device *master, struct device *slave)
{
	bonding_t *private = (struct bonding *) master->priv;
	slave_queue_t *queue = (struct slave_queue *) private->queue;
	slave_t *new_slave;
	unsigned long flags;
	
	if (master == NULL || slave == NULL) 
		return -ENODEV;

#ifdef BONDING_DEBUG
	printk (KERN_WARNING "%s: enslaving '%s'\n", master->name, slave->name);
#endif

	save_flags(flags);
	cli();

	/* not running. */
	if ((slave->flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING)) {
		restore_flags(flags);
		return -EINVAL;
	}

	/* already enslaved */
	if (master->flags & IFF_SLAVE || slave->flags & IFF_SLAVE) {
		restore_flags(flags);
		return -EBUSY;
	}
		   
	if ((new_slave = kmalloc(sizeof(slave_t), GFP_KERNEL)) == NULL) {
		restore_flags(flags);
		return -ENOMEM;
	}
	memset(new_slave, 0, sizeof(slave_t));

	slave->slave = master;     /* save the master in slave->slave */
	slave->flags |= IFF_SLAVE;

	new_slave->dev = slave;

	if (queue->head == NULL) {
		queue->head = new_slave;
		queue->current_slave = queue->head;
	} else {
		queue->tail->next = new_slave;
	}

	queue->tail = new_slave;
	queue->num_slaves++;

	restore_flags(flags);

	return 0;
}

static int bond_release(struct device *master, struct device *slave)
{
	printk (KERN_DEBUG "%s: releasing `%s`\n", master->name, slave->name);

	return -EINVAL;
}

static int bond_sethwaddr(struct device *master, struct device *slave)
{
	memcpy(master->dev_addr, slave->dev_addr, slave->addr_len);
	return 0;
}

static int bond_ioctl(struct device *master, struct ifreq *ifr, int cmd)
{
	struct device *slave = dev_get(ifr->ifr_slave);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

#ifdef BONDING_DEBUG
	printk("master=%s, slave=%s\n", master->name, slave->name);
#endif

	switch (cmd) {
	case BOND_ENSLAVE:
		return bond_enslave(master, slave);
	case BOND_RELEASE:
		return bond_release(master, slave);
	case BOND_SETHWADDR:
		return bond_sethwaddr(master, slave);
	default:
			return -EOPNOTSUPP;
	}
}

#ifdef CONFIG_NET_FASTROUTE
static int bond_accept_fastpath(struct device *dev, struct dst_entry *dst)
{
	return -1;
}
#endif

__initfunc(int bond_init(struct device *dev))
{
	bonding_t *bond;

	/* Initialize the device structure. */
	dev->hard_start_xmit = bond_xmit;

	dev->priv = kmalloc(sizeof(struct bonding), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct bonding));

	bond = (struct bonding *) dev->priv;

	bond->queue = kmalloc(sizeof(struct slave_queue), GFP_KERNEL);
	if (bond->queue == NULL) {
		kfree(dev->priv);
		return -ENOMEM;
	}

	memset(bond->queue, 0, sizeof(struct slave_queue));

	bond->stats = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL);
	if (bond->stats == NULL) {
		kfree(dev->priv);
		kfree(bond->queue);
		return -ENOMEM;
	}

	memset(bond->stats, 0, sizeof(struct enet_statistics));

	dev->get_stats	= bond_get_stats;

	dev->open = bond_open;
	dev->stop = bond_close;
	dev->set_multicast_list = set_multicast_list;

	dev->do_ioctl = bond_ioctl;

	/* Fill in the fields of the device structure with ethernet-generic 
	   values. */
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->flags |= (IFF_MASTER|IFF_MULTICAST);
#ifdef CONFIG_NET_FASTROUTE
	dev->accept_fastpath = bond_accept_fastpath;
#endif

	return 0;
}

static int bond_xmit(struct sk_buff *skb, struct device *dev)
{
	struct device *slave;
	struct bonding *bond = (struct bonding *) dev->priv;
	struct slave_queue *queue = bond->queue;
	int good = 0;
	
	if(!queue->num_slaves) {
		dev_kfree_skb(skb);
		return 0;
	}

	while (good == 0) {
		slave = queue->current_slave->dev;
		if (slave->flags & (IFF_UP|IFF_RUNNING)) {
			skb->dev = slave;
			skb->priority = 1;
			dev_queue_xmit(skb);
			good = 1;
		}
		if (queue->current_slave->next != NULL) {
			queue->current_slave = queue->current_slave->next;
		} else {
			queue->current_slave = queue->head;
		}
	}

	return 0;
}

static struct net_device_stats *bond_get_stats(struct device *dev)
{
	bonding_t *private = dev->priv;
	slave_queue_t *queue = private->queue;
	struct net_device_stats *stats = private->stats, *sstats;
	slave_t *slave;
	
	memset(private->stats, 0, sizeof(struct net_device_stats));
	for (slave=queue->head; slave != NULL; slave=slave->next) {
		sstats = slave->dev->get_stats(slave->dev);
 
		stats->rx_packets += sstats->rx_packets;
		stats->rx_bytes += sstats->rx_bytes;
		stats->rx_errors += sstats->rx_errors;
		stats->rx_dropped += sstats->rx_dropped;

		stats->tx_packets += sstats->tx_packets;
		stats->tx_bytes += sstats->tx_bytes;
		stats->tx_errors += sstats->tx_errors;
		stats->tx_dropped += sstats->tx_dropped;

		stats->multicast += sstats->multicast;
		stats->collisions += sstats->collisions;

		stats->rx_length_errors += sstats->rx_length_errors;
		stats->rx_over_errors += sstats->rx_over_errors;
		stats->rx_crc_errors += sstats->rx_crc_errors;
		stats->rx_frame_errors += sstats->rx_frame_errors;
		stats->rx_fifo_errors += sstats->rx_fifo_errors;	
		stats->rx_missed_errors += sstats->rx_missed_errors;
	
		stats->tx_aborted_errors += sstats->tx_aborted_errors;
		stats->tx_carrier_errors += sstats->tx_carrier_errors;
		stats->tx_fifo_errors += sstats->tx_fifo_errors;
		stats->tx_heartbeat_errors += sstats->tx_heartbeat_errors;
		stats->tx_window_errors += sstats->tx_window_errors;

	}

	return stats;
}

#ifdef MODULE

__initfunc(static int bond_probe(struct device *dev))
{
	bond_init(dev);
	return 0;
}

static char bond_name[16];

static struct device dev_bond = {
		bond_name, 	/* Needs to be writeable */
		0, 0, 0, 0,
	 	0x0, 0,
	 	0, 0, 0, NULL, bond_probe };

int init_module(void)
{
	/* Find a name for this unit */
	int err=dev_alloc_name(&dev_bond,"bond%d");

	if (err<0)
		return err;

	if (register_netdev(&dev_bond) != 0)
		return -EIO;

	return 0;
}

void cleanup_module(void)
{
	struct bonding *bond = (struct bonding *) dev_bond.priv;

	unregister_netdev(&dev_bond);

	kfree(bond->queue);
	kfree(bond->stats);
	kfree(dev_bond.priv);

	dev_bond.priv = NULL;
}
#endif /* MODULE */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
