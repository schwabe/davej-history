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
 *	ROSE 001	Jonathan(G4KLX)	Cloned from nr_out.c
 *	ROSE 003	Jonathan(G4KLX)	Removed M bit processing.
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
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/rose.h>

/* 
 *	This procedure is passed a buffer descriptor for an iframe. It builds
 *	the rest of the control part of the frame and then writes it out.
 */
static void rose_send_iframe(struct sock *sk, struct sk_buff *skb)
{
	if (skb == NULL)
		return;

	skb->data[2] |= (sk->protinfo.rose->vr << 5) & 0xE0;
	skb->data[2] |= (sk->protinfo.rose->vs << 1) & 0x0E;

	rose_transmit_link(skb, sk->protinfo.rose->neighbour);	
}

void rose_kick(struct sock *sk)
{
	struct sk_buff *skb;
	unsigned short end;

	del_timer(&sk->timer);

	end = (sk->protinfo.rose->va + sysctl_rose_window_size) % ROSE_MODULUS;

	if (!(sk->protinfo.rose->condition & ROSE_COND_PEER_RX_BUSY) &&
	    sk->protinfo.rose->vs != end                             &&
	    skb_peek(&sk->write_queue) != NULL) {
		/*
		 * Transmit data until either we're out of data to send or
		 * the window is full.
		 */

		skb  = skb_dequeue(&sk->write_queue);

		do {
			/*
			 * Transmit the frame.
			 */
			rose_send_iframe(sk, skb);

			sk->protinfo.rose->vs = (sk->protinfo.rose->vs + 1) % ROSE_MODULUS;

		} while (sk->protinfo.rose->vs != end && (skb = skb_dequeue(&sk->write_queue)) != NULL);

		sk->protinfo.rose->vl         = sk->protinfo.rose->vr;
		sk->protinfo.rose->condition &= ~ROSE_COND_ACK_PENDING;
		sk->protinfo.rose->timer      = 0;
	}

	rose_set_timer(sk);
}

/*
 * The following routines are taken from page 170 of the 7th ARRL Computer
 * Networking Conference paper, as is the whole state machine.
 */

void rose_enquiry_response(struct sock *sk)
{
	if (sk->protinfo.rose->condition & ROSE_COND_OWN_RX_BUSY)
		rose_write_internal(sk, ROSE_RNR);
	else
		rose_write_internal(sk, ROSE_RR);

	sk->protinfo.rose->vl         = sk->protinfo.rose->vr;
	sk->protinfo.rose->condition &= ~ROSE_COND_ACK_PENDING;
	sk->protinfo.rose->timer      = 0;
}

void rose_check_iframes_acked(struct sock *sk, unsigned short nr)
{
	if (sk->protinfo.rose->vs == nr) {
		sk->protinfo.rose->va = nr;
	} else {
		if (sk->protinfo.rose->va != nr)
			sk->protinfo.rose->va = nr;
	}
}

#endif