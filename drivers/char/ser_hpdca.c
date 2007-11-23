/*
 * Driver for the 98626/98644/internal serial interface on hp300/hp400
 * (based on the National Semiconductor INS8250/NS16550AF/WD16C552 UARTs)
 *
 * This driver was written by Peter Maydell <pmaydell@chiark.greenend.org.uk>
 * based on informaition gleaned from the NetBSD driver and the ser_ioext
 * driver. Copyright(C) Peter Maydell 05/1998.
 * The most significant difference between us and ser_ioext is that 
 * we have to worry about UARTs with no FIFO (ignoring the Amiga vs HP differences...)
 *
 * We worry about multiple boards in a system because ser_ioext does and
 * it seems the sensible thing to do. It's untested, though. Also, are we
 * duplicating work done by the hardware-independent m68k serial layer?
 * (multiple devices seems like an obvious thing to go for...)
 *
 * The driver is called hpdca because the NetBSD driver is 'dca' and 
 * I wanted something less generic than hp300...
 *
 *  N.B. On the hp700 and some hp300s, there is a "secret bit" with
 *  undocumented behavior.  The third bit of the Modem Control Register
 *  (MCR_IEN == 0x08) must be set to enable interrupts.  Failure to do
 *  so can result in deadlock on those machines, whereas there don't seem to
 *  be any harmful side-effects from setting this bit on non-affected
 *  machines.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/m68kserial.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/console.h>

#include <asm/blinken.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/hwtest.h>                           /* hwreg_present() */
#include <asm/io.h>                               /* readb(), writeb() */
#include <linux/dio.h>

#include "ser_hpdca.h"

/* Set these to 0 when the driver is finished */
#define HPDCA_DEBUG 1
#define DEBUG_IRQ 1
#define DEBUG 1
#define DEBUG_FLOW 1

#define FIFO_TRIGGER_LEVEL FIFO_TRIG_8

/***************************** Prototypes *****************************/
static void hpdca_ser_interrupt(volatile struct uart_16c550 *uart, int line, 
                                int hasfifo, int *spurious_count);
static void ser_init( struct m68k_async_struct *info );
static void ser_deinit( struct m68k_async_struct *info, int leave_dtr );
static void ser_enab_tx_int( struct m68k_async_struct *info, int enab_flag );
static int  ser_check_custom_divisor(struct m68k_async_struct *info,
				     int baud_base, int divisor);
static void ser_change_speed( struct m68k_async_struct *info );
static void ser_throttle( struct m68k_async_struct *info, int status );
static void ser_set_break( struct m68k_async_struct *info, int break_flag );
static void ser_get_serial_info( struct m68k_async_struct *info,
				struct serial_struct *retinfo );
static unsigned int ser_get_modem_info( struct m68k_async_struct *info );
static int ser_set_modem_info( struct m68k_async_struct *info, int new_dtr,
			      int new_rts );
static void ser_stop_receive(struct m68k_async_struct *info);
static int ser_trans_empty(struct m68k_async_struct *info);
/************************* End of Prototypes **************************/

/*
 * SERIALSWITCH structure for the dca
 */

static SERIALSWITCH hpdca_ser_switch = {
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
        NULL,
        ser_stop_receive, ser_trans_empty, NULL
};

/* table of divisors to use for various baud rates. DCABRD() is a macro
 * which gives the correct ratio for the clock speed (hp300 vs hp700)
 */
static int hpdca_baud_table[16] = {
 	/* B0     */ 0, /* Never use this value !!! */
	/* B50    */ DCABRD(50),
	/* B75    */ DCABRD(75),
	/* B110   */ DCABRD(110),
	/* B134	  */ DCABRD(134),
	/* B150	  */ DCABRD(150),
	/* B200	  */ DCABRD(200),
	/* B300	  */ DCABRD(300),
	/* B600	  */ DCABRD(600),
	/* B1200  */ DCABRD(1200),
	/* B1800  */ DCABRD(1800),
	/* B2400  */ DCABRD(2400),
	/* B4800  */ DCABRD(4800),
	/* B9600  */ DCABRD(9600),
	/* B19200 */ DCABRD(19200),
	/* B38400 */ DCABRD(38400)   /* The last of the standard rates.  */
};

int hpdca_num;
hpdcaInfoType hpdca_info[MAX_HPDCA];

/*
 * Functions
 *
 * dev_id = hpdca_info.
 *
 */
static void hpdca_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        /* We have to take this interrupt and work out which board it's for. */
        int b;

        if (hpdca_num == 0) {
                /* This interrupt can't be for us */
                return;
        }
   
        for (b = 0; b < hpdca_num; b++) {
                hpdca_struct *board = hpdca_info[b].board;
                hpdcaInfoType *board_info = &hpdca_info[b];

                if ((board->dca_ic & (IC_IR|IC_IE)) != (IC_IR|IC_IE))
                        /* interrupts not enabled, not for us */
                        continue;
      
                /* Service any uart irqs. */
                hpdca_ser_interrupt(board_info->uart, board_info->line, 
                                    board_info->hasfifo, &board_info->spurious_count);

                if (board_info->spurious_count > 10000) {
                        board->dca_ic &= ~IC_IE;
                        printk("Too many spurious interrupts, disabling board irq\n");
                        board_info->spurious_count = 0;
                }
        } /* for b */
}

/*
** hpdca_ser_interrupt()
** 
** Check for and handle interrupts for this uart.
*/
static void hpdca_ser_interrupt(volatile struct uart_16c550 *uart, int line, 
                                int hasfifo, int *spurious_count)
{
        struct m68k_async_struct *info = rs_table + line;

        u_char iir;
        u_char lsr;
        int ch;
        
        iir = uart->IIR;

        ++*spurious_count;

#if DEBUG_IRQ
        printk("ttyS%d: IER=%02X, IIR=%02X, LSR=%02X, MSR=%02X\n", 
               line, uart->IER, iir, uart->LSR, uart->MSR);
#endif

        while (!(iir & IRQ_PEND)) {
                /* IRQ for this uart */
#if DEBUG_IRQ
                printk("IRQ_PEND on ttyS%d...\n", line);
#endif
                
                /* This really is our interrupt */
                *spurious_count = 0;
                
                switch (iir & (IRQ_ID1 | IRQ_ID2 | IRQ_ID3)) {
                case IRQ_RLS: /* Receiver Line Status */
#if DEBUG_FLOW
                        printk("RLS irq on ttyS%d\n", line);
#endif
                case IRQ_CTI: /* Character Timeout */
                case IRQ_RDA: /* Received Data Available */
#if DEBUG_IRQ
                        printk("irq (IIR=%02X) on ttyS%d\n", line, iir);
#endif
                        /*
                         * Copy chars to the tty-queue ...
                         * Be careful that we aren't passing one of the
                         * Receiver Line Status interrupt-conditions without noticing.
                         */
                        if (!hasfifo)
                        {
                                /* with no FIFO reads are trivial */
                                ch = uart->RBR;
                                rs_receive_char(info, ch, 0);
#ifdef DEBUG_IRQ
                                printk("Read a char from FIFOless uart: %02X\n", ch);
#endif
                        }
                        else
                        {
                                int got = 0;

                                lsr = uart->LSR;
#if DEBUG_IRQ
                                printk("uart->LSR & DR = %02X\n", lsr & DR);
#endif
                                while (lsr & DR) {
                                        u_char err = 0;
                                        ch = uart->RBR;
#if DEBUG_IRQ
                                        printk("Read a char from the uart: %02X, lsr=%02X\n", 
                                               ch, lsr);
#endif
                                        if (lsr & BI) {
                                                err = TTY_BREAK;
                                        }
                                        else if (lsr & PE) {
                                                err = TTY_PARITY;
                                        }
                                        else if (lsr & OE) {
                                                err = TTY_OVERRUN;
                                        }
                                        else if (lsr & FE) {
                                                err = TTY_FRAME;
                                        }
#if DEBUG_IRQ
                                        printk("rs_receive_char(ch=%02X, err=%02X)\n", ch, err);
#endif
                                        rs_receive_char(info, ch, err);
                                        got++;
                                        lsr = uart->LSR;
                                }
#if DEBUG_FLOW
                                printk("[%d<]", got);
#endif
                        }
                        break;

                case IRQ_THRE: /* Transmitter holding register empty */
                {
                        int fifo_space = (hasfifo ? 16 : 1);      /* no FIFO => send one char */
                        int sent = 0;
                        
#if DEBUG_IRQ
                        printk("THRE-irq for ttyS%d\n", line);
#endif

                        /* If the uart is ready to receive data and there are chars in */
                        /* the queue we transfer all we can to the uart's FIFO         */
                        if (info->xmit_cnt <= 0 || info->tty->stopped ||
                            info->tty->hw_stopped) {
                                /* Disable transmitter empty interrupt */
                                uart->IER &= ~(ETHREI);
                                /* Need to send a char to acknowledge the interrupt */
                                uart->THR = 0;
#if DEBUG_FLOW
                                if (info->tty->hw_stopped) {
                                        printk("[-]");
                                }
                                if (info->tty->stopped) {
                                        printk("[*]");
                                }
#endif
                                break;
                        }

                        /* Handle software flow control */
                        if (info->x_char) {
#if DEBUG_FLOW
                                printk("[^%c]", info->x_char + '@');
#endif
                                uart->THR = info->x_char;
                                info->x_char = 0;
                                fifo_space--;
                                sent++;
                        }

                        /* Fill the fifo */
                        while (fifo_space > 0) {
                                fifo_space--;
#if DEBUG_IRQ
                                printk("Sending %02x to the uart.\n", 
                                       info->xmit_buf[info->xmit_tail]);
#endif
                                uart->THR = info->xmit_buf[info->xmit_tail++];
                                sent++;
                                info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
                                if (--info->xmit_cnt == 0) {
                                        break;
                                }
                        }
#if DEBUG_FLOW
                        printk("[>%d]", sent);
#endif

                        if (info->xmit_cnt == 0) {
#if DEBUG_IRQ
                                printk("Sent last char - turning off THRE interrupts\n");
#endif
                                /* Don't need THR interrupts any more */
                                uart->IER &= ~(ETHREI);
                        }

                        if (info->xmit_cnt < WAKEUP_CHARS) {
                                rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
                        }
                }
                break;

                case IRQ_MS: /* Must be modem status register interrupt? */
                {
                        u_char msr = uart->MSR;
#if DEBUG_IRQ
                        printk("MS-irq for ttyS%d: %02x\n", line, msr);
#endif

                        if (info->flags & ASYNC_INITIALIZED) {
                                if (msr & DCTS) {
                                        rs_check_cts(info, (msr & CTS)); /* active high */
#if DEBUG_FLOW
                                        printk("[%c-%d]", (msr & CTS) ? '^' : 'v', 
                                               info->tty ? info->tty->hw_stopped : -1);
#endif
                                }
                                if (msr & DDCD) {
#if DEBUG
                                        printk("rs_dcd_changed(%d)\n", !(msr & DCD));
#endif
                                        rs_dcd_changed(info, !(msr & DCD)); /* active low */
                                }
                        }
                }
                break;

                default:
#if DEBUG_IRQ
                        printk("Unexpected irq for ttyS%d\n", line);
#endif
                        break;
                } /* switch (iir) */
                iir = uart->IIR;
        } /* while IRQ_PEND */
}

static void ser_init(struct m68k_async_struct *info)
{
#if DEBUG
	printk("ser_init\n");
#endif

	while ((curruart(info)->LSR) & DR) {
#if HPDCA_DEBUG
		printk("Emptying uart\n");
#endif
		(void)curruart(info)->RBR;
	}

	/* Set DTR and RTS */
	curruart(info)->MCR |= (DTR | RTS | OUT2);

        /* Enable interrupts. IF_EXTER irq has already been enabled in hpdca_init()*/
        /* DON'T enable ETHREI here because there is nothing to send yet (murray) */
	curruart(info)->IER |= (ERDAI | ELSI | EMSI);

	MOD_INC_USE_COUNT;
}


static void ser_deinit(struct m68k_async_struct *info, int leave_dtr)
{
#if DEBUG
	printk("ser_deinit\n");
#endif

	/* Wait for the uart to get empty */
	while(!(curruart(info)->LSR & TEMT)) {
#if HPDCA_DEBUG
		printk("Waiting for the transmitter to finish\n");
#endif
	}

	while(curruart(info)->LSR & DR) {
#if HPDCA_DEBUG
		printk("Uart not empty - flushing!\n");
#endif
		(void)curruart(info)->RBR;
	}

	/* No need to disable UART interrupts since this will already
	 * have been done via ser_enab_tx_int() and ser_stop_receive()
	 */

	ser_RTSoff(info);
	if (!leave_dtr) {
		ser_DTRoff(info);
	}

	MOD_DEC_USE_COUNT;
}

/*
** ser_enab_tx_int()
** 
** Enable or disable tx interrupts.
** Note that contrary to popular belief, it is not necessary to
** send a character to cause an interrupt to occur. Whenever the
** THR is empty and THRE interrupts are enabled, an interrupt will occur.
** (murray)
*/
static void ser_enab_tx_int(struct m68k_async_struct *info, int enab_flag)
{
	if (enab_flag) {
		curruart(info)->IER |= ETHREI;
	}
	else {
		curruart(info)->IER &= ~(ETHREI);
	}
}

static int  ser_check_custom_divisor(struct m68k_async_struct *info,
				     int baud_base, int divisor)
{
	/* Always return 0 or else setserial spd_hi/spd_vhi doesn't work */
	return 0;
}

static void ser_change_speed(struct m68k_async_struct *info)
{
	unsigned int cflag, baud, chsize, stopb, parity, aflags;
	unsigned int div = 0, ctrl = 0;

#if DEBUG
	printk("ser_change_speed\n");
#endif

	if (!info->tty || !info->tty->termios) 
                return;

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

#if DEBUG
	printk("Changing to baud-rate %i\n", baud);
#endif

	if (baud & CBAUDEX) {
		baud &= ~CBAUDEX;
		if (baud < 1 || baud > 4)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			baud += 15;
	}
        
        /* Maximum speed is 38400 */
        if (baud > 15) 
                baud = 15;
        div = hpdca_baud_table[baud];

	if (!div) {
                /* speed == 0 -> drop DTR */
#if DEBUG
                printk("Dropping DTR\n");
#endif
                ser_DTRoff(info);
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
         * The pc-guys do this as well.
         */
	ser_DTRon(info);
        
	if (chsize == CS8) {
#if DEBUG
		printk("Setting serial word-length to 8-bits\n");
#endif
		ctrl |= data_8bit;
	}
	else if (chsize == CS7) {
#if DEBUG
		printk("Setting serial word-length to 7-bits\n");
#endif
		ctrl |= data_7bit;
	}
	else if (chsize == CS6) {
#if DEBUG
		printk("Setting serial word-length to 6-bits\n");
#endif
		ctrl |= data_6bit;
	}
	else if (chsize == CS5) {
#if DEBUG
		printk("Setting serial word-length to 5-bits\n");
#endif
		ctrl |= data_5bit;
	};


	/* If stopb is true we set STB which means 2 stop-bits - */
	/* otherwise we  only get 1 stop-bit.                    */
	ctrl |= (stopb ? STB : 0);
     
        /* if parity disabled, ctrl |= 0
         * if odd parity, ctrl |= PEN
         * if even parity, ctrl |= PEN|EPS
         * Not my code -- PMM :->
         */
	ctrl |= ((parity & PARENB) ? 
                 ((parity & PARODD) ? (PEN) : (PEN | EPS)) : 0x00 ); 

#if DEBUG
	printk ("Storing serial-divisor %i\n", div);
#endif

	curruart(info)->LCR = (ctrl | DLAB);
        
	/* Store high byte of divisor */
	curruart(info)->DLM = ((div >> 8) & 0xff);
  
	/* Store low byte of divisor */

	curruart(info)->DLL = (div & 0xff);

	curruart(info)->LCR = ctrl;
}


static void ser_throttle(struct m68k_async_struct *info, int status){

#if DEBUG
	printk("ser_throttle\n");
#endif
	if (status){
		ser_RTSoff(info);
	}
	else{
		ser_RTSon(info);
	}
}


static void ser_set_break(struct m68k_async_struct *info, int break_flag)
{
#if HPDCA_DEBUG
	printk("ser_set_break\n");
#endif
	if (break_flag)
		curruart(info)->LCR |= SET_BREAK;
	else
		curruart(info)->LCR &= ~SET_BREAK;
}


static void ser_get_serial_info(struct m68k_async_struct *info,
				struct serial_struct *retinfo)
{
        int b;
   
#if DEBUG
	printk("ser_get_serial_info\n");
#endif

	retinfo->baud_base = HPDCA_BAUD_BASE;
        /* This field is currently ignored by the upper layers of
         * the serial driver, but we set it anyway :->
         * Is this really the best way to find out which board our caller
         * is referring to???
         */
        for (b = 0; b < hpdca_num; b++)
                if (hpdca_info[b].uart == curruart(info))
                        retinfo->xmit_fifo_size = (hpdca_info[b].hasfifo ? 16 : 1);

        retinfo->custom_divisor = info->custom_divisor;
}

static unsigned int ser_get_modem_info(struct m68k_async_struct *info)
{
	unsigned char msr, mcr;

#if DEBUG
	printk("ser_get_modem_info\n");
#endif

	msr = curruart(info)->MSR;
	mcr = curruart(info)->MCR; /* The DTR and RTS are located in the */
				   /* ModemControlRegister ...           */

	return(
		((mcr & DTR) ? TIOCM_DTR : 0) |
		((mcr & RTS) ? TIOCM_RTS : 0) |

		((msr & DCD) ? 0 : TIOCM_CAR) | /* DCD is active low */
		((msr & CTS) ? TIOCM_CTS : 0) |
		((msr & DSR) ? TIOCM_DSR : 0) |
		((msr & RING_I) ? TIOCM_RNG : 0)
		);
}

static int ser_set_modem_info(struct m68k_async_struct *info, int new_dtr,
			      int new_rts)
{
#if DEBUG
	printk("ser_set_modem_info new_dtr=%i new_rts=%i\n", new_dtr, new_rts);
#endif
	if (new_dtr == 0)
		ser_DTRoff(info);
	else if (new_dtr == 1)
		ser_DTRon(info);

	if (new_rts == 0)
		ser_RTSoff(info);
	else {
		if (new_rts == 1)
			ser_RTSon(info);
	}

	return 0;
}

static void ser_stop_receive (struct m68k_async_struct *info)
{
	/* Disable uart receive and status interrupts */
	curruart(info)->IER &= ~(ERDAI | ELSI | EMSI);
}

static int ser_trans_empty (struct m68k_async_struct *info)
{
	return (curruart(info)->LSR & THRE);
}

/*
** init_hpdca_uart(): init the uart. Returns 1 if UART has a FIFO, 0 otherwise
** 
*/
static int init_hpdca_uart(struct uart_16c550 *uart)
{
    /* Wait for the uart to get empty */
        while (!(uart->LSR & TEMT)) {
#if HPDCA_DEBUG
                printk("Waiting for transmitter to finish\n");
#endif
        }

        /*
         * Disable all uart interrups (they will be re-enabled in
         * ser_init when they are needed).
         */
        uart->IER = 0;
        /* Master interrupt enable - National semconductors doesn't like
         * to follow standards, so their version of the chip is
         * different and I only had a copy of their data-sheets :-(
         */
        uart->MCR = OUT2;
        
        /*
         * Set the uart to a default setting of 8N1 - 9600
         */
        uart->LCR = (data_8bit | DLAB);
        uart->DLM = 0;
        uart->DLL = 48;
        uart->LCR = (data_8bit);
        
        /* Enable + reset the tx and rx FIFO's. Set the rx FIFO-trigger count.
         * If we then find that the bits in IIR that indicate enabling of 
         * the FIFOs are zero, we conclude that this uart has no FIFOs.
         */
        uart->FCR =  (FIFO_ENA | RCVR_FIFO_RES | XMIT_FIFO_RES | FIFO_TRIGGER_LEVEL);
        udelay(100);                                  /* wait for uart to get its act together */
        return (uart->IIR & (FIFO_ENA1 | FIFO_ENA0));
}

#ifdef CONFIG_SERIAL_CONSOLE

/* 
 * Support for a serial console on the DCA port.  We used to do this in config.c
 * so it got set up early (and even if you didn't ask for serial console), but we
 * don't any more. (PB)    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *                               [I don't believe this!-- PMM]
 * 
 * However, we still want it set up earlier than the normal serial device driver,
 * so I've added a hook in m68kserial.c's serial_console_init() which is IMHO the
 * right place for it.     -- PMM
 */
static volatile struct uart_16c550 *uart = NULL;
static int conscode;

/* Warning: we get called very early, so you can't allocate memory!
 * [we could pass this function kmem_start, kmem_end if necessary]
 */
__initfunc(int hpdca_init_serial_console(int cflag))
{
        /* here cflag encodes rqd baud rate, parity, etc */
        int baud = cflag & CBAUD;
        int ctrl = 0;
        int div, scode;
        u_char stat;
        int dly;
        
        switch (cflag & CSIZE)
        {
        case CS5:
                ctrl = data_5bit;
                break;
        case CS6:
                ctrl = data_6bit;
                break;
        case CS7:
                ctrl = data_7bit;
                break;
        case CS8:
                ctrl = data_8bit;
                break;
        }
        
        ctrl |= ((cflag & PARENB) ? ((cflag & PARODD) ? 0x8 : 0x18) : 0x00);
        
        if (baud < B1200 || baud > B38400) 
                baud = B9600;  /* sensible default for unimplemented rates */
        
        div = hpdca_baud_table[baud];
        /* Unfortunately we have to do a complete scan of the DIO bus in 
         * order to locate the serial port...
         */
        for (scode = 0; scode < DIO_SCMAX; ++scode)
        {
                u_char *va;
                u_char id;
                
                /* skip the hole and the internal HPIB controller */
                if (DIO_SCINHOLE(scode) || DIO_ISIHPIB(scode))
                        continue;
                
                va = dio_scodetoviraddr(scode);
                if (!va)
                        continue;
                
                if (!hwreg_present(va + DIO_IDOFF))         /* nothing there */
                        continue;

                id = DIO_ID(va);                            /* get the ID byte */
                
                if (id != DIO_ID_DCA0 && id != DIO_ID_DCA0REM 
                    && id != DIO_ID_DCA1 && id != DIO_ID_DCA1REM)
                        continue;
                
                printk("Console is HP DCA at select code %d\n", scode);
                /* OK, this is the DCA, set it up */
                writeb(0xff, va);                 /* DCA reset */
                /* should udelay(100) here */
                for(dly = 0; dly < 10000; dly++)
                        barrier();                /* defeat GCC optimisation! */
                
                uart = (struct uart_16c550 *)(va+16); /* offset of the UART on the board */
                conscode = scode;                 /* save so we don't init this board again */
                
                break;
        }
        
        if (!uart)                                /* no DCA detected, abort */
                return 0;

        uart->IER = 0;                            /* disable interrupts */
        uart->MCR = OUT2;                         /* master interrupt enable */
        uart->LCR = (ctrl | DLAB);                /* usual messing around to set divisor */
        uart->DLM = ((div >> 8) & 0xff);
        uart->DLL = (div & 0xff);
        uart->LCR = ctrl;
        for(dly = 0; dly < 10000; dly++)
                barrier();                        /* defeat GCC optimisation! */
        stat = uart->IIR;                         /* ack any interrupts raised */

        return 1;                                 /* success */
}
	
static inline void hpdca_out(char c)
{
        u_char stat;
        int timo;
      
        timo = 50000;
        while (!(uart->LSR & THRE) && --timo)
                barrier();
        uart->THR = c;
        timo = 1500000;
        while (!(uart->LSR & THRE) && --timo)          /* wait for Tx to complete */
                barrier();
        stat = uart->IIR;                             /* ack any generated interrupts */
}


void hpdca_serial_console_write(struct console *co, const char *str,
				       unsigned int count)
{
        while (count--) {
                if (*str == '\n')
                        hpdca_out('\r');
                hpdca_out(*str++);
        }
}

int hpdca_serial_console_wait_key(struct console *co)
{
        u_char stat;

        while(!(uart->LSR & DR))
                barrier();
        stat = uart->IIR;                         /* clear any generated ints */
        return(uart->RBR);
}

#ifdef UNDEF
static struct console hpdca_console_driver = {
	"debug",
	hpdca_serial_console_write,
	NULL, /* read */
	NULL, /* device */
	hpdca_serial_console_wait_key,
	NULL, /* unblank */
	NULL, /* setup */
	CON_PRINTBUFFER,
	-1,
	0,
	NULL
};
#endif /* UNDEF */
#endif /* CONFIG_SERIAL_CONSOLE */

/*
 * Detect and initialize all HP dca boards in this system.
 */
__initfunc(int hpdca_init(void))
{
	int isr_installed = 0;
	unsigned int scode = 0;
	static char support_string[50] = "HP 98644A dca serial";
        int ipl;

        if (!MACH_IS_HP300)
		return -ENODEV;

	memset(hpdca_info, 0, sizeof(hpdca_info));

	for (;;) {
		hpdca_struct *board;
		int line;
                struct uart_16c550 *uart;
                struct serial_struct req;
                
                /* We detect boards by looking for DIO boards which match a
                 * given subset of IDs. dio_find() returns the board's scancode.
                 * The scancode to physaddr mapping is a property of the hardware,
                 * as is the scancode to IPL (interrupt priority) mapping.
                 */
                scode = dio_find(DIO_ID_DCA0);
                if (!scode)
                        scode = dio_find(DIO_ID_DCA0REM);
                if (!scode)
                        scode = dio_find(DIO_ID_DCA1);
                if (!scode)
                        scode = dio_find(DIO_ID_DCA1REM);
                if (!scode)
                        break;                    /* no, none at all */

#ifdef HPDCA_DEBUG                
                printk("Found DCA scode %d",scode);
#endif                
                board = (hpdca_struct *) dio_scodetoviraddr(scode);
                ipl = dio_scodetoipl(scode);
#ifdef HPDCA_DEBUG
                printk(" at viraddr %08lX, ipl %d\n",(u_long)board, ipl);
#endif
                hpdca_info[hpdca_num].board = board;
                hpdca_info[hpdca_num].scode = scode;
#ifdef CONFIG_SERIAL_CONSOLE
                if (scode == conscode)
                {
                        printk("Whoops, that's the console!\n");
                        dio_config_board(scode);
                        /* What are we actually supposed to do here??? */
                        continue;
                }
#endif
                /* Reset the DCA */
                board->dca_reset = 0xff;
                udelay(100);
                
		/* Register the serial port device. */
                uart = &board->uart;

                req.line = -1; /* first free ttyS? device */
                req.type = SER_HPDCA;
                req.port = (int)uart;             /* yuck */
                if ((line = register_serial(&req)) < 0) {
                        printk( "Cannot register HP dca serial port: no free device\n" );
                        break;
                }

                /* Add this uart to our hpdca_info[] table */
                hpdca_info[hpdca_num].line = line;
                hpdca_info[hpdca_num].uart = uart;

                rs_table[line].sw = &hpdca_ser_switch;

                /* We don't use these values */
                rs_table[line].nr_uarts = 0;
                rs_table[line].board_base = board;
                
                /* init the UART proper and find out if it's got a FIFO */
                hpdca_info[hpdca_num].hasfifo = init_hpdca_uart(uart);
                
		/* Install ISR if it hasn't been installed already */
		if (!isr_installed) {
			request_irq(ipl, hpdca_interrupt, 0,
				    support_string, hpdca_info);
			isr_installed++;
		}
		hpdca_num++;
                
                /* tell the DIO code that this board is configured */
                dio_config_board(scode);
        }

	return(0);
}

#ifdef MODULE
int init_module(void)
{
	return(hpdca_init());
}

void cleanup_module(void)
{
        int i;

        for (i = 0; i < hpdca_num; i++) {
                hpdca_struct *board = hpdca_info[i].board;
                int j;

                /* Disable board-interrupts */
                board->dca_ic &= ~IC_IE;
                
                /* Disable "master" interrupt select on uart */
                board->uart.MCR = 0;

                /* Disable all uart interrupts */
                board->uart.IER = 0;
    
                unregister_serial(hpdca_info[i].line);
                
                dio_unconfig_board(board->scode);
        }
        
        if (hpdca_num != 0) {
                /* Indicate to the IRQ handler that nothing needs to be serviced */
                hpdca_num = 0;
                free_irq(dio_scodetoipl(hpdca_info[0].scode), hpdca_info);
        }
}
#endif
