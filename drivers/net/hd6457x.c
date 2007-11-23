/*
 * Hitachi SCA HD64570 and HD64572 common driver for Linux
 *
 * Copyright (C) 1998-2000 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Sources of information:
 *    Hitachi HD64570 SCA User's Manual
 *    Hitachi HD64572 SCA-II User's Manual
 *
 *
 * Some details:
 * dev->tbusy bit 0 is used as hard_start_xmit lock
 *	      bit 1 is a xmit buffer full flag (set by xmit, cleared by intr)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <linux/hdlc.h>

#if (!defined (__HD64570_H) && !defined (__HD64572_H)) || \
    (defined (__HD64570_H) && defined (__HD64572_H))
#error Either hd64570.h or hd64572.h must be included
#endif


static card_t *first_card=NULL;
static card_t **new_card=&first_card;


/* Maximum events to handle at each interrupt - should I increase it? */
#define INTR_WORK 4

#define get_msci(port)	  (phy_node(port) ?   MSCI1_OFFSET :   MSCI0_OFFSET)
#define get_dmac_rx(port) (phy_node(port) ? DMAC1RX_OFFSET : DMAC0RX_OFFSET)
#define get_dmac_tx(port) (phy_node(port) ? DMAC1TX_OFFSET : DMAC0TX_OFFSET)

#define SCA_INTR_MSCI(node)    (node ? 0x10 : 0x01)
#define SCA_INTR_DMAC_RX(node) (node ? 0x20 : 0x02)
#define SCA_INTR_DMAC_TX(node) (node ? 0x40 : 0x04)

#ifdef __HD64570_H /* HD64570 */
#define sca_outa(value, reg, card)	sca_outw(value, reg, card)
#define sca_ina(reg, card)		sca_inw(reg, card)
#define writea(value, ptr)		writew(value, ptr)

static __inline__ int sca_intr_status(card_t *card)
{
	u8 isr0 = sca_in(ISR0, card);
	u8 isr1 = sca_in(ISR1, card);
	u8 result = 0;

	if (isr1 & 0x03) result |= SCA_INTR_DMAC_RX(0);
	if (isr1 & 0x0C) result |= SCA_INTR_DMAC_TX(0);
	if (isr1 & 0x30) result |= SCA_INTR_DMAC_RX(1);
	if (isr1 & 0xC0) result |= SCA_INTR_DMAC_TX(1);
	if (isr0 & 0x0F) result |= SCA_INTR_MSCI(0);
	if (isr0 & 0xF0) result |= SCA_INTR_MSCI(1);

	return result;
}

#else /* HD64572 */
#define sca_outa(value, reg, card)	sca_outl(value, reg, card)
#define sca_ina(reg, card)		sca_inl(reg, card)
#define writea(value, ptr)		writel(value, ptr)


static __inline__ int sca_intr_status(card_t *card)
{
	u32 isr0 = sca_inl(ISR0, card);
	u8 result = 0;
  
	if (isr0 & 0x0000000F) result |= SCA_INTR_DMAC_RX(0);
	if (isr0 & 0x000000F0) result |= SCA_INTR_DMAC_TX(0);
	if (isr0 & 0x00000F00) result |= SCA_INTR_DMAC_RX(1);
	if (isr0 & 0x0000F000) result |= SCA_INTR_DMAC_TX(1);
	if (isr0 & 0x003E0000) result |= SCA_INTR_MSCI(0);
	if (isr0 & 0x3E000000) result |= SCA_INTR_MSCI(1);

	return result;
}

#endif /* HD64570 vs HD64572 */




static __inline__ port_t* hdlc_to_port(hdlc_device *hdlc)
{
	return (port_t*)hdlc;
}



static __inline__ port_t* dev_to_port(struct device *dev)
{
	return hdlc_to_port(dev_to_hdlc(dev));
}



static __inline__ u8 next_desc(port_t *port, u8 desc)
{
	return (desc+1) % port_to_card(port)->ring_buffers;
}



static __inline__ u16 desc_offset(port_t *port, u8 desc, u8 transmit)
{
	/* Descriptor offset always fits in 16 bytes */
	u8 buffs=port_to_card(port)->ring_buffers;
	return ((log_node(port) * 2 + transmit) * buffs + (desc % buffs)) *
		sizeof(pkt_desc);
}



static __inline__ pkt_desc* desc_address(port_t *port, u8 desc, u8 transmit)
{
#ifdef PAGE0_ALWAYS_MAPPED
	return (pkt_desc*)(win0base(port_to_card(port))
			   + desc_offset(port, desc, transmit));
#else
	return (pkt_desc*)(winbase(port_to_card(port))
			   + desc_offset(port, desc, transmit));
#endif  
}



static __inline__ u32 buffer_offset(port_t *port, u8 desc, u8 transmit)
{
	u8 buffs=port_to_card(port)->ring_buffers;
	return port_to_card(port)->buff_offset +
		((log_node(port) * 2 + transmit) * buffs + (desc % buffs)) *
		(u32)HDLC_MAX_MTU;
}



static void sca_create_rings(port_t *port)
{
	card_t *card=port_to_card(port);
	u8 transmit, i;
	u16 dmac, buffs=card->ring_buffers;
  
#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	openwin(card, 0);
#endif  

	for (transmit=0; transmit<2; transmit++) {
		for (i = 0; i<buffs; i++) {
			pkt_desc* desc = desc_address(port, i, transmit);
			u16 chain_off = desc_offset(port, i+1, transmit);
			u32 buff_off = buffer_offset(port, i, transmit);
      
			writea(chain_off, &desc->cp);
			writel(buff_off, &desc->bp);
			writew(0, &desc->len);
			writeb(0, &desc->stat);
		}
    
		dmac = transmit ? get_dmac_tx(port) : get_dmac_rx(port);
		/* DMA disable - to halt state */
		sca_out(0, transmit ? DSR_TX(phy_node(port)) :
			DSR_RX(phy_node(port)), card);
		/* software ABORT - to initial state */
		sca_out(DCR_ABORT, transmit ? DCR_TX(phy_node(port)) :
			DCR_RX(phy_node(port)), card);
    
#ifdef __HD64570_H
		sca_out(0, dmac+CPB, card); /* pointer base */
#endif
		/* current desc addr */
		sca_outa(desc_offset(port, 0, transmit), dmac+CDAL, card);
		if (!transmit)
			sca_outa(desc_offset(port, buffs-1, transmit),
				 dmac+EDAL, card);
		else
			sca_outa(desc_offset(port, 0, transmit), dmac+EDAL,
				 card);
    
		/* clear frame end interrupt counter */
		sca_out(DCR_CLEAR_EOF, transmit ? DCR_TX(phy_node(port)) :
			DCR_RX(phy_node(port)), card);

		if (!transmit) { /* Receive */
			/* set buffer length */
			sca_outw(HDLC_MAX_MTU, dmac+BFLL, card);
			/* Chain mode, Multi-frame */
			sca_out(0x14, DMR_RX(phy_node(port)), card);
			sca_out(DIR_EOME|DIR_BOFE, DIR_RX(phy_node(port)),
				card);
			/* DMA enable */
			sca_out(DSR_DE, DSR_RX(phy_node(port)), card);
		} else {	/* Transmit */
			/* Chain mode, Multi-frame */
			sca_out(0x14, DMR_TX(phy_node(port)), card);
			/* enable underflow interrupts */
			sca_out(DIR_BOFE, DIR_TX(phy_node(port)), card);
		}
	}
}



static void sca_init_sync_port(port_t *port)
{
	port->rxin = 0;
	port->txin = 0;
	port->txlast = 0;
	sca_create_rings(port);
}



/* MSCI interrupt service */
static void sca_msci_intr(port_t *port)
{
	u16 msci=get_msci(port);
	card_t* card=port_to_card(port);
	u8 stat = sca_in(msci+ST1, card); /* read MSCI ST1 status */

	/* printk(KERN_DEBUG "MSCI INT: ST1=%02X ILAR=%02X\n",
	   stat, sca_in(ILAR, card)); */

	/* Reset MSCI TX underrun status bit */
	sca_out(stat & ST1_UDRN, msci+ST1, card);

	if (stat & ST1_UDRN) {
		port->hdlc.stats.tx_errors++; /* TX Underrun error detected */
		port->hdlc.stats.tx_fifo_errors++;
	}
}



/* Receive DMA interrupt service */
static void sca_rx_intr(port_t *port)
{
	u16 dmac = get_dmac_rx(port);
	card_t *card = port_to_card(port);
	u8 stat = sca_in(DSR_RX(phy_node(port)), card); /* read DMA Status */

	/* Reset DSR status bits */
	sca_out((stat&(DSR_EOT|DSR_EOM|DSR_BOF|DSR_COF)) | DSR_DWE,
		DSR_RX(phy_node(port)), card);

	if (stat & DSR_BOF) {
		port->hdlc.stats.rx_errors++; /* Dropped one or more frames */
		port->hdlc.stats.rx_over_errors++;
	}
  
	while (1) {
		u32 desc_off = desc_offset(port, port->rxin, 0);
		pkt_desc *desc;
		u32 cda=sca_ina(dmac+CDAL, card);

		if (cda == desc_off)
			break;	/* No frame received */

#ifdef __HD64572_H
		if (cda == desc_off + 8)
			break;	/* SCA-II updates CDA in 2 steps */
#endif
    
		desc = desc_address(port, port->rxin, 0);
		stat = readb(&desc->stat);
		if (stat & ST_RX_OVERRUN) {
			port->hdlc.stats.rx_errors++;
			port->hdlc.stats.rx_fifo_errors++;
		} else if (stat & ST_RX_CRC) {
			port->hdlc.stats.rx_errors++;
			port->hdlc.stats.rx_crc_errors++;
		} else if ((stat & (ST_RX_SHORT|ST_RX_ABORT|ST_RX_RESBIT)) ||
			   !(stat & (ST_RX_EOM))) {
			port->hdlc.stats.rx_errors++; /* Malformed frame */
			port->hdlc.stats.rx_frame_errors++;
		} else {
			u16 len = readw(&desc->len);
			struct sk_buff *skb = dev_alloc_skb(len);
			if (!skb)
				port->hdlc.stats.rx_dropped++;
			else {
#ifdef DEBUG_PKT
				int i;
#endif
				u32 buff = buffer_offset(port, port->rxin, 0);
#ifndef ALL_PAGES_ALWAYS_MAPPED
				u32 maxlen;
				u8 page = buff / winsize(card);
				buff = buff % winsize(card);
				maxlen = winsize(card) - buff;
	
				openwin(card, page);

				if (len > maxlen) {
					memcpy_fromio(skb->data,
						      winbase(card) + buff,
						      maxlen);
					openwin(card, page+1);
					memcpy_fromio(skb->data + maxlen,
						      winbase(card),
						      len - maxlen);
				} else
#endif
					memcpy_fromio(skb->data,
						      winbase(card) + buff,
						      len);

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
				/* select pkt_desc table page back */
				openwin(card, 0);
#endif
				skb_put(skb, len);
	
#ifdef DEBUG_PKT
				printk(KERN_DEBUG "%s RX:", port->hdlc.name);
				for (i=0; i<skb->len; i++)
					printk(" %02X", skb->data[i]);
				printk("\n");
#endif

				hdlc_netif_rx(&port->hdlc, skb, 0);
			}
		}

		/* Set new error descriptor address */
		sca_outa(desc_off, dmac+EDAL, card);
		port->rxin = next_desc(port, port->rxin);
	}

	/* make sure RX DMA is enabled */
	sca_out(DSR_DE, DSR_RX(phy_node(port)), card);
}



/* Transmit DMA interrupt service */
static void sca_tx_intr(port_t *port)
{
	u16 dmac = get_dmac_tx(port);
	card_t* card=port_to_card(port);
	u8 stat = sca_in(DSR_TX(phy_node(port)), card); /* read DMA Status */

	/* Reset DSR status bits */
	sca_out((stat&(DSR_EOT|DSR_EOM|DSR_BOF|DSR_COF)) | DSR_DWE,
		DSR_TX(phy_node(port)), card);

	while (1) {
		u32 desc_off = desc_offset(port, port->txlast, 1);
		pkt_desc *desc;
		u16 len;

		if (sca_ina(dmac+CDAL, card) == desc_off)
			break;	/* Transmitter is/will_be sending this frame */

		desc = desc_address(port, port->txlast, 1);
		len=readw(&desc->len);

		port->hdlc.stats.tx_packets++;
		port->hdlc.stats.tx_bytes+=len;
		writeb(0, &desc->stat);	/* Free descriptor */

		port->txlast = (port->txlast + 1) %
			port_to_card(port)->ring_buffers;
	}

	if (test_and_clear_bit(1, &(hdlc_to_dev(&port->hdlc)->tbusy)))
		mark_bh(NET_BH); /* Tell upper layers */
}



static void sca_intr(int irq, void* dev_id, struct pt_regs *regs)
{
	card_t *card=dev_id;
	int boguscnt = INTR_WORK;
	int i;
	u8 stat;

#ifndef ALL_PAGES_ALWAYS_MAPPED
	u8 page = sca_get_page(card);
#endif
  
	while((stat = sca_intr_status(card)) != 0) {
		for (i=0; i<2; i++) {
			port_t *port=get_port(card, i);
			if (port) {
				if (stat & SCA_INTR_MSCI(i))
					sca_msci_intr(port);
	
				if (stat & SCA_INTR_DMAC_RX(i))
					sca_rx_intr(port);
	
				if (stat & SCA_INTR_DMAC_TX(i))
					sca_tx_intr(port);
			}

			if (--boguscnt < 0) {
				printk(KERN_ERR "%s: too much work at "
				       "interrupt\n", port->hdlc.name);
				goto exit;
			}
		}
	}
  
 exit:
#ifndef ALL_PAGES_ALWAYS_MAPPED
	openwin(card, page);		/* Restore original page */
#endif
}



static inline void sca_set_loopback(port_t *port, int loopback)
{
	card_t* card=port_to_card(port);
	u8 msci = get_msci(port);
	u8 md2 = sca_in(msci+MD2, card);

	if (loopback)
		md2 |= MD2_LOOPBACK;
	else
		md2 &= ~MD2_LOOPBACK;

	sca_out(md2, msci+MD2, card);
	port->loopback = loopback;
}



static int sca_set_clock(port_t *port, int clock)
{
	card_t *card=port_to_card(port);
	u8 msci=get_msci(port);

	u8 result=set_clock(port, clock); /* Card-dependent settings */
	if (result)
		return result;

	if (port->clkmode != LINE_EXT_CLOCK) {
		unsigned int tmc, br=10, brv=1024;

		do {
			br--;
			brv>>=1; /* brv=2^9=512 max in specs */

			/* Baud Rate = CLOCK_BASE / TMC / 2^BR */
			tmc=CLOCK_BASE / (brv*port->clkmode);
		}while(br>1 && tmc<=128); /* Try lower br for better accuracy*/

		if (tmc<1) {
			tmc=1;
			br=0;	/* For baud=CLOCK_BASE we use tmc=1 br=0 */
			brv=1;
		}
		else if (tmc>255)
			tmc=256; /* tmc=0 means 256 - low baud rates */

		port->clkmode=CLOCK_BASE/(brv*tmc);
		printk(KERN_INFO "%s: using internal clock %u. br=%d, "
		       "tmc=%d\n", port->hdlc.name, port->clkmode, br, tmc);
    
		/* baud divisor - time constant*/
#ifdef __HD64570_H
		sca_out(tmc, msci+TMC, card);
#else
		sca_out(tmc, msci+TMCR, card);
		sca_out(tmc, msci+TMCT, card);
#endif    
		sca_out(CLK_BRG_RX|br, msci+RXS, card); /* BRG output */
		sca_out(CLK_BRG_TX|br, msci+TXS, card); /* BRG output */
	} else {
#ifdef __HD64570_H
		sca_out(1, msci+TMC, card);
#else
		sca_out(1, msci+TMCR, card);
		sca_out(1, msci+TMCT, card);
#endif    
		sca_out(CLK_LINE_RX, msci+RXS, card); /* RXC - line input */
		sca_out(CLK_LINE_TX, msci+TXS, card); /* TXC input */
	}

	return 0;
}



static void sca_set_hdlc_mode(port_t *port, u8 idle, u8 crc, u8 nrzi)
{
	card_t* card=port_to_card(port);
	u8 msci = get_msci(port);
	u8 md2 = (nrzi ? MD2_NRZI : 0) | (port->loopback ? MD2_LOOPBACK : 0);
	u8 ctl = (idle ? CTL_IDLE : 0);
#ifdef __HD64572_H
	ctl |= CTL_URCT | CTL_URSKP; /* Skip the rest of underrun frame */
#endif

	sca_out(CMD_RESET, msci+CMD, card);
	sca_out(MD0_HDLC|crc, msci+MD0, card);
	sca_out(0x00, msci+MD1, card); /* no address field check */
	sca_out(md2, msci+MD2, card);
	sca_out(0x7E, msci+IDL, card); /* flag character 0x7E */
	sca_out(ctl, msci+CTL, card);

#ifdef __HD64570_H
	/* Allow at least 8 bytes before requesting RX DMA operation */
	/* TX with higher priority and possibly with shorter transfers */
	sca_out(0x07, msci+RRC, card); /* +1=RXRDY/DMA activation condition */
	sca_out(0x10, msci+TRC0, card); /* = TXRDY/DMA activation condition */
	sca_out(0x14, msci+TRC1, card); /* +1=TXRDY/DMA deactiv condition */
#else
	sca_out(0x0F, msci+RNR, card); /* +1=RX DMA activation condition */
	/* Setting than to larger value may cause Illegal Access */
	sca_out(0x20, msci+TNR0, card); /* =TX DMA activation condition */
	sca_out(0x30, msci+TNR1, card); /* +1=TX DMA deactivation condition */
	sca_out(0x04, msci+TCR, card); /* =Critical TX DMA activ condition */
#endif

	sca_set_clock(port, port->clkmode);
	open_port(port);
  
#ifdef __HD64570_H
	/* MSCI TX INT IRQ enable */
	sca_out(IE0_TXINT, msci+IE0, card);
	sca_out(IE1_UDRN, msci+IE1, card); /* TX underrun IRQ */
	sca_out(sca_in(IER0, card) | (phy_node(port) ? 0x80 : 0x08),
		IER0, card);
	/* DMA IRQ enable */
	sca_out(sca_in(IER1, card) | (phy_node(port) ? 0xF0 : 0x0F),
		IER1, card);
#else
	/* MSCI TX INT and underrrun IRQ enable */
	sca_outl(IE0_TXINT|IE0_UDRN, msci+IE0, card);
	/* DMA & MSCI IRQ enable */
	sca_outl(sca_in(IER0, card) |
		 (phy_node(port) ? 0x02006600 : 0x00020066), IER0, card);
#endif

	sca_out(CMD_TX_ENABLE, msci+CMD, card);
	sca_out(CMD_RX_ENABLE, msci+CMD, card);
}



static int sca_ioctl(hdlc_device *hdlc, struct ifreq *ifr, int cmd)
{
	int value;
	port_t *port=hdlc_to_port(hdlc);
#ifdef DEBUG_RINGS
	card_t *card=port_to_card(port);
	u16 cnt;
#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	u8 page;
#endif
#endif

	if(!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch(cmd) {
	case HDLCSETLINE:
		value = ifr->ifr_ifru.ifru_ivalue;
		switch (value) {
		case LINE_LOOPBACK:
			sca_set_loopback(port, 1);
			return 0;

		case LINE_NOLOOPBACK:
			sca_set_loopback(port, 0);
			return 0;

		default:
			if (value == LINE_EXT_CLOCK || value >= LINE_MIN_CLOCK)
				return sca_set_clock(port, value);
		}
		return select_phys_iface(port, value); /* Last resort */
		
#ifdef DEBUG_RINGS
	default:
#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
		page=sca_get_page(card);
		openwin(card, 0);
#endif

		printk(KERN_ERR "RX ring: CDA=%u EDA=%u DSR=%02X in=%u "
		       "%sactive",
		       sca_ina(get_dmac_rx(port)+CDAL, card),
		       sca_ina(get_dmac_rx(port)+EDAL, card),
		       sca_in(DSR_RX(phy_node(port)), card),
		       port->rxin,
		       sca_in(DSR_RX(phy_node(port)), card) & DSR_DE?"":"in");
		for (cnt=0; cnt<port_to_card(port)->ring_buffers; cnt++)
			printk(" %02X",
			       readb(&(desc_address(port, cnt, 0)->stat)));
		
		printk("\n" KERN_ERR "TX ring: CDA=%u EDA=%u DSR=%02X in=%u "
		       "last=%u %sactive",
		       sca_ina(get_dmac_tx(port)+CDAL, card),
		       sca_ina(get_dmac_tx(port)+EDAL, card),
		       sca_in(DSR_TX(phy_node(port)), card), port->txin,
		       port->txlast,
		       sca_in(DSR_TX(phy_node(port)), card) & DSR_DE?"":"in");
      
		for (cnt=0; cnt<port_to_card(port)->ring_buffers; cnt++)
			printk(" %02X",
			       readb(&(desc_address(port, cnt, 1)->stat)));
		printk("\n");

		printk(KERN_ERR "MSCI: MD: %02x %02x %02x, "
		       "ST: %02x %02x %02x %02x"
#ifdef __HD64572_H
		       " %02x"
#endif
		       ", FST: %02x CST: %02x %02x\n",
		       sca_in(get_msci(port)+MD0, card),
		       sca_in(get_msci(port)+MD1, card),
		       sca_in(get_msci(port)+MD2, card),
		       sca_in(get_msci(port)+ST0, card),
		       sca_in(get_msci(port)+ST1, card),
		       sca_in(get_msci(port)+ST2, card),
		       sca_in(get_msci(port)+ST3, card),
#ifdef __HD64572_H
		       sca_in(get_msci(port)+ST4, card),
#endif
		       sca_in(get_msci(port)+FST, card),
		       sca_in(get_msci(port)+CST0, card),
		       sca_in(get_msci(port)+CST1, card));

#ifdef __HD64572_H
		printk(KERN_ERR "ILAR: %02x\n", sca_in(ILAR, card));
#endif

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
		openwin(card, page); /* Restore original page */
#endif
		return 0;
#endif /* DEBUG_RINGS */
	}
	return -EINVAL;
}



static int sca_open(hdlc_device *hdlc)
{
	port_t *port=hdlc_to_port(hdlc);

	MOD_INC_USE_COUNT;
	sca_set_hdlc_mode(port, 1, MD0_CRC_ITU, 0);
	return 0;
}


static void sca_close(hdlc_device *hdlc)
{
	port_t *port=hdlc_to_port(hdlc);

	/* reset channel */
	sca_out(CMD_RESET, get_msci(port) + CMD, port_to_card(port));
	close_port(port);
	MOD_DEC_USE_COUNT;
}



static int sca_xmit(struct sk_buff *skb, struct device *dev)
{
	port_t *port = dev_to_port(dev);
	card_t *card = port_to_card(port);
	pkt_desc *desc;
	u32 buff, len;
#ifndef ALL_PAGES_ALWAYS_MAPPED
	u8 page;
	u32 maxlen;
#endif

#ifdef DEBUG_PKT
	int i;
#endif

	/* We assume SCA/SCA-II never hangs */
	if (test_bit(1, &dev->tbusy)) /* Transmitter buffer still full */
		return 1;

	/* Block a transmit from overlapping.
	   Would waiting on semaphore here be more effective? */
	if (test_and_set_bit(0, &dev->tbusy))
		return 1;

	desc = desc_address(port, port->txin+1, 1);
	if (readb(&desc->stat)) { /* allow 1 packet gap */
#ifdef DEBUG_PKT
		printk(KERN_DEBUG "%s: transmitter buffer full\n", dev->name);
#endif
		set_bit(1, &dev->tbusy); /* Transmitter buffer full */
		clear_bit(0, &dev->tbusy); /* no hard_start_xmit running */
		return 1;	/* request packet to be queued */
	}

#ifdef DEBUG_PKT
	printk(KERN_DEBUG "%s TX:", dev->name);
	for (i=0; i<skb->len; i++)
		printk(" %02X", skb->data[i]);
	printk("\n");
#endif

	desc = desc_address(port, port->txin, 1);
	buff = buffer_offset(port, port->txin, 1);
	len = skb->len;
#ifndef ALL_PAGES_ALWAYS_MAPPED
	page = buff / winsize(card);
	buff = buff % winsize(card);
	maxlen = winsize(card) - buff;
  
	openwin(card, page);
	if (len > maxlen) {
		memcpy_toio(winbase(card) + buff, skb->data, maxlen);
		openwin(card, page+1);
		memcpy_toio(winbase(card), skb->data + maxlen, len - maxlen);
	}
	else
#endif
		memcpy_toio(winbase(card) + buff, skb->data, len);

#if !defined(PAGE0_ALWAYS_MAPPED) && !defined(ALL_PAGES_ALWAYS_MAPPED)
	openwin(card, 0);	/* select pkt_desc table page back */
#endif
	writew(len, &desc->len);
	writeb(ST_TX_EOM, &desc->stat);

	port->txin = next_desc(port, port->txin);
	sca_outa(desc_offset(port, port->txin, 1),
		 get_dmac_tx(port)+EDAL, card);
  
	sca_out(DSR_DE, DSR_TX(phy_node(port)), card); /* Enable TX DMA */
	clear_bit(0, &dev->tbusy); /* no hard_start_xmit running */

	dev_kfree_skb(skb);
	return 0;
}


static void sca_init(card_t *card, int wait_states)
{
	sca_out(wait_states, WCRL, card); /* Wait Control */
	sca_out(wait_states, WCRM, card);
	sca_out(wait_states, WCRH, card);

	sca_out(0, DMER, card);	/* DMA Master disable */
	sca_out(0x03, PCR, card); /* DMA priority */
	sca_out(0, IER1, card);	/* DMA interrupt disable */
	sca_out(0, DSR_RX(0), card); /* DMA disable - to halt state */
	sca_out(0, DSR_TX(0), card);
	sca_out(0, DSR_RX(1), card);
	sca_out(0, DSR_TX(1), card);
	sca_out(DMER_DME, DMER, card); /* DMA Master enable */
}
