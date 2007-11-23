/*
 * Generic HDLC support routines for Linux
 *
 * Copyright (C) 1999, 2000 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Current status:
 *    - this is work in progress
 *    - not heavily tested on SMP
 *    - currently supported:
 *	* raw IP-in-HDLC
 *	* Cisco HDLC
 *	* Frame Relay with ANSI or CCITT LMI (both user and network side)
 *	* PPP (using syncppp.c)
 *
 * Use sethdlc utility to set line parameters, protocol and PVCs
 */
 /*
   Patched by Pavel Selivanov. 08 Aug. 2001
   If we are using dev_queue_xmit, and we have a listeners,
   we should set skb->nh.raw. If no, we'll get a lot of warnings in 
   /var/log/debug
   Look at core/net/dev.c dev_queue_xmit_nit  
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/pkt_sched.h>
#include <linux/inetdevice.h>

#include "syncppp.h"
#include <linux/hdlc.h>

/* #define DEBUG_PKT */
/* #define DEBUG_HARD_HEADER */

#ifndef MODULE
static int version_printed=0;
#endif
static const char* version = "HDLC support routines revision: 1.0";


#define CISCO_MULTICAST         0x8f    /* Cisco multicast address */
#define CISCO_UNICAST           0x0f    /* Cisco unicast address */
#define CISCO_KEEPALIVE         0x8035  /* Cisco keepalive protocol */
#define CISCO_ADDR_REQ          0       /* Cisco address request */
#define CISCO_ADDR_REPLY        1       /* Cisco address reply */
#define CISCO_KEEPALIVE_REQ     2       /* Cisco keepalive request */
#define CISCO_SYS_INFO		0x2000	/* Cisco interface/system info */

static int hdlc_ioctl(struct device *dev, struct ifreq *ifr, int cmd);

/********************************************************
 *
 * Cisco HDLC support
 *
 *******************************************************/

static int cisco_hard_header(struct sk_buff *skb, struct device *dev, u16 type,
			     void *daddr, void *saddr, unsigned int len)
{
	hdlc_header *data;
#ifdef DEBUG_HARD_HEADER
	printk(KERN_DEBUG "%s: cisco_hard_header called\n", dev->name);
#endif
	
	skb_push(skb, sizeof(hdlc_header));
	data=(hdlc_header*)skb->data;
	if (type == CISCO_KEEPALIVE)
		data->address = CISCO_MULTICAST;
	else
		data->address = CISCO_UNICAST;
	data->control = 0;
	data->protocol = htons(type);
	
	return sizeof(hdlc_header);
}



static void cisco_keepalive_send(hdlc_device *hdlc, u32 type,
				 u32 par1, u32 par2)
{
	struct sk_buff *skb;
	cisco_packet *data;
 
	skb=dev_alloc_skb(sizeof(hdlc_header)+sizeof(cisco_packet));
	skb_reserve(skb, 4);
	cisco_hard_header(skb, hdlc_to_dev(hdlc), CISCO_KEEPALIVE,
			  NULL, NULL, 0);
	data=(cisco_packet*)skb->tail;
	
	data->type = htonl(type);
	data->par1 = htonl(par1);
	data->par2 = htonl(par2);
	data->rel = 0xFFFF;
	data->time = htonl(jiffies * 1000/HZ);
	
	skb_put(skb, sizeof(cisco_packet));
	skb->priority=TC_PRIO_CONTROL;
	skb->dev = hdlc_to_dev(hdlc);
	skb->nh.raw = skb->data;
	
	dev_queue_xmit(skb);
}



static void cisco_netif(hdlc_device *hdlc, struct sk_buff *skb)
{
	hdlc_header *data = (hdlc_header*)skb->data;
	cisco_packet *cisco_data;
	if (skb->len<sizeof(hdlc_header))
		goto rx_error;

	if (data->address != CISCO_MULTICAST &&
	    data->address != CISCO_UNICAST)
		goto rx_error;

	skb_pull(skb, sizeof(hdlc_header));

	switch(ntohs(data->protocol)) {
#ifdef CONFIG_INET
	case ETH_P_IP:
#endif
#ifdef CONFIG_IPX
	case ETH_P_IPX:
#endif
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	case ETH_P_IPV6:
#endif
#if defined(CONFIG_INET) || defined(CONFIG_IPX) || \
	    defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		hdlc->stats.rx_packets++; /* data traffic */
		hdlc->stats.rx_bytes+=skb->len;
		skb->protocol=data->protocol;
		skb->dev=hdlc_to_dev(hdlc);
		netif_rx(skb);
		return;
#endif

	case CISCO_SYS_INFO:
		/* Packet is not needed, drop it. */
		dev_kfree_skb(skb);
		return;

	case CISCO_KEEPALIVE:
		if (skb->len != CISCO_PACKET_LEN && 
		    skb->len != CISCO_BIG_PACKET_LEN) {
			printk(KERN_INFO "%s: Invalid length of Cisco "
			       "control packet (%d bytes)\n", hdlc->name, 
			       skb->len);
			goto rx_error;
		}

		cisco_data=(cisco_packet*)skb->data;

		switch(ntohl (cisco_data->type)) {
		case CISCO_ADDR_REQ: { /* Stolen from syncppp.c :-) */
			struct in_device *in_dev=hdlc_to_dev(hdlc)->ip_ptr;
			u32 addr = 0, mask = ~0; /* is the mask correct? */
	  
			if (in_dev != NULL) {
				struct in_ifaddr **ifap=&in_dev->ifa_list;
	    
				while (*ifap != NULL) {
					if (strcmp(hdlc_to_dev(hdlc)->name,
						   (*ifap)->ifa_label) == 0) {
						addr = (*ifap)->ifa_local;
						mask = (*ifap)->ifa_mask;
						break;
					}
					ifap=&(*ifap)->ifa_next;
				}

				hdlc->stats.rx_bytes+=skb->len;
				hdlc->stats.rx_packets++;
				cisco_keepalive_send(hdlc, CISCO_ADDR_REPLY,
						     addr, mask);
				return;
			}
		}

		case CISCO_ADDR_REPLY:
			printk(KERN_INFO "%s: Unexpected Cisco IP address "
			       "reply\n", hdlc->name);
			goto rx_error;
	  
		case CISCO_KEEPALIVE_REQ:
			hdlc->lmi.rxseq = ntohl(cisco_data->par1);
			if (ntohl(cisco_data->par2) == hdlc->lmi.txseq) {
				hdlc->lmi.last_poll = jiffies;
				if (!(hdlc->lmi.state & LINK_STATE_RELIABLE)) {
					u32 sec, min, hrs, days;
					sec = ntohl(cisco_data->time)/1000;
					min = sec / 60; sec -= min * 60;
					hrs = min / 60; min -= hrs * 60;
					days = hrs / 24; hrs -= days * 24;
					printk(KERN_INFO "%s: Link up (peer uptime %ud%uh%um%us)\n",
					       hdlc->name, days, hrs, min, sec);
				}
				hdlc->lmi.state |= LINK_STATE_RELIABLE;
			}

			hdlc->stats.rx_bytes+=skb->len;
			hdlc->stats.rx_packets++;
			dev_kfree_skb(skb);
			return;
		} /* switch(keepalive type) */
	} /* switch(protocol) */

	printk(KERN_INFO "%s: Unsupported protocol %x\n", hdlc->name,
	       data->protocol);
	hdlc->stats.rx_bytes+=skb->len;
	hdlc->stats.rx_packets++;
	dev_kfree_skb(skb);
	return;
  
 rx_error:
	hdlc->stats.rx_errors++; /* Mark error */
	dev_kfree_skb(skb);
}



static void cisco_timer(unsigned long arg)
{
	hdlc_device *hdlc=(hdlc_device*)arg;

	if ((hdlc->lmi.state & LINK_STATE_RELIABLE) &&
	    (jiffies - hdlc->lmi.last_poll >= hdlc->lmi.T392 * HZ)) {
		hdlc->lmi.state &= ~LINK_STATE_RELIABLE;
		printk(KERN_INFO "%s: Link down\n", hdlc->name);
	}

	cisco_keepalive_send(hdlc, CISCO_KEEPALIVE_REQ, ++hdlc->lmi.txseq,
			     hdlc->lmi.rxseq);
	hdlc->timer.expires = jiffies + hdlc->lmi.T391*HZ;

	hdlc->timer.function = cisco_timer;
	hdlc->timer.data = arg;
	add_timer(&hdlc->timer);
}



/******************************************************************
 *
 *     generic Frame Relay routines
 *
 *****************************************************************/


static int fr_hard_header(struct sk_buff *skb, struct device *dev, u16 type,
			  void *daddr, void *saddr, unsigned int len)
{
	u16 head_len;

	if (!daddr)
		daddr=dev->broadcast;

#ifdef DEBUG_HARD_HEADER
	printk(KERN_DEBUG "%s: fr_hard_header called\n", dev->name);
#endif

	switch(type) {
	case ETH_P_IP:
		head_len=4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_IP;
		break;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	case ETH_P_IPV6:
		head_len=4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_IPV6;
		break;
#endif

	case LMI_PROTO:
		head_len=4;
		skb_push(skb, head_len);
		skb->data[3] = LMI_PROTO;
		break;

	default:
		head_len=10;
		skb_push(skb, head_len);
		skb->data[3] = FR_PAD;
		skb->data[4] = NLPID_SNAP;
		skb->data[5] = FR_PAD;
		skb->data[6] = FR_PAD;
		skb->data[7] = FR_PAD;
		skb->data[8] = type>>8;
		skb->data[9] = (u8)type;
	}

	memcpy(skb->data, daddr, 2);
	skb->data[2] = FR_UI;
  
	return head_len;
}



static inline void fr_log_dlci_active(pvc_device *pvc)
{
	printk(KERN_INFO "%s: %sactive%s\n", pvc->name,
	       pvc->state & PVC_STATE_ACTIVE ? "" : "in",
	       pvc->state & PVC_STATE_NEW ? " new" : "");
}



static inline u8 fr_lmi_nextseq(u8 x)
{
	x++;
	return x ? x : 1;
}



static void fr_lmi_send(hdlc_device *hdlc, int fullrep)
{
	struct sk_buff *skb;
	pvc_device *pvc=hdlc->first_pvc;
	int len = mode_is(hdlc, MODE_FR_ANSI) ? LMI_ANSI_LENGTH : LMI_LENGTH;
	int stat_len = 3;
	u8 *data;
	int i=0;
  
	if (mode_is(hdlc, MODE_DCE) && fullrep) {
		len += hdlc->pvc_count * (2 + stat_len);
		if (len>HDLC_MAX_MTU) {
			printk(KERN_WARNING "%s: Too many PVCs while sending "
			       "LMI full report\n", hdlc->name);
			return;
		}
	}

	skb=dev_alloc_skb(len);
	memset(skb->data, 0, len);
	skb_reserve(skb, 4);
	fr_hard_header(skb, hdlc_to_dev(hdlc), LMI_PROTO, NULL, NULL, 0);
	data=skb->tail;
	data[i++] = LMI_CALLREF;
	data[i++] = mode_is(hdlc, MODE_DCE) ? LMI_STATUS : LMI_STATUS_ENQUIRY;
	if (mode_is(hdlc, MODE_FR_ANSI))
		data[i++] = LMI_ANSI_LOCKSHIFT;
	data[i++] = mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_REPTYPE :
		LMI_REPTYPE;
	data[i++] = LMI_REPT_LEN;
	data[i++] = fullrep ? LMI_FULLREP : LMI_INTEGRITY;

	data[i++] = mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_ALIVE : LMI_ALIVE;
	data[i++] = LMI_INTEG_LEN;
	data[i++] = hdlc->lmi.txseq = fr_lmi_nextseq(hdlc->lmi.txseq);
	data[i++] = hdlc->lmi.rxseq;

	if (mode_is(hdlc, MODE_DCE) && fullrep) {
		while (pvc) {
			data[i++] = mode_is(hdlc, MODE_FR_CCITT) ?
				LMI_CCITT_PVCSTAT:LMI_PVCSTAT;
			data[i++] = stat_len;
      
			if ((hdlc->lmi.state & LINK_STATE_RELIABLE) &&
			    (pvc->netdev.flags & IFF_UP) &&
			    !(pvc->state & (PVC_STATE_ACTIVE|PVC_STATE_NEW))) {
				pvc->state |= PVC_STATE_NEW;
				fr_log_dlci_active(pvc);
			}

			dlci_to_status(hdlc, netdev_dlci(&pvc->netdev),
				       data+i, pvc->state);
			i+=stat_len;
			pvc=pvc->next;
		}
	}

	skb_put(skb, i);
	skb->priority=TC_PRIO_CONTROL;
	skb->dev = hdlc_to_dev(hdlc);
	skb->nh.raw = skb->data;
	
	dev_queue_xmit(skb);
}



static void fr_timer(unsigned long arg)
{
	hdlc_device *hdlc=(hdlc_device*)arg;
	int i, cnt=0, reliable;
	u32 list;

	if (mode_is(hdlc, MODE_DCE))
		reliable = (jiffies - hdlc->lmi.last_poll < hdlc->lmi.T392*HZ);
	else {
		hdlc->lmi.last_errors <<= 1; /* Shift the list */
		if (hdlc->lmi.state & LINK_STATE_REQUEST) {
			printk(KERN_INFO "%s: No LMI status reply received\n",
			       hdlc->name);
			hdlc->lmi.last_errors |= 1;
		}

		for (i=0, list=hdlc->lmi.last_errors; i<hdlc->lmi.N393;
		     i++, list>>=1)
			cnt += (list & 1);	/* errors count */

		reliable = (cnt < hdlc->lmi.N392);
	}

	if ((hdlc->lmi.state & LINK_STATE_RELIABLE) !=
	    (reliable ? LINK_STATE_RELIABLE : 0)) {
		pvc_device *pvc=hdlc->first_pvc;

		while (pvc) {/* Deactivate all PVCs */
			pvc->state &= ~(PVC_STATE_NEW | PVC_STATE_ACTIVE);
			pvc=pvc->next;
		}

		hdlc->lmi.state ^= LINK_STATE_RELIABLE;
		printk(KERN_INFO "%s: Link %sreliable\n", hdlc->name,
		       reliable ? "" : "un");
    
		if (reliable) {
			hdlc->lmi.N391cnt=0;	/* Request full status */
			hdlc->lmi.state |= LINK_STATE_CHANGED;
		}
	}

	if (mode_is(hdlc, MODE_DCE))
		hdlc->timer.expires = jiffies + hdlc->lmi.T392*HZ;
	else {
		if (hdlc->lmi.N391cnt)
			hdlc->lmi.N391cnt--;
		
		fr_lmi_send(hdlc, hdlc->lmi.N391cnt == 0);
		
		hdlc->lmi.state |= LINK_STATE_REQUEST;
		hdlc->timer.expires = jiffies + hdlc->lmi.T391*HZ;
	}

	hdlc->timer.function = fr_timer;
	hdlc->timer.data = arg;
	add_timer(&hdlc->timer);
}



static int fr_lmi_recv(hdlc_device *hdlc, struct sk_buff *skb)
{
	int stat_len;
	pvc_device *pvc;
	int reptype=-1, error;
	u8 rxseq, txseq;
	int i;

	if (skb->len < (mode_is(hdlc, MODE_FR_ANSI) ?
			LMI_ANSI_LENGTH : LMI_LENGTH)) {
		printk(KERN_INFO "%s: Short LMI frame\n", hdlc->name);
		return 1;
	}

	if (skb->data[5] != (!mode_is(hdlc, MODE_DCE) ?
			     LMI_STATUS : LMI_STATUS_ENQUIRY)) {
		printk(KERN_INFO "%s: LMI msgtype=%x, Not LMI status %s\n",
		       hdlc->name, skb->data[2],
		       mode_is(hdlc, MODE_DCE) ? "enquiry" : "reply");
		return 1;
	}
  
	i = mode_is(hdlc, MODE_FR_ANSI) ? 7 : 6;
  
	if (skb->data[i] !=
	    (mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_REPTYPE : LMI_REPTYPE)) {
		printk(KERN_INFO "%s: Not a report type=%x\n", hdlc->name,
		       skb->data[i]);
		return 1;
	}
	i++;

	i++;				/* Skip length field */

	reptype=skb->data[i++];

	if (skb->data[i]!=
	    (mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_ALIVE : LMI_ALIVE)) {
		printk(KERN_INFO "%s: Unsupported status element=%x\n",
		       hdlc->name, skb->data[i]);
		return 1;
	}
	i++;

	i++;			/* Skip length field */
  
	hdlc->lmi.rxseq = skb->data[i++]; /* TX sequence from peer */
	rxseq = skb->data[i++];	/* Should confirm our sequence */

	txseq = hdlc->lmi.txseq;

	if (mode_is(hdlc, MODE_DCE)) {
		if (reptype != LMI_FULLREP && reptype != LMI_INTEGRITY) {
			printk(KERN_INFO "%s: Unsupported report type=%x\n",
			       hdlc->name, reptype);
			return 1;
		}
	}

	error=0;
	if (!(hdlc->lmi.state & LINK_STATE_RELIABLE))
		error=1;

	if (rxseq == 0 || rxseq != txseq) {
		hdlc->lmi.N391cnt=0;	/* Ask for full report next time */
		error=1;
	}

	if (mode_is(hdlc, MODE_DCE)) {
		if ((hdlc->lmi.state & LINK_STATE_FULLREP_SENT) && !error) {
/* Stop sending full report - the last one has been confirmed by DTE */
			hdlc->lmi.state &= ~LINK_STATE_FULLREP_SENT;
			pvc=hdlc->first_pvc;
			while (pvc) {
				if (pvc->state & PVC_STATE_NEW) {
					pvc->state &= ~PVC_STATE_NEW;
					pvc->state |= PVC_STATE_ACTIVE;
					fr_log_dlci_active(pvc);
	  
/* Tell DTE that new PVC is now active */
					hdlc->lmi.state |= LINK_STATE_CHANGED;
				}
				pvc=pvc->next;
			}
		}

		if (hdlc->lmi.state & LINK_STATE_CHANGED) {
			reptype = LMI_FULLREP;
			hdlc->lmi.state |= LINK_STATE_FULLREP_SENT;
			hdlc->lmi.state &= ~LINK_STATE_CHANGED;
		}
    
		fr_lmi_send(hdlc, reptype == LMI_FULLREP ? 1 : 0);
		return 0;
	}

	/* DTE */

	if (reptype != LMI_FULLREP || error)
		return 0;

	stat_len = 3;
	pvc=hdlc->first_pvc;

	while (pvc) {
		pvc->newstate = 0;
		pvc=pvc->next;
	}

	while (skb->len >= i + 2 + stat_len) {
		u16 dlci;
		u8 state=0;

		if (skb->data[i] != (mode_is(hdlc, MODE_FR_CCITT) ?
				     LMI_CCITT_PVCSTAT : LMI_PVCSTAT)) {
			printk(KERN_WARNING "%s: Invalid PVCSTAT ID: %x\n",
			       hdlc->name, skb->data[i]);
			return 1;
		}
		i++;
      
		if (skb->data[i] != stat_len) {
			printk(KERN_WARNING "%s: Invalid PVCSTAT length: %x\n",
			       hdlc->name, skb->data[i]);
			return 1;
		}
		i++;
	
		dlci=status_to_dlci(hdlc, skb->data+i, &state);
		pvc=find_pvc(hdlc, dlci);

		if (pvc)
			pvc->newstate = state;
		else if (state == PVC_STATE_NEW)
			printk(KERN_INFO "%s: new PVC available, DLCI=%u\n",
			       hdlc->name, dlci);

		i+=stat_len;
	}

	pvc=hdlc->first_pvc;
    
	while (pvc) {
		if (pvc->newstate == PVC_STATE_NEW)
			pvc->newstate = PVC_STATE_ACTIVE;

		pvc->newstate |= (pvc->state &
				  ~(PVC_STATE_NEW|PVC_STATE_ACTIVE));
		if (pvc->state != pvc->newstate) {
			pvc->state=pvc->newstate;
			fr_log_dlci_active(pvc);
		}
		pvc=pvc->next;
	}

	/* Next full report after N391 polls */
	hdlc->lmi.N391cnt = hdlc->lmi.N391;

	return 0;
}



static void fr_netif(hdlc_device *hdlc, struct sk_buff *skb)
{
	fr_hdr *fh = (fr_hdr*)skb->data;
	u8 *data = skb->data;
	u16 dlci;
	pvc_device *pvc;

	if (skb->len<4 || fh->ea1 || data[2] != FR_UI)
		goto rx_error;

	dlci = q922_to_dlci(skb->data);

	if (dlci == LMI_DLCI) {
		if (data[3] == LMI_PROTO) {
			if (fr_lmi_recv(hdlc, skb))
				goto rx_error;
			else {
				/* No request pending */
				hdlc->lmi.state &= ~LINK_STATE_REQUEST;
				hdlc->lmi.last_poll = jiffies;
				hdlc->stats.rx_bytes+=skb->len;
				hdlc->stats.rx_packets++;
				dev_kfree_skb(skb);
				return;
			}
		}

		printk(KERN_INFO "%s: Received non-LMI frame with LMI DLCI\n",
		       hdlc->name);
		goto rx_error;
	}

	pvc=find_pvc(hdlc, dlci);
	if (!pvc) {
#ifdef DEBUG_PKT
		printk(KERN_INFO "%s: No PVC for received frame's DLCI %d\n",
		       hdlc->name, dlci);
#endif
		goto rx_error;
	}

	if ((pvc->netdev.flags & IFF_UP)==0) {
#ifdef DEBUG_PKT
		printk(KERN_INFO "%s: PVC for received frame's DLCI %d is down\n",
		       hdlc->name, dlci);
#endif
		goto rx_error;
	}

	hdlc->stats.rx_packets++;	/* PVC traffic */
	hdlc->stats.rx_bytes+=skb->len;
	pvc->stats.rx_packets++;
	pvc->stats.rx_bytes+=skb->len;
    
	if ((pvc->state & PVC_STATE_FECN) != (fh->fecn ? PVC_STATE_FECN : 0)) {
		printk(KERN_INFO "%s: FECN O%s\n", pvc->name,
		       fh->fecn ? "N" : "FF");
		pvc->state ^= PVC_STATE_FECN;
	}

	if ((pvc->state & PVC_STATE_BECN) != (fh->becn ? PVC_STATE_BECN : 0)) {
		printk(KERN_INFO "%s: BECN O%s\n", pvc->name, fh->becn ? "N" : "FF");
		pvc->state ^= PVC_STATE_BECN;
	}
 

	if (data[3]==NLPID_IP) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->protocol=htons(ETH_P_IP);
		skb->dev=&pvc->netdev;
		netif_rx(skb);
		return;
	}
  

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (data[3]==NLPID_IPV6) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->protocol=htons(ETH_P_IPV6);
		skb->dev=&pvc->netdev;
		netif_rx(skb);
		return;
	}
#endif

	if (data[3]==FR_PAD && data[4]==NLPID_SNAP && data[5]==FR_PAD &&
	    data[6]==FR_PAD && data[7]==FR_PAD &&
	    ((data[8]<<8) | data[9]) == ETH_P_ARP) {
		skb_pull(skb, 10);
		skb->protocol=htons(ETH_P_ARP);
		skb->dev=&pvc->netdev;
		netif_rx(skb);
		return;
	}
  
	printk(KERN_INFO "%s: Unusupported protocol %x\n",
	       hdlc->name, data[3]);
	dev_kfree_skb(skb);
	return;

 rx_error:
	hdlc->stats.rx_errors++; /* Mark error */
	dev_kfree_skb(skb);
}



static void fr_cisco_open(hdlc_device *hdlc)
{
	hdlc->lmi.state = LINK_STATE_CHANGED;
	hdlc->lmi.txseq = hdlc->lmi.rxseq = 0;
	hdlc->lmi.last_errors = 0xFFFFFFFF;
	hdlc->lmi.N391cnt = 0;

	if (mode_is(hdlc, MODE_CISCO))
		hdlc_to_dev(hdlc)->hard_header=cisco_hard_header;
	else
		hdlc_to_dev(hdlc)->hard_header=fr_hard_header;

	init_timer(&hdlc->timer);
	hdlc->timer.expires = jiffies + HZ; /* First poll after 1 second */
	hdlc->timer.function = mode_is(hdlc, MODE_FR) ? fr_timer : cisco_timer;
	hdlc->timer.data = (unsigned long)hdlc;
	add_timer(&hdlc->timer);
}



static void fr_cisco_close(hdlc_device *hdlc)
{
	pvc_device *pvc=hdlc->first_pvc;

	del_timer(&hdlc->timer);
  
	while(pvc)		/* NULL in Cisco mode */ {
		dev_close(&pvc->netdev); /* Shutdown all PVCs for this FRAD */
		pvc=pvc->next;
	}
}



/******************************************************************
 *
 *     generic HDLC routines
 *
 *****************************************************************/



static int hdlc_change_mtu(struct device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > HDLC_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}



/********************************************************
 *
 * PVC device routines
 *
 *******************************************************/

static int pvc_open(struct device *dev)
{
	pvc_device *pvc=dev_to_pvc(dev);
	int result=0;

	if ((hdlc_to_dev(pvc->master)->flags & IFF_UP) == 0)
		return -EIO;  /* Master must be UP in order to activate PVC */
  
	memset(&(pvc->stats), 0, sizeof(struct net_device_stats));
	pvc->state=0;

	if (!mode_is(pvc->master, MODE_SOFT) && pvc->master->open_pvc)
		result=pvc->master->open_pvc(pvc);
	if (result)
		return result;

	pvc->master->lmi.state |= LINK_STATE_CHANGED;
	return 0;
}



static int pvc_close(struct device *dev)
{
	pvc_device *pvc=dev_to_pvc(dev);
	pvc->state=0;

	if (!mode_is(pvc->master, MODE_SOFT) && pvc->master->close_pvc)
		pvc->master->close_pvc(pvc);

	pvc->master->lmi.state |= LINK_STATE_CHANGED;
	return 0;
}



static int pvc_xmit(struct sk_buff *skb, struct device *dev)
{
	pvc_device *pvc=dev_to_pvc(dev);

	skb->nh.raw = skb->data;
	if (pvc->state & PVC_STATE_ACTIVE) {
		skb->dev = hdlc_to_dev(pvc->master);
		pvc->stats.tx_bytes+=skb->len;
		pvc->stats.tx_packets++;
		dev_queue_xmit(skb);
	} else {
		pvc->stats.tx_dropped++;
		dev_kfree_skb(skb);
	}
  
	return 0;
}



static struct net_device_stats *pvc_get_stats(struct device *dev)
{
	pvc_device *pvc=dev_to_pvc(dev);
	return &pvc->stats;
}



static int pvc_change_mtu(struct device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > PVC_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}



static void destroy_pvc_list(hdlc_device *hdlc)
{
	pvc_device *pvc=hdlc->first_pvc;
	while(pvc) {
		pvc_device *next=pvc->next;
		unregister_netdevice(&pvc->netdev);
		kfree(pvc);
		pvc=next;
	}

	hdlc->first_pvc=NULL;	/* All PVCs destroyed */
	hdlc->pvc_count=0;
	hdlc->lmi.state |= LINK_STATE_CHANGED;
}



/********************************************************
 *
 * HDLC device routines
 *
 *******************************************************/

static int hdlc_open(struct device *dev)
{
	hdlc_device *hdlc=dev_to_hdlc(dev);
	int result;

	if (hdlc->mode==MODE_NONE)
		return -ENOSYS;

	memset(&(hdlc->stats), 0, sizeof(struct net_device_stats));

	if (mode_is(hdlc, MODE_FR | MODE_SOFT) ||
	    mode_is(hdlc, MODE_CISCO | MODE_SOFT))
		fr_cisco_open(hdlc);

	else if (mode_is(hdlc, MODE_PPP | MODE_SOFT)) {
		sppp_attach(&hdlc->pppdev);
		/* sppp_attach nukes them. We don't need syncppp's ioctl */
		/* Anyway, I'm going to replace it with ppp_synctty.c */
		dev->do_ioctl = hdlc_ioctl;
		hdlc->pppdev.sppp.pp_flags&=~PP_CISCO;
		dev->type=ARPHRD_PPP;
		result = sppp_open(dev);
		if (result) {
			sppp_detach(dev);
			return result;
		}
	}

	result=hdlc->open(hdlc);
	if (result) {
		if (mode_is(hdlc, MODE_FR | MODE_SOFT) ||
		    mode_is(hdlc, MODE_CISCO | MODE_SOFT))
			fr_cisco_close(hdlc);

		else if (mode_is(hdlc, MODE_PPP | MODE_SOFT)) {
			sppp_close(dev);
			sppp_detach(dev);
			dev->rebuild_header=NULL;
			dev->change_mtu=hdlc_change_mtu;
			dev->mtu=HDLC_MAX_MTU;
			dev->hard_header_len=16;
		}

	}

	return result;
}



static int hdlc_close(struct device *dev)
{
	hdlc_device *hdlc=dev_to_hdlc(dev);

	hdlc->close(hdlc);

	if (mode_is(hdlc, MODE_FR | MODE_SOFT) ||
	    mode_is(hdlc, MODE_CISCO | MODE_SOFT))
		fr_cisco_close(hdlc);

	else if (mode_is(hdlc, MODE_PPP | MODE_SOFT)) {
		sppp_close(dev);
		sppp_detach(dev);
		dev->rebuild_header=NULL;
		dev->change_mtu=hdlc_change_mtu;
		dev->mtu=HDLC_MAX_MTU;
		dev->hard_header_len=16;
	}

	return 0;
}



void hdlc_netif_rx(hdlc_device *hdlc, struct sk_buff *skb, int dlci)
{
	skb->mac.raw=skb->data;
  
	if (mode_is(hdlc, MODE_SOFT)) {
		if (mode_is(hdlc, MODE_FR)) {
			fr_netif(hdlc, skb);
			return;
		} else if (mode_is(hdlc, MODE_CISCO)) {
			cisco_netif(hdlc, skb);
			return;
		} else if (mode_is(hdlc, MODE_PPP)) {
			hdlc->stats.rx_bytes+=skb->len;
			hdlc->stats.rx_packets++;
			skb->protocol=htons(ETH_P_WAN_PPP);
			skb->dev=hdlc_to_dev(hdlc);
			netif_rx(skb);
			return;
		}
	} else {		/* protocol support in hardware/firmware */
		hdlc->stats.rx_bytes+=skb->len;
		hdlc->stats.rx_packets++;

		if (mode_is(hdlc, MODE_HDLC))
			skb->protocol=htons(ETH_P_IP);
		/* otherwise protocol set by hw driver */
		
		if (mode_is(hdlc, MODE_FR)) {
			pvc_device *pvc=find_pvc(hdlc, dlci);
			if (!pvc) { /* packet from nonexistent PVC */
				hdlc->stats.rx_errors++;
				dev_kfree_skb(skb);
			}
      
			pvc->stats.rx_bytes+=skb->len;
			pvc->stats.rx_packets++;
			skb->dev=&pvc->netdev;
		} else
			skb->dev=hdlc_to_dev(hdlc);

		netif_rx(skb);
		return;
	}
    
	hdlc->stats.rx_errors++; /* unsupported mode */
	dev_kfree_skb(skb);
}



static struct net_device_stats *hdlc_get_stats(struct device *dev)
{
	return &dev_to_hdlc(dev)->stats;
}



static int hdlc_set_mode(hdlc_device *hdlc, int mode)
{
	int result=-1;		/* Default to soft modes */

	if(!capable(CAP_NET_ADMIN))
		return -EPERM;

	if(hdlc_to_dev(hdlc)->flags & IFF_UP)
		return -EBUSY;

	hdlc_to_dev(hdlc)->addr_len=0;
	hdlc->mode=MODE_NONE;
  
	if (!(mode & MODE_SOFT))
		switch(mode) {
		case MODE_HDLC:
			result = hdlc->set_mode ?
				hdlc->set_mode(hdlc, MODE_HDLC) : 0;
			break;

		case MODE_X25:	/* By card */
		case MODE_CISCO:
		case MODE_PPP:
		case MODE_FR_ANSI:
		case MODE_FR_CCITT:
		case MODE_FR_ANSI  | MODE_DCE:
		case MODE_FR_CCITT | MODE_DCE:
			result = hdlc->set_mode ?
				hdlc->set_mode(hdlc, mode) : -ENOSYS;
			break;
	
		default:
			return -EINVAL;
		}

	if (result) {
		mode |= MODE_SOFT; /* Try "host software" protocol */

		switch(mode & ~MODE_SOFT) {
		case MODE_CISCO:
		case MODE_PPP:
			break;

		case MODE_FR_ANSI:
		case MODE_FR_CCITT:
		case MODE_FR_ANSI  | MODE_DCE:
		case MODE_FR_CCITT | MODE_DCE:
			hdlc_to_dev(hdlc)->addr_len=2;
			*(u16*)hdlc_to_dev(hdlc)->dev_addr=htons(LMI_DLCI);
			dlci_to_q922(hdlc_to_dev(hdlc)->broadcast, LMI_DLCI);
			break;

		default:
			return -EINVAL;
		}

		result = hdlc->set_mode ?
			hdlc->set_mode(hdlc, MODE_HDLC) : 0;
	}

	if (result)
		return result;

	hdlc->mode=mode;
	if (mode_is(hdlc, MODE_PPP))
		hdlc_to_dev(hdlc)->type=ARPHRD_PPP;
	if (mode_is(hdlc, MODE_X25))
		hdlc_to_dev(hdlc)->type=ARPHRD_X25;
	else if (mode_is(hdlc, MODE_FR))
		hdlc_to_dev(hdlc)->type=ARPHRD_FRAD;
	else			/* Conflict - raw HDLC and Cisco */
		hdlc_to_dev(hdlc)->type=ARPHRD_HDLC;
  
	memset(&(hdlc->stats), 0, sizeof(struct net_device_stats));
	destroy_pvc_list(hdlc);
	return 0;
}



static int hdlc_fr_pvc(hdlc_device *hdlc, int dlci)
{
	pvc_device **pvc_p=&hdlc->first_pvc;
	pvc_device *pvc;
	int result, create=1;	/* Create or delete PVC */

	if(!capable(CAP_NET_ADMIN))
		return -EPERM;

	if(dlci<0) {
		dlci=-dlci;
		create=0;
	}

	if(dlci<=0 || dlci>=1024)
		return -EINVAL;	/* Only 10 bits for DLCI, DLCI=0 is reserved */

	if(!mode_is(hdlc, MODE_FR))
		return -EINVAL;	/* Only meaningfull on FR */
    
	while(*pvc_p) {
		if (netdev_dlci(&(*pvc_p)->netdev)==dlci)
			break;
		pvc_p=&(*pvc_p)->next;
	}

	if (create) {		/* Create PVC */
		if (*pvc_p!=NULL)
			return -EEXIST;
    
		*pvc_p=kmalloc(sizeof(pvc_device), GFP_KERNEL);
		pvc=*pvc_p;
		memset(pvc, 0, sizeof(pvc_device));
    
		pvc->netdev.name=pvc->name;
		pvc->netdev.hard_start_xmit=pvc_xmit;
		pvc->netdev.get_stats=pvc_get_stats;
		pvc->netdev.open=pvc_open;
		pvc->netdev.stop=pvc_close;
		pvc->netdev.change_mtu=pvc_change_mtu;
		pvc->netdev.mtu=PVC_MAX_MTU;
  
		pvc->netdev.type=ARPHRD_DLCI;
		pvc->netdev.hard_header_len=16;
		pvc->netdev.hard_header=fr_hard_header;
		pvc->netdev.tx_queue_len=0;
		pvc->netdev.flags=IFF_POINTOPOINT;
  
		dev_init_buffers(&pvc->netdev);

		pvc->master=hdlc;
		*(u16*)pvc->netdev.dev_addr=htons(dlci);
		dlci_to_q922(pvc->netdev.broadcast, dlci);
		pvc->netdev.addr_len=2;	/* 16 bits is enough */
		pvc->netdev.irq=hdlc_to_dev(hdlc)->irq;

		result=dev_alloc_name(&pvc->netdev, "pvc%d");
		if (result<0) {
			kfree(pvc);
			*pvc_p=NULL;
			return result;
		}

		if (register_netdevice(&pvc->netdev)!=0) {
			kfree(pvc);
			*pvc_p=NULL;
			return -EIO;
		}

		if (!mode_is(hdlc, MODE_SOFT) && hdlc->create_pvc) {
			result=hdlc->create_pvc(pvc);
			if (result) {
				unregister_netdevice(&pvc->netdev);
				kfree(pvc);
				*pvc_p=NULL;
				return result;
			}
		}

		hdlc->lmi.state |= LINK_STATE_CHANGED;
		hdlc->pvc_count++;
		return 0;
	}

	if (*pvc_p==NULL)		/* Delete PVC */
		return -ENOENT;
  
	pvc=*pvc_p;
  
	if (pvc->netdev.flags & IFF_UP)
		return -EBUSY;		/* PVC in use */
  
	if (!mode_is(hdlc, MODE_SOFT) && hdlc->destroy_pvc)
		hdlc->destroy_pvc(pvc);

	hdlc->lmi.state |= LINK_STATE_CHANGED;
	hdlc->pvc_count--;
	*pvc_p=pvc->next;
	unregister_netdevice(&pvc->netdev);
	kfree(pvc);
	return 0;
}



static int hdlc_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
	hdlc_device *hdlc=dev_to_hdlc(dev);
  
	switch(cmd) {
	case HDLCSETMODE:
		return hdlc_set_mode(hdlc, ifr->ifr_ifru.ifru_ivalue);
      
	case HDLCPVC:
		return hdlc_fr_pvc(hdlc, ifr->ifr_ifru.ifru_ivalue);
      
	default:
		if (hdlc->ioctl!=NULL)
			return hdlc->ioctl(hdlc, ifr, cmd);
	}

	return -EINVAL;
}



static int hdlc_init(struct device *dev)
{
	hdlc_device *hdlc=dev_to_hdlc(dev);

	memset(&(hdlc->stats), 0, sizeof(struct net_device_stats));

	dev->get_stats=hdlc_get_stats;
	dev->open=hdlc_open;
	dev->stop=hdlc_close;
	dev->do_ioctl=hdlc_ioctl;
	dev->change_mtu=hdlc_change_mtu;
	dev->mtu=HDLC_MAX_MTU;

	dev->type=ARPHRD_HDLC;
	dev->hard_header_len=16;
  
	dev->flags=IFF_POINTOPOINT | IFF_NOARP;

	dev_init_buffers(dev);
	return 0;
}



int register_hdlc_device(hdlc_device *hdlc)
{
	int result;

#ifndef MODULE
	if (!version_printed) {
		printk(KERN_INFO "%s\n", version);
		version_printed = 1;
	}
#endif

	hdlc_to_dev(hdlc)->name = hdlc->name;
	hdlc_to_dev(hdlc)->init = hdlc_init;
	hdlc_to_dev(hdlc)->priv = &hdlc->syncppp_ptr; /* remove in 2.3 */
	hdlc->syncppp_ptr = &hdlc->pppdev;
	hdlc->pppdev.dev=hdlc_to_dev(hdlc);
	hdlc->mode = MODE_NONE;
	hdlc->lmi.T391 = 10;	/* polling verification timer */
	hdlc->lmi.T392 = 15;	/* link integrity verification polling timer */
	hdlc->lmi.N391 = 6;	/* full status polling counter */
	hdlc->lmi.N392 = 3;	/* error threshold */
	hdlc->lmi.N393 = 4;	/* monitored events count */

	result=dev_alloc_name(hdlc_to_dev(hdlc), "hdlc%d");
	if (result<0)
		return result;
  
	if (register_netdevice(hdlc_to_dev(hdlc))!=0) {
		hdlc_to_dev(hdlc)->name=NULL; /* non-NULL means registered */
		return -EIO;
	}

	dev_init_buffers(hdlc_to_dev(hdlc));
	MOD_INC_USE_COUNT;
	return 0;
}



void unregister_hdlc_device(hdlc_device *hdlc)
{
	if (hdlc_to_dev(hdlc)->name==NULL)
		return;		/* device not registered */

	destroy_pvc_list(hdlc);
	unregister_netdevice(hdlc_to_dev(hdlc));
	MOD_DEC_USE_COUNT;
}



#ifdef MODULE

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("HDLC support module");

int init_module(void)
{
	printk(KERN_INFO "%s\n", version);
	return 0;
}



void cleanup_module(void)
{
}

#endif
