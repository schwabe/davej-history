/*
 *	ROSE release 003
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	ROSE 003	Jonathan(G4KLX)	Created this file from nr_loopback.c.
 *
 */

#include <linux/config.h>
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/timer.h>
#include <net/ax25.h>
#include <linux/skbuff.h>
#include <net/rose.h>

static struct sk_buff_head loopback_queue;
static struct timer_list loopback_timer;

static void rose_set_loopback_timer(void);

void rose_loopback_init(void)
{
	skb_queue_head_init(&loopback_queue);

	init_timer(&loopback_timer);
}

void rose_loopback_clear(void)
{
	struct sk_buff *skb;

	del_timer(&loopback_timer);

	while ((skb = skb_dequeue(&loopback_queue)) != NULL) {
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
	}
}

static int rose_loopback_running(void)
{
	return (loopback_timer.prev != NULL || loopback_timer.next != NULL);
}

int rose_loopback_queue(struct sk_buff *skb, struct rose_neigh *neigh)
{
	struct sk_buff *skbn;

	skbn = skb_clone(skb, GFP_ATOMIC);

	kfree_skb(skb, FREE_WRITE);

	if (skbn != NULL) {
		skbn->sk = (struct sock *)neigh;
		skb_queue_tail(&loopback_queue, skbn);
	}

	if (!rose_loopback_running())
	{
		rose_set_loopback_timer();
	}
	return 1;
}

static void rose_loopback_timer(unsigned long);

static void rose_set_loopback_timer(void)
{
	del_timer(&loopback_timer);

	loopback_timer.data     = 0;
	loopback_timer.function = &rose_loopback_timer;
	loopback_timer.expires  = jiffies + 10;

	add_timer(&loopback_timer);
}

static void rose_loopback_timer(unsigned long param)
{
	struct sk_buff *skb;
	struct rose_neigh *rose_neigh;
	struct sock *sk;
	struct device *dev;
	rose_address *dest_addr;
	unsigned int lci;
	unsigned short frametype;

	while ((skb = skb_dequeue(&loopback_queue)) != NULL) {
		rose_neigh = (struct rose_neigh *)skb->sk;
		lci        = ((skb->data[0] << 8) & 0xF00) + ((skb->data[1] << 0) & 0x0FF);
		dest_addr = (rose_address *)(skb->data + 4);

		/* F1OAT : Patch the LCI for proper loopback operation */
		/* Work only if the system open less than 2048 VC !!! */
		
		lci = 4096 - lci;
		skb->data[0] = (lci >> 8) & 0x0F;
		skb->data[1] = lci & 0xFF;
		
		skb->sk = NULL;

		frametype = skb->data[2];

		if (frametype == ROSE_CALL_REQUEST) {
			if ((dev = rose_dev_get(dest_addr)) == NULL) {
				kfree_skb(skb, FREE_READ);
				continue;			
			}
			if (!rose_rx_call_request(skb, dev, rose_neigh, lci))
				kfree_skb(skb, FREE_READ); 
		}
		else {
			if ((sk = rose_find_socket(lci, rose_neigh)) == NULL) {
				kfree_skb(skb, FREE_READ);
				continue;
			}
			skb->h.raw = skb->data;

			if (rose_process_rx_frame(sk, skb) == 0)
				kfree_skb(skb, FREE_READ); 
		}
	}
}

#endif
