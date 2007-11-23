/*
 *	ROSE release 003
 *
 *	This code REQUIRES 2.1.0 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	ROSE 001	Jonathan(G4KLX)	Cloned from nr_route.c.
 *			Terry(VK2KTJ)	Added support for variable length
 *					address masks.
 *	ROSE 002	Jonathan(G4KLX)	Uprated through routing of packets.
 *					Routing loop detection.
 *	ROSE 003	Jonathan(G4KLX)	Added use count to neighbours.
 */

#include <linux/config.h>
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/arp.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/firewall.h>
#include <net/rose.h>

static unsigned int rose_neigh_no = 1;

static struct rose_node  *rose_node_list  = NULL;
static struct rose_neigh *rose_neigh_list = NULL;
static struct rose_route *rose_route_list = NULL;

static void rose_remove_neigh(struct rose_neigh *);
static int rose_del_node(struct rose_route_struct *, struct device *);

static int rose_is_null(rose_address *address)
{
	int i;
	
	for (i = 0 ; i < sizeof(rose_address) ; i++)
		if (address->rose_addr[i])
			return 0;
	return 1;
}

/*
 *	Add a new route to a node, and in the process add the node and the
 *	neighbour if it is new.
 */
static int rose_add_node(struct rose_route_struct *rose_route, struct device *dev)
{
	struct rose_node  *rose_node, *rose_tmpn, *rose_tmpp;
	struct rose_neigh *rose_neigh;
	struct rose_route_struct dummy_route;
	unsigned long flags;
	int i;

	for (rose_node = rose_node_list; rose_node != NULL; rose_node = rose_node->next)
		if ((rose_node->mask == rose_route->mask) && (rosecmpm(&rose_route->address, &rose_node->address, rose_route->mask) == 0))
			break;

	for (rose_neigh = rose_neigh_list; rose_neigh != NULL; rose_neigh = rose_neigh->next)
		if (ax25cmp(&rose_route->neighbour, &rose_neigh->callsign) == 0 && rose_neigh->dev == dev)
			break;

	if (rose_neigh == NULL) {
		if ((rose_neigh = (struct rose_neigh *)kmalloc(sizeof(*rose_neigh), GFP_ATOMIC)) == NULL)
			return -ENOMEM;

		rose_neigh->callsign  = rose_route->neighbour;
		rose_neigh->digipeat  = NULL;
		rose_neigh->ax25      = NULL;
		rose_neigh->dev       = dev;
		rose_neigh->count     = 0;
		rose_neigh->use       = 0;
		rose_neigh->dce_mode  = 0;
		rose_neigh->number    = rose_neigh_no++;
		rose_neigh->restarted = 0;
		skb_queue_head_init(&rose_neigh->queue);
		rose_neigh->t0timer   = 0;
		rose_neigh->ftimer    = 0;
		init_timer(&rose_neigh->timer);

		if (rose_route->ndigis != 0) {
			if ((rose_neigh->digipeat = kmalloc(sizeof(ax25_digi), GFP_KERNEL)) == NULL) {
				kfree_s(rose_neigh, sizeof(*rose_neigh));
				return -ENOMEM;
			}

			rose_neigh->digipeat->ndigi      = rose_route->ndigis;
			rose_neigh->digipeat->lastrepeat = -1;

			for (i = 0; i < rose_route->ndigis; i++) {
				rose_neigh->digipeat->calls[i]    = rose_route->digipeaters[i];
				rose_neigh->digipeat->repeated[i] = 0;
			}
		}

		save_flags(flags); cli();
		rose_neigh->next = rose_neigh_list;
		rose_neigh_list  = rose_neigh;
		restore_flags(flags);
	}

	/*
	 * This is a new node to be inserted into the list. Find where it needs
	 * to be inserted into the list, and insert it. We want to be sure
	 * to order the list in descending order of mask size to ensure that
	 * later when we are searching this list the first match will be the
	 * best match.
	 */
	if (rose_node == NULL) {
		rose_tmpn = rose_node_list;
		rose_tmpp = NULL;

		while (rose_tmpn != NULL) {
			if (rose_tmpn->mask > rose_route->mask) {
				rose_tmpp = rose_tmpn;
				rose_tmpn = rose_tmpn->next;
			} else {
				break;
			}
		}

		/* create new node */
		if ((rose_node = (struct rose_node *)kmalloc(sizeof(*rose_node), GFP_ATOMIC)) == NULL)
			return -ENOMEM;

		rose_node->address = rose_route->address;
		rose_node->mask    = rose_route->mask;
		rose_node->count   = 1;
		rose_node->neighbour[0] = rose_neigh;

		save_flags(flags); cli();

		if (rose_tmpn == NULL) {
			if (rose_tmpp == NULL) {	/* Empty list */
				rose_node_list  = rose_node;
				rose_node->next = NULL;
			} else {
				rose_tmpp->next = rose_node;
				rose_node->next = NULL;
			}
		} else {
			if (rose_tmpp == NULL) {	/* 1st node */
				rose_node->next = rose_node_list;
				rose_node_list  = rose_node;
			} else {
				rose_tmpp->next = rose_node;
				rose_node->next = rose_tmpn;
			}
		}

		restore_flags(flags);

		rose_neigh->count++;

	} else if (rose_node->count < ROSE_MAX_ALTERNATE) {
		/* We have space, slot it in */
		rose_node->neighbour[rose_node->count] = rose_neigh;
		rose_node->count++;
		rose_neigh->count++;
	}

	if (!rose_is_null(&rose_route->address))
	{
		/* Delete this neighbourg from the dummy node 0000000000 */
		dummy_route = *rose_route;
		dummy_route.mask = 10;
		memset(&dummy_route.address, 0, sizeof(rose_address));
		rose_del_node(&dummy_route, dev);
	}
	
	return 0;
}

static void rose_remove_node(struct rose_node *rose_node)
{
	struct rose_node *s;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if ((s = rose_node_list) == rose_node) {
		rose_node_list = rose_node->next;
		restore_flags(flags);
		kfree_s(rose_node, sizeof(struct rose_node));
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == rose_node) {
			s->next = rose_node->next;
			restore_flags(flags);
			kfree_s(rose_node, sizeof(struct rose_node));
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

static void rose_remove_neigh(struct rose_neigh *rose_neigh)
{
	struct rose_neigh *s;
	unsigned long flags;
	struct sk_buff *skb;

	del_timer(&rose_neigh->timer);

	while ((skb = skb_dequeue(&rose_neigh->queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	save_flags(flags);
	cli();

	if ((s = rose_neigh_list) == rose_neigh) {
		rose_neigh_list = rose_neigh->next;
		restore_flags(flags);
		if (rose_neigh->digipeat != NULL)
			kfree_s(rose_neigh->digipeat, sizeof(ax25_digi));
		kfree_s(rose_neigh, sizeof(struct rose_neigh));
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == rose_neigh) {
			s->next = rose_neigh->next;
			restore_flags(flags);
			if (rose_neigh->digipeat != NULL)
				kfree_s(rose_neigh->digipeat, sizeof(ax25_digi));
			kfree_s(rose_neigh, sizeof(struct rose_neigh));
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

static void rose_remove_route(struct rose_route *rose_route)
{
	struct rose_route *s;
	unsigned long flags;

	if (rose_route->tr1.neigh != NULL)
		rose_route->tr1.neigh->use--;

	if (rose_route->tr2.neigh != NULL)
		rose_route->tr2.neigh->use--;

	save_flags(flags);
	cli();

	if ((s = rose_route_list) == rose_route) {
		rose_route_list = rose_route->next;
		restore_flags(flags);
		kfree_s(rose_route, sizeof(struct rose_route));
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == rose_route) {
			s->next = rose_route->next;
			restore_flags(flags);
			kfree_s(rose_route, sizeof(struct rose_route));
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

/*
 *	"Delete" a node. Strictly speaking remove a route to a node. The node
 *	is only deleted if no routes are left to it.
 */
static int rose_del_node(struct rose_route_struct *rose_route, struct device *dev)
{
	struct rose_node  *rose_node;
	struct rose_neigh *rose_neigh;
	struct rose_route_struct dummy_route;
	int i;

	for (rose_node = rose_node_list; rose_node != NULL; rose_node = rose_node->next)
		if ((rose_node->mask == rose_route->mask) && (rosecmpm(&rose_route->address, &rose_node->address, rose_route->mask) == 0))
			break;

	if (rose_node == NULL) return -EINVAL;

	for (rose_neigh = rose_neigh_list; rose_neigh != NULL; rose_neigh = rose_neigh->next)
		if (ax25cmp(&rose_route->neighbour, &rose_neigh->callsign) == 0 && rose_neigh->dev == dev)
			break;

	if (rose_neigh == NULL) return -EINVAL;

	for (i = 0; i < rose_node->count; i++) {
		if (rose_node->neighbour[i] == rose_neigh) {
			rose_neigh->count--;

			if (rose_neigh->count == 0) {
				/* Add a dummy node to keep the neighbourg still visible */
				dummy_route = *rose_route;
				dummy_route.mask = 10;
				memset(&dummy_route.address, 0, sizeof(rose_address));
				rose_add_node(&dummy_route, dev);
			}
			/* if (rose_neigh->count == 0 && rose_neigh->use == 0) {
				rose_remove_neigh(rose_neigh);
			} */

			rose_node->count--;

			if (rose_node->count == 0) {
				rose_remove_node(rose_node);
			} else {
				switch (i) {
					case 0:
						rose_node->neighbour[0] = rose_node->neighbour[1];
					case 1:
						rose_node->neighbour[1] = rose_node->neighbour[2];
					case 2:
						break;
				}
			}

			return 0;
		}
	}

	return -EINVAL;
}

/*
 *	A device has been removed. Remove its routes and neighbours.
 */
void rose_rt_device_down(struct device *dev)
{
	struct rose_neigh *s, *rose_neigh = rose_neigh_list;
	struct rose_node  *t, *rose_node;
	int i;

	while (rose_neigh != NULL) {
		s          = rose_neigh;
		rose_neigh = rose_neigh->next;

		if (s->dev == dev) {
			rose_node = rose_node_list;

			while (rose_node != NULL) {
				t         = rose_node;
				rose_node = rose_node->next;

				for (i = 0; i < t->count; i++) {
					if (t->neighbour[i] == s) {
						t->count--;

						switch (i) {
							case 0:
								t->neighbour[0] = t->neighbour[1];
							case 1:
								t->neighbour[1] = t->neighbour[2];
							case 2:
								break;
						}
					}
				}

				if (t->count <= 0)
					rose_remove_node(t);
			}

			rose_remove_neigh(s);
		}
	}
}

/*
 *	A device has been removed. Remove its links.
 */
void rose_route_device_down(struct device *dev)
{
	struct rose_route *s, *rose_route = rose_route_list;

	while (rose_route != NULL) {
		s          = rose_route;
		rose_route = rose_route->next;

		if (s->tr1.neigh->dev == dev || s->tr2.neigh->dev == dev)
			rose_remove_route(s);
	}
}

/*
 *	Clear all nodes and neighbours out, except for neighbours with
 *	active connections going through them.
 */
static int rose_clear_routes(void)
{
	struct rose_neigh *s, *rose_neigh = rose_neigh_list;
	struct rose_node  *t, *rose_node  = rose_node_list;

	while (rose_node != NULL) {
		t         = rose_node;
		rose_node = rose_node->next;

		rose_remove_node(t);
	}

	while (rose_neigh != NULL) {
		s          = rose_neigh;
		rose_neigh = rose_neigh->next;

		s->count = 0;

		if (s->use == 0)
			rose_remove_neigh(s);
	}

	return 0;
}

/*
 *	Check that the device given is a valid AX.25 interface that is "up".
 */
struct device *rose_ax25_dev_get(char *devname)
{
	struct device *dev;

	if ((dev = dev_get(devname)) == NULL)
		return NULL;

	if ((dev->flags & IFF_UP) && dev->type == ARPHRD_AX25)
		return dev;

	return NULL;
}

/*
 *	Find the first active ROSE device, usually "rose0".
 */
struct device *rose_dev_first(void)
{
	struct device *dev, *first = NULL;

	for (dev = dev_base; dev != NULL; dev = dev->next)
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_ROSE)
			if (first == NULL || strncmp(dev->name, first->name, 3) < 0)
				first = dev;

	return first;
}

/*
 *	Find the ROSE device for the given address.
 */
struct device *rose_dev_get(rose_address *addr)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next)
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_ROSE && rosecmp(addr, (rose_address *)dev->dev_addr) == 0)
			return dev;

	return NULL;
}

struct rose_route *rose_route_free_lci(unsigned int lci, struct rose_neigh *neigh)
{
	struct rose_route *rose_route;

	for (rose_route = rose_route_list; rose_route != NULL; rose_route = rose_route->next)
		if ((rose_route->tr1.neigh == neigh && rose_route->tr1.lci == lci) ||
		    (rose_route->tr2.neigh == neigh && rose_route->tr2.lci == lci))
			return rose_route;

	return NULL;
}

/*
 *	Find a neighbour given a ROSE address.
 */
struct rose_neigh *rose_get_neigh(rose_address *addr, unsigned char *cause, unsigned char *diagnostic)
{
	struct rose_node *node;
	int failed = 0;
	int i;

	for (node = rose_node_list; node != NULL; node = node->next) {
		if (rosecmpm(addr, &node->address, node->mask) == 0) {
			for (i = 0; i < node->count; i++) {
				if (node->neighbour[i]->ftimer == 0)
					return node->neighbour[i];
				else
					failed = 1;
			}
			/* do not accept other node address */
			break;
		}
	}

	if (failed) {
		*cause      = ROSE_OUT_OF_ORDER;
		*diagnostic = 0;
	} else {
		*cause      = ROSE_NOT_OBTAINABLE;
		*diagnostic = 0;
	}

	return NULL;
}

/*
 *	Handle the ioctls that control the routing functions.
 */
int rose_rt_ioctl(unsigned int cmd, void *arg)
{
	struct rose_route_struct rose_route;
	struct device *dev;
	int err;

	switch (cmd) {

		case SIOCADDRT:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(struct rose_route_struct))) != 0)
				return err;
			memcpy_fromfs(&rose_route, arg, sizeof(struct rose_route_struct));
			if ((dev = rose_ax25_dev_get(rose_route.device)) == NULL)
				return -EINVAL;
			/* if (rose_dev_get(&rose_route.address) != NULL)	/ Can't add routes to ourself /
				return -EINVAL; */
			if (rose_route.mask > 10) /* Mask can't be more than 10 digits */
				return -EINVAL;

			return rose_add_node(&rose_route, dev);

		case SIOCDELRT:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(struct rose_route_struct))) != 0)
				return err;
			memcpy_fromfs(&rose_route, arg, sizeof(struct rose_route_struct));
			if ((dev = rose_ax25_dev_get(rose_route.device)) == NULL)
				return -EINVAL;
			return rose_del_node(&rose_route, dev);

		case SIOCRSCLRRT:
			return rose_clear_routes();

		default:
			return -EINVAL;
	}

	return 0;
}

static void rose_del_route_by_neigh(struct rose_neigh *rose_neigh)
{
	struct rose_route *rose_route, *s;
	struct sk_buff    *skb;

	rose_neigh->restarted = 0;
	rose_neigh->t0timer   = 0;
	rose_neigh->ftimer    = sysctl_rose_link_fail_timeout;

	rose_link_set_timer(rose_neigh);

	while ((skb = skb_dequeue(&rose_neigh->queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	rose_route = rose_route_list;

	while (rose_route != NULL) {
		if ((rose_route->tr1.neigh == rose_neigh && rose_route->tr2.neigh == rose_neigh) ||
		    (rose_route->tr1.neigh == rose_neigh && rose_route->tr2.neigh == NULL)       ||
		    (rose_route->tr2.neigh == rose_neigh && rose_route->tr1.neigh == NULL)) {
			s = rose_route->next;
			rose_remove_route(rose_route);
			rose_route = s;
			continue;
		}

		if (rose_route->tr1.neigh == rose_neigh) {
			rose_route->tr1.neigh->use--;
			rose_route->tr1.neigh = NULL;
			rose_transmit_clear_request(rose_route->tr2.neigh, rose_route->tr2.lci, ROSE_OUT_OF_ORDER, 0);
		}

		if (rose_route->tr2.neigh == rose_neigh) {
			rose_route->tr2.neigh->use--;
			rose_route->tr2.neigh = NULL;
			rose_transmit_clear_request(rose_route->tr1.neigh, rose_route->tr1.lci, ROSE_OUT_OF_ORDER, 0);
		}

		rose_route = rose_route->next;
	}
}

/*
 * 	A level 2 link has timed out, therefore it appears to be a poor link,
 *	then don't use that neighbour until it is reset. Blow away all through
 *	routes and connections using this route.
 */
void rose_link_failed(ax25_cb *ax25, int reason)
{
	struct rose_neigh *rose_neigh;

	for (rose_neigh = rose_neigh_list; rose_neigh != NULL; rose_neigh = rose_neigh->next)
		if (rose_neigh->ax25 == ax25)
			break;

	if (rose_neigh == NULL) return;

	rose_neigh->ax25 = NULL;

	rose_del_route_by_neigh(rose_neigh);
	rose_kill_by_neigh(rose_neigh);
}

/*
 *	A device has been "downed" remove its link status. Blow away all
 *	through routes and connections that use this device.
 */
void rose_link_device_down(struct device *dev)
{
	struct rose_neigh *rose_neigh;

	for (rose_neigh = rose_neigh_list; rose_neigh != NULL; rose_neigh = rose_neigh->next) {
		if (rose_neigh->dev == dev) {
			rose_del_route_by_neigh(rose_neigh);
			rose_kill_by_neigh(rose_neigh);
		}
	}
}

/*
 *	A Restart has occured. Delete all existing routes to this neighbour
 */
void rose_clean_neighbour(struct rose_neigh *rose_neigh)
{
	rose_del_route_by_neigh(rose_neigh);
	rose_kill_by_neigh(rose_neigh);
}


/*
 *	Route a frame to an appropriate AX.25 connection.
 */
int rose_route_frame(struct sk_buff *skb, ax25_cb *ax25)
{
	struct rose_neigh *rose_neigh, *new_neigh;
	struct rose_route *rose_route;
	struct rose_facilities_struct facilities;
	rose_address *src_addr, *dest_addr;
	struct sock *sk;
	unsigned short frametype;
	unsigned int lci, new_lci;
	unsigned char cause, diagnostic;
	struct device *dev;
	unsigned long flags;
	int len;
	struct sk_buff *skbn;

#ifdef CONFIG_FIREWALL
	if (call_in_firewall(PF_ROSE, skb->dev, skb->data, NULL) != FW_ACCEPT)
		return 0;
#endif

	frametype = skb->data[2];
	lci = ((skb->data[0] << 8) & 0xF00) + ((skb->data[1] << 0) & 0x0FF);
	src_addr  = (rose_address *)(skb->data + 9);
	dest_addr = (rose_address *)(skb->data + 4);

	for (rose_neigh = rose_neigh_list; rose_neigh != NULL; rose_neigh = rose_neigh->next)
		if (ax25cmp(&ax25->dest_addr, &rose_neigh->callsign) == 0 && ax25->device == rose_neigh->dev)
			break;

	if (rose_neigh == NULL) {
		printk("rose_route : unknown neighbour or device %s\n", ax2asc(&ax25->dest_addr));
		return 0;
	}

	/*
	 *	Obviously the link is working, halt the ftimer.
	 */
	rose_neigh->ftimer = 0;

	/*
	 *	LCI of zero is always for us, and its always a restart
	 * 	frame.
	 */
	if (lci == 0) {
		rose_link_rx_restart(skb, rose_neigh, frametype);
		return 0;
	}

	/*
	 *	Find an existing socket.
	 */
	if ((sk = rose_find_socket(lci, rose_neigh)) != NULL) {
		if (frametype == ROSE_CALL_REQUEST)
		{
			/* F6FBB - Remove an existing unused socket */
			rose_clear_queues(sk);
			sk->protinfo.rose->cause      = ROSE_NETWORK_CONGESTION;
			sk->protinfo.rose->diagnostic = 0;
			sk->protinfo.rose->neighbour->use--;
			sk->protinfo.rose->neighbour = NULL;
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->err                  = 0;
			sk->shutdown            |= SEND_SHUTDOWN;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                 = 1;
		}
		else
		{
			skb->h.raw = skb->data;
			return rose_process_rx_frame(sk, skb);
		}
	}

	/*
	 *	Is it a Call Request and is it for us ?
	 */
	if (frametype == ROSE_CALL_REQUEST)
		if ((dev = rose_dev_get(dest_addr)) != NULL) {
			return rose_rx_call_request(skb, dev, rose_neigh, lci);
		}

	if (!sysctl_rose_routing_control) {
		rose_transmit_clear_request(rose_neigh, lci, ROSE_NOT_OBTAINABLE, 0);
		return 0;
	}

	/*
	 *	Route it to the next in line if we have an entry for it.
	 */
	for (rose_route = rose_route_list; rose_route != NULL; rose_route = rose_route->next) {
		if (rose_route->tr1.lci == lci && rose_route->tr1.neigh == rose_neigh) {
			if (frametype == ROSE_CALL_REQUEST) {
				/* F6FBB - Remove an existing unused route */
				rose_remove_route(rose_route);
				break;
			} else if (rose_route->tr2.neigh != NULL) {
				if ((skbn = skb_copy(skb, GFP_ATOMIC)) != NULL) {
					kfree_skb(skb, FREE_READ);
					skb = skbn;
				}
				skb->data[0] &= 0xF0;
				skb->data[0] |= (rose_route->tr2.lci >> 8) & 0x0F;
				skb->data[1]  = (rose_route->tr2.lci >> 0) & 0xFF;
				rose_transmit_link(skb, rose_route->tr2.neigh);
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				return 1;
			} else {
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				return 0;
			}
		}
		if (rose_route->tr2.lci == lci && rose_route->tr2.neigh == rose_neigh) {
			if (frametype == ROSE_CALL_REQUEST) {
				/* F6FBB - Remove an existing unused route */
				rose_remove_route(rose_route);
				break;
			} else if (rose_route->tr1.neigh != NULL) {
				if ((skbn = skb_copy(skb, GFP_ATOMIC)) != NULL) {
					kfree_skb(skb, FREE_READ);
					skb = skbn;
				}
				skb->data[0] &= 0xF0;
				skb->data[0] |= (rose_route->tr1.lci >> 8) & 0x0F;
				skb->data[1]  = (rose_route->tr1.lci >> 0) & 0xFF;
				rose_transmit_link(skb, rose_route->tr1.neigh);
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				return 1;
			} else {
				if (frametype == ROSE_CLEAR_CONFIRMATION)
					rose_remove_route(rose_route);
				return 0;
			}
		}
	}

	/*
	 *	We know that:
	 *	1. The frame isn't for us,
	 *	2. It isn't "owned" by any existing route.
	 */
	if (frametype != ROSE_CALL_REQUEST)	{ /* Trashed packet */
		return 0;
	}

	len  = (((skb->data[3] >> 4) & 0x0F) + 1) / 2;
	len += (((skb->data[3] >> 0) & 0x0F) + 1) / 2;

	memset(&facilities, 0x00, sizeof(struct rose_facilities_struct));
	
	if (!rose_parse_facilities(skb->data + len + 4, &facilities)) {
		printk("CR : rose_parse_facilities error\n");
		rose_transmit_clear_request(rose_neigh, lci, ROSE_INVALID_FACILITY, 76);
		return 0;
	}

	/*
	 *	Check for routing loops.
	 */
	for (rose_route = rose_route_list; rose_route != NULL; rose_route = rose_route->next) {
		if (rose_route->rand == facilities.rand &&
		    rosecmp(src_addr, &rose_route->src_addr) == 0 &&
		    ax25cmp(&facilities.dest_call, &rose_route->src_call) == 0 &&
		    ax25cmp(&facilities.source_call, &rose_route->dest_call) == 0) {
			rose_transmit_clear_request(rose_neigh, lci, ROSE_NOT_OBTAINABLE, 120);
			return 0;
		}
	}

	if ((new_neigh = rose_get_neigh(dest_addr, &cause, &diagnostic)) == NULL) {
		rose_transmit_clear_request(rose_neigh, lci, cause, diagnostic);
		return 0;
	}

	if ((new_lci = rose_new_lci(new_neigh)) == 0) {
		rose_transmit_clear_request(rose_neigh, lci, ROSE_NETWORK_CONGESTION, 71);
		return 0;
	}

	if ((rose_route = (struct rose_route *)kmalloc(sizeof(*rose_route), GFP_ATOMIC)) == NULL) {
		rose_transmit_clear_request(rose_neigh, lci, ROSE_NETWORK_CONGESTION, 121);
		return 0;
	}

	rose_route->src_addr  = *src_addr;
	rose_route->dest_addr = *dest_addr;
	rose_route->src_call  = facilities.dest_call;
	rose_route->dest_call = facilities.source_call;
	rose_route->rand      = facilities.rand;
	rose_route->tr1.lci   = lci;
	rose_route->tr1.neigh = rose_neigh;
	rose_route->tr2.lci   = new_lci;
	rose_route->tr2.neigh = new_neigh;

	rose_route->tr1.neigh->use++;
	rose_route->tr2.neigh->use++;

	save_flags(flags); cli();
	rose_route->next = rose_route_list;
	rose_route_list  = rose_route;
	restore_flags(flags);

	if ((skbn = skb_copy(skb, GFP_ATOMIC)) != NULL) {
		kfree_skb(skb, FREE_READ);
		skb = skbn;
	}

	skb->data[0] &= 0xF0;
	skb->data[0] |= (rose_route->tr2.lci >> 8) & 0x0F;
	skb->data[1]  = (rose_route->tr2.lci >> 0) & 0xFF;

	rose_transmit_link(skb, rose_route->tr2.neigh);

	return 1;
}

int rose_nodes_get_info(char *buffer, char **start, off_t offset,
		      int length, int dummy)
{
	struct rose_node *rose_node;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;
	int i;

	cli();

	len += sprintf(buffer, "address    mask n neigh neigh neigh\n");

	for (rose_node = rose_node_list; rose_node != NULL; rose_node = rose_node->next) {
		len += sprintf(buffer + len, "%-10s %04d %d",
			rose2asc(&rose_node->address),
			rose_node->mask,
			rose_node->count);			

		for (i = 0; i < rose_node->count; i++)
			len += sprintf(buffer + len, " %05d",
				rose_node->neighbour[i]->number);

		len += sprintf(buffer + len, "\n");

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return len;
} 

int rose_neigh_get_info(char *buffer, char **start, off_t offset,
		      int length, int dummy)
{
	struct rose_neigh *rose_neigh;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;
	int i;

	cli();

	len += sprintf(buffer, "addr  callsign  dev  count use mode restart  t0  tf digipeaters\n");

	for (rose_neigh = rose_neigh_list; rose_neigh != NULL; rose_neigh = rose_neigh->next) {
		len += sprintf(buffer + len, "%05d %-9s %-4s   %3d %3d  %3s     %3s %3d %3d",
			rose_neigh->number,
			ax2asc(&rose_neigh->callsign),
			rose_neigh->dev ? rose_neigh->dev->name : "???",
			rose_neigh->count,
			rose_neigh->use,
			(rose_neigh->dce_mode) ? "DCE" : "DTE",
			(rose_neigh->restarted) ? "yes" : "no",
			rose_neigh->t0timer / ROSE_SLOWHZ,
			rose_neigh->ftimer  / ROSE_SLOWHZ);

		if (rose_neigh->digipeat != NULL) {
			for (i = 0; i < rose_neigh->digipeat->ndigi; i++)
				len += sprintf(buffer + len, " %s", ax2asc(&rose_neigh->digipeat->calls[i]));
		}

		len += sprintf(buffer + len, "\n");

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return len;
} 

int rose_routes_get_info(char *buffer, char **start, off_t offset,
		      int length, int dummy)
{
	struct rose_route *rose_route;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;

	cli();

	len += sprintf(buffer, "lci  address     callsign   neigh  <-> lci  address     callsign   neigh\n");

	for (rose_route = rose_route_list; rose_route != NULL; rose_route = rose_route->next) {
		if (rose_route->tr1.neigh != NULL) {
			len += sprintf(buffer + len, "%3.3X  %-10s  %-9s  %05d      ",
				rose_route->tr1.lci,
				rose2asc(&rose_route->src_addr),
				ax2asc(&rose_route->src_call),
				rose_route->tr1.neigh->number);
		} else {
			len += sprintf(buffer + len, "000  *           *          00000      ");
		}

		if (rose_route->tr2.neigh != NULL) {
			len += sprintf(buffer + len, "%3.3X  %-10s  %-9s  %05d\n",
				rose_route->tr2.lci,
				rose2asc(&rose_route->dest_addr),
				ax2asc(&rose_route->dest_call),
				rose_route->tr2.neigh->number);
		} else {
			len += sprintf(buffer + len, "000  *           *          00000\n");
		}

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return len;
} 

#ifdef MODULE

/*
 *	Release all memory associated with ROSE routing structures.
 */
void rose_rt_free(void)
{
	struct rose_neigh *s, *rose_neigh = rose_neigh_list;
	struct rose_node  *t, *rose_node  = rose_node_list;
	struct rose_route *u, *rose_route = rose_route_list;

	while (rose_neigh != NULL) {
		s          = rose_neigh;
		rose_neigh = rose_neigh->next;

		rose_remove_neigh(s);
	}

	while (rose_node != NULL) {
		t         = rose_node;
		rose_node = rose_node->next;

		rose_remove_node(t);
	}

	while (rose_route != NULL) {
		u          = rose_route;
		rose_route = rose_route->next;

		rose_remove_route(u);
	}
}

#endif

#endif
