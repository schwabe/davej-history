/* wanxl.c
 * -*- linux-c -*-
 * 
 *  	(C) 1999 RG Studio s.c., http://www.rgstudio.com.pl/
 *	Written by Krzysztof Halasa <khc@rgstudio.com.pl>
 *
 *  	Portions (C) SBE Inc., used by permission.
 *
 *	Sources:
 *		wanXL technical reference manuals
 *		wanXL UNIXware X.25 driver
 *		Donald Becker's skeleton.c driver
 *		"Linux Kernel Module Programming" by Ori Pomerantz
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_frad.h>

#include <linux/pci.h>

#include "syncppp.h"
#include <linux/hdlc.h>
#include "plx9060.h"
#include "wanxl.h"

static const char* version = "SBE wanXL driver revision: 1.0-pre9";
static const char* irqdevname = "SBE wanXL";

#ifdef __LITTLE_ENDIAN
#define SWAP_OPTION MBX_CMD_BSWAP_1
#else
#error "big endian untested yet"
#define SWAP_OPTION MBX_CMD_BSWAP_0
#endif


#define WANXL_DEVICE_IDS 3    /* number of possible device IDs we will look
				 for on the PCI bus */

static int SBEdeviceIDs[WANXL_DEVICE_IDS] = { PCI_DEVICE_ID_SBE_WANXL100, 
					      PCI_DEVICE_ID_SBE_WANXL200, 
					      PCI_DEVICE_ID_SBE_WANXL400 };

#define MAX_PUTS_WAIT 20	/* in  seconds */
#define MAX_QUERY_WAIT 1
#define WANXL_HOST_MEM_SIZE	0x10000000
#define WANXL_BUS2HOST_MAP_ADDR 0x00000000

#define PCI_COMM_MAE            0x0002  /* memory access enable */
#define PCI_COMM_ME             0x0004  /* master enable */
#define PCI_CONF_COMM           0x4     /* command register, 2 bytes  */


static sbe_card *first_card=NULL;
static sbe_card **new_card=&first_card;

static const char *cable_types[]={ "undefined0",
				   "undefined1",
				   "V.35",
				   "X.21",
				   "RS232",
				   "EIA530",
				   "no port",
				   "none" };


#define max(x, y) (x > y ? x : y)



static int wanxl_run_board(sbe_card *card, char *data, u32 *length);
#ifdef WANXL_QUERY
static int wanxl_query_port(sbe_port *port, char *data, u32 *length);
#endif

static inline sbe_port* hdlc_to_port(hdlc_device *hdlc)
{
	return (sbe_port*)hdlc;
}



static inline sbe_port* dev_to_port(struct device *dev)
{
	return hdlc_to_port(dev_to_hdlc(dev));
}



static inline char *port_name(sbe_port *port)
{
	return port->hdlc.name;
}



static inline char *card_name(sbe_card *card)
{
	return card->first_port->hdlc.name;
}



static inline int wanxl_card_running(sbe_card *card)
{
	return card->running;
}



static inline void wanxl_send_interrupt(sbe_port *port, int transmit)
{
	if ((transmit && ((port->tx_ring->r_flags & BF_IREQ) == 0)) ||
	    (!transmit && ((port->rx_ring->r_flags & BF_IREQ) == 0)))
		return;		/* No need for interrupt signaling */
  
#ifdef DEBUG_INTR
	printk(KERN_DEBUG "%s: wanxl_send_interrupt (%s, node=%u)\n",
	       port_name(port), transmit ? "TX" : "RX", port->node);
#endif
	if (transmit)
		transmit=TX_IRQ_BIAS;
  
	writel(1<<(transmit+port->node), &port->card->wx_plx_viraddr->dbr_in);
}



static void wanxl_parse_injector(sbe_port *port, void *data, u32 len)
{
	int i;

	printk(KERN_INFO "%s: got injector (len=%X):", port->hdlc.name, len);
  
	for (i=0; i<(len+3)/4; i++)
		printk(" %08X", ((u32*)data)[i]);
	printk("\n");
}



static int wanxl_submit_injector(sbe_port *port, void *packet, u32 size,
				 int wait)
{
#ifdef DEBUG_INJ
	int i;
#endif
	pkt_desc *tx_pkt=port->tx_ring->v_inject;
	pkt_desc *rx_pkt=port->rx_ring->v_inject;
	bio_wrap *bio_tx=tx_pkt->v_data;
	bio_wrap *bio_rx=rx_pkt->v_data;
	u32 cmd;
 
	if (tx_pkt->pd_flags & PD_READY) {
		sleep_on_timeout(&port->tx_inj, HZ*MAX_QUERY_WAIT);
		if (tx_pkt->pd_flags & PD_READY)
			return -EBUSY; /* TX injector still in use */
	}
  
	memcpy(bio_tx, packet, size);
	cmd=bio_tx->type;
	tx_pkt->pd_length=size;
#ifdef DEBUG_INJ
	printk(KERN_DEBUG "%s: Sending injector:", port_name(port));
	for (i=0; i<size/4; i++)
		printk(" %08X", ((u32*)packet)[i]);
	printk("\n");
#endif
	tx_pkt->pd_flags=PD_FLAGS_INJ|PD_READY;
	wanxl_send_interrupt(port, 1);

	if (!wait)
		return 0;

	while(1) {
		sleep_on_timeout(&port->rx_inj, HZ*MAX_QUERY_WAIT);
		if ((rx_pkt->pd_flags & PD_READY)==0) {
			printk(KERN_WARNING "%s: Timeout when waiting for "
			       "injector %X\n", port_name(port), cmd);
			return -EIO;
		}
  
		if (bio_rx->type==cmd && rx_pkt->pd_length==size)
			break;

		wanxl_parse_injector(port, bio_rx, rx_pkt->pd_length);
		rx_pkt->pd_flags=PD_FLAGS_INJ;
		wanxl_send_interrupt(port, 0);
	}

	memcpy(packet, bio_rx, size);
	rx_pkt->pd_flags=PD_FLAGS_INJ;
	wanxl_send_interrupt(port, 0);

	if (bio_rx->status) {
		printk(KERN_WARNING "%s: injector %X failed, status=%X\n",
		       port_name(port), cmd, bio_rx->status);
		return -EIO;
	}

	return 0;
}



static void wanxl_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	sbe_card *card=dev_id;
	sbe_port *port;
	u32 int_mask;
  
	if (!wanxl_card_running(card))
		return;

	int_mask=readl(&card->wx_plx_viraddr->dbr_out);
	writel(int_mask, &card->wx_plx_viraddr->dbr_out); /* Confirm IRQ */

	for (port=card->first_port; port!=NULL; port=port->next_port) {
		pkt_desc *pkt;

		if (port->rx_ring->v_inject->pd_flags & PD_READY)
			if (waitqueue_active(&port->rx_inj))
				wake_up(&port->rx_inj);	/* Got injector response */
 
		if ((port->tx_ring->v_inject->pd_flags & PD_READY) == 0)
			if (waitqueue_active(&port->tx_inj))
				wake_up(&port->tx_inj);	/* Got free injector */
    
		pkt=port->next_rx_pkt;
		while (pkt->pd_flags & PD_READY) {
#ifdef DEBUG_PKT
			int i;
#endif
			if (pkt->pd_flags & PD_ERR)
				port->hdlc.stats.rx_errors++; /* RX error */
			else {
				struct sk_buff *skb=pkt->v_data;
#ifdef DEBUG_PKT
				printk(KERN_DEBUG "%s RX (PCI=%08x, "
				       "proto=%x):", port_name(port),
				       pkt->p_data, pkt->mux.fr.protid);
				for (i=0; i<pkt->pd_length; i++)
					printk(" %02X", skb->data[i]);
				printk("\n");
#endif
				if ((port->mode & MODE_FR) && pkt->mux.fr.protid!=IP_PROTID &&
				    pkt->mux.fr.protid!=ARP_PROTID)
					port->hdlc.stats.rx_errors++; /* Ignore it for now */
				else {
					struct sk_buff *newskb=dev_alloc_skb(HDLC_MAX_MTU);
					if (newskb==NULL)
						port->hdlc.stats.rx_dropped++;
					else {
						skb_put(skb, pkt->pd_length);
						if ((port->mode & MODE_FR) && pkt->mux.fr.protid==ARP_PROTID) {
							skb_pull(skb, 2); /* Remove 2 byte header left by PDM */
							skb->protocol=htons(ETH_P_ARP);
						} else
							skb->protocol=htons(ETH_P_IP);
		
						hdlc_netif_rx(&port->hdlc, skb, pkt->mux.fr.dlci);
						pkt->v_data=newskb;
						pkt->p_data=virt_to_bus(newskb->data);
					}
				}
			}

			pkt->pd_length=0;
			pkt->pd_flags = PD_FLAGS_RX | (pkt->pd_flags & PD_LAST);

			if (pkt->pd_flags & PD_LAST)
				pkt=port->rx_ring->v_ring; /* Rewind */
			else
				pkt++;
		} /* while */

		port->next_rx_pkt=pkt;
		wanxl_send_interrupt(port, 0); /* RX ring serviced */

		pkt=port->next_tx_pkt_done;
		while (pkt->pd_flags & PD_DONE) {
			if (pkt->pd_flags & PD_ERR)
				printk(KERN_WARNING "%s: PD_ERROR set on TX packet\n",
				       port_name(port));
      
			port->hdlc.stats.tx_packets++;
			port->hdlc.stats.tx_bytes+=pkt->pd_length;
      
			dev_kfree_skb(pkt->v_data);
			pkt->pd_flags = PD_FLAGS_TX | (pkt->pd_flags & PD_LAST);
      
			if (pkt->pd_flags & PD_LAST)
				pkt=port->tx_ring->v_ring; /* Rewind */
			else
				pkt++;
		} /* while */
		port->next_tx_pkt_done=pkt;
		wanxl_send_interrupt(port, 1); /* TX ring serviced */
	}
}



static int wanxl_set_mode(hdlc_device *hdlc, int mode)
{
	sbe_port *port=hdlc_to_port(hdlc);

	if (wanxl_card_running(port->card)==0)
		return -ENOSYS;	/* Card not initialized */
  
	if (mode!=MODE_HDLC && mode!=MODE_FR_ANSI && mode!=MODE_FR_CCITT)
		return -ENOSYS;	/* Not supported on this hardware yet */

	port->mode=mode;
	return 0;
}



static int wanxl_create_pvc(pvc_device *pvc)
{
	sbe_port *port=hdlc_to_port(pvc->master);
	u16 dlci=netdev_dlci(&pvc->netdev);
	b_tnioc packet;
	int result;

	if (wanxl_card_running(port->card)==0)
		return -ENOSYS;	/* Card not initialized */

	if (!(hdlc_to_dev(pvc->master)->flags & IFF_UP))
		return -ENOSYS;	/* HDLC port down */

	memset(&packet, 0, sizeof(packet));
	packet.bio.type=B_SETTUNE;
	packet.tune.fr_pvc.dlci=dlci;
	packet.tune.fr_pvc.cir=16000;
	packet.tune.fr_pvc.Bc=16000;
	packet.tune.fr_pvc.Be=2048000;
	packet.tune.fr_pvc.stepcount=8;
	packet.tune.fr_pvc.flowstyle=FECN;
	/* FIXME - must be set ? why ? */
	packet.tune.fr_pvc.protid=IP_PROTID|ARP_PROTID;
	packet.type=FR_PVC_TYPE;
	result = wanxl_submit_injector(port, &packet, sizeof(packet), 0);
	if (result)
		return result;

	return 0;
}



static int wanxl_open_pvc(pvc_device *pvc)
{
	pvc->state |= PVC_STATE_ACTIVE; /* FIXME */
	return 0;
}



static int wanxl_open(hdlc_device *hdlc)
{
	sbe_port *port=hdlc_to_port(hdlc);
	b_tnioc packet;
	int result;

	memset(&packet, 0, sizeof(packet));
	packet.bio.type=B_SETTUNE;
	packet.tune.wan.WAN_baud=2048000;
	packet.tune.wan.WAN_maxframe=HDLC_MAX_MTU;
	packet.type=HDLC_TYPE;
	result=wanxl_submit_injector(port, &packet, sizeof(packet), 1);
	if (result)
		return result;


	memset(&packet.bio, 0, sizeof(packet.bio));
	packet.bio.type=B_OPEN;
	result=wanxl_submit_injector(port, &packet.bio, sizeof(packet.bio), 1);
	if (result)
		return result;


	if (port->mode==MODE_FR_ANSI || port->mode==MODE_FR_CCITT) {
		memset(&packet, 0, sizeof(packet));
		packet.bio.type=B_SETTUNE;
		packet.tune.fr.T391=hdlc->lmi.T391;
		packet.tune.fr.T392=hdlc->lmi.T392;
		packet.tune.fr.N391=hdlc->lmi.N391;
		packet.tune.fr.N392=hdlc->lmi.N392;
		packet.tune.fr.N393=hdlc->lmi.N393;
		packet.tune.fr.maxframesize=HDLC_MAX_MTU;
		packet.tune.fr.accessrate=2048000;
		if (port->mode==MODE_FR_CCITT)
			packet.tune.fr.standard=ITU;
		else
			packet.tune.fr.standard=ANSI;
		packet.tune.fr.conform=NONE;
		packet.type=FR_TYPE;
		result=wanxl_submit_injector(port, &packet, sizeof(packet), 0);
		if (result)
			return result;
	}


	memset(&packet.bio, 0, sizeof(packet.bio));
	packet.bio.type=B_START;
	result=wanxl_submit_injector(port, &packet.bio, sizeof(packet.bio), 1);
	if (result)
		return result;
  
	MOD_INC_USE_COUNT;
	return 0;
}



static void wanxl_close(hdlc_device *hdlc)
{
	sbe_port *port=hdlc_to_port(hdlc);
	bio_wrap packet;

	memset(&packet, 0, sizeof(packet));
	packet.type=B_CLOSE;
	wanxl_submit_injector(port, &packet, sizeof(packet), 1);

	MOD_DEC_USE_COUNT;
}



static int wanxl_xmit(struct sk_buff *skb, struct device *dev)
{
	u16 dlci=0;
	u8 proto=0;
	sbe_port *port=dev_to_port(dev);
	pkt_desc *pkt=port->next_tx_pkt;
#ifdef DEBUG_PKT
	int i;
#endif
  
	if (pkt->pd_flags & (PD_READY|PD_DONE)) {
#ifdef DEBUG_PKT
		printk(KERN_DEBUG "%s: Unable to send packet\n",
		       port_name(port));
#endif
		port->hdlc.stats.tx_dropped++;
		dev_kfree_skb(skb);
		return 0;	/* Out of card buffers - drop the skb */
	}

	if (port->mode & MODE_FR) {
		dlci=q922_to_dlci(skb->data);
		switch(skb->protocol) {
		case __constant_htons(ETH_P_IP):
			proto=IP_PROTID;
			skb_pull(skb, 4);
			break;
	
		case __constant_htons(ETH_P_ARP):
			proto=ARP_PROTID;
			skb_pull(skb, 10);
			break;
	
		default:
			port->hdlc.stats.tx_errors++;
			dev_kfree_skb(skb);
			return 0; /* Ignore packet */
		}
	}

	/* Align buffer at 4-byte boundary */
	if (virt_to_bus(skb->data) % 4) {
		struct sk_buff *new=alloc_skb(skb->len, GFP_ATOMIC);
		if (!new) {
			port->hdlc.stats.tx_dropped++;
			dev_kfree_skb(skb);
			return 0; /* Out of memory - drop packet */
		}

		memcpy(new->data, skb->data, skb->len);
		skb_put(new, skb->len);
		dev_kfree_skb(skb);
		skb=new;
	}

	pkt->v_data=skb;
	pkt->p_data=virt_to_bus(skb->data);
	pkt->pd_length=skb->len;
	pkt->mux.fr.dlci=dlci;
	pkt->mux.fr.protid = (skb->protocol == __constant_htons(ETH_P_ARP) ?
			      ARP_PROTID : IP_PROTID);

	if (pkt->pd_flags & PD_LAST)
		port->next_tx_pkt=port->tx_ring->v_ring; /* Rewind */
	else
		port->next_tx_pkt=pkt+1;
 
#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s TX (PCI=%08x, proto=%x):",
	       dev->name, pkt->p_data, pkt->mux.fr.protid);
	for (i=0; i<skb->len; i++)
		printk(" %02X", skb->data[i]);
	printk("\n");
#endif

	pkt->pd_flags=PD_FLAGS_TX|PD_READY | (pkt->pd_flags&PD_LAST);
	wanxl_send_interrupt(port, 1);
	return 0;
}



static int wanxl_ioctl(hdlc_device *hdlc, struct ifreq *ifr, int cmd)
{
	sbe_port *port=hdlc_to_port(hdlc);
	u32 length;
	int result;

	if(!capable(CAP_NET_ADMIN))
		return -EPERM;
  
	if (ifr->ifr_data==NULL)
		return -EFAULT;
  
	if (copy_from_user(&length, ifr->ifr_data, 4)!=0)
		return -EFAULT;

	if (length > INT_MAX)
		return -EINVAL;

	switch(cmd) {
	case HDLCRUN:
		if (port->card->first_port != port) /* Not initial port */
			result=-EINVAL;
		else if (port->card->rx_ring || port->card->tx_ring)
			result=-EBUSY; /* Already running */
		else
			result=wanxl_run_board(port->card, ifr->ifr_data+4,
					       &length);
		break;
      
#ifdef WANXLQUERY
	case WANXLQUERY:
		if (wanxl_card_running(port->card)==0)
			result=-EIO; /* Board must be run first */
		else
			result=wanxl_query_port(port, ifr->ifr_data+4,
						&length);
		break;
#endif
	default:
		result=-EINVAL;
		length=0;
	}
  
	if (copy_to_user(ifr->ifr_data, &length, 4)!=0)
		return -EFAULT;

	return result;
}




static inline int wanxl_init_hdlc_port(sbe_port *port)
{
	port->hdlc.set_mode=wanxl_set_mode;
	port->hdlc.open=wanxl_open;
	port->hdlc.close=wanxl_close;
	port->hdlc.ioctl=wanxl_ioctl;
	port->hdlc.create_pvc=wanxl_create_pvc;
	port->hdlc.open_pvc=wanxl_open_pvc;
	hdlc_to_dev(&port->hdlc)->hard_start_xmit=wanxl_xmit;
	return register_hdlc_device(&port->hdlc);
}



static void wanxl_destroy_ring(desc_ring **root)
{
	int cnt;
	desc_ring *node;
	pkt_desc *pkts;

	if (*root==NULL)
		return;

	node=*root;
	while(node) {
		pkts=node->v_ring;
		if (pkts)
			for (cnt=0; cnt<node->ndesc; cnt++) {
				if (pkts->v_data)
					dev_kfree_skb(pkts->v_data);
				pkts++;
			}
		node=node->next_node ? bus_to_virt(node->next_node) : NULL;
	}

	if ((*root)->v_inject) {
		if ((*root)->v_inject->v_data)
			/* Delete injector buffers */
			kfree((*root)->v_inject->v_data);
    
		kfree((*root)->v_inject); /* Delete pkts descriptors */
	}


	kfree(*root);		/* Delete ring descriptors */
	*root=NULL;
}



static int wanxl_create_ring(desc_ring **root, int transmit, int ports)
{
	int node, cnt;
	desc_ring *ring;
	pkt_desc *pkts;
	char *inj;
	int descs=transmit ? WANXL_TX_BUFFERS : WANXL_RX_BUFFERS;

	u32 ring_size=sizeof(desc_ring) * ports;
	u32 pkts_size=sizeof(pkt_desc) * (descs+1) * ports;
	u32 inj_size=MAX_IOCTL_PACKET * ports;
  
	ring=kmalloc(ring_size, GFP_KERNEL);
	if (ring==NULL)
		return -ENOBUFS;
	memset(ring, 0, ring_size);

	pkts=kmalloc(pkts_size, GFP_KERNEL);
	if (pkts==NULL) {
		kfree(ring);
		return -ENOBUFS;
	}
	memset(pkts, 0, pkts_size);

	inj=kmalloc(inj_size, GFP_KERNEL);
	if (inj==NULL) {
		kfree(ring);
		kfree(pkts);
		return -ENOBUFS;
	}
	memset(inj, 0, inj_size);

	*root=ring;

	for (node=0; node<ports; node++) {
		ring->r_flags = HF_IREQ | HF_ACTIVE | node;
		ring->ndesc=descs;
		ring->pdm_offset=0;
		ring->cpu=0;
		ring->next_cpu=NULL;
		ring->next_node = (node==ports-1 ? 0 : virt_to_bus(ring+1));

		ring->p_inject=virt_to_bus(pkts); /* Injector */
		ring->v_inject=pkts;
		pkts->pd_flags=PD_FLAGS_INJ;
		pkts->p_data=virt_to_bus(inj);
		pkts->v_data=inj;
		pkts->pd_max_len=MAX_IOCTL_PACKET;
		pkts++;
		inj+=MAX_IOCTL_PACKET;
    
		ring->p_ring=virt_to_bus(pkts); /* Buffers */
		ring->v_ring=pkts; /* for host usage only */
		for (cnt=0; cnt<descs; cnt++) {
			if (transmit)
				pkts->pd_flags=PD_FLAGS_TX;
			else {
				struct sk_buff *skb=dev_alloc_skb(HDLC_MAX_MTU);
				if (skb==NULL) {
					wanxl_destroy_ring(root);
					return -ENOBUFS;
				}
	
				pkts->pd_flags=PD_FLAGS_RX;
				pkts->p_data=virt_to_bus(skb->data);
				pkts->v_data=skb;
			}

			if (cnt==descs-1)
				pkts->pd_flags|=PD_LAST;
			pkts->pd_max_len=HDLC_MAX_MTU;
			pkts++;
		}
		ring++;
	}

	return 0;
}



static inline int wanxl_wait_on_box(volatile u32 *box_addr,
				    u32 *store_value, /* store value here */
				    u32 break_mask, /* breaks */
				    u32 equal, /* equal breaks */
				    long ms) /* ms delay count */
{
	u32 result;
	unsigned long timeout=jiffies+max(2, ms * HZ / 1000);
	do {
		result=readl(box_addr);

		if ((result & break_mask) || (result==equal))
			break;

		schedule();
	}while(jiffies<timeout);
  
	if (store_value!=NULL)
		*store_value=result;

	if ((result & break_mask) || (result==equal))
		return 0;

	return -1;
}




/*****************************************************************************
 * wx_setmbx() -- board setup, set server's hardware mailbox
 *
 * Configuration of certain server parameters is accomplished via writing to
 * that board's mailbox.  This routine writes into the board's Mailbox
 * Command register, then polls for completion.  Completion is signaled by
 * clearing of the Mailbox Command register by on/board code.  Should the
 * clearing fail to occur or take excessive time, the polling will time-out.
 *
 * The delay count parameter, wcnt, should specifiy the maximum number
 * of milliseconds (ms) to wait.  There are 1000 milliseconds per second.
 *****************************************************************************/

static int wanxl_setmbx(sbe_card *card,
			u32 cmd, /* Mailbox Command */
			long wcnt) /* ms delay */
{
	u32 result;
	writel(cmd, &card->wx_plx_viraddr->mbox_1);
  
	if (wanxl_wait_on_box(&card->wx_plx_viraddr->mbox_1, &result,
			      0, 0x0, wcnt)!=0) {
		printk(KERN_WARNING "%s: wanxl_setmbx: timeout processing "
		       "command 0x%x, result=0x%x\n",
		       card_name(card), cmd, result);
		return -ENODEV;
	}

	return 0;
}



/* issue a reset to the board */
static int wanxl_reset_board(sbe_card *card, int wait)
{
	u32 result;
	unsigned long timeout;

	printk(KERN_DEBUG "%s: resetting board\n", card_name(card));
	writel(MBX_STS_PCIRESET, &card->wx_plx_viraddr->mbox_0);
  
	result=readl(&card->wx_plx_viraddr->control) | CTL_RESET;
	writel(result, &card->wx_plx_viraddr->control);
  
	timeout = jiffies + max(2, HZ / 500);
	do
		schedule();
	while(jiffies<timeout);
      
	result=readl(&card->wx_plx_viraddr->control) & ~CTL_RESET;
	writel(result, &card->wx_plx_viraddr->control);

	if (!wait)
		return 0;

	/* wait for successful completion of self-tests */
	timeout = jiffies + MAX_PUTS_WAIT * HZ;
	do {
		result=readl(&card->wx_plx_viraddr->mbox_0);
		if (result) {
			if (result & MBX_STS_ERROR) {
				printk(KERN_WARNING "%s: self-test failed, "
				       "status=0x%x.\n",
				       card_name(card), result);
				return -ENODEV;
			}
			schedule();
		} else
			break;
	}while(jiffies<timeout);
  
	if (result) {
		printk(KERN_WARNING "%s: self-test failed to complete\n",
		       card_name(card));
		return -ENODEV;
	}

	printk(KERN_DEBUG "%s: self-test done.\n", card_name(card));
	return 0;
}



#ifdef WANXL_QUERY
static int wanxl_query_port(sbe_port *port, char *data, u32 *length)
{
	int result;
	char packet[MAX_IOCTL_PACKET];

	if (wanxl_card_running(port->card)==0)
		return -ENOSYS;		/* Card not initialized */

	if (copy_from_user(packet, data, *length))
		return -EFAULT;

	result=wanxl_submit_injector(port, packet, *length, 1);
	if (result)
		return result;

	if (copy_to_user(data, packet, *length))
		return -EFAULT;

	return 0;
}
#endif



static void wanxl_destroy_card(sbe_card *card, int destroy_all)
{
	sbe_port *port=card->first_port;

	wanxl_reset_board(card, 0);

	if (!destroy_all && port) {
		port=port->next_port;	/* Omit first port */
		card->first_port->next_port=NULL;
	}

	while (port) {
		sbe_port *ptr=port;
    
		unregister_hdlc_device(&port->hdlc);
		port=port->next_port;
		kfree(ptr);
	}

	if (card->rx_ring)
		wanxl_destroy_ring(&card->rx_ring);
  
	if (card->tx_ring)
		wanxl_destroy_ring(&card->tx_ring);

	if (destroy_all) {
		if (card->irq)
			free_irq(card->irq, card);
    
		if (card->wx_plx_viraddr)
			iounmap(card->wx_plx_viraddr);
		if (card->wx_mem_viraddr)
			iounmap(card->wx_mem_viraddr);
		kfree(card);
	}
}



static int wanxl_run_board(sbe_card *card, char *data, u32 *length)
{
	sbe_port *port=card->first_port, **prev_port;
	desc_ring *tx_ring, *rx_ring;
	u32 len=*length;
	int result, cnt;
	u32 bdsize;
	ulong mt;
	char *ms;

	if (len>MAX_PDM_LEN || len<=sizeof(PCI360Header) || len%4) {
		printk(KERN_WARNING "%s: Invalid firmware length\n",
		       port_name(port));
		return -EFAULT;
	}
  
  
	/* reset board and wait for PUTS to complete */
	result=wanxl_reset_board(card, 1);
	if (result)
		return result;
  
	/* get on-board memory size */
	bdsize = readl(&card->wx_plx_viraddr->mbox_2) & MBX_MEMSZ_MASK;

	/* sanity check the board's reported memory size */
	if (bdsize < MBX_MEMSZ_1MB || bdsize > MBX_MEMSZ_4MB) {
		printk(KERN_WARNING "%s: illegal board memory size 0x%x",
		       port_name(port), bdsize);
		return -ENODEV;
	}

	/* now, calculate the timeout value that will be added to a delay later
	   this is to allow time for clearing of on-board memory */
	switch (bdsize) {
	case MBX_MEMSZ_1MB: ms = "1MB"; mt = 2800; break;
	case MBX_MEMSZ_2MB: ms = "2MB"; mt = 3600; break;
	case MBX_MEMSZ_4MB: ms = "4MB"; mt = 5200; break;
	case MBX_MEMSZ_8MB: ms = "8MB"; mt = 8400; break;
	default: ms = "(size?)"; mt = 3600;
	}
	printk(KERN_INFO "%s: board memory size 0x%x (%cMB)\n",
	       port_name(port),
	       bdsize, *ms);
  
	/*
	 * The processing of the BSWAP command is estimated at 50ms (board),
	 * there we set timeout value to 2 seconds, and add in the 0.8
	 * seconds per MB of on/board memory.
	 */

	result=wanxl_setmbx(card, SWAP_OPTION, mt);
	if (result) {
		printk(KERN_WARNING "%s: unable to set byte-swap option, "
		       "aborting.\n", port_name(port));
		return result;
	}

	/*
	 * The processing of the HBA and HMS Mailbox Commands is estimated at
	 * 0.5 seconds each (wanXL), and we select 1 second for the timeout
	 * value.
	 *
	 * Host Memory Base Address and Host Memory Size setup are Host
	 * dependent.  HMS should be set first to avoid PUTS Warning msg.
	 */

	result=wanxl_setmbx(card, MBX_CMD_SETHMS+(WANXL_HOST_MEM_SIZE>>8),
			    1000);
	if (result) {
		printk(KERN_WARNING "%s: mbx SETHMS command failure\n",
		       port_name(port));
		return result;
	}
  
	result=wanxl_setmbx(card, MBX_CMD_SETHBA|(WANXL_BUS2HOST_MAP_ADDR>>8),
			    1000);
	if (result) {
		printk(KERN_WARNING "%s: mbx SETHBA command failure\n",
		       port_name(port));
		return result;
	}
  
	if (copy_from_user((char*)card->wx_mem_viraddr_order+PDM_OFFSET,
			   data+sizeof(PCI360Header),
			   len-sizeof(PCI360Header)))
		return -EFAULT;
  
	writel(PDM_OFFSET, card->wx_mem_viraddr);

	writel(0, &card->wx_plx_viraddr->mbox_5);
	writel(0, &card->wx_plx_viraddr->mbox_6);
	writel(0, &card->wx_plx_viraddr->mbox_7);

	result=wanxl_setmbx(card, MBX_CMD_ABORTJ, 1000);
	if (result) {
		printk(KERN_WARNING "%s: unable to send AbortAndJump "
		       "command\n", port_name(port));
		return result;
	}

	if (wanxl_wait_on_box(&card->wx_plx_viraddr->mbox_5, &result,
			      0xFFFFFFFF, 0xFFFFFFFF, 1000)!=0) {
		printk(KERN_WARNING "%s: board never became ready after "
		       "AbortAndJump command\n", port_name(port));
		return -EFAULT;
	}
  
	card->config=(board_cfg*)(result+card->wx_mem_viraddr);
  
	printk(KERN_INFO "%s: serial number %d\n", port_name(port),
	       card->config->serial_num);
	printk(KERN_INFO "%s: %d port%s, RAM=0x%x len=0x%x\n", port_name(port),
	       card->config->num_ports, card->config->num_ports>1 ? "s" : "",
	       card->config->mem_base, card->config->mem_size);
	printk(KERN_DEBUG "%s: firmware version: %s\n", port_name(port),
	       card->wx_mem_viraddr_order+card->config->fw_version);
	printk(KERN_DEBUG "%s: PDM version: %s\n", port_name(port),
	       card->wx_mem_viraddr_order+card->config->pdm_version);
	printk(KERN_DEBUG "%s: PDM compiled: %s\n", port_name(port),
	       card->wx_mem_viraddr_order+card->config->pdm_compiled);

	if (card->config->num_ports > MAX_PORTS_PER_CARD) {
		card->config->num_ports = MAX_PORTS_PER_CARD;
		printk(KERN_WARNING "%s: Using only %i ports\n", port_name(port),
		       MAX_PORTS_PER_CARD);
	}
  
	if (wanxl_create_ring(&card->rx_ring, 0, MAX_PORTS_PER_CARD)!=0)
		return -EFAULT;
  
	if (wanxl_create_ring(&card->tx_ring, 1, MAX_PORTS_PER_CARD)!=0) {
		wanxl_destroy_ring(&card->rx_ring);
		return -EFAULT;
	}

	card->config->rx_ring_head=virt_to_bus(card->rx_ring);
	card->config->tx_ring_head=virt_to_bus(card->tx_ring);
	card->config->rx_max_pkts=10;
	card->config->tx_max_pkts=10;

	prev_port = &port->next_port;	/* Back pointer to next_port address */
	rx_ring = card->rx_ring;
	tx_ring = card->tx_ring;

	len=0;
	for (cnt=0; cnt<MAX_PORTS_PER_CARD && len<card->config->num_ports;
	     cnt++) {
		if (((card->config->port_info[cnt]&0xE0)>>5) != 6) {
			/* port exist */
			if (len!=0) {
				/* Create next ports */
				port=kmalloc(sizeof(sbe_port), GFP_KERNEL);
				if (port==NULL) {
					printk(KERN_WARNING "%s: Unable to create port structure\n",
					       card_name(card));
					wanxl_destroy_card(card, 0);
				}
				memset(port, 0, sizeof(sbe_port));
      
				hdlc_to_dev(&port->hdlc)->irq=card->irq;
				port->card=card;
				*prev_port=port; /* Connect port to chain */
				prev_port=&port->next_port;

				result=wanxl_init_hdlc_port(port);
				if (result) {
					printk(KERN_WARNING "%s: Unable to register net device\n",
					       port_name(port));
					wanxl_destroy_card(card, 0);
					return result;
				}
			}
    
			port->node=cnt;
			printk(KERN_INFO "%s: cable=%s, PM=%s\n",
			       port_name(port),
			       cable_types[card->config->port_info[cnt]&0x7],
			       cable_types[(card->config->port_info[cnt]&0xE0)>>5]);
			len++;			/* port found */
		}
		port->rx_ring=rx_ring++;
		port->tx_ring=tx_ring++;
		port->next_rx_pkt = port->rx_ring->v_ring;
		port->next_tx_pkt = port->next_tx_pkt_done = port->tx_ring->v_ring;
	}

	writel(HDLC_MAX_MTU, &card->config->max_frame_size);
	writel(HDLC_MAX_MTU, &card->config->avg_msg_size);
	writel(HDLC_MAX_MTU, &card->config->scc_rx_mblk_sz);
	writel(100, &card->config->tx_intr_tmo);

	writel(1, &card->config->valid);
  
	copy_to_user(data, card->config, sizeof(board_cfg));
	*length=sizeof(board_cfg);
	card->running=1;
	return 0;
}



/*****************************************************************
 *
 *  Module initialization and destruction
 *
 ****************************************************************/
  
__initfunc(int wanxl_pci_init(struct pci_dev *pdev))
{
	int result;
	u16 command;
	sbe_card *card;
	sbe_port *port;
	u32 memsize;

	card=kmalloc(sizeof(sbe_card), GFP_KERNEL);
	if (card==NULL)
		return -ENOBUFS;
	memset(card, 0, sizeof(sbe_card));

	port=kmalloc(sizeof(sbe_port), GFP_KERNEL);
	if (port==NULL) {
		kfree(card);
		return -ENOBUFS;
	}
	memset(port, 0, sizeof(sbe_port));

	card->first_port=port;
	port->card=card;
	port->node=0;
	result=wanxl_init_hdlc_port(port);
	if (result) {
		printk(KERN_WARNING "wanXL: Unable to register hdlc device\n");
		kfree(card);
		kfree(port);
		return result;
	}

	pci_read_config_word(pdev, PCI_CONF_COMM, &command);
	if ((command & (PCI_COMM_ME|PCI_COMM_MAE))==0) {
		command|=(PCI_COMM_ME|PCI_COMM_MAE);
		printk(KERN_WARNING "%s: updating PCI config register %d to 0x%x\n",
		       port_name(port), PCI_CONF_COMM, command);
		pci_write_config_word(pdev, PCI_CONF_COMM, command);
	}

	/* set up addresses and mappings */
	card->wx_plx_phyaddr=pdev->base_address[0];
	card->wx_mem_phyaddr=pdev->base_address[2];
	card->wx_plx_viraddr=ioremap_nocache(card->wx_plx_phyaddr,
					     sizeof(plx9060));
  
	memsize = card->wx_plx_viraddr->mbox_2 & MBX_MEMSZ_MASK;
	card->wx_mem_viraddr = ioremap_nocache(card->wx_mem_phyaddr,
					       memsize*2);
	card->wx_mem_viraddr_order = card->wx_mem_viraddr + memsize;
  
	printk(KERN_INFO "%s: plx9060/9080 at 0x%x, RAM at 0x%x, irq %u\n",
	       port_name(port),
	       card->wx_plx_phyaddr, card->wx_mem_phyaddr, pdev->irq);
 
	/* allocate IRQ for the board */

	if (request_irq(pdev->irq, &wanxl_intr, SA_SHIRQ, irqdevname, card)) {
		printk(KERN_WARNING "%s: could not allocate IRQ%d.\n",
		       port_name(port), pdev->irq);
		wanxl_destroy_card(card, 1);
		return(-ENODEV);
	}
	card->irq=hdlc_to_dev(&port->hdlc)->irq=pdev->irq;

	*new_card=card;
	new_card=&card->next_card;

	return 0;
}



__initfunc(int wanxl_init(void))
{
	int i, devs=0, rep=0;
  
	if(!pci_present())
		return -ENODEV;   
  
	for(i=0; i<WANXL_DEVICE_IDS; i++) {
		struct pci_dev *pdev=NULL;
		do {
			pdev=pci_find_device(PCI_VENDOR_ID_SBE,
					     SBEdeviceIDs[i], pdev);
			if (pdev) {
				int result;

				if (!rep) {
					printk(KERN_INFO "%s\n", version);
					rep=1;
				}

				result=wanxl_pci_init(pdev);
				if (result==0)
					devs++;	/* Initialized a card */
				else
					printk(KERN_WARNING "wanXL: Error initializing card, result=%i\n",
					       result);
			}
		}while (pdev!=NULL);
	}

	if(devs==0)
		return -ENODEV;		/* No cards found */

	return 0;
}


#ifdef MODULE

int init_module(void)
{
	return wanxl_init();
}


void cleanup_module(void)
{
	sbe_card *card=first_card;
  
	while (card) {
		sbe_card *ptr=card;
		card=card->next_card;
		wanxl_destroy_card(ptr, 1);
	}
}

#endif /* MODULE */
