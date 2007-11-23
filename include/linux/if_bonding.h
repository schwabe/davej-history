/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 * 
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 * 
 */

#ifndef _LINUX_IF_BONDING_H
#define _LINUX_IF_BONDING_H

#include <linux/timer.h>

#define BOND_ENSLAVE     (SIOCDEVPRIVATE)
#define BOND_RELEASE     (SIOCDEVPRIVATE + 1)
#define BOND_SETHWADDR   (SIOCDEVPRIVATE + 2)

typedef struct slave {
	struct device *dev;
	struct slave *next;
	struct slave *prev;
} slave_t;

typedef struct slave_queue {
	slave_t *head;
	slave_t *tail;
	slave_t *current_slave;
	int num_slaves;
	struct device *master_dev;
	char lock;
} slave_queue_t;

typedef struct bonding {
	slave_queue_t *queue;
	int min_slaves;
	int max_slaves;
	struct net_device_stats *stats;
	struct timer_list timer;
	char timer_on;
} bonding_t;  

#endif /* _LINUX_BOND_H */

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
