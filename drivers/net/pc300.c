#define	USE_PCI_CLOCK
static char rcsid[] =
"Revision: 3.4.2 Date: 2001/10/11 ";

/*
 * pc300.c	Cyclades-PC300(tm) Driver.
 *
 * Author:	Ivan Passos <ivan@cyclades.com>
 *
 * Copyright:	(c) 1999-2001 Cyclades Corp.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 * 
 * $Log: pc300.c,v $
 * Revision 3.2 to 3.12  2001/10/11 20:26:04  daniela
 * Fixes for noisy lines: return the size of bad frames in 
 * dma_get_rx_frame_size, so that the Rx buffer descriptors can be cleaned by 
 * dma_buf_read (called in cpc_net_rx); improved Rx statistics; created 
 * rx_dma_start routine. 
 * Changed file revision to the package revision, changed T1/E1 master clock 
 * configuration, reviewed boot messages and default configuration. 
 * Included new configuration parameters (line code, CRC calculation and clock)
 * Changed the header of message trace to include the device name. New format:
 * "hdlcX[R/T]: ".
 *
 * Revision 3.1  2001/06/15 12:41:10  regina
 * upping major version number
 *
 * Revision 1.1.1.1  2001/06/13 20:24:25  daniela
 * PC300 initial CVS version (3.4.0-pre1)
 *
 * Revision 3.1.0.7 2001/06/08 daniela
 * Did some changes in the DMA programming implementation to avoid the 
 * occurrence of a SCA-II bug when CDA is accessed during a DMA transfer.
 *
 * Revision 3.1.0.6 2001/03/02 daniela
 * Changed SIOCGPC300CONF ioctl, to give hw information to pc300util.
 *
 * Revision 3.1.0.5 2001/02/23 daniela
 * Fixed falc_check_status for Unframed E1.
 * 
 * Revision 3.1.0.4 2000/12/22 daniela,ivan
 * Added support for Unframed E1.
 * Implemented pc300util support: trace, statistics, status and loopback
 * tests for the PC300 TE boards.
 * Implemented monitor mode.
 * Fixed DCD sensitivity on the second channel.
 *
 * Revision 3.1.0.3 2000/09/28 daniela,ivan
 * Implemented DCD sensitivity.
 * Changed location of pc300.h .
 *
 * Revision 3.1.0.2 2000/06/27 ivan
 * Previous bugfix for the framing errors with external clock made X21 
 * boards stop working. This version fixes it.
 * 
 * Revision 3.1.0.1 2000/06/23 ivan
 * Revisited cpc_queue_xmit to prevent race conditions on Tx DMA buffer 
 * handling when Tx timeouts occur.
 * Revisited Rx statistics.
 * Added support for loopback mode in the SCA-II.
 * Fixed a bug in the SCA-II programming that would cause framing errors 
 * when external clock was configured.
 *
 * Revision 3.1.0.0 2000/05/26 ivan
 * Added Frame-Relay support.
 * Driver now uses HDLC generic driver to provide protocol support.
 * Added logic in the SCA interrupt handler so that no board can monopolize 
 * the driver.
 * Request PLX I/O region, although driver doesn't use it, to avoid
 * problems with other drivers accessing it.
 *
 * Revision 3.0.0.0 2000/05/15 ivan
 * Did some changes in the DMA programming implementation to avoid the 
 * occurrence of a SCA-II bug in the second channel.
 * Implemented workaround for PLX9050 bug that would cause a system lockup
 * in certain systems, depending on the MMIO addresses allocated to the
 * board.
 * Fixed the FALC chip programming to avoid synchronization problems in the 
 * second channel (TE only).
 * Implemented a cleaner and faster Tx DMA descriptor cleanup procedure in 
 * cpc_queue_xmit().
 * Changed the built-in driver implementation so that the driver can use the 
 * general 'hdlcN' naming convention instead of proprietary device names.
 * Driver load messages are now device-centric, instead of board-centric.
 * Dynamic allocation of device structures.
 *
 * Revision 2.0.0.0 2000/04/15 ivan
 * Added support for the PC300/TE boards (T1/FT1/E1/FE1).
 *
 * Revision 1.1.0.0 2000/02/28 ivan
 * Major changes in the driver architecture.
 * Driver now reports physical instead of virtual memory addresses.
 * Added cpc_change_mtu function.
 *
 * Revision 1.0.0.0 1999/12/16 ivan
 * First official release.
 * Support for 1- and 2-channel boards (which use distinct PCI Device ID's).
 * Support for monolythic installation (i.e., drv built into the kernel).
 * X.25 additional checking when lapb_[dis]connect_request returns an error.
 * SCA programming now covers X.21 as well.
 *
 * Revision 0.3.1.0 1999/11/18 ivan
 * Made X.25 support configuration-dependent (as it depends on external 
 * modules to work).
 * Changed X.25-specific function names to comply with adopted convention.
 * Fixed typos in X.25 functions that would cause compile errors (Daniela).
 * Fixed bug in ch_config that would disable interrupts on a previously 
 * enabled channel if the other channel on the same board was enabled later.
 *
 * Revision 0.3.0.0 1999/11/16 daniela
 * X.25 support.
 *
 * Revision 0.2.3.0 1999/11/15 ivan
 * Function cpc_ch_status now provides more detailed information.
 * Added support for X.21 clock configuration.
 * Changed TNR1 setting in order to prevent Tx FIFO overaccesses by the SCA.
 * Now using PCI clock instead of internal oscillator clock for the SCA.
 *
 * Revision 0.2.2.0 1999/11/10 ivan
 * Changed the *_dma_buf_check functions so that they would print only 
 * the useful info instead of the whole buffer descriptor bank.
 * Fixed bug in cpc_queue_xmit that would eventually crash the system 
 * in case of a packet drop.
 * Implemented TX underrun handling.
 * Improved SCA fine tuning to boost up its performance.
 *
 * Revision 0.2.1.0 1999/11/03 ivan
 * Added functions *dma_buf_pt_init to allow independent initialization 
 * of the next-descr. and DMA buffer pointers on the DMA descriptors.
 * Kernel buffer release and tbusy clearing is now done in the interrupt 
 * handler.
 * Fixed bug in cpc_open that would cause an interface reopen to fail.
 * Added a protocol-specific code section in cpc_net_rx.
 * Removed printk level defs (they might be added back after the beta phase).
 *
 * Revision 0.2.0.0 1999/10/28 ivan
 * Revisited the code so that new protocols can be easily added / supported. 
 *
 * Revision 0.1.0.1 1999/10/20 ivan
 * Mostly "esthetic" changes.
 *
 * Revision 0.1.0.0 1999/10/11 ivan
 * Initial version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <net/arp.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/spinlock.h>

#ifdef CONFIG_PC300_X25
#include <linux/lapb.h>
#endif /* CONFIG_PC300_X25 */

#include "syncppp.h"
#include "pc300.h"

#define	CPC_LOCK(card,flags)					\
		do {						\
		spin_lock_irqsave(&card->card_lock, flags);	\
		} while (0)

#define CPC_UNLOCK(card,flags)						\
		do {							\
		spin_unlock_irqrestore(&card->card_lock, flags);	\
		} while (0)

#undef	PC300_DEBUG_PCI
#undef	PC300_DEBUG_INTR
#undef	PC300_DEBUG_TX
#undef	PC300_DEBUG_RX
#undef	PC300_DEBUG_OTHER

/* Hardware configuration options.
 * These are arrays of configuration options used by verification routines.
 * The first element of each array is its size (i.e. number of options).
 */
static unsigned short	cpc_pci_dev_id[] = {
	PCI_DEVICE_ID_PC300_RX_1,	/* PC300/RSV or PC300/X21, 1 chan */
	PCI_DEVICE_ID_PC300_RX_2,	/* PC300/RSV or PC300/X21, 2 chan */
	PCI_DEVICE_ID_PC300_TE_1,	/* PC300/TE, 1 chan */
	PCI_DEVICE_ID_PC300_TE_2,	/* PC300/TE, 2 chan */
	0				/* end of table */
};

/* This is the per-card data structure containing mem. addresses, irq, etc.
 * This driver supports a maximum of PC300_MAXCARDS cards.
 */
static pc300_t	cpc_card[PC300_MAXCARDS];

static ucshort	cpc_nboards = 0;

#ifndef min
#define	min(a,b)	(((a)<(b))?(a):(b))
#endif
#ifndef max
#define	max(a,b)	(((a)>(b))?(a):(b))
#endif

/************************/
/***   DMA Routines   ***/
/************************/
static void 
tx_dma_buf_pt_init(pc300_t *card, int ch)
{
    int i;
    int ch_factor = ch * N_DMA_TX_BUF;
    volatile pcsca_bd_t *ptdescr = (pcsca_bd_t *)(card->hw.rambase 
		+ DMA_TX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

    for (i = 0 ; i < N_DMA_TX_BUF ; i++, ptdescr++) {
	cpc_writel(&ptdescr->next, (uclong) (DMA_TX_BD_BASE + 
		   (ch_factor + ((i + 1) & (N_DMA_TX_BUF - 1))) * 
		   sizeof(pcsca_bd_t)));
	cpc_writel(&ptdescr->ptbuf, 
		   (uclong)(DMA_TX_BASE + (ch_factor + i)*BD_DEF_LEN));
    }
}

static void 
tx_dma_buf_init(pc300_t *card, int ch)
{
    int i;
    int ch_factor = ch * N_DMA_TX_BUF;
    volatile pcsca_bd_t *ptdescr = (pcsca_bd_t *)(card->hw.rambase 
		+ DMA_TX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

    for (i = 0 ; i < N_DMA_TX_BUF ; i++, ptdescr++) {
	memset_io(ptdescr, 0, sizeof (pcsca_bd_t));
	cpc_writew(&ptdescr->len, 0);
	cpc_writeb(&ptdescr->status, 0);
    }
    tx_dma_buf_pt_init(card, ch);
}

static void 
rx_dma_buf_pt_init(pc300_t *card, int ch)
{
    int i;
    int ch_factor = ch * N_DMA_RX_BUF;
    volatile pcsca_bd_t *ptdescr = (pcsca_bd_t *)(card->hw.rambase 
		+ DMA_RX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

    for (i = 0 ; i < N_DMA_RX_BUF ; i++, ptdescr++) {
	cpc_writel(&ptdescr->next, (uclong) (DMA_RX_BD_BASE + 
		   (ch_factor + ((i + 1) & (N_DMA_RX_BUF - 1))) * 
		   sizeof(pcsca_bd_t)));
	cpc_writel(&ptdescr->ptbuf, 
		   (uclong)(DMA_RX_BASE + (ch_factor + i)*BD_DEF_LEN));
    }
}

static void 
rx_dma_buf_init(pc300_t *card, int ch)
{
    int i;
    int ch_factor = ch * N_DMA_RX_BUF;
    volatile pcsca_bd_t *ptdescr = (pcsca_bd_t *)(card->hw.rambase 
		+ DMA_RX_BD_BASE + ch_factor * sizeof(pcsca_bd_t));

    for (i = 0 ; i < N_DMA_RX_BUF ; i++, ptdescr++) {
	memset_io(ptdescr, 0, sizeof (pcsca_bd_t));
	cpc_writew(&ptdescr->len, 0);
	cpc_writeb(&ptdescr->status, 0);
    }
    rx_dma_buf_pt_init(card, ch);
}

static void
tx_dma_buf_check(pc300_t *card, int ch)
{
    volatile pcsca_bd_t *ptdescr;
    int i;
    ucshort first_bd = card->chan[ch].tx_first_bd;
    ucshort next_bd = card->chan[ch].tx_next_bd;

    printk("#CH%d: f_bd = %d(0x%08x), n_bd = %d(0x%08x)\n", ch, 
	   first_bd, TX_BD_ADDR(ch, first_bd), 
	   next_bd, TX_BD_ADDR(ch, next_bd));
    for (i = first_bd, 
	  ptdescr = (pcsca_bd_t *)(card->hw.rambase + TX_BD_ADDR(ch, first_bd));
	 i != ((next_bd + 1) & (N_DMA_TX_BUF - 1)) ;
	 i = (i + 1) & (N_DMA_TX_BUF - 1), 
	  ptdescr = (pcsca_bd_t *)(card->hw.rambase + TX_BD_ADDR(ch, i))) {
	printk("\n CH%d TX%d: next=0x%lx, ptbuf=0x%lx, ST=0x%x, len=%d", 
	       ch, i,
	       (uclong)cpc_readl(&ptdescr->next), 
	       (uclong)cpc_readl(&ptdescr->ptbuf),
	       cpc_readb(&ptdescr->status), 
	       cpc_readw(&ptdescr->len));
    }
    printk("\n");
}

static void
rx_dma_buf_check(pc300_t *card, int ch)
{
    volatile pcsca_bd_t *ptdescr;
    int i;
    ucshort first_bd = card->chan[ch].rx_first_bd;
    ucshort last_bd = card->chan[ch].rx_last_bd;
    int ch_factor;

    ch_factor = ch * N_DMA_RX_BUF;
    printk("#CH%d: f_bd = %d, l_bd = %d\n", ch, first_bd, last_bd);
    for (i = 0, ptdescr = (pcsca_bd_t *)(card->hw.rambase +
                        DMA_RX_BD_BASE + ch_factor*sizeof(pcsca_bd_t));
	 i < N_DMA_RX_BUF ;
	 i++, ptdescr++) {
	if (cpc_readb(&ptdescr->status) & DST_OSB)
	    printk("\n CH%d RX%d: next=0x%lx, ptbuf=0x%lx, ST=0x%x, len=%d", 
		   ch, i,
		   (uclong)cpc_readl(&ptdescr->next), 
		   (uclong)cpc_readl(&ptdescr->ptbuf),
		   cpc_readb(&ptdescr->status), 
		   cpc_readw(&ptdescr->len));
    }
    printk("\n");
}

int 
dma_get_rx_frame_size(pc300_t *card, int ch)
{
    volatile pcsca_bd_t *ptdescr;
    ucshort first_bd = card->chan[ch].rx_first_bd;
    int rcvd = 0;
    volatile ucchar status;

    ptdescr = (pcsca_bd_t *)(card->hw.rambase + RX_BD_ADDR(ch, first_bd));
    while ((status = cpc_readb(&ptdescr->status)) & DST_OSB) {
	rcvd += cpc_readw(&ptdescr->len);
	first_bd = (first_bd + 1) & (N_DMA_RX_BUF - 1);
	if ((status & DST_EOM) || (first_bd == card->chan[ch].rx_last_bd)) {
	    /* Return the size of a good frame or incomplete bad frame 
	     * (dma_buf_read will clean the buffer descriptors in this case). */
	    return (rcvd);
	}
	ptdescr = (pcsca_bd_t *)(card->hw.rambase + cpc_readl(&ptdescr->next));
    }
    return (-1);
}

/*
 * dma_buf_write: writes a frame to the Tx DMA buffers
 * NOTE: this function writes one frame at a time.
 */ 
int 
dma_buf_write(pc300_t *card, int ch, ucchar *ptdata, int len)
{
    int i, nchar;
    volatile pcsca_bd_t *ptdescr;
    int tosend = len;
    ucchar nbuf = ((len - 1)/BD_DEF_LEN) + 1;

    for (i = 0 ; i < nbuf ; i++) {
	ptdescr = (pcsca_bd_t *)(card->hw.rambase + 
		TX_BD_ADDR(ch, card->chan[ch].tx_next_bd));
	nchar = min(BD_DEF_LEN,tosend);
	if (!(cpc_readb(&ptdescr->status) & DST_OSB)) {
	    memcpy_toio((void *)(card->hw.rambase + 
				 cpc_readl(&ptdescr->ptbuf)), 
			&ptdata[len - tosend], 
			nchar);
	    if ((i + 1) == nbuf) {
		/* This must be the last BD to be used */
		cpc_writeb(&ptdescr->status, (DST_EOM | DST_EOT));
	    } else {
		cpc_writeb(&ptdescr->status, 0);
	    }
	    cpc_writew(&ptdescr->len, nchar);
	} else {
	    return -ENOMEM;
	}
	tosend -= nchar;
	card->chan[ch].tx_next_bd = 
		(card->chan[ch].tx_next_bd + 1) & (N_DMA_TX_BUF - 1);
    }
    /* If it gets to here, it means we have sent the whole frame */
    return 0;
}

/*
 * dma_buf_read: reads a frame from the Rx DMA buffers
 * NOTE: this function reads one frame at a time.
 */ 
int 
dma_buf_read(pc300_t *card, int ch, struct sk_buff *skb)
{
    int nchar;
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    volatile pcsca_bd_t *ptdescr;
    int rcvd = 0;
    volatile ucchar status;

    ptdescr = (pcsca_bd_t *)(card->hw.rambase + 
			     RX_BD_ADDR(ch, chan->rx_first_bd));
    while ((status = cpc_readb(&ptdescr->status)) & DST_OSB) {
	nchar = cpc_readw(&ptdescr->len);
	if ((status & (DST_OVR | DST_CRC | DST_RBIT | DST_SHRT | DST_ABT)) ||
							(nchar > BD_DEF_LEN)) {
	    if (nchar > BD_DEF_LEN) status |= DST_RBIT;
	    rcvd = -status;
	    /* Discard remaining descriptors used by the bad frame */
	    while(chan->rx_first_bd != chan->rx_last_bd) {
		cpc_writeb(&ptdescr->status, 0);
		chan->rx_first_bd = 
			(chan->rx_first_bd + 1) & (N_DMA_RX_BUF - 1);
		if(status & DST_EOM)
		    break;
		ptdescr = (pcsca_bd_t *)(card->hw.rambase + 
					 cpc_readl(&ptdescr->next));
		status = cpc_readb(&ptdescr->status);
	    }
	    break;
	}
	if (nchar != 0) {
	    if (skb) {
		memcpy_fromio(skb_put(skb, nchar), 
			      (void *)(card->hw.rambase + 
				       cpc_readl(&ptdescr->ptbuf)), 
			      nchar);
	    }
	    rcvd += nchar;
	}
	cpc_writeb(&ptdescr->status, 0);
	cpc_writeb(&ptdescr->len, 0);
	chan->rx_first_bd = (chan->rx_first_bd + 1) & (N_DMA_RX_BUF - 1);

	if (status & DST_EOM)	break;

	ptdescr = (pcsca_bd_t *)(card->hw.rambase + cpc_readl(&ptdescr->next));
    }

    if (rcvd != 0) {
	/* Update pointer */
	chan->rx_last_bd = (chan->rx_first_bd - 1) & (N_DMA_RX_BUF - 1);
	/* Update EDA */
	cpc_writel(card->hw.scabase + DRX_REG(EDAL, ch),
		   RX_BD_ADDR(ch, chan->rx_last_bd));
    }
    return (rcvd);
}

void 
tx_dma_stop(pc300_t *card, int ch)
{
    uclong scabase = card->hw.scabase;
    ucchar drr_ena_bit = 1<<(5 + 2*ch);
    ucchar drr_rst_bit = 1<<(1 + 2*ch);

    /* Disable DMA */
    cpc_writeb(scabase + DRR, drr_ena_bit);
    cpc_writeb(scabase + DRR, drr_rst_bit & ~drr_ena_bit);
}

void 
rx_dma_stop(pc300_t *card, int ch)
{
    uclong scabase = card->hw.scabase;
    ucchar drr_ena_bit = 1<<(4 + 2*ch);
    ucchar drr_rst_bit = 1<<(2*ch);

    /* Disable DMA */
    cpc_writeb(scabase + DRR, drr_ena_bit);
    cpc_writeb(scabase + DRR, drr_rst_bit & ~drr_ena_bit);
}

void 
rx_dma_start(pc300_t *card, int ch)
{
    uclong scabase = card->hw.scabase;
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];

    /* Start DMA */
    cpc_writel(scabase + DRX_REG(CDAL, ch), 
		RX_BD_ADDR(ch, chan->rx_first_bd));
    if (cpc_readl(scabase + DRX_REG(CDAL,ch)) !=
		   RX_BD_ADDR(ch, chan->rx_first_bd)) {
	cpc_writel(scabase + DRX_REG(CDAL, ch),
		    RX_BD_ADDR(ch, chan->rx_first_bd));
    }
    cpc_writel(scabase + DRX_REG(EDAL, ch), 
		RX_BD_ADDR(ch, chan->rx_last_bd));
    cpc_writew(scabase + DRX_REG(BFLL, ch), BD_DEF_LEN);
    cpc_writeb(scabase + DSR_RX(ch), DSR_DE);
    if (!(cpc_readb(scabase + DSR_RX(ch)) & DSR_DE)) {
	cpc_writeb(scabase + DSR_RX(ch), DSR_DE);
    }
}

/*************************/
/***   FALC Routines   ***/
/*************************/
void 
falc_issue_cmd(pc300_t *card, int ch, ucchar cmd)
{
    uclong falcbase = card->hw.falcbase;
    unsigned long i = 0;

    while (cpc_readb(falcbase + F_REG(SIS, ch)) & SIS_CEC) {
        if (i++ >= PC300_FALC_MAXLOOP) {
            printk("%s: FALC command locked(cmd=0x%x).\n", 
		   card->chan[ch].d.name, cmd);
            break;
        }
    }
    cpc_writeb(falcbase + F_REG(CMDR, ch), cmd);
}

void 
falc_intr_enable(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;

    /* Interrupt pins are open-drain */
    cpc_writeb(falcbase + F_REG(IPC, ch), 
	       cpc_readb(falcbase + F_REG(IPC, ch)) & ~IPC_IC0);
    /* Conters updated each second */
    cpc_writeb(falcbase + F_REG(FMR1, ch), 
	       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_ECM);
    /* Enable SEC and ES interrupts  */
    cpc_writeb(falcbase + F_REG(IMR3, ch), 
	       cpc_readb(falcbase + F_REG(IMR3, ch)) & ~(IMR3_SEC | IMR3_ES));
    if (conf->fr_mode == PC300_FR_UNFRAMED) {
	cpc_writeb(falcbase + F_REG(IMR4, ch),
		   cpc_readb(falcbase + F_REG(IMR4, ch)) & ~(IMR4_LOS));
    } else {
	cpc_writeb(falcbase + F_REG(IMR4, ch),
		   cpc_readb(falcbase + F_REG(IMR4, ch)) &
		   ~(IMR4_LFA | IMR4_AIS | IMR4_LOS | IMR4_SLIP));
    }
    if (conf->media == LINE_T1) {
	cpc_writeb(falcbase + F_REG(IMR3, ch), 
		   cpc_readb(falcbase + F_REG(IMR3, ch)) & ~IMR3_LLBSC);
    } else {
	cpc_writeb(falcbase + F_REG(IPC, ch), 
		   cpc_readb(falcbase + F_REG(IPC, ch)) | IPC_SCI);
	if (conf->fr_mode == PC300_FR_UNFRAMED) {
	    cpc_writeb(falcbase + F_REG(IMR2, ch),
		       cpc_readb(falcbase + F_REG(IMR2, ch)) & ~(IMR2_LOS));
	} else {
	    cpc_writeb(falcbase + F_REG(IMR2, ch),
		       cpc_readb(falcbase + F_REG(IMR2, ch)) &
		       ~(IMR2_FAR | IMR2_LFA | IMR2_AIS | IMR2_LOS));
	    if (pfalc->multiframe_mode) {
		cpc_writeb(falcbase + F_REG(IMR2, ch),
			   cpc_readb(falcbase + F_REG(IMR2, ch)) &
			   ~(IMR2_T400MS | IMR2_MFAR));
	    } else {
		cpc_writeb(falcbase + F_REG(IMR2, ch),
			   cpc_readb(falcbase + F_REG(IMR2, ch)) |
			   IMR2_T400MS | IMR2_MFAR);
	    }
	}
    }
}

void 
falc_open_timeslot(pc300_t *card, int ch, int timeslot)
{
    uclong falcbase = card->hw.falcbase;
    ucchar tshf = card->chan[ch].falc.offset;

    cpc_writeb(falcbase + F_REG((ICB1 + (timeslot - tshf)/8), ch),
	       cpc_readb(falcbase + F_REG((ICB1 + (timeslot - tshf)/8), ch)) & 
	       ~(0x80 >> ((timeslot - tshf) & 0x07)));
    cpc_writeb(falcbase + F_REG((TTR1 + timeslot/8), ch),
	       cpc_readb(falcbase + F_REG((TTR1 + timeslot/8), ch)) | 
	       (0x80 >> (timeslot & 0x07)));
    cpc_writeb(falcbase + F_REG((RTR1 + timeslot/8), ch),
	       cpc_readb(falcbase + F_REG((RTR1 + timeslot/8), ch)) | 
	       (0x80 >> (timeslot & 0x07)));
}

void 
falc_close_timeslot(pc300_t *card, int ch, int timeslot)
{
    uclong falcbase = card->hw.falcbase;
    ucchar tshf = card->chan[ch].falc.offset;

    cpc_writeb(falcbase + F_REG((ICB1 + (timeslot - tshf)/8), ch),
	       cpc_readb(falcbase + F_REG((ICB1 + (timeslot - tshf)/8), ch)) | 
	       (0x80 >> ((timeslot - tshf) & 0x07)));
    cpc_writeb(falcbase + F_REG((TTR1 + timeslot/8), ch),
	       cpc_readb(falcbase + F_REG((TTR1 + timeslot/8), ch)) & 
	       ~(0x80 >> (timeslot & 0x07)));
    cpc_writeb(falcbase + F_REG((RTR1 + timeslot/8), ch),
	       cpc_readb(falcbase + F_REG((RTR1 + timeslot/8), ch)) & 
	       ~(0x80 >> (timeslot & 0x07)));
}

void 
falc_close_all_timeslots(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    uclong falcbase = card->hw.falcbase;

    cpc_writeb(falcbase + F_REG(ICB1, ch), 0xff);
    cpc_writeb(falcbase + F_REG(TTR1, ch), 0);
    cpc_writeb(falcbase + F_REG(RTR1, ch), 0);
    cpc_writeb(falcbase + F_REG(ICB2, ch), 0xff);
    cpc_writeb(falcbase + F_REG(TTR2, ch), 0);
    cpc_writeb(falcbase + F_REG(RTR2, ch), 0);
    cpc_writeb(falcbase + F_REG(ICB3, ch), 0xff);
    cpc_writeb(falcbase + F_REG(TTR3, ch), 0);
    cpc_writeb(falcbase + F_REG(RTR3, ch), 0);
    if (conf->media == LINE_E1) {
	cpc_writeb(falcbase + F_REG(ICB4, ch), 0xff);
	cpc_writeb(falcbase + F_REG(TTR4, ch), 0);
	cpc_writeb(falcbase + F_REG(RTR4, ch), 0);
    }
}

void 
falc_open_all_timeslots(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    uclong falcbase = card->hw.falcbase;

    cpc_writeb(falcbase + F_REG(ICB1, ch), 0);
    if (conf->fr_mode == PC300_FR_UNFRAMED) {
	cpc_writeb(falcbase + F_REG(TTR1, ch), 0xff);
	cpc_writeb(falcbase + F_REG(RTR1, ch), 0xff);
    } else {
	/* Timeslot 0 is never enabled */
	cpc_writeb(falcbase + F_REG(TTR1, ch), 0x7f);
	cpc_writeb(falcbase + F_REG(RTR1, ch), 0x7f);
    }
    cpc_writeb(falcbase + F_REG(ICB2, ch), 0);
    cpc_writeb(falcbase + F_REG(TTR2, ch), 0xff);
    cpc_writeb(falcbase + F_REG(RTR2, ch), 0xff);
    cpc_writeb(falcbase + F_REG(ICB3, ch), 0);
    cpc_writeb(falcbase + F_REG(TTR3, ch), 0xff);
    cpc_writeb(falcbase + F_REG(RTR3, ch), 0xff);
    if (conf->media == LINE_E1) {
	cpc_writeb(falcbase + F_REG(ICB4, ch), 0);
	cpc_writeb(falcbase + F_REG(TTR4, ch), 0xff);
	cpc_writeb(falcbase + F_REG(RTR4, ch), 0xff);
    } else {
	cpc_writeb(falcbase + F_REG(ICB4, ch), 0xff);
	cpc_writeb(falcbase + F_REG(TTR4, ch), 0x80);
	cpc_writeb(falcbase + F_REG(RTR4, ch), 0x80);
    }
}

void 
falc_init_timeslot(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    int tslot;

    for (tslot = 0 ; tslot < pfalc->num_channels ; tslot++) {
	if (conf->tslot_bitmap & (1<<tslot)) {
	    // Channel enabled
	    falc_open_timeslot(card, ch, tslot + 1);
	} else {
	    // Channel disabled
	    falc_close_timeslot(card, ch, tslot + 1);
	}
    }
}

void 
falc_enable_comm(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;

    if (pfalc->full_bandwidth) {
	falc_open_all_timeslots(card, ch);
    } else {
	falc_init_timeslot(card, ch);
    }
    // CTS/DCD ON
    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
	       cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) & 
	       ~((CPLD_REG1_FALC_DCD | CPLD_REG1_FALC_CTS) << (2*ch)));
}

void 
falc_disable_comm(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;

    if (pfalc->loop_active != 2) {
	falc_close_all_timeslots(card, ch);
    }
    // CTS/DCD OFF
    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
	       cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) | 
	       ((CPLD_REG1_FALC_DCD | CPLD_REG1_FALC_CTS) << (2*ch)));
}

void
falc_init_t1(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
    ucchar dja = (ch ? (LIM2_DJA2|LIM2_DJA1) : 0);

    /* Switch to T1 mode (PCM 24) */
    cpc_writeb(falcbase + F_REG(FMR1, ch), FMR1_PMOD);

    /* Wait 20 us for setup */
    udelay(20);

    /* Transmit Buffer Size (1 frame) */
    cpc_writeb(falcbase + F_REG(SIC1, ch), SIC1_XBS0); 
	
    /* Clock mode */
    if (conf->clktype == PC300_CLOCK_INT) {	/* Master mode */
	cpc_writeb(falcbase + F_REG(LIM0, ch), 
		   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_MAS); 
    } else {	/* Slave mode */
	cpc_writeb(falcbase + F_REG(LIM0, ch), 
		   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_MAS); 
	cpc_writeb(falcbase + F_REG(LOOP, ch), 
		   cpc_readb(falcbase + F_REG(LOOP, ch)) & ~LOOP_RTM); 
    }

    cpc_writeb(falcbase + F_REG(IPC, ch), IPC_SCI);
    cpc_writeb(falcbase + F_REG(FMR0, ch), 
	       cpc_readb(falcbase + F_REG(FMR0, ch)) & 
	       ~(FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1));

    switch (conf->lcode) {
	case PC300_LC_AMI:
	    cpc_writeb(falcbase + F_REG(FMR0, ch), 
		       cpc_readb(falcbase + F_REG(FMR0, ch)) | 
		       FMR0_XC1 | FMR0_RC1);
	    /* Clear Channel register to ON for all channels */
	    cpc_writeb(falcbase + F_REG(CCB1, ch), 0xff);
	    cpc_writeb(falcbase + F_REG(CCB2, ch), 0xff);
	    cpc_writeb(falcbase + F_REG(CCB3, ch), 0xff);
	    break;			

	case PC300_LC_B8ZS:
	    cpc_writeb(falcbase + F_REG(FMR0, ch), 
		       cpc_readb(falcbase + F_REG(FMR0, ch)) | 
		       FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1);
	    break;

	case PC300_LC_NRZ:
	    cpc_writeb(falcbase + F_REG(FMR0, ch), 
		       cpc_readb(falcbase + F_REG(FMR0, ch)) | 0x00);
	    break;
    }

    cpc_writeb(falcbase + F_REG(LIM0, ch), 
	       cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_ELOS); 
    cpc_writeb(falcbase + F_REG(LIM0, ch), 
	       cpc_readb(falcbase + F_REG(LIM0, ch)) &
	       ~(LIM0_SCL1 | LIM0_SCL0)); 
    /* Set interface mode to 2 MBPS */	
    cpc_writeb(falcbase + F_REG(FMR1, ch), 
	       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_IMOD); 

    switch (conf->fr_mode) {
	case PC300_FR_ESF:
	    pfalc->multiframe_mode = 0;
	    cpc_writeb(falcbase + F_REG(FMR4, ch), 
		       cpc_readb(falcbase + F_REG(FMR4, ch)) | FMR4_FM1);
	    cpc_writeb(falcbase + F_REG(FMR1, ch), 
		       cpc_readb(falcbase + F_REG(FMR1, ch)) | 
		       FMR1_CRC | FMR1_EDL);
	    cpc_writeb(falcbase + F_REG(XDL1, ch), 0);
	    cpc_writeb(falcbase + F_REG(XDL2, ch), 0);
	    cpc_writeb(falcbase + F_REG(XDL3, ch), 0);
	    cpc_writeb(falcbase + F_REG(FMR0, ch), 
		       cpc_readb(falcbase + F_REG(FMR0, ch)) & ~FMR0_SRAF);
	    cpc_writeb(falcbase + F_REG(FMR2, ch), 
		       cpc_readb(falcbase + F_REG(FMR2, ch)) | 
		       FMR2_MCSP | FMR2_SSP);
	    break;

	case PC300_FR_D4:
	    pfalc->multiframe_mode = 1;
	    cpc_writeb(falcbase + F_REG(FMR4, ch), 
		       cpc_readb(falcbase + F_REG(FMR4, ch)) &
	    	       ~(FMR4_FM1 | FMR4_FM0));
	    cpc_writeb(falcbase + F_REG(FMR0, ch), 
		       cpc_readb(falcbase + F_REG(FMR0, ch)) | FMR0_SRAF);
	    cpc_writeb(falcbase + F_REG(FMR2, ch), 
		       cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_SSP);
	    break;
    }
	
    /* Enable Automatic Resynchronization */
    cpc_writeb(falcbase + F_REG(FMR4, ch), 
	       cpc_readb(falcbase + F_REG(FMR4, ch)) | FMR4_AUTO);

    /* Transmit Automatic Remote Alarm */
    cpc_writeb(falcbase + F_REG(FMR2, ch), 
	       cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_AXRA);

    /* Channel translation mode 1 : one to one */
    cpc_writeb(falcbase + F_REG(FMR1, ch), 
	       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_CTM);

    /* No signaling */
    cpc_writeb(falcbase + F_REG(FMR1, ch), 
	       cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_SIGM);
    cpc_writeb(falcbase + F_REG(FMR5, ch), 
	       cpc_readb(falcbase + F_REG(FMR5, ch)) & 
	       ~(FMR5_EIBR | FMR5_SRS));
    cpc_writeb(falcbase + F_REG(CCR1, ch), 0);

    cpc_writeb(falcbase + F_REG(LIM1, ch), 
	       cpc_readb(falcbase + F_REG(LIM1, ch)) | LIM1_RIL0 | LIM1_RIL1); 

    switch (conf->lbo) {
	/* Provides proper Line Build Out */
	case PC300_LBO_0_DB:
	    cpc_writeb(falcbase + F_REG(LIM2, ch), (LIM2_LOS1 | dja));
	    cpc_writeb(falcbase + F_REG(XPM0, ch), 0x5a);
	    cpc_writeb(falcbase + F_REG(XPM1, ch), 0x8f);
	    cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
	    break;
	case PC300_LBO_7_5_DB:
	    cpc_writeb(falcbase + F_REG(LIM2, ch), (0x40 | LIM2_LOS1 | dja));
	    cpc_writeb(falcbase + F_REG(XPM0, ch), 0x11);
	    cpc_writeb(falcbase + F_REG(XPM1, ch), 0x02);
	    cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
	    break;
	case PC300_LBO_15_DB:
	    cpc_writeb(falcbase + F_REG(LIM2, ch), (0x80 | LIM2_LOS1 | dja));
	    cpc_writeb(falcbase + F_REG(XPM0, ch), 0x8e);
	    cpc_writeb(falcbase + F_REG(XPM1, ch), 0x01);
	    cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
	    break;
	case PC300_LBO_22_5_DB:
	    cpc_writeb(falcbase + F_REG(LIM2, ch), (0xc0 | LIM2_LOS1 | dja));
	    cpc_writeb(falcbase + F_REG(XPM0, ch), 0x09);
	    cpc_writeb(falcbase + F_REG(XPM1, ch), 0x01);
	    cpc_writeb(falcbase + F_REG(XPM2, ch), 0x20);
	    break;
    }
                                              
    /* Transmit Clock-Slot Offset */
    cpc_writeb(falcbase + F_REG(XC0, ch), 
	       cpc_readb(falcbase + F_REG(XC0, ch)) | 0x01); 
    /* Transmit Time-slot Offset */
    cpc_writeb(falcbase + F_REG(XC1, ch), 0x3e);
    /* Receive  Clock-Slot offset */
    cpc_writeb(falcbase + F_REG(RC0, ch), 0x05);
    /* Receive  Time-slot offset */
    cpc_writeb(falcbase + F_REG(RC1, ch), 0x00);

    /* LOS Detection after 176 consecutive 0s */
    cpc_writeb(falcbase + F_REG(PCDR, ch), 0x0a);
    /* LOS Recovery after 22 ones in the time window of PCD */
    cpc_writeb(falcbase + F_REG(PCRR, ch), 0x15);

    cpc_writeb(falcbase + F_REG(IDLE, ch), 0x7f);

    if (conf->fr_mode == PC300_FR_ESF_JAPAN) {
	cpc_writeb(falcbase + F_REG(RC1, ch), 
		   cpc_readb(falcbase + F_REG(RC1, ch)) | 0x80);
    }
	
    falc_close_all_timeslots(card, ch);
}

void
falc_init_e1(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
    ucchar dja = (ch ? (LIM2_DJA2|LIM2_DJA1) : 0);

    /* Switch to E1 mode (PCM 30) */
    cpc_writeb(falcbase + F_REG(FMR1, ch), 
	       cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_PMOD);

    /* Clock mode */
    if (conf->clktype == PC300_CLOCK_INT) {	/* Master mode */
	cpc_writeb(falcbase + F_REG(LIM0, ch), 
		   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_MAS); 
    } else {	/* Slave mode */
	cpc_writeb(falcbase + F_REG(LIM0, ch), 
		   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_MAS); 
    }
    cpc_writeb(falcbase + F_REG(LOOP, ch), 
	       cpc_readb(falcbase + F_REG(LOOP, ch)) & ~LOOP_SFM); 

    cpc_writeb(falcbase + F_REG(IPC, ch), IPC_SCI);
    cpc_writeb(falcbase + F_REG(FMR0, ch), 
	       cpc_readb(falcbase + F_REG(FMR0, ch)) & 
	       ~(FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1));

    switch (conf->lcode) {
	case PC300_LC_AMI:
	    cpc_writeb(falcbase + F_REG(FMR0, ch), 
		       cpc_readb(falcbase + F_REG(FMR0, ch)) | 
		       FMR0_XC1 | FMR0_RC1);
	    break;			

	case PC300_LC_HDB3:
	    cpc_writeb(falcbase + F_REG(FMR0, ch), 
		       cpc_readb(falcbase + F_REG(FMR0, ch)) | 
		       FMR0_XC0 | FMR0_XC1 | FMR0_RC0 | FMR0_RC1);
	    break;

	case PC300_LC_NRZ:
	    break;
    }

    cpc_writeb(falcbase + F_REG(LIM0, ch), 
	       cpc_readb(falcbase + F_REG(LIM0, ch)) &
	       ~(LIM0_SCL1 | LIM0_SCL0)); 
    /* Set interface mode to 2 MBPS */	
    cpc_writeb(falcbase + F_REG(FMR1, ch), 
	       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_IMOD); 

    cpc_writeb(falcbase + F_REG(XPM0, ch), 0x18);
    cpc_writeb(falcbase + F_REG(XPM1, ch), 0x03);
    cpc_writeb(falcbase + F_REG(XPM2, ch), 0x00);

    switch (conf->fr_mode) {
	case PC300_FR_MF_CRC4:
	    pfalc->multiframe_mode = 1;
	    cpc_writeb(falcbase + F_REG(FMR1, ch), 
		       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_XFS);
	    cpc_writeb(falcbase + F_REG(FMR2, ch), 
		       cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_RFS1);
	    cpc_writeb(falcbase + F_REG(FMR2, ch), 
		       cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_RFS0);
	    cpc_writeb(falcbase + F_REG(FMR3, ch), 
		       cpc_readb(falcbase + F_REG(FMR3, ch)) & ~FMR3_EXTIW);
			
	    /* MultiFrame Resynchronization */
	    cpc_writeb(falcbase + F_REG(FMR1, ch), 
		       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_MFCS);

	    /* Automatic Loss of Multiframe > 914 CRC errors */
	    cpc_writeb(falcbase + F_REG(FMR2, ch), 
		       cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_ALMF);

	    /* S1 and SI1/SI2 spare Bits set to 1 */
	    cpc_writeb(falcbase + F_REG(XSP, ch), 
		       cpc_readb(falcbase + F_REG(XSP, ch)) & ~XSP_AXS);
	    cpc_writeb(falcbase + F_REG(XSP, ch), 
		       cpc_readb(falcbase + F_REG(XSP, ch)) | XSP_EBP);
	    cpc_writeb(falcbase + F_REG(XSP, ch), 
		       cpc_readb(falcbase + F_REG(XSP, ch)) | 
		       XSP_XS13 | XSP_XS15);

	    /* Automatic Force Resynchronization */
	    cpc_writeb(falcbase + F_REG(FMR1, ch),
		       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_AFR);

	    /* Transmit Automatic Remote Alarm */
	    cpc_writeb(falcbase + F_REG(FMR2, ch),
		       cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_AXRA);

	    /* Transmit Spare Bits for National Use (Y, Sn, Sa) */
	    cpc_writeb(falcbase + F_REG(XSW, ch),
		       cpc_readb(falcbase + F_REG(XSW, ch)) |
		       XSW_XY0 | XSW_XY1 | XSW_XY2 | XSW_XY3 | XSW_XY4);
	    break;

	case PC300_FR_MF_NON_CRC4:
	case PC300_FR_D4:
	    pfalc->multiframe_mode = 0;
	    cpc_writeb(falcbase + F_REG(FMR1, ch), 
		       cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_XFS);
	    cpc_writeb(falcbase + F_REG(FMR2, ch), 
		       cpc_readb(falcbase + F_REG(FMR2, ch)) &
		       ~(FMR2_RFS1|FMR2_RFS0));
	    cpc_writeb(falcbase + F_REG(XSW, ch), 
		       cpc_readb(falcbase + F_REG(XSW, ch)) | XSW_XSIS);
	    cpc_writeb(falcbase + F_REG(XSP, ch), 
		       cpc_readb(falcbase + F_REG(XSP, ch)) | XSP_XSIF);

	    /* Automatic Force Resynchronization */
	    cpc_writeb(falcbase + F_REG(FMR1, ch),
		       cpc_readb(falcbase + F_REG(FMR1, ch)) | FMR1_AFR);

	    /* Transmit Automatic Remote Alarm */
	    cpc_writeb(falcbase + F_REG(FMR2, ch),
		       cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_AXRA);

	    /* Transmit Spare Bits for National Use (Y, Sn, Sa) */
	    cpc_writeb(falcbase + F_REG(XSW, ch),
		       cpc_readb(falcbase + F_REG(XSW, ch)) |
		       XSW_XY0 | XSW_XY1 | XSW_XY2 | XSW_XY3 | XSW_XY4);
	    break;

	case PC300_FR_UNFRAMED:
	    pfalc->multiframe_mode = 0;
	    cpc_writeb(falcbase + F_REG(FMR1, ch),
		       cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_XFS);
	    cpc_writeb(falcbase + F_REG(FMR2, ch),
		       cpc_readb(falcbase + F_REG(FMR2, ch)) &
		       ~(FMR2_RFS1|FMR2_RFS0));
	    cpc_writeb(falcbase + F_REG(XSP, ch),
		       cpc_readb(falcbase + F_REG(XSP, ch)) | XSP_TT0);
	    cpc_writeb(falcbase + F_REG(XSW, ch),
		       cpc_readb(falcbase + F_REG(XSW, ch)) &
		       ~(XSW_XTM|XSW_XY0|XSW_XY1|XSW_XY2|XSW_XY3|XSW_XY4));
	    cpc_writeb(falcbase + F_REG(TSWM, ch), 0xff);
	    cpc_writeb(falcbase + F_REG(FMR2, ch),
		       cpc_readb(falcbase + F_REG(FMR2, ch)) |
		       (FMR2_RTM|FMR2_DAIS));
	    cpc_writeb(falcbase + F_REG(FMR2, ch),
		       cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_AXRA);
	    cpc_writeb(falcbase + F_REG(FMR1, ch),
		       cpc_readb(falcbase + F_REG(FMR1, ch)) & ~FMR1_AFR);
	    pfalc->sync = 1;
	    cpc_writeb(falcbase + card->hw.cpld_reg2,
		       cpc_readb(falcbase + card->hw.cpld_reg2) |
		       (CPLD_REG2_FALC_LED2 << (2*ch)));
            break;
    }

    /* No signaling */
    cpc_writeb(falcbase + F_REG(XSP, ch), 
	       cpc_readb(falcbase + F_REG(XSP, ch)) & ~XSP_CASEN);
    cpc_writeb(falcbase + F_REG(CCR1, ch), 0);

    cpc_writeb(falcbase + F_REG(LIM1, ch), 
	       cpc_readb(falcbase + F_REG(LIM1, ch)) | LIM1_RIL0 | LIM1_RIL1); 
    cpc_writeb(falcbase + F_REG(LIM2, ch), (LIM2_LOS1 | dja));

    /* Transmit Clock-Slot Offset */
    cpc_writeb(falcbase + F_REG(XC0, ch), 
	       cpc_readb(falcbase + F_REG(XC0, ch)) | 0x01); 
    /* Transmit Time-slot Offset */
    cpc_writeb(falcbase + F_REG(XC1, ch), 0x3e);
    /* Receive  Clock-Slot offset */
    cpc_writeb(falcbase + F_REG(RC0, ch), 0x05);
    /* Receive  Time-slot offset */
    cpc_writeb(falcbase + F_REG(RC1, ch), 0x00);

    /* LOS Detection after 176 consecutive 0s */
    cpc_writeb(falcbase + F_REG(PCDR, ch), 0x0a);
    /* LOS Recovery after 22 ones in the time window of PCD */
    cpc_writeb(falcbase + F_REG(PCRR, ch), 0x15);

    cpc_writeb(falcbase + F_REG(IDLE, ch), 0x7f);

    falc_close_all_timeslots(card, ch);
}

void
falc_init_hdlc(pc300_t *card, int ch)
{
    uclong falcbase = card->hw.falcbase;
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;

    /* Enable transparent data transfer */
    if (conf->fr_mode == PC300_FR_UNFRAMED) {
	cpc_writeb(falcbase + F_REG(MODE, ch), 0);
    } else {
	cpc_writeb(falcbase + F_REG(MODE, ch),
		   cpc_readb(falcbase + F_REG(MODE, ch)) |
		   (MODE_HRAC|MODE_MDS2));
	cpc_writeb(falcbase + F_REG(RAH2, ch), 0xff);
	cpc_writeb(falcbase + F_REG(RAH1, ch), 0xff);
	cpc_writeb(falcbase + F_REG(RAL2, ch), 0xff);
	cpc_writeb(falcbase + F_REG(RAL1, ch), 0xff);
    }

    /* Tx/Rx reset  */
    falc_issue_cmd(card, ch, CMDR_RRES | CMDR_XRES | CMDR_SRES);  

    /* Enable interrupt sources */
    falc_intr_enable(card, ch);
}

void 
te_config(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
    ucchar dummy;
    unsigned long flags;

    memset(pfalc, 0, sizeof(falc_t));
    switch (conf->media) {
	case LINE_T1:
	    pfalc->num_channels = NUM_OF_T1_CHANNELS;
	    pfalc->offset = 1;
	    break;
	case LINE_E1:
	    pfalc->num_channels = NUM_OF_E1_CHANNELS;
	    pfalc->offset = 0;
	    break;
    }
    if (conf->tslot_bitmap == 0xffffffffUL)
	pfalc->full_bandwidth = 1;
    else
	pfalc->full_bandwidth = 0;

    CPC_LOCK(card, flags);
    /* Reset the FALC chip */
    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
	       cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) | 
	       (CPLD_REG1_FALC_RESET << (2*ch)));
    udelay(10000);
    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
	       cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) & 
	       ~(CPLD_REG1_FALC_RESET << (2*ch)));

    if (conf->media == LINE_T1) {
	falc_init_t1(card, ch);
    } else {
	falc_init_e1(card, ch);
    }
    falc_init_hdlc(card, ch);
    if (conf->rx_sens == PC300_RX_SENS_SH) {
	cpc_writeb(falcbase + F_REG(LIM0, ch),
		   cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_EQON);
    } else {
	cpc_writeb(falcbase + F_REG(LIM0, ch),
		   cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_EQON);
    }
    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
	       cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) | 
	       ((CPLD_REG2_FALC_TX_CLK | CPLD_REG2_FALC_RX_CLK) << (2*ch)));

    /* Clear all interrupt registers */
    dummy = cpc_readb(falcbase + F_REG(FISR0, ch)) +
	    cpc_readb(falcbase + F_REG(FISR1, ch)) +
	    cpc_readb(falcbase + F_REG(FISR2, ch)) +
	    cpc_readb(falcbase + F_REG(FISR3, ch));
    CPC_UNLOCK(card, flags);
} 

void 
falc_check_status (pc300_t *card, int ch, unsigned char frs0)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;

    if (conf->fr_mode != PC300_FR_UNFRAMED) {
	/* Verify AIS alarm */
	if (frs0 & FRS0_AIS) {
	    if (!pfalc->blue_alarm) {
		pfalc->blue_alarm = 1;
		pfalc->ais++;
		// EVENT_AIS
		if (conf->media == LINE_T1) {
		    /* Disable this interrupt as it may otherwise interfere with
		       other working boards. */
		    cpc_writeb(falcbase + F_REG(IMR0, ch), 
		    	       cpc_readb(falcbase + F_REG(IMR0, ch)) | 
			       IMR0_PDEN);
		}
		falc_disable_comm(card, ch);
		// EVENT_AIS
	    }
	} else {
	    pfalc->blue_alarm = 0;
 	} 

	/* Verify LOS */
	if (frs0 & FRS0_LOS) {
	    if (!pfalc->red_alarm) {
		pfalc->red_alarm = 1;
		pfalc->los++;
		if (!pfalc->blue_alarm) {
		    // EVENT_FALC_ABNORMAL
		    if (conf->media == LINE_T1) {
			/* Disable this interrupt as it may otherwise interfere 
			   with other working boards. */
			cpc_writeb(falcbase + F_REG(IMR0, ch), 
				   cpc_readb(falcbase + F_REG(IMR0, ch)) | 
				   IMR0_PDEN);
		    }
		    falc_disable_comm(card, ch);
		    // EVENT_FALC_ABNORMAL
		}
	    }
	} else {
	    if (pfalc->red_alarm) {
		pfalc->red_alarm = 0;
		pfalc->losr++;
	    }
	}

	/* Verify LFA */
	if (frs0 & FRS0_LFA) {
	    if (!pfalc->loss_fa) {
		pfalc->loss_fa = 1;
		pfalc->lfa++;
		if (!pfalc->blue_alarm && !pfalc->red_alarm) {
		    // EVENT_FALC_ABNORMAL
		    if (conf->media == LINE_T1) {
			/* Disable this interrupt as it may otherwise 
			   interfere with other working boards. */
			cpc_writeb(falcbase + F_REG(IMR0, ch), 
				   cpc_readb(falcbase + F_REG(IMR0, ch)) | 
				   IMR0_PDEN);
		    }
		    falc_disable_comm(card, ch);
		    // EVENT_FALC_ABNORMAL
		}
	    }
	} else {
	    if (pfalc->loss_fa) {
		pfalc->loss_fa = 0;
		pfalc->farec++;
	    }
	}
									
	/* Verify LMFA */
	if (pfalc->multiframe_mode && (frs0 & FRS0_LMFA)) { 
	    /* D4 or CRC4 frame mode */
	    if (!pfalc->loss_mfa) {
		pfalc->loss_mfa = 1;
		pfalc->lmfa++;
		if (!pfalc->blue_alarm && !pfalc->red_alarm && 
		    !pfalc->loss_fa) {
		    // EVENT_FALC_ABNORMAL
		    if (conf->media == LINE_T1) {
			/* Disable this interrupt as it may otherwise 
			   interfere with other working boards. */
			cpc_writeb(falcbase + F_REG(IMR0, ch), 
				   cpc_readb(falcbase + F_REG(IMR0, ch)) | 
				   IMR0_PDEN);
		    }
		    falc_disable_comm(card, ch);
		    // EVENT_FALC_ABNORMAL
		}
	    }
	} else {
	    pfalc->loss_mfa = 0;
	}

	if (pfalc->red_alarm || pfalc->loss_fa || 
	    pfalc->loss_mfa || pfalc->blue_alarm) {
	    if (pfalc->sync) {
		pfalc->sync = 0;
		chan->d.line_off++;
		cpc_writeb(falcbase + card->hw.cpld_reg2,
			   cpc_readb(falcbase + card->hw.cpld_reg2) & 
			   ~(CPLD_REG2_FALC_LED2 << (2*ch)));
	    }
	} else {
	    if (!pfalc->sync) {
		pfalc->sync = 1;
		chan->d.line_on++;
		cpc_writeb(falcbase + card->hw.cpld_reg2,
			   cpc_readb(falcbase + card->hw.cpld_reg2) |
			   (CPLD_REG2_FALC_LED2 << (2*ch)));
	    }
	}

 	/* Verify Remote Alarm */
	if (frs0 & FRS0_RRA) {
	    if (!pfalc->yellow_alarm) {
		pfalc->yellow_alarm = 1;
		pfalc->rai++;
		if (pfalc->sync) {
		    // EVENT_RAI
		    falc_disable_comm(card, ch);
		    // EVENT_RAI
		}
	    }
	} else {
	    pfalc->yellow_alarm = 0;
	}
    } /* if !PC300_UNFRAMED */

    if (pfalc->sync && !pfalc->yellow_alarm) {
	if (!pfalc->active) {
	    // EVENT_FALC_NORMAL
	    if (pfalc->loop_active) {
		return;
	    }
	    if (conf->media == LINE_T1) {
		cpc_writeb(falcbase + F_REG(IMR0, ch), 
			   cpc_readb(falcbase + F_REG(IMR0, ch)) & ~IMR0_PDEN);
	    }
	    falc_enable_comm(card, ch);
	    // EVENT_FALC_NORMAL
	    pfalc->active = 1;
	}
    } else {
	if (pfalc->active) {
	    pfalc->active = 0;
	}
    }
}

void 
falc_update_stats(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
    ucshort counter;

    counter = cpc_readb(falcbase + F_REG(FECL, ch));
    counter |= cpc_readb(falcbase + F_REG(FECH, ch)) << 8;
    pfalc->fec += counter;

    counter = cpc_readb(falcbase + F_REG(CVCL, ch));
    counter |= cpc_readb(falcbase + F_REG(CVCH, ch)) << 8;
    pfalc->cvc += counter;

    counter = cpc_readb(falcbase + F_REG(CECL, ch));
    counter |= cpc_readb(falcbase + F_REG(CECH, ch)) << 8;
    pfalc->cec += counter;

    counter = cpc_readb(falcbase + F_REG(EBCL, ch));
    counter |= cpc_readb(falcbase + F_REG(EBCH, ch)) << 8;
    pfalc->ebc += counter;

    if (cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_EPRM) {
	mdelay (10);
	counter = cpc_readb(falcbase + F_REG(BECL, ch));
	counter |= cpc_readb(falcbase + F_REG(BECH, ch)) << 8;
	pfalc->bec += counter;

	if (((conf->media == LINE_T1) && 
	     (cpc_readb(falcbase + F_REG(FRS1, ch)) & FRS1_LLBAD) && 
	     (!(cpc_readb(falcbase + F_REG(FRS1, ch)) & FRS1_PDEN))) 
	    ||
	    ((conf->media == LINE_E1) && 
	     (cpc_readb(falcbase + F_REG(RSP, ch)) & RSP_LLBAD))) {
	    pfalc->prbs = 2;
	} else {
	    pfalc->prbs = 1;
	}
    }
}

/*----------------------------------------------------------------------------
 * falc_remote_loop
 *----------------------------------------------------------------------------
 * Description:	In the remote loopback mode the clock and data recovered
 *		from the line inputs RL1/2 or RDIP/RDIN are routed back
 *		to the line outputs XL1/2 or XDOP/XDON via the analog
 *		transmitter. As in normal mode they are processsed by
 *		the synchronizer and then sent to the system interface.
 *----------------------------------------------------------------------------
 */
void 
falc_remote_loop(pc300_t *card, int ch, int loop_on)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;

    if (loop_on) {
	// EVENT_FALC_ABNORMAL
	if (conf->media == LINE_T1) {
	    /* Disable this interrupt as it may otherwise interfere with 
	       other working boards. */
	    cpc_writeb(falcbase + F_REG(IMR0, ch), 
		       cpc_readb(falcbase + F_REG(IMR0, ch)) | IMR0_PDEN);
	}
	falc_disable_comm(card, ch);
	// EVENT_FALC_ABNORMAL
	cpc_writeb(falcbase + F_REG(LIM1, ch), 
		   cpc_readb(falcbase + F_REG(LIM1, ch)) | LIM1_RL);
	pfalc->loop_active = 1;
    } else {
	cpc_writeb(falcbase + F_REG(LIM1, ch), 
		   cpc_readb(falcbase + F_REG(LIM1, ch)) & ~LIM1_RL);
	pfalc->sync = 0;
	cpc_writeb(falcbase + card->hw.cpld_reg2,
		   cpc_readb(falcbase + card->hw.cpld_reg2) & 
		   ~(CPLD_REG2_FALC_LED2 << (2*ch)));
	pfalc->active = 0;
	falc_issue_cmd(card, ch, CMDR_XRES);
	pfalc->loop_active = 0;
    }
}

/*----------------------------------------------------------------------------
 * falc_local_loop
 *----------------------------------------------------------------------------
 * Description: The local loopback mode disconnects the receive lines 
 *		RL1/RL2 resp. RDIP/RDIN from the receiver. Instead of the
 *		signals coming from the line the data provided by system
 *		interface are routed through the analog receiver back to
 *		the system interface. The unipolar bit stream will be
 *		undisturbed transmitted on the line. Receiver and transmitter
 *		coding must be identical.
 *----------------------------------------------------------------------------
 */
void 
falc_local_loop(pc300_t *card, int ch, int loop_on)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
	
	if (loop_on) {
	    cpc_writeb(falcbase + F_REG(LIM0, ch), 
			cpc_readb(falcbase + F_REG(LIM0, ch)) | LIM0_LL);
	    pfalc->loop_active = 1;
	}
	else {
	    cpc_writeb(falcbase + F_REG(LIM0, ch), 
			cpc_readb(falcbase + F_REG(LIM0, ch)) & ~LIM0_LL);
	    pfalc->loop_active = 0;
	}
}

/*----------------------------------------------------------------------------
 * falc_payload_loop
 *----------------------------------------------------------------------------
 * Description: This routine allows to enable/disable payload loopback.
 *		When the payload loop is activated, the received 192 bits
 *		of payload data will be looped back to the transmit
 *		direction. The framing bits, CRC6 and DL bits are not 
 *		looped. They are originated by the FALC-LH transmitter.
 *----------------------------------------------------------------------------
 */
void 
falc_payload_loop(pc300_t *card, int ch, int loop_on)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
	
    if (loop_on) {
	// EVENT_FALC_ABNORMAL
	if (conf->media == LINE_T1) {
	    /* Disable this interrupt as it may otherwise interfere with 
	       other working boards. */
	    cpc_writeb(falcbase + F_REG(IMR0, ch), 
		       cpc_readb(falcbase + F_REG(IMR0, ch)) | IMR0_PDEN);
	}
	falc_disable_comm(card, ch);
	// EVENT_FALC_ABNORMAL
	cpc_writeb(falcbase + F_REG(FMR2, ch), 
		   cpc_readb(falcbase + F_REG(FMR2, ch)) | FMR2_PLB);
	if (conf->media == LINE_T1) {
	    cpc_writeb(falcbase + F_REG(FMR4, ch), 
		   cpc_readb(falcbase + F_REG(FMR4, ch)) | FMR4_TM);
	} else {
	    cpc_writeb(falcbase + F_REG(FMR5, ch), 
		   cpc_readb(falcbase + F_REG(FMR5, ch)) | XSP_TT0);
	}
	falc_open_all_timeslots(card, ch);
	pfalc->loop_active = 2;
    } else {
	cpc_writeb(falcbase + F_REG(FMR2, ch), 
		cpc_readb(falcbase + F_REG(FMR2, ch)) & ~FMR2_PLB);
	if (conf->media == LINE_T1) {
	    cpc_writeb(falcbase + F_REG(FMR4, ch), 
		   cpc_readb(falcbase + F_REG(FMR4, ch)) & ~FMR4_TM);
	} else {
	    cpc_writeb(falcbase + F_REG(FMR5, ch), 
		   cpc_readb(falcbase + F_REG(FMR5, ch)) & ~XSP_TT0);
	}
	pfalc->sync = 0;
	cpc_writeb(falcbase + card->hw.cpld_reg2,
		   cpc_readb(falcbase + card->hw.cpld_reg2) & 
		   ~(CPLD_REG2_FALC_LED2 << (2*ch)));
	pfalc->active = 0;
	falc_issue_cmd(card, ch, CMDR_XRES);
	pfalc->loop_active = 0;
    }
}

/*----------------------------------------------------------------------------
 * turn_off_xlu
 *----------------------------------------------------------------------------
 * Description:	Turns XLU bit off in the proper register
 *----------------------------------------------------------------------------
 */
void
turn_off_xlu(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    uclong falcbase = card->hw.falcbase;

    if (conf->media == LINE_T1) {
	cpc_writeb(falcbase + F_REG(FMR5, ch), 
		cpc_readb(falcbase + F_REG(FMR5, ch)) & ~FMR5_XLU);
    } else {
	cpc_writeb(falcbase + F_REG(FMR3, ch), 
		cpc_readb(falcbase + F_REG(FMR3, ch)) & ~FMR3_XLU);
    }
}

/*----------------------------------------------------------------------------
 * turn_off_xld
 *----------------------------------------------------------------------------
 * Description: Turns XLD bit off in the proper register
 *----------------------------------------------------------------------------
 */
void 
turn_off_xld (pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    uclong falcbase = card->hw.falcbase;

    if (conf->media == LINE_T1) {
	cpc_writeb(falcbase + F_REG(FMR5, ch), 
		cpc_readb(falcbase + F_REG(FMR5, ch)) & ~FMR5_XLD);
    } else {
	cpc_writeb(falcbase + F_REG(FMR3, ch), 
		cpc_readb(falcbase + F_REG(FMR3, ch)) & ~FMR3_XLD);
    }
}

/*----------------------------------------------------------------------------
 * falc_generate_loop_up_code
 *----------------------------------------------------------------------------
 * Description:	This routine writes the proper FALC chip register in order
 *		to generate a LOOP activation code over a T1/E1 line.
 *----------------------------------------------------------------------------
 */
void 
falc_generate_loop_up_code (pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;

    if (conf->media == LINE_T1) {
	cpc_writeb(falcbase + F_REG(FMR5, ch), 
		cpc_readb(falcbase + F_REG(FMR5, ch)) | FMR5_XLU);
    } else {
	cpc_writeb(falcbase + F_REG(FMR3, ch), 
		cpc_readb(falcbase + F_REG(FMR3, ch)) | FMR3_XLU);
    }
    // EVENT_FALC_ABNORMAL
    if (conf->media == LINE_T1) {
	/* Disable this interrupt as it may otherwise interfere with 
	   other working boards. */
	cpc_writeb(falcbase + F_REG(IMR0, ch), 
	       cpc_readb(falcbase + F_REG(IMR0, ch)) | IMR0_PDEN);
    }
    falc_disable_comm(card, ch);
    // EVENT_FALC_ABNORMAL
    pfalc->loop_gen = 1;
}

/*----------------------------------------------------------------------------
 * falc_generate_loop_down_code
 *----------------------------------------------------------------------------
 * Description:	This routine writes the proper FALC chip register in order
 *		to generate a LOOP deactivation code over a T1/E1 line.
 *----------------------------------------------------------------------------
 */
void 
falc_generate_loop_down_code(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;

    if (conf->media == LINE_T1) {
	cpc_writeb(falcbase + F_REG(FMR5, ch), 
		cpc_readb(falcbase + F_REG(FMR5, ch)) | FMR5_XLD);
    } else {
	cpc_writeb(falcbase + F_REG(FMR3, ch), 
		cpc_readb(falcbase + F_REG(FMR3, ch)) | FMR3_XLD);
    }
    pfalc->sync = 0;
    cpc_writeb(falcbase + card->hw.cpld_reg2,
	   cpc_readb(falcbase + card->hw.cpld_reg2) & 
	   ~(CPLD_REG2_FALC_LED2 << (2*ch)));
    pfalc->active = 0;
//?    falc_issue_cmd(card, ch, CMDR_XRES);
    pfalc->loop_gen = 0;
}

/*----------------------------------------------------------------------------
 * falc_pattern_test
 *----------------------------------------------------------------------------
 * Description:	This routine generates a pattern code and checks
 *		it on the reception side.
 *----------------------------------------------------------------------------
 */
void
falc_pattern_test(pc300_t *card, int ch, unsigned int activate)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
	
    if (activate) {
	pfalc->prbs = 1;
	pfalc->bec = 0;
	if (conf->media == LINE_T1) {
	    /* Disable local loop activation/deactivation detect */
	    cpc_writeb(falcbase + F_REG(IMR3, ch), 
		cpc_readb(falcbase + F_REG(IMR3, ch)) | IMR3_LLBSC);
	} else {
	    /* Disable local loop activation/deactivation detect */
	    cpc_writeb(falcbase + F_REG(IMR1, ch), 
		cpc_readb(falcbase + F_REG(IMR1, ch)) | IMR1_LLBSC);
	}
	/* Activates generation and monitoring of PRBS 
	 * (Pseudo Random Bit Sequence) */
	cpc_writeb(falcbase + F_REG(LCR1, ch), 
	    cpc_readb(falcbase + F_REG(LCR1, ch)) | LCR1_EPRM | LCR1_XPRBS);
    } else {
	pfalc->prbs = 0;
	/* Deactivates generation and monitoring of PRBS 
	 * (Pseudo Random Bit Sequence) */
	cpc_writeb(falcbase + F_REG(LCR1, ch), 
	    cpc_readb(falcbase + F_REG(LCR1, ch)) & ~(LCR1_EPRM | LCR1_XPRBS));
	if (conf->media == LINE_T1) {
	    /* Enable local loop activation/deactivation detect */
	    cpc_writeb(falcbase + F_REG(IMR3, ch), 
		cpc_readb(falcbase + F_REG(IMR3, ch)) & ~IMR3_LLBSC);
	} else {
	    /* Enable local loop activation/deactivation detect */
	    cpc_writeb(falcbase + F_REG(IMR1, ch), 
		cpc_readb(falcbase + F_REG(IMR1, ch)) & ~IMR1_LLBSC);
	}
    }
}

/*----------------------------------------------------------------------------
 * falc_pattern_test_error
 *----------------------------------------------------------------------------
 * Description:	This routine returns the bit error counter value
 *----------------------------------------------------------------------------
 */
ucshort falc_pattern_test_error(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;
	
    return (pfalc->bec);
}

/**********************************/
/***   Net Interface Routines   ***/
/**********************************/

static void 
cpc_trace (struct device *dev, struct sk_buff *skb_main, char rx_tx)
{
    struct sk_buff *skb;

    if ((skb = dev_alloc_skb(10 + skb_main->len)) == NULL) {
	printk("%s: out of memory\n", dev->name);
	return;
    }
    skb_put (skb, 10 + skb_main->len);
    
    skb->dev = dev;
    skb->protocol = htons(ETH_P_CUST);
    skb->mac.raw  = skb->data;
    skb->pkt_type = PACKET_HOST;
    skb->len = 10 + skb_main->len;

    memcpy(&skb->data[0], dev->name, 5);
    skb->data[5] = '[';
    skb->data[6] = rx_tx;
    skb->data[7] = ']';
    skb->data[8] = ':';
    skb->data[9] = ' ';
    memcpy(&skb->data[10], skb_main->data, skb_main->len);

    netif_rx(skb);
}

int
cpc_queue_xmit(struct sk_buff *skb, struct device *dev)
{
    pc300dev_t *d = (pc300dev_t *)dev->priv;
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    struct net_device_stats *stats = &d->hdlc->stats;
    int ch = chan->channel;
    volatile pcsca_bd_t *ptdescr;
    uclong flags;
#ifdef PC300_DEBUG_TX
    int i;
#endif

    if (chan->conf.monitor) {
	/* In monitor mode no Tx is done: ignore packet */
	dev_kfree_skb(skb);
	return 0;
    } else if (!(dev->flags & IFF_RUNNING)) {
	/* DCD must be OFF: drop packet */
	dev_kfree_skb(skb);
	stats->tx_errors++;
	stats->tx_carrier_errors++;
	return 0;
    } else if (cpc_readb(card->hw.scabase + M_REG(ST3, ch)) & ST3_DCD) { 
	printk("%s: DCD is OFF. Going admnistrative down.\n", dev->name);
	stats->tx_errors++;
	stats->tx_carrier_errors++;
	dev_kfree_skb(skb);
	dev->flags &= ~IFF_RUNNING;
	CPC_LOCK(card, flags);
	if (d->tx_skb) {
	    dev_kfree_skb(d->tx_skb);
	    d->tx_skb = NULL;
	}
	cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_TX_BUF_CLR);
	if (card->hw.type == PC300_TE) {
	    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
		       cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) &
		       ~(CPLD_REG2_FALC_LED1 << (2*ch)));
	}
	dev->tbusy = 0;
	CPC_UNLOCK(card, flags);
	return 0;
    }
    
    if (dev->tbusy) {
	ucchar ilar;

	if (time_before(jiffies, dev->trans_start + PC300_TX_TIMEOUT))
	    return 1;

	stats->tx_errors++;
	stats->tx_aborted_errors++;
	printk("%s: transmit timed out, restarting channel.", dev->name);
	CPC_LOCK(card, flags);
	if ((ilar = cpc_readb(card->hw.scabase + ILAR)) != 0) {
	    printk("(ILAR=0x%x)", ilar);
	    cpc_writeb(card->hw.scabase + ILAR, ilar);
	    cpc_writeb(card->hw.scabase + DMER, 0x80);
	}
	printk("\n");
	cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_TX_BUF_CLR);
	if (d->tx_skb) {
	    dev_kfree_skb(d->tx_skb);
	    d->tx_skb = NULL;
	}
	if (card->hw.type == PC300_TE) {
	    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
		       cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) &
		       ~(CPLD_REG2_FALC_LED1 << (2*ch)));
	}
	CPC_UNLOCK(card, flags);
	dev->tbusy = 0;
    }
    if (test_and_set_bit(0, (void*)&dev->tbusy)) {
	printk("%s: transmitter access conflict.\n", dev->name);
	return 1;
    }

    /* Clean up descriptors from previous transmission */
    while (chan->tx_first_bd != chan->tx_next_bd) {
	ptdescr = (pcsca_bd_t *)
		   (card->hw.rambase + TX_BD_ADDR(ch, chan->tx_first_bd));
	cpc_writeb(&ptdescr->status, 0);
	chan->tx_first_bd = (chan->tx_first_bd + 1) & (N_DMA_TX_BUF - 1);
    }
    /* Clean up next free descriptor to avoid race problems with timeout
       conditions */
    ptdescr = (pcsca_bd_t *)
		(card->hw.rambase + TX_BD_ADDR(ch, chan->tx_next_bd));
    cpc_writeb(&ptdescr->status, 0);

    /* Write buffer to DMA buffers */
    if(dma_buf_write(card, ch, (ucchar *)skb->data, skb->len) != 0) {
	CPC_LOCK(card, flags);
//	printk("%s: write error. Dropping TX packet.\n", dev->name);
	dev->tbusy = 0;
	CPC_UNLOCK(card, flags);
	stats->tx_dropped++;
	return 1;
    }

#ifdef PC300_DEBUG_TX
    printk("%s T:", dev->name);
    for(i = 0 ; i < skb->len ; i++)
	printk(" %02x", *(skb->data + i));
    printk("\n");
#endif

    if (d->trace_on) {
	cpc_trace (dev, skb, 'T');
    }
    d->tx_skb = skb;
    dev->trans_start = jiffies;

    /* Start transmission */
    CPC_LOCK(card, flags);
    cpc_writel(card->hw.scabase + DTX_REG(EDAL, ch), 
	       TX_BD_ADDR(ch, chan->tx_next_bd));
    cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_TX_ENA);
    cpc_writeb(card->hw.scabase + DSR_TX(ch), DSR_DE);
    if (card->hw.type == PC300_TE) {
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) |
		   (CPLD_REG2_FALC_LED1 << (2*ch)));
    }
    CPC_UNLOCK(card, flags);

    return 0;
}

void 
cpc_net_rx(hdlc_device *hdlc)
{
    struct device *dev = hdlc_to_dev(hdlc);
    pc300dev_t *d = (pc300dev_t *)dev->priv;
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    struct net_device_stats *stats = &hdlc->stats;
    int ch = chan->channel;
#ifdef PC300_DEBUG_RX
    int i;
#endif
    int rxb;
    struct sk_buff *skb;

    while (1) {
	if ((rxb = dma_get_rx_frame_size(card, ch)) == -1)
	    return;
	
	if (rxb > (dev->mtu + 40)) {
	    printk("%s : MTU exceeded %d\n", dev->name, rxb); 
	    skb = NULL;
	} else {
	    skb = dev_alloc_skb(rxb);
	    if (skb == NULL) {
		printk("%s: Memory squeeze!!\n", dev->name);
	 	return;
	    }
	    skb->dev = dev;
	}

	if(((rxb = dma_buf_read(card, ch, skb)) <= 0) || (skb == NULL)) {
#ifdef PC300_DEBUG_RX
	    printk("%s: rxb = %x\n", dev->name, rxb);
#endif
	    if ((skb == NULL) && (rxb >= 0)) {
		/* rxb > dev->mtu */
		stats->rx_errors++;
		stats->rx_length_errors++;
		continue;
	    }

	    if (rxb < 0) {	/* Invalid frame */
		rxb = -rxb;
		if (rxb & DST_OVR) {
		    stats->rx_errors++;
		    stats->rx_fifo_errors++;
		}
		if (rxb & DST_CRC) {
		    stats->rx_errors++;
		    stats->rx_crc_errors++;
		}
		if (rxb & (DST_RBIT | DST_SHRT | DST_ABT)) {
		    stats->rx_errors++;
		    stats->rx_frame_errors++;
		}
	    }
	    if (skb) {
		dev_kfree_skb(skb);
	    }
	    continue;
	}

#ifdef PC300_DEBUG_RX
	printk("%s R:", dev->name);
	for(i = 0 ; i < skb->len ; i++)
	    printk(" %02x", *(skb->data + i));
	printk("\n");
#endif
	if (d->trace_on) {
	    cpc_trace (dev, skb, 'R');
	}

	switch(hdlc->mode & ~MODE_SOFT) {
#ifdef CONFIG_PC300_X25
	    case MODE_X25:
	    {
		int err;

		skb->protocol=htons(ETH_P_X25);
		skb->mac.raw=skb->data;
		skb->dev=hdlc_to_dev(d->hdlc);

		/* Send it to the upper layer */
		if ((err = lapb_data_received(d, skb)) != LAPB_OK) {
		    printk("%s: lapb_data_received err - %d\n", dev->name, err);
		    dev_kfree_skb(skb);
		}
		stats->rx_bytes += rxb;
		stats->rx_packets++;
		break;
	    }
#endif /* CONFIG_PC300_X25 */
	    default:
		hdlc_netif_rx(hdlc, skb, 0);
		break;
	}
    }
}

#ifdef CONFIG_PC300_X25
/*********************************/
/***   X.25 Support Routines   ***/
/*********************************/
int 
cpc_x25_packetlayer_xmit(struct sk_buff *skb, struct device *dev)
{
    pc300dev_t *d = (pc300dev_t *)dev->priv;
    int err;
	
    switch (skb->data[0]) {
	case 0x00:
	    break;

	case 0x01:
	    if ((err = lapb_connect_request(d)) != LAPB_OK) {
		if (err == LAPB_CONNECTED) {
		    /* Send connect confirm. msg to level 3 */
		    cpc_lapb_connected(d, 0);
		} else {
		    printk("%s: lapb_connect_request error - %d\n",
			   dev->name, err);
		}
	    }
	    dev_kfree_skb(skb);
	    return 0;

	case 0x02:
	    if ((err = lapb_disconnect_request(d)) != LAPB_OK) {
		if (err == LAPB_NOTCONNECTED) {
		    /* Send disconnect confirm. msg to level 3 */
		    cpc_lapb_disconnected(d, 0);
		} else {
		    printk("%s: lapb_disconnect_request error - %d\n",
			   dev->name, err);
		}
	    }
	    dev_kfree_skb(skb);
	    return 0;
			
	default:
	    dev_kfree_skb(skb);
	    return 0;
    }

    skb_pull(skb, 1);

    if ((err = lapb_data_request(d, skb)) != LAPB_OK) {
	printk("%s: lapb_data_request error - %d\n", dev->name, err);
	dev_kfree_skb(skb);
	return -ENOMEM;
    }

    return 0;
}

void 
cpc_lapb_connected(void *token, int reason)
{
    pc300dev_t *d = (pc300dev_t *)token;
    struct sk_buff *skb;
    unsigned char *ptr;

    if ((skb = dev_alloc_skb(1)) == NULL) {
	printk("%s: out of memory\n", d->name);
	return;
    }

    ptr  = skb_put(skb, 1);
    *ptr = 0x01;

    skb->dev      = hdlc_to_dev(d->hdlc);
    skb->protocol = htons(ETH_P_X25);
    skb->mac.raw  = skb->data;
    skb->pkt_type = PACKET_HOST;

    netif_rx(skb);
}

void 
cpc_lapb_disconnected(void *token, int reason)
{
    pc300dev_t *d = (pc300dev_t *)token;
    struct sk_buff *skb;
    unsigned char *ptr;

    if ((skb = dev_alloc_skb(1)) == NULL) {
	printk("%s: out of memory\n", d->name);
	return;
    }

    ptr  = skb_put(skb, 1);
    *ptr = 0x02;

    skb->dev      = hdlc_to_dev(d->hdlc);
    skb->protocol = htons(ETH_P_X25);
    skb->mac.raw  = skb->data;
    skb->pkt_type = PACKET_HOST;

    netif_rx(skb);
}

void 
cpc_lapb_data_indication(void *token, struct sk_buff *skb)
{
    pc300dev_t *d = (pc300dev_t *)token;
    unsigned char *ptr;

    ptr  = skb_push(skb, 1);
    *ptr = 0x00;

    skb->dev      = hdlc_to_dev(d->hdlc);
    skb->protocol = htons(ETH_P_X25);
    skb->mac.raw  = skb->data;
    skb->pkt_type = PACKET_HOST;

    netif_rx(skb);
}

void 
cpc_lapb_data_transmit(void *token, struct sk_buff *skb)
{
    pc300dev_t *d = (pc300dev_t *)token;
    struct device *dev = hdlc_to_dev(d->hdlc);

    cpc_queue_xmit (skb, dev);
}
#endif /* CONFIG_PC300_X25 */

/************************************/
/***   PC300 Interrupt Routines   ***/
/************************************/
static void 
sca_intr(pc300_t *card)
{
    uclong scabase = card->hw.scabase;
    volatile uclong status;
    int ch;
    int intr_count = 0;

    while ((status = cpc_readl(scabase + ISR0)) != 0) {
	for (ch = 0 ; ch < card->hw.nchan ; ch++) {
	    pc300ch_t *chan = &card->chan[ch];
	    pc300dev_t *d = &chan->d;
	    hdlc_device *hdlc = d->hdlc;
	    struct device *dev = hdlc_to_dev(hdlc);

	    spin_lock(&card->card_lock);
	    dev->interrupt = 1;

	    /**** Reception ****/
	    if (status & IR0_DRX((IR0_DMIA|IR0_DMIB), ch)) {
		ucchar drx_stat = cpc_readb(scabase + DSR_RX(ch));

		/* Clear RX interrupts */
		cpc_writeb(scabase + DSR_RX(ch), drx_stat | DSR_DWE);

#ifdef PC300_DEBUG_INTR
	        printk("sca_intr: RX intr chan[%d] (st=0x%08lx, dsr=0x%02x)\n", 
		       ch, status, drx_stat);
#endif
		if (status & IR0_DRX(IR0_DMIA, ch)) {
		    if (drx_stat & DSR_BOF) {
			if ((cpc_readb(scabase + DSR_RX(ch)) & DSR_DE)) {
			    rx_dma_stop(card, ch);
			}
			cpc_net_rx(hdlc);
			/* Discard invalid frames */
			hdlc->stats.rx_errors++;
			hdlc->stats.rx_over_errors++;
			chan->rx_first_bd = 0;
			chan->rx_last_bd = N_DMA_RX_BUF - 1;
			rx_dma_start(card, ch);
		    }
		}
		if (status & IR0_DRX(IR0_DMIB, ch)) {
		    if (drx_stat & DSR_EOM) {
			if (card->hw.type == PC300_TE) {
			    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
				       cpc_readb(card->hw.falcbase + 
						 card->hw.cpld_reg2)
				       | (CPLD_REG2_FALC_LED1 << (2*ch)));
			}
			cpc_net_rx(hdlc);
			if (card->hw.type == PC300_TE) {
			    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
				       cpc_readb(card->hw.falcbase + 
						 card->hw.cpld_reg2)
				       & ~(CPLD_REG2_FALC_LED1 << (2*ch)));
			}
		    }
		}
	    }

	    /**** Transmission ****/
	    if (status & IR0_DTX((IR0_EFT|IR0_DMIA|IR0_DMIB), ch)) {
		ucchar dtx_stat = cpc_readb(scabase + DSR_TX(ch));

		/* Clear TX interrupts */
		cpc_writeb(scabase + DSR_TX(ch), dtx_stat | DSR_DWE);

#ifdef PC300_DEBUG_INTR
	        printk("sca_intr: TX intr chan[%d] (st=0x%08lx, dsr=0x%02x)\n", 
		       ch, status, dtx_stat);
#endif
		if (status & IR0_DTX(IR0_EFT, ch)) {
		    if (dtx_stat & DSR_UDRF) {
			if (cpc_readb(scabase + M_REG(TBN, ch)) != 0) {
			    cpc_writeb(scabase + M_REG(CMD, ch), 
				CMD_TX_BUF_CLR);
			}
			if (card->hw.type == PC300_TE) {
			    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
				       cpc_readb(card->hw.falcbase + 
						 card->hw.cpld_reg2)
				       & ~(CPLD_REG2_FALC_LED1 << (2*ch)));
			}
			if (d->tx_skb) {
			    struct sk_buff *skb = d->tx_skb;

			    dev_kfree_skb(skb);
			    d->tx_skb = NULL;
			    hdlc->stats.tx_errors++;
			    hdlc->stats.tx_fifo_errors++;
			    /* Tell the upper layer we are ready to transmit
			       more packets */
			    dev->tbusy = 0;
			    mark_bh(NET_BH);
			}
		    }
		}
		if (status & IR0_DTX(IR0_DMIA, ch)) {
		    if (dtx_stat & DSR_BOF) {
		    }
		}
		if (status & IR0_DTX(IR0_DMIB, ch)) {
		    if (dtx_stat & DSR_EOM) {
			if (card->hw.type == PC300_TE) {
			    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
				       cpc_readb(card->hw.falcbase + 
						 card->hw.cpld_reg2)
				       & ~(CPLD_REG2_FALC_LED1 << (2*ch)));
			}
			if (d->tx_skb) {
			    struct sk_buff *skb = d->tx_skb;

			    dev_kfree_skb(skb);
			    d->tx_skb = NULL;
			    hdlc->stats.tx_bytes += skb->len;
			    hdlc->stats.tx_packets++;
			    /* Tell the upper layer we are ready to transmit
			       more packets */
			    dev->tbusy = 0;
			    mark_bh(NET_BH);
			}
		    }
		}
	    }
	    
	    /**** MSCI ****/
	    if (status & IR0_M(IR0_RXINTA, ch)) {
		ucchar st1 = cpc_readb(scabase + M_REG(ST1, ch));

		/* Clear MSCI interrupts */
		cpc_writeb(scabase + M_REG(ST1, ch), st1);
		
#ifdef PC300_DEBUG_INTR
	        printk("sca_intr: MSCI intr chan[%d] (st=0x%08lx, st1=0x%02x)\n"
			,ch, status, st1);
#endif
		if (st1 & ST1_CDCD) { /* DCD changed */
		    if (cpc_readb(scabase + M_REG(ST3, ch)) & ST3_DCD) {
			printk("%s: DCD is OFF. Going administrative down.\n", 
			       dev->name);
			dev->flags &= ~IFF_RUNNING;
			card->chan[ch].d.line_off++;
		    } else { /* DCD = 1 */
			printk("%s: DCD is ON. Going administrative up.\n", 
			       dev->name);
			dev->flags |= IFF_RUNNING;	
			card->chan[ch].d.line_on++;
		    }
		}
	    }
	    dev->interrupt = 0;
	    spin_unlock(&card->card_lock);
	}
	if (++intr_count == 10) 
	    /* Too much work at this board. Force exit */
	    break;
    }
}

static void 
falc_t1_loop_detection(pc300_t *card, int ch, ucchar frs1)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;

    if (((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_XPRBS) == 0) && 
	!pfalc->loop_gen) {
	if (frs1 & FRS1_LLBDD) {
	    // A Line Loop Back Deactivation signal detected
	    if (pfalc->loop_active) {
		falc_remote_loop(card, ch, 0);
	    }
	} else {
	    if ((frs1 & FRS1_LLBAD) && 
		((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_EPRM) == 0)) {
		// A Line Loop Back Activation signal detected	
		if (!pfalc->loop_active) {
		    falc_remote_loop(card, ch, 1);
		}
	    }
	}
    }
}

static void 
falc_e1_loop_detection(pc300_t *card, int ch, ucchar rsp)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;

    if (((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_XPRBS) == 0) && 
	!pfalc->loop_gen) {
	if (rsp & RSP_LLBDD) {
	    // A Line Loop Back Deactivation signal detected
	    if (pfalc->loop_active) {
		falc_remote_loop(card, ch, 0);
	    }
	} else {
	    if ((rsp & RSP_LLBAD) && 
		((cpc_readb(falcbase + F_REG(LCR1, ch)) & LCR1_EPRM) == 0)) {
		// A Line Loop Back Activation signal detected	
		if (!pfalc->loop_active) {
		    falc_remote_loop(card, ch, 1);
		}
	    }
	}
    }
}

static void 
falc_t1_intr(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
    ucchar isr0, isr3, gis;
    ucchar dummy;

    while ((gis = cpc_readb(falcbase + F_REG(GIS, ch))) != 0) {
	if (gis & GIS_ISR0) {
	    isr0 = cpc_readb(falcbase + F_REG(FISR0, ch));
	    if (isr0 & FISR0_PDEN)  { 
		/* Read the bit to clear the situation */
		if (cpc_readb(falcbase + F_REG(FRS1, ch)) & FRS1_PDEN) {
			pfalc->pden++;
		}
	    }
	}

	if (gis & GIS_ISR1) {
	    dummy = cpc_readb(falcbase + F_REG(FISR1, ch));
	}
	      
	if (gis & GIS_ISR2) {
	    dummy = cpc_readb(falcbase + F_REG(FISR2, ch));
	}
      
	if (gis & GIS_ISR3) {
	    isr3 = cpc_readb(falcbase + F_REG(FISR3, ch));
	    if (isr3 & FISR3_SEC) {
		pfalc->sec++;
		falc_update_stats(card, ch);
		falc_check_status(card, ch, 
				  cpc_readb(falcbase + F_REG(FRS0, ch)));
	    }
	    if (isr3 & FISR3_ES) {
		pfalc->es++;
	    }
	    if (isr3 & FISR3_LLBSC) {
		falc_t1_loop_detection(card, ch, 
				cpc_readb(falcbase + F_REG(FRS1, ch)));
	    }
	}
    }
}

static void 
falc_e1_intr(pc300_t *card, int ch)
{
    pc300ch_t *chan = (pc300ch_t *)&card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong falcbase = card->hw.falcbase;
    ucchar isr1, isr2, isr3, gis, rsp;
    ucchar dummy;

    while ((gis = cpc_readb(falcbase + F_REG(GIS, ch))) != 0) {
	rsp = cpc_readb(falcbase + F_REG(RSP, ch));

	if (gis & GIS_ISR0)  { 
	    dummy = cpc_readb(falcbase + F_REG(FISR0, ch));
	}
	if (gis & GIS_ISR1) {
	    isr1 = cpc_readb(falcbase + F_REG(FISR1, ch));
	    if (isr1 & FISR1_XMB) {
		if ((pfalc->xmb_cause & 2) && pfalc->multiframe_mode) {
		    if (cpc_readb(falcbase + F_REG(FRS0, ch)) & 
			(FRS0_LOS | FRS0_AIS | FRS0_LFA)) {
			cpc_writeb(falcbase + F_REG(XSP, ch), 
				   cpc_readb(falcbase + F_REG(XSP, ch)) & 
				   ~XSP_AXS);
		    } else {
			cpc_writeb(falcbase + F_REG(XSP, ch), 
				   cpc_readb(falcbase + F_REG(XSP, ch)) | 
				   XSP_AXS);
		    }
		}
		pfalc->xmb_cause = 0;
		cpc_writeb(falcbase + F_REG(IMR1, ch), 
			   cpc_readb(falcbase + F_REG(IMR1, ch)) | IMR1_XMB);
	    }
	    if (isr1 & FISR1_LLBSC) {
		falc_e1_loop_detection(card, ch, rsp);
	    }
	}
	if (gis & GIS_ISR2) {
	    isr2 = cpc_readb(falcbase + F_REG(FISR2, ch));
	    if (isr2 & FISR2_T400MS) {
		cpc_writeb(falcbase + F_REG(XSW, ch), 
			   cpc_readb(falcbase + F_REG(XSW, ch)) | XSW_XRA);
	    }
	    if (isr2 & FISR2_MFAR) {
		cpc_writeb(falcbase + F_REG(XSW, ch), 
			   cpc_readb(falcbase + F_REG(XSW, ch)) & ~XSW_XRA);
	    }
	    if (isr2 & (FISR2_FAR | FISR2_LFA | FISR2_AIS | FISR2_LOS)) {
		pfalc->xmb_cause |= 2;
		cpc_writeb(falcbase + F_REG(IMR1, ch), 
			   cpc_readb(falcbase + F_REG(IMR1, ch)) & ~IMR1_XMB);
	    }
	}
	if (gis & GIS_ISR3) {
	    isr3 = cpc_readb(falcbase + F_REG(FISR3, ch));
	    if (isr3 & FISR3_SEC) {
		pfalc->sec++;
		falc_update_stats(card, ch);
		falc_check_status(card, ch, 
				  cpc_readb(falcbase + F_REG(FRS0, ch)));
	    }
	    if (isr3 & FISR3_ES) {
		pfalc->es++;
	    }
	}
    }
}

static void
falc_intr(pc300_t *card)
{
    int ch;

    for (ch = 0 ; ch < card->hw.nchan ; ch++) {
	pc300ch_t *chan = &card->chan[ch];
	pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;

	if (conf->media == LINE_T1) {
	    falc_t1_intr(card, ch);
	} else {
	    falc_e1_intr(card, ch);
	}
    }
}

static void
cpc_intr(int irq, void *dev_id, struct pt_regs *regs)
{
    pc300_t *card;
    volatile ucchar plx_status;

    if((card = (pc300_t *)dev_id) == 0){
#ifdef PC300_DEBUG_INTR
        printk("cpc_intr: spurious intr %d\n", irq);
#endif
        return; /* spurious intr */
    }

    if (card->hw.rambase == 0) {
        printk("cpc_intr: spurious intr2 %d\n", irq);
        return; /* spurious intr */
    }

    switch (card->hw.type) {
	case PC300_RSV:
	case PC300_X21:
	    sca_intr(card);
	    break;

	case PC300_TE:
	    while ((plx_status = (cpc_readb(card->hw.plxbase + 0x4c) & 
			(PLX_9050_LINT1_STATUS|PLX_9050_LINT2_STATUS))) != 0) {
		if (plx_status & PLX_9050_LINT1_STATUS) { /* SCA Interrupt */
		    sca_intr(card);
		}
		if (plx_status & PLX_9050_LINT2_STATUS) { /* FALC Interrupt */
		    falc_intr(card);
		}
	    }
	    break;
    }
}

void 
cpc_sca_status(pc300_t *card, int ch)
{
    ucchar ilar;
    uclong scabase = card->hw.scabase;
    uclong flags;

    tx_dma_buf_check(card, ch);
    rx_dma_buf_check(card, ch);
    ilar = cpc_readb(scabase + ILAR);
    printk("ILAR=0x%02x, WCRL=0x%02x, PCR=0x%02x, BTCR=0x%02x, BOLR=0x%02x\n", 
	   ilar, cpc_readb(scabase + WCRL),
	   cpc_readb(scabase + PCR),
	   cpc_readb(scabase + BTCR),
	   cpc_readb(scabase + BOLR));
    printk("TX_CDA=0x%08lx, TX_EDA=0x%08lx\n", 
	   (uclong)cpc_readl(scabase + DTX_REG(CDAL, ch)),
	   (uclong)cpc_readl(scabase + DTX_REG(EDAL, ch)));
    printk("RX_CDA=0x%08lx, RX_EDA=0x%08lx, BFL=0x%04x\n", 
	   (uclong)cpc_readl(scabase + DRX_REG(CDAL, ch)),
	   (uclong)cpc_readl(scabase + DRX_REG(EDAL, ch)),
	   cpc_readw(scabase + DRX_REG(BFLL, ch)));
    printk("DMER=0x%02x, DSR_TX=0x%02x, DSR_RX=0x%02x\n", 
	   cpc_readb(scabase + DMER),
	   cpc_readb(scabase + DSR_TX(ch)),
	   cpc_readb(scabase + DSR_RX(ch)));
    printk("DMR_TX=0x%02x, DMR_RX=0x%02x, DIR_TX=0x%02x, DIR_RX=0x%02x\n", 
	   cpc_readb(scabase + DMR_TX(ch)),
	   cpc_readb(scabase + DMR_RX(ch)),
	   cpc_readb(scabase + DIR_TX(ch)),
	   cpc_readb(scabase + DIR_RX(ch)));
    printk("DCR_TX=0x%02x, DCR_RX=0x%02x, FCT_TX=0x%02x, FCT_RX=0x%02x\n", 
	   cpc_readb(scabase + DCR_TX(ch)),
	   cpc_readb(scabase + DCR_RX(ch)),
	   cpc_readb(scabase + FCT_TX(ch)),
	   cpc_readb(scabase + FCT_RX(ch)));
    printk("MD0=0x%02x, MD1=0x%02x, MD2=0x%02x, MD3=0x%02x, IDL=0x%02x\n",
	   cpc_readb(scabase + M_REG(MD0, ch)),
	   cpc_readb(scabase + M_REG(MD1, ch)),
	   cpc_readb(scabase + M_REG(MD2, ch)),
	   cpc_readb(scabase + M_REG(MD3, ch)),
	   cpc_readb(scabase + M_REG(IDL, ch)));
    printk("CMD=0x%02x, SA0=0x%02x, SA1=0x%02x, TFN=0x%02x, CTL=0x%02x\n",
	   cpc_readb(scabase + M_REG(CMD, ch)),
	   cpc_readb(scabase + M_REG(SA0, ch)),
	   cpc_readb(scabase + M_REG(SA1, ch)),
	   cpc_readb(scabase + M_REG(TFN, ch)),
	   cpc_readb(scabase + M_REG(CTL, ch)));
    printk("ST0=0x%02x, ST1=0x%02x, ST2=0x%02x, ST3=0x%02x, ST4=0x%02x\n",
	   cpc_readb(scabase + M_REG(ST0, ch)),
	   cpc_readb(scabase + M_REG(ST1, ch)),
	   cpc_readb(scabase + M_REG(ST2, ch)),
	   cpc_readb(scabase + M_REG(ST3, ch)),
	   cpc_readb(scabase + M_REG(ST4, ch)));
    printk("CST0=0x%02x, CST1=0x%02x, CST2=0x%02x, CST3=0x%02x, FST=0x%02x\n",
	   cpc_readb(scabase + M_REG(CST0, ch)),
	   cpc_readb(scabase + M_REG(CST1, ch)),
	   cpc_readb(scabase + M_REG(CST2, ch)),
	   cpc_readb(scabase + M_REG(CST3, ch)),
	   cpc_readb(scabase + M_REG(FST, ch)));
    printk("TRC0=0x%02x, TRC1=0x%02x, RRC=0x%02x, TBN=0x%02x, RBN=0x%02x\n",
	   cpc_readb(scabase + M_REG(TRC0, ch)),
	   cpc_readb(scabase + M_REG(TRC1, ch)),
	   cpc_readb(scabase + M_REG(RRC, ch)),
	   cpc_readb(scabase + M_REG(TBN, ch)),
	   cpc_readb(scabase + M_REG(RBN, ch)));
    printk("TFS=0x%02x, TNR0=0x%02x, TNR1=0x%02x, RNR=0x%02x\n",
	   cpc_readb(scabase + M_REG(TFS, ch)),
	   cpc_readb(scabase + M_REG(TNR0, ch)),
	   cpc_readb(scabase + M_REG(TNR1, ch)),
	   cpc_readb(scabase + M_REG(RNR, ch)));
    printk("TCR=0x%02x, RCR=0x%02x, TNR1=0x%02x, RNR=0x%02x\n",
	   cpc_readb(scabase + M_REG(TCR, ch)),
	   cpc_readb(scabase + M_REG(RCR, ch)),
	   cpc_readb(scabase + M_REG(TNR1, ch)),
	   cpc_readb(scabase + M_REG(RNR, ch)));
    printk("TXS=0x%02x, RXS=0x%02x, EXS=0x%02x, TMCT=0x%02x, TMCR=0x%02x\n",
	   cpc_readb(scabase + M_REG(TXS, ch)),
	   cpc_readb(scabase + M_REG(RXS, ch)),
	   cpc_readb(scabase + M_REG(EXS, ch)),
	   cpc_readb(scabase + M_REG(TMCT, ch)),
	   cpc_readb(scabase + M_REG(TMCR, ch)));
    printk("IE0=0x%02x, IE1=0x%02x, IE2=0x%02x, IE4=0x%02x, FIE=0x%02x\n",
	   cpc_readb(scabase + M_REG(IE0, ch)),
	   cpc_readb(scabase + M_REG(IE1, ch)),
	   cpc_readb(scabase + M_REG(IE2, ch)),
	   cpc_readb(scabase + M_REG(IE4, ch)),
	   cpc_readb(scabase + M_REG(FIE, ch)));
    printk("IER0=0x%08lx\n", (uclong)cpc_readl(scabase + IER0)); 

    if (ilar != 0) {
	CPC_LOCK(card, flags);
	cpc_writeb(scabase + ILAR, ilar);
	cpc_writeb(scabase + DMER, 0x80);
	CPC_UNLOCK(card, flags);
    }
}

void 
cpc_falc_status(pc300_t *card, int ch)
{
    pc300ch_t *chan = &card->chan[ch];
    falc_t *pfalc = (falc_t *)&chan->falc;
    uclong flags;

    CPC_LOCK(card, flags);
    printk("CH%d:   %s %s  %d channels\n",
	ch, (pfalc->sync ? "SYNC":""), (pfalc->active ? "ACTIVE":""), 
	pfalc->num_channels);

    printk("        pden=%d,  los=%d,  losr=%d,  lfa=%d,  farec=%d\n",
        pfalc->pden, pfalc->los, pfalc->losr, pfalc->lfa, pfalc->farec);
    printk("        lmfa=%d,  ais=%d,  sec=%d,  es=%d,  rai=%d\n",
        pfalc->lmfa, pfalc->ais, pfalc->sec, pfalc->es, pfalc->rai);
    printk("        bec=%d,  fec=%d,  cvc=%d,  cec=%d,  ebc=%d\n",
        pfalc->bec, pfalc->fec, pfalc->cvc, pfalc->cec, pfalc->ebc);

    printk("\n");
    printk("        STATUS: %s  %s  %s  %s  %s  %s\n",
	(pfalc->red_alarm ? "RED":""), 
	(pfalc->blue_alarm ? "BLU":""), 
	(pfalc->yellow_alarm ? "YEL":""), 
	(pfalc->loss_fa ? "LFA":""), 
	(pfalc->loss_mfa ? "LMF":""), 
	(pfalc->prbs ? "PRB":""));
    CPC_UNLOCK(card, flags);
}

int 
cpc_ioctl(hdlc_device *hdlc, struct ifreq *ifr, int cmd)
{
    struct device *dev = hdlc_to_dev(hdlc);
    pc300dev_t *d = (pc300dev_t *)dev->priv;
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    pc300conf_t conf_aux;
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    int ch = chan->channel;
    int value;
    void *arg = (void *) ifr->ifr_data;
    uclong scabase = card->hw.scabase;

    if(!capable(CAP_NET_ADMIN))
	return -EPERM;

    switch(cmd) {
	case SIOCGPC300CONF:
	    conf->proto = hdlc->mode;
	    memcpy(&conf_aux.conf, conf, sizeof(pc300chconf_t));
	    memcpy(&conf_aux.hw, &card->hw, sizeof(pc300hw_t));
	    if (!arg || copy_to_user(arg, &conf_aux, sizeof(pc300conf_t)))
		return -EINVAL;
	    return 0;
	case SIOCSPC300CONF:
	    if (!suser())
		return -EPERM;
	    if (!arg || copy_from_user(&conf_aux.conf, arg, 
					sizeof(pc300chconf_t)))
		return -EINVAL;
	    if (card->hw.cpld_id < 0x02 &&
		conf_aux.conf.fr_mode == PC300_FR_UNFRAMED) {
		/* CPLD_ID < 0x02 doesn't support Unframed E1 */
		return -EINVAL;
	    }
	    memcpy(conf, &conf_aux.conf, sizeof(pc300chconf_t));
	    hdlc->mode = conf->proto;
	    return 0;
	case SIOCGPC300STATUS:
	    cpc_sca_status(card, ch);
	    return 0;
	case SIOCGPC300FALCSTATUS:
	    cpc_falc_status(card, ch);
	    return 0;
	case HDLCSETLINE:
	    value = ifr->ifr_ifru.ifru_ivalue;
	    switch (value) {
		case LINE_LOOPBACK:
		    cpc_writeb(card->hw.scabase + M_REG(MD2, ch), 
			       cpc_readb(card->hw.scabase + M_REG(MD2, ch)) | 
			       MD2_LOOP_MIR);
		    conf->loopback = 1;
		    return 0;

		case LINE_NOLOOPBACK:
		    cpc_writeb(card->hw.scabase + M_REG(MD2, ch), 
			       cpc_readb(card->hw.scabase + M_REG(MD2, ch)) & 
			       ~MD2_LOOP_MIR);
		    conf->loopback = 0;
		    return 0;

		case LINE_V35:
		case LINE_X21:
		case LINE_RS232:
		case LINE_T1:
		case LINE_E1:
		    /* Media */
		    conf->media = value;
		    return 0;

		default:
		    /* Clock rate */
		    conf->clkrate = value;
		    return 0;
	    }

	case  SIOCGPC300UTILSTATS:
	    {
	    pc300stats_t pc300stats;
	   
	    memset(&pc300stats, 0, sizeof(pc300stats_t));
	    pc300stats.hw_type = card->hw.type;
	    pc300stats.line_on = card->chan[ch].d.line_on;
	    pc300stats.line_off = card->chan[ch].d.line_off;
	    memcpy(&pc300stats.gen_stats, &d->hdlc->stats, 
		   sizeof(struct net_device_stats));
	    if (card->hw.type == PC300_TE)
	        memcpy(&pc300stats.te_stats, &chan->falc, sizeof(falc_t));
	    if (!arg || copy_to_user(arg, &pc300stats, sizeof(pc300stats_t)))
		return -EINVAL;
	    return 0;
	    }

	case SIOCGPC300UTILSTATUS:
	    {
	    struct pc300status pc300status;

	    pc300status.hw_type = card->hw.type;
	    if (card->hw.type == PC300_TE) {
		pc300status.te_status.sync         = chan->falc.sync;
		pc300status.te_status.red_alarm    = chan->falc.red_alarm;
		pc300status.te_status.blue_alarm   = chan->falc.blue_alarm;
		pc300status.te_status.loss_fa      = chan->falc.loss_fa;
		pc300status.te_status.yellow_alarm = chan->falc.yellow_alarm;
		pc300status.te_status.loss_mfa     = chan->falc.loss_mfa;
		pc300status.te_status.prbs         = chan->falc.prbs;
	    } else {
		pc300status.gen_status.dcd = 
			!(cpc_readb(scabase + M_REG(ST3, ch)) & ST3_DCD);
		pc300status.gen_status.cts = 
			!(cpc_readb(scabase + M_REG(ST3, ch)) & ST3_CTS); 
		pc300status.gen_status.rts = 
			!(cpc_readb(scabase + M_REG(CTL, ch)) & CTL_RTS); 
		pc300status.gen_status.dtr = 
			!(cpc_readb(scabase + M_REG(CTL, ch)) & CTL_DTR);
		/* There is no DSR in HD64572 */
	    }
	    if (!arg || copy_to_user(arg, &pc300status, sizeof(pc300status_t)))
		return -EINVAL;
	    return 0;
	    }

	case SIOCSPC300TRACE:
	    /* Sets/resets a trace_flag for the respective device */
	    if (!arg || copy_from_user(&d->trace_on, arg, 
				       sizeof(unsigned char)))
		return -EINVAL;
	    return 0;

	case SIOCSPC300LOOPBACK:
	    {
	    struct pc300loopback pc300loop;

	    /* TE boards only */
	    if (card->hw.type != PC300_TE)
		return -EINVAL;
	    
	    if (!arg || copy_from_user(&pc300loop, arg,
				       sizeof(pc300loopback_t)))
		return -EINVAL;
	    switch (pc300loop.loop_type) {
		case PC300LOCLOOP: /* Turn the local loop on/off */
		    falc_local_loop(card, ch, pc300loop.loop_on);
		    return 0;

		case PC300REMLOOP: /* Turn the remote loop on/off */
		    falc_remote_loop(card, ch, pc300loop.loop_on);
		    return 0;

		case PC300PAYLOADLOOP: /* Turn the payload loop on/off */
		    falc_payload_loop(card, ch, pc300loop.loop_on);
		    return 0;

		case PC300GENLOOPUP: /* Generate loop UP */
		    if (pc300loop.loop_on) {
			falc_generate_loop_up_code(card, ch);
		    } else {
			turn_off_xlu(card, ch);
		    }
		    return 0;

		case PC300GENLOOPDOWN: /* Generate loop DOWN */
		    if (pc300loop.loop_on) {
			falc_generate_loop_down_code(card, ch);
		    } else {
			turn_off_xld(card, ch);
		    }
		    return 0;

		default:
		    return -EINVAL;
	    }
	    }

	case SIOCSPC300PATTERNTEST:
	    /* Turn the pattern test on/off and show the errors counter */
	    {
	    struct pc300patterntst pc300patrntst;
	    
	    /* TE boards only */
	    if (card->hw.type != PC300_TE)
		return -EINVAL;

	    if (card->hw.cpld_id < 0x02) {
		/* CPLD_ID < 0x02 doesn't support pattern test */
		return -EINVAL;
	    }
	    if (!arg || copy_from_user(&pc300patrntst, arg,
				       sizeof(pc300patterntst_t)))
		return -EINVAL;
	    if (pc300patrntst.patrntst_on == 2) {
		if (chan->falc.prbs == 0) {
			falc_pattern_test(card, ch, 1);
		}
		pc300patrntst.num_errors = falc_pattern_test_error(card, ch);
	    	if (!arg || copy_to_user(arg, &pc300patrntst, 
					sizeof(pc300patterntst_t)))
			return -EINVAL;
	    } else {
		falc_pattern_test(card, ch, pc300patrntst.patrntst_on);
	    }
	    return 0;
	    }

	default:
	    switch(hdlc->mode & ~MODE_SOFT) {
#ifdef CONFIG_PC300_X25
		case MODE_X25:
		    /* There are no X.25-specific ioctls */
		    return -EINVAL;
#endif /* CONFIG_PC300_X25 */
		default:
		    return -EINVAL;
	    }
    }
}

static int
clock_rate_calc(uclong rate, uclong clock, int *br_io)
{
    int br, tc;
    int br_pwr, error;

    if (rate == 0)
	return (0);

    for (br = 0, br_pwr = 1 ; br <= 9 ; br++, br_pwr <<= 1) {
	if ((tc = clock / br_pwr / rate) <= 0xff) {
	    *br_io = br;
	    break;
	}
    }

    if (tc <= 0xff) {
	error = ((rate - (clock / br_pwr / rate)) / rate) * 1000;
	/* Errors bigger than +/- 1% won't be tolerated */
	if (error < -10 || error > 10)
	    return (-1);
	else
	    return (tc);
    } else {
	return (-1);
    }
}

int
ch_config(pc300dev_t *d)
{
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300chconf_t *conf = (pc300chconf_t *)&chan->conf;
    pc300_t *card = (pc300_t *)chan->card;
    uclong scabase = card->hw.scabase;
    uclong plxbase = card->hw.plxbase;
    int ch = chan->channel;
    uclong clkrate = chan->conf.clkrate;
    uclong clktype = chan->conf.clktype;
    ucchar loopback = (conf->loopback ? MD2_LOOP_MIR : MD2_F_DUPLEX);
    ucshort encoding = chan->conf.encoding;
    ucshort parity = chan->conf.parity;
    int tmc, br;
    ucchar md0, md2;

    /* Reset the channel */
    cpc_writeb(scabase + M_REG(CMD, ch), CMD_CH_RST);

    /* Configure the SCA registers */
    switch (parity) {
	case PC300_PARITY_NONE:
	    md0 = MD0_BIT_SYNC;
	    break;
	case PC300_PARITY_CRC16_PR0:
	    md0 = MD0_CRC16_0|MD0_CRCC0|MD0_BIT_SYNC;
	    break;
	case PC300_PARITY_CRC16_PR1:
	    md0 = MD0_CRC16_1|MD0_CRCC0|MD0_BIT_SYNC;
	    break;
	case PC300_PARITY_CRC32_PR1_CCITT:
	    md0 = MD0_CRC32|MD0_CRCC0|MD0_BIT_SYNC;
	    break;
	case PC300_PARITY_CRC16_PR1_CCITT:
	default:
	    md0 = MD0_CRC_CCITT|MD0_CRCC0|MD0_BIT_SYNC;
	    break;
    }
    switch (encoding) {
	case PC300_ENCODING_NRZI:
	    md2 = loopback|MD2_ADPLL_X8|MD2_NRZI;
	    break;
	case PC300_ENCODING_FM_MARK:	/* FM1 */
	    md2 = loopback|MD2_ADPLL_X8|MD2_FM|MD2_FM1;
	    break;
	case PC300_ENCODING_FM_SPACE:	/* FM0 */
	    md2 = loopback|MD2_ADPLL_X8|MD2_FM|MD2_FM0;
	    break;
	case PC300_ENCODING_MANCHESTER: /* It's not working... */
	    md2 = loopback|MD2_ADPLL_X8|MD2_FM|MD2_MANCH;
	    break;
	case PC300_ENCODING_NRZ:
	default:
	    md2 = loopback|MD2_ADPLL_X8|MD2_NRZ;
	    break;
    }

    cpc_writeb(scabase + M_REG(MD0, ch), md0);
    cpc_writeb(scabase + M_REG(MD1, ch), 0);
    cpc_writeb(scabase + M_REG(MD2, ch), md2);
    cpc_writeb(scabase + M_REG(IDL, ch), 0x7e);
    cpc_writeb(scabase + M_REG(CTL, ch), CTL_URSKP|CTL_IDLC);

    /* Configure HW media */
    switch(card->hw.type) {
	case PC300_RSV:
	    if(conf->media == LINE_V35) {
		cpc_writel((plxbase+0x50),
			cpc_readl(plxbase+0x50) | PC300_CHMEDIA_MASK(ch));
	    } else {
		cpc_writel((plxbase+0x50),
			cpc_readl(plxbase+0x50) & ~PC300_CHMEDIA_MASK(ch));
	    }
	    break;

	case PC300_X21:
	    break;

	case PC300_TE:
	    te_config(card, ch);
	    break;
    }

    switch(card->hw.type) {
	case PC300_RSV:
	case PC300_X21:
	    if (clktype == PC300_CLOCK_INT || clktype == PC300_CLOCK_TXINT) {
		/* Calculate the clkrate parameters */
		tmc = clock_rate_calc(clkrate, card->hw.clock, &br);
		cpc_writeb(scabase + M_REG(TMCT, ch), tmc);
		cpc_writeb(scabase + M_REG(TXS, ch), (TXS_DTRXC|TXS_IBRG|br));
		if (clktype == PC300_CLOCK_INT) {
		    cpc_writeb(scabase + M_REG(TMCR, ch), tmc);
		    cpc_writeb(scabase + M_REG(RXS, ch), (RXS_IBRG|br));
		} else {
		    cpc_writeb(scabase + M_REG(TMCR, ch), 1);
		    cpc_writeb(scabase + M_REG(RXS, ch), 0);
		}
		if (card->hw.type == PC300_X21) {
		    cpc_writeb(scabase + M_REG(GPO, ch), 1);
		    cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1|EXS_RES1);
		} else {
		    cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1);
		}
	    } else {
		cpc_writeb(scabase + M_REG(TMCT, ch), 1);
		if (clktype == PC300_CLOCK_EXT) {
		    cpc_writeb(scabase + M_REG(TXS, ch), TXS_DTRXC);
		} else {
		    cpc_writeb(scabase + M_REG(TXS, ch), TXS_DTRXC|TXS_RCLK);
		}
		cpc_writeb(scabase + M_REG(TMCR, ch), 1);
		cpc_writeb(scabase + M_REG(RXS, ch), 0);
		if (card->hw.type == PC300_X21) {
		    cpc_writeb(scabase + M_REG(GPO, ch), 0);
		    cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1|EXS_RES1);
		} else {
		    cpc_writeb(scabase + M_REG(EXS, ch), EXS_TES1);
		}
	    }
	    break;

	case PC300_TE:
	    /* SCA always receives clock from the FALC chip */
	    cpc_writeb(scabase + M_REG(TMCT, ch), 1);
	    cpc_writeb(scabase + M_REG(TXS, ch), 0);
	    cpc_writeb(scabase + M_REG(TMCR, ch), 1);
	    cpc_writeb(scabase + M_REG(RXS, ch), 0);
	    cpc_writeb(scabase + M_REG(EXS, ch), 0);
	    break;
    }

    /* Enable Interrupts */
    cpc_writel(scabase + IER0, 
	       cpc_readl(scabase + IER0) |
	       IR0_M(IR0_RXINTA, ch) |
	       IR0_DRX(IR0_EFT|IR0_DMIA|IR0_DMIB, ch) |
	       IR0_DTX(IR0_EFT|IR0_DMIA|IR0_DMIB, ch));
    cpc_writeb(scabase + M_REG(IE0, ch), 
	       cpc_readb(scabase + M_REG(IE0, ch)) | IE0_RXINTA);
    cpc_writeb(scabase + M_REG(IE1, ch), 
	       cpc_readb(scabase + M_REG(IE1, ch)) | IE1_CDCD);
    return 0;
}

int
rx_config(pc300dev_t *d)
{
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    uclong scabase = card->hw.scabase;
    int ch = chan->channel;

    cpc_writeb(scabase + DSR_RX(ch), 0);

    /* General RX settings */
    cpc_writeb(scabase + M_REG(RRC, ch), 0);
    cpc_writeb(scabase + M_REG(RNR, ch), 16);

    /* Enable reception */
    cpc_writeb(scabase + M_REG(CMD, ch), CMD_RX_CRC_INIT);
    cpc_writeb(scabase + M_REG(CMD, ch), CMD_RX_ENA);

    /* Initialize DMA stuff */
    chan->rx_first_bd = 0;
    chan->rx_last_bd = N_DMA_RX_BUF - 1;
    rx_dma_buf_init(card, ch);
    cpc_writeb(scabase + DCR_RX(ch), DCR_FCT_CLR);
    cpc_writeb(scabase + DMR_RX(ch), (DMR_TMOD | DMR_NF));
    cpc_writeb(scabase + DIR_RX(ch), (DIR_EOM | DIR_BOF));

    /* Start DMA */
    rx_dma_start(card, ch);

    return 0;
}

int
tx_config(pc300dev_t *d)
{
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    uclong scabase = card->hw.scabase;
    int ch = chan->channel;

    cpc_writeb(scabase + DSR_TX(ch), 0);

    /* General TX settings */
    cpc_writeb(scabase + M_REG(TRC0, ch), 0);
    cpc_writeb(scabase + M_REG(TFS, ch), 32);
    cpc_writeb(scabase + M_REG(TNR0, ch), 20);
    cpc_writeb(scabase + M_REG(TNR1, ch), 48);
    cpc_writeb(scabase + M_REG(TCR, ch), 8);

    /* Enable transmission */
    cpc_writeb(scabase + M_REG(CMD, ch), CMD_TX_CRC_INIT);

    /* Initialize DMA stuff */
    chan->tx_first_bd = 0;
    chan->tx_next_bd = 0;
    tx_dma_buf_init(card, ch);
    cpc_writeb(scabase + DCR_TX(ch), DCR_FCT_CLR);
    cpc_writeb(scabase + DMR_TX(ch), (DMR_TMOD | DMR_NF));
    cpc_writeb(scabase + DIR_TX(ch), (DIR_EOM | DIR_BOF | DIR_UDRF));
    cpc_writel(scabase + DTX_REG(CDAL, ch), TX_BD_ADDR(ch, chan->tx_first_bd));
    cpc_writel(scabase + DTX_REG(EDAL, ch), TX_BD_ADDR(ch, chan->tx_next_bd));

    return 0;
}

int
cpc_opench(pc300dev_t *d)
{
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    int ch = chan->channel;
    uclong scabase = card->hw.scabase;
    int err = -1;

    err = ch_config(d);
    if (err)
	return err;

    err = rx_config(d);
    if (err)
	return err;

    err = tx_config(d);
    if (err)
	return err;

    /* Assert RTS and DTR */
    cpc_writeb(scabase + M_REG(CTL, ch), 
	cpc_readb(scabase + M_REG(CTL, ch)) & ~(CTL_RTS | CTL_DTR));

    return 0;
}

void
cpc_closech(pc300dev_t *d)
{
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    falc_t *pfalc = (falc_t *)&chan->falc;
    int ch = chan->channel;

    cpc_writeb(card->hw.scabase + M_REG(CMD, ch), CMD_CH_RST);
    rx_dma_stop(card, ch);
    tx_dma_stop(card, ch);
    
    if (card->hw.type == PC300_TE) {
	memset(pfalc, 0, sizeof(falc_t));
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg2,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg2) & 
		   ~((CPLD_REG2_FALC_TX_CLK | CPLD_REG2_FALC_RX_CLK | 
		      CPLD_REG2_FALC_LED2) << (2*ch)));
	/* Reset the FALC chip */
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) | 
		   (CPLD_REG1_FALC_RESET << (2*ch)));
	udelay(10000);
	cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
		   cpc_readb(card->hw.falcbase + card->hw.cpld_reg1) & 
		   ~(CPLD_REG1_FALC_RESET << (2*ch)));
    }
}

int
cpc_open(hdlc_device *hdlc)
{
    struct device *dev = hdlc_to_dev(hdlc);
    pc300dev_t *d = (pc300dev_t *)dev->priv;
    int err = -1;

#ifdef	PC300_DEBUG_OTHER
    printk("pc300: cpc_open");
#endif

    err = cpc_opench(d);
    if (err)
	return err;

    switch(hdlc->mode & ~MODE_SOFT) {
#ifdef CONFIG_PC300_X25
	case MODE_X25:
	{
	    struct lapb_register_struct cpc_lapb_callbacks;

	    hdlc->ioctl = cpc_ioctl;
	    cpc_lapb_callbacks.connect_confirmation = cpc_lapb_connected;
	    cpc_lapb_callbacks.connect_indication = cpc_lapb_connected;
	    cpc_lapb_callbacks.disconnect_confirmation = cpc_lapb_disconnected;
	    cpc_lapb_callbacks.disconnect_indication = cpc_lapb_disconnected;
	    cpc_lapb_callbacks.data_indication = cpc_lapb_data_indication;
	    cpc_lapb_callbacks.data_transmit = cpc_lapb_data_transmit;

	    if ((err = lapb_register(d, &cpc_lapb_callbacks)) != LAPB_OK) {
		printk("%s: lapb_register error - %d\n", dev->name, err);
		dev->tbusy = 1;
		dev->start = 0;
		return -ENODEV;
	    }
	    dev->hard_start_xmit = cpc_x25_packetlayer_xmit;
	    dev->type = ARPHRD_X25;
	    dev->hard_header_len = 2;
	    dev->addr_len = 0;
	    break;
	}
#endif /* CONFIG_PC300_X25 */
	default:
	    break;
    }
    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;

    MOD_INC_USE_COUNT;
    return 0;
}

void
cpc_close(hdlc_device *hdlc)
{
    struct device *dev = hdlc_to_dev(hdlc);
    pc300dev_t *d = (pc300dev_t *)dev->priv;
    pc300ch_t *chan = (pc300ch_t *)d->chan;
    pc300_t *card = (pc300_t *)chan->card;
    uclong flags;

#ifdef	PC300_DEBUG_OTHER
    printk("pc300: cpc_close");
#endif

    CPC_LOCK(card, flags);
    switch(hdlc->mode & ~MODE_SOFT) {
#ifdef CONFIG_PC300_X25
	case MODE_X25:
	{
	    int err;

	    if ((err = lapb_unregister(d)) != LAPB_OK) {
		printk("%s: lapb_unregister error - %d\n", dev->name, err);
	    }
	    break;
	}
#endif /* CONFIG_PC300_X25 */
	default:
	    break;
    }
    if (d->tx_skb) {
	dev_kfree_skb(d->tx_skb);
	d->tx_skb = NULL;
    }
    dev->tbusy = 1;
    dev->start = 0;
    cpc_closech(d);
    CPC_UNLOCK(card, flags);

    MOD_DEC_USE_COUNT;
}

static uclong
detect_ram(pc300_t *card)
{
    uclong i;
    ucchar data;
    uclong rambase = card->hw.rambase;

    card->hw.ramsize = PC300_RAMSIZE;
    /* Let's find out how much RAM is present on this board */
    for (i = 0; i < card->hw.ramsize ; i++) {
        data = (ucchar)(i & 0xff);
        cpc_writeb(rambase + i, data);
        if (cpc_readb(rambase + i) != data) {
            break;
        }
    }
    return (i);
}

static void
plx_init(pc300_t *card)
{
  struct RUNTIME_9050 *plx_ctl = (struct RUNTIME_9050 *)card->hw.plxbase;

    /* Reset PLX */
    cpc_writel(&plx_ctl->init_ctrl, cpc_readl(&plx_ctl->init_ctrl)|0x40000000);
    udelay(10000L);
    cpc_writel(&plx_ctl->init_ctrl, cpc_readl(&plx_ctl->init_ctrl)&~0x40000000);

    /* Reload Config. Registers from EEPROM */
    cpc_writel(&plx_ctl->init_ctrl, cpc_readl(&plx_ctl->init_ctrl)|0x20000000);
    udelay(10000L);
    cpc_writel(&plx_ctl->init_ctrl, cpc_readl(&plx_ctl->init_ctrl)&~0x20000000);

}

__initfunc(static int  
cpc_detect(void))
{
#ifdef CONFIG_PCI
    static struct pci_dev	*pdev = NULL;
    ucchar	cpc_rev_id;
    ucchar	cpc_irq = 0;
    uclong	cpc_plxphys, cpc_ramphys, cpc_scaphys, cpc_falcphys, cpc_iophys;
    uclong	cpc_plxbase, cpc_rambase, cpc_scabase, cpc_falcbase;
    ucshort	i, j, eeprom_outdated = 0;
    ucshort	device_id, dev_index = 0;
    pc300_t	*card;

    if(pci_present() == 0) {    /* PCI bus not present */
	return 0;
    }
    for (i = 0 ; i < PC300_MAXCARDS ; i++) {
	/* look for a Cyclades card by vendor and device id */
	while((device_id = cpc_pci_dev_id[dev_index]) != 0) {
	    if((pdev = pci_find_device(PCI_VENDOR_ID_CYCLADES, 
				       device_id, pdev)) == NULL) {
		dev_index++;    /* try next device id */
	    } else {
		break;    /* found a board */
	    }
	}
	if (device_id == 0)
	    break;

	/* read PCI configuration area */
	cpc_irq = pdev->irq;
	cpc_iophys = pdev->base_address[1];
	cpc_scaphys = pdev->base_address[2];
	cpc_ramphys = pdev->base_address[3];
	cpc_falcphys = pdev->base_address[4];
	cpc_plxphys = pdev->base_address[5];
	pci_read_config_byte(pdev, PCI_REVISION_ID, &cpc_rev_id);

	if ((device_id == PCI_DEVICE_ID_PC300_RX_1) ||
	    (device_id == PCI_DEVICE_ID_PC300_RX_2) ||
	    (device_id == PCI_DEVICE_ID_PC300_TE_1) ||
	    (device_id == PCI_DEVICE_ID_PC300_TE_2)) {
#ifdef PC300_DEBUG_PCI
	    printk("cpc (bus=0x0%x, pci_id=0x%x, ",
		   pdev->bus->number, pdev->devfn);
	    printk("rev_id=%d) IRQ%d\n", cpc_rev_id, (int)cpc_irq);
	    printk("cpc:found  ramaddr=0x%lx plxaddr=0x%lx "
		   "ctladdr=0x%lx falcaddr=0x%lx\n",
		   (ulong)cpc_ramphys, (ulong)cpc_plxphys, 
		   (ulong)cpc_scaphys, (ulong)cpc_falcphys);
#endif
	    cpc_iophys &= PCI_BASE_ADDRESS_IO_MASK;
	    cpc_plxphys &= PCI_BASE_ADDRESS_MEM_MASK;
	    cpc_ramphys &= PCI_BASE_ADDRESS_MEM_MASK;
	    cpc_scaphys &= PCI_BASE_ADDRESS_MEM_MASK;
	    cpc_falcphys &= PCI_BASE_ADDRESS_MEM_MASK;

	    /* Although we don't use this I/O region, we should
	       request it from the kernel anyway, to avoid problems
	       with other drivers accessing it. */
	    request_region(cpc_iophys, PC300_PLX_WIN, "Cyclades-PC300");

	    if (cpc_plxphys) {
		pdev->base_address[0] = cpc_plxphys;
		pci_write_config_dword(pdev, PCI_BASE_ADDRESS_0, cpc_plxphys);
	    } else {
		eeprom_outdated = 1;
		cpc_plxphys = pdev->base_address[0];
		cpc_plxphys &= PCI_BASE_ADDRESS_MEM_MASK;
	    }

	    cpc_plxbase = (uclong) ioremap(cpc_plxphys, PC300_PLX_WIN);
	    cpc_rambase = (uclong) ioremap(cpc_ramphys, PC300_RAMSIZE);
	    cpc_scabase = (uclong) ioremap(cpc_scaphys, PC300_SCASIZE);
	    switch(device_id) {
		case PCI_DEVICE_ID_PC300_TE_1:
		case PCI_DEVICE_ID_PC300_TE_2:
		    cpc_falcbase = (uclong) ioremap(cpc_falcphys, 
						    PC300_FALCSIZE);
		    break;

		case PCI_DEVICE_ID_PC300_RX_1:
		case PCI_DEVICE_ID_PC300_RX_2:
		default:
		    cpc_falcbase = 0;
		    break;
	    }

#ifdef PC300_DEBUG_PCI
	    printk("cpc: relocate ramaddr=0x%lx plxaddr=0x%lx "
		   "ctladdr=0x%lx falcaddr=0x%lx\n",
		   (ulong)cpc_rambase, (ulong)cpc_plxbase, 
		   (ulong)cpc_scabase, (ulong)cpc_falcbase);
#endif
	    /* Fill the next card structure available */
	    for (j = 0 ; j < PC300_MAXCARDS ; j++) {
		if (cpc_card[j].hw.rambase == 0)  break;
	    }

	    if (j == PC300_MAXCARDS) {	/* No more cards available */
		printk("PC300 found at RAM 0x%lx, "
		       "but no more cards can be used.\n",
		       (ulong) cpc_ramphys);
		printk("Change PC300_MAXCARDS in pc300drv.c and "
		       "recompile your kernel.\n");
		return(i);
	    }

	    /* Allocate IRQ */
	    if(request_irq(cpc_irq, cpc_intr, SA_SHIRQ, "Cyclades-PC300", 
			   &cpc_card[j]))
	    {
		printk("PC300 found at RAM 0x%lx, "
		       "but could not allocate IRQ%d.\n",
		       (ulong) cpc_ramphys, cpc_irq);
		return(i);
	    }

	    card = &cpc_card[j];

	    /* Set PC300 HW structure */
	    card->hw.plxphys = cpc_plxphys;
	    card->hw.plxbase = cpc_plxbase;
	    card->hw.plxsize = (uclong)PC300_PLX_WIN;
	    card->hw.scaphys = cpc_scaphys;
	    card->hw.scabase = cpc_scabase;
	    card->hw.scasize = (uclong)PC300_SCASIZE;
	    card->hw.ramphys = cpc_ramphys;
	    card->hw.rambase = cpc_rambase;
	    card->hw.ramsize = detect_ram(card);
	    card->hw.falcphys = cpc_falcphys;
	    card->hw.falcbase = cpc_falcbase;
	    card->hw.falcsize = (uclong)PC300_FALCSIZE;
	    card->hw.irq = (int)cpc_irq;
	    switch(device_id) {
		case PCI_DEVICE_ID_PC300_RX_1:
		case PCI_DEVICE_ID_PC300_TE_1:
		    card->hw.nchan = 1;
		    break;

		case PCI_DEVICE_ID_PC300_RX_2:
		case PCI_DEVICE_ID_PC300_TE_2:
		default:
		    card->hw.nchan = PC300_MAXCHAN;
		    break;
	    }

	    /* Enable interrupts on the PCI bridge */
	    plx_init(&cpc_card[j]);
	    cpc_writew(card->hw.plxbase+0x4c, 
		cpc_readw(card->hw.plxbase+0x4c) | 0x0040);

#ifdef USE_PCI_CLOCK
	    /* Set board clock to PCI clock */
	    cpc_writel(card->hw.plxbase+0x50, 
		cpc_readl(card->hw.plxbase+0x50) | 0x00000004UL);
	    card->hw.clock = PC300_PCI_CLOCK;
#else
	    /* Set board clock to internal oscillator clock */
	    cpc_writel(card->hw.plxbase+0x50, 
		cpc_readl(card->hw.plxbase+0x50) & ~0x00000004UL);
	    card->hw.clock = PC300_OSC_CLOCK;
#endif

	    /* Set Global SCA-II registers */
	    cpc_writeb(card->hw.scabase + PCR, PCR_PR2);
	    cpc_writeb(card->hw.scabase + BTCR, 0x10);
	    cpc_writeb(card->hw.scabase + WCRL, 0);
	    cpc_writeb(card->hw.scabase + DMER, 0x80);

	    /* Set board type */
	    switch(device_id) {
		case PCI_DEVICE_ID_PC300_TE_1:
		case PCI_DEVICE_ID_PC300_TE_2: {
		    ucchar reg1;

		    card->hw.type = PC300_TE;

		    /* Check CPLD version */
		    reg1 = cpc_readb(card->hw.falcbase + CPLD_REG1);
		    cpc_writeb(card->hw.falcbase + CPLD_REG1, (reg1 + 0x5a));
		    if (cpc_readb(card->hw.falcbase + CPLD_REG1) == reg1) {
			/* New CPLD */
			card->hw.cpld_id = cpc_readb(card->hw.falcbase + 
						     CPLD_ID_REG);
			card->hw.cpld_reg1 = CPLD_V2_REG1;
			card->hw.cpld_reg2 = CPLD_V2_REG2;
		    } else {
			/* old CPLD */
			card->hw.cpld_id = 0;
			card->hw.cpld_reg1 = CPLD_REG1;
			card->hw.cpld_reg2 = CPLD_REG2;
			cpc_writeb(card->hw.falcbase + CPLD_REG1, reg1);
		    }

		    /* Enable the board's global clock */
		    cpc_writeb(card->hw.falcbase + card->hw.cpld_reg1,
			       cpc_readb(card->hw.falcbase + 
					 card->hw.cpld_reg1) | 
			       CPLD_REG1_GLOBAL_CLK);
		    break;
		}
		case PCI_DEVICE_ID_PC300_RX_1:
		case PCI_DEVICE_ID_PC300_RX_2:
		default:
		    if((cpc_readl(card->hw.plxbase+0x50) & PC300_CTYPE_MASK)) {
			card->hw.type = PC300_X21;
		    } else {
			card->hw.type = PC300_RSV;
		    }
		    break;
	    }
	}
    }
    if (eeprom_outdated)
	printk("WARNING: detected at least one PC300 with an outdated "
	       "EEPROM.\n");
    return i;
#else
    printk("cpc: WARNING: your kernel does not have PCI support.\n");
    return 0;
#endif /* CONFIG_PCI */
}

/*
 * This routine prints out the appropriate serial driver version number
 * and identifies which options were configured into this driver.
 */
static inline void
show_version(void)
{
  char *rcsvers, *rcsdate, *tmp;
    rcsvers = strchr(rcsid, ' '); rcsvers++;
    tmp = strchr(rcsvers, ' '); *tmp++ = '\0';
    rcsdate = strchr(tmp, ' '); rcsdate++;
    tmp = strrchr(rcsdate, ' '); *tmp = '\0';
    printk(KERN_INFO "Cyclades-PC300 driver %s %s (built %s %s)\n", 
		     rcsvers, rcsdate, __DATE__, __TIME__);
} /* show_version */

__initfunc(int 
cpc_init(void))
{
    int i, j, devcount = 0;

    show_version();
    memset(&cpc_card, 0, (PC300_MAXCARDS * sizeof(pc300_t)));
    if((cpc_nboards = cpc_detect()) == 0) {
	printk("** No boards were found.\n");
	return -ENODEV;
    }

    /* Fill in valid structures and invalidate the remaining */
    for (i = 0 ; i < PC300_MAXCARDS ; i++) {
	pc300_t *card = &cpc_card[i];

	if (card->hw.rambase != 0) {
	    for(j = 0; j < card->hw.nchan; j++) {
		pc300ch_t *chan = &card->chan[j];
		pc300dev_t *d = &chan->d;
		hdlc_device *hdlc;
		struct device *dev;

		memset(chan, 0, sizeof(pc300ch_t));

		chan->card = card;
		chan->channel = j;
		chan->conf.clkrate = 0;
		chan->conf.clktype = PC300_CLOCK_EXT;
		chan->conf.loopback = 0;
		chan->conf.encoding = PC300_ENCODING_NRZ;
		chan->conf.parity = PC300_PARITY_CRC16_PR1_CCITT;
		switch(card->hw.type) {
		    case PC300_TE:
			chan->conf.media = LINE_T1;
			chan->conf.lcode = PC300_LC_B8ZS;
			chan->conf.fr_mode = PC300_FR_ESF;
			chan->conf.lbo = PC300_LBO_0_DB;
			chan->conf.rx_sens = PC300_RX_SENS_SH;
			chan->conf.tslot_bitmap = 0xffffffffUL;
			break;

		    case PC300_X21:
			chan->conf.media = LINE_X21;
			break;

		    case PC300_RSV:
		    default:
			chan->conf.media = LINE_V35;
			break;
		}
		chan->tx_first_bd = 0;
		chan->tx_next_bd = 0;
		chan->rx_first_bd = 0;
		chan->rx_last_bd = N_DMA_RX_BUF - 1;

		d->chan = chan;
		d->tx_skb = NULL;
		d->trace_on = 0;
		d->line_on = 0;
		d->line_off = 0;

		d->hdlc = (hdlc_device *)
			kmalloc(sizeof(hdlc_device), GFP_KERNEL);
		if (d->hdlc == NULL)
		    continue;
		memset(d->hdlc, 0, sizeof(hdlc_device));

		hdlc = d->hdlc;
		hdlc->open = cpc_open;
		hdlc->close = cpc_close;
		hdlc->ioctl = cpc_ioctl;
		d->if_ptr = &hdlc->pppdev;

		dev = hdlc_to_dev(d->hdlc);
		dev->mem_start = card->hw.ramphys;
		dev->mem_end = card->hw.ramphys + card->hw.ramsize - 1;
		dev->irq = card->hw.irq;
		dev->interrupt = 0;
		dev->start = 0;
		dev->tx_queue_len = PC300_TX_QUEUE_LEN;
		dev->hard_start_xmit = cpc_queue_xmit;
		dev->set_multicast_list = NULL;
		dev->set_mac_address = NULL;

		if(register_hdlc_device(hdlc) == 0) {
		    dev->priv = d;	/* We need 'priv', hdlc doesn't */
		    printk("%s: Cyclades-PC300/", hdlc->name);
		    switch(card->hw.type) {
			case PC300_TE:
			    printk("TE ");
			    break;

			case PC300_X21:
			    printk("X21");
			    break;

			case PC300_RSV:
			default:
			    printk("RSV");
			    break;
		    }
		    printk(" #%d, %ldKB of RAM at 0x%lx, IRQ%d, channel %d.\n",
			   i + 1, card->hw.ramsize/1024, card->hw.ramphys, 
			   card->hw.irq, j + 1);
		    devcount++;
		} else {
		    *(d->name) = 0;
		    kfree(d->hdlc);
		    continue;
		}
	    }
	    spin_lock_init(&card->card_lock);
	} else {
	    memset(&card->hw, 0, sizeof (pc300hw_t));
	}
    }

    return 0;
}

#ifdef MODULE
int init_module (void)
{
    return (cpc_init());
} /* init_module */

void cleanup_module (void)
{
    int i, j;

    for (i = 0 ; i < PC300_MAXCARDS ; i++) {
	pc300_t *card = &cpc_card[i];

        if (card->hw.rambase != 0) {
	    /* Disable interrupts on the PCI bridge */
	    cpc_writew(card->hw.plxbase+0x4c, 
		cpc_readw(card->hw.plxbase+0x4c) & ~(0x0040));

	    for(j = 0 ; j < card->hw.nchan ; j++) {
		unregister_hdlc_device(card->chan[j].d.hdlc);
	    }

	    iounmap((void *)card->hw.plxbase);
	    iounmap((void *)card->hw.scabase);
	    iounmap((void *)card->hw.rambase);
	    if (card->hw.type == PC300_TE)
		iounmap((void *)card->hw.falcbase);
	    if (card->hw.irq)
		free_irq(card->hw.irq, card);
        }
    }
}
#endif

