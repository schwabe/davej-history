/*
 * drivers/char/amiga_ser.c: Amiga built-in serial port driver.
 *
 * Copyright 1994 Roman Hodek, 1994 Hamish Macdonald
 *       Based on the Atari MFP driver by Roman Hodek
 *
 * Modifications by Matthias Welwarsky <Matthias.Welwarsky@ppp.th-darmstadt.de>
 * - fixed reentrancy problem in ser_tx_int()
 * 
 * 20/02/99 - Jesper Skov: Added mb() calls and KGDB support.
 * 27/04/96 - Jes Soerensen: Upgraded for Linux-1.3.x.
 * 02/09/96 - Jes Soerensen: Moved the {request,free}_irq call for
 *            AMIGA_VERTB interrupts into the init/deinit funtions as
 *            there is no reason to service the ser_vbl_int when the
 *            serial port is not in use.
 * 30/04/96 - Geert Uytterhoeven: Added incoming BREAK detection
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


/*
 * This file implements the driver for the Amiga built-in serial port.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/interrupt.h>
#include <linux/m68kserial.h>
#include <linux/linkage.h>

#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/atomic.h>

/* some serial hardware definitions */
#define SDR_OVRUN   (1<<15)
#define SDR_RBF     (1<<14)
#define SDR_TBE     (1<<13)
#define SDR_TSRE    (1<<12)

#define AC_SETCLR   (1<<15)
#define AC_UARTBRK  (1<<11)

#define SER_DTR     (1<<7)
#define SER_RTS     (1<<6)
#define SER_DCD     (1<<5)
#define SER_CTS     (1<<4)
#define SER_DSR     (1<<3)

/***************************** Prototypes *****************************/

static void ser_rx_int( int irq, void *data, struct pt_regs *fp);
static void ser_tx_int( int irq, void *data, struct pt_regs *fp);
static void ser_vbl_int( int irq, void *data, struct pt_regs *fp);
static void ser_init( struct m68k_async_struct *info );
static void ser_deinit( struct m68k_async_struct *info, int leave_dtr );
static void ser_enab_tx_int( struct m68k_async_struct *info, int enab_flag );
static int  ser_check_custom_divisor (struct m68k_async_struct *info,
				      int baud_base, int divisor);
static void ser_change_speed( struct m68k_async_struct *info );
static void ser_throttle( struct m68k_async_struct *info, int status );
static void ser_set_break( struct m68k_async_struct *info, int break_flag );
static void ser_get_serial_info( struct m68k_async_struct *info,
				struct serial_struct *retinfo );
static unsigned int ser_get_modem_info( struct m68k_async_struct *info );
static int ser_set_modem_info( struct m68k_async_struct *info, int new_dtr,
			      int new_rts );
static void ser_stop_receive( struct m68k_async_struct *info );
static int ser_trans_empty( struct m68k_async_struct *info );

/************************* End of Prototypes **************************/



/* SERIALSWITCH structure for the Amiga serial port
 */

static SERIALSWITCH amiga_ser_switch = {
    ser_init, ser_deinit, ser_enab_tx_int,
    ser_check_custom_divisor, ser_change_speed,
    ser_throttle, ser_set_break,
    ser_get_serial_info, ser_get_modem_info,
    ser_set_modem_info, NULL, ser_stop_receive, ser_trans_empty, NULL
};

/* Standard speeds table */
static int baud_table[19] = {
    /* B0     */ 0,
    /* B50    */ 50,
    /* B75    */ 75,
    /* B110   */ 110,
    /* B134   */ 134,
    /* B150   */ 150,
    /* B200   */ 200,
    /* B300   */ 300,
    /* B600   */ 600,
    /* B1200  */ 1200,
    /* B1800  */ 1800,
    /* B2400  */ 2400,
    /* B4800  */ 4800,
    /* B9600  */ 9600,
    /* B19200 */ 19200,
    /* B38400 */ 38400,
    /* B57600 */ 57600,
    /* B115200*/ 115200,
    /* B230400*/ 230400
};
	
static __inline__ void ser_DTRoff(void)
{
    ciab.pra |= SER_DTR; /* active low */
}

static __inline__ void ser_DTRon(void)
{
    ciab.pra &= ~SER_DTR; /* active low */
}

static __inline__ void ser_RTSoff(void)
{
    ciab.pra |= SER_RTS; /* active low */
}

static __inline__ void ser_RTSon(void)
{
    ciab.pra &= ~SER_RTS; /* active low */
}

static int line; /* the serial line assigned by register_serial() */
/* use this value in isr's to get rid of the data pointer in the future */
/* This variable holds the current state of the DCD/CTS bits */
static unsigned char current_ctl_bits;

static __inline__ void check_modem_status(struct m68k_async_struct *info)
{
	unsigned char bits;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

	bits = ciab.pra & (SER_DCD | SER_CTS);

	if (bits ^ current_ctl_bits) {
		if ((bits ^ current_ctl_bits) & SER_DCD) {
			rs_dcd_changed(info, !(bits & SER_DCD));
		}

		if ((bits ^ current_ctl_bits) & SER_CTS)
			rs_check_cts(info, !(bits & SER_CTS));
	}
	current_ctl_bits = bits;
}

static struct m68k_async_struct *amiga_info;

int amiga_serinit( void )
{	
	unsigned long flags;
	struct serial_struct req;
	struct m68k_async_struct *info;
	
	if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(AMI_SERIAL))
		return -ENODEV;

	req.line = -1; /* first free ttyS? device */
	req.type = SER_AMIGA;
	req.port = (int) &custom.serdatr; /* dummy value */
	if ((line = m68k_register_serial( &req )) < 0) {
		printk( "Cannot register built-in serial port: no free device\n" );
		return -EBUSY;
	}
    info = &rs_table[line];
	
    save_flags (flags);
    cli();

    /* set ISRs, and then disable the rx interrupts */
    request_irq(IRQ_AMIGA_TBE, ser_tx_int, 0, "serial TX", info);
    request_irq(IRQ_AMIGA_RBF, ser_rx_int, 0, "serial RX", info);

    amiga_info = info;

    /* turn off Rx and Tx interrupts */
    custom.intena = IF_RBF | IF_TBE;

    /* clear any pending interrupt */
    custom.intreq = IF_RBF | IF_TBE;
    restore_flags (flags);

    /*
     * set the appropriate directions for the modem control flags,
     * and clear RTS and DTR
     */
    ciab.ddra |= (SER_DTR | SER_RTS);   /* outputs */
    ciab.ddra &= ~(SER_DCD | SER_CTS | SER_DSR);  /* inputs */
    
    info->sw   = &amiga_ser_switch;

#ifdef CONFIG_KGDB
    /* turn Rx interrupts on for GDB */
    custom.intena = IF_SETCLR | IF_RBF;
    ser_RTSon();
#endif

    return 0;
}

static void ser_rx_int(int irq, void *data, struct pt_regs *fp)
{
#ifdef CONFIG_KGDB
      extern void breakpoint (void);
      int ch;

      ch = custom.serdatr;
      mb();

      custom.intreq = IF_RBF;

      /* Break signal from GDB? */
      if (0x03 == (ch & 0xff)) {
	      /* FIXME: This way of doing a breakpoint sucks
		 big time. I will fix it later. */
	      breakpoint ();
      }
#else
      struct m68k_async_struct *info = data;
      int ch, err;

      ch = custom.serdatr;
      mb();

      custom.intreq = IF_RBF;

      if ((ch & 0x1ff) == 0)
	  err = TTY_BREAK;
      else if (ch & SDR_OVRUN)
	  err = TTY_OVERRUN;
      else
	  err = 0;
      rs_receive_char(info, ch & 0xff, err);
#endif
}

static void ser_tx_int( int irq, void *data, struct pt_regs *fp)
{
#ifndef CONFIG_KGDB
      struct m68k_async_struct *info = data;
      int ch;

      if (custom.serdatr & SDR_TBE) {
	   if ((ch = rs_get_tx_char(info)) >= 0)
	        /* write next char */
	        custom.serdat = ch | 0x100;
	   if (ch == -1 || rs_no_more_tx( info ))
	        /* disable tx interrupts */
	        custom.intena = IF_TBE;
      }
#endif
}


static void ser_vbl_int( int irq, void *data, struct pt_regs *fp)
{
	struct m68k_async_struct *info = data;

	check_modem_status(info);
}


static void ser_init( struct m68k_async_struct *info )
{	
#ifndef CONFIG_KGDB
	request_irq(IRQ_AMIGA_VERTB, ser_vbl_int, 0,
		    "serial status", amiga_info);

	/* enable both Rx and Tx interrupts */
	custom.intena = IF_SETCLR | IF_RBF | IF_TBE;

	/* turn on DTR and RTS */
	ser_DTRon();
	ser_RTSon();

	/* remember current state of the DCD and CTS bits */
	current_ctl_bits = ciab.pra & (SER_DCD | SER_CTS);

	MOD_INC_USE_COUNT;
#endif
}


static void ser_enab_tx_int (struct m68k_async_struct *info, int enab_flag)
{
#ifndef CONFIG_KGDB
    if (enab_flag) {
        unsigned long flags;
	save_flags(flags);
	cli();
	custom.intena = IF_SETCLR | IF_TBE;
	mb();
	/* set a pending Tx Interrupt, transmitter should restart now */
	custom.intreq = IF_SETCLR | IF_TBE;
	mb();
	restore_flags(flags);
    } else {
	/* disable Tx interrupt and remove any pending interrupts */
        custom.intena = IF_TBE;
	custom.intreq = IF_TBE;
    }
#endif
}


static void ser_deinit( struct m68k_async_struct *info, int leave_dtr )
{
#ifndef CONFIG_KGDB
    /* disable Rx and Tx interrupt */
    custom.intena = IF_RBF | IF_TBE;
    mb();

    /* wait for last byte to be completely shifted out */
    while( !(custom.serdatr & SDR_TSRE) )
	barrier();

    /* drop RTS and DTR if required */
    ser_RTSoff();
    if (!leave_dtr)
	ser_DTRoff();

    free_irq(IRQ_AMIGA_VERTB, amiga_info);
    MOD_DEC_USE_COUNT;
#endif
}


static int ser_check_custom_divisor (struct m68k_async_struct *info,
				     int baud_base, int divisor)
{
    /* allow any divisor */
    return 0;
}


static void ser_change_speed( struct m68k_async_struct *info )
{
#ifndef CONFIG_KGDB
    unsigned	cflag, baud, chsize, stopb, parity, aflags;
    unsigned	div = 0;
    int realbaud;

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

    if (baud & CBAUDEX)
	baud = baud - (B57600 - B38400 - 1);


    if (baud == 15) {
	    switch (aflags) {
	    case ASYNC_SPD_HI:
		    baud += 1;
		    break;
	    case ASYNC_SPD_VHI:
		    baud += 2;
		    break;
	    case ASYNC_SPD_SHI:
		    baud += 3;
		    break;
	    case ASYNC_SPD_WARP:
		    baud += 4;
		    break;
	    case ASYNC_SPD_CUST:
		    div = info->custom_divisor;
		    break;
	    }
    }
    if (!div){
	/* Maximum speed is 230400 :-) */
	if (baud > 18) baud = 18;
	realbaud = baud_table[baud];
	if (realbaud)
	    div = (amiga_colorclock+realbaud/2)/realbaud - 1;
    }

    if (div) {
	    /* turn on DTR */
	    ser_DTRon();
    } else {
	    /* speed == 0 -> drop DTR */
	    ser_DTRoff();
	    return;
    }

    /* setup the serial port period register */
    custom.serper = div;
#endif
}


static void ser_throttle( struct m68k_async_struct *info, int status )
{
#ifndef CONFIG_KGDB
    if (status)
	ser_RTSoff();
    else
	ser_RTSon();
#endif
}


static void ser_set_break( struct m68k_async_struct *info, int break_flag )
{
#ifndef CONFIG_KGDB
    if (break_flag)
	custom.adkcon = AC_SETCLR | AC_UARTBRK;
    else
	custom.adkcon = AC_UARTBRK;
#endif
}


static void ser_get_serial_info( struct m68k_async_struct *info,
				 struct serial_struct *retinfo )
{
    retinfo->baud_base = amiga_colorclock;
    retinfo->custom_divisor = info->custom_divisor;
}


static unsigned int ser_get_modem_info( struct m68k_async_struct *info )
{
    unsigned int minfo = ciab.pra;

    return(
	   ((minfo & SER_DTR) ? 0 : TIOCM_DTR) |
	   ((minfo & SER_RTS) ? 0 : TIOCM_RTS) |
	   ((minfo & SER_DCD) ? 0 : TIOCM_CAR) |
	   ((minfo & SER_CTS) ? 0 : TIOCM_CTS) |
	   ((minfo & SER_DSR) ? 0 : TIOCM_DSR) |
	   /* TICM_RNG */ 0
	   );
}


static int ser_set_modem_info( struct m68k_async_struct *info,
			       int new_dtr, int new_rts )
{
#ifndef CONFIG_KGDB
    if (new_dtr == 0)
	ser_DTRoff();
    else if (new_dtr == 1)
	ser_DTRon();

    if (new_rts == 0)
	ser_RTSoff();
    else if (new_rts == 1)
	ser_RTSon();

    return 0;
#endif
}

static void ser_stop_receive( struct m68k_async_struct *info )
{
#ifndef CONFIG_KGDB
	/* disable receive interrupts */
	custom.intena = IF_RBF;
	/* clear any pending receive interrupt */
	custom.intreq = IF_RBF;
#endif
}

static int ser_trans_empty( struct m68k_async_struct *info )
{
       return (custom.serdatr & SDR_TSRE);
}

#ifdef CONFIG_KGDB
int amiga_ser_out( unsigned char c )
{
	custom.serdat = c | 0x100;
	mb();
	while (!(custom.serdatr & 0x2000))
		barrier();
	return 1;
}

unsigned char amiga_ser_in( void )
{
	unsigned char c;

	/* XXX: is that ok?? derived from amiga_ser.c... */
	while( !(custom.intreqr & IF_RBF) )
		barrier();
	c = custom.serdatr;
	/* clear the interrupt, so that another character can be read */
	custom.intreq = IF_RBF;
	return c;
}
#endif

#ifdef MODULE
int init_module(void)
{
	return amiga_serinit();
}

void cleanup_module(void)
{
	m68k_unregister_serial(line);
	custom.intena = IF_RBF | IF_TBE; /* forbid interrupts */
	custom.intreq = IF_RBF | IF_TBE; /* clear pending interrupts */
	mb();
	free_irq(IRQ_AMIGA_TBE, amiga_info);
	free_irq(IRQ_AMIGA_RBF, amiga_info);
}
#endif


/* ------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * End:
 * ------------------------------------
 */
