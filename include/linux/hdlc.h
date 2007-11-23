/*
 * Generic HDLC support routines for Linux
 *
 * Copyright (C) 1999, 2000 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HDLC_H
#define __HDLC_H

/* Ioctls */
#define HDLCSETMODE	(0x89FC) /* FIXME */
#define HDLCPVC		(0x89FD)
#define HDLCSETLINE	(0x89FE) /* Handled by low-level driver */
#define HDLCRUN		(0x89FF)

/* Modes */
#define MODE_NONE	0x00000000 /* Not initialized */
#define MODE_DCE	0x00000080 /* DCE */
#define MODE_HDLC	0x00000100 /* Raw HDLC frames */
#define MODE_CISCO	0x00000200
#define MODE_PPP	0x00000400
#define MODE_FR		0x00000800 /* Any LMI */
#define MODE_FR_ANSI	0x00000801
#define MODE_FR_CCITT	0x00000802
#define MODE_X25	0x00001000
#define MODE_MASK	0x0000FF00
#define MODE_SOFT	0x80000000 /* Driver modes, using hardware HDLC */

/* Setline values */
#define LINE_EXT_CLOCK	0	/* External clock */
#define LINE_V35	1
#define LINE_RS232	2
#define LINE_X21	3
#define LINE_LOOPBACK	4	/* On-card loopback */
#define LINE_NOLOOPBACK	5
#define LINE_MIN_CLOCK 75	/* This & higher values are real clock rates */


#define HDLC_MAX_MTU 1600
/* Allow max 10 bytes for FR header */
#define PVC_MAX_MTU (HDLC_MAX_MTU - 10)

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>

#define MAXLEN_LMISTAT  20	/* max size status enquiry frame */

#define LINK_STATE_RELIABLE 0x01
#define LINK_STATE_REQUEST  0x02 /* full stat sent (DCE) / req pending (DTE) */
#define LINK_STATE_CHANGED  0x04 /* change in PVCs state, send full report */
#define LINK_STATE_FULLREP_SENT 0x08 /* full report sent */

#define PVC_STATE_NEW       0x01
#define PVC_STATE_ACTIVE    0x02
#define PVC_STATE_FECN	    0x08 /* FECN condition */
#define PVC_STATE_BECN      0x10 /* BECN condition */


#define FR_UI              0x03
#define FR_PAD             0x00

#define NLPID_IP           0xCC
#define NLPID_IPV6         0x8E
#define NLPID_SNAP         0x80
#define NLPID_PAD          0x00
#define NLPID_Q933         0x08


#define LMI_DLCI                   0 /* LMI DLCI */
#define LMI_PROTO               0x08
#define LMI_CALLREF             0x00 /* Call Reference */
#define LMI_ANSI_LOCKSHIFT      0x95 /* ANSI lockshift */
#define LMI_REPTYPE                1 /* report type */
#define LMI_CCITT_REPTYPE       0x51
#define LMI_ALIVE                  3 /* keep alive */
#define LMI_CCITT_ALIVE         0x53
#define LMI_PVCSTAT                7 /* pvc status */
#define LMI_CCITT_PVCSTAT       0x57
#define LMI_FULLREP                0 /* full report  */
#define LMI_INTEGRITY              1 /* link integrity report */
#define LMI_SINGLE                 2 /* single pvc report */
#define LMI_STATUS_ENQUIRY      0x75
#define LMI_STATUS              0x7D /* reply */

#define LMI_REPT_LEN               1 /* report type element length */
#define LMI_INTEG_LEN              2 /* link integrity element length */

#define LMI_LENGTH                13 /* standard LMI frame length */
#define LMI_ANSI_LENGTH           14



typedef struct {
	unsigned ea1  : 1;
	unsigned cr   : 1;
	unsigned dlcih: 6;
  
	unsigned ea2  : 1;
	unsigned de   : 1;
	unsigned becn : 1;
	unsigned fecn : 1;
	unsigned dlcil: 4;
}__attribute__ ((packed)) fr_hdr;



typedef struct {		/* Used in Cisco and PPP mode */
	u8 address;
	u8 control;
	u16 protocol;
}__attribute__ ((packed)) hdlc_header;



typedef struct {
	u32 type;		/* code */
	u32 par1;
	u32 par2;
	u16 rel;		/* reliability */
	u32 time;
}__attribute__ ((packed)) cisco_packet;



typedef struct pvc_device_struct {
	struct device netdev;	/* PVC net device - must be first */
	struct net_device_stats stats;
	struct hdlc_device_struct *master;
	struct pvc_device_struct *next;

	u8 state;
	u8 newstate;
	char name[IFNAMSIZ];
}pvc_device;



typedef struct {
	u32 last_errors;	/* last errors bit list */
	int last_poll;		/* ! */
	u8 T391;		/* ! link integrity verification polling timer */
	u8 T392;		/* ! polling verification timer */
	u8 N391;		/* full status polling counter */
	u8 N392;		/* error threshold */
	u8 N393;		/* monitored events count */
	u8 N391cnt;

	u8 state;		/* ! */
	u8 txseq;		/* ! TX sequence number */
	u8 rxseq;		/* ! RX sequence number */
}fr_lmi;			/* ! means used in Cisco HDLC as well */


typedef struct hdlc_device_struct {
	/* to be initialized by hardware driver: */
	struct device dev; /* master net device - must be first */
	struct net_device_stats stats;

	struct ppp_device pppdev; /* to be removed in 2.3 */
	struct ppp_device *syncppp_ptr;

	/* set_mode may be NULL if HDLC-only board */
	int (*set_mode)(struct hdlc_device_struct *hdlc, int mode);
	int (*open)(struct hdlc_device_struct *hdlc);
	void (*close)(struct hdlc_device_struct *hdlc);
	int (*ioctl)(struct hdlc_device_struct *hdlc, struct ifreq *ifr,
		     int cmd);
  
	/* Only in "hardware" FR modes etc. - may be NULL */
	int (*create_pvc)(pvc_device *pvc);
	void (*destroy_pvc)(pvc_device *pvc);
	int (*open_pvc)(pvc_device *pvc);
	void (*close_pvc)(pvc_device *pvc);

	/* for hdlc.c internal use only */
	pvc_device *first_pvc;
	u16 pvc_count;
	char name[IFNAMSIZ];
	int mode;

	struct timer_list timer;
	fr_lmi lmi;
}hdlc_device;


int register_hdlc_device(hdlc_device *device);
void unregister_hdlc_device(hdlc_device *device);
void hdlc_netif_rx(hdlc_device *hdlc, struct sk_buff *skb, int dlci);


extern __inline__ struct device* hdlc_to_dev(hdlc_device *hdlc)
{
	return &hdlc->dev;
}


extern __inline__ hdlc_device* dev_to_hdlc(struct device *dev)
{
	return (hdlc_device*)dev;
}


extern __inline__ pvc_device* dev_to_pvc(struct device *dev)
{
	return (pvc_device*)dev;
}


extern __inline__ u16 status_to_dlci(hdlc_device *hdlc, u8 *status, u8 *state)
{
	*state &= ~(PVC_STATE_ACTIVE | PVC_STATE_NEW);
	if (status[2] & 0x08)
		*state |= PVC_STATE_NEW;
	else if (status[2] & 0x02)
		*state |= PVC_STATE_ACTIVE;

	return ((status[0] & 0x3F)<<4) | ((status[1] & 0x78)>>3);
}


extern __inline__ void dlci_to_status(hdlc_device *hdlc, u16 dlci, u8 *status,
				      u8 state)
{
	status[0] = (dlci>>4) & 0x3F;
	status[1] = ((dlci<<3) & 0x78) | 0x80;
	status[2] = 0x80;

	if (state & PVC_STATE_NEW)
		status[2] |= 0x08;
	else if (state & PVC_STATE_ACTIVE)
		status[2] |= 0x02;
}



extern __inline__ u16 netdev_dlci(struct device *dev)
{
	return ntohs(*(u16*)dev->dev_addr);
}



extern __inline__ u16 q922_to_dlci(u8 *hdr)
{
	return ((hdr[0] & 0xFC)<<2) | ((hdr[1] & 0xF0)>>4);
}



extern __inline__ void dlci_to_q922(u8 *hdr, u16 dlci)
{
	hdr[0] = (dlci>>2) & 0xFC;
	hdr[1] = ((dlci<<4) & 0xF0) | 0x01;
}



extern __inline__ int mode_is(hdlc_device *hdlc, int mask)
{
	return (hdlc->mode & mask) == mask;
}



extern __inline__ pvc_device* find_pvc(hdlc_device *hdlc, u16 dlci)
{
	pvc_device *pvc=hdlc->first_pvc;
	
	while (pvc) {
		if (netdev_dlci(&pvc->netdev) == dlci)
			return pvc;
		pvc=pvc->next;
	}

	return NULL;
}

#endif /* __KERNEL */
#endif /* __HDLC_H */
