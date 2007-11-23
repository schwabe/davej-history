/*
 * drivers/char/atari_MFPser.c: Atari MFP serial ports implementation
 *
 * Copyright 1994 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 * Partially based on PC-Linux serial.c by Linus Torvalds and Theodore Ts'o
 *
 * Special thanks to Harun Scheutzow (developer of RSVE, RSFI, ST_ESCC and
 * author of hsmoda-package) for the code to detect RSVE/RSFI.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


/* This file implements the MFP serial ports. These come in two
 * flavors: with or without control lines (RTS, CTS, DTR, ...). They
 * are distinguished by having two different types, MFP_CTRL and
 * MFP_BARE, resp. Most of the low-level functions are the same for
 * both, but some differ.
 *
 * Note that some assumptions are made about where to access the
 * control lines. If the port type is MFP_CTRL, the input lines (CTS
 * and DCD) are assumed to be in the MFP GPIP register, bits 1 and 2.
 * The output lines (DTR and RTS) have to be in the Soundchip Port A,
 * bits 3 and 4. This is the standard ST/TT assigment. If Atari will
 * build a serial port in future, that uses other registers, you have
 * to rewrite this code. But if the port type is MFP_BARE, no such
 * assumptions are necessary. All registers needed are fixed by the
 * MFP hardware. The only parameter is the MFP base address. This is
 * used to implement Serial1 for the TT and the (not connected) MFP
 * port of the Falcon.
 *
 * Juergen: changes based on Harun Scheutzows code
 *   o added detection of RSVE, RSFI and possible PLL's
 *   o set info->hub6 to identify speeder-hardware
 *   o changed Tx output-level when transmitter is disabled
 *   o no need for CONFIG_ATARI_MFPSER_EXT
 *
 *
 *   o add delays for baudrate-setting to lock PLLs (RSFI, RSVE)
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/m68kserial.h>

#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/irq.h>

#include "atari_MFPser.h"

#define RSFI_DEBUG		/* undefine to get rid of "detect_MFP_speeder: clock[x] = y" */
#define RSFI_PLL_LOCK_DELAY	/* use delay to allow PLL settling */

/***************************** Prototypes *****************************/

static void MFPser_init_port( struct m68k_async_struct *info, int type,
			     int tt_flag );
#ifdef MODULE
static void MFPser_deinit_port( struct m68k_async_struct *info, int tt_flag );
#endif
static void MFPser_rx_int (int irq, void *data, struct pt_regs *fp);
static void MFPser_rxerr_int (int irq, void *data, struct pt_regs *fp);
static void MFPser_tx_int (int irq, void *data, struct pt_regs *fp);
static void MFPctrl_dcd_int (int irq, void *data, struct pt_regs *fp);
static void MFPctrl_cts_int (int irq, void *data, struct pt_regs *fp);
static void MFPctrl_ri_int (int irq, void *data, struct pt_regs *fp);
static void MFPser_init( struct m68k_async_struct *info );
static void MFPser_deinit( struct m68k_async_struct *info, int leave_dtr );
static void MFPser_enab_tx_int( struct m68k_async_struct *info, int enab_flag );
static int MFPser_check_custom_divisor (struct m68k_async_struct *info,
					int baud_base, int divisor);
static void MFPser_change_speed( struct m68k_async_struct *info );
static void MFPctrl_throttle( struct m68k_async_struct *info, int status );
static void MFPbare_throttle( struct m68k_async_struct *info, int status );
static void MFPser_set_break( struct m68k_async_struct *info, int break_flag );
static void MFPser_get_serial_info( struct m68k_async_struct *info, struct
                                    serial_struct *retinfo );
static unsigned int MFPctrl_get_modem_info( struct m68k_async_struct *info );
static unsigned int MFPbare_get_modem_info( struct m68k_async_struct *info );
static int MFPctrl_set_modem_info( struct m68k_async_struct *info, int new_dtr,
                                   int new_rts );
static int MFPbare_set_modem_info( struct m68k_async_struct *info, int new_dtr,
                                   int new_rts );
static void MFPser_stop_receive (struct m68k_async_struct *info);
static int MFPser_trans_empty (struct m68k_async_struct *info);

/************************* End of Prototypes **************************/


/* SERIALSWITCH structures for MFP ports
 * Most functions are common to MFP ports with or without control lines
 */

static SERIALSWITCH MFPctrl_switch = {
	MFPser_init, MFPser_deinit, MFPser_enab_tx_int,
	MFPser_check_custom_divisor, MFPser_change_speed,
	MFPctrl_throttle, MFPser_set_break,
	MFPser_get_serial_info, MFPctrl_get_modem_info,
	MFPctrl_set_modem_info, NULL, MFPser_stop_receive, MFPser_trans_empty,
	NULL
};

static SERIALSWITCH MFPbare_switch = {
	MFPser_init, MFPser_deinit, MFPser_enab_tx_int,
	MFPser_check_custom_divisor, MFPser_change_speed,
	MFPbare_throttle, MFPser_set_break,
	MFPser_get_serial_info, MFPbare_get_modem_info,
	MFPbare_set_modem_info, NULL, MFPser_stop_receive, MFPser_trans_empty,
	NULL
};

  /* MFP Timer Modes divided by 2 (this already done in the BAUD_BASE 
   * The real 68901 prescaler factors are twice these values! 
   * prescaler_factor[] = {4, 10, 16, 50, 64, 100, 200}
   */
int MFP_timer_modes[] = { 2, 5, 8, 25, 32, 50, 100 };
 
  /* RSVE or RSSPEED will only recognize the 3 frequencies for 
   * 110, 134, 150 Baud, if the prescaler is 4 and the counter value does 
   * the rest. The divisors 350 and 256 can be built in multiple ways. 
   * This driver tries to use the largest prescaler factor possible and 
   * uses small counter values. TOS uses a prescaler factor of 4
   * ==> MFP_timer_mode = 2 ==> Index 0. Then RSVE replaces the clock 
   * correctly. Since the absolute frequencies don't have to be so accurate 
   * but the the prescaler factor has to be 4 we have to make sure that the 
   * divisors for the 'real' 110, 134, 150 baud can be built with a 
   * prescaler factor > 4 and the divisors for the replaced 110, 134, 150
   * baud can only be built with a prescaler factor of 4 and a larger 
   * counter value. Accuracy is not so important here, since RSVE catches 
   * the values by ignoring the lower 3 bits.
   */

   /* Added support for RSFI
    * RSFI is a hardware-FIFO with the following features:
    *  o 2048bit Rx-FIFO
    *  o baudrates: 38400, 57600, 76800, 115200, 153600, 230400
    *    baudrate-selection and FIFO-enable is done by setting
    *    the effective baudrate to 50 .. 200. In contrast to the
    *    RSVE the clockselection is independend from the prescale-
    *    factor. Instead, the MFP must be in x1 clockmode.
    *  o the FIFO is only enabled when speeds are above 19k2
    *
    *  Relationship between MFP-baudrate and RSFI-baudrate
    *   MFP		mode	RSFI
    *	 50	x1	 76.8K *)
    *	 75	x1	153.6K *)
    *	110	x1	 38.4K
    *	134	x1	 57.6K
    *	150	x1	115.2K
    *	200	x1	230.4K
    *
    * *) Non-standard baudrates, not supported.
    *   
    * We keep the speeder-type in info->hub6. This flag is unused on
    * non-Intel architectures.
    *
    */

int MFP_baud_table[22] = { /* Divisors for standard speeds, RSVE & RSFI */
  /* B0      */ 0,
  /* B50     */ 768,
  /* B75     */ 512,
  /* B110    */ 350, /* really 109.71 bps */  /* MFP_mode 50 != 2 */
  /* B134    */ 288, /* really 133.33 bps */  /* MFP_mode  8 != 2 */
  /* B150    */ 256,                          /* MFP_mode 32 != 2 */
  /* B200    */ 192,
  /* B300    */ 128,
  /* B600    */ 64,
  /* B1200   */ 32,
  /* B1800   */ 21,
  /* B2400   */ 16,
  /* B4800   */ 8,
  /* B9600   */ 4,
  /* B19200  */ 2,
  /* B38400  */ 348,  /*  38.4K with RSVE/RSFI, prescaler 4 = MFP_mode 2 */
  /* B57600  */ 286,  /*  57.6K with RSVE/RSFI, prescaler 4 = MFP_mode 2 */
  /* B115200 */ 258,  /* 115.2K with RSVE/RSFI, prescaler 4 = MFP_mode 2 */
  /* --------------- the following values are ignored in RSVE-mode */
  /* B230400 */ 192,  /* 230.4K with RSFI */
  /* B460800 */ 0,    /* illegal */
};


#define DEFAULT_STMFP_LINE	0	/* ttyS0 */
#define DEFAULT_TTMFP_LINE	2	/* ttyS2 */

static int stmfp_line = -1, ttmfp_line = -1;

extern int atari_MFP_init_done;


int atari_MFPser_init( void )

{	struct serial_struct req;
	int nr = 0;
	extern char m68k_debug_device[];

	if (!MACH_IS_ATARI)
		return( -ENODEV );

	if (ATARIHW_PRESENT(ST_MFP)) {
		if (!strcmp( m68k_debug_device, "ser1" ))
			printk(KERN_NOTICE "ST-MFP serial port used as debug device\n" );
		else {
			req.line = DEFAULT_STMFP_LINE;
			req.type = SER_MFP_CTRL;
			req.port = (int)&mfp;
			if ((stmfp_line = register_serial( &req )) >= 0) {
				MFPser_init_port( &rs_table[stmfp_line], req.type, 0 );
				++nr;
			}
			else
				printk(KERN_WARNING "Cannot allocate ttyS%d for ST-MFP\n", req.line );
		}
	}

	if (ATARIHW_PRESENT(TT_MFP)) {
		req.line = DEFAULT_TTMFP_LINE;
		req.type = SER_MFP_BARE;
		req.port = (int)&tt_mfp;
		if ((ttmfp_line = register_serial( &req )) >= 0) {
			MFPser_init_port( &rs_table[ttmfp_line], req.type, 1 );
			++nr;
		}
		else
			printk(KERN_WARNING "Cannot allocate ttyS%d for TT-MFP\n", req.line );
	}

	return( nr > 0 ? 0 : -ENODEV );
}

static void __inline__ set_timer_D(volatile struct MFP *thismfp,
								   int baud, int prescale) {
/* set timer d to given value, prescale 4
 * allow PLL-settling (3 bit-times)
 */

    int count;

	thismfp->tim_ct_cd &= 0xf8;		/* disable timer D */
	thismfp->tim_dt_d = baud;		/* preset baudrate */
	thismfp->tim_ct_cd |= prescale;	/* enable timer D, prescale N */

	for( count = 6; count; --count ) {
		thismfp->int_pn_b = ~0x10;
		while( !(thismfp->int_pn_b & 0x10) )
			;
	}
}

static int detect_MFP_speeder(volatile struct MFP *currMFP) {
/* try to autodetect RSVE, RSFI or similiar RS232 speeders
 *
 *  (c) Harun Scheutzow 
 *	    developer of RSVE, RSFI, ST_ESCC and author of hsmoda-package
 *
 *  integrated by Juergen Orschiedt
 *
 * noise-free detection of Tx/Rx baudrate
 *  - set MFP to loopback, syncmode, 8bpc, syncchar=0xff
 *  - enable transmitter and measure time between syncdetect
 * depending on the relationship between measured time and
 * timer-d setting we can tell which (if any) speeder we have.
 *
 * returncodes:
 *	0	something wrong (1200 too slow)
 *	1	no speeder detected
 *	2	RSVE detected
 *	3	RSFI detected
 *	4	PLL or fixed Baudrate (Hardware-hack)
 */

	int count, speeder;
	unsigned int flags;
	int imra, imrb;

	/* prepare IRQ registers for measurement */

	save_flags(flags);
	cli();

	imra = currMFP->int_mk_a;
	imrb = currMFP->int_mk_b;

	currMFP->int_mk_a = imra & 0xe1;	/* mask off all Rx/Tx ints */
	currMFP->int_mk_b = imrb & 0xef;	/* disable timer d int in IMRB */
	currMFP->int_en_b |= 0x10;		/* enable in IERB (to see pending ints) */
	restore_flags(flags);

	(void)currMFP->par_dt_reg;		/* consume some cycles */
	(void)currMFP->par_dt_reg;

	/* initialize MFP */
	currMFP->rcv_stat = 0x00;		/* disable Rx */
	currMFP->trn_stat = TSR_SOMODE_HIGH;	/* disable Tx, output-level high */
	currMFP->sync_char= 0xff;		/* syncchar = 0xff */
	currMFP->usart_ctr= (UCR_PARITY_OFF | UCR_SYNC_MODE | UCR_CHSIZE_8);
	currMFP->trn_stat = (TSR_TX_ENAB | TSR_SOMODE_LOOP); 

	/* look at 1200 baud setting (== effective 19200 in syncmode) */
	set_timer_D(currMFP, 0x10, 0x01);
	save_flags(flags);
	cli();

	/* check for fixed speed / bad speed */
	currMFP->rcv_stat = RSR_RX_ENAB;
	count = -1;
	do {
	  continue_outer:
		++count;
		currMFP->int_pn_b = ~0x10;
		do {
			if (currMFP->int_pn_b & 0x10)
				goto continue_outer;
		} while( !(currMFP->rcv_stat & RSR_SYNC_SEARCH) && count <= 22 );
	} while(0);
	restore_flags(flags);

	/* for RSxx or standard MFP we have 8 bittimes (count=16) */
#ifdef	RSFI_DEBUG
	printk(KERN_INFO "    detect_MFP_speeder: count[1200]=%d\n", count);
#endif

	if (count < 10)	
		speeder = MFP_WITH_PLL;		/* less than 5 bittimes: primitive speeder */
	else
	if (count >22)
		speeder = MFP_WITH_WEIRED_CLOCK;	/* something wrong - too slow! */
	else {
		/* 1200 baud is working, we neither have fixed clock nor simple PLL */ 
		set_timer_D(currMFP, 0xaf, 0x01);
		save_flags(flags);
		cli();

		/* check for RSxx or Standard MFP with 110 baud
		 * timer D toggles each 290us
		 *	      bps	sync char count
		 * RSVE:    614400	 22.33		 13uS
		 * RSFI:     38400	  1.39		208us
		 * Standard:  1720	  0.06
		 * 
		 */
		currMFP->int_pn_b = ~0x10;
		/* syncronize to timer D */
		while( !(currMFP->int_pn_b & 0x10) )
			;
		currMFP->int_pn_b = ~0x10;
		count = -1;
		do {
		  continue_outer2:
			currMFP->rcv_stat = 0;				/* disable Rx */
			++count;
			(void)currMFP->par_dt_reg;			/* delay */
			currMFP->rcv_stat = RSR_RX_ENAB;	/* enable Rx */
			nop();
			do {
				/* increment counter if sync char detected */
				if (currMFP->rcv_stat & RSR_SYNC_SEARCH)
					goto continue_outer2;
			} while( !(currMFP->int_pn_b & 0x10) );
		} while(0);

#ifdef RSFI_DEBUG
		printk(KERN_INFO "    detect_MFP_speeder: count[110]=%d\n", count);
#endif

		if (count < 1) speeder = MFP_STANDARD;	/* no speeder detected */
		else
			if (count > 4) speeder = MFP_WITH_RSVE;
		else
			speeder = MFP_WITH_RSFI;
	}

	restore_flags(flags);
	currMFP->rcv_stat = 0x00;		/* disable Rx */
	currMFP->trn_stat = TSR_SOMODE_HIGH;	/* disable Tx, output-level high */
	currMFP->usart_ctr= (UCR_PARITY_OFF | UCR_ASYNC_2 | UCR_CHSIZE_8);
	currMFP->int_mk_a = imra;
	currMFP->int_mk_b = imrb;
	currMFP->int_en_b &= 0xef;
	currMFP->int_pn_a = 0xe1;		/* mask off pending Rx/Tx */
	currMFP->int_pn_b = 0xef;		/* mask off pending Timer D */
	return speeder;

}

static void MFPser_init_port( struct m68k_async_struct *info, int type, int tt_flag)
{
	INIT_currMFP(info);
	int speeder;
	static char *speeder_name[]= {"", "", "PLL or fixed clock", "RSVE", "RSFI" };

	/* look for possible speeders */
	info->hub6 = speeder = detect_MFP_speeder((struct MFP *)currMFP);
	if (speeder > MFP_STANDARD)
	    printk(KERN_INFO "ttyS%d: Detected %s extension\n",
		   info->line, speeder_name[speeder]);

	
	/* set ISRs, but don't enable interrupts yet (done in init());
	 * all ints are choosen of type FAST, and they're really quite fast.
	 * Furthermore, we have to account for the fact that these are three ints,
	 * and one can interrupt another. So better protect them against one
	 * another...
	 */
	request_irq(tt_flag ? IRQ_TT_MFP_SEREMPT : IRQ_MFP_SEREMPT,
	            MFPser_tx_int, IRQ_TYPE_FAST,
	            tt_flag ? "TT-MFP TX" : "ST-MFP TX", info);
	request_irq(tt_flag ? IRQ_TT_MFP_RECFULL : IRQ_MFP_RECFULL,
	            MFPser_rx_int, IRQ_TYPE_FAST,
	            tt_flag ? "TT-MFP RX" : "ST-MFP RX", info);
	request_irq(tt_flag ? IRQ_TT_MFP_RECERR : IRQ_MFP_RECERR,
	            MFPser_rxerr_int, IRQ_TYPE_FAST,
	            tt_flag ? "TT-MFP RX error" : "ST-MFP RX error", info);
	/* Tx_err interrupt unused (it signals only that the Tx shift reg
	 * is empty)
	 */

	if (type == SER_MFP_CTRL && !tt_flag) {
		/* The DCD, CTS and RI ints are slow ints, because I
		   see no races with the other ints */
		request_irq(IRQ_MFP_DCD, MFPctrl_dcd_int, IRQ_TYPE_SLOW,
		            "ST-MFP DCD", info);
		request_irq(IRQ_MFP_CTS, MFPctrl_cts_int, IRQ_TYPE_SLOW,
		            "ST-MFP CTS", info);
		request_irq(IRQ_MFP_RI, MFPctrl_ri_int, IRQ_TYPE_SLOW,
		            "ST-MFP RI", info);
		/* clear RTS and DTR */
		if (!atari_MFP_init_done)
			/* clear RTS and DTR */
			GIACCESS( GI_RTS | GI_DTR );
	}

	info->sw = (type == SER_MFP_CTRL ? &MFPctrl_switch : &MFPbare_switch);

  	info->custom_divisor = 4;          /* 9600 Baud */
  	info->baud_base = MFP_BAUD_BASE;

	if (tt_flag || !atari_MFP_init_done) {
		currMFP->rcv_stat  = 0;	/* disable Rx */
		currMFP->trn_stat  = TSR_SOMODE_HIGH;	/* disable Tx */
	}
}


#ifdef MODULE
static void MFPser_deinit_port( struct m68k_async_struct *info, int tt_flag )
{
	free_irq(tt_flag ? IRQ_TT_MFP_SEREMPT : IRQ_MFP_SEREMPT, info);
	free_irq(tt_flag ? IRQ_TT_MFP_RECFULL : IRQ_MFP_RECFULL, info);
	free_irq(tt_flag ? IRQ_TT_MFP_RECERR : IRQ_MFP_RECERR, info);
	if (info->type == SER_MFP_CTRL && !tt_flag) {
		free_irq(IRQ_MFP_DCD, info );
		free_irq(IRQ_MFP_CTS, info );
		free_irq(IRQ_MFP_RI, info);
	}
}
#endif


static void MFPser_rx_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	int		ch, stat, err;
	INIT_currMFP(info);

	stat = currMFP->rcv_stat;
	ch   = currMFP->usart_dta;
	/* Frame Errors don't cause a RxErr IRQ! */
	err  = (stat & RSR_FRAME_ERR) ? TTY_FRAME : 0;

	rs_receive_char (info, ch, err);
}


static void MFPser_rxerr_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	int		ch, stat, err;
	INIT_currMFP(info);

	stat = currMFP->rcv_stat;
	ch   = currMFP->usart_dta; /* most probably junk data */

	if (stat & RSR_PARITY_ERR)
		err = TTY_PARITY;
	else if (stat & RSR_OVERRUN_ERR)
		err = TTY_OVERRUN;
	else if (stat & RSR_BREAK_DETECT)
		err = TTY_BREAK;
	else if (stat & RSR_FRAME_ERR)	/* should not be needed */
		err = TTY_FRAME;
	else
		err = 0;

	rs_receive_char (info, ch, err);
}


static void MFPser_tx_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	int ch;
	INIT_currMFP(info);

	if (currMFP->trn_stat & TSR_BUF_EMPTY) {
		if ((ch = rs_get_tx_char( info )) >= 0)
			currMFP->usart_dta = ch;
		if (ch == -1 || rs_no_more_tx( info ))
			/* disable tx interrupts */
			currMFP->int_en_a &= ~0x04;
	}
}


static void MFPctrl_dcd_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	INIT_currMFP(info);

	/* Toggle active edge to get next change of DCD! */
	currMFP->active_edge ^= GPIP_DCD;

	rs_dcd_changed( info, !(currMFP->par_dt_reg & GPIP_DCD) );
}


static void MFPctrl_cts_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	INIT_currMFP(info);

	/* Toggle active edge to get next change of CTS! */
	currMFP->active_edge ^= GPIP_CTS;

	rs_check_cts( info, !(currMFP->par_dt_reg & GPIP_CTS) );
}


static void MFPctrl_ri_int(int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;
	/* update input line counter */
	info->icount.rng++;
	wake_up_interruptible(&info->delta_msr_wait);
}


static void MFPser_init( struct m68k_async_struct *info )
{
	INIT_currMFP(info);

	/* base value for UCR */
	if (info->type != SER_MFP_CTRL || !atari_MFP_init_done)
		currMFP->usart_ctr = (UCR_PARITY_OFF | UCR_ASYNC_1 |
							  UCR_CHSIZE_8 | UCR_PREDIV);

	/* enable Rx and clear any error conditions */
	currMFP->rcv_stat = RSR_RX_ENAB;

	/* enable Tx */
	currMFP->trn_stat = (TSR_TX_ENAB | TSR_SOMODE_HIGH);

	/* enable Rx, RxErr and Tx interrupts */
	currMFP->int_en_a |= 0x1c;
	currMFP->int_mk_a |= 0x1c;

	if (info->type == SER_MFP_CTRL) {

		int status;

		/* set RTS and DTR (low-active!) */
		GIACCESS( ~(GI_RTS | GI_DTR) );

		/* Set active edge of CTS and DCD signals depending on their
		 * current state.
		 * If the line status changes between reading the status and
		 * enabling the interrupt, this won't work :-( How could it be
		 * done better??
		 * ++andreas: do it better by looping until stable
		 */
		do {
		    status = currMFP->par_dt_reg & GPIP_CTS;
		    if (status)
			currMFP->active_edge &= ~GPIP_CTS;
		    else
			currMFP->active_edge |= GPIP_CTS;
		} while ((currMFP->par_dt_reg & GPIP_CTS) != status);

		do {
		    status = currMFP->par_dt_reg & GPIP_DCD;
		    if (status)
			currMFP->active_edge &= ~GPIP_DCD;
		    else
			currMFP->active_edge |= GPIP_DCD;
		} while ((currMFP->par_dt_reg & GPIP_DCD) != status);

		/* enable CTS and DCD interrupts */
		currMFP->int_en_b |= 0x06;
		currMFP->int_mk_b |= 0x06;
	}
	MOD_INC_USE_COUNT;
}


static void MFPser_deinit( struct m68k_async_struct *info, int leave_dtr )
{
	INIT_currMFP(info);
	
	/* disable Rx, RxErr and Tx interrupts */
	currMFP->int_en_a &= ~0x1c;

	if (info->type == SER_MFP_CTRL) {
		/* disable CTS and DCD interrupts */
		currMFP->int_en_b &= ~0x06;
	}

	/* disable Rx and Tx */
	currMFP->rcv_stat = 0;
	currMFP->trn_stat = TSR_SOMODE_HIGH;

	/* wait for last byte to be completely shifted out */
	while( !(currMFP->trn_stat & TSR_LAST_BYTE_SENT) )
		;

	if (info->type == SER_MFP_CTRL) {
		/* drop RTS and DTR if required */
		MFPser_RTSoff();
		if (!leave_dtr)
			MFPser_DTRoff();
	}

	/* read Rx status and data to clean up */
	(void)currMFP->rcv_stat;
	(void)currMFP->usart_dta;
	MOD_DEC_USE_COUNT;
}


static void MFPser_enab_tx_int( struct m68k_async_struct *info, int enab_flag )
{
	INIT_currMFP(info);

	if (enab_flag) {
		unsigned long flags;
		currMFP->int_en_a |= 0x04;
		save_flags(flags);
		cli();
		MFPser_tx_int (0, info, 0);
		restore_flags(flags);
	}
	else
		currMFP->int_en_a &= ~0x04;
}


static int MFPser_check_custom_divisor (struct m68k_async_struct *info,
					int baud_base, int divisor)
{
	int		i;

	if (baud_base != MFP_BAUD_BASE)
		return -1;

	/* divisor must be a multiple of 2 or 5 (because of timer modes) */
	if (divisor == 0 || ((divisor & 1) && (divisor % 5) != 0)) return( -1 );

	/* Determine what timer mode would be selected and look if the
	 * timer value would be greater than 256
	 */
	for( i = sizeof(MFP_timer_modes)/sizeof(*MFP_timer_modes)-1; i >= 0; --i )
		if (divisor % MFP_timer_modes[i] == 0) break;
	if (i < 0) return( -1 ); /* no suitable timer mode found */

	return (divisor / MFP_timer_modes[i] > 256);
}


static void MFPser_change_speed( struct m68k_async_struct *info )
{
	unsigned	cflag, baud, chsize, stopb, parity, aflags;
	unsigned	div = 0, timer_val;
	int			timer_mode;
	unsigned long ipl;
	INIT_currMFP(info);

	if (!info->tty || !info->tty->termios) return;

	cflag  = info->tty->termios->c_cflag;
	baud   = cflag & CBAUD;
	chsize = cflag & CSIZE;
	stopb  = cflag & CSTOPB;
	parity = cflag & (PARENB | PARODD);
	aflags = info->flags & ASYNC_SPD_MASK;

	if (cflag & CRTSCTS)
		info->flags |= ASYNC_CTS_FLOW;
	else
		info->flags &= ~ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else
		info->flags |= ASYNC_CHECK_CD;

	if (baud & CBAUDEX) {
		baud &= ~CBAUDEX;
		if (baud < 1 || baud > 4)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			if (info->hub6 > MFP_WITH_PLL)	/* speeder detected? */
			    baud += 15;
	}
	if ((info->hub6 > MFP_WITH_PLL) && (baud == 15)) {
		/* only for speeders... */
		switch (aflags) {
		case ASYNC_SPD_HI:
			baud += 1;  /* 134 Baud, with RSVE =  57600 */
			break;
		case ASYNC_SPD_VHI:
			baud += 2;  /* 150 Baud, with RSVE = 115200 */
			break;
		case ASYNC_SPD_SHI:
			baud += 3;	/* with RSFI: 230400 Baud */
			break;
		case ASYNC_SPD_WARP:
			baud += 4;
			break;
		case ASYNC_SPD_CUST:
			div = info->custom_divisor;
			break;
		}
	}
	if (!div) {
		/* max. tableentries depending on speeder type */
		static int maxbaud[] = { 14, 14, 14, 17, 18};
		if (baud > maxbaud[info->hub6])
			baud = maxbaud[info->hub6];
		div = MFP_baud_table[baud];
	}

	if (div) {
		/* turn on DTR */
		MFPser_DTRon ();
	} else {
		/* speed == 0 -> drop DTR */
		MFPser_DTRoff();
		return;
	}

	/* compute timer value and timer mode (garuateed to succeed, because
	 * the divisor was checked before by check_custom_divisor(), if it
	 * is used-supplied)
	 */
	for( timer_mode = sizeof(MFP_timer_modes)/sizeof(*MFP_timer_modes)-1;
		 timer_mode >= 0; --timer_mode )
		if (div % MFP_timer_modes[timer_mode] == 0) break;
	timer_val = div / MFP_timer_modes[timer_mode];

	save_flags (ipl);
	cli();
	/* disable Rx and Tx while changing parameters */
	currMFP->rcv_stat = 0;
	currMFP->trn_stat = TSR_SOMODE_HIGH;

#ifdef RSFI_PLL_LOCK_DELAY
	currMFP->int_en_b |= 0x10;
	set_timer_D(currMFP, timer_val, timer_mode+1);
	currMFP->int_en_b &= 0xef;
#else
	/* stop timer D to set new timer value immediatly after re-enabling */
	currMFP->tim_ct_cd &= ~0x07;
	currMFP->tim_dt_d = timer_val;
	currMFP->tim_ct_cd |= (timer_mode+1);
#endif
	{
	    unsigned shadow_ctr;

	    shadow_ctr = ((parity & PARENB) ?
			 ((parity & PARODD) ? UCR_PARITY_ODD : UCR_PARITY_EVEN) :
			 UCR_PARITY_OFF ) |
			    ( chsize == CS5 ? UCR_CHSIZE_5 :
			      chsize == CS6 ? UCR_CHSIZE_6 :
			      chsize == CS7 ? UCR_CHSIZE_7 : UCR_CHSIZE_8 ) |
			( stopb ? UCR_ASYNC_2 : UCR_ASYNC_1 );

	    if ((baud < 15) || (info->hub6 != MFP_WITH_RSFI))
		shadow_ctr |= UCR_PREDIV;
	    currMFP->usart_ctr = shadow_ctr;
	}

	/* re-enable Rx and Tx */
	currMFP->rcv_stat = RSR_RX_ENAB;
	currMFP->trn_stat = (TSR_TX_ENAB | TSR_SOMODE_HIGH);
	restore_flags (ipl);
}

static void MFPctrl_throttle( struct m68k_async_struct *info, int status )
{
	if (status)
		MFPser_RTSoff();
	else
		MFPser_RTSon();
}


static void MFPbare_throttle( struct m68k_async_struct *info, int status )
{
	/* no-op */
}


static void MFPser_set_break( struct m68k_async_struct *info, int break_flag )
{
	INIT_currMFP(info);
	
	if (break_flag)
		currMFP->trn_stat |= TSR_SEND_BREAK;
	else
		currMFP->trn_stat &= ~TSR_SEND_BREAK;
}


static void MFPser_get_serial_info( struct m68k_async_struct *info,
				   struct serial_struct *retinfo )
{
	retinfo->baud_base = info->baud_base;
	retinfo->custom_divisor = info->custom_divisor;
}


static unsigned int MFPctrl_get_modem_info( struct m68k_async_struct *info )
{
	unsigned	gpip, gi;
	unsigned int ri;
	unsigned long ipl;
	INIT_currMFP(info);

	save_flags (ipl);
	cli();
	gpip = currMFP->par_dt_reg;
	gi   = GIACCESS( 0 );
	restore_flags (ipl);

	/* DSR is not connected on the Atari, assume it to be set;
	 * RI is tested by the RI bitpos field of info, because the RI is
	 * signalled at different ports on TT and Falcon
	 * ++andreas: the signals are inverted!
	 */
	/* If there is a SCC but no TT_MFP then RI on the ST_MFP is
	   used for SCC channel b */
	if (ATARIHW_PRESENT (SCC) && !ATARIHW_PRESENT (TT_MFP))
		ri = 0;
	else if (currMFP == &mfp)
		ri = gpip & GPIP_RI ? 0 : TIOCM_RNG;
	else
		ri = 0;
	return (((gi   & GI_RTS  ) ? 0 : TIOCM_RTS) |
		((gi   & GI_DTR  ) ? 0 : TIOCM_DTR) |
		((gpip & GPIP_DCD) ? 0 : TIOCM_CAR) |
		((gpip & GPIP_CTS) ? 0 : TIOCM_CTS) |
		TIOCM_DSR | ri);
}


static unsigned int MFPbare_get_modem_info( struct m68k_async_struct *info )
{
	return( TIOCM_RTS | TIOCM_DTR | TIOCM_CAR | TIOCM_CTS | TIOCM_DSR );
}


static int MFPctrl_set_modem_info( struct m68k_async_struct *info,
				  int new_dtr, int new_rts )
{
	if (new_dtr == 0)
		MFPser_DTRoff();
	else if (new_dtr == 1)
		MFPser_DTRon();

	if (new_rts == 0)
		MFPser_RTSoff();
	else if (new_rts == 1)
		MFPser_RTSon();

	return( 0 );
}


static int MFPbare_set_modem_info( struct m68k_async_struct *info,
				  int new_dtr, int new_rts )
{
	/* no-op */

	/* Is it right to return an error or should the attempt to change
	 * DTR or RTS be silently ignored?
	 */
	return( -EINVAL );
}

static void MFPser_stop_receive (struct m68k_async_struct *info)
{
	INIT_currMFP(info);

	/* disable rx and rxerr interrupt */
	currMFP->int_en_a &= ~0x18;

	/* disable receiver */
	currMFP->rcv_stat = 0;
	/* disable transmitter */
	currMFP->trn_stat = TSR_SOMODE_HIGH;
}

static int MFPser_trans_empty (struct m68k_async_struct *info)
{
	INIT_currMFP(info);
	return (currMFP->trn_stat & TSR_LAST_BYTE_SENT) != 0;
}

#ifdef MODULE
int init_module(void)
{
	return( atari_MFPser_init() );
}

void cleanup_module(void)
{
	if (stmfp_line >= 0) {
		MFPser_deinit_port( &rs_table[stmfp_line], 0 );
		unregister_serial( stmfp_line );
	}
	if (ttmfp_line >= 0) {
		MFPser_deinit_port( &rs_table[ttmfp_line], 1 );
		unregister_serial( ttmfp_line );
	}
}
#endif
