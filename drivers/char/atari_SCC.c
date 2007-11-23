/*
 * drivers/char/atari_SCC.c: Atari SCC serial ports implementation
 *
 * Copyright 1994-95 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 * Partially based on PC-Linux serial.c by Linus Torvalds and Theodore Ts'o
 *
 * Some parts were taken from the (incomplete) SCC driver by Lars Brinkhoff
 * <f93labr@dd.chalmers.se>
 *
 * Adapted to 1.2 by Andreas Schwab
 *
 * Adapted to support MVME147, MVME162 and BVME6000 by Richard Hirst
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Description/Notes:
 *
 *  - This driver currently handles the asynchronous modes of the SCC ports
 *    only. Synchronous operation or packet modes aren't implemented yet.
 *
 *  - Since there are many variations how the SCC can be integrated, the
 *    driver offers the possibility to provide the frequencies attached to the
 *    various clock inputs via an ioctl (along with an externally calculated
 *    baud table).
 *
 *  - I haven't spent much time for optimizations yet...
 *
 *  - Channel A is accessible via two different devices: ttyS3 and ttyS4. The
 *    former is the RS232 "Serial2" port, the latter the RS422 "LAN" port.
 *    Only one of these devices can be open at one time.
 *
 *  - ++TeSche 12/96: DMA support for channel A, see atari_SCC.README for details
 *    send comments/problems to: itschere@techfak.uni-bielefeld.de
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/m68kserial.h>
#include <linux/tqueue.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>
#include <asm/setup.h>
#ifdef CONFIG_MVME147_SCC
#include <asm/mvme147hw.h>
#endif
#ifdef CONFIG_MVME162_SCC
#include <asm/mvme16xhw.h>
#endif
#ifdef CONFIG_BVME6000_SCC
#include <asm/bvme6000hw.h>
#endif
#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#include <asm/atariints.h>
#endif
#include <asm/irq.h>
#include <asm/atari_SCCserial.h>

#include "atari_SCC.h"


#if defined CONFIG_ATARI_SCC || defined CONFIG_ATARI_SCC_MODULE
#define ENABLE_ATARI_SCC
#endif

#define	DEBUG_INT	0x01
#define	DEBUG_INIT	0x02
#define	DEBUG_THROTTLE	0x04
#define	DEBUG_INFO	0x08
#define	DEBUG_SPEED	0x10
#define	DEBUG_OPEN	0x20
#define	DEBUG_OVERRUNS	0x40
/* warning: DEBUG_DMA will lead to a driver which is not able to operate at
 * more than 19200 bps without causing overruns!
 */
#define DEBUG_DMA	0x80

#define	DEBUG_ALL	0xffffffff
#define	DEBUG_NONE	0

#define	DEBUG	DEBUG_NONE

#define CHANNEL_A	0
#define CHANNEL_B	1


/* Shadows for all SCC write registers */
static unsigned char SCC_shadow[2][16];

/* Location to access for SCC register access delay */
static volatile unsigned char *scc_del;

/* To keep track of STATUS_REG state for detection of Ext/Status int source */
static unsigned char SCC_last_status_reg[2];

/* This array tells the clocks connected to RTxC or TRxC, resp., (2nd
 * index) for each channel (1st index).
 *
 * This table is initialzed for the TT. If we run on another machine,
 * the values are changed by the initialization function.
 */

static unsigned SCC_clocks[2][2] = {
	  /* RTxC */			/* TRxC */
	{ SCC_BAUD_BASE_PCLK4,	SCC_BAUD_BASE_NONE },	/* Channel A */
	{ SCC_BAUD_BASE_TIMC,	SCC_BAUD_BASE_BCLK }	/* Channel B */
};

/* The SCC's master clock (as variable, in case someone has unusual
 * hardware)
 */

static unsigned SCC_PCLK = SCC_BAUD_BASE_PCLK;


/* BRG values for the standard speeds and the various clock sources */

typedef struct {
	unsigned	clksrc;		/* clock source to use or -1 for not possible */
	unsigned	div;		/* divisor: 1, 2 and 4 correspond to
					 * direct 1:16, 1:32 and 1:64 modes,
					 * divisors >= 4 yield a BRG value of
					 * div/2-2 (in 1:16 mode)
					 */
} BAUD_ENTRY;

/* A pointer for each channel to the current baud table */
static BAUD_ENTRY *SCC_baud_table[2];

/* Baud table format:
 *
 * Each entry consists of the clock source (CLK_RTxC, CLK_TRxC or
 * CLK_PCLK) and a divisor. The following rules apply to the divisor:
 *
 *   - CLK_RTxC: 1 or even (1, 2 and 4 are the direct modes, > 4 use
 *               the BRG)
 *
 *   - CLK_TRxC: 1, 2 or 4 (no BRG, only direct modes possible)
 *
 *   - CLK_PCLK: >= 4 and even (no direct modes, only BRG)
 *
 */

#ifdef ENABLE_ATARI_SCC
/* This table is used if RTxC = 3.672 MHz. This is the case for TT's
 * channel A and for both channels on the Mega STE/Falcon. (TRxC is unused)
 */

static BAUD_ENTRY bdtab_norm[20] = {
	/* B0      */ { 0, 0 },
	/* B50     */ { CLK_RTxC, 4590 },
	/* B75     */ { CLK_RTxC, 3060 },
	/* B110    */ { CLK_PCLK, 4576 },
	/* B134    */ { CLK_PCLK, 3756 },
	/* B150    */ { CLK_RTxC, 1530 },
	/* B200    */ { CLK_PCLK, 2516 },
	/* B300    */ { CLK_PCLK, 1678 },
	/* B600    */ { CLK_PCLK, 838 },
	/* B1200   */ { CLK_PCLK, 420 },
	/* B1800   */ { CLK_PCLK, 280 },
	/* B2400   */ { CLK_PCLK, 210 },
	/* B4800   */ { CLK_RTxC, 48 },
	/* B9600   */ { CLK_RTxC, 24 },
	/* B19200  */ { CLK_RTxC, 12 },
	/* B38400  */ { CLK_RTxC, 6 },   /* #15 spd_extra */
	/* B57600  */ { CLK_RTxC, 4 },   /* #16 spd_hi */
	/* B115200 */ { CLK_RTxC, 2 },   /* #17 spd_vhi */
	/* B230400 */ { CLK_RTxC, 1 },   /* #18 spd_shi */
	/* B460800 */ { 0, 0 }           /* #19 spd_warp: Impossible */
};

/* This is a special table for the TT channel B with 307.2 kHz at RTxC
 * and 2.4576 MHz at TRxC
 */
static BAUD_ENTRY bdtab_TTChB[20] = {
	/* B0      */ { 0, 0 },
	/* B50     */ { CLK_RTxC, 384 },
	/* B75     */ { CLK_RTxC, 256 },
	/* B110    */ { CLK_PCLK, 4576 },
	/* B134    */ { CLK_PCLK, 3756 },
	/* B150    */ { CLK_RTxC, 128 },
	/* B200    */ { CLK_RTxC, 96 },
	/* B300    */ { CLK_RTxC, 64 },
	/* B600    */ { CLK_RTxC, 32 },
	/* B1200   */ { CLK_RTxC, 16 },
	/* B1800   */ { CLK_PCLK, 280 },
	/* B2400   */ { CLK_RTxC, 8 },
	/* B4800   */ { CLK_RTxC, 4 },
	/* B9600   */ { CLK_RTxC, 2 },
	/* B19200  */ { CLK_RTxC, 1 },
	/* B38400  */ { CLK_TRxC, 4 },
	/* B57600  */ { CLK_TRxC, 2 }, /* 57600 is not possible, use 76800 instead */
	/* B115200 */ { CLK_TRxC, 1 }, /* 115200 is not possible, use 153600 instead */
	/* B230400 */ { 0, 0 },        /* #18 spd_shi: Impossible  */
	/* B460800 */ { 0, 0 }         /* #19 spd_warp: Impossible */
};
#endif

#ifdef CONFIG_MVME147_SCC
/* This table is used if RTxC = pCLK = 5 MHz. This is the case for MVME147
 */
static BAUD_ENTRY bdtab_m147[19] = {
	/* B0      */ { 0, 0 },
	/* B50     */ { CLK_PCLK, 6250 },
	/* B75     */ { CLK_PCLK, 4166 },
	/* B110    */ { CLK_PCLK, 2814 },
	/* B134    */ { CLK_PCLK, 2322 },
	/* B150    */ { CLK_PCLK, 2084 },
	/* B200    */ { CLK_PCLK, 1562 },
	/* B300    */ { CLK_PCLK, 1040 },
	/* B600    */ { CLK_PCLK, 520 },
	/* B1200   */ { CLK_PCLK, 260 },
	/* B1800   */ { CLK_PCLK, 194 },
	/* B2400   */ { CLK_PCLK, 130 },
	/* B4800   */ { CLK_PCLK, 64 },
	/* B9600   */ { CLK_PCLK, 32 },
	/* B19200  */ { CLK_PCLK, 16 },
	/* B38400  */ { CLK_PCLK, 8 },
	/* B57600  */ { CLK_PCLK, 4 },
	/* B115200 */ { CLK_PCLK, 2 },
	/* B230400 */ { CLK_PCLK, 1 },   /* #18 spd_shi */
};
#endif

#ifdef CONFIG_MVME162_SCC
/* This table is used if RTxC = pCLK = 10 MHz. This is the case for MVME162
 */
static BAUD_ENTRY bdtab_mvme[20] = {
	/* B0      */ { 0, 0 },
	/* B50     */ { CLK_PCLK, 12500 },
	/* B75     */ { CLK_PCLK, 8332 },
	/* B110    */ { CLK_PCLK, 5682 },
	/* B134    */ { CLK_PCLK, 4646 },
	/* B150    */ { CLK_PCLK, 4166 },
	/* B200    */ { CLK_PCLK, 3124 },
	/* B300    */ { CLK_PCLK, 2082 },
	/* B600    */ { CLK_PCLK, 1042 },
	/* B1200   */ { CLK_PCLK, 520 },
	/* B1800   */ { CLK_PCLK, 390 },
	/* B2400   */ { CLK_PCLK, 260 },
	/* B4800   */ { CLK_PCLK, 130 },
	/* B9600   */ { CLK_PCLK, 64 },
	/* B19200  */ { CLK_PCLK, 32 },
	/* B38400  */ { CLK_PCLK, 16 },
	/* B57600  */ { CLK_PCLK, 8 },
	/* B115200 */ { CLK_PCLK, 4 },
	/* B230400 */ { CLK_PCLK, 2 },   /* #18 spd_shi */
	/* B460800 */ { CLK_PCLK, 1 }    /* #19 spd_warp */
};
#endif

#ifdef CONFIG_BVME6000_SCC

/* This table is used if RTxC = 7.3728 MHz. This is the case for BVMs
 */
static BAUD_ENTRY bdtab_bvme[18] = {
	/* B0      */ { 0, 0 },
	/* B50     */ { CLK_RTxC, 9216 },
	/* B75     */ { CLK_RTxC, 6144 },
	/* B110    */ { CLK_RTxC, 4188 },
	/* B134    */ { CLK_RTxC, 3424 },
	/* B150    */ { CLK_RTxC, 3072 },
	/* B200    */ { CLK_RTxC, 2304 },
	/* B300    */ { CLK_RTxC, 1536 },
	/* B600    */ { CLK_RTxC, 768 },
	/* B1200   */ { CLK_RTxC, 384 },
	/* B1800   */ { CLK_RTxC, 256 },
	/* B2400   */ { CLK_RTxC, 192 },
	/* B4800   */ { CLK_RTxC, 96 },
	/* B9600   */ { CLK_RTxC, 48 },
	/* B19200  */ { CLK_RTxC, 24 },
	/* B38400  */ { CLK_RTxC, 12 },
	/* B57600  */ { CLK_RTxC, 8 },
	/* B115200 */ { CLK_RTxC, 4 }
};
#endif


/* User settable tables */
static BAUD_ENTRY bdtab_usr[2][20];


/*
   Here are the values to compute the tables above. For each base
   clock, the BRG values for the common bps rates are listed. The
   divisor is (BRG+2)*2. For each clock, the 1:16 and 1:32 are also
   usable (and the BRG isn't used). 1:64 is the same as BRG with
   k==0. If more than clock source was possible for a bps rate I've
   choosen the one with the smallest error.

   For 307.2 kHz == base 19200:
     50    bps -> 190
     75    bps -> 126
     110   bps -> 85 (really 110.34 bps, error 0.31 %)
     134   bps -> 70 (really 133.33 bps, error 0.49 %)
     150   bps -> 62
     200   bps -> 46
     300   bps -> 30
     600   bps -> 14
     1200  bps -> 6
     1800  bps -> 3 (really 1920 bps, error 6.7 %)
     2400  bps -> 2
     4800  bps -> 0

   For 2.4576 MHz == base 153600:
     50    bps -> 1534
     75    bps -> 1022
     110   bps -> 696 (really 110.03 bps, error 0.027 %)
     134   bps -> 571 (really 134.03 bps, error 0.022 %)
     150   bps -> 510
     200   bps -> 382
     300   bps -> 254
     600   bps -> 126
     1200  bps -> 62
     1800  bps -> 41 (really 1786.1 bps, error 0.77 %)
     2400  bps -> 30
     4800  bps -> 14
     9600  bps -> 6
     19200 bps -> 2
     38400 bps -> 0

   For 3.672 MHz == base 229500:
     50    bps -> 2293
     75    bps -> 1528
     110   bps -> 1041
     134   bps -> 854
     150   bps -> 763
     200   bps -> 572
     300   bps -> 381
     600   bps -> 189
     1200  bps -> 94
     1800  bps -> 62
     2400  bps -> 46
     4800  bps -> 22
     9600  bps -> 10
     19200 bps -> 4
     38400 bps -> 1
     57600 bps -> 0

   For 8.053976 MHz == base 503374:
	  0    	 bps -> 0
	  50     bps -> 5032
	  75     bps -> 3354
	  110    bps -> 2286
	  134    bps -> 1876
	  150	 bps -> 1676
	  200	 bps -> 1256
	  300	 bps -> 837
	  600	 bps -> 417
	  1200   bps -> 208
	  1800   bps -> 138
	  2400   bps -> 103
	  4800   bps -> 50
	  9600   bps -> 24
	  19200  bps -> 11
	  31500  bps -> 6 (really 31461 bps)
	  50000  bps -> 3
	  125000 bps -> 0

*/


/* Is channel A switchable between two hardware ports? (Serial2/LAN) */

#define	SCCA_SWITCH_SERIAL2_ONLY	0	/* only connected to Serial2 */
#define	SCCA_SWITCH_LAN_ONLY		1	/* only connected to LAN */
#define	SCCA_SWITCH_BOTH		2	/* connected to both, switch by
						 * IO7 in the PSG */

static int SCC_chan_a_switchable;

/* Is channel A (two ports!) already open? */
static int SCC_chan_a_open;

/* For which line has channel A been opened? */
static int SCC_chan_a_line;

/* info pointer for SCC_chan_a_line */
static struct m68k_async_struct *SCC_chan_a_info;

/* Are the register addresses for the channels reversed? (B before A). This is
 * the case for the ST_ESCC. */
static int ChannelsReversed;

/* This macro sets up the 'info' pointer for the interrupt functions of
 * channel A. It addresses the following problem: The isrs were registered
 * with callback_data == &rs_table[3] (= Serial2). But they can also be for
 * &rs_table[4] (LAN), if this port is the open one. SETUP_INFO() thus
 * advances the pointer if info->line == 3.
 */

#define DEFAULT_CHANNEL_B_LINE		1 /* ttyS1 */
#define DEFAULT_CHANNEL_A232_LINE	3 /* ttyS3 */
#define DEFAULT_CHANNEL_A422_LINE	4 /* ttyS4 */

static int chb_line = -1, cha232_line = -1, cha422_line = -1;

#ifndef CONFIG_ATARI_SCC_DMA
#define scca_dma  0        /* No DMA support */
#else
static int scca_dma = 0;   /* whether DMA is supported at all */

/* ++TeSche: these next few things are for DMA support on channel A. both
 * BUFFERS and BUFSIZE must be a power of two (because of speed reasons)!
 */
#define SCCA_DMA_BUFFERS 8
#define SCCA_DMA_BUFSIZE 2048
typedef struct {
	u_char *buf, *pbuf, *err;
	int inbuf;
	short cntOver, cntPar, cntFrame;
	unsigned active:1;   /* whether a DMA transfer is using this buffer */
	unsigned needsFlushing:1;   /* whether it's dirty (even when inbuf==0) */
} DMABUF;
static DMABUF scca_dma_buf[SCCA_DMA_BUFFERS];
static DMABUF *scca_dma_head, *scca_dma_tail;
static DMABUF *scca_dma_end = &scca_dma_buf[SCCA_DMA_BUFFERS];

#endif

#define	SETUP_INFO(info)						\
	do {										\
		if (info->line == cha232_line)			\
			info = SCC_chan_a_info;				\
	} while(0)


/***************************** Prototypes *****************************/

#ifdef ENABLE_ATARI_SCC
static void SCC_init_port( struct m68k_async_struct *info, int type, int channel );
#endif
#ifdef CONFIG_MVME147_SCC
static void m147_init_port( struct m68k_async_struct *info, int type, int channel );
#endif
#ifdef CONFIG_MVME162_SCC
static void mvme_init_port( struct m68k_async_struct *info, int type, int channel );
#endif
#ifdef CONFIG_BVME6000_SCC
static void bvme_init_port( struct m68k_async_struct *info, int type, int channel );
#endif
#ifdef MODULE
static void SCC_deinit_port( struct m68k_async_struct *info, int channel );
#endif
static void SCC_rx_int (int irq, void *data, struct pt_regs *fp);
static void SCC_spcond_int (int irq, void *data, struct pt_regs *fp);
static void SCC_tx_int (int irq, void *data, struct pt_regs *fp);
static void SCC_stat_int (int irq, void *data, struct pt_regs *fp);
#ifdef ENABLE_ATARI_SCC
static void SCC_ri_int (int irq, void *data, struct pt_regs *fp);
#endif
static int SCC_check_open( struct m68k_async_struct *info, struct tty_struct
                           *tty, struct file *file );
static void SCC_init( struct m68k_async_struct *info );
static void SCC_deinit( struct m68k_async_struct *info, int leave_dtr );
static void SCC_enab_tx_int( struct m68k_async_struct *info, int enab_flag );
static int SCC_check_custom_divisor( struct m68k_async_struct *info, int baud_base,
				    int divisor );
static void SCC_change_speed( struct m68k_async_struct *info );
static int SCC_clocksrc( unsigned baud_base, unsigned channel );
static void SCC_throttle( struct m68k_async_struct *info, int status );
static void SCC_set_break( struct m68k_async_struct *info, int break_flag );
static void SCC_get_serial_info( struct m68k_async_struct *info, struct
				serial_struct *retinfo );
static unsigned int SCC_get_modem_info( struct m68k_async_struct *info );
static int SCC_set_modem_info( struct m68k_async_struct *info, int new_dtr, int
			      new_rts );
static int SCC_ioctl( struct tty_struct *tty, struct file *file, struct
		     m68k_async_struct *info, unsigned int cmd, unsigned long
		     arg );
static void SCC_stop_receive (struct m68k_async_struct *info);
static int SCC_trans_empty (struct m68k_async_struct *info);
#ifdef CONFIG_ATARI_SCC_DMA
static void SCC_timer_int (int irq, void *data, struct pt_regs *fp);
static void SCC_dma_int (int irq, void *data, struct pt_regs *fp);
#endif

/************************* End of Prototypes **************************/


static SERIALSWITCH SCC_switch = {
	SCC_init, SCC_deinit, SCC_enab_tx_int,
	SCC_check_custom_divisor, SCC_change_speed,
	SCC_throttle, SCC_set_break,
	SCC_get_serial_info, SCC_get_modem_info,
	SCC_set_modem_info, SCC_ioctl, SCC_stop_receive, SCC_trans_empty,
	SCC_check_open
};

extern int atari_SCC_init_done;
extern int atari_SCC_reset_done;


#ifdef CONFIG_BVME6000_SCC

int bvme_SCC_init( void )
{
	struct serial_struct req;
	int nr = 0;

	if (!MACH_IS_BVME6000)
		return (-ENODEV);

	scc_del = (unsigned char *)0;

	SCC_chan_a_switchable = SCCA_SWITCH_SERIAL2_ONLY;
	SCC_PCLK = SCC_BAUD_BASE_BVME_PCLK;

	/* General initialization */
	ChannelsReversed = 8;
	SCC_chan_a_open = 0;

	req.line = DEFAULT_CHANNEL_B_LINE;
	req.type = SER_SCC_BVME;
	req.port = BVME_SCC_B_ADDR;
	if ((chb_line = register_serial( &req )) >= 0) {
		bvme_init_port( &rs_table[chb_line], req.type, CHANNEL_B );
		++nr;
	}
	else
		printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel B\n", req.line );

	/* Init channel A, RS232 part (Serial2) */
	req.line = 0;
	req.type = SER_SCC_BVME;
	req.port = BVME_SCC_A_ADDR;
	if ((cha232_line = register_serial( &req )) >= 0) {
		bvme_init_port( &rs_table[cha232_line], req.type, CHANNEL_A );
		++nr;
	}
	else
		printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel A\n", req.line );

	return( nr > 0 ? 0 : -ENODEV );
}


static void bvme_init_port( struct m68k_async_struct *info, int type, int channel )
{
	static int called = 0, ch_a_inited = 0;
	SCC_ACCESS_INIT(info);

	info->sw = &SCC_switch;

	/* set ISRs, but don't enable interrupts yet (done in init());
	 */
	if (channel == CHANNEL_B || !ch_a_inited) {
		request_irq(channel ? BVME_IRQ_SCCB_TX : BVME_IRQ_SCCA_TX,
		            SCC_tx_int, BVME_IRQ_TYPE_PRIO,
		            channel ? "SCC-B TX" : "SCC-A TX", info);
		request_irq(channel ? BVME_IRQ_SCCB_STAT : BVME_IRQ_SCCA_STAT,
		            SCC_stat_int, BVME_IRQ_TYPE_PRIO,
		            channel ? "SCC-B status" : "SCC-A status", info);
		request_irq(channel ? BVME_IRQ_SCCB_RX : BVME_IRQ_SCCA_RX,
		            SCC_rx_int, BVME_IRQ_TYPE_PRIO,
		            channel ? "SCC-B RX" : "SCC-A RX", info);
		request_irq(channel ? BVME_IRQ_SCCB_SPCOND : BVME_IRQ_SCCA_SPCOND,
		            SCC_spcond_int, BVME_IRQ_TYPE_PRIO,
		            channel ? "SCC-B special cond" : "SCC-A special cond", info);

	}

	/* Hardware initialization */

	if (!called) {
		/* Set the interrupt vector */
		SCCwrite( INT_VECTOR_REG, BVME_IRQ_SCC_BASE );

		/* Interrupt parameters: vector includes status, status low */
		SCCwrite( MASTER_INT_CTRL, MIC_VEC_INCL_STAT );

		/* Set the baud tables */
		SCC_baud_table[CHANNEL_A] = bdtab_bvme;
		SCC_baud_table[CHANNEL_B] = bdtab_bvme;

		/* Set the clocks */
		SCC_clocks[CHANNEL_A][CLK_RTxC] = SCC_BAUD_BASE_BVME;
		SCC_clocks[CHANNEL_A][CLK_TRxC] = SCC_BAUD_BASE_NONE;
		SCC_clocks[CHANNEL_B][CLK_RTxC] = SCC_BAUD_BASE_BVME;
		SCC_clocks[CHANNEL_B][CLK_TRxC] = SCC_BAUD_BASE_NONE;

		SCCmod( MASTER_INT_CTRL, 0xff, MIC_MASTER_INT_ENAB );
	}

	/* disable interrupts for this channel */
	SCCwrite( INT_AND_DMA_REG, 0 );

	called = 1;
	if (CHANNR(info) == CHANNEL_A) ch_a_inited = 1;
}

#endif


#ifdef CONFIG_MVME147_SCC

int m147_SCC_init( void )
{
	struct serial_struct req;
	int nr = 0;

	if (!MACH_IS_MVME147)
		return (-ENODEV);

	scc_del = (unsigned char *)0;

	SCC_chan_a_switchable = SCCA_SWITCH_SERIAL2_ONLY;
	SCC_PCLK = SCC_BAUD_BASE_M147_PCLK;

	/* General initialization */
	ChannelsReversed = 2;
	SCC_chan_a_open = 0;

	req.line = DEFAULT_CHANNEL_B_LINE;
	req.type = SER_SCC_MVME;
	req.port = M147_SCC_B_ADDR;
	if ((chb_line = register_serial( &req )) >= 0) {
		m147_init_port( &rs_table[chb_line], req.type, CHANNEL_B );
		++nr;
	}
	else
		printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel B\n", req.line );

	/* Init channel A, RS232 part (Serial2) */
	req.line = 0;
	req.type = SER_SCC_MVME;
	req.port = M147_SCC_A_ADDR;
	if ((cha232_line = register_serial( &req )) >= 0) {
		m147_init_port( &rs_table[cha232_line], req.type, CHANNEL_A );
		++nr;
	}
	else
		printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel A\n", req.line );
        /*
         * Ensure interrupts are enabled in the PCC chip
         */
        m147_pcc->serial_cntrl=PCC_LEVEL_SERIAL|PCC_INT_ENAB;

	return( nr > 0 ? 0 : -ENODEV );
}


static void m147_init_port( struct m68k_async_struct *info, int type, int channel )
{
	static int called = 0, ch_a_inited = 0;
	SCC_ACCESS_INIT(info);

	info->sw = &SCC_switch;

	/* set ISRs, but don't enable interrupts yet (done in init());
	 */
	if (channel == CHANNEL_B || !ch_a_inited) {
		request_irq(channel ? MVME147_IRQ_SCCB_TX : MVME147_IRQ_SCCA_TX,
		            SCC_tx_int, MVME147_IRQ_TYPE_PRIO,
		            channel ? "SCC-B TX" : "SCC-A TX", info);
		request_irq(channel ? MVME147_IRQ_SCCB_STAT : MVME147_IRQ_SCCA_STAT,
		            SCC_stat_int, MVME147_IRQ_TYPE_PRIO,
		            channel ? "SCC-B status" : "SCC-A status", info);
		request_irq(channel ? MVME147_IRQ_SCCB_RX : MVME147_IRQ_SCCA_RX,
		            SCC_rx_int, MVME147_IRQ_TYPE_PRIO,
		            channel ? "SCC-B RX" : "SCC-A RX", info);
		request_irq(channel ? MVME147_IRQ_SCCB_SPCOND : MVME147_IRQ_SCCA_SPCOND,
		            SCC_spcond_int, MVME147_IRQ_TYPE_PRIO,
		            channel ? "SCC-B special cond" : "SCC-A special cond", info);

	}

	/* Hardware initialization */

	if (!called) {
		/* Set the interrupt vector */
		SCCwrite( INT_VECTOR_REG, MVME147_IRQ_SCC_BASE );

		/* Interrupt parameters: vector includes status, status low */
		SCCwrite( MASTER_INT_CTRL, MIC_VEC_INCL_STAT );

		/* Set the baud tables */
		SCC_baud_table[CHANNEL_A] = bdtab_m147;
		SCC_baud_table[CHANNEL_B] = bdtab_m147;

		/* Set the clocks */
		SCC_clocks[CHANNEL_A][CLK_RTxC] = SCC_BAUD_BASE_M147;
		SCC_clocks[CHANNEL_A][CLK_TRxC] = SCC_BAUD_BASE_NONE;
		SCC_clocks[CHANNEL_B][CLK_RTxC] = SCC_BAUD_BASE_M147;
		SCC_clocks[CHANNEL_B][CLK_TRxC] = SCC_BAUD_BASE_NONE;

		SCCmod( MASTER_INT_CTRL, 0xff, MIC_MASTER_INT_ENAB );
	}

	/* disable interrupts for this channel */
	SCCwrite( INT_AND_DMA_REG, 0 );

	called = 1;
	if (CHANNR(info) == CHANNEL_A) ch_a_inited = 1;
}

#endif

#ifdef CONFIG_MVME162_SCC

int mvme_SCC_init( void )
{
	struct serial_struct req;
	int nr = 0;

	if (!MACH_IS_MVME16x || !(mvme16x_config & MVME16x_CONFIG_GOT_SCCA))
		return (-ENODEV);

	scc_del = (unsigned char *)0;

	SCC_chan_a_switchable = SCCA_SWITCH_SERIAL2_ONLY;
	SCC_PCLK = SCC_BAUD_BASE_MVME_PCLK;

	/* General initialization */
	ChannelsReversed = 4;
	SCC_chan_a_open = 0;

	req.line = DEFAULT_CHANNEL_B_LINE;
	req.type = SER_SCC_MVME;
	req.port = MVME_SCC_B_ADDR;
	if ((chb_line = register_serial( &req )) >= 0) {
		mvme_init_port( &rs_table[chb_line], req.type, CHANNEL_B );
		++nr;
	}
	else
		printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel B\n", req.line );

	/* Init channel A, RS232 part (Serial2) */
	req.line = 0;
	req.type = SER_SCC_MVME;
	req.port = MVME_SCC_A_ADDR;
	if ((cha232_line = register_serial( &req )) >= 0) {
		mvme_init_port( &rs_table[cha232_line], req.type, CHANNEL_A );
		++nr;
	}
	else
		printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel A\n", req.line );
        /*
         * Ensure interrupts are enabled in the MC2 chip
         */
        *(volatile char *)0xfff4201d = 0x14;

	return( nr > 0 ? 0 : -ENODEV );
}


static void mvme_init_port( struct m68k_async_struct *info, int type, int channel )
{
	static int called = 0, ch_a_inited = 0;
	SCC_ACCESS_INIT(info);

	info->sw = &SCC_switch;

	/* set ISRs, but don't enable interrupts yet (done in init());
	 */
	if (channel == CHANNEL_B || !ch_a_inited) {
		request_irq(channel ? MVME162_IRQ_SCCB_TX : MVME162_IRQ_SCCA_TX,
		            SCC_tx_int, MVME162_IRQ_TYPE_PRIO,
		            channel ? "SCC-B TX" : "SCC-A TX", info);
		request_irq(channel ? MVME162_IRQ_SCCB_STAT : MVME162_IRQ_SCCA_STAT,
		            SCC_stat_int, MVME162_IRQ_TYPE_PRIO,
		            channel ? "SCC-B status" : "SCC-A status", info);
		request_irq(channel ? MVME162_IRQ_SCCB_RX : MVME162_IRQ_SCCA_RX,
		            SCC_rx_int, MVME162_IRQ_TYPE_PRIO,
		            channel ? "SCC-B RX" : "SCC-A RX", info);
		request_irq(channel ? MVME162_IRQ_SCCB_SPCOND : MVME162_IRQ_SCCA_SPCOND,
		            SCC_spcond_int, MVME162_IRQ_TYPE_PRIO,
		            channel ? "SCC-B special cond" : "SCC-A special cond", info);

	}

	/* Hardware initialization */

	if (!called) {
		/* Set the interrupt vector */
		SCCwrite( INT_VECTOR_REG, MVME162_IRQ_SCC_BASE );

		/* Interrupt parameters: vector includes status, status low */
		SCCwrite( MASTER_INT_CTRL, MIC_VEC_INCL_STAT );

		/* Set the baud tables */
		SCC_baud_table[CHANNEL_A] = bdtab_mvme;
		SCC_baud_table[CHANNEL_B] = bdtab_mvme;

		/* Set the clocks */
		SCC_clocks[CHANNEL_A][CLK_RTxC] = SCC_BAUD_BASE_MVME;
		SCC_clocks[CHANNEL_A][CLK_TRxC] = SCC_BAUD_BASE_NONE;
		SCC_clocks[CHANNEL_B][CLK_RTxC] = SCC_BAUD_BASE_MVME;
		SCC_clocks[CHANNEL_B][CLK_TRxC] = SCC_BAUD_BASE_NONE;

		SCCmod( MASTER_INT_CTRL, 0xff, MIC_MASTER_INT_ENAB );
	}

	/* disable interrupts for this channel */
	SCCwrite( INT_AND_DMA_REG, 0 );

	called = 1;
	if (CHANNR(info) == CHANNEL_A) ch_a_inited = 1;
}

#endif

#ifdef ENABLE_ATARI_SCC

int atari_SCC_init( void )
{
	struct serial_struct req;
	int escc = ATARIHW_PRESENT(ST_ESCC);
	int nr = 0;
	extern char m68k_debug_device[];

	/* SCC present at all? */
	if (!(ATARIHW_PRESENT(SCC) || ATARIHW_PRESENT(ST_ESCC)))
		return( -ENODEV );

	scc_del = &mfp.par_dt_reg;

#ifdef CONFIG_ATARI_SCC_DMA
	/* strengthen the condition a bit to be on the safer side...
	 */
	scca_dma = ATARIHW_PRESENT(SCC_DMA) && ATARIHW_PRESENT (TT_MFP);
#endif

	/* Channel A is switchable on the TT, MegaSTE and Medusa (extension), i.e.
	 * all machines with an SCC except the Falcon. If there's a machine where
	 * channel A is fixed to a RS-232 Serial2, add code to set to
	 * SCCA_SWITCH_SERIAL2_ONLY.
	 */
	if (MACH_IS_FALCON)
		SCC_chan_a_switchable = SCCA_SWITCH_LAN_ONLY;
	else if (ATARIHW_PRESENT(TT_MFP) || MACH_IS_MSTE)
		SCC_chan_a_switchable = SCCA_SWITCH_BOTH;
	else
		SCC_chan_a_switchable = SCCA_SWITCH_SERIAL2_ONLY;

	/* General initialization */
	ChannelsReversed = escc ? 4 : 0;
	SCC_chan_a_open = 0;

	/* Init channel B */
	if (!strcmp( m68k_debug_device, "ser2" ))
		printk(KERN_NOTICE "SCC channel B: used as debug device\n" );
	else {
		req.line = DEFAULT_CHANNEL_B_LINE;
		req.type = SER_SCC_NORM;
		req.port = (int)(escc ? &st_escc.cha_b_ctrl : &scc.cha_b_ctrl);
		if ((chb_line = register_serial( &req )) >= 0) {
			SCC_init_port( &rs_table[chb_line], req.type, CHANNEL_B );
			++nr;
		}
		else
			printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel B\n", req.line );
	}

	/* Init channel A, RS232 part (Serial2) */
	if (SCC_chan_a_switchable != SCCA_SWITCH_LAN_ONLY) {
		req.line = DEFAULT_CHANNEL_A232_LINE;
		req.type = scca_dma ? SER_SCC_DMA : SER_SCC_NORM;
		req.port = (int)(escc ? &st_escc.cha_a_ctrl : &scc.cha_a_ctrl);
		if ((cha232_line = register_serial( &req )) >= 0) {
			SCC_init_port( &rs_table[cha232_line], req.type, CHANNEL_A );
			++nr;
		}
		else
			printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel A\n", req.line );
	}

	/* Init channel A, RS422 part (LAN) */
	if (SCC_chan_a_switchable != SCCA_SWITCH_SERIAL2_ONLY) {
		req.line = DEFAULT_CHANNEL_A422_LINE;
		req.type = scca_dma ? SER_SCC_DMA : SER_SCC_NORM;
		req.port = (int)(escc ? &st_escc.cha_a_ctrl : &scc.cha_a_ctrl);
		if ((cha422_line = register_serial( &req )) >= 0) {
			SCC_init_port( &rs_table[cha422_line], req.type, CHANNEL_A );
			++nr;
		}
		else
			printk(KERN_WARNING "Cannot allocate ttyS%d for SCC channel A\n", req.line );
	}

	return( nr > 0 ? 0 : -ENODEV );
}


static void SCC_init_port( struct m68k_async_struct *info, int type, int channel )
{
	static int called = 0, ch_a_inited = 0;
	SCC_ACCESS_INIT(info);

	info->sw = &SCC_switch;

	/* set ISRs, but don't enable interrupts yet (done in init());
	 * All interrupts are of type PRIORITIZED, which means they can be
	 * interrupted by all level 6 ints, but not by another SCC (or other level
	 * 5) int. I see no races with any MFP int, but I'm not quite sure yet
	 * whether longer delays in between the two-stage SCC register access can
	 * break things...
	 */
	if (channel == CHANNEL_B || !ch_a_inited) {
		request_irq(channel ? IRQ_SCCB_TX : IRQ_SCCA_TX,
		            SCC_tx_int, IRQ_TYPE_PRIO,
		            channel ? "SCC-B TX" : "SCC-A TX", info);
		request_irq(channel ? IRQ_SCCB_STAT : IRQ_SCCA_STAT,
		            SCC_stat_int, IRQ_TYPE_PRIO,
		            channel ? "SCC-B status" : "SCC-A status", info);
		request_irq(channel ? IRQ_SCCB_RX : IRQ_SCCA_RX,
		            SCC_rx_int, IRQ_TYPE_PRIO,
		            channel ? "SCC-B RX" : "SCC-A RX", info);
		request_irq(channel ? IRQ_SCCB_SPCOND : IRQ_SCCA_SPCOND,
		            SCC_spcond_int, IRQ_TYPE_PRIO,
		            channel ? "SCC-B special cond" : "SCC-A special cond", info);

		if (channel != 0 && ATARIHW_PRESENT (TT_MFP))
			request_irq(IRQ_TT_MFP_RI, SCC_ri_int, IRQ_TYPE_SLOW,
			            "TT-MFP ring indicator (modem 2)", info);

#ifdef CONFIG_ATARI_SCC_DMA
		if (channel == CHANNEL_A && !ch_a_inited && type == SER_SCC_DMA && scca_dma) {

			int i, size = SCCA_DMA_BUFFERS * SCCA_DMA_BUFSIZE;

			if (!(scca_dma_buf[0].err = kmalloc (size, GFP_KERNEL))) {
				printk ("SCC-A: Cannot allocate buffers, DMA support disabled\n");
				scca_dma = 0;
			}

			if (scca_dma) {
				size = (size + PAGE_SIZE - 1) >> 12;
				if (!(scca_dma_buf[0].buf = (u_char *)__get_dma_pages (GFP_KERNEL, size))) {
					printk ("SCC-A: Cannot allocate buffers, DMA support disabled\n");
					kfree (scca_dma_buf[0].err);
					scca_dma = 0;
				}
			}

			if (scca_dma)
			  if (tt_mfp.int_en_a & tt_mfp.int_mk_a & 0x20) {
				  printk ("SCC-A: TT_MFP Timer A already in use, DMA support disabled\n");
				  free_pages ((unsigned long)scca_dma_buf[0].buf, size);
				  kfree (scca_dma_buf[0].err);
				  scca_dma = 0;
			  }

			if (scca_dma) {

				printk ("SCC-A: using %d buffers a %d bytes for DMA\n", SCCA_DMA_BUFFERS, SCCA_DMA_BUFSIZE);

				size = SCCA_DMA_BUFSIZE;
				for (i=1; i<SCCA_DMA_BUFFERS; i++) {
					scca_dma_buf[i].buf = scca_dma_buf[0].buf + size;
					scca_dma_buf[i].err = scca_dma_buf[0].err + size;
					size += SCCA_DMA_BUFSIZE;
				}

				for (i=0; i<SCCA_DMA_BUFFERS; i++)
				  scca_dma_buf[i].pbuf = (char *)virt_to_phys(scca_dma_buf[i].buf);

				tt_mfp.int_en_a &= ~0x20;
				tt_mfp.int_pn_a = ~0x20;
				tt_mfp.tim_ct_a = 0x00;
				tt_mfp.tim_dt_a = 0x00;
				request_irq (IRQ_TT_MFP_TIMA, SCC_timer_int, IRQ_TYPE_SLOW,
							 "SCC-A Timer", info);
				tt_mfp.int_mk_a |= 0x20;
				tt_mfp.int_en_a |= 0x20;
				tt_mfp.tim_ct_a = 0x07;

				/* timer now runs with 1/200 pre-divisor and 1/256 data
				 * counter, say 1/51200 at 2.4576 MHz, say 48 Hz. seems to be
				 * the slowest frequency possible.
				 */
				request_irq (IRQ_TT_MFP_SCC, SCC_dma_int, IRQ_TYPE_PRIO,
							 "SCC-A DMA", info);
			}
		}
#endif
	}

	/* Hardware initialization */

	if (!called) {
		/* Before accessing the SCC the first time, do a read to the
		 * control register to reset the internal pointers
		 */
		SCCread( STATUS_REG );

		if (!atari_SCC_reset_done) {
			/* The master reset with additional delay */
			SCCwrite( MASTER_INT_CTRL, MIC_HARD_RESET );
			udelay(40);
			atari_SCC_reset_done = 1;
		}

		/* Set the interrupt vector, 0x60 is standard Atari */
		SCCwrite( INT_VECTOR_REG, 0x60 );

		/* Interrupt parameters: vector includes status, status low */
		SCCwrite( MASTER_INT_CTRL, MIC_VEC_INCL_STAT );

		/* to do only once, too: If a TT-MFP is present, initialize its timer
		 * C that is connected to RTxC of channel B */
		if (ATARIHW_PRESENT(TT_MFP)) {
			/* If on a TT, program the TT-MFP's timer C to 307200 kHz
			 * for RTxC on channel B
			 */
			tt_mfp.tim_ct_cd = (tt_mfp.tim_ct_cd & ~0x70) | 0x10; /* 1:4 mode */
			tt_mfp.tim_dt_c  = 1;
			/* make sure the timer interrupt is disabled, the timer is used
			 * only for generating a clock */
			atari_turnoff_irq( IRQ_TT_MFP_TIMC );

			/* Set the baud tables */
			SCC_baud_table[CHANNEL_A] = bdtab_norm;
			SCC_baud_table[CHANNEL_B] = bdtab_TTChB;

			/* The clocks are already initialzed for the TT. */
		}
		else {
			/* Set the baud tables */
			SCC_baud_table[CHANNEL_A] = bdtab_norm;
			SCC_baud_table[CHANNEL_B] = bdtab_norm;

			/* Set the clocks; only RTxCB is different compared to the TT */
			SCC_clocks[CHANNEL_B][CLK_RTxC] = SCC_BAUD_BASE_PCLK4;
		}

		SCCmod( MASTER_INT_CTRL, 0xff, MIC_MASTER_INT_ENAB );
	}

	/* disable interrupts for this channel */
	SCCwrite( INT_AND_DMA_REG, 0 );

	called = 1;
	if (CHANNR(info) == CHANNEL_A) ch_a_inited = 1;
}

#endif

#ifdef MODULE
static void SCC_deinit_port( struct m68k_async_struct *info, int channel )
{
#ifdef ENABLE_ATARI_SCC
	if (MACH_IS_ATARI) {
		free_irq(channel ? IRQ_SCCB_TX : IRQ_SCCA_TX, info);
		free_irq(channel ? IRQ_SCCB_STAT : IRQ_SCCA_STAT, info);
		free_irq(channel ? IRQ_SCCB_RX : IRQ_SCCA_RX, info);
		free_irq(channel ? IRQ_SCCB_SPCOND : IRQ_SCCA_SPCOND, info);
		if (channel != 0 && ATARIHW_PRESENT (TT_MFP))
			free_irq(IRQ_TT_MFP_RI, info);

#ifdef CONFIG_ATARI_SCC_DMA
		if (channel == CHANNEL_A && scca_dma) {
			tt_mfp.int_en_a &= ~0x20;
			tt_mfp.int_pn_a = ~0x20;
			tt_mfp.int_mk_a &= ~0x20;
			free_irq(IRQ_TT_MFP_SCC, info);
			free_irq(IRQ_TT_MFP_TIMA, info);
			free_pages ((unsigned long)scca_dma_buf[0].buf,
				(SCCA_DMA_BUFFERS * SCCA_DMA_BUFSIZE + PAGE_SIZE - 1) >> 12);
			kfree (scca_dma_buf[0].err);
		}
#endif
	}
#endif
#ifdef CONFIG_MVME147
	if (MACH_IS_MVME147) {
		free_irq(channel ? MVME147_IRQ_SCCB_TX : MVME147_IRQ_SCCA_TX, info);
		free_irq(channel ? MVME147_IRQ_SCCB_STAT : MVME147_IRQ_SCCA_STAT, info);
		free_irq(channel ? MVME147_IRQ_SCCB_RX : MVME147_IRQ_SCCA_RX, info);
		free_irq(channel ? MVME147_IRQ_SCCB_SPCOND : MVME147_IRQ_SCCA_SPCOND,
			info);
	}
#endif
#ifdef CONFIG_MVME16x
	if (MACH_IS_MVME16x) {
		free_irq(channel ? MVME162_IRQ_SCCB_TX : MVME162_IRQ_SCCA_TX, info);
		free_irq(channel ? MVME162_IRQ_SCCB_STAT : MVME162_IRQ_SCCA_STAT, info);
		free_irq(channel ? MVME162_IRQ_SCCB_RX : MVME162_IRQ_SCCA_RX, info);
		free_irq(channel ? MVME162_IRQ_SCCB_SPCOND : MVME162_IRQ_SCCA_SPCOND,
			info);
	}
#endif
#ifdef CONFIG_BVME6000
	if (MACH_IS_BVME6000) {
		free_irq(channel ? BVME_IRQ_SCCB_TX : BVME_IRQ_SCCA_TX, info);
		free_irq(channel ? BVME_IRQ_SCCB_STAT : BVME_IRQ_SCCA_STAT, info);
		free_irq(channel ? BVME_IRQ_SCCB_RX : BVME_IRQ_SCCA_RX, info);
		free_irq(channel ? BVME_IRQ_SCCB_SPCOND : BVME_IRQ_SCCA_SPCOND,
			info);
	}
#endif
}
#endif


#ifdef CONFIG_ATARI_SCC_DMA

/*****************************************************************************/

/*
 * TeSche's high-speed debugging helpers. it has proven (not only at this
 * place) that ordinary printk()s cause much more problems than they solve when
 * debugging interrupt handlers. these ones are not nice, but fast.
 */

#if DEBUG & DEBUG_DMA

#define DEBUGBUFSIZE 1024
static char debugBuf[DEBUGBUFSIZE];
static char *debugPtr = &debugBuf[0];
static char *debugEndPtr = &debugBuf[DEBUGBUFSIZE-1];

static inline void debugString (char *s)
{
	while (*s && (debugPtr != debugEndPtr))
	  *debugPtr++ = *s++;
}

static inline void debugInt (int i)
{
	char *tmp = "0123456789";   /* maxint (unsigned) = 4294967296, 10 digits */
	short cnt = 10;

	while (--cnt > 0) {
		tmp[cnt] = '0' + (i % 10);
		i /= 10;
	}

	while (*tmp == '0')
	  tmp++;

	while (*tmp && (debugPtr != debugEndPtr))
	  *debugPtr++ = *tmp++;
}

static char int2hex[] = "0123456789abcdef";

static inline void debugHex (unsigned int h)
{
	char *tmp = "01234567";
	short cnt = 8;

	while (--cnt > 0) {
		tmp[cnt] = int2hex[h & 15];
		h >>= 4;
	}

	while (*tmp == '0')
	  tmp++;

	while (*tmp && (debugPtr != debugEndPtr))
	  *debugPtr++ = *tmp++;
}

static inline void debugFlush (void)
{
	if (debugPtr != &debugBuf[0]) {
		*debugPtr = 0;
		printk ("%s\n", debugBuf);
		debugPtr = &debugBuf[0];
	}
}

#endif /* DEBUG & DEBUG_DMA */


/*****************************************************************************/

/*
 * these functions are for DMA support. they all assume that they're called
 * with INTs off so that they can play with their data structures undisturbed.
 */

static ulong dmaStartAddr = 0;   /* 0 means DMA not running */
static ulong dmaSize;


/* start DMA on current (scca_head) write buffer
 */
static inline void dma_start (void)
{
	if ((dmaSize = SCCA_DMA_BUFSIZE - scca_dma_head->inbuf) > 0) {
		dmaStartAddr = (ulong)(scca_dma_head->pbuf + scca_dma_head->inbuf); /* needs no virt_to_phys() */
#if DEBUG & DEBUG_DMA
		debugString ("[start@0x");
		debugHex (dmaStartAddr & 0xffff);
		debugString ("/");
		debugInt (dmaSize);
		debugString ("] ");
#endif
		tt_scc_dma.dma_ctrl &= ~3;
		__asm__ __volatile__ ("movep.l %0,%1@(0)\n\t"
							  "movep.l %2,%3@(0)\n\t"
							  : /* no outputs */
							  : "d"(dmaStartAddr), "a"(&tt_scc_dma.dma_addr_hi),
							  "d"(dmaSize), "a"(&tt_scc_dma.dma_cnt_hi)
							  : "memory");
		tt_scc_dma.dma_ctrl |= 2;
	} else {
		dmaStartAddr = 0;
	}
}


/* stop DMA, read restbytes and adjust buffer counters
 */
static inline void dma_stop (void)
{
	register short rest;
	ulong size = 0, endaddr;
	unsigned char *from = (unsigned char *)&tt_scc_dma.dma_restdata, *to;

	if (!dmaStartAddr)
	  return;

	tt_scc_dma.dma_ctrl &= ~3;   /* stop DMA */

	/* ++TeSche: I've had tremendous problems with looking at dma_addr to see
	 * how many bytes were received rather than looking at dma_cnt. To me it
	 * looks like there are cases when dma_addr is not properly updated when
	 * DMA is aborted, so I ended up with calculating less bytes than actually
	 * were received. Dma_cnt seems to be ok for me, so this is the way I go.
	 *
	 * sigh, yet one more unspecified hardware feature? is it at all anywhere
	 * specified what happens when a DMA is aborted before it ends?
	 */
	__asm__ __volatile__ ("movep.l %1@(0),%0\n\t"
						  : "=d"(size)
						  : "a"(&tt_scc_dma.dma_cnt_hi)
						  : "memory");
	size = dmaSize - size;

#if DEBUG & DEBUG_DMA
	__asm__ __volatile__ ("movep.l %1@(0),%0\n\t"
						  : "=d"(endaddr)
						  : "a"(&tt_scc_dma.dma_addr_hi)
						  : "memory");
	if (endaddr - dmaStartAddr != size) {
		debugString ("[size=");
		debugInt (size);
		debugString (",addr=");
		debugInt (endaddr-dmaStartAddr);
		debugString ("] ");
	}
#endif

	endaddr = dmaStartAddr + size;

	if ((dmaStartAddr & ~3) != (endaddr & ~3)) {
		/* at least one long was written. lower two bits of endaddress are
		 * number of restbytes. write them left-justified to the long the
		 * endaddress is in.
		 */
		rest = endaddr & 3;
		to = scca_dma_head->buf + (endaddr & (SCCA_DMA_BUFSIZE-1) & ~3);   /* needs no PTOV */
	} else {
		/* no long written. number of restbytes is endaddress -
		 * startaddress. write them to the startaddress.
		 */
		rest = size;   /* must and will be 0..3 */
		from += dmaStartAddr & 3;
		to = scca_dma_head->buf + (dmaStartAddr & (SCCA_DMA_BUFSIZE-1));   /* needs no PTOV */
	}

#if DEBUG & DEBUG_DMA
	debugString ("[stop@0x");
	debugHex (endaddr & 0xffff);
	debugString ("/");
	debugInt (size);
	if (rest) {
		debugString (",rest=");
		debugInt (rest);
		debugString ("@0x");
		debugHex ((unsigned int)from);
		debugString ("->");
		debugHex (((unsigned int)to) & 0xffff);
	}
	debugString ("] ");
#endif

	while (--rest >= 0)
	  *to++ = *from++;

	scca_dma_head->inbuf += size;
}


/* used by the SPCOND INT handler to deliver error codes. it's not very clean,
 * maybe overwrites older values, but since that only happens when something
 * has already gone wrong I don't consider this a problem.
 */
static inline void dma_fake_receive (u_char data, u_char err)
{
	scca_dma_head->buf[scca_dma_head->inbuf] = data;
	scca_dma_head->err[scca_dma_head->inbuf] = err;

	if (scca_dma_head->inbuf < SCCA_DMA_BUFSIZE-1)
	  scca_dma_head->inbuf++;

	switch (err) {
	  case TTY_OVERRUN:
		scca_dma_head->cntOver++;
		break;
	  case TTY_PARITY:
		scca_dma_head->cntPar++;
		break;
	  case TTY_FRAME:
		scca_dma_head->cntFrame++;
	}
}


/*****************************************************************************/

/*
 * these functions are for high-level DMA support. they make no assumptions
 * about current INT status when called.
 */

/* can't be called more than once due to tqueue handling + clever (?:) variable
 * design -> no cli/sti.
 */
static void SCC_flush (struct tty_struct *tty)
{
	int loops = 0;
	static int highWater = (95 * SCCA_DMA_BUFSIZE) / 100;

	/* a potential endless loop, but that's *really* unlikely...
	 */
	while (scca_dma_tail->needsFlushing) {

		/* ...anyway, be save
		 */
		if (++loops > SCCA_DMA_BUFFERS) {
			printk ("SCC-A: flush loop overrun\n");
			break;
		}

#if DEBUG & DEBUG_DMA
		if (scca_dma_tail->cntOver || scca_dma_tail->cntPar || scca_dma_tail->cntFrame) {
			debugString ("[ovr=");
			debugInt (scca_dma_tail->cntOver);
			debugString (",par=");
			debugInt (scca_dma_tail->cntPar);
			debugString (",frm=");
			debugInt (scca_dma_tail->cntFrame);
			debugString ("] ");
		}
#else
		if (scca_dma_tail->cntOver || scca_dma_tail->cntPar || scca_dma_tail->cntFrame)
		  printk ("SCC-A: %d overrun, %d parity, %d frame errors\n",
				  scca_dma_tail->cntOver, scca_dma_tail->cntPar, scca_dma_tail->cntFrame);
#endif

		if (scca_dma_tail->inbuf > highWater)
		  printk ("SCC-A: warning: buffer usage: %d/%d chars\n", scca_dma_tail->inbuf, SCCA_DMA_BUFSIZE);

#if 0
		if (scca_dma_tail->inbuf > 1) {
			scca_dma_tail->buf[0] |= 0x1;
			scca_dma_tail->buf[scca_dma_tail->inbuf-1] |= 0x2;
		}
#endif

		tty->ldisc.receive_buf (tty, scca_dma_tail->buf, scca_dma_tail->err, scca_dma_tail->inbuf);

		scca_dma_tail->cntOver = 0;
		scca_dma_tail->cntPar = 0;
		scca_dma_tail->cntFrame = 0;
		scca_dma_tail->inbuf = 0;
		scca_dma_tail->needsFlushing = 0;

		if (++scca_dma_tail == scca_dma_end)
		  scca_dma_tail = scca_dma_buf;
	}

#if DEBUG & DEBUG_DMA
	debugFlush ();
#endif
}


static struct tq_struct SCC_flush_tqueue = {
	NULL,		/* next */
	0,			/* sync */
	(void (*)(void*)) SCC_flush,  /* routine, must have (void *) arg... */
	NULL		/* data */
};


/* the 48Hz timer to flush data. runs in fact only every 4th call, say at 12Hz.
 */
static void SCC_timer_int (int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	static int delay = 4;
	ulong flags;
	SCC_ACCESS_INIT(info);

	/* if 'fp' is NULL we're called from SCC_dma_int, in which case we must
	 * respond immediately!
	 */
	if (fp && --delay > 0)
	  return;

#if DEBUG & DEBUG_DMA
	delay = 100;   /* 0.48Hz for better debugging, no more than 19k2b! */
#else
	delay = 4;   /* 12Hz delivery frequency (960 bytes / delivery @ 115k2b) */
#endif

	if (!SCC_flush_tqueue.data)
	  return;   /* no program listening... */

	save_flags (flags);
	cli ();

	if (scca_dma_head->active) {
		SCCmod (INT_AND_DMA_REG, ~(IDR_RX_INT_MASK|IDR_WAITREQ_ENAB), 0x00);
		dma_stop ();
		scca_dma_head->needsFlushing = 1;
		scca_dma_head->active = 0;
		if (++scca_dma_head == scca_dma_end)
		  scca_dma_head = scca_dma_buf;
	}

	if (!scca_dma_head->needsFlushing) {
		scca_dma_head->active = 1;
		dma_start ();
		SCCmod (INT_AND_DMA_REG, 0xff, IDR_RX_INT_SPCOND|IDR_WAITREQ_ENAB);
		/* this must *happen* after re-starting DMA for speed reasons.
		 */
		memset (scca_dma_head->err, 0x00, SCCA_DMA_BUFSIZE);
	} else {
		printk ("SCC-A: fatal buffer overflow, data lost!\n");
	}

	queue_task (&SCC_flush_tqueue, &tq_immediate);
	mark_bh (IMMEDIATE_BH);

	restore_flags (flags);
}


/* DMA finished before timer occurred?
 */
static void SCC_dma_int (int irq, void *data, struct pt_regs *fp)
{
	printk ("SCC-A: DMA-INT occurred, data lost!\n");
#if 0
	/* is there any reason why we should call this? if the timer INT was
	 * delayed so long that this happened then this INT was delayed too, so
	 * it's already too late.
	 */
	SCC_timer_int (irq, (struct m68k_async_struct *)data, NULL);
#endif
}

/*****************************************************************************/

#endif


#if DEBUG & DEBUG_OVERRUNS
static int SCC_ch_cnt[2] = { 0, 0 }, SCC_ch_ovrrun[2] = { 0, 0 };
#endif

static void SCC_rx_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	unsigned char	ch;
	SCC_ACCESS_INIT(info);

	SETUP_INFO(info);

	ch = SCCread_NB( RX_DATA_REG );
#if DEBUG & DEBUG_INT
	printk( "SCC ch %d rx int: char %02x\n", CHANNR(info), ch );
#endif
	rs_receive_char (info, ch, 0);
#if DEBUG & DEBUG_OVERRUNS
	{ int channel = CHANNR(info);
	  if (++SCC_ch_cnt[channel] == 10000) {
		  printk( "SCC ch. %d: overrun rate %d.%02d\n", channel,
				  SCC_ch_ovrrun[channel] / 100,
				  SCC_ch_ovrrun[channel] % 100 );
		  SCC_ch_cnt[channel] = SCC_ch_ovrrun[channel] = 0;
	  }
	}
#endif

	/* Check if another character is already ready; in that case, the
	 * spcond_int() function must be used, because this character may have an
	 * error condition that isn't signalled by the interrupt vector used!
	 */
	if (SCCread( INT_PENDING_REG ) &
	    (CHANNR(info) == CHANNEL_A ? IPR_A_RX : IPR_B_RX)) {
		SCC_spcond_int (0, info, 0);
		return;
	}

#ifndef ATARI_USE_SOFTWARE_EOI
	SCCwrite_NB( COMMAND_REG, CR_HIGHEST_IUS_RESET );
#endif
}


static void SCC_spcond_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	unsigned char	stat, ch, err;
	int		int_pending_mask = CHANNR(info) == CHANNEL_A ?
			                   IPR_A_RX : IPR_B_RX;
#ifdef CONFIG_ATARI_SCC_DMA
	int isdma = (CHANNR(info) == CHANNEL_A) && scca_dma;
	ulong flags = 0;
#endif
	SCC_ACCESS_INIT(info);
	
	SETUP_INFO(info);

#ifdef CONFIG_ATARI_SCC_DMA
	if (isdma) {
		save_flags (flags);
		cli ();
		SCCmod (INT_AND_DMA_REG, ~(IDR_RX_INT_MASK|IDR_WAITREQ_ENAB), 0);
		dma_stop ();
	}
#endif

	do {
		stat = SCCread( SPCOND_STATUS_REG );
		ch = SCCread_NB(RX_DATA_REG);
#if DEBUG & DEBUG_INT
		printk( "SCC ch %d spcond int: char %02x stat %02x\n",
			   CHANNR(info), ch, stat );
#endif

		if (stat & SCSR_RX_OVERRUN)
			err = TTY_OVERRUN;
		else if (stat & SCSR_PARITY_ERR)
			err = TTY_PARITY;
		else if (stat & SCSR_CRC_FRAME_ERR)
			err = TTY_FRAME;
		else
			err = 0;

#ifdef CONFIG_ATARI_SCC_DMA
		if (isdma)
		  dma_fake_receive (ch, err);
		else
#endif
		  rs_receive_char (info, ch, err);

		/* ++TeSche: *All* errors have to be cleared manually,
		 * else the condition persists for the next chars
		 */
		if (err)
		  SCCwrite(COMMAND_REG, CR_ERROR_RESET);

#if DEBUG & DEBUG_OVERRUNS
		{ int channel = CHANNR(info);
		  if (err == TTY_OVERRUN) SCC_ch_ovrrun[channel]++;
		  if (++SCC_ch_cnt[channel] == 10000) {
			  printk( "SCC ch. %d: overrun rate %d.%02d %%\n", channel,
					  SCC_ch_ovrrun[channel] / 100,
					  SCC_ch_ovrrun[channel] % 100 );
			  SCC_ch_cnt[channel] = SCC_ch_ovrrun[channel] = 0;
		  }
	    }
#endif

	} while( SCCread( INT_PENDING_REG ) & int_pending_mask );

#ifdef CONFIG_ATARI_SCC_DMA
	if (isdma) {
		dma_start ();
		SCCmod (INT_AND_DMA_REG, 0xff, IDR_RX_INT_SPCOND|IDR_WAITREQ_ENAB);
		restore_flags (flags);
	}
#endif

#ifndef ATARI_USE_SOFTWARE_EOI
	SCCwrite_NB( COMMAND_REG, CR_HIGHEST_IUS_RESET );
#endif
}


#ifdef ENABLE_ATARI_SCC
static void SCC_ri_int(int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	/* update input line counter */
	info->icount.rng++;
	wake_up_interruptible(&info->delta_msr_wait);
}
#endif


static void SCC_tx_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	int ch;
	SCC_ACCESS_INIT(info);

	SETUP_INFO(info);

	while( (SCCread_NB( STATUS_REG ) & SR_TX_BUF_EMPTY) &&
		   (ch = rs_get_tx_char( info )) >= 0 ) {
		SCCwrite( TX_DATA_REG, ch );
#if DEBUG & DEBUG_INT
		printk( "SCC ch. %d tx int: sent char %02x\n", CHANNR(info), ch );
#endif
	}

	if (rs_no_more_tx( info )) {
		/* disable tx interrupts */
		SCCmod (INT_AND_DMA_REG, ~IDR_TX_INT_ENAB, 0);
		SCCwrite( COMMAND_REG, CR_TX_PENDING_RESET );   /* disable tx_int on next tx underrun? */
#if DEBUG & DEBUG_INT
		printk ("SCC ch %d tx int: no more chars after %d sent\n",
				CHANNR (info), total);
#endif
	}

#ifndef ATARI_USE_SOFTWARE_EOI
	SCCwrite_NB( COMMAND_REG, CR_HIGHEST_IUS_RESET );
#endif
}


static void SCC_stat_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	unsigned channel = CHANNR(info);
	unsigned char	last_sr, sr, changed;
	SCC_ACCESS_INIT(info);

	SETUP_INFO(info);

	last_sr = SCC_last_status_reg[channel];
	sr = SCC_last_status_reg[channel] = SCCread_NB( STATUS_REG );
	changed = last_sr ^ sr;
#if DEBUG & DEBUG_INT
	printk( "SCC ch %d stat int: sr=%02x last_sr=%02x\n",
			CHANNR(info), sr, last_sr );
#endif

	if (changed & SR_DCD)
		rs_dcd_changed( info, sr & SR_DCD );

	if (changed & SR_CTS) {
#if DEBUG & DEBUG_THROTTLE
		printk( "SCC ch. %d: now CTS=%d\n", CHANNR(info), !!(sr & SR_CTS) );
#endif
		rs_check_cts( info, sr & SR_CTS );
	}

	if (changed & SR_SYNC_ABORT) { /* Data Set Ready */
		/* update input line counter */
		info->icount.dsr++;
		wake_up_interruptible(&info->delta_msr_wait);
	}

	SCCwrite( COMMAND_REG, CR_EXTSTAT_RESET );
#ifndef ATARI_USE_SOFTWARE_EOI
	SCCwrite_NB( COMMAND_REG, CR_HIGHEST_IUS_RESET );
#endif
}


static int SCC_check_open( struct m68k_async_struct *info, struct tty_struct *tty,
			  struct file *file )
{
	/* If channel A is opened, check if one of the compounded ports (ttyS3 and
	 * ttyS4) is already open, else activate the appropriate port hardware.
	 */

#if DEBUG & DEBUG_OPEN
	printk( "SCC: about to open channel %d as line %d\n",
			CHANNR(info), info->line );
#endif

	if (CHANNR(info) == CHANNEL_A) {

		if (SCC_chan_a_open) {
			if (SCC_chan_a_line != info->line) {
#if DEBUG & DEBUG_OPEN
				printk("SCC: channel 0 was already open\n");
#endif
				return -EBUSY;
			}
			else
				return 0;
		}

		if ((info->line == cha232_line &&
		     SCC_chan_a_switchable == SCCA_SWITCH_LAN_ONLY) ||
		    (info->line == cha422_line &&
		     SCC_chan_a_switchable == SCCA_SWITCH_SERIAL2_ONLY))
			return( -ENODEV );

		SCC_chan_a_open = 1;
		SCC_chan_a_line = info->line;
		SCC_chan_a_info = &rs_table[info->line];
#ifdef ENABLE_ATARI_SCC
		if (SCC_chan_a_switchable == SCCA_SWITCH_BOTH) {
			unsigned long flags; 
			unsigned char tmp;

			save_flags(flags);
			cli();
			sound_ym.rd_data_reg_sel = 14;
			tmp = sound_ym.rd_data_reg_sel;
			sound_ym.wd_data = (info->line == cha232_line
					    ? tmp | 0x80
					    : tmp & 0x7f);
#if DEBUG & DEBUG_OPEN
			printk( "SCC: set PSG IO7 to %02x (was %02x)\n",
			       (info->line & 1) ? (tmp | 0x80) : (tmp & 0x7f),
			       tmp );
#endif
			restore_flags(flags);
		}
#endif
	}
	return( 0 );
}


static void SCC_init( struct m68k_async_struct *info )
{
	int i, channel = CHANNR(info);
	unsigned long	flags;
	SCC_ACCESS_INIT(info);
#ifdef ENABLE_ATARI_SCC
	static const struct {
		unsigned reg, val;
	} init_tab[] = {
		/* no parity, 1 stop bit, async, 1:16 */
		{ AUX1_CTRL_REG, A1CR_PARITY_NONE|A1CR_MODE_ASYNC_1|A1CR_CLKMODE_x64 },
		/* parity error is special cond, ints disabled, no DMA */
		{ INT_AND_DMA_REG, IDR_PARERR_AS_SPCOND | IDR_RX_INT_DISAB },
		/* Rx 8 bits/char, no auto enable, Rx off */
		{ RX_CTRL_REG, RCR_CHSIZE_8 },
		/* DTR off, Tx 8 bits/char, RTS off, Tx off */
		{ TX_CTRL_REG, TCR_CHSIZE_8 },
		/* special features off */
		{ AUX2_CTRL_REG, 0 },
		/* RTxC is XTAL, TRxC is input, both clocks = RTxC */
		{ CLK_CTRL_REG, CCR_TRxCOUT_XTAL | CCR_TXCLK_RTxC | CCR_RXCLK_RTxC },
		{ DPLL_CTRL_REG, 0 },
		/* Start Rx */
		{ RX_CTRL_REG, RCR_RX_ENAB | RCR_CHSIZE_8 },
		/* Start Tx */
		{ TX_CTRL_REG, TCR_TX_ENAB | TCR_RTS | TCR_DTR | TCR_CHSIZE_8 },
		/* Ext/Stat ints: CTS, DCD, SYNC (DSR) */
		{ INT_CTRL_REG, ICR_ENAB_DCD_INT | ICR_ENAB_CTS_INT | ICR_ENAB_SYNC_INT },
		/* Reset Ext/Stat ints */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* ...again */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* Rx int always, TX int off, Ext/Stat int on */
		{ INT_AND_DMA_REG, IDR_EXTSTAT_INT_ENAB |
		  IDR_PARERR_AS_SPCOND | IDR_RX_INT_ALL }
	};
#ifdef CONFIG_ATARI_SCC_DMA
	static const struct {
		unsigned reg, val;
	} init_withdma_tab[] = {
		/* no parity, 1 stop bit, async, 1:16 */
		{ AUX1_CTRL_REG, A1CR_PARITY_NONE|A1CR_MODE_ASYNC_1|A1CR_CLKMODE_x64 },
		/* parity error is special cond, ints disabled, DMA receive but disabled */
		{ INT_AND_DMA_REG, IDR_PARERR_AS_SPCOND | IDR_RX_INT_DISAB |
			IDR_WAITREQ_RX | IDR_WAITREQ_IS_REQ},
		/* Rx 8 bits/char, no auto enable, Rx off */
		{ RX_CTRL_REG, RCR_CHSIZE_8 },
		/* DTR off, Tx 8 bits/char, RTS off, Tx off */
		{ TX_CTRL_REG, TCR_CHSIZE_8 },
		/* special features off */
		{ AUX2_CTRL_REG, 0 },
		/* RTxC is XTAL, TRxC is input, both clocks = RTxC */
		{ CLK_CTRL_REG, CCR_TRxCOUT_XTAL | CCR_TXCLK_RTxC | CCR_RXCLK_RTxC },
		{ DPLL_CTRL_REG, 0 },
		/* Start Rx */
		{ RX_CTRL_REG, RCR_RX_ENAB | RCR_CHSIZE_8 },
		/* Start Tx */
		{ TX_CTRL_REG, TCR_TX_ENAB | TCR_RTS | TCR_DTR | TCR_CHSIZE_8 },
		/* Ext/Stat ints: CTS, DCD, SYNC (DSR) */
		{ INT_CTRL_REG, ICR_ENAB_DCD_INT | ICR_ENAB_CTS_INT | ICR_ENAB_SYNC_INT },
		/* Reset Ext/Stat ints */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* ...again */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* parity error is special cond, Tx & SPcond ints enabled, Rx int disabled, DMA receive but disabled */
		{ INT_AND_DMA_REG, IDR_EXTSTAT_INT_ENAB | IDR_PARERR_AS_SPCOND |
			IDR_RX_INT_DISAB | IDR_WAITREQ_RX | IDR_WAITREQ_IS_REQ}
	};
#endif
#endif
#ifdef CONFIG_MVME147_SCC
	static const struct {
		unsigned reg, val;
	} m147_init_tab[] = {
		/* Values for MVME147 */
		/* no parity, 1 stop bit, async, 1:16 */
		{ AUX1_CTRL_REG, A1CR_PARITY_NONE|A1CR_MODE_ASYNC_1|A1CR_CLKMODE_x16 },
		/* parity error is special cond, ints disabled, no DMA */
		{ INT_AND_DMA_REG, IDR_PARERR_AS_SPCOND | IDR_RX_INT_DISAB },
		/* Rx 8 bits/char, no auto enable, Rx off */
		{ RX_CTRL_REG, RCR_CHSIZE_8 },
		/* DTR off, Tx 8 bits/char, RTS off, Tx off */
		{ TX_CTRL_REG, TCR_CHSIZE_8 },
		/* special features off */
		{ AUX2_CTRL_REG, 0 },
		{ CLK_CTRL_REG, CCR_RXCLK_BRG | CCR_TXCLK_BRG },
		{ DPLL_CTRL_REG, DCR_BRG_ENAB | DCR_BRG_USE_PCLK },
		/* Start Rx */
		{ RX_CTRL_REG, RCR_RX_ENAB | RCR_CHSIZE_8 },
		/* Start Tx */
		{ TX_CTRL_REG, TCR_TX_ENAB | TCR_RTS | TCR_DTR | TCR_CHSIZE_8 },
		/* Ext/Stat ints: CTS, DCD, SYNC (DSR) */
		{ INT_CTRL_REG, ICR_ENAB_DCD_INT | ICR_ENAB_CTS_INT | ICR_ENAB_SYNC_INT },
		/* Reset Ext/Stat ints */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* ...again */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* Rx int always, TX int off, Ext/Stat int on */
		{ INT_AND_DMA_REG, IDR_EXTSTAT_INT_ENAB |
		  IDR_PARERR_AS_SPCOND | IDR_RX_INT_ALL }
	};
#endif
#ifdef CONFIG_MVME162_SCC
	static const struct {
		unsigned reg, val;
	} mvme_init_tab[] = {
		/* Values for MVME162 */
		/* no parity, 1 stop bit, async, 1:16 */
		{ AUX1_CTRL_REG, A1CR_PARITY_NONE|A1CR_MODE_ASYNC_1|A1CR_CLKMODE_x16 },
		/* parity error is special cond, ints disabled, no DMA */
		{ INT_AND_DMA_REG, IDR_PARERR_AS_SPCOND | IDR_RX_INT_DISAB },
		/* Rx 8 bits/char, no auto enable, Rx off */
		{ RX_CTRL_REG, RCR_CHSIZE_8 },
		/* DTR off, Tx 8 bits/char, RTS off, Tx off */
		{ TX_CTRL_REG, TCR_CHSIZE_8 },
		/* special features off */
		{ AUX2_CTRL_REG, 0 },
		{ CLK_CTRL_REG, CCR_RXCLK_BRG | CCR_TXCLK_BRG },
		{ DPLL_CTRL_REG, DCR_BRG_ENAB | DCR_BRG_USE_PCLK },
		/* Start Rx */
		{ RX_CTRL_REG, RCR_RX_ENAB | RCR_CHSIZE_8 },
		/* Start Tx */
		{ TX_CTRL_REG, TCR_TX_ENAB | TCR_RTS | TCR_DTR | TCR_CHSIZE_8 },
		/* Ext/Stat ints: CTS, DCD, SYNC (DSR) */
		{ INT_CTRL_REG, ICR_ENAB_DCD_INT | ICR_ENAB_CTS_INT | ICR_ENAB_SYNC_INT },
		/* Reset Ext/Stat ints */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* ...again */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* Rx int always, TX int off, Ext/Stat int on */
		{ INT_AND_DMA_REG, IDR_EXTSTAT_INT_ENAB |
		  IDR_PARERR_AS_SPCOND | IDR_RX_INT_ALL }
	};
#endif
#ifdef CONFIG_BVME6000_SCC
	static const struct {
		unsigned reg, val;
	} bvme_init_tab[] = {
		/* Values for BVME6000 */
		/* no parity, 1 stop bit, async, 1:16 */
		{ AUX1_CTRL_REG, A1CR_PARITY_NONE|A1CR_MODE_ASYNC_1|A1CR_CLKMODE_x16 },
		/* parity error is special cond, ints disabled, no DMA */
		{ INT_AND_DMA_REG, IDR_PARERR_AS_SPCOND | IDR_RX_INT_DISAB },
		/* Rx 8 bits/char, no auto enable, Rx off */
		{ RX_CTRL_REG, RCR_CHSIZE_8 },
		/* DTR off, Tx 8 bits/char, RTS off, Tx off */
		{ TX_CTRL_REG, TCR_CHSIZE_8 },
		/* special features off */
		{ AUX2_CTRL_REG, 0 },
		{ CLK_CTRL_REG, CCR_RTxC_XTAL | CCR_RXCLK_BRG | CCR_TXCLK_BRG },
		{ DPLL_CTRL_REG, DCR_BRG_ENAB },
		/* Start Rx */
		{ RX_CTRL_REG, RCR_RX_ENAB | RCR_CHSIZE_8 },
		/* Start Tx */
		{ TX_CTRL_REG, TCR_TX_ENAB | TCR_RTS | TCR_DTR | TCR_CHSIZE_8 },
		/* Ext/Stat ints: CTS, DCD, SYNC (DSR) */
		{ INT_CTRL_REG, ICR_ENAB_DCD_INT | ICR_ENAB_CTS_INT | ICR_ENAB_SYNC_INT },
		/* Reset Ext/Stat ints */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* ...again */
		{ COMMAND_REG, CR_EXTSTAT_RESET },
		/* Rx int always, TX int off, Ext/Stat int on */
		{ INT_AND_DMA_REG, IDR_EXTSTAT_INT_ENAB |
		  IDR_PARERR_AS_SPCOND | IDR_RX_INT_ALL }
	};
#endif
	save_flags(flags);
	cli();

	if (!MACH_IS_MVME16x && !MACH_IS_BVME6000 && !MACH_IS_MVME147) {
		SCCmod( MASTER_INT_CTRL, 0x3f,
			channel == 0 ? MIC_CH_A_RESET : MIC_CH_B_RESET );
		udelay(40); /* extra delay after a reset */
	}

#ifdef ENABLE_ATARI_SCC
	if (MACH_IS_ATARI) {
#ifdef CONFIG_ATARI_SCC_DMA
		if (channel == CHANNEL_A && scca_dma) {

			for (i=0; i<sizeof(init_withdma_tab)/sizeof(*init_withdma_tab); ++i)
				  SCCwrite( init_withdma_tab[i].reg, init_withdma_tab[i].val );

			/* enable SCC-DMA INTs
			 */
			tt_mfp.int_en_b &= ~4;
			tt_mfp.active_edge |= 4;
			tt_mfp.int_pn_b = ~4;
			tt_mfp.int_mk_b |= 4;
			tt_mfp.int_en_b |= 4;

			/* init and start dma
			 */
			scca_dma_head = scca_dma_tail = scca_dma_buf;
			for (i=0; i<SCCA_DMA_BUFFERS; i++) {
				scca_dma_buf[i].inbuf = 0;
				scca_dma_buf[i].cntOver = 0;
				scca_dma_buf[i].cntPar = 0;
				scca_dma_buf[i].cntFrame = 0;
				scca_dma_buf[i].active = 0;
				scca_dma_buf[i].needsFlushing = 0;
				memset (scca_dma_buf[i].err, 0, SCCA_DMA_BUFSIZE);
			}

			scca_dma_head->active = 1;
			dma_start ();
			SCCmod (INT_AND_DMA_REG, 0xff, IDR_RX_INT_SPCOND|IDR_WAITREQ_ENAB);

			SCC_flush_tqueue.data = ((struct m68k_async_struct *)info)->tty;

		} else
#endif
		{
			for (i=0; i<sizeof(init_tab)/sizeof(*init_tab); ++i)
				SCCwrite( init_tab[i].reg, init_tab[i].val );
		}
	}
#endif
#ifdef CONFIG_MVME147_SCC
	if (MACH_IS_MVME147) {
		for (i=0; i<sizeof(m147_init_tab)/sizeof(*m147_init_tab); ++i)
			SCCwrite( m147_init_tab[i].reg, m147_init_tab[i].val );
	}
#endif
#ifdef CONFIG_MVME162_SCC
	if (MACH_IS_MVME16x) {
		for (i=0; i<sizeof(mvme_init_tab)/sizeof(*mvme_init_tab); ++i)
			SCCwrite( mvme_init_tab[i].reg, mvme_init_tab[i].val );
	}
#endif
#ifdef CONFIG_BVME6000_SCC
	if (MACH_IS_BVME6000) {
		for (i=0; i<sizeof(bvme_init_tab)/sizeof(*bvme_init_tab); ++i)
			SCCwrite( bvme_init_tab[i].reg, bvme_init_tab[i].val );
	}
#endif

	/* remember status register for detection of DCD and CTS changes */
	SCC_last_status_reg[channel] = SCCread( STATUS_REG );
	restore_flags(flags);
#if DEBUG & DEBUG_INIT
	printk( "SCC channel %d inited\n", CHANNR(info) );
#endif
#if DEBUG & DEBUG_OPEN
	printk( "SCC channel %d opened\n", CHANNR(info) );
#endif
	MOD_INC_USE_COUNT;
}


static void SCC_deinit( struct m68k_async_struct *info, int leave_dtr )
{
	unsigned long	flags, timeout;
	SCC_ACCESS_INIT(info);

#if DEBUG & DEBUG_INIT
	printk( "SCC channel %d about to be deinited\n", CHANNR(info) );
#endif

	if (MACH_IS_MVME147 && CHANNR(info) == CHANNEL_A)
		return;		/* Channel A is our console */
	if (MACH_IS_MVME16x && CHANNR(info) == CHANNEL_A)
		return;		/* 162Bug uses channel A */
	if (MACH_IS_BVME6000 && CHANNR(info) == CHANNEL_A)
		return;		/* BVMbug uses channel A */

	save_flags(flags);
	cli();

#ifdef CONFIG_ATARI_SCC_DMA
	if (CHANNR(info) == CHANNEL_A && scca_dma) {
		SCC_flush_tqueue.data = NULL;
		SCCmod (INT_AND_DMA_REG, ~(IDR_RX_INT_MASK|IDR_WAITREQ_ENAB), 0);
		dma_stop ();
		tt_mfp.int_en_b &= ~4; /* disable TT-MFP INTs */
		/* ++TeSche: for the moment we don't care if there's still data unflushed */
	}
#endif

	/* disable interrupts */
	SCCmod( INT_AND_DMA_REG, ~(IDR_EXTSTAT_INT_ENAB | IDR_TX_INT_ENAB |
							   IDR_RX_INT_SPCOND), 0 );

	/* disable Rx */
	SCCmod( RX_CTRL_REG, ~RCR_RX_ENAB, 0 );

	/* disable Transmitter */
	SCCmod( TX_CTRL_REG, ~TCR_TX_ENAB, 0 );

	/* wait until character is completely sent */
	timeout = jiffies + 50;
	restore_flags(flags);
	while( !(SCCread( SPCOND_STATUS_REG ) & SCSR_ALL_SENT) ) {
		if (time_after(jiffies, timeout)) break;
	}
	save_flags(flags);
	cli();

	/* drop RTS and maybe DTR */
	SCCmod( TX_CTRL_REG, ~(TCR_RTS | (leave_dtr ? 0 : TCR_DTR)), 0 );

	restore_flags(flags);
#if DEBUG & DEBUG_INIT
	printk( "SCC channel %d deinited\n", CHANNR(info) );
#endif

	if (CHANNR(info) == CHANNEL_A)
		SCC_chan_a_open = 0;
#if DEBUG & DEBUG_OPEN
	printk( "SCC channel %d closed, chanAlock now = %d\n",
			CHANNR(info), SCC_chan_a_open );
#endif
	MOD_DEC_USE_COUNT;
}


static void SCC_enab_tx_int( struct m68k_async_struct *info, int enab_flag )
{
	unsigned long	flags;
	unsigned char	iadr;
	SCC_ACCESS_INIT(info);

	save_flags(flags);
	cli();

	iadr = SCCread( INT_AND_DMA_REG );
	if (!!(iadr & IDR_TX_INT_ENAB) != !!enab_flag) {
		SCCwrite(INT_AND_DMA_REG, iadr ^ IDR_TX_INT_ENAB);
#if DEBUG & DEBUG_INT
		printk("SCC ch %d: tx int %sabled\n", CHANNR(info),
		       enab_flag ? "en" : "dis");
#endif
		if (enab_flag)
		  /* restart the transmitter */
		  SCC_tx_int (0, info, 0);
	}

	restore_flags(flags);
}


static int SCC_check_custom_divisor( struct m68k_async_struct *info,
				    int baud_base, int divisor )
{
	int		clksrc;

	clksrc = SCC_clocksrc (baud_base, CHANNR (info));
	if (clksrc < 0)
		/* invalid baud base */
		return( -1 );

	/* check for maximum (BRG values start from 4 with step 2) */
	if (divisor/2-2 > 65535)
		return( -1 );

	switch( clksrc ) {

	  case CLK_PCLK:
		/* The master clock can only be used with the BRG, divisors
		 * range from 4 and must be a multiple of 2
		 */
		return( !(divisor >= 4 && (divisor & 1) == 0) );

	  case CLK_RTxC:
		/* The RTxC clock can either be used for the direct 1:16, 1:32
		 * or 1:64 modes (divisors 1, 2 or 4, resp.) or with the BRG
		 * (divisors from 4 and a multiple of 2)
		 */
		return( !(divisor >= 1 && (divisor == 1 || (divisor & 1) == 0)) );

	  case CLK_TRxC:
		/* The TRxC clock can only be used for direct 1:16, 1:32 or
		 * 1:64 modes
		 */
		return( !(divisor == 1 || divisor == 2 || divisor == 4) );

	}
	return( -1 );
}


static void SCC_change_speed( struct m68k_async_struct *info )
{
	/* the SCC has char sizes 5,7,6,8 in that order! */
	static int chsize_map[4] = { 0, 2, 1, 3 };
	unsigned cflag, baud, chsize, aflags;
	unsigned channel, div = 0, clkmode, brgmode, brgval;
	int clksrc = 0;
	unsigned long flags;
	SCC_ACCESS_INIT(info);

	if (!info->tty || !info->tty->termios) return;

	channel = CHANNR(info);

	if (MACH_IS_MVME147 && channel == CHANNEL_A)
		return;		/* Settings controlled by 147Bug */
	if (MACH_IS_MVME16x && channel == CHANNEL_A)
		return;		/* Settings controlled by 162Bug */
	if (MACH_IS_BVME6000 && channel == CHANNEL_A)
		return;		/* Settings controlled by BVMBug */

	cflag  = info->tty->termios->c_cflag;
	baud   = cflag & CBAUD;
	chsize = (cflag & CSIZE) >> 4;
	aflags = info->flags & ASYNC_SPD_MASK;

	if (cflag & CRTSCTS)
		info->flags |= ASYNC_CTS_FLOW;
	else
		info->flags &= ~ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else
		info->flags |= ASYNC_CHECK_CD;

#if DEBUG & DEBUG_SPEED
	printk( "SCC channel %d: doing new settings:\n", CHANNR(info) );
	printk( "  baud=%d chsize=%d aflags=%04x base_baud=%d divisor=%d\n",
			baud, chsize, aflags, info->baud_base, info->custom_divisor );
#endif

	if (baud == 0 && !aflags) {
		/* speed == 0 -> drop DTR */
		save_flags(flags);
		cli();
		SCCmod( TX_CTRL_REG, ~TCR_DTR, 0 );
		restore_flags(flags);
		return;
	}

	if (baud & CBAUDEX) {
		baud &= ~CBAUDEX;
		if (baud < 1 || baud > (MACH_IS_MVME16x ? 2 : 4))
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			baud += 15;
	}
	if (baud == 15 && aflags) {
		switch( aflags) {
		  case ASYNC_SPD_HI:
			baud = 16;
			break;
		  case ASYNC_SPD_VHI:
			baud = 17;
			break;
		  case ASYNC_SPD_SHI:
			baud = 18;
			break;
		  case ASYNC_SPD_WARP:
			baud = 19;
			break;
		  case ASYNC_SPD_CUST:
			/* Custom divisor: Compute clock source from the base_baud
			 * field */
			if ((clksrc = SCC_clocksrc( info->baud_base, channel )) < 0)
				/* This shouldn't happen... the baud_base has been checked
				 * before by check_custom_divisor() */
				return;
			div = info->custom_divisor;
		}
	}

	if (!div) {
		if (baud > 19) baud = 19;
		clksrc = SCC_baud_table[channel][baud].clksrc;
		div = SCC_baud_table[channel][baud].div;
		if(!div)
		{
			printk(" SCC_change_speed: divisor = 0 !!!");
			return;
		}
	}

	/* compute the SCC's clock source, clock mode, BRG mode and BRG
	 * value from clksrc and div
	 */
	if (div <= 4) {
		clkmode = (div == 1 ? A1CR_CLKMODE_x16 :
			   div == 2 ? A1CR_CLKMODE_x32 :
				      A1CR_CLKMODE_x64);
		clksrc  = (clksrc == CLK_RTxC
			   ? CCR_TXCLK_RTxC | CCR_RXCLK_RTxC
			   : CCR_TXCLK_TRxC | CCR_RXCLK_TRxC);
		brgmode = 0; /* off */
		brgval  = 0;
	}
	else {
		brgval  = div/2 - 2;
		brgmode = (DCR_BRG_ENAB |
			   (clksrc == CLK_PCLK ? DCR_BRG_USE_PCLK : 0));
		clkmode = A1CR_CLKMODE_x16;
		clksrc  = CCR_TXCLK_BRG | CCR_RXCLK_BRG;
	}

	/* Now we have all parameters and can go to set them: */
	save_flags(flags);
	cli();
#if DEBUG & DEBUG_SPEED
	printk( "  brgval=%d brgmode=%02x clkmode=%02x clksrc=%02x\n",
			brgval, brgmode, clkmode, clksrc );
#endif

	/* receiver's character size */
	SCCmod( RX_CTRL_REG, ~RCR_CHSIZE_MASK, chsize_map[chsize] << 6 );
#if DEBUG & DEBUG_SPEED
	printk( "  RX_CTRL_REG <- %02x\n", SCCread( RX_CTRL_REG ) );
#endif

	/* parity and stop bits (both, Tx and Rx) and clock mode */
	SCCmod (AUX1_CTRL_REG,
		~(A1CR_PARITY_MASK | A1CR_MODE_MASK | A1CR_CLKMODE_MASK),
		((cflag & PARENB
		  ? (cflag & PARODD ? A1CR_PARITY_ODD : A1CR_PARITY_EVEN)
		  : A1CR_PARITY_NONE)
		 | (cflag & CSTOPB ? A1CR_MODE_ASYNC_2 : A1CR_MODE_ASYNC_1)
		 | clkmode));
#if DEBUG & DEBUG_SPEED
	printk( "  AUX1_CTRL_REG <- %02x\n", SCCread( AUX1_CTRL_REG ) );
#endif

	/* sender's character size */
	/* Set DTR for valid baud rates! Tnx to jds@kom.auc.dk */
	SCCmod( TX_CTRL_REG, ~TCR_CHSIZE_MASK, chsize_map[chsize] << 5 | TCR_DTR );
#if DEBUG & DEBUG_SPEED
	printk( "  TX_CTRL_REG <- %02x\n", SCCread( TX_CTRL_REG ) );
#endif

	/* clock sources */
	SCCmod( CLK_CTRL_REG, ~(CCR_TXCLK_MASK | CCR_RXCLK_MASK), clksrc );
#if DEBUG & DEBUG_SPEED
	printk( "  CLK_CTRL_REG <- %02x\n", SCCread( CLK_CTRL_REG ) );
#endif

	/* disable BRG before changing the value */
	SCCmod( DPLL_CTRL_REG, ~DCR_BRG_ENAB, 0 );

	/* BRG value */
	SCCwrite( TIMER_LOW_REG, brgval & 0xff );
	SCCwrite( TIMER_HIGH_REG, (brgval >> 8) & 0xff );

	/* BRG enable and clock source */
	SCCmod( DPLL_CTRL_REG, ~(DCR_BRG_ENAB | DCR_BRG_USE_PCLK), brgmode );
#if DEBUG & DEBUG_SPEED
	printk( "  TIMER_LOW_REG <- %02x\n", SCCread( TIMER_LOW_REG ) );
	printk( "  TIMER_HIGH_REG <- %02x\n", SCCread( TIMER_HIGH_REG ) );
#endif
#if DEBUG & DEBUG_SPEED
	printk( "  DPLL_CTRL_REG <- %02x\n", SCCread( DPLL_CTRL_REG ) );
#endif

	restore_flags(flags);
}


static int SCC_clocksrc( unsigned baud_base, unsigned channel )
{
	if (baud_base == SCC_PCLK)
		return( CLK_PCLK );
	else if (SCC_clocks[channel][CLK_RTxC] != SCC_BAUD_BASE_NONE &&
		 baud_base == SCC_clocks[channel][CLK_RTxC])
		return( CLK_RTxC );
	else if (SCC_clocks[channel][CLK_TRxC] != SCC_BAUD_BASE_NONE &&
		 baud_base == SCC_clocks[channel][CLK_TRxC])
		return( CLK_TRxC );
	else
		return( -1 );
}

static void SCC_throttle( struct m68k_async_struct *info, int status )
{
	unsigned long	flags;
	SCC_ACCESS_INIT(info);

#if DEBUG & DEBUG_THROTTLE
	printk( "SCC channel %d: throttle %s\n",
	       CHANNR(info), status ? "full" : "avail" );
#endif
	save_flags(flags);
	cli();

	if (status)
		SCCmod( TX_CTRL_REG, ~TCR_RTS, 0 );
	else
		SCCmod( TX_CTRL_REG, 0xff, TCR_RTS );

#if DEBUG & DEBUG_THROTTLE
	printk( "  now TX_CTRL_REG = %02x\n", SCCread( TX_CTRL_REG ) );
#endif

	restore_flags(flags);
}


static void SCC_set_break( struct m68k_async_struct *info, int break_flag )
{
	unsigned long	flags;
	SCC_ACCESS_INIT(info);

	save_flags(flags);
	cli();

	if (break_flag) {
		SCCmod( TX_CTRL_REG, 0xff, TCR_SEND_BREAK );
	} else {
		SCCmod( TX_CTRL_REG, ~TCR_SEND_BREAK, 0 );
	}

	restore_flags(flags);
}


static void SCC_get_serial_info( struct m68k_async_struct *info,
				struct serial_struct *retinfo )
{
	retinfo->baud_base = info->baud_base;
	retinfo->custom_divisor = info->custom_divisor;
}


static unsigned int SCC_get_modem_info( struct m68k_async_struct *info )
{
	unsigned	sr, tcr, ri = 0, dsr = 0;
	unsigned long	flags;
	SCC_ACCESS_INIT(info);
	
	save_flags(flags);
	cli();
	sr = SCCread( STATUS_REG );
	tcr = SCCread( TX_CTRL_REG );
	restore_flags(flags);
#if DEBUG & DEBUG_INFO
	printk( "SCC channel %d: get info, sr=%02x tcr=%02x\n",
			CHANNR(info), sr, tcr );
#endif
#if defined(CONFIG_MVME162_SCC) || defined(CONFIG_BVME6000_SCC) || defined(CONFIG_MVME147_SCC)
	if (MACH_IS_MVME147 || MACH_IS_MVME16x || MACH_IS_BVME6000) {
		ri = 0;
		dsr = sr & SR_SYNC_ABORT ? TIOCM_DSR : 0;
	}
#endif
#ifdef ENABLE_ATARI_SCC
	if (MACH_IS_ATARI) {
		if (CHANNR (info) == 0)
			ri = 0;
		else if (ATARIHW_PRESENT (TT_MFP))
			ri = tt_mfp.par_dt_reg & (1 << 3) ? 0 : TIOCM_RNG;
		else
			ri = mfp.par_dt_reg & (1 << 6) ? 0 : TIOCM_RNG;

		if (ATARIHW_PRESENT (ST_ESCC))
			dsr = st_escc_dsr & (1 << (3 - CHANNR(info))) ? TIOCM_DSR : 0;
		else
			dsr = sr & SR_SYNC_ABORT ? TIOCM_DSR : 0;
	}
#endif
	return (((tcr & TCR_RTS) ? TIOCM_RTS : 0) |
		((tcr & TCR_DTR) ? TIOCM_DTR : 0) |
		((sr & SR_DCD ) ? TIOCM_CAR : 0) |
		((sr & SR_CTS ) ? TIOCM_CTS : 0) |
		dsr | ri);
}


static int SCC_set_modem_info( struct m68k_async_struct *info,
			      int new_dtr, int new_rts )
{
	unsigned long	flags;
	SCC_ACCESS_INIT(info);
	
	save_flags(flags);
	cli();

	if (new_dtr == 0) {
		SCCmod( TX_CTRL_REG, ~TCR_DTR, 0 );
	} else if (new_dtr == 1) {
		SCCmod( TX_CTRL_REG, 0xff, TCR_DTR );
	}

	if (new_rts == 0) {
		SCCmod( TX_CTRL_REG, ~TCR_RTS, 0 );
	} else if (new_rts == 1) {
		SCCmod( TX_CTRL_REG, 0xff, TCR_RTS );
	}

#if DEBUG & DEBUG_INFO
	printk( "SCC channel %d: set info (dtr=%d,rts=%d), now tcr=%02x\n",
	       CHANNR(info), new_dtr, new_rts, SCCread( TX_CTRL_REG ) );
#endif

	restore_flags(flags);
	return( 0 );
}

static void SCC_stop_receive (struct m68k_async_struct *info)
{
	SCC_ACCESS_INIT(info);
	
#ifdef CONFIG_ATARI_SCC_DMA
	dma_stop ();
#endif

	/* disable Rx interrupts */
	SCCmod (INT_AND_DMA_REG, ~IDR_RX_INT_MASK, 0);

	/* disable Rx */
	if (!((MACH_IS_MVME16x || MACH_IS_BVME6000) && CHANNR(info) == CHANNEL_A))
		SCCmod (RX_CTRL_REG, ~RCR_RX_ENAB, 0);
}

static int SCC_trans_empty (struct m68k_async_struct *info)
{
	SCC_ACCESS_INIT(info);
	
	return (SCCread (SPCOND_STATUS_REG) & SCSR_ALL_SENT) != 0;
}

static int SCC_ioctl( struct tty_struct *tty, struct file *file,
		     struct m68k_async_struct *info, unsigned int cmd,
		     unsigned long arg )
{
	struct atari_SCCserial *cp = (void *)arg;
	int error;
	unsigned channel = CHANNR(info), i, clk, div, rtxc, trxc, pclk;

	switch( cmd ) {

	  case TIOCGATSCC:

		error = verify_area( VERIFY_WRITE, (void *)arg,
				    sizeof(struct atari_SCCserial) );
		if (error)
			return error;

		put_user(SCC_clocks[channel][CLK_RTxC], &cp->RTxC_base);
		put_user(SCC_clocks[channel][CLK_TRxC], &cp->TRxC_base);
		put_user(SCC_PCLK, &cp->PCLK_base);
		copy_to_user(cp->baud_table, SCC_baud_table[channel] + 1,
			     sizeof(cp->baud_table));

		return( 0 );

	  case TIOCSATSCC:

		if (!suser()) return( -EPERM );

		error = verify_area(VERIFY_READ, (void *)arg,
				    sizeof(struct atari_SCCserial) );
		if (error)
			return error;

		get_user(rtxc, &cp->RTxC_base);
		get_user(trxc, &cp->TRxC_base);
		get_user(pclk, &cp->PCLK_base);

		if (pclk == SCC_BAUD_BASE_NONE)
			/* This is really not possible :-) */
			return( -EINVAL );

		/* Check the baud table for consistency */
		for( i = 0; i < sizeof(cp->baud_table)/sizeof(cp->baud_table[0]); ++i ) {

			get_user(clk, &cp->baud_table[i].clksrc);
			get_user(div, &cp->baud_table[i].divisor);

			switch( clk ) {
			  case CLK_RTxC:
				if (rtxc == SCC_BAUD_BASE_NONE)
					return( -EINVAL );
				if (((div & 1) && div != 1) ||
				    (div >= 4 && div/2-2 > 65535))
					return( -EINVAL );
				break;
			  case CLK_TRxC:
				if (trxc == SCC_BAUD_BASE_NONE)
					return( -EINVAL );
				if (div != 1 && div != 2 && div != 4)
					return( -EINVAL );
				break;
			  case CLK_PCLK:
				if (div < 4 || (div & 1) || div/2-2 > 65535)
					return( -EINVAL );
				break;
			  default:
				/* invalid valid clock source */
				return( -EINVAL );
			}
		}

		/* After all the checks, set the values */

		SCC_clocks[channel][CLK_RTxC] = rtxc;
		SCC_clocks[channel][CLK_TRxC] = trxc;
		SCC_PCLK = pclk;

		copy_from_user(bdtab_usr[channel] + 1, cp->baud_table,
			       sizeof(cp->baud_table));
		/* Now use the user supplied baud table */
		SCC_baud_table[channel] = bdtab_usr[channel];

		return( 0 );

	  case TIOCDATSCC:

		if (!suser()) return( -EPERM );
#ifdef ENABLE_ATARI_SCC
		if (!MACH_IS_ATARI)
			return 0; /* XXX */

		if (ATARIHW_PRESENT(TT_MFP)) {
			SCC_clocks[channel][CLK_RTxC] =
				(channel == CHANNEL_A) ?
					SCC_BAUD_BASE_PCLK4 :
					SCC_BAUD_BASE_TIMC;
			SCC_clocks[channel][CLK_TRxC] =
				(channel == CHANNEL_A) ?
					SCC_BAUD_BASE_NONE :
					SCC_BAUD_BASE_BCLK;
		}
		else {
			SCC_clocks[channel][CLK_RTxC] = SCC_BAUD_BASE_PCLK4;
			SCC_clocks[channel][CLK_TRxC] =
				(channel == CHANNEL_A) ?
					SCC_BAUD_BASE_NONE :
					SCC_BAUD_BASE_BCLK;
		}

		SCC_PCLK = SCC_BAUD_BASE_PCLK;
		SCC_baud_table[channel] =
			((ATARIHW_PRESENT(TT_MFP) && channel == 1) ?
			 bdtab_TTChB : bdtab_norm);
#endif
		return( 0 );

	}
	return( -ENOIOCTLCMD );
}




#ifdef MODULE
int init_module(void)
{
#ifdef ENABLE_ATARI_SCC
	if (MACH_IS_ATARI)
		return atari_SCC_init();
#endif
	return -ENODEV;
}

void cleanup_module(void)
{
	if (chb_line >= 0) {
		SCC_deinit_port( &rs_table[chb_line], CHANNEL_B );
		unregister_serial( chb_line );
	}

    /* ++Juergen Starek: use proper structure to deinitialize port
     *                   because atari_free_irq relies on the valid
     *                   `dev_id` parameter!
     *			  If we use only the cha232_line, unloading a 
     *			  module causes a damaged irq list!
     */
	/* We must deinit channel A only once! ++Andreas. */
	if (cha232_line >= 0)
		SCC_deinit_port(&rs_table[cha232_line], CHANNEL_A);
	else if (cha422_line >= 0)
		SCC_deinit_port(&rs_table[cha422_line], CHANNEL_A);

	if (cha232_line >= 0)
		unregister_serial( cha232_line );
	if (cha422_line >= 0)
		unregister_serial( cha422_line );
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
