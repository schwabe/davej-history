/*
 * Copyright 2000, Xpeed, Inc.
 * fr.h $Revision: 1.10 $
 * License to copy and distribute is GNU General Public License, version 2.
 * Some code adapted from Mike McLagen's /usr/include/linux/if_frad.h in
 * Linux kernels 2.0-2.2.
 */

#ifndef XPDS_ENCAP_FR_H
#define XPDS_ENCAP_FR_H

#include <linux/config.h>
#include <linux/if.h>
#include <asm/types.h>
#include "xpds-softnet.h"

#ifndef CONFIG_DLCI_MAX
#define CONFIG_DLCI_MODULE	1
#define CONFIG_DLCI_MAX	8
#define CONFIG_DLCI_COUNT	24
#endif

#define FRAD_STATION_CPE	0x0000
#define FRAD_STATION_NODE	0x0001

#define FRAD_TX_IGNORE_CIR	0x0001
#define FRAD_RX_ACCOUNT_CIR	0x0002
#define FRAD_DROP_ABORTED	0x0004
#define FRAD_BUFFERIF		0x0008
#define FRAD_STATS		0x0010
#define FRAD_MCI		0x0100
#define FRAD_AUTODLCI		0x8000
#define FRAD_VALID_FLAGS	0x811F

#define FRAD_CLOCK_INT		0x0001
#define FRAD_CLOCK_EXT		0x0000

/* these are the fields of an RFC 1490 header */
struct frhdr {
	u8 addr_control[2] __attribute__ ((packed));
	u8 control __attribute__ ((packed));

	/* for IP packets, this can be the NLPID */
	u8 pad __attribute__ ((packed));

	u8 NLPID __attribute__ ((packed));
	u8 OUI[3] __attribute__ ((packed));
	u16 PID __attribute__ ((packed));

#define IP_NLPID pad
} __attribute__ ((packed));

/* see RFC 1490 for the definition of the following */
#define FRAD_I_UI		0x03

#define FRAD_P_PADDING		0x00
#define FRAD_P_Q933		0x08
#define FRAD_P_SNAP		0x80
#define FRAD_P_CLNP		0x81
#define FRAD_P_IP		0xCC

#define FRAD_OUI_BRIDGED_0	0x00
#define FRAD_OUI_BRIDGED_1	0x80
#define FRAD_OUI_BRIDGED_2	0xc2

#define FRAD_PID		0x0007

struct frad_local {
	short dlci[CONFIG_DLCI_MAX];

	/* fields for LMI messages */
	int liv_send_sequence;
	int liv_receive_sequence;
	int remote_liv_send_sequence;
	int remote_liv_receive_sequence;
	int new_liv_send_sequence;
	int pvc_active[CONFIG_DLCI_MAX];
	int pvc_new[CONFIG_DLCI_MAX];
	int no_initiate_lmi;
	int message_number;

};

extern void xpds_dlci_receive(struct sk_buff *skb, struct net_device *dev);
extern int xpds_dlci_transmit(struct sk_buff *skb, struct net_device *dev);

#define XPDS_DEFAULT_DLCI	16
#define XPDS_DEFAULT_DLCI_CR	0 /* 0 or 2 are the possible values */

#define XPDS_DLCI_LMI_LT_OR_NT		(-1)
#define XPDS_DLCI_LMI_NONE		0
#define XPDS_DLCI_LMI_LT		1
#define XPDS_DLCI_LMI_NT		2
#define XPDS_DLCI_LMI_NT_BIDIRECTIONAL	3

typedef struct dlci_lmi_timer_data_t {
        struct net_device   *dev;
	int             dlci_num;
} dlci_lmi_timer_data_t;

void xpds_dlci_install_lmi_timer (int i, struct net_device *dev);
void xpds_dlci_remove_lmi_timer (int i, struct net_device *dev);


#endif
