/*
 * Copyright 2000 Xpeed, Inc.
 * fr.c $Revision: 1.23 $
 * License to copy and distribute is GNU General Public License, version 2.
 * Some code adapted from Mike McLagan's dlci.c 0.30 from Linux kernels
 * 2.0-2.2.
 */
#define __KERNEL__ 1
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include "xpds.h"
#include "xpds-encap-fr.h"
#include "xpds-softnet.h"

#define XPDS_DLCI_LMI_OFF		0
#define XPDS_DLCI_LMI_LT		1
#define XPDS_DLCI_LMI_NT		2
#define XPDS_DLCI_LMI_NT_BIDIRECTIONAL	3

int xpds_dlci_t391 = 10;
int xpds_dlci_n391 = 6;

#define DEBUG_DLCI	4
extern int xpds_debug_level;

#if DEBUG
#define	dprintk	if (xpds_debug_level & DEBUG_DLCI) printk
#else
#define	dprintk	if (0) printk
#endif

#define nrprintk if (net_ratelimit()) printk

typedef struct {
	u8	dlci_header[2] __attribute__ ((packed));
	u8	protocol_discriminator __attribute__ ((packed));
	u8	call_reference __attribute__ ((packed));
	u8	padding __attribute__ ((packed));
} dlci_status_head_t __attribute__ ((packed));

typedef struct {
	u8	message_type __attribute__ ((packed));
	u8	locking_shift __attribute__ ((packed));
	u8	report_content_id __attribute__ ((packed));
	u8	report_content_length __attribute__ ((packed));
	u8	report_type __attribute__ ((packed));
	u8	liv_element_id __attribute__ ((packed));
	u8	liv_length __attribute__ ((packed));
	u8	liv_send_sequence __attribute__ ((packed));
	u8	liv_receive_sequence __attribute__ ((packed));
} dlci_status_tail_t __attribute__ ((packed));

#define MESSAGE_STATUS_ENQUIRY	0x75
#define MESSAGE_STATUS		0x7d

#define REPORT_FULL_STATUS	0x00
#define REPORT_LIV		0x01
#define REPORT_SINGLE_PVC_STATS	0x02

typedef struct {
	u8	pvc_status_id __attribute__ ((packed));
	u8	length __attribute__ ((packed));
	u8	dlci_info[2] __attribute__ ((packed));
	u8	flags __attribute__ ((packed));
} dlci_pvc_status_t;

#define PVC_FLAGS__NEW		0x08
#define PVC_FLAGS__ACTIVE	0x02

typedef struct dlci_timer_data_t {
	struct net_device	*dev;
	int		dlci_num;
} dlci_timer_data_t;

/*
 * We generate LMI status enquiries in DLCI_LMI_NT mode.
 */
static void
xpds_dlci_lmi_timer (void *p)
{
	dlci_timer_data_t	*data;
	struct net_device		*dev;
	int			card_num;
	struct frad_local	*flp;
	dlci_status_head_t	*status_head;
	dlci_status_tail_t	*status_tail;
	struct sk_buff		*skb;
	short			dlci, lmi_dlci = 0;
	int			i;

	data = (dlci_timer_data_t *) p;

	dev = data->dev;
	card_num = dev - xpds_devs;
	flp = &(xpds_data[card_num].frad_data);

	dprintk (KERN_DEBUG "%s: xpds_dlci_lmi_timer()\n", dev->name);

	if (xpds_data[card_num].dlci_lmi != XPDS_DLCI_LMI_NT &&
		xpds_data[card_num].dlci_lmi != XPDS_DLCI_LMI_NT_BIDIRECTIONAL){
		return;
	}

	if (flp->no_initiate_lmi) return;

	dlci = flp->dlci[data->dlci_num];
	dprintk (KERN_DEBUG "%s: xpds_dlci_lmi_timer(), dev = %p, dlci = %d\n", xpds_devs[card_num].name, dev, dlci);

	if (xpds_if_busy(dev)) {

		skb = alloc_skb (sizeof (dlci_status_head_t) +
			sizeof (dlci_status_tail_t), GFP_ATOMIC);
		if (skb == NULL) {
			printk (KERN_ERR "%s: unable to allocate skb in xlds_dlci_lmi_timer()\n", dev->name);
			return;
		}
		skb->dev = dev;
		skb->len = sizeof (dlci_status_head_t) +
			sizeof (dlci_status_tail_t);
		status_head = (dlci_status_head_t *) skb->data;
		status_tail = (dlci_status_tail_t *) (skb->data +
			sizeof (dlci_status_head_t));
		status_head->dlci_header[0] = ((lmi_dlci >> 4) << 2) |
			xpds_data[card_num].dlci_cr;
		status_head->dlci_header[1] = (lmi_dlci << 4) | 1;
		status_head->protocol_discriminator = 0x03;
		status_head->call_reference = 0x08;
		status_head->padding = 0;

		status_tail->message_type = MESSAGE_STATUS_ENQUIRY;
		status_tail->locking_shift = 0x95;
		status_tail->report_content_id = 0x01;
		status_tail->report_content_length = 0x01;
		status_tail->liv_element_id = 0x03 /* 0x19 */;
		status_tail->liv_length = 0x02;

		status_tail->liv_send_sequence = (flp->liv_send_sequence) ++;
		status_tail->liv_receive_sequence = flp->liv_receive_sequence;
		if (flp->pvc_active[data->dlci_num]) {
			if (flp->remote_liv_receive_sequence >=
				flp->new_liv_send_sequence) {
				flp->pvc_new[data->dlci_num] = 0;
			} else {
				flp->pvc_new[data->dlci_num] = 1;
			}
		} else {
			flp->new_liv_send_sequence =
				status_tail->liv_send_sequence;
		}
		dprintk (KERN_DEBUG "%s: dlci = %d ", xpds_devs[card_num].name, dlci);
		dprintk (KERN_DEBUG "pvc = %d, new = %d\n",
			flp->pvc_active[data->dlci_num], flp->pvc_new[data->dlci_num]);
		dprintk (KERN_DEBUG "%s: liv_send_sequence = %d, liv_receive_sequence = %d\n", xpds_devs[card_num].name, status_tail->liv_send_sequence, status_tail->liv_receive_sequence);
		flp->message_number ++;
		if (flp->message_number >= xpds_dlci_n391) {
			flp->message_number = 0;
		}
		status_tail->report_type =
			(flp->message_number == 0) ?
				REPORT_FULL_STATUS : REPORT_LIV;
		dprintk (KERN_DEBUG "message_number = %d, sending %02x\n",
			flp->message_number, status_tail->report_type);
		dprintk (KERN_DEBUG "%s: sending LMI packet in xpds_dlci_lmi_timer()\n", xpds_devs[card_num].name);
		xpds_tx (skb->data, skb->len, dev);

		dev_kfree_skb (skb);
	}

	i = data->dlci_num;

	init_timer(&(xpds_data[card_num].dlci_lmi_timers[i]));
	xpds_data[card_num].dlci_lmi_timers[i].function = (void *)&xpds_dlci_lmi_timer;
	xpds_data[card_num].dlci_lmi_timers[i].expires = jiffies + xpds_dlci_t391 * HZ;
	xpds_data[card_num].dlci_lmi_timers[i].data = (unsigned long)&(xpds_data[card_num].dlci_lmi_timer_data[i]);
	add_timer(&(xpds_data[card_num].dlci_lmi_timers[i]));

	dprintk(KERN_DEBUG "%s: xpds_dlci_lmi_timer()\n", dev->name);
}

void
xpds_dlci_install_lmi_timer (int i, struct net_device *dev)
{
	int	card_num;

	card_num = dev - xpds_devs;

	dprintk (KERN_DEBUG "%s: xpds_dlci_install_lmi_timer (%d, %p)\n", xpds_devs[card_num].name, i, dev);
	if (i < 0 || i >= CONFIG_DLCI_COUNT) {
		printk (KERN_ERR "%s: invalid DLCI device number %d\n", xpds_devs[card_num].name, i);
		return;
	}
	init_timer(&(xpds_data[card_num].dlci_lmi_timers[i]));
	xpds_data[card_num].dlci_lmi_timers[i].function = (void *)&xpds_dlci_lmi_timer;
	xpds_data[card_num].dlci_lmi_timers[i].expires = jiffies + xpds_dlci_t391 * HZ;
	xpds_data[card_num].dlci_lmi_timer_data[i].dev = dev;
	xpds_data[card_num].dlci_lmi_timer_data[i].dlci_num = i;
	xpds_data[card_num].dlci_lmi_timers[i].data = (unsigned long)&(xpds_data[card_num].dlci_lmi_timer_data[i]);
	add_timer(&(xpds_data[card_num].dlci_lmi_timers[i]));
	dprintk (KERN_DEBUG "%s: xpds_dlci_install_lmi_timer() done\n",
		dev->name);
}

void
xpds_dlci_remove_lmi_timer (int i, struct net_device *dev)
{
	int	card_num;

	card_num = dev - xpds_devs;

	dprintk (KERN_DEBUG "%s: xpds_dlci_remove_lmi_timer (%d, %p)\n",
		dev->name, i, dev);
	if (i < 0 || i >= CONFIG_DLCI_COUNT) {
		printk (KERN_ERR "%s: invalid DLCI device number %d\n", xpds_devs[card_num].name, i);
		return;
	}
	del_timer(&(xpds_data[card_num].dlci_lmi_timers[i]));
	dprintk (KERN_DEBUG "%s: xpds_dlci_remove_lmi_timer() done\n",
		dev->name);
}


static void
xpds_dlci_handle_status (struct sk_buff *skb, struct net_device *dev)
{
	struct frad_local	*flp;
	dlci_status_head_t	*dlci_status_head, *reply_status_head;
	dlci_status_tail_t	*dlci_status_tail, *reply_status_tail;
	dlci_pvc_status_t	*dlci_pvc_status, *reply_pvc_status;
	struct sk_buff		*reply_skb;
	short			dlci, lmi_dlci = 0;
	int			i;
	int			old_pvc_active;
	int			has_pvc_status;
	int			has_padding;
	int			card_num;

	card_num = dev - xpds_devs;

	if (xpds_data[card_num].dlci_lmi == XPDS_DLCI_LMI_OFF) return;

	flp = &(xpds_data[card_num].frad_data);
	skb->dev = dev;

	dlci_status_head = (dlci_status_head_t *)skb->data;
	if (dlci_status_head->padding != 0) {
		/*
		 * no padding
		 */
		has_padding = 0;
	} else {
		/*
		 * one byte of zero padding between call reference
		 * and message type
		 */
		has_padding = 1;
	}
	dlci_status_tail = (dlci_status_tail_t *)
		(skb->data + sizeof (*dlci_status_head) - 1 + has_padding);
	if (skb->len < sizeof (*dlci_status_head) + sizeof (*dlci_status_tail) - 1 + has_padding) {
		dprintk (KERN_ERR "%s: LMI packet of length %lu is too short\n", xpds_devs[card_num].name, (unsigned long) (skb->len));
	}
	dlci_pvc_status = (dlci_pvc_status_t *)
		(skb->data + sizeof (*dlci_status_head) +
		sizeof (*dlci_status_tail) - 1 + has_padding);

	dprintk (KERN_DEBUG "%s: LMI packet received has protocol discriminator = 0x%02x\n", xpds_devs[card_num].name, dlci_status_head->protocol_discriminator);
	dprintk (KERN_DEBUG "%s: LMI packet received has call reference = 0x%02x\n",
		dev->name, dlci_status_head->call_reference);
	if (dlci_status_head->protocol_discriminator != FRAD_I_UI ||
		dlci_status_head->call_reference != FRAD_P_Q933) {
		printk (KERN_NOTICE "%s: dlci_handle_status called with 0x%02x 0x%02x\n",
			dev->name, dlci_status_head->protocol_discriminator,
			dlci_status_head->call_reference);
		return;
	}

	dprintk (KERN_DEBUG "LMI packet received has message type = 0x%02x\n",
		dlci_status_tail->message_type);
	if (dlci_status_tail->message_type != MESSAGE_STATUS_ENQUIRY &&
		dlci_status_tail->message_type != MESSAGE_STATUS) {
		printk (KERN_NOTICE "unknown message type 0x%02x\n",
			dlci_status_tail->message_type);
		return;
	}

	dprintk (KERN_DEBUG "LMI packet received has report type = 0x%02x\n",
		dlci_status_tail->report_type);
	if (dlci_status_tail->report_type != REPORT_FULL_STATUS &&
		dlci_status_tail->report_type != REPORT_LIV) {
		dprintk (KERN_DEBUG "report type is not full status or LIV\n");
		return;
	}

	dprintk (KERN_DEBUG "LMI packet received has receive sequence = %d\n",
		dlci_status_tail->liv_receive_sequence);
	dprintk (KERN_DEBUG "LMI packet received has send sequence = %d\n",
		dlci_status_tail->liv_send_sequence);

	reply_skb = alloc_skb (sizeof (dlci_status_head_t) +
		sizeof (dlci_status_tail_t) + sizeof (dlci_pvc_status_t),
		GFP_ATOMIC);
	if (reply_skb == NULL) {
		printk (KERN_ERR "%s: unable to allocate reply_skb in xpds_dlci_handle_status().\n", dev->name);
		return;
	}
	reply_skb->len = sizeof (dlci_status_head_t) +
		sizeof (dlci_status_tail_t) /* + sizeof (dlci_pvc_status_t) */;
	reply_status_head = (dlci_status_head_t *) reply_skb->data;
	reply_status_tail = (dlci_status_tail_t *) (reply_skb->data +
		sizeof (dlci_status_head_t));
	reply_pvc_status = (dlci_pvc_status_t *) (reply_skb->data +
		sizeof (dlci_status_head_t) + sizeof (dlci_status_tail_t) );

	if (dlci_status_tail->liv_receive_sequence > flp->liv_send_sequence){
		dprintk (KERN_DEBUG "received receive sequence number %d > %d\n",
			dlci_status_tail->liv_receive_sequence,
			flp->liv_send_sequence);
	}
	flp->remote_liv_receive_sequence =
		dlci_status_tail->liv_receive_sequence;
	flp->liv_send_sequence = dlci_status_tail->liv_receive_sequence + 1;

	flp->remote_liv_send_sequence =
		dlci_status_tail->liv_send_sequence;
	flp->liv_receive_sequence = dlci_status_tail->liv_send_sequence;

	if (dlci_status_tail->report_type == REPORT_FULL_STATUS) {
		if (skb->len < sizeof (*dlci_status_head) + sizeof (*dlci_status_tail) - 1 + has_padding + sizeof (*dlci_pvc_status)) {
			if (dlci_status_tail->message_type == MESSAGE_STATUS) {
				printk (KERN_ERR "%s: LMI packet length %lu is too short for report type REPORT_FULL_STATUS\n", xpds_devs[card_num].name, (unsigned long) (skb->len));
			}
			dlci = -1;
			has_pvc_status = 0;
		} else {
			dlci = ((dlci_pvc_status->dlci_info[0] & 0x7f) << 4) |
				((dlci_pvc_status->dlci_info[1] & 0x7f) >> 3);
			has_pvc_status = 1;
		}
	} else {
		dprintk (KERN_DEBUG "%s: dlci_status_tail->report_type = 0x%02x (not REPORT_FULL_STATUS)\n", xpds_devs[card_num].name, dlci_status_tail->report_type);
		dlci = -1;
		has_pvc_status = 0;
	}
	dprintk (KERN_DEBUG "%s: LMI packet received has DLCI = %d, has_pvc_status = %d\n", xpds_devs[card_num].name, dlci, has_pvc_status);

	if (has_pvc_status) {
		for (i = 0; i < CONFIG_DLCI_MAX; i ++) {
			if (dlci == flp->dlci[i]) break;
		}
		if (i >= CONFIG_DLCI_MAX) {
			int	j;

			printk (KERN_ERR "%s: invalid DLCI %d\n",
				dev->name, dlci);

			printk (KERN_ERR "%s: flp->dlci[] = {", xpds_devs[card_num].name);
			for (j = 0; j < CONFIG_DLCI_MAX; j ++) {
				if (j != 0) printk (", ");
				printk ("%d", flp->dlci[j]);
			}
			printk ("}\n");
			dev_kfree_skb (reply_skb);
			return;
		}
	} else {
		i = 0;
	}

	reply_status_head->dlci_header[0] = ((lmi_dlci >> 4) << 2) |
		xpds_data[card_num].dlci_cr;
	reply_status_head->dlci_header[1] = (lmi_dlci << 4) | 1;
	reply_status_head->protocol_discriminator = 0x03;
	reply_status_head->call_reference = 0x08;
	reply_status_head->padding = 0;

	reply_status_tail->message_type = MESSAGE_STATUS;
	reply_status_tail->locking_shift = 0x95;
	reply_status_tail->report_content_id = 0x01;
	reply_status_tail->report_content_length = 0x01;
	flp->message_number ++;
	if (flp->message_number >= xpds_dlci_n391) {
		flp->message_number = 0;
	}
	reply_status_tail->report_type =
		(flp->message_number == 0) ?
			REPORT_FULL_STATUS : REPORT_LIV;
	dprintk (KERN_DEBUG "%s: message_number = %d, sending 0x%02x\n",
		dev->name, flp->message_number, reply_status_tail->report_type);
	reply_status_tail->liv_element_id = 0x03 /* 0x19 */;
	reply_status_tail->liv_length = 0x02;
	reply_status_tail->liv_send_sequence = (flp->liv_send_sequence) ++;
	reply_status_tail->liv_receive_sequence = flp->liv_receive_sequence;

	if (has_pvc_status) {
		reply_pvc_status->pvc_status_id = 0x7;
		reply_pvc_status->length = 3;
		reply_pvc_status->dlci_info[0] = (dlci >> 6) & 0x3f;
		reply_pvc_status->dlci_info[1] = ((dlci << 3) & 0x78) | 0x80;

		dprintk (KERN_DEBUG "dlci_pvc_status->pvc_status_id = 0x%02x\n",
			dlci_pvc_status->pvc_status_id);
		dprintk (KERN_DEBUG "dlci_pvc_status->length = 0x%02x\n",
			dlci_pvc_status->length);
		dprintk (KERN_DEBUG "dlci_pvc_status->dlci_info = 0x%02x 0x%02x\n", dlci_pvc_status->dlci_info[0], dlci_pvc_status->dlci_info[1]);
		dprintk (KERN_DEBUG "dlci_pvc_status->flags = 0x%02x\n",
			dlci_pvc_status->flags);
		old_pvc_active = flp->pvc_active[i];
		flp->pvc_active[i] = (dlci_pvc_status->flags & PVC_FLAGS__ACTIVE) != 0;
		if (! old_pvc_active && flp->pvc_active[i]) {
			printk (KERN_NOTICE "%s: DLCI %d PVC became active\n", xpds_devs[card_num].name, dlci);
		}
		if (old_pvc_active && ! flp->pvc_active[i]) {
			printk (KERN_NOTICE "%s: DLCI %d PVC became inactive\n", xpds_devs[card_num].name, dlci);
		}
		if (flp->pvc_active[i]) {
			if (dlci_status_tail->liv_receive_sequence >=
				flp->new_liv_send_sequence) {
				flp->pvc_new[i] = 0;
			} else {
				flp->pvc_new[i] = 1;
			}
		} else {
			flp->new_liv_send_sequence =
				reply_status_tail->liv_send_sequence;
		}
		dprintk (KERN_DEBUG "dlci = %d\n", dlci);
		dprintk (KERN_DEBUG "pvc = %d, new = %d\n",
			flp->pvc_active[i], flp->pvc_new[i]);
		dprintk (KERN_DEBUG "liv_send_sequence = %d, liv_receive_sequence = %d\n", reply_status_tail->liv_send_sequence, reply_status_tail->liv_receive_sequence);
		reply_pvc_status->flags = 0x80 | (flp->pvc_new[i] << 3) |
			(flp->pvc_active[i] << 1);
	}

	/* ... */

	if (xpds_data[card_num].dlci_lmi == XPDS_DLCI_LMI_LT ||
		xpds_data[card_num].dlci_lmi == XPDS_DLCI_LMI_NT_BIDIRECTIONAL){
		dprintk (KERN_DEBUG "sending LMI packet in xpds_dlci_handle_status()\n");
		xpds_tx (reply_skb->data, reply_skb->len, dev);
		dev_kfree_skb(reply_skb);
	}

	/* This is freed by the caller. */
	/* dev_kfree_skb(skb); */
}

void
xpds_dlci_receive(struct sk_buff *skb, struct net_device *dev)
{
	int card_num;
	struct frhdr *hdr;
	struct frad_local *flp;
	int process, header, actual_header_size;

	dprintk(KERN_DEBUG "xpds_dlci_receive (%p, %p)\n", skb, dev);
	card_num = dev - xpds_devs;
	flp = &(xpds_data[card_num].frad_data);
	hdr = (struct frhdr *) (skb->data);
	process = 0;
	header = sizeof(hdr->addr_control);
	skb->dev = dev;

	actual_header_size = skb->tail - skb->data;
	if (actual_header_size < header + 2) {
		nrprintk (KERN_NOTICE "%s: header is too short (%d bytes)\n",
			dev->name, skb->tail - skb->data);
		dev_kfree_skb(skb);
		xpds_data[card_num].stats.rx_errors++;
		return;
	}

	if (hdr->control != FRAD_I_UI) {
		nrprintk(KERN_NOTICE "%s: Invalid header flag 0x%02X.\n",
		       dev->name, hdr->control);
		xpds_data[card_num].stats.rx_errors++;
	} else {
		switch (hdr->IP_NLPID) {
		case FRAD_P_PADDING:
			if (hdr->NLPID != FRAD_P_SNAP) {
				nrprintk(KERN_NOTICE
				       "%s: Unsupported NLPID 0x%02X.\n",
				       dev->name, hdr->NLPID);
				xpds_data[card_num].stats.rx_errors++;
				break;
			}

			if (actual_header_size < sizeof (struct frhdr)) {
				nrprintk (KERN_NOTICE "%s: header is too short (%d bytes, IP_NLPID == FRAD_P_PADDING)\n", dev->name, skb->tail - skb->data);
				dev_kfree_skb(skb);
				return;
			}

			if (hdr->OUI[0] == FRAD_OUI_BRIDGED_0 &&
			    hdr->OUI[1] == FRAD_OUI_BRIDGED_1 &&
			    hdr->OUI[2] == FRAD_OUI_BRIDGED_2) {
				/* bridged ethernet */

				struct ethhdr *ehdr;

				header = sizeof(struct frhdr);
				ehdr =
				    (struct ethhdr *) (skb->data + header);
				skb->protocol = ntohs(ehdr->h_proto);
				/* skb->mac.raw = (char *)&(ehdr->h_dest); */
				dprintk(KERN_DEBUG
					"hdr->NLPID = FRAD_P_SNAP (%02X), protocol = 0x%04x\n",
					FRAD_P_SNAP, skb->protocol);
				process = 1;
				break;
			}

			if (hdr->OUI[0] + hdr->OUI[1] + hdr->OUI[2] != 0) {
				nrprintk(KERN_NOTICE
				       "%s: Unsupported organizationally unique identifier 0x%02X-%02X-%02X.\n",
				       dev->name, hdr->OUI[0], hdr->OUI[1],
				       hdr->OUI[2]);
				xpds_data[card_num].stats.rx_errors++;
				break;
			}

			/* at this point, it's an EtherType frame */
			header = sizeof(struct frhdr);
			/* Already in network order ! */
			skb->protocol = hdr->PID;
			process = 1;
			break;

		case FRAD_P_IP:
			dprintk(KERN_DEBUG
				"hdr->IP_NLPID = FRAD_P_IP (%02X)\n",
				FRAD_P_IP);
			header =
			    sizeof(hdr->addr_control) +
			    sizeof(hdr->control) + sizeof(hdr->IP_NLPID);
			if (actual_header_size < header) {
				nrprintk (KERN_NOTICE "%s: header is too short (%d bytes, IP_NLPID == FRAD_P_IP)\n", dev->name, skb->tail - skb->data);
				xpds_data[card_num].stats.rx_errors++;
			} else {
				skb->protocol = htons(ETH_P_IP);
				process = 1;
			}
			break;

		case FRAD_P_Q933:
			/* status / status enquiry message */
			dprintk(KERN_DEBUG
				"hdr->IP_NLPID = FRAD_P_Q933 (%02X)\n",
				FRAD_P_Q933);
			xpds_dlci_handle_status(skb, dev);
			break;
		case FRAD_P_SNAP:
			dprintk(KERN_DEBUG
				"hdr->IP_NLPID = FRAD_P_SNAP (%02X)\n",
				FRAD_P_SNAP);
			skb->protocol = htons(ETH_P_ARP);
			process = 1;
			break;
		case FRAD_P_CLNP:
			nrprintk(KERN_NOTICE
			       "%s: Unsupported NLPID 0x%02X.\n",
			       dev->name, hdr->pad);
			xpds_data[card_num].stats.rx_errors++;
			break;

		default:
			nrprintk(KERN_NOTICE
			       "%s: Invalid pad byte 0x%02X.\n", dev->name,
			       hdr->pad);
			xpds_data[card_num].stats.rx_errors++;
			break;
		}
	}

	if (process) {
		dprintk(KERN_DEBUG "%s: header size = %d\n", dev->name,
			header);
		if (xpds_data[card_num].bridged_ethernet) {
			skb_pull(skb, header);
		} else {
			struct ethhdr *ehdr;
			skb_pull(skb, header);
			skb_push(skb, sizeof(struct ethhdr));
			ehdr = (struct ethhdr *) skb->data;
			memcpy(ehdr->h_dest,
			       xpds_data[card_num].serial_data.mac_address,
			       sizeof(ehdr->h_dest));
			/* +++ */
			memset(ehdr->h_source, 0xff,
			       sizeof(ehdr->h_source));
			/* +++ */
			ehdr->h_proto = skb->protocol;
		}
		skb->ip_summed = CHECKSUM_NONE;
#if DEBUG
		{
			long	i, skblen;

			dprintk(KERN_DEBUG
				"%s: packet of length %ld to be given to netif_rx():", dev->name, (long) (skb->len));
			skblen = skb->len > 256 ? 256 : skb->len;
			for (i = 0; i < skblen; i++) {
				dprintk(" %02x", skb->data[i]);
			}
			if (skb->len > skblen) dprintk (" ...");
			dprintk("\n");
			dprintk(KERN_DEBUG
				"%s: skb->head = %p, skb->data = %p, skb->tail = %p, skb->end = %p\n",
				dev->name, skb->head, skb->data, skb->tail,
				skb->end);
		}
#endif
		skb->protocol = eth_type_trans(skb, dev);
		dprintk(KERN_DEBUG "calling netif_rx (%p)\n", skb);
		netif_rx(skb);
	} else {
		dev_kfree_skb(skb);
	}
	dprintk(KERN_DEBUG "xpds_dlci_receive done\n");
}

static unsigned int
xpds_dlci_add_fr_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, u8 * hdr)
{
	unsigned hlen;
	short dlci;
	struct frad_local *flp;
	int card_num;
	struct frhdr *frhdr;

	frhdr = (struct frhdr *) hdr;
	card_num = dev - xpds_devs;
	dprintk(KERN_DEBUG "%s: adding frame relay header\n", dev->name);
	flp = &(xpds_data[card_num].frad_data);
	dlci = xpds_data[card_num].dlci;
	hdr[0] = ((dlci >> 4) << 2) | xpds_data[card_num].dlci_cr;
	hdr[1] = (dlci << 4) | 1;

	frhdr->control = FRAD_I_UI;
	dprintk(KERN_DEBUG "%s: type = 0x%04x\n", dev->name, type);
	switch (type) {
	case ETH_P_IP:
	case ETH_P_ARP:
		if (xpds_data[card_num].bridged_ethernet) {
			frhdr->pad = FRAD_P_PADDING;
			frhdr->NLPID = FRAD_P_SNAP;
			frhdr->OUI[0] = FRAD_OUI_BRIDGED_0;
			frhdr->OUI[1] = FRAD_OUI_BRIDGED_1;
			frhdr->OUI[2] = FRAD_OUI_BRIDGED_2;
			frhdr->PID = htons(FRAD_PID);
			hlen = sizeof(*frhdr);
		} else {
			frhdr->IP_NLPID = FRAD_P_IP;
			hlen =
			    sizeof(frhdr->addr_control) + 
			    sizeof(frhdr->control) +
			    sizeof(frhdr->IP_NLPID);
		}
		break;

		/* feel free to add other types, if necessary */

	default:
		frhdr->pad = FRAD_P_PADDING;
		frhdr->NLPID = FRAD_P_SNAP;
		memset(frhdr->OUI, 0, sizeof(frhdr->OUI));
		frhdr->PID = htons(type);
		hlen = sizeof(*frhdr);
		break;
	}

	dprintk(KERN_DEBUG "type = 0x%02x, bridged = %d, hlen = %d\n",
		type, xpds_data[card_num].bridged_ethernet, hlen);

#if DEBUG
	{
		int i;

		dprintk(KERN_DEBUG "dlci_header added:");
		for (i = 0; i < hlen; i++) {
			dprintk(" %02x", ((u8 *) hdr)[i]);
		}
		dprintk("\n");
	}
#endif
	return hlen;
}

int
xpds_dlci_transmit(struct sk_buff *skb, struct net_device *dev)
{
	u8 *hdr, *buffer;
	int card_num, rc;
	unsigned int ptr, len;

	dprintk(KERN_DEBUG "xpds_dlci_transmit (%p, %p)\n", skb, dev);
	card_num = dev - xpds_devs;

	buffer = kmalloc (skb->len + sizeof(struct frhdr), GFP_ATOMIC);
	if (buffer == NULL) {
		printk (KERN_ERR "%s: failed to allocate buffer in xpds_dlci_transmit()\n", xpds_devs[card_num].name);
		return -ENOMEM;
	}
	hdr = buffer;
	ptr = xpds_dlci_add_fr_header(skb, dev, ntohs(skb->protocol), hdr);

	if (xpds_data[card_num].bridged_ethernet) {
		memcpy(buffer + ptr, skb->data,
		       sizeof(struct ethhdr));
		ptr += sizeof(struct ethhdr);
	}
	memcpy(buffer + ptr,
	       skb->data + sizeof(struct ethhdr),
	       skb->len - sizeof(struct ethhdr));
	len = ptr + skb->len - sizeof(struct ethhdr);

	rc = xpds_tx(buffer, len, dev);
	kfree (buffer);

	if (rc) {
		if (rc != -EBUSY) xpds_data[card_num].stats.tx_errors++;
	} else {
		xpds_data[card_num].stats.tx_packets++;
		dev_kfree_skb(skb);
	}

	dprintk(KERN_DEBUG "xpds_dlci_transmit done\n");
	return rc;
}
