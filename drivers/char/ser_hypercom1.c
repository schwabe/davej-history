#define HYPERCOM1_VERSION 1
#define HYPERCOM1_REV 13
#define HYPERCOM1_DATE "8.7.99"
/*
 * HyperCOM1 driver for LinuxAPUS/M68K
 * Project started 25-May-1999 by Gordon Huby
 * Email: <gordon@ghuby.freeserve.co.uk>
 * ---------------------------------------------------------------------------
 * This code is based on ser_ioext.c by Jes Sorensen (jds@kom.auc.dk)
 * and ser_whippet.c by Chris Sumner (chris@cpsumner.freeserve.co.uk)
 * ---------------------------------------------------------------------------
 * $Id: ser_hypercom1.c,v 1.13 1999/07/08 17:41:48 gordon Exp $
 * ---------------------------------------------------------------------------
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 * ---------------------------------------------------------------------------
 */

/* #include <linux/config.h> */
#include <linux/module.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/termios.h>
#include <linux/interrupt.h>
#include <linux/m68kserial.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/irq.h>

#include "ser_hypercom1.h"

/* comment out for no debuging */
/* #define	HYPERCOM1_DEBUG	1 */

/* port number */
#ifndef SER_HYPERCOM1
#define	SER_HYPERCOM1	113
#endif

/* Some usefull macros */
#define ser_DTRon(uart)		((uart->MCR) |= MCR_DTR)
#define ser_DTRoff(uart)	((uart->MCR) &= ~(MCR_DTR))
#define ser_RTSon(uart)		((uart->MCR) |= MCR_RTS)
#define ser_RTSoff(uart)	((uart->MCR) &= ~(MCR_RTS))

/***************************** Prototypes *****************************/
static void ser_init(struct m68k_async_struct *info);
static void ser_deinit(struct m68k_async_struct *info,int leave_dtr);
static void ser_enab_tx_int(struct m68k_async_struct *info,int enab_flag);
static int  ser_check_custom_divisor(struct m68k_async_struct *info,int baud_base,int divisor);
static void ser_change_speed(struct m68k_async_struct *info);
static void ser_throttle(struct m68k_async_struct *info,int status);
static void ser_break(struct m68k_async_struct *info,int break_flag);
static void ser_get_serial_info(struct m68k_async_struct *info,struct serial_struct *retinfo);
static unsigned int ser_get_modem_info(struct m68k_async_struct *info);
static int ser_set_modem_info(struct m68k_async_struct *info,int new_dtr,int new_rts);
static int ser_ioctl(struct tty_struct *tty,struct file *file,
	struct m68k_async_struct *info,unsigned int cmd,unsigned long arg);
static void ser_stop_receive(struct m68k_async_struct *info);
static int ser_trans_empty(struct m68k_async_struct *info);
/************************* End of Prototypes **************************/

/*
 * SERIALSWITCH structure for HyperCOM1 serial port
 */

static SERIALSWITCH hypercom1_ser_switch = {
	ser_init,
	ser_deinit,
	ser_enab_tx_int,
	ser_check_custom_divisor,
	ser_change_speed,
	ser_throttle,
	ser_break,
	ser_get_serial_info,
	ser_get_modem_info,
	ser_set_modem_info,
	ser_ioctl,
	ser_stop_receive,
	ser_trans_empty,
	NULL,
};

static u_int hypercom1_baud_table[20] = {
	0,
	50,
	75,
	110,
	134,
	150,
	200,
	300,
	600,
	1200,
	1800,
	2400,
	4800,
	9600,
	19200,
	38400,  /* The last of the standard rates. */
	57600,  /* ASYNC_SPD_HI   */
	115200, /* ASYNC_SPD_VHI  */
	230400, /* ASYNC_SPD_SHI  */
	480600, /* ASYNC_SPD_WARP */
};

/***************************************************************************/
static const u_char fifo_trigger_level = TX_FIFO_TRIG_16 | RX_FIFO_TRIG_28; /* changeable */
/*-------------------------------------------------------------------------*/
static struct STARTECH_16C650 *hypercom1;
static struct m68k_async_struct *amiga_info;
static int line;
/***************************************************************************/

/*
 * ser_interrupt()
 */
static void ser_interrupt(int irq, void *data, struct pt_regs *regs)
{
	struct m68k_async_struct *info = data;
	u_char lsr,isr,ier;

#ifdef HYPERCOM1_DEBUG
	printk("ser_interrupt()\n");
#endif
	isr = hypercom1->ISR;
	ier = hypercom1->IER;

	while (!(isr & IRQ_PEND)) {	/* loop until no more ints */

		switch (isr & IRQ_MASK) {
                    case IRQ_RLS:		/* Receiver Line Status */
                    case IRQ_RDTO:		/* Receiver Data Timeout */
                    case IRQ_RDR:		/* Receiver Data Ready */
         /*
	 * Copy chars to the tty-queue ...
	 * Be careful that we aren't passing one of the
	 * Receiver Line Status interrupt-conditions without noticing.
	 */
                    {
                            int ch;
                            
                            lsr = hypercom1->LSR;
                            while (lsr & LSR_RDR) {
                                    u_char err;
                                    ch = hypercom1->RHR;
                                    
                                    if (lsr & LSR_BI)	    err = TTY_BREAK;
                                    else if (lsr & LSR_PE)  err = TTY_PARITY;
                                    else if (lsr & LSR_ORE) err = TTY_OVERRUN;
                                    else if (lsr & LSR_FE)  err = TTY_FRAME;
                                    else err = 0;
                                    
                                    rs_receive_char(info, ch, err);
                                    lsr = hypercom1->LSR;
                            }
                    }
                    break;
                    case IRQ_THRE:	/* Transmitter holding register empty */
                    {
                            int fifo_space = FIFO_SIZE;
                            int ch;

                            while (fifo_space > 0) {
                                    fifo_space--;
                                    if ((ch = rs_get_tx_char(info)) >= 0)        
                                            hypercom1->THR = (u_char)ch;
                                    if (ch == -1 || rs_no_more_tx(info)) {
                                            ier &= ~(IER_THR);
                                            break;
                                    }
                            }
#ifdef HYPERCOM1_DEBUG
                            printk("fifo_space=%d\n",fifo_space);
#endif
                    }
                    break;
                    case IRQ_MSR: /* Must be modem status register interrupt? */
                    {
                            u_char msr = hypercom1->MSR;

                            if (info->flags & ASYNC_INITIALIZED) {
                                    if (msr & MSR_DCTS) {
#ifdef HYPERCOM1_DEBUG
                                            printk("CTS = %s\n",(msr & MSR_CTS) ? "ON" : "OFF");
#endif
                                            rs_check_cts(info, (msr & MSR_CTS));
                                    }
                                    if (msr & MSR_DCD)
                                            rs_dcd_changed(info, (msr & MSR_CD));
                            }
                    }
                    break;
                    
		} /* switch (iir) */

		isr = hypercom1->ISR;
	} /* while IRQ_PEND */

/* Re-enable UART interrupts */
	hypercom1->IER = ier;
}

/*-------------------------------------------------------------------------*/

/* 
 * ser_init() init the port as necessary, set RTS and DTR and enable interrupts
 * It does not need to set the speed and other parameters, because change_speed,
 * is called too.
 */

static void ser_init(struct m68k_async_struct *info)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_init()\n");
#endif
	/* Enable DTR and RTS */
	hypercom1->MCR |= (MCR_DTR | MCR_RTS | MCR_OP2);

       	hypercom1->FCR	= FCR_FE | FCR_RFR | FCR_XFR | fifo_trigger_level;

/* Enable interrupts. IF_EXTER irq has already been enabled in hypercom1_init()*/
/* DON'T enable IER_THR here because there is nothing to send yet (murray)    */

	hypercom1->IER |= (IER_RHR | IER_RLS | IER_MSI);

	MOD_INC_USE_COUNT;
}

/*-------------------------------------------------------------------------*/

/*
 * deinit(): Stop and shutdown the port (e.g. disable interrupts, ...)
 */
static void ser_deinit(struct m68k_async_struct *info,int leave_dtr)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_deinit()\n");
#endif
	hypercom1->IER=0x00;
        
        hypercom1->FCR = FCR_FE | FCR_RFR | FCR_XFR | fifo_trigger_level;

	ser_RTSoff(hypercom1);
	if (!leave_dtr)	ser_DTRoff(hypercom1);

	MOD_DEC_USE_COUNT;
}

/*-------------------------------------------------------------------------*/

/*
 *   enab_tx_int(): Enable or disable the Tx Buffer Empty interrupt
 *      independently from other interrupt sources. If the int is
 *      enabled, the transmitter should also be restarted, i.e. if there
 *      are any chars to be sent, they should be put into the Tx
 *      register. The real en/disabling of the interrupt may be a no-op
 *      if there is no way to do this or it is too complex. This Tx ints
 *      are just disabled to save some interrupts if the transmitter is
 *      stopped anyway. But the restarting must be implemented!
 */
static void ser_enab_tx_int(struct m68k_async_struct *info,int enab_flag)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_enab_tx_int(%s)\n",(enab_flag) ? "ON" : "OFF");
#endif
	if (enab_flag)	hypercom1->IER |= IER_THR;
	else		hypercom1->IER &= ~(IER_THR);
}

/*-------------------------------------------------------------------------*/

/*
 *   check_custom_divisor(): Check the given custom divisor for legality
 *      and return 0 if OK, non-zero otherwise.
 */
static int ser_check_custom_divisor(struct m68k_async_struct *info,int baud_base,int divisor)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_check_custom_divisor()\n");
#endif
	return 0;
}

/*-------------------------------------------------------------------------*/

/*
 *   change_speed(): Set port speed, character size, number of stop
 *      bits and parity from the termios structure. If the user wants
 *      to set the speed with a custom divisor, he is required to
 *      check the baud_base first!
 */
static void ser_change_speed(struct m68k_async_struct *info)
{
	u_int cflag, real_baud, baud, chsize, stopb, parity, aflags;
	u_int div = 0, ctrl = 0;

#ifdef HYPERCOM1_DEBUG
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

	if (baud & CBAUDEX)
		baud = baud - (B57600 - B38400 -1);

#ifdef HYPERCOM1_DEBUG
	printk("Baud=%d\n",baud);
#endif
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
	
	if (!div) {
		/* Maximum speed is 460800 */
		if (baud > 19) baud = 19;
		real_baud = hypercom1_baud_table[baud];
		if (real_baud==0) div=0;
		else div = HYPERCOM1_BAUD_BASE/real_baud;
	}

#ifdef HYPERCOM1_DEBUG
	printk("divisor=%d, baud rate=%d\n",div,real_baud);
#endif
	if (!div) {
		/* speed == 0 -> drop DTR */
		ser_DTRoff(hypercom1);
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
	ser_DTRon(hypercom1);

	if (chsize == CS8)	ctrl |= data_8bit;
	else if (chsize == CS7)	ctrl |= data_7bit;
	else if	(chsize == CS6)	ctrl |= data_6bit;
	else if (chsize == CS5)	ctrl |= data_5bit;

/* If stopb is true we set STB which means 2 stop-bits */
/* otherwise we only get 1 stop-bit.                   */

	ctrl |= (stopb ? LCR_SB : 0);

	ctrl |= ((parity & PARENB) ? ((parity & PARODD) ? (LCR_PE) : (LCR_PE | LCR_EP)) : 0x00 );

	hypercom1->LCR = (ctrl | LCR_DLE);

	/* Store high byte of divisor */
	hypercom1->DLM = ((div >> 8) & 0xff);

#ifdef HYPERCOM1_DEBUG
	printk("DLM = 0x%.2X\n",((div >> 8) & 0xff));
#endif
	/* Store low byte of divisor */
	hypercom1->DLL = (div & 0xff);

#ifdef HYPERCOM1_DEBUG
	printk("DLL = 0x%.2X\n",(div & 0xff));
#endif
	hypercom1->LCR = ctrl;
}

/*-------------------------------------------------------------------------*/

/*
 *   throttle(): Set or clear the RTS line according to 'status'.
 */
static void ser_throttle(struct m68k_async_struct *info,int status)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_throttle(rts=%s)\n", (status) ? "OFF" : "ON");
#endif
	if (status)	ser_RTSoff(hypercom1);
	else		ser_RTSon(hypercom1);
}

/*-------------------------------------------------------------------------*/

/*
 *   set_break(): Set or clear the 'Send a Break' flag.
 */
static void ser_break(struct m68k_async_struct *info,int break_flag)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_set_break(%s)\n", (break_flag) ? "ON" : "OFF");
#endif
	if (break_flag)	hypercom1->LCR |= LCR_SETB;
	else		hypercom1->LCR &= ~LCR_SETB;

	return;
}

/*-------------------------------------------------------------------------*/

/*
 *   get_serial_info(): Fill in the baud_base and custom_divisor
 *      fields of a serial_struct. It may also modify other fields, if
 *      needed.
 */
static void ser_get_serial_info(struct m68k_async_struct *info,struct serial_struct *retinfo)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_get_serial_info()\n");
#endif	
	retinfo->baud_base = HYPERCOM1_BAUD_BASE;
	retinfo->xmit_fifo_size = FIFO_SIZE;	/* This field is currently ignored, */
						/* by the upper layers of the       */
						/* serial-driver.                   */
	retinfo->custom_divisor = info->custom_divisor;
}

/*-------------------------------------------------------------------------*/

/*
 *   get_modem_info(): Return the status of RTS, DTR, DCD, RI, DSR and CTS.
 */
static unsigned int ser_get_modem_info(struct m68k_async_struct *info)
{
	u_char msr, mcr;

#ifdef HYPERCOM1_DEBUG
	printk("ser_get_modem()\n");
#endif

	msr = hypercom1->MSR;
	mcr = hypercom1->MCR;	/* The DTR and RTS are located in the */
				/* ModemControlRegister ...           */
	return (
		((mcr & MCR_DTR) ? TIOCM_DTR : 0) |
		((mcr & MCR_RTS) ? TIOCM_RTS : 0) |

		((msr & MSR_CD) ? TIOCM_CAR : 0) |
		((msr & MSR_CTS) ? TIOCM_CTS : 0) |
		((msr & MSR_DSR) ? TIOCM_DSR : 0) |
		((msr & MSR_RING) ? TIOCM_RNG : 0)
		);
}

/*-------------------------------------------------------------------------*/

/*
 *   set_modem_info(): Set the status of RTS and DTR according to
 *      'new_dtr' and 'new_rts', resp. 0 = clear, 1 = set, -1 = don't change
 */
static int ser_set_modem_info(struct m68k_async_struct *info,int new_dtr,int new_rts)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_set_modem(dtr=%s, rts=%s)\n",(new_dtr == 0) ? "OFF" : "ON",(new_rts == 0) ? "OFF" : "ON");
#endif

	if (new_dtr == 0)	ser_DTRoff(hypercom1);
	else if (new_dtr == 1)	ser_DTRon(hypercom1);

	if (new_rts == 0)	ser_RTSoff(hypercom1);
	else if (new_rts == 1)	ser_RTSon(hypercom1);

	return 0;
}

/*-------------------------------------------------------------------------*/

/*
 *   ioctl(): Process any port-specific ioctl's. This pointer may be
 *      NULL, if the port has no own ioctl's.
 */
static int ser_ioctl(struct tty_struct *tty,struct file *file,
	struct m68k_async_struct *info,unsigned int cmd,unsigned long arg)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_ioctl()\n");
#endif
	return -ENOIOCTLCMD;
}

/*-------------------------------------------------------------------------*/

/*
 *   stop_receive(): Turn off the Rx part of the port, so no more characters
 *      will be received. This is called before shutting the port down.
 */
static void ser_stop_receive(struct m68k_async_struct *info)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_stop_receive()\n");
#endif
	/* Disable uart receive and status interrupts */
	hypercom1->IER &= ~(IER_RHR | IER_RLS | IER_MSI);
}

/*-------------------------------------------------------------------------*/

/*
 *   trans_empty(): Return !=0 if there are no more characters still to be
 *      sent out (Tx buffer register and FIFOs empty)
 */
static int ser_trans_empty(struct m68k_async_struct *info)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_trans_empty()\n");
#endif
	return (hypercom1->LSR & LSR_THE);
}

/*-------------------------------------------------------------------------*/

static void ser_reset_port(void)
{
#ifdef HYPERCOM1_DEBUG
	printk("ser_reset_port()\n");
#endif
	hypercom1->IER=0x00;	/* disable interrupts */
	hypercom1->MCR=MCR_OP2;	/* master int */

	(void)hypercom1->ISR;
	(void)hypercom1->LSR;
	(void)hypercom1->MSR;

	hypercom1->LCR=LCR_DLE;
	hypercom1->DLL=0x30;
	hypercom1->DLM=0x00;

	hypercom1->LCR=0x03;	/* 8Bits, no parity, 1 stop bits, and disable latch enable */
	hypercom1->FCR=FCR_FE | FCR_RFR | FCR_XFR | fifo_trigger_level;
}

/*-------------------------------------------------------------------------*/

static int check_port(void)
{
	int port_found=0;

	/* A write to SCRATCH with any byte, selects hypercom3/3+ first uart. */
	hypercom1->SCR=0xaa;

	if (hypercom1->LSR & LSR_TE)
		  port_found=1;

	return port_found;
}

/*--------------------------------------------------------------------------*/

int hypercom1_init(void)
{
	unsigned long flags;
	struct serial_struct req;
	struct m68k_async_struct *info;
	
#ifdef HYPERCOM1_DEBUG
	printk("hypercom1_init\n");
#endif
        if (!(MACH_IS_AMIGA))
                return -ENODEV;

	/* Check if amiga1200 */
	if (amiga_model != AMI_1200)
		return -ENODEV;

	/* init hypercom1 pointer */
	hypercom1 = (struct STARTECH_16C650 *) (zTwoBase + HYPERCOM1_PHYSADDR);

	/* Check if port exists */
	printk("Probing for HyperCOM1, version %d.%d date %s\n",HYPERCOM1_VERSION,HYPERCOM1_REV,HYPERCOM1_DATE); 
	if (check_port() == 0) {
		printk("HyperCOM1 A1200 serial port not found\n");
		return -ENODEV;
	}
        printk("Found HyperCOM1 A1200 serial port\n");
        
	ser_reset_port();

	req.line = -1;
	req.type = SER_HYPERCOM1;
	req.port = (int) &(hypercom1->RHR);

	if ((line = register_serial( &req )) < 0) {
		printk("Cannot register Hypercom1 serial port: no free device\n");
		return -EBUSY;
	}
	info = &rs_table[line];
	info->nr_uarts = 1;
	info->sw = &hypercom1_ser_switch;

	amiga_info = info; /* static variable */

	save_flags(flags);
	cli();

	/* Install handler for EXTER interupts */
	request_irq(IRQ_AMIGA_EXTER, ser_interrupt, 0, "hypercom1 serial port", info);

	restore_flags(flags);
	return 0;
}

/************************* Module Functions ********************************/

#ifdef MODULE
int init_module(void)
{
	return hypercom1_init();
}

void cleanup_module(void)
{
#ifdef HYPERCOM1_DEBUG
	printk("Closing HyperCOM1 Device!\n");
#endif
	unregister_serial(line);

	ser_reset_port();

	free_irq(IRQ_AMIGA_EXTER,amiga_info);
}
#endif
