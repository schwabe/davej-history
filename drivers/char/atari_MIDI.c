/*
 * drivers/char/atari_MIDI.c: Atari MIDI driver as serial port
 *
 * Copyright 1994 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 * Partially based on PC-Linux serial.c by Linus Torvalds and Theodore Ts'o
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Modified for midi by Martin Schaller
 */


#include <linux/module.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/m68kserial.h>
#include <linux/init.h>

#include <asm/traps.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atarikb.h>

#include "atari_MIDI.h"


/* Defines */

/* #define _DEBUG_MIDI_ */

#define DEFAULT_MIDI_LINE       5          /* ttyS5 */
#define MIDI_BAUD_BASE          500000     /* 31250 */

#define STOP_SHIFT 0
#define PAR_SHIFT  4
#define CS_SHIFT   8

/* Prototypes */
static void MIDI_init_port(struct m68k_async_struct* info, int type, int tt_flag);
static void MIDI_int(void);      /* called from keyboard int handler */
static void MIDI_init( struct m68k_async_struct * info );
static void MIDI_deinit( struct m68k_async_struct * info, int leave_dtr );
static void MIDI_enab_tx_int( struct m68k_async_struct * info, int enab_flag );
static int MIDI_check_custom_divisor(struct m68k_async_struct* info, int baud_base, int divisor);
static void MIDI_change_speed( struct m68k_async_struct * info );
static void MIDI_throttle( struct m68k_async_struct * info, int status );
static void MIDI_set_break( struct m68k_async_struct * info, int break_flag );
static unsigned int MIDI_get_modem_info( struct m68k_async_struct * info );
static void MIDI_get_serial_info( struct m68k_async_struct* info, struct serial_struct* retinfo);
static int MIDI_set_modem_info( struct m68k_async_struct* info, int new_dtr, int new_rts );
static void MIDI_stop_receive(struct m68k_async_struct * info);
static int MIDI_trans_empty(struct m68k_async_struct * info);


struct m68k_async_struct*            midi_info;
static unsigned char            mid_ctrl_shadow;   /* Bernd Harries, 960525 */
static unsigned char            mid_stat_shadow;   /* Bernd Harries, 961207 */


#ifdef _BHA_MIDI_CHAR_FMT
static int MIDI_char_fmt[8] = {
    (CS7 << CS_SHIFT) | (PARENB << PAR_SHIFT)            | (2 << STOP_SHIFT),
    (CS7 << CS_SHIFT) | ((PARENB | PARODD) << PAR_SHIFT) | (2 << STOP_SHIFT),
    (CS7 << CS_SHIFT) | (PARENB << PAR_SHIFT)            | (1 << STOP_SHIFT),
    (CS7 << CS_SHIFT) | ((PARENB | PARODD) << PAR_SHIFT) | (1 << STOP_SHIFT),
    (CS8 << CS_SHIFT) | (0 << PAR_SHIFT)                 | (2 << STOP_SHIFT),
    (CS8 << CS_SHIFT) | (0 << PAR_SHIFT)                 | (1 << STOP_SHIFT),
    (CS8 << CS_SHIFT) | (PARENB << PAR_SHIFT)            | (1 << STOP_SHIFT),
    (CS8 << CS_SHIFT) | ((PARENB | PARODD) << PAR_SHIFT) | (1 << STOP_SHIFT),
};
#endif

  /* index = P * 4 + L * 2 + S * 1 */
  /* S = (2 == Stopbits)           */
  /* L = (8 == Databits)           */
  /* P = Parity {Odd, Even, None}  */

static char ACIA_char_fmt[16] = {
    ACIA_D7O1S,
    ACIA_D7O2S,
    ACIA_D8O1S,
    -1, 
    ACIA_D7E1S,
    ACIA_D7E2S,
    ACIA_D8E1S,
    -1, 
    -1, 
    -1, 
    ACIA_D8N1S,
    ACIA_D8N2S,
    -2
};

/* Divisors for standard speeds & RSVE   Bernd Harries 961127 */
static int MIDI_baud_table[20] = {
    /* B0      */ 0,    /* invalid */
    /* B50     */ 0,
    /* B75     */ 0,
    /* B110    */ 0,
    /* B134    */ 0,
    /* B150    */ 0,
    /* B200    */ 0,
    /* B300    */ 0,
    /* B600    */ 0,
    /* B1200   */ 0,
    /* B1800   */ 0,
    /* B2400   */ 0,
    /* B4800   */ 0,    /* invalid */
    /* B9600   */ 64,   /* really 7812.5 bps */
    /* B19200  */ 0,    /* invalid */
    /* B38400  */ 16,   /* really 31250 bps */
    /* B57600  */ 0,    /* invalid */
    /* B115200 */ 0,    /* invalid */
    /* B230400 */ 0,    /* invalid */
    /* B460800 */ 1     /* really 500000 bps */
};

  /* The following 2 arrays must be congruent! */
static int ACIA_prescaler_modes[] = { 1, 16, 64 };
static char ACIA_baud_masks[] = { ACIA_DIV1, ACIA_DIV16, ACIA_DIV64 };

  /* 
   * SERIALSWITCH structures for MIDI port
   */

static SERIALSWITCH  MIDI_switch = {
    MIDI_init,
    MIDI_deinit,
    MIDI_enab_tx_int,
    MIDI_check_custom_divisor,
    MIDI_change_speed,
    MIDI_throttle,
    MIDI_set_break,
    MIDI_get_serial_info,
    MIDI_get_modem_info,
    MIDI_set_modem_info,
    NULL,                   /* MIDI_ioctl, */
    MIDI_stop_receive,
    MIDI_trans_empty,
    NULL                    /* MIDI_check_open */
};


__initfunc(int atari_MIDI_init( void ))
{
	extern char  m68k_debug_device[];
  	static  struct serial_struct  req;
	int  midi_line;
	int  nr = 0;
  
#ifdef _DEBUG_MIDI_
	printk(" atari_MIDI_init() \n");
#endif

	if (!strcmp( m68k_debug_device, "midi" ))
    		printk(KERN_NOTICE "MIDI serial port used as debug device\n" );
	else {
		req.line = DEFAULT_MIDI_LINE;
    		req.type = SER_MIDI;
    		req.port = (int) &acia.mid_ctrl;

    		midi_line = register_serial( &req );
    		if (midi_line >= 0) {
      			MIDI_init_port(&rs_table[midi_line], req.type, 0);
      			++nr;
    		}
    		else {
      			printk(KERN_WARNING "Cannot allocate ttyS%d for MIDI\n", req.line );
    		}
  	}
  	return( nr > 0 ? 0 : -ENODEV );
}


static void MIDI_init_port(struct m68k_async_struct * info, int type, int tt_flag)
{
  	midi_info = info;    /* modulglobal !!!!! */

  	info->sw = &MIDI_switch;
  	info->custom_divisor = 16;          /* 31250 Baud */
  	info->baud_base = MIDI_BAUD_BASE;
  	info->sw = &MIDI_switch;


  /* set ISRs, but don't enable interrupts yet (done in init());
   * all ints are choosen of type FAST, and they're really quite fast.
   * Furthermore, we have to account for the fact that these are three ints,
   * and one can interrupt another. So better protect them against one
   * another...
   */
  /*
	request_irq(IRQ_MIDI_SEREMPT, MIDI_tx_int, IRQ_TYPE_FAST, "MIDI TX", info);
	request_irq(IRQ_MIDI_RECFULL, MIDI_rx_int, IRQ_TYPE_FAST, "MIDI RX", info);
	request_irq(IRQ_MIDI_RECERR,
    			MIDI_rxerr_int,
    			IRQ_TYPE_FAST,
    			"MIDI RX error",
    			info);
   */
 
  /* Tx_err interrupt unused (it signals only that the Tx shift reg
   * is empty)
   */
  /* Leave RTS high for now if selected as a switch, so that it's still valid
   * as long as the port isn't opened.
   */
  	mid_ctrl_shadow = ACIA_DIV16 | ACIA_D8N1S |
			  ((atari_switches&ATARI_SWITCH_MIDI) ?
			   ACIA_RHTID : ACIA_RLTID);
  	acia.mid_ctrl = mid_ctrl_shadow;

  	atari_MIDI_interrupt_hook = MIDI_int;
}


static void MIDI_int(void)    /* called from keyboard int handler */
{
	static int    err;  
/*  	register int  stat; */
	register int  ch;

	mid_stat_shadow = acia.mid_ctrl;

	/* if == Rx Data Reg Full -> Interrupt */
	if (mid_stat_shadow & (ACIA_RDRF | ACIA_FE | ACIA_OVRN)) {
		ch = acia.mid_data;
		err = 0;
		if(mid_stat_shadow & ACIA_FE)      err = TTY_FRAME;
		if(mid_stat_shadow & ACIA_OVRN)    err = TTY_OVERRUN;
		rs_receive_char(midi_info, ch, err);
		/* printk("R"); */
	}

	if (acia.mid_ctrl & ACIA_TDRE) {   /* Tx Data Reg Empty  Transmit Interrupt */
		ch = rs_get_tx_char( midi_info );

		if (ch >= 0) {         /* 32 Bit */
			acia.mid_data = ch;
			/* printk("_%c", ch); */
		}

		if (ch == -1 || rs_no_more_tx( midi_info )) {
			/* disable tx interrupts    %x01xxxxx ==> %x00xxxxx */
			/* RTS Low, Tx IRQ Disabled */
			mid_ctrl_shadow &= ~ACIA_RLTIE;
			acia.mid_ctrl = mid_ctrl_shadow;
			/* printk("T"); */
		}
	}
}

static void MIDI_init( struct m68k_async_struct * info )
{  
#ifdef _DEBUG_MIDI_
	printk(" MIDI_init() \n");
#endif
	/* Baud = DIV16, 8N1, denable rx interrupts */
	mid_ctrl_shadow = ACIA_DIV16 | ACIA_D8N1S | ACIA_RIE;
	acia.mid_ctrl = mid_ctrl_shadow;

	MOD_INC_USE_COUNT;
}

static void MIDI_deinit( struct m68k_async_struct *info, int leave_dtr )
{
#ifdef _DEBUG_MIDI_
	printk(" MIDI_deinit() \n");
#endif

	/* seems like the status register changes on read */
#ifdef _MIDI_WAIT_FOR_TX_EMPTY_
	while(!(mid_stat_shadow & ACIA_TDRE)) {  /* wait for TX empty */
		;
		/* printk("m"); */
	}
#endif

	/* Baud = DIV16, 8N1, disable Rx and Tx interrupts */
	mid_ctrl_shadow =  ACIA_DIV16 | ACIA_D8N1S;
	acia.mid_ctrl = mid_ctrl_shadow;

	/* read Rx status and data to clean up */
	(void)acia.mid_ctrl;
	(void)acia.mid_data;

	MOD_DEC_USE_COUNT;
}


/*
 * ACIA Control Register can only be written! Read accesses Status Register!
 * Shadowing may be nescessary here like for SCC
 *
 *  Bernd Harries, 960525  Tel.: +49-421-804309
 *  harries@asrv01.atlas.de
 *  Bernd_Harries@hb2.maus.de
 */

static void MIDI_enab_tx_int( struct m68k_async_struct * info, int enab_flag )
{
	unsigned long  cpu_status;

	if (enab_flag) {
		register int  ch;

		save_flags(cpu_status);
		cli();
		/* RTS Low, Tx IRQ Enabled */
		mid_ctrl_shadow |= ACIA_RLTIE;
		acia.mid_ctrl = mid_ctrl_shadow;
		/* restarted the transmitter */
		/* 
		 * These extensions since 0.9.5 are only allowed, if the 
		 * Tx Data Register is Empty!
		 * In 1.2.13pl10 this did not work. So I added the if().
		 *
		 * Bernd Harries                         960530
		 *   harries@atlas.de
		 *   harries@asrv01.atlas.de
		 *   Bernd_Harries@hb2.maus.de
		 */
		if (acia.mid_ctrl & ACIA_TDRE) {  /* If last Char since disabling is gone */
			ch = rs_get_tx_char( midi_info );
			if (ch >= 0) {
				acia.mid_data = ch;
				/* printk("=%c", ch); */
			}

			if (ch == -1 || rs_no_more_tx( midi_info )) {
				/* disable tx interrupts */
				/* RTS Low, Tx IRQ Disabled */
				mid_ctrl_shadow &= ~ACIA_RLTIE;
				acia.mid_ctrl = mid_ctrl_shadow;

			}
		}
		restore_flags(cpu_status);
	} else {
		save_flags(cpu_status);
		cli();
		/* RTS Low, Tx IRQ Disabled */
		mid_ctrl_shadow &= ~ACIA_RLTIE;
		acia.mid_ctrl = mid_ctrl_shadow;
		restore_flags(cpu_status);
	}
}


static int MIDI_check_custom_divisor(struct m68k_async_struct* info, int baud_base, int divisor)
{  
#ifdef _DEBUG_MIDI_
	printk(" MIDI_check_custom_divisor() \n");
#endif

	if (baud_base != MIDI_BAUD_BASE)  return -1;

	/* divisor must be a multiple of 1, 16, 64  */

	switch (divisor) {
		case 1:
		case 16:
		case 64:  return(0);	
	}

	return(-1);
}


static void MIDI_change_speed( struct m68k_async_struct *info )
{
	unsigned long   cpu_status;
	unsigned int    baud, stopb, parity, aflags;
	unsigned int    div, cflag, chsize;
	int             timer_mode;
	int             index;

	unsigned char   mid_ctrl_new;
	unsigned char   baud_mask; 
	unsigned char   chsize_mask; 
	unsigned char   fmt_mask; 

#ifdef _DEBUG_MIDI_
	printk(" MIDI_change_speed() \n");
#endif

	div = 0;
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
			baud += 15;
	}
	if (baud == 15) {
		switch (aflags) {
			case ASYNC_SPD_HI:
				baud = 16;  /* 134 Baud, with RSVE =  57600 */
				break;
			case ASYNC_SPD_VHI:
				baud = 17;  /* 150 Baud, with RSVE = 115200 */
				break;
			case ASYNC_SPD_SHI:
				baud = 18;
				break;
			case ASYNC_SPD_WARP:
				baud = 19;
				break;
			case ASYNC_SPD_CUST:
				div = info->custom_divisor;
				break;
		}
	}

	if (!div) {
		/* Maximum MIDI speed is 500000  */
		if (baud > 19)  baud = 19;
		div = MIDI_baud_table[baud];
	}

	if (div) {
		/* turn on DTR */
		/* MIDI_DTRon(); */
	} else {
		/* speed == 0 -> drop DTR */
		/* MIDI_DTRoff(); */
		return;
	}

	mid_ctrl_new = 0;
	for (timer_mode = 2; timer_mode >= 0; timer_mode--)
		if (ACIA_prescaler_modes[timer_mode] == div)  
			break;

	baud_mask = ACIA_baud_masks[timer_mode];

	chsize_mask = 0;
	if (chsize == CS8)  chsize_mask = ACIA_D8N2S;

	index = 0;
	if (stopb)  index |= (1 << 0);
	if (chsize == CS8) index |= (1 << 1);
	if (parity == 0)
		index |= (2 << 2);
	else if (parity == PARENB)
		index |= (1 << 2);
	else {  /* if(parity == PARENB | PARODD) */
		/* index |= (0 << 2); */
	}

	fmt_mask = ACIA_char_fmt[index];

	/* Now we have all parameters and can go to set them: */
	save_flags(cpu_status);
	cli();

	/* disable Rx and Tx while changing parameters 
	 * stop timer D to set new timer value immediatly after re-enabling
	 */
	mid_ctrl_shadow &= ~ACIA_RESET;   /* MASK_OUT significant bits */
	mid_ctrl_shadow |= baud_mask;
	mid_ctrl_shadow &= ~ACIA_D8O1S;
	mid_ctrl_shadow |= fmt_mask;

	acia.mid_ctrl = mid_ctrl_shadow;    /* Write only */
	restore_flags(cpu_status);

#ifdef _DEBUG_MIDI_
	printk(" MIDI_change_speed() done. \n");
#endif
}


static void MIDI_throttle( struct m68k_async_struct * info, int status )
{
	if (status)  ;    /* MIDI_RTSoff(); */
	else         ;    /* MIDI_RTSon();  */
}


static void MIDI_set_break( struct m68k_async_struct * info, int break_flag )
{
}


static unsigned int MIDI_get_modem_info( struct m68k_async_struct *info )
{
	return( TIOCM_RTS | TIOCM_DTR | TIOCM_CAR | TIOCM_CTS | TIOCM_DSR );
}


static void MIDI_get_serial_info( struct m68k_async_struct* info, struct serial_struct* retinfo )
{
#ifdef _DEBUG_MIDI_
	printk(" MIDI_get_serial_info() \n");
#endif

	retinfo->baud_base = info->baud_base;
	retinfo->custom_divisor =  info->custom_divisor;
}


static int MIDI_set_modem_info( struct m68k_async_struct *info, int new_dtr, int new_rts )
{
	/* Is it right to return an error or should the attempt to change
 	 * DTR or RTS be silently ignored?
 	 */
	return( -EINVAL );
}


static void MIDI_stop_receive (struct m68k_async_struct *info)
{
	unsigned long  cpu_status;

	save_flags(cpu_status);
	cli();

	/* disable receive interrupts */
	mid_ctrl_shadow &= ~ACIA_RIE;
	acia.mid_ctrl = mid_ctrl_shadow;
	restore_flags(cpu_status);
}


static int MIDI_trans_empty (struct m68k_async_struct *info)
{
	return (acia.mid_ctrl & ACIA_TDRE) != 0;
}


#ifdef MODULE
int init_module(void)
{
	return( atari_MIDI_init() );
}

void cleanup_module(void)
{
	atari_MIDI_interrupt_hook = NULL;
	unregister_serial( midi_info->line );
}
#endif

