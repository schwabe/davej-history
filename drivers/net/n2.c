/*
 * SDL Inc. RISCom/N2 synchronous serial card driver for Linux
 *
 * Copyright (C) 1998-2000 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Requires hdlc.c, a generic HDLC support module
 * Available from ftp://ftp.pm.waw.pl/pub/Linux/hdlc/
 *
 * Current status:
 *    - this is work in progress
 *    - no support for integrated CSU/DSU/DDS
 *    - no support for SMP yet
 *
 * Tested with dual channel card with 2 Mbps FR links and with loopback
 * at 10Mbps.
 *
 * Sources of information:
 *    Hitachi HD64570 SCA User's Manual
 *    SDL Inc. PPP/HDLC/CISCO driver
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/io.h>

#include <linux/netdevice.h>

#include "syncppp.h"
#include <linux/hdlc.h>
#include "hd64570.h"

#define DEBUG_RINGS
/* #define DEBUG_PKT */

static const char* version = "SDL RISCom/N2 driver revision: 1.0-pre9";
static const char* devname = "RISCom/N2";

#define USE_WINDOWSIZE 16384
#define USE_BUS16BITS 1
#define CLOCK_BASE 9830400	/* 9.8304 MHz */

#define N2_IOPORTS 0x10

static char *hw=NULL;	/* pointer to hw=xxx command line string */

/* RISCom/N2 Board Registers */

/* PC Control Register */
#define N2_PCR 0
#define PCR_RUNSCA 1     /* Run 64570 */
#define PCR_VPM    2     /* Enable VPM - needed if using RAM above 1 MB */
#define PCR_ENWIN  4     /* Open window */
#define PCR_BUS16  8     /* 16-bit bus */


/* Memory Base Address Register */
#define N2_BAR 2


/* Page Scan Register  */
#define N2_PSR 4
#define WIN16K       0x00
#define WIN32K       0x20
#define WIN64K       0x40
#define PSR_WINBITS  0x60
#define PSR_DMAEN    0x80
#define PSR_PAGEBITS 0x0F


/* Modem Control Reg */
#define N2_MCR 6
#define CLOCK_OUT_PORT1 0x80
#define CLOCK_OUT_PORT0 0x40
#define TX422_PORT1     0x20
#define TX422_PORT0     0x10
#define DSR_PORT1       0x08
#define DSR_PORT0       0x04
#define DTR_PORT1       0x02
#define DTR_PORT0       0x01


typedef struct port_s {
	hdlc_device hdlc;	/* HDLC device struct - must be first */
	struct card_s *card;
	int clkmode;		/* clock speed */
	int loopback;
	u8 valid;		/* port enabled */
	u8 phy_node;		/* physical port # - 0 or 1 */
	u8 log_node;		/* logical port # */
	u8 rxin;		/* rx ring buffer 'in' pointer */
	u8 txin;		/* tx ring buffer 'in' and 'last' pointers */
	u8 txlast;
}port_t;



typedef struct card_s {
	u8 *winbase;		/* ISA window base address */
	u32 ram_size;		/* number of bytes */
	u16 io;			/* IO Base address */
	u16 buff_offset;	/* offset of first buffer of first channel */
	u8 irq;			/* IRQ (3-15) */
	u8 ring_buffers;	/* number of buffers in a ring */

	port_t ports[2];
	struct card_s *next_card;
}card_t;



#define sca_reg(reg, card) (0x8000 | (card)->io | \
			    ((reg)&0x0F) | (((reg)&0xF0) << 6))
#define sca_in(reg, card)		inb(sca_reg(reg, card))
#define sca_out(value, reg, card)	outb(value, sca_reg(reg, card))
#define sca_inw(reg, card)		inw(sca_reg(reg, card))
#define sca_outw(value, reg, card)	outw(value, sca_reg(reg, card))

#define port_to_card(port)		((port)->card)
#define log_node(port)			((port)->log_node)
#define phy_node(port)			((port)->phy_node)
#define winsize(card)			(USE_WINDOWSIZE)
#define winbase(card)      	     	((card)->winbase)
#define get_port(card, port)		((card)->ports[port].valid ? \
					 &(card)->ports[port] : NULL)


static __inline__ u8 sca_get_page(card_t *card)
{
	return inb(card->io+N2_PSR) & PSR_PAGEBITS;
}


static __inline__ void openwin(card_t *card, u8 page)
{
	u8 psr=inb(card->io+N2_PSR);
	outb((psr & ~PSR_PAGEBITS) | page, card->io+N2_PSR);
}


static __inline__ void close_windows(card_t *card)
{
	outb(inb(card->io+N2_PCR) & ~PCR_ENWIN, card->io+N2_PCR);
}


static __inline__ int set_clock(port_t *port, int clock)
{
	int io=port->card->io;
	u8 mcr = inb(io+N2_MCR);
  
	if (clock == LINE_EXT_CLOCK) /* External clock */
		mcr &= port->phy_node ? ~CLOCK_OUT_PORT1 : ~CLOCK_OUT_PORT0;
	else
		mcr |= port->phy_node ? CLOCK_OUT_PORT1 : CLOCK_OUT_PORT0;

	outb(mcr, io+N2_MCR);
	port->clkmode = clock;
	return 0;
}



#define select_phys_iface(port, iface) (-EINVAL) /* No hardware support */


static __inline__ void open_port(port_t *port)
{
	int io=port->card->io;
	u8 mcr = inb(io+N2_MCR) | (port->phy_node ? TX422_PORT1 : TX422_PORT0);

	mcr &= port->phy_node ? ~DTR_PORT1 : ~DTR_PORT0; /* set DTR ON */
	outb(mcr, io+N2_MCR);
  
	outb(inb(io+N2_PCR) | PCR_ENWIN, io+N2_PCR); /* open window */
	outb(inb(io+N2_PSR) | PSR_DMAEN, io+N2_PSR); /* enable dma */
}



static __inline__ void close_port(port_t *port)
{
	int io=port->card->io;
	u8 mcr = inb(io+N2_MCR) | (port->phy_node ? TX422_PORT1 : TX422_PORT0);

	/* set DTR OFF */
	mcr |= port->phy_node ? DTR_PORT1 : DTR_PORT0;
	outb(mcr, io+N2_MCR);
}



#include "hd6457x.c"


static u8 n2_count_page(card_t *card)
{
	u8 page;
	int i, bcount=USE_WINDOWSIZE, wcount=USE_WINDOWSIZE/2;
	u16 *dp=(u16*)card->winbase;
	u8 *bp=(u8*)card->winbase;
	u8 psr=inb(card->io+N2_PSR) & PSR_WINBITS;


	for (page=0; page<16; page++) {
		outb(psr | page, card->io+N2_PSR); /* select a page */
		writeb(page, dp);
		if (readb(dp) != page)
			break;	/* If can't read back, no good memory */

		outb(psr, card->io+N2_PSR); /* goto page 0 */
		if (readb(dp))
			break;	/* If page 0 changed, then wrapped around */

		outb(psr | page, card->io+N2_PSR); /* select page again */

		/*  first do byte tests */
		for (i = 0; i < bcount; i++)
			writeb(i, bp + i);
		for (i = 0; i < bcount; i++)
			if (readb(bp + i) != (i & 0xff))
				return 0;

		for (i = 0; i < bcount; i++)
			writeb(~i, bp + i);
		for (i = 0; i < bcount; i++)
			if (readb(bp + i) != (~i & 0xff))
				return 0;

		/* next do 16-bit tests */
		for (i = 0; i < wcount; i++)
			writew(0x55AA, dp + i);
		for (i = 0; i < wcount; i++)
			if (readw(dp + i) != 0x55AA)
				return 0;

		for (i = 0; i < wcount; i++)
			writew(0xAA55, dp + i);
		for (i = 0; i < wcount; i++)
			if (readw(dp + i) != 0xAA55)
				return 0;

		for (i = 0; i < wcount; i++)
			writew(page, dp + i);
	}

	return page;
}



static void n2_destroy_card(card_t *card)
{
	int cnt;

	for (cnt=0; cnt<2; cnt++)
		if (card->ports[cnt].card)
			unregister_hdlc_device(&card->ports[cnt].hdlc);

	if (card->irq)
		free_irq(card->irq, card);

	if (card->io)
		release_region(card->io, N2_IOPORTS);
	kfree(card);
}



static int n2_run(unsigned long io, unsigned long irq, unsigned long winbase,
		  long valid0, long valid1)
{
	card_t *card;
	u8 cnt, pcr;

	if (io<0x200 || io>0x3FF || (io % N2_IOPORTS)!=0) {
		printk(KERN_ERR "n2: invalid I/O port value\n");
		return -ENODEV;
	}

	if (irq<3 || irq>15 || irq == 6) /* FIXME */ {
		printk(KERN_ERR "n2: invalid IRQ value\n");
		return -ENODEV;
	}
    
	if (winbase<0xA0000 || winbase>0xFFFFF || (winbase&0xFFF)!=0) {
		printk(KERN_ERR "n2: invalid RAM value\n");
		return -ENODEV;
	}

	if (check_region(io, N2_IOPORTS)) {
		printk(KERN_ERR "n2: I/O port region in use\n");
		return -EBUSY;
	}

	card=kmalloc(sizeof(card_t), GFP_KERNEL);
	if (card==NULL) {
		printk(KERN_ERR "n2: unable to allocate memory\n");
		return -ENOBUFS;
	}
	memset(card, 0, sizeof(card_t));

	request_region(io, N2_IOPORTS, devname);
	card->io=io;

	if (request_irq(irq, &sca_intr, 0, devname, card)) {
		printk(KERN_ERR "n2: could not allocate IRQ\n");
		n2_destroy_card(card);
		return(-EBUSY);
	}
	card->irq=irq;

	card->winbase=(u8*)winbase;

	outb(0, io+N2_PCR);
	outb(winbase>>12, io+N2_BAR);

	switch (USE_WINDOWSIZE) {
	case 16384:
		outb(WIN16K, io+N2_PSR);
		break;

	case 32768:
		outb(WIN32K, io+N2_PSR);
		break;

	case 65536:
		outb(WIN64K, io+N2_PSR);
		break;

	default:
		printk(KERN_ERR "n2: invalid window size\n");
		n2_destroy_card(card);
		return -ENODEV;
	}

	pcr = PCR_ENWIN | PCR_VPM | (USE_BUS16BITS ? PCR_BUS16 : 0);
	outb(pcr, io+N2_PCR);

	cnt = n2_count_page(card);
	if (!cnt) {
		printk(KERN_ERR "n2: memory test failed.\n");
		n2_destroy_card(card);
		return -EIO;
	}

	card->ram_size=cnt*USE_WINDOWSIZE;

	/* 4 rings required for 2 ports, 2 rings for one port */
	card->ring_buffers = card->ram_size /
		((valid0 + valid1) * 2 * (sizeof(pkt_desc)+HDLC_MAX_MTU));

	card->buff_offset = (valid0 + valid1) * 2 * (sizeof(pkt_desc))
		* card->ring_buffers;

	printk(KERN_DEBUG "n2: RISCom/N2 %u KB RAM, IRQ%u, "
	       "using %u packets rings\n", card->ram_size/1024, card->irq,
	       card->ring_buffers);

	pcr |= PCR_RUNSCA;		/* run SCA */
	outb(pcr, io+N2_PCR);
	outb(0, io+N2_MCR);

	sca_init(card, 0);
	for (cnt=0; cnt<2; cnt++) {
		port_t *port = &card->ports[cnt];

		if ((cnt == 0 && !valid0) || (cnt == 1 && !valid1))
			continue;

		port->phy_node = cnt;
		port->valid = 1;

		if ((cnt == 1) && valid0)
			port->log_node = 1;

		hdlc_to_dev(&port->hdlc)->irq=irq;
		hdlc_to_dev(&port->hdlc)->mem_start = winbase;
		hdlc_to_dev(&port->hdlc)->mem_end = winbase + USE_WINDOWSIZE-1;
		hdlc_to_dev(&port->hdlc)->tx_queue_len=50;
		port->hdlc.ioctl=sca_ioctl;
		port->hdlc.open=sca_open;
		port->hdlc.close=sca_close;
		hdlc_to_dev(&port->hdlc)->hard_start_xmit=sca_xmit;

		if (register_hdlc_device(&port->hdlc)) {
			printk(KERN_WARNING "n2: unable to register hdlc "
			       "device\n");
			n2_destroy_card(card);
			return -ENOBUFS;
		}
		port->card = card;
		sca_init_sync_port(port); /* Set up SCA memory */

		printk(KERN_INFO "%s: RISCom/N2 node %d\n", port->hdlc.name,
		       port->phy_node);
	}

	*new_card=card;
	new_card=&card->next_card;

	return 0;
}



void n2_init(void)
{
	if (hw==NULL)
		return;

	printk(KERN_INFO "%s\n", version);

	do {
		unsigned long io, irq, ram;
		long valid[2] = { 0, 0 }; /* Default = both ports disabled */

		io=simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		irq=simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		ram=simple_strtoul(hw, &hw, 0);

		if (*hw++ != ',')
			break;
		while(1) {
			if (*hw=='0' && !valid[0])
				valid[0]=1; /* Port 0 enabled */
			else if (*hw=='1' && !valid[1])
				valid[1]=1; /* Port 1 enabled */
			else
				break;
			hw++;
		}
      
		if (!valid[0] && !valid[1])
			break;	/* at least one port must be used */

		if (*hw == ':' || *hw == '\x0')
			n2_run(io, irq, ram, valid[0], valid[1]);

		if (*hw == '\x0')
			return;
	}while(*hw++ == ':');

	printk(KERN_ERR "n2: invalid hardware parameters\n");
}


#ifndef MODULE

void n2_setup(char *str, int *ints)
{
	hw=str;
}

#else

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("RISCom/N2 serial port driver");
MODULE_PARM(hw, "s");		/* hw=io,irq,ram,ports:io,irq,... */
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if (hw==NULL) {
		printk(KERN_ERR "n2: no card initialized\n");
		return -ENOSYS;	/* no parameters specified, abort */
	}

	n2_init();
	return first_card ? 0 : -ENOSYS;
}



void cleanup_module(void)
{
	card_t *card=first_card;

	while (card) {
		card_t *ptr=card;
		card=card->next_card;
		n2_destroy_card(ptr);
	}
}

#endif /* MODULE */
