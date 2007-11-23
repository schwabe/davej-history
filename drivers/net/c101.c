/*
 * Moxa C101 synchronous serial card driver for Linux
 *
 * Copyright (C) 2000 Krzysztof Halasa <khc@pm.waw.pl>
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
 *    - no support for SMP yet
 *
 * Sources of information:
 *    Hitachi HD64570 SCA User's Manual
 *    Moxa C101 User's Manual
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <asm/io.h>
#include <asm/delay.h>

#include "syncppp.h"
#include <linux/hdlc.h>
#include "hd64570.h"

#define DEBUG_RINGS
#undef DEBUG_PKT

static const char* version = "Moxa C101 driver revision: 1.0-pre9";
static const char* devname = "C101";

#define C101_PAGE 0x1D00
#define C101_DTR 0x1E00
#define C101_SCA 0x1F00
#define C101_WINDOW_SIZE 0x2000

#define RAM_SIZE (256*1024)
#define CLOCK_BASE 9830400	/* 9.8304 MHz */
#define PAGE0_ALWAYS_MAPPED

static char *hw=NULL;		/* pointer to hw=xxx command line string */


typedef struct card_s {
	hdlc_device hdlc;	/* HDLC device struct - must be first */
	int clkmode;		/* clock speed, 0=ext */
	int loopback;
	u8 *win0base;		/* ISA window base address */
	u16 buff_offset;	/* offset of first buffer of first channel */
	u8 irq;			/* IRQ (3-15) */
	u8 ring_buffers;	/* number of buffers in a ring */
	u8 page;

	u8 rxin;		/* rx ring buffer 'in' pointer */
	u8 txin;		/* tx ring buffer 'in' and 'last' pointers */
	u8 txlast;

	struct card_s *next_card;
}card_t;

typedef card_t port_t;


#define sca_in(reg, card)	   readb((card)->win0base+C101_SCA+(reg))
#define sca_out(value, reg, card)  writeb(value, (card)->win0base+C101_SCA+(reg))
#define sca_inw(reg, card)	   readw((card)->win0base+C101_SCA+(reg))
#define sca_outw(value, reg, card) writew(value, (card)->win0base+C101_SCA+(reg))

#define port_to_card(port)	     (port)
#define log_node(port)		     (0)
#define phy_node(port)		     (0)
#define winsize(card)		     (C101_WINDOW_SIZE)
#define win0base(card)		     ((card)->win0base)
#define winbase(card)      	     ((card)->win0base+0x2000)
#define get_port(card, port)	     ((port) == 0 ? (card) : NULL)


static __inline__ u8 sca_get_page(card_t *card)
{
	return card->page;
}



static __inline__ void openwin(card_t *card, u8 page)
{
	card->page=page;
	writeb(page, card->win0base+C101_PAGE);
}


#define close_windows(card) {} /* no hardware support */


static __inline__ int set_clock(port_t *port, int clock)
{
	port->clkmode = clock;	/* selectable by jumper */
	return 0;
}


#define select_phys_iface(port, iface) (-EINVAL) /* No hardware support */


static __inline__ void open_port(port_t *port)
{
	writeb(1, port->win0base+C101_DTR);
	sca_out(0, MSCI1_OFFSET + CTL, port); /* RTS uses ch#2 output */
}


static __inline__ void close_port(port_t *port)
{
	writeb(0, port->win0base+C101_DTR);
	sca_out(CTL_NORTS, MSCI1_OFFSET + CTL, port);
}


#include "hd6457x.c"


static void c101_destroy_card(card_t *card)
{
	if (card->irq)
		free_irq(card->irq, card);

	kfree(card);
}



static int c101_run(unsigned long irq, unsigned long winbase)
{
	card_t *card;
	int result;


	if (irq<3 || irq>15 || irq == 6) /* FIXME */ {
		printk(KERN_ERR "c101: invalid IRQ value\n");
		return -ENODEV;
	}
    
	if (winbase<0xC0000 || winbase>0xDFFFF || (winbase&0x3FFF)!=0) {
		printk(KERN_ERR "c101: invalid RAM value\n");
		return -ENODEV;
	}

	card=kmalloc(sizeof(card_t), GFP_KERNEL);
	if (card==NULL) {
		printk(KERN_ERR "c101: unable to allocate memory\n");
		return -ENOBUFS;
	}
	memset(card, 0, sizeof(card_t));

	if (request_irq(irq, sca_intr, 0, devname, card)) {
		printk(KERN_ERR "c101: could not allocate IRQ\n");
		c101_destroy_card(card);
		return(-EBUSY);
	}
	card->irq=irq;

	card->win0base=(u8*)winbase;

	/* 2 rings required for 1 port */
	card->ring_buffers = (RAM_SIZE-C101_WINDOW_SIZE) / (2 * HDLC_MAX_MTU);
	printk(KERN_DEBUG "c101: using %u packets rings\n",card->ring_buffers);

	card->buff_offset = C101_WINDOW_SIZE; /* Bytes 1D00-1FFF reserved */

	readb(card->win0base+C101_PAGE); /* Resets SCA? */
	udelay(100);
	writeb(0, card->win0base+C101_PAGE);
	writeb(0, card->win0base+C101_DTR); /* Power-up for RAM? */

	sca_init(card, 0);

	card->hdlc.ioctl=sca_ioctl;
	card->hdlc.open=sca_open;
	card->hdlc.close=sca_close;
	hdlc_to_dev(&card->hdlc)->hard_start_xmit=sca_xmit;
	hdlc_to_dev(&card->hdlc)->irq=irq;
	hdlc_to_dev(&card->hdlc)->tx_queue_len=50;

	result=register_hdlc_device(&card->hdlc);
	if (result) {
		printk(KERN_WARNING "c101: unable to register hdlc device\n");
		c101_destroy_card(card);
		return result;
	}

	sca_init_sync_port(card); /* Set up C101 memory */

	*new_card=card;
	new_card=&card->next_card;
	return 0;
}



void c101_init(void)
{
	if (hw==NULL)
		return;

	printk(KERN_INFO "%s\n", version);

	do {
		unsigned long irq, ram;

		irq=simple_strtoul(hw, &hw, 0);
    
		if (*hw++ != ',')
			break;
		ram=simple_strtoul(hw, &hw, 0);

		if (*hw == ':' || *hw == '\x0')
			c101_run(irq, ram);

		if (*hw == '\x0')
			return;
	}while(*hw++ == ':');

	printk(KERN_ERR "c101: invalid hardware parameters\n");
}


#ifndef MODULE

void c101_setup(char *str, int *ints)
{
	hw=str;
}

#else

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("Moxa C101 serial port driver");
MODULE_PARM(hw, "s");		/* hw=irq,ram,clock:irq,... */
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if (hw==NULL) {
		printk(KERN_ERR "c101: no card initialized\n");
		return -ENOSYS;	/* no parameters specified, abort */
	}

	c101_init();
	return first_card ? 0 : -ENOSYS;
}



void cleanup_module(void)
{
	card_t *card=first_card;

	while (card) {
		card_t *ptr=card;
		card=card->next_card;
		unregister_hdlc_device(&ptr->hdlc);
		c101_destroy_card(ptr);
	}
}

#endif /* MODULE */
