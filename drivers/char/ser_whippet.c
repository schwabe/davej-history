#define WHIPPET_VER "2.2.0pre7"
#define WHIPPET_REV 0
#define WHIPPET_DATE "31/Jan/1999"

/*
 * ser_whippet.c - version info as above
 *
 * Copyright (C) 1997,98,99 Chris Sumner (chris@cpsumner.freeserve.co.uk)
 *
 * This is a driver for the Hisoft Whippet PCMCIA serial port for
 * the Amiga. (16c550b UART)
 *
 * The code is mostly based on ser_ioext.c by Jes Sorensen,
 * (jds@kom.auc.dk) but has been modified to cope with the different
 * hardware footprint of the Whippet.
 *
 * Modified:
 *
 *   11/Feb/98 - General tidying up
 *   31/Mar/98 - More tidying up
 *    7/Apr/98 - Fixed nasty little bug concerning gayle ints
 *    8/Apr/98 - Changed PCMCIA access timings to 100ns
 *   31/Jul/98 - Changed email address
 *    2/Nov/98 - Fixed to work with 2.1.124
 *    7/Nov/98 - Added support for ASYNC_SPD_SHI and _WARP
 *    8/Nov/98 - Fixed a few small bugs
 *   20/Nov/98 - Re-structured code a bit and tidied up
 *   21/Nov/98 - Added support for modules
 *   22/Nov/98 - Fixed inverted DCD bug
 *   23/Nov/98 - Re-structured interrupt code and fixed a few bugs - (2.1.127)
 *   31/Jan/99 - Changed email (again) and modified for 2.2.0pre7
 *
 * To Do:
 *
 *   - Test at 230k4 and 460k8 rates
 *   - Dynamic changing of fifo trigger level (via ioctl? via overrun count?)
 *   - Handling of card insertion and removal (more than just shout at user!)
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/m68kserial.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amigayle.h>

#ifdef gayle
#undef gayle			/* We use our own definition */
#endif

#include "ser_whippet.h"

#define WHIPPET_DEBUG	0	/* debug level */

#define ser_DTRon(uart)  (uart->MCR |=  DTR)
#define ser_RTSon(uart)  (uart->MCR |=  RTS)
#define ser_DTRoff(uart) (uart->MCR &= ~DTR)
#define ser_RTSoff(uart) (uart->MCR &= ~RTS)


/***************************** Prototypes *****************************/
static void ser_init(struct m68k_async_struct *info);
static void ser_deinit(struct m68k_async_struct *info, int leave_dtr);
static void ser_enab_tx_int(struct m68k_async_struct *info, int enab_flag);
static int  ser_check_custom_divisor(struct m68k_async_struct *info,
				     int baud_base, int divisor);
static void ser_change_speed(struct m68k_async_struct *info);
static void ser_throttle(struct m68k_async_struct *info, int status);
static void ser_set_break(struct m68k_async_struct *info, int break_flag);
static void ser_get_serial_info(struct m68k_async_struct *info,
				struct serial_struct *retinfo);
static unsigned int ser_get_modem_info(struct m68k_async_struct *info);
static int ser_set_modem_info(struct m68k_async_struct *info, int new_dtr,
			      int new_rts);
static int ser_ioctl(struct tty_struct *tty, struct file *file,
		     struct m68k_async_struct *info, unsigned int cmd,
		     unsigned long arg);
static void ser_stop_receive(struct m68k_async_struct *info);
static int ser_trans_empty(struct m68k_async_struct *info);
/************************* End of Prototypes **************************/

/*
 * SERIALSWITCH structure for the Whippet serial interface.
 */

static SERIALSWITCH whippet_ser_switch = {
	ser_init,
	ser_deinit,
	ser_enab_tx_int,
	ser_check_custom_divisor,
	ser_change_speed,
	ser_throttle,
	ser_set_break,
	ser_get_serial_info,
	ser_get_modem_info,
	ser_set_modem_info,
	ser_ioctl,
	ser_stop_receive,
	ser_trans_empty,
	NULL
};

static int whippet_baud_table[20] = {
	/* B0     */ 0,
	/* B50    */ 9216,
	/* B75    */ 6144,
	/* B110   */ 4189,	/* 110.00238 */
	/* B134.5 */ 3426,	/* 134.50087 */
	/* B150   */ 3072,
	/* B200   */ 2304,
	/* B300   */ 1536,
	/* B600   */ 768,
	/* B1200  */ 384,
	/* B1800  */ 256,
	/* B2400  */ 192,
	/* B4800  */ 96,
	/* B9600  */ 48,
	/* B19200 */ 24,
	/* B38400 */ 12,	/* The last of the standard rates.  */
	/* B57600 */ 8,		/* ASYNC_SPD_HI                     */
	/* B115K2 */ 4,		/* ASYNC_SPD_VHI                    */
	/* B230K4 */ 2,		/* ASYNC_SPD_SHI                    */
	/* B460K8 */ 1		/* ASYNC_SPD_WARP                   */
};


static volatile struct GAYLE *gayle;		/* gayle register struct */
static volatile struct WHIPPET *whippet;	/* whippet regs struct */

static int fifo_trig_level=FIFO_TRIG_4;		/* can be changed */

/*
 * There are 4 receiver FIFO-interrupt trigger levels (FIFO_TRIG_x), that
 * indicates how many bytes are to be allowed in the receiver-FIFO before
 * an interrupt is generated:
 *                x =  1 =  1 byte
 *                x =  4 =  4 bytes
 *                x =  8 =  8 bytes
 *                x = 14 = 14 bytes
 * If you keep getting overruns try lowering this value one step at a time.
 */

/***** start of ser_interrupt() - Handler for serial interrupt. *****/

static void ser_interrupt(int irq, void *data, struct pt_regs *regs)
{
struct m68k_async_struct *info = data;
u_char iir,ier,lsr,gayleirq = gayle->intreq;

	if ((gayleirq & 0x7c)==0) return;	/* quick exit */

	if (gayleirq & 0x5c) {
		gayle->intreq = ((gayleirq & 0x5c) ^ 0x5c) | (GAYLE_IRQ_IDE | GAYLE_IRQ_SC);
		printk("gayle->intreq = 0x%02x; gayle->inten = 0x%02x\n",gayleirq, gayle->inten);
		if (gayleirq & GAYLE_IRQ_CCDET) {
			if (gayle->cardstatus & GAYLE_CS_CCDET) {
				printk("Card inserted! Don't do that!\n");
			} else {
				printk("Card removed! Don't do that!\n");
			}
		}
	}

	if ((gayleirq & GAYLE_IRQ_SC)==0) return;

/* If we got here, then there is an interrupt waiting for us to service */

	iir = whippet->IIR;

/* Disable UART interrupts for now... */

	ier = whippet->IER; whippet->IER = 0;

	while (!(iir & IRQ_PEND)) {	/* loop until no more ints */

		switch (iir & (IRQ_ID1 | IRQ_ID2 | IRQ_ID3)) {
			case IRQ_RLS:		/* Receiver Line Status */
			case IRQ_CTI:		/* Character Timeout */
			case IRQ_RDA:		/* Received Data Available */
	/*
	 * Copy chars to the tty-queue ...
	 * Be careful that we aren't passing one of the
	 * Receiver Line Status interrupt-conditions without noticing.
	 */
			{
				int ch;

				lsr = whippet->LSR;
				while (lsr & DR) {
					u_char err;
					ch = whippet->RBR;

					if (lsr & BI)      err = TTY_BREAK;
					else if (lsr & PE) err = TTY_PARITY;
					else if (lsr & OE) err = TTY_OVERRUN;
					else if (lsr & FE) err = TTY_FRAME;
					else err = 0;

					rs_receive_char(info, ch, err);
					lsr = whippet->LSR;
				}
			}
			break;

			case IRQ_THRE:	/* Transmitter holding register empty */
			{
				int fifo_space = FIFO_SIZE;

	/* If the uart is ready to receive data and there are chars in */
	/* the queue we transfer all we can to the uart's FIFO         */

				if (rs_no_more_tx(info)) {
#if WHIPPET_DEBUG
					printk("rs_no_more_tx()\n");
#endif
		/* Disable transmitter empty interrupt */
					ier &= ~(ETHREI);

		/* Read IIR to acknowledge the interrupt */
					(void)whippet->IIR;
					break;
				}

		/* Handle software flow control */
				if (info->x_char) {
#if WHIPPET_DEBUG
					printk("Flow: X%s\n",(info->x_char == 19) ? "OFF" : "ON");
#endif
					whippet->THR = info->x_char;
					info->icount.tx++;
					info->x_char = 0;
					fifo_space--;
				}

		/* Fill the fifo */
				while (fifo_space > 0) {
					fifo_space--;
					whippet->THR = info->xmit_buf[info->xmit_tail++];
					info->xmit_tail &= (SERIAL_XMIT_SIZE-1);
					info->icount.tx++;
					if (--info->xmit_cnt == 0) break;
				}
#if WHIPPET_DEBUG
				if (fifo_space == 0) printk("fifo full\n");
#endif
		/* Don't need THR interrupts any more */
				if (info->xmit_cnt == 0) {
#if WHIPPET_DEBUG
					printk("TX ints OFF\n");
#endif
					ier &= ~(ETHREI);
				}

				if (info->xmit_cnt < WAKEUP_CHARS) {
#if WHIPPET_DEBUG
					printk("rs_sched_event()\n");
#endif
					rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
				}
			}
			break;

			case IRQ_MS: /* Must be modem status register interrupt? */
			{
				u_char msr = whippet->MSR;

				if (info->flags & ASYNC_INITIALIZED) {
					if (msr & DCTS) {
#if WHIPPET_DEBUG
						printk("CTS = %s\n",(msr & CTS) ? "ON" : "OFF");
#endif
						rs_check_cts(info, (msr & CTS));
					}
					if (msr & DDCD)
						rs_dcd_changed(info, (msr & DCD));
				}
			}
			break;

		} /* switch (iir) */

		iir = whippet->IIR;
	} /* while IRQ_PEND */

/* Acknowledge gayle STATUS_CHANGE interrupt */

	gayle->intreq = ((gayleirq & GAYLE_IRQ_SC) ^ GAYLE_IRQ_SC) | GAYLE_IRQ_IDE;

/* Re-enable UART interrupts */

	whippet->IER = ier;
}

/***** end of ser_interrupt() *****/


/***** start of ser_init() *****/

static void ser_init(struct m68k_async_struct *info)
{
#if WHIPPET_DEBUG
	printk("ser_init()\n");
#endif
	while ((whippet->LSR) & DR)
		(void)whippet->RBR;		/* read a byte */

/* Set DTR and RTS */
	whippet->MCR |= (DTR | RTS);

/* Enable interrupts. IF_PORTS irq has already been enabled in whippet_init()*/
/* DON'T enable ETHREI here because there is nothing to send yet (murray)    */
	whippet->IER |= (ERDAI | ELSI | EMSI);

	MOD_INC_USE_COUNT;
}

/***** end of ser_init() *****/


/***** start of ser_deinit() *****/

static void ser_deinit(struct m68k_async_struct *info, int leave_dtr)
{
#if WHIPPET_DEBUG
	printk("ser_deinit()\n");
#endif
	/* Wait for the uart to get empty */
	while (!((whippet->LSR) & TEMT)) {
	}

	while ((whippet->LSR) & DR) {
		(void)whippet->RBR;
	}

/* No need to disable UART interrupts since this will already
 * have been done via ser_enab_tx_int() and ser_stop_receive()
 */

	ser_RTSoff(whippet);
	if (!leave_dtr)	ser_DTRoff(whippet);

	MOD_DEC_USE_COUNT;
}

/***** end of ser_deinit() *****/


/***** start of ser_enab_tx_int() *****/

/*
** Enable or disable tx interrupts.
** Note that contrary to popular belief, it is not necessary to
** send a character to cause an interrupt to occur. Whenever the
** THR is empty and THRE interrupts are enabled, an interrupt will occur.
** (murray)
*/
static void ser_enab_tx_int(struct m68k_async_struct *info, int enab_flag)
{
#if WHIPPET_DEBUG
	printk("ser_enab_tx_int(%s)\n",(enab_flag) ? "ON" : "OFF");
#endif
	if (enab_flag)	whippet->IER |= ETHREI;
	else		whippet->IER &= ~(ETHREI);
}

/***** end of ser_enab_tx_int() *****/


static int  ser_check_custom_divisor(struct m68k_async_struct *info,
				     int baud_base, int divisor)
{
#if WHIPPET_DEBUG
	printk("ser_check_custom_divisor()\n");
#endif
	/* Always return 0 or else setserial spd_hi/spd_vhi doesn't work */
	return 0;
}


/***** start of ser_change_speed() *****/

static void ser_change_speed(struct m68k_async_struct *info)
{
u_int cflag, baud, chsize, stopb, parity, aflags;
u_int div = 0, ctrl = 0;

#if WHIPPET_DEBUG
	printk("ser_change_speed()\n");
#endif

	if (!info->tty || !info->tty->termios) return;

	cflag  = info->tty->termios->c_cflag;
	baud   = cflag & CBAUD;
	chsize = cflag & CSIZE;
	stopb  = cflag & CSTOPB;
	parity = cflag & (PARENB | PARODD);
	aflags = info->flags & ASYNC_SPD_MASK;

	if (cflag & CRTSCTS)	info->flags |= ASYNC_CTS_FLOW;
	else			info->flags &= ~ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)	info->flags &= ~ASYNC_CHECK_CD;
	else			info->flags |= ASYNC_CHECK_CD;

	if (baud & CBAUDEX) {
		baud &= ~CBAUDEX;
		if (baud < 1 || baud > 2)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			baud += 15;
	}
	if (baud == 15) {
		if (aflags == ASYNC_SPD_HI)	/*  57k6 */
			baud += 1;
		if (aflags == ASYNC_SPD_VHI)	/* 115k2 */
			baud += 2;
		if (aflags == ASYNC_SPD_SHI)	/* 230k4 */
			baud += 3;
		if (aflags == ASYNC_SPD_WARP)	/* 460k8 */
			baud += 4;
		if (aflags == ASYNC_SPD_CUST)
			div = info->custom_divisor;
	}
	if (!div) {
		/* Maximum speed is 460800 */
			if (baud > 19) baud = 19;
			div = whippet_baud_table[baud];
	}

#if WHIPPET_DEBUG
	printk("divisor=%d, baud rate=%d\n",div,(div==0)? -1 : WHIPPET_BAUD_BASE/div);
#endif

	if (!div) {
		/* speed == 0 -> drop DTR */
		ser_DTRoff(whippet);
		return;
	}

/*
 * We have to set DTR when a valid rate is chosen, otherwise DTR
 * might get lost when programs use this sequence to clear the line:
 *
 * change_speed(baud = B0);
 * sleep(1);
 * change_speed(baud = Bx); x != 0
 *
 * The pc-guys do this aswell.
 */
	ser_DTRon(whippet);

	if (chsize == CS8)	ctrl |= data_8bit;
	else if (chsize == CS7)	ctrl |= data_7bit;
	else if	(chsize == CS6)	ctrl |= data_6bit;
	else if (chsize == CS5)	ctrl |= data_5bit;

/* If stopb is true we set STB which means 2 stop-bits */
/* otherwise we only get 1 stop-bit.                   */

	ctrl |= (stopb ? STB : 0);
	ctrl |= ((parity & PARENB) ? ((parity & PARODD) ? (PEN) : (PEN |
							  EPS)) : 0x00 );

	whippet->LCR = (ctrl | DLAB);

		/* Store high byte of divisor */

	whippet->DLM = ((div >> 8) & 0xff);

		/* Store low byte of divisor */

	whippet->DLL = (div & 0xff);
	whippet->LCR = ctrl;
}

/***** end of ser_change_speed() *****/


/***** start of ser_throttle() *****/

static void ser_throttle(struct m68k_async_struct *info, int status)
{
#if WHIPPET_DEBUG
	printk("ser_throttle(rts=%s)\n", (status) ? "OFF" : "ON");
#endif
	if (status)	ser_RTSoff(whippet);
	else		ser_RTSon(whippet);
}

/***** end of ser_throttle() *****/


/***** start of ser_set_break() *****/

static void ser_set_break(struct m68k_async_struct *info, int break_flag)
{
#if WHIPPET_DEBUG
	printk("ser_set_break(%s)\n", (break_flag) ? "ON" : "OFF");
#endif
	if (break_flag)	whippet->LCR |= SET_BREAK;
	else		whippet->LCR &= ~SET_BREAK;
}

/***** end of ser_set_break() *****/


/***** start of ser_get_serial_info() *****/

static void ser_get_serial_info(struct m68k_async_struct *info,
				struct serial_struct *retinfo)
{
#if WHIPPET_DEBUG
	printk("ser_get_serial_info()\n");
#endif

	retinfo->baud_base = WHIPPET_BAUD_BASE;
	retinfo->xmit_fifo_size = FIFO_SIZE;	/* This field is currently ignored, */
						/* by the upper layers of the       */
						/* serial-driver.                   */
	retinfo->custom_divisor = info->custom_divisor;
}

/***** end of ser_get_serial_info() *****/


/***** start of ser_get_modem() *****/

static unsigned int ser_get_modem_info(struct m68k_async_struct *info)
{
unsigned char msr, mcr;

#if WHIPPET_DEBUG > 1
	printk("ser_get_modem()\n");
#endif

	msr = whippet->MSR;
	mcr = whippet->MCR;	/* The DTR and RTS are located in the */
				/* ModemControlRegister ...           */

	return (
		((mcr & DTR) ? TIOCM_DTR : 0) |
		((mcr & RTS) ? TIOCM_RTS : 0) |

		((msr & DCD) ? TIOCM_CAR : 0) |
		((msr & CTS) ? TIOCM_CTS : 0) |
		((msr & DSR) ? TIOCM_DSR : 0) |
		((msr & RING_I) ? TIOCM_RNG : 0)
	);
}

/***** end of ser_get_modem() *****/


/***** start of ser_set_modem_info() *****/

static int ser_set_modem_info(struct m68k_async_struct *info, int new_dtr,
			      int new_rts)
{
#if WHIPPET_DEBUG
	printk("ser_set_modem(dtr=%s, rts=%s)\n",(new_dtr == 0) ? "OFF" : "ON",(new_rts == 0) ? "OFF" : "ON");
#endif

	if (new_dtr == 0)	ser_DTRoff(whippet);
	else if (new_dtr == 1)	ser_DTRon(whippet);

	if (new_rts == 0)	ser_RTSoff(whippet);
	else if (new_rts == 1)	ser_RTSon(whippet);

	return 0;
};

/***** end of ser_set_modem_info() *****/


/***** start of ser_stop_receive() *****/

static void ser_stop_receive (struct m68k_async_struct *info)
{
#if WHIPPET_DEBUG
	printk("ser_stop_receive()\n");
#endif
	/* Disable uart receive and status interrupts */
	whippet->IER &= ~(ERDAI | ELSI | EMSI);
}

/***** end of ser_stop_receive() *****/


/***** start of ser_trans_empty() *****/

static int ser_trans_empty (struct m68k_async_struct *info)
{
#if WHIPPET_DEBUG
	printk("ser_trans_empty()\n");
#endif
	return (whippet->LSR & THRE);
}

/***** end of ser_trans_empty() *****/


/***** start of ser_ioctl() *****/
static int ser_ioctl(struct tty_struct *tty, struct file *file,
		     struct m68k_async_struct *info, unsigned int cmd,
		     unsigned long arg)
{
/*	switch(cmd) {
		case:
	}*/
	return -ENOIOCTLCMD;
}

/***** end of ser_ioctl() *****/


/***** start of ser_reset_port() *****/

void ser_reset_port(void)
{
#if WHIPPET_DEBUG
	printk("ser_reset_port()\n");
#endif
/*
 * Try and reset the serial port to a default state
 */
	whippet->IER = 0x00;	/* disable interrupts */
	(void)whippet->IIR;	/* clear any pending misc interrupts */
	(void)whippet->LSR;	/* clear any pending LSR interrupts */
	(void)whippet->MSR;	/* clear any pending MSR interrupts */

/*
 * Set the serial port to a default setting of 8N1 - 9600
 */
	whippet->LCR = (data_8bit | DLAB);
	whippet->DLM = 0;
	whippet->DLL = 48;
	whippet->LCR = (data_8bit);

/*
 * Set the rx FIFO-trigger count.
 */
	whippet->FCR = (RCVR_FIFO_RES | FIFO_ENA |
			XMIT_FIFO_RES | fifo_trig_level );
	whippet->MCR = 0;
}

/***** end of ser_reset_port() *****/


/***** start of whippet_init() *****/

/*
 * Detect and initialize any Whippet found in the system.
 */

static int line;	/* The line assigned to us by register_serial() */

static struct m68k_async_struct *amiga_info;	/* our async struct */

int whippet_init(void)
{
unsigned long flags;
struct serial_struct req;
struct m68k_async_struct *info;

#if WHIPPET_DEBUG
	printk("whippet_init()\n");
#endif
	if (!(MACH_IS_AMIGA))
		return -ENODEV;

	if ((amiga_model!=AMI_1200) && (amiga_model!=AMI_600))
		return -ENODEV;

	if (!(AMIGAHW_PRESENT(PCMCIA)))	/* might be missing, you never know! */
		return -ENODEV;

/*
 * Initialise hardware structure pointers
 */
	gayle = (struct GAYLE *)(zTwoBase + GAYLE_ADDRESS);
	whippet = (struct WHIPPET *)(zTwoBase + WHIPPET_PHYSADDR);

#if WHIPPET_DEBUG
	printk("gayle->cardstatus = 0x%02x\n",gayle->cardstatus);
	printk("gayle->intreq = 0x%02x\n",gayle->intreq);
	printk("gayle->inten  = 0x%02x\n",gayle->inten);
	printk("gayle->config = 0x%02x\n",gayle->config);
#endif
	printk("Probing for Whippet serial port... (v%s r%i - %s)\n",WHIPPET_VER, WHIPPET_REV, WHIPPET_DATE);

/*
 * Test gayle cardstatus bits for presence of a card
 */
	if (!(gayle->cardstatus & GAYLE_CS_CCDET)) {
		printk("No PCMCIA Card detected\n");
		return -ENODEV;
#if WHIPPET_DEBUG
	} else { printk("PCMCIA Card detected\n");
#endif
	}

/*
 * Card detected, but is it a Whippet??? Let's try and find out...
 */
	{
	u_char ch1,ch2;
		whippet->SCR = 0x42;
		whippet->IER = 0x00;
		ch1=whippet->SCR;	/* should be 0x42 */
		whippet->SCR = 0x99;
		whippet->IER = 0x00;
		ch2=whippet->SCR;	/* should be 0x99 */
		if ((ch1!=0x42) || (ch2!=0x99)) return -ENODEV;
	}

	ser_reset_port();	/* initialise the serial port */

/*
 * Set the necessary tty-stuff.
 */
	req.line = -1;			/* first free ttyS? device */
	req.type = SER_WHIPPET;
	req.port = (int) &(whippet->RBR);

	if ((line = register_serial( &req )) < 0) {
		printk( "Cannot register Whippet serial port: no free device\n" );
		return -EBUSY;
	}

	info = &rs_table[line];	/* set info == struct *m68k_async_struct */

	info->nr_uarts = 1;			/* one UART         */
	info->sw = &whippet_ser_switch;		/* switch functions */
	info->icount.cts = info->icount.dsr = 0;
	info->icount.rng = info->icount.dcd = 0;
	info->icount.rx = info->icount.tx = 0;
	info->icount.frame = info->icount.parity = 0;
	info->icount.overrun = info->icount.brk = 0;

	amiga_info = info;	/* initialise our static async struct */

/*
 * Clear any spurious interrupts in gayle
 */
	gayle->intreq = ((gayle->intreq & 0x6c) ^ 0x6c) | GAYLE_IRQ_IDE;

/*
 * Install ISR - level 2 - data is struct *m68k_async_struct
 */
	request_irq(IRQ_AMIGA_PORTS, ser_interrupt, 0, "whippet serial", info);

	save_flags(flags);
	cli();

#if WHIPPET_DEBUG
	printk("gayle->cardstatus = 0x%02x\n",gayle->cardstatus);
	printk("gayle->intreq = 0x%02x\n",gayle->intreq);
	printk("gayle->inten  = 0x%02x\n",gayle->inten);
	printk("gayle->config = 0x%02x\n",gayle->config);
#endif

/*
 * Enable status_change interrupts in gayle
 */

	gayle->inten |= GAYLE_IRQ_SC;
	gayle->cardstatus = GAYLE_CS_WR | GAYLE_CS_DA;
	gayle->config = 0;

#if WHIPPET_DEBUG
	printk("gayle->cardstatus = 0x%02x\n",gayle->cardstatus);
	printk("gayle->intreq = 0x%02x\n",gayle->intreq);
	printk("gayle->inten  = 0x%02x\n",gayle->inten);
	printk("gayle->config = 0x%02x\n",gayle->config);
#endif

	restore_flags(flags);

#if WHIPPET_DEBUG
	printk("Detected Whippet Serial Port at 0x%08x (ttyS%i)\n",(int)whippet,line);
#endif
	return 0;
}

/***** end of whippet_init() *****/


/***** Module functions *****/

#ifdef MODULE
int init_module(void)
{
	return whippet_init();
}

void cleanup_module(void)
{
#if WHIPPET_DEBUG
	printk("Closing Whippet Device!\n");
#endif
	unregister_serial(line);

#if WHIPPET_DEBUG
	printk("gayle->cardstatus = 0x%02x\n",gayle->cardstatus);
	printk("gayle->intreq = 0x%02x\n",gayle->intreq);
	printk("gayle->inten  = 0x%02x\n",gayle->inten);
	printk("gayle->config = 0x%02x\n",gayle->config);
#endif
	ser_reset_port();

	gayle->cardstatus = 0;
	gayle->intreq = ((gayle->intreq & 0x6c) ^ 0x6c) | GAYLE_IRQ_IDE;
	gayle->inten &= GAYLE_IRQ_IDE;

#if WHIPPET_DEBUG
	printk("gayle->cardstatus = 0x%02x\n",gayle->cardstatus);
	printk("gayle->intreq = 0x%02x\n",gayle->intreq);
	printk("gayle->inten  = 0x%02x\n",gayle->inten);
	printk("gayle->config = 0x%02x\n",gayle->config);
#endif
	free_irq(IRQ_AMIGA_PORTS,amiga_info);
}
#endif

/***** end of Module functions *****/
