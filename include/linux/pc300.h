/*
 * pc300.h	Cyclades-PC300(tm) Kernel API Definitions.
 *
 * Author:	Ivan Passos <ivan@cyclades.com>
 *
 * Copyright:	(c) 1999-2000 Cyclades Corp.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * $Log: pc300.h,v $
 * Revision 2.2 2000/06/23 ivan
 * Inclusion of 'loopback' field on structure 'pc300chconf', to allow 
 * loopback mode operation.
 * 
 * Revision 2.1 2000/06/09 ivan
 * Changes to use the new generic HDLC layer in the driver.
 *
 * Revision 2.0 2000/03/27 ivan
 * Added support for the PC300/TE cards.
 *
 * Revision 1.1 2000/01/31 ivan
 * Replaced 'pc300[drv|sca].h' former PC300 driver include files.
 *
 * Revision 1.0 1999/12/16 ivan
 * First official release.
 * Inclusion of 'nchan' field on structure 'pc300hw', to allow variable 
 * number of ports per card.
 * Inclusion of 'if_ptr' field on structure 'pc300dev'.
 *
 * Revision 0.6 1999/11/17 ivan
 * Changed X.25-specific function names to comply with adopted convention.
 *
 * Revision 0.5 1999/11/16 Daniela Squassoni
 * X.25 support.
 *
 * Revision 0.4 1999/11/15 ivan
 * Inclusion of 'clock' field on structure 'pc300hw'.
 *
 * Revision 0.3 1999/11/10 ivan
 * IOCTL name changing.
 * Inclusion of driver function prototypes.
 *
 * Revision 0.2 1999/11/03 ivan
 * Inclusion of 'tx_skb' and union 'ifu' on structure 'pc300dev'.
 *
 * Revision 0.1 1999/01/15 ivan
 * Initial version.
 *
 */

#ifndef	_PC300_H
#define	_PC300_H

#ifndef __HDLC_H
#include <linux/hdlc.h>
#endif
#ifndef _HD64572_H
#include <linux/hd64572.h>
#endif
#ifndef _FALC_LH_H
#include <linux/falc-lh.h>
#endif

#ifndef CY_TYPES
#define CY_TYPES
#if defined(__alpha__)
typedef	unsigned long	ucdouble;	/* 64 bits, unsigned */
typedef	unsigned int	uclong;		/* 32 bits, unsigned */
#else
typedef	unsigned long	uclong;		/* 32 bits, unsigned */
#endif
typedef	unsigned short	ucshort;	/* 16 bits, unsigned */
typedef	unsigned char	ucchar;		/* 8 bits, unsigned */
#endif /* CY_TYPES */

#define	PC300_DEVNAME	"hdlc"	/* Dev. name base (for hdlc0, hdlc1, etc.) */
#define	PC300_MAXINDEX	100	/* Max dev. name index (the '0' in hdlc0) */

#define	PC300_MAXCARDS	4	/* Max number of cards per system */
#define	PC300_MAXCHAN	2	/* Number of channels per card */

#define	PC300_PLX_WIN	0x80    /* PLX control window size (128b) */
#define	PC300_RAMSIZE	0x80000 /* RAM window size (512Kb) */
#define	PC300_SCASIZE	0x400   /* SCA window size (1Kb) */
#define	PC300_FALCSIZE	0x400	/* FALC window size (1Kb) */

#define PC300_OSC_CLOCK	24576000
#define PC300_PCI_CLOCK	33000000

#define BD_DEF_LEN	0x0800	/* DMA buffer length (2KB) */
#define DMA_TX_MEMSZ	0x8000	/* Total DMA Tx memory size (32KB/ch) */
#define DMA_RX_MEMSZ	0x10000	/* Total DMA Rx memory size (64KB/ch) */

#define N_DMA_TX_BUF	(DMA_TX_MEMSZ / BD_DEF_LEN)	/* DMA Tx buffers */
#define N_DMA_RX_BUF	(DMA_RX_MEMSZ / BD_DEF_LEN)	/* DMA Rx buffers */

/* DMA Buffer Offsets */
#define DMA_TX_BASE	((N_DMA_TX_BUF + N_DMA_RX_BUF) *	\
			 PC300_MAXCHAN * sizeof(pcsca_bd_t))
#define DMA_RX_BASE	(DMA_TX_BASE + PC300_MAXCHAN*DMA_TX_MEMSZ)

/* DMA Descriptor Offsets */
#define DMA_TX_BD_BASE	0x0000
#define DMA_RX_BD_BASE	(DMA_TX_BD_BASE + ((PC300_MAXCHAN*DMA_TX_MEMSZ / \
				BD_DEF_LEN) * sizeof(pcsca_bd_t)))

/* DMA Descriptor Macros */
#define TX_BD_ADDR(chan, n)	(DMA_TX_BD_BASE + \
				 ((N_DMA_TX_BUF*chan) + n) * sizeof(pcsca_bd_t))
#define RX_BD_ADDR(chan, n)	(DMA_RX_BD_BASE + \
				 ((N_DMA_RX_BUF*chan) + n) * sizeof(pcsca_bd_t))

/* Macro to access the FALC registers (TE only) */
#define F_REG(reg, chan)	(0x200*(chan) + ((reg)<<2))

/***************************************
 * Memory access functions/macros      *
 * (required to support Alpha systems) *
 ***************************************/
#ifdef __KERNEL__
#define cpc_writeb(port,val)	{writeb((ucchar)(val),(ulong)(port)); mb();}
#define cpc_writew(port,val)	{writew((ushort)(val),(ulong)(port)); mb();}
#define cpc_writel(port,val)	{writel((uclong)(val),(ulong)(port)); mb();}

#define cpc_readb(port)		readb(port)
#define cpc_readw(port)		readw(port)
#define cpc_readl(port)		readl(port)

#else /* __KERNEL__ */
#define cpc_writeb(port,val)	(*(volatile ucchar *)(port) = (ucchar)(val))
#define cpc_writew(port,val)	(*(volatile ucshort *)(port) = (ucshort)(val))
#define cpc_writel(port,val)	(*(volatile uclong *)(port) = (uclong)(val))

#define cpc_readb(port)		(*(volatile ucchar *)(port))
#define cpc_readw(port)		(*(volatile ucshort *)(port))
#define cpc_readl(port)		(*(volatile uclong *)(port))

#endif /* __KERNEL__ */

/****** Data Structures *****************************************************/

/*
 *      RUNTIME_9050 - PLX PCI9050-1 local configuration and shared runtime
 *      registers. This structure can be used to access the 9050 registers
 *      (memory mapped).
 */
struct RUNTIME_9050 {
	uclong	loc_addr_range[4];	/* 00-0Ch : Local Address Ranges */
	uclong	loc_rom_range;		/* 10h : Local ROM Range */
	uclong	loc_addr_base[4];	/* 14-20h : Local Address Base Addrs */
	uclong	loc_rom_base;		/* 24h : Local ROM Base */
	uclong	loc_bus_descr[4];	/* 28-34h : Local Bus Descriptors */
	uclong	rom_bus_descr;		/* 38h : ROM Bus Descriptor */
	uclong	cs_base[4];		/* 3C-48h : Chip Select Base Addrs */
	uclong	intr_ctrl_stat;		/* 4Ch : Interrupt Control/Status */
	uclong	init_ctrl;		/* 50h : EEPROM ctrl, Init Ctrl, etc */
};

#define PLX_9050_LINT1_ENABLE	0x01
#define PLX_9050_LINT1_POL	0x02
#define PLX_9050_LINT1_STATUS	0x04
#define PLX_9050_LINT2_ENABLE	0x08
#define PLX_9050_LINT2_POL	0x10
#define PLX_9050_LINT2_STATUS	0x20
#define PLX_9050_INTR_ENABLE	0x40
#define PLX_9050_SW_INTR	0x80

/* Masks to access the init_ctrl PLX register */
#define	PC300_CLKSEL_MASK		(0x00000004UL)
#define	PC300_CHMEDIA_MASK(chan)	(0x00000020UL<<(chan*3))
#define	PC300_CTYPE_MASK		(0x00000800UL)

/* CPLD Registers (base addr = falcbase, TE only) */
#define CPLD_REG1	0x140	/* Chip resets, DCD/CTS status */
#define CPLD_REG2	0x144	/* Clock enable , LED control */

/* CPLD Register bit description: for the FALC bits, they should always be 
   set based on the channel (use (bit<<(2*ch)) to access the correct bit for 
   that channel) */
#define CPLD_REG1_FALC_RESET	0x01
#define CPLD_REG1_SCA_RESET	0x02
#define CPLD_REG1_GLOBAL_CLK	0x08
#define CPLD_REG1_FALC_DCD	0x10
#define CPLD_REG1_FALC_CTS	0x20

#define CPLD_REG2_FALC_TX_CLK	0x01
#define CPLD_REG2_FALC_RX_CLK	0x02
#define CPLD_REG2_FALC_LED1	0x10
#define CPLD_REG2_FALC_LED2	0x20

/* Structure with FALC-related fields (TE only) */
#define PC300_FALC_MAXLOOP	0x0000ffff	/* for falc_issue_cmd() */

typedef struct falc {
	ucchar sync;		/* If true FALC is synchronized */
	ucchar active;		/* if TRUE then already active */
	ucchar loop_active;	/* if TRUE a line loopback UP was received */
	ucchar loop_gen;	/* if TRUE a line loopback UP was issued */

	ucchar num_channels;
	ucchar offset;		/* 1 for T1, 0 for E1 */
	ucchar full_bandwidth;

	ucchar xmb_cause;
	ucchar multiframe_mode;

	/* Statistics */
	ucshort pden;	/* Pulse Density violation count */
	ucshort los;	/* Loss of Signal count */
	ucshort losr;	/* Loss of Signal recovery count */
	ucshort lfa;	/* Loss of frame alignment count */
	ucshort farec;	/* Frame Alignment Recovery count */
	ucshort lmfa;	/* Loss of multiframe alignment count */
	ucshort ais;	/* Remote Alarm indication Signal count */
	ucshort sec;	/* One-second timer */
	ucshort es;	/* Errored second */
	ucshort rai;	/* remote alarm received */
	ucshort bec;
	ucshort fec;
	ucshort cvc;
	ucshort cec;
	ucshort ebc;

	/* Status */
	ucchar red_alarm;
	ucchar blue_alarm;
	ucchar loss_fa;
	ucchar yellow_alarm;
	ucchar loss_mfa;
	ucchar prbs;
} falc_t;

typedef struct pc300dev {
	void *if_ptr;	/* General purpose pointer */
	struct pc300ch *chan;
#ifdef __KERNEL__
	char name[16];
	void *private;
	hdlc_device *hdlc;
	struct net_device_stats stats;
	struct sk_buff *tx_skb;
#endif /* __KERNEL__ */
}pc300dev_t;

typedef struct pc300hw {
	int type;		/* RSV, X21, etc. */
	int nchan;		/* number of channels */
	int irq;		/* interrupt request level */
	uclong clock;		/* Board clock */
	uclong plxphys;		/* PLX registers MMIO base (physical) */
	uclong plxbase;		/* PLX registers MMIO base (virtual) */
	uclong plxsize;		/* PLX registers MMIO size */
	uclong scaphys;		/* SCA registers MMIO base (physical) */
	uclong scabase;		/* SCA registers MMIO base (virtual) */
	uclong scasize;		/* SCA registers MMIO size */
	uclong ramphys;		/* On-board RAM MMIO base (physical) */
	uclong rambase;		/* On-board RAM MMIO base (virtual) */
	uclong ramsize;		/* On-board RAM MMIO size */
	uclong falcphys;	/* FALC registers MMIO base (physical) */
	uclong falcbase;	/* FALC registers MMIO base (virtual) */
	uclong falcsize;	/* FALC registers MMIO size */
} pc300hw_t;

typedef struct pc300chconf {
	ucchar media;		/* HW media (RS232, V.35, etc.) */
	uclong proto;		/* Protocol (PPP, X.25, etc.) */
	uclong clkrate;		/* Clock rate (in bps, 0 = ext. clock) */
	ucchar loopback;	/* Loopback mode */

	/* TE-specific parameters */
	ucchar lcode;		/* Line Code (AMI, B8ZS, etc.) */
	ucchar fr_mode;		/* Frame Mode (ESF, D4, etc.) */
	ucchar lbo;		/* Line Build Out */
	ucchar rx_sens;		/* Rx Sensitivity (long- or short-haul) */
	uclong tslot_bitmap;	/* bit[i]=1  =>  timeslot _i_ is active */
} pc300chconf_t;

typedef struct pc300ch {
	struct pc300 *card;
	int channel;
	pc300dev_t d;
	pc300chconf_t conf;
	ucchar tx_first_bd;	/* First TX DMA block descr. w/ data */
	ucchar tx_next_bd;	/* Next free TX DMA block descriptor */
	ucchar rx_first_bd;	/* First free RX DMA block descriptor */
	ucchar rx_last_bd;	/* Last free RX DMA block descriptor */
	falc_t falc;		/* FALC structure (TE only) */
} pc300ch_t;

typedef struct pc300 {
	pc300hw_t hw;			/* hardware config. */
	pc300ch_t chan[PC300_MAXCHAN];
#ifdef __KERNEL__
	spinlock_t card_lock;
#endif /* __KERNEL__ */
} pc300_t;

/* DEV ioctl() commands */
#define	N_SPPP_IOCTLS	2

enum pc300_ioctl_cmds {
	SIOCCPCRESERVED = (SIOCDEVPRIVATE + N_SPPP_IOCTLS),
	SIOCGPC300CONF,
	SIOCSPC300CONF,
	SIOCGPC300STATUS,
	SIOCGPC300FALCSTATUS,
};

/* Control Constant Definitions */
#define	PC300_RSV	0x01
#define	PC300_X21	0x02
#define	PC300_TE	0x03

#define PC300_LC_AMI	0x01
#define PC300_LC_B8ZS	0x02
#define PC300_LC_NRZ	0x03
#define PC300_LC_HDB3	0x04

/* Framing (T1) */
#define PC300_FR_ESF		0x01
#define PC300_FR_D4		0x02
#define PC300_FR_ESF_JAPAN	0x03

/* Framing (E1) */
#define PC300_FR_MF_CRC4	0x04
#define PC300_FR_MF_NON_CRC4	0x05

#define PC300_LBO_0_DB		0x00
#define PC300_LBO_7_5_DB	0x01
#define PC300_LBO_15_DB		0x02
#define PC300_LBO_22_5_DB	0x03

#define PC300_RX_SENS_SH	0x01
#define PC300_RX_SENS_LH	0x02

#define PC300_TX_TIMEOUT	(2*HZ)
#define PC300_TX_QUEUE_LEN	10
#define	PC300_DEF_MTU		1500

#ifdef __KERNEL__
/* Function Prototypes */
static void tx_dma_buf_pt_init(pc300_t *, int);
static void tx_dma_buf_init(pc300_t *, int);
static void rx_dma_buf_pt_init(pc300_t *, int);
static void rx_dma_buf_init(pc300_t *, int);
static void tx_dma_buf_check(pc300_t *, int);
static void rx_dma_buf_check(pc300_t *, int);
int dma_buf_write(pc300_t *, int, ucchar *, int);
int dma_buf_read(pc300_t *, int, struct sk_buff *);
void tx_dma_start(pc300_t *, int);
void rx_dma_start(pc300_t *, int);
void tx_dma_stop(pc300_t *, int);
void rx_dma_stop(pc300_t *, int);
int cpc_queue_xmit(struct sk_buff *, struct device *);
void cpc_net_rx(hdlc_device *);
#ifdef CONFIG_PC300_X25
int cpc_x25_packetlayer_xmit(struct sk_buff *, struct device *);
void cpc_lapb_connected(void *, int);
void cpc_lapb_disconnected(void *, int);
void cpc_lapb_data_indication(void *, struct sk_buff *);
void cpc_lapb_data_transmit(void *, struct sk_buff *);
#endif /* CONFIG_PC300_X25 */
static void cpc_intr(int, void *, struct pt_regs *);
void cpc_sca_status(pc300_t *, int);
int cpc_ioctl(hdlc_device *, struct ifreq *, int);
static int clock_rate_calc(uclong, uclong, int *);
int ch_config(pc300dev_t *);
int rx_config(pc300dev_t *);
int tx_config(pc300dev_t *);
int cpc_opench(pc300dev_t *);
void cpc_closech(pc300dev_t *);
int cpc_open(hdlc_device *);
void cpc_close(hdlc_device *);
static uclong detect_ram(pc300_t *);
static void plx_init(pc300_t *);
static int cpc_detect(void);
#endif /* __KERNEL__ */

#endif	/* _PC300_H */
