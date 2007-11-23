/*
 *	MACII	ADB keyboard handler.
 *		Copyright (c) 1997 Alan Cox
 *
 *	Derived from code 
 *		Copyright (C) 1996 Paul Mackerras.
 *
 *	MSch (9/97) Partial rewrite of interrupt handler to MacII style
 *		ADB handshake, based on:
 *			- Guide to Mac Hardware
 *			- Guido Koerber's session with a logic analyzer
 *
 *	MSch (1/98) Integrated start of IIsi driver by Robert Thompson
 *
 *      1999-05-30 (JMT) - Added support for reading/writing the PRAM clock.
 *			   We really need to clean up the reply_expected usage
 *			   in here so that the special cases in adb_interrupt
 *			   for faking replies can be removed.
 *
 *      1999-06-10 (JMT) - Wedged in some IOP support. This is really getting
 *			   messy...
 *
 *      1999-06-11 (JMT) - IOP support sort of working. Autopolling of the
 *			   keyboard works but if we transmit and get a timeout
 *			   we'll get bombarded with timeout message from the
 *			   IOP and I don't know how to make them stop. Thus
 *			   I have disabled transmission until I can figure out
 *			   how to handle the timeouts properly.
 *      1999-06-12 (JMT) - Rewrote IOP support to match changes in IOP
 *			   architecture. Also discovered that you need to send
 *			   a sensible reply to an autopoll packet or else the
 *			   keyboard will stop sending packets. I chose to send
 *			   back what we received, as that was what we were
 *			   essentially doing before the IOP rewrite and it
 *			   seemed to work.
 */
 
#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/adb.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/setup.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>
#include <asm/mac_iop.h>
#include <asm/adb_iop.h>

#define MACII		/* For now - will be a switch */

/* Bits in B data register: all active low */
#define TREQ		0x08		/* Transfer request (input) */
#define TACK		0x10		/* Transfer acknowledge (output) */
#define TIP		0x20		/* Transfer in progress (output) */

/* Bits in B data register: ADB transaction states MacII */
#define ST_MASK		0x30		/* mask for selecting ADB state bits */
/* ADB transaction states according to GMHW */
#define ST_CMD		0x00		/* ADB state: command byte */
#define ST_EVEN		0x10		/* ADB state: even data byte */
#define ST_ODD		0x20		/* ADB state: odd data byte */
#define ST_IDLE		0x30		/* ADB state: idle, nothing to send */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */
#define SR_DATA		0x08		/* Shift register data */
#define SR_CLOCK	0x10		/* Shift register clock */

/* JRT */
#define ADB_DELAY 150

#ifdef DEBUG_ADB_INTS
static int nclock, ndata;
#endif

static int need_poll    = 0;
static int command_byte = 0;
static int last_reply   = 0;
static int last_active  = 0;

static struct adb_request *retry_req;

static struct adb_handler {
    void (*handler)(unsigned char *, int, struct pt_regs *);
} adb_handler[16];

static enum adb_state {
    idle,
    sent_first_byte,
    sending,
    reading,
    read_done,
    awaiting_reply
} adb_state;

static struct adb_request *current_req;
static struct adb_request *last_req;
static unsigned char cuda_rbuf[16];
static unsigned char *reply_ptr;
static int reply_len;
static int reading_reply;
static int data_index;
static int first_byte;
static int prefix_len;

static int status = ST_IDLE|TREQ;
static int last_status;

static int driver_running = 0;

/*static int adb_delay;*/
int in_keybinit = 1;

static void adb_start(void);
static void adb_interrupt(int irq, void *arg, struct pt_regs *regs);
static void adb_cuda_interrupt(int irq, void *arg, struct pt_regs *regs);
static void adb_pm_interrupt(int irq, void *arg, struct pt_regs *regs);
#ifdef DEBUG_ADB_INTS
static void adb_clock_interrupt(int irq, void *arg, struct pt_regs *regs);
static void adb_data_interrupt(int irq, void *arg, struct pt_regs *regs);
#endif
static void adb_input(unsigned char *buf, int nb, struct pt_regs *regs);
static void adb_iop_receive(struct iop_msg *, struct pt_regs *);

static void adb_hw_setup_macII(void);
static void adb_hw_setup_IIsi(void);
static void adb_hw_setup_cuda(void);
static void adb_hw_setup_iop(void);
static void adb_hw_setup_pm(void);

void adb_retransmit(int);

/*
 * debug level 10 required for ADB logging (should be && debug_adb, ideally)
 */

extern int console_loglevel;

/*
 * Misc. defines for testing - should go to header :-(
 */

#define ADBDEBUG_STATUS		(1)
#define ADBDEBUG_STATE		(2)
#define ADBDEBUG_READ		(4)
#define ADBDEBUG_WRITE		(8)
#define ADBDEBUG_START		(16)
#define ADBDEBUG_RETRY		(32)
#define ADBDEBUG_POLL		(64)
#define ADBDEBUG_INT		(128)
#define ADBDEBUG_PROT		(256)
#define ADBDEBUG_SRQ		(512)
#define ADBDEBUG_REQUEST	(1024)
#define ADBDEBUG_INPUT		(2048)
#define ADBDEBUG_DEVICE		(4096)

#define ADBDEBUG_IISI		(8192)


/*#define DEBUG_ADB*/

#ifdef DEBUG_ADB
#define ADBDEBUG	(ADBDEBUG_INPUT | ADBDEBUG_READ | ADBDEBUG_START | ADBDEBUG_WRITE | ADBDEBUG_SRQ | ADBDEBUG_REQUEST)
#else
#define ADBDEBUG	(0)
#endif

#define TRY_CUDA

void adb_bus_init(void)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	/*
	 *	Setup ADB
	 */
	 
	switch(macintosh_config->adb_type)
	{
	
		case MAC_ADB_II:
			printk(KERN_INFO "adb: MacII style keyboard/mouse driver.\n");
			adb_hw_setup_macII(); 
			request_irq(IRQ_MAC_ADB, adb_interrupt, IRQ_FLG_LOCK, 
				    "adb interrupt", adb_interrupt);
			adb_state = idle;
			break;
		case MAC_ADB_IISI:
			printk("adb: Using IIsi hardware.\n");
			adb_hw_setup_IIsi();
			request_irq(IRQ_MAC_ADB, adb_cuda_interrupt, IRQ_FLG_LOCK, 
				    "adb interrupt", adb_cuda_interrupt);
			adb_state = idle;
 			break;
		case MAC_ADB_CUDA:
			printk(KERN_INFO "adb: CUDA interface.\n");
			
			adb_hw_setup_cuda();
			request_irq(IRQ_MAC_ADB, adb_cuda_interrupt, IRQ_FLG_LOCK, 
				    "adb CUDA interrupt", adb_cuda_interrupt);
			adb_state = idle;
			break;
		case MAC_ADB_IOP:
			printk("adb: using IOP for ADB...good luck.\n");
			iop_listen(ADB_IOP, ADB_CHAN, adb_iop_receive, "ADB");
			adb_hw_setup_iop();
			adb_state = idle;
			break;
	        case MAC_ADB_PB1:
        	case MAC_ADB_PB2:
			printk("adb: using PowerManager for ADB... you are doomed.\n");
			adb_hw_setup_pm();
			request_irq(IRQ_MAC_ADB, adb_pm_interrupt, IRQ_FLG_LOCK, 
				    "adb PowerManager interrupt", adb_pm_interrupt);
			adb_state = idle;
			break;
		default:
			printk("adb: Unknown hardware interface.\n");
			restore_flags(flags);	
			return;
	}

#ifdef DEBUG_ADB_INTS
	request_irq(IRQ_MAC_ADB_CL, adb_clock_interrupt, IRQ_FLG_LOCK, 
		    "adb clock interrupt", adb_clock_interrupt);
	request_irq(IRQ_MAC_ADB_SD, adb_data_interrupt, IRQ_FLG_LOCK, 
		    "adb data interrupt", adb_data_interrupt);
#endif

	printk("adb: init done.\n");
	restore_flags(flags);	
} 
	
/* Set up IOP ADB */

void adb_hw_setup_iop(void)
{
#if 0
	volatile struct adb_request req;
#endif

	printk("adb-iop: hardware setup in progress.\n");

#if 0
	adb_request((struct adb_request *) &req, NULL,
			2, ADB_PACKET, AIF_RESET);
	while (!req.complete);
#endif

	printk("adb-iop: hardware setup done!\n");
}

/* Strip the adb message out of an IOP message */

static void adb_from_iopmsg(struct iop_msg *src, struct adb_iopmsg *dst,
				int is_reply)
{
	if (is_reply) {
		memcpy(dst, &src->reply[0], sizeof(struct adb_iopmsg));
	} else {
		memcpy(dst, &src->message[0], sizeof(struct adb_iopmsg));
	}
}

/*
 * Receive an ADB message from the IOP.
 *
 * This will be called in two cases:
 *
 * 1. A message has been successfully sent to the IOP.
 * 4. An unsolicited message was received from the IOP.
 */

void adb_iop_receive(struct iop_msg *msg, struct pt_regs *regs)
{
	struct adb_iopmsg amsg;
	struct adb_request *req;
	unsigned char packet[16];
	int i;

	req = current_req;

	if (msg->status == IOP_MSGSTATUS_COMPLETE) {
		if (req->reply_expected) {
			adb_from_iopmsg(msg, &amsg, 1);
			req->reply_len = amsg.count + 1;
			req->reply[0] = ADB_PACKET;
			for (i = 0 ; i < sizeof(amsg.data) ; i++) {
				req->reply[i+1] = amsg.data[i];
			}
		}
		req->complete = 1;
		current_req = req->next;
		if (req->done) (*req->done)(req);
		adb_state = idle;
	} else if (msg->status == IOP_MSGSTATUS_UNSOL) {
		adb_from_iopmsg(msg, &amsg, 0);
		if (amsg.flags & ADB_IOP_TIMEOUT) {	/* timeout */
			printk("adb_iop_receive: timeout, retransmitting to %d\n", last_active);
			if (last_active == -1)
				last_active = (amsg.cmd&0xf0)>>4;
			if (current_req) {
				adb_start();
			} else {
				adb_retransmit(last_active);
			}
		} else {
			/* fake a CUDA-format packet */
			packet[0] = ADB_PACKET;	/* packet type  */
			packet[1] = 0;		/* ????         */
			packet[2] = amsg.cmd;	/* command byte */
			i = 0;
			while (i < amsg.count) {
				packet[i+3] = amsg.data[i];
				i++;
			}
			adb_input(packet, amsg.count + 3, regs);
			memcpy(&msg->reply[0], &msg->message[0], IOP_MSG_LEN);
		}
		iop_complete_message(msg);
		adb_state = idle;
	} else {
		printk("adb_iop_receive: unknown IOP message state %d.\n",
			msg->status);
	}
}

void adb_hw_setup_cuda(void)
{
	int		x;
	unsigned long	flags;

	printk("CUDA: HW Setup:");

	save_flags(flags);
	cli();

	if (console_loglevel == 10) 
		printk("  1,");

	/* Set the direction of the cuda signals, TIP+TACK are output TREQ is an input */
	via_write( via1, vDirB, via_read( via1, vDirB ) | TIP | TACK );
	via_write( via1, vDirB, via_read( via1, vDirB ) & ~TREQ );

	if (console_loglevel == 10) 
		printk("2,");

	/* Set the clock control. Set to shift data in by external clock CB1 */
	via_write( via1, vACR, ( via_read(via1, vACR ) | SR_EXT ) & ~SR_OUT );

	if (console_loglevel == 10) 
		printk("3,");

	/* Clear any possible Cuda interrupt */
	x = via_read( via1, vSR );

	if (console_loglevel == 10) 
		printk("4,");

	/* Terminate transaction and set idle state */
	via_write( via1, vBufB, via_read( via1, vBufB ) | TIP | TACK );

	if (console_loglevel == 10) 
		printk("5,");

	/* Delay 4 mS for ADB reset to complete */
	udelay(4000);

	if (console_loglevel == 10) 
		printk("6,");

	/* Clear pending interrupts... */
	x = via_read( via1, vSR );

	if (console_loglevel == 10) 
		printk("7,");
	/* Issue a sync transaction, TACK asserted while TIP negated */
	via_write( via1, vBufB, via_read( via1, vBufB ) & ~TACK );

	if (console_loglevel == 10) 
		printk("8,");

	/* Wait for the sync acknowledgement, Cuda to assert TREQ */
	while( ( via_read( via1, vBufB ) & TREQ ) != 0 )
		barrier();

	if (console_loglevel == 10) 
		printk("9,");

	/* Wait for the sync acknowledment interrupt */
	while( ( via_read( via1, vIFR ) & SR_INT ) == 0 )
		barrier();

	if (console_loglevel == 10) 
		printk("10,");

	/* Clear pending interrupts... */
	x = via_read( via1, vSR );

	if (console_loglevel == 10) 
		printk("11,");

	/* Terminate the sync cycle by negating TACK */
	via_write( via1, vBufB, via_read( via1, vBufB ) | TACK );

	if (console_loglevel == 10) 
		printk("12,");

	/* Wait for the sync termination acknowledgement, Cuda to negate TREQ */
	while( ( via_read( via1, vBufB ) & TREQ ) == 0 )
		barrier();

	if (console_loglevel == 10) 
		printk("13,");

	/* Wait for the sync termination acknowledment interrupt */
	while( ( via_read( via1, vIFR ) & SR_INT ) == 0 )
		barrier();

	if (console_loglevel == 10) 
		printk("14,");

	/* Terminate transaction and set idle state, TIP+TACK negate */
	via_write( via1, vBufB, via_read( via1, vBufB ) | TIP );

	if (console_loglevel == 10) 
		printk("15 !");

	/* Clear pending interrupts... */
	x = via_read( via1, vSR );

	restore_flags(flags);

	printk("\nCUDA: HW Setup done!\n");
}

static void adb_hw_setup_macII(void)
{
	/* Set the lines up. We want TREQ as input TACK|TIP as output */
	via_write(via1, vDirB, ((via_read(via1,vDirB)|TACK|TIP)&~TREQ));
	/*
	 * Docs suggest TREQ should be output - that seems nuts
	 * BSD agrees here :-)
	 * Setup vPCR ??
	 */

	/* Set up state: idle */
	via_write(via1, vBufB, via_read(via1, vBufB) | ST_IDLE);
	last_status = (via_read(via1, vBufB)&~ST_MASK);

	/* Shift register on input */
	via_write(via1, vACR, (via_read(via1, vACR) & ~SR_CTRL) | SR_EXT);
	/* Wipe any pending data and int */
	via_read(via1, vSR);

	/* This is interrupts on enable SR for keyboard */
	via_write(via1, vIER, IER_SET|SR_INT); 
	/* This clears the interrupt bit */
	via_write(via1, vIFR, SR_INT);
}

static void adb_IIsi_cleanup(void)
{
	int dummy;
	long poll_timeout;

	printk(KERN_DEBUG "adb_IIsi: cleanup!\n");

	/* ??? */
	udelay(ADB_DELAY);

	/* set SR to shift in */
	via_write(via1, vACR, via_read(via1, vACR ) & ~SR_OUT);
	/* disable SR int. */
	via_write(via1, vIER, IER_CLR|SR_INT);

	/* this is required, especially on faster machines */
	udelay(ADB_DELAY);

	if (!(via_read(via1, vBufB) & TREQ)) { /* IRQ on */
		/* start frame */
		via_write(via1, vBufB,via_read(via1,vBufB) | TIP);

		while (1) {
			/* poll for ADB interrupt and watch for timeout */
			/* if time out, keep going in hopes of not hanging the
			 * ADB chip - I think */
			poll_timeout = ADB_DELAY * 5;
			while ( !(via_read(via1, vIFR) & SR_INT) 
				&& (poll_timeout-- > 0) )
				dummy = via_read(via1, vBufB);

			dummy = via_read(via1, vSR);	/* reset interrupt flag */

			/* perhaps put in a check here that ignores all data
			 * after the first ADB_MAX_MSG_LENGTH bytes ??? */

			/* end of frame reached ?? */
			if (via_read(via1, vBufB) & TREQ)
				break;

			/* set ACK */
			via_write(via1,vBufB,via_read(via1, vBufB) | TACK);
			/* delay */
			udelay(ADB_DELAY);
			/* clear ACK */
			via_write(via1,vBufB,via_read(via1, vBufB) & ~TACK);
		}
		/* end frame */
		via_write(via1, vBufB,via_read(via1,vBufB) & ~TIP);
		/* probably don't need to delay this long */
		udelay(ADB_DELAY);
	}
	/* re-enable SR int. */
	via_write(via1, vIER, IER_SET|SR_INT);
}

static void adb_hw_setup_IIsi(void)
{
	int i;

	/* Set the lines up. We want TREQ as input TACK|TIP as output */
	via_write(via1, vDirB, (via_read(via1,vDirB)|TACK|TIP) & ~TREQ);

	/* Shift register on input */
	via_write(via1, vACR, (via_read(via1, vACR) & ~SR_CTRL) | SR_EXT);
	/* Wipe any pending data and int */
	via_read(via1, vSR);

	/* This is interrupts on enable SR for keyboard */
	via_write(via1, vIER, IER_SET|SR_INT); 

	/* Set initial state: idle */
	via_write(via1, vBufB, via_read(via1, vBufB) & ~ST_IDLE);
	last_status = (via_read(via1, vBufB)&~ST_MASK);

	/* This clears the interrupt bit */
	via_write(via1, vIFR, SR_INT);

	/* get those pesky clock ticks we missed while booting */
	for ( i = 0; i < 60; i++) {
		udelay(ADB_DELAY);
		adb_IIsi_cleanup();
		udelay(ADB_DELAY);
		if (via_read(via1, vBufB) & TREQ)
			break;
	}
	if (i == 60)
		printk("adb_IIsi: maybe bus jammed ??\n");
}

static void adb_hw_setup_pm(void)
{
}

#define WAIT_FOR(cond, what)				\
    do {						\
	for (x = 1000; !(cond); --x) {			\
	    if (x == 0) {				\
		printk("Timeout waiting for " what);	\
		return 0;				\
	    }						\
	    __delay(100*160);					\
	}						\
    } while (0)

/* 
 *	Construct and send an adb request 
 *	This function is the main entry point into the ADB driver from 
 *	kernel code; it takes the request data supplied and populates the
 *	adb_request structure.
 *	In order to keep this interface independent from any assumption about
 *	the underlying ADB hardware, we take requests in CUDA format here, 
 *	the ADB packet 'prefixed' with a packet type code.
 *	Non-CUDA hardware is confused by this, so we strip the packet type
 *	here depending on hardware type ...
 */
int adb_request(struct adb_request *req, void (*done)(struct adb_request *),
		int nbytes, ...)
{
	va_list list;
	int i, start;

	va_start(list, nbytes);

	/*
	 * skip first byte if not CUDA 
	 */
	if (macintosh_config->adb_type == MAC_ADB_II) {
/*
	    (macintosh_config->adb_type == MAC_ADB_IOP)) {
*/
		start =  va_arg(list, int);
		nbytes--;
	}
	req->nbytes = nbytes;
	req->done = done;
#if (ADBDEBUG & ADBDEBUG_REQUEST)
	if (console_loglevel == 10)
		printk("adb_request, data bytes: ");
#endif
	for (i = 0; i < nbytes; ++i) {
		req->data[i] = va_arg(list, int);
#if (ADBDEBUG & ADBDEBUG_REQUEST)
		if (console_loglevel == 10)
			printk("%x ", req->data[i]);
#endif
	}
#if (ADBDEBUG & ADBDEBUG_REQUEST)
	if (console_loglevel == 10)
		printk(" !\n");
#endif
	va_end(list);
	/* 
	 * XXX: This might be fatal if no reply is generated (i.e. Listen) ! 
	 * Currently, the interrupt handler 'fakes' a reply on non-TALK 
	 * commands for this reason.
	 * Also, we need a CUDA_AUTOPOLL emulation here for non-CUDA 
	 * Macs, and some mechanism to remember the last issued TALK
	 * request for resending it repeatedly on timeout!
	 */
	req->reply_expected = 1;
	return adb_send_request(req);
}

/* 
 *	Construct an adb request for later sending
 *	This function only populates the adb_request structure, without 
 *	actually queueing it.
 *	Reason: Poll requests and Talk requests need to be handled in a way
 *	different from 'user' requests; no reply_expected is set and 
 *	Poll requests need to be placed at the head of the request queue.
 *	Using adb_request results in implicit queueing at the tail of the 
 *	request queue (duplicating the Poll) with reply_expected set.
 *	No adjustment of packet data is necessary, as this mechanisnm is not
 *	used by CUDA hardware (Autopoll used instead).
 */
int adb_build_request(struct adb_request *req, void (*done)(struct adb_request *),
	     int nbytes, ...)
{
	va_list list;
	int i;

	req->nbytes = nbytes;
	req->done = done;
	va_start(list, nbytes);
#if (ADBDEBUG & ADBDEBUG_REQUEST)
	if (console_loglevel == 10)
		printk("adb__build_request, data bytes: ");
#endif
	/*
	 * skip first byte if not CUDA ?
	 */
	for (i = 0; i < nbytes; ++i) {
		req->data[i] = va_arg(list, int);
#if (ADBDEBUG & ADBDEBUG_REQUEST)
		if (console_loglevel == 10)
			printk("%x ", req->data[i]);
#endif
	}
#if (ADBDEBUG & ADBDEBUG_REQUEST)
	if (console_loglevel == 10)
		printk(" !\n");
#endif
	va_end(list);

	req->reply_expected = 0;
	return 0;
}

/*
 *	Send an ADB poll (Talk, tagged on the front of the request queue)
 */
void adb_queue_poll(void)
{
	static int pod=0;
	static int in_poll=0;
	static struct adb_request r;
	unsigned long flags;

	if(in_poll)
		printk("Double poll!\n");

	in_poll++;
	pod++;
	if(pod>7) /* 15 */
		pod=0;

#if (ADBDEBUG & ADBDEBUG_POLL)
	if (console_loglevel == 10)
		printk("adb: Polling %d\n",pod);
#endif

	if (macintosh_config->adb_type == MAC_ADB_II)
		/* XXX: that's a TALK, register 0, MacII version */
		adb_build_request(&r,NULL, 1, (pod<<4|0xC));
	else
		/* CUDA etc. version */
		adb_build_request(&r,NULL, 2, 0, (pod<<4|0xC));

	r.reply_expected=0;
	r.done=NULL;
	r.sent=0;
	r.complete=0;
	r.reply_len=0;
	save_flags(flags);
	cli();
	/* Poll inserted at head of queue ... */
	r.next=current_req;
	current_req=&r;
	restore_flags(flags);
	adb_start();
	in_poll--;
}

/*
 *	Send an ADB retransmit (Talk, appended to the request queue)
 */
void adb_retransmit(int device)
{
	static int in_retransmit=0;
	static struct adb_request rt;
	unsigned long flags;

	if(in_retransmit)
		printk("Double retransmit!\n");

	in_retransmit++;

#if (ADBDEBUG & ADBDEBUG_POLL)
	if (console_loglevel == 10)
		printk("adb: Sending retransmit: %d\n", device);
#endif

	/* MacII version */
	adb_build_request(&rt,NULL, 1, (device<<4|0xC));

	rt.reply_expected = 0;
	rt.done           = NULL;
	rt.sent           = 0;
	rt.complete      = 0;
	rt.reply_len      = 0;
	rt.next           = NULL;

	save_flags(flags);
	cli();

	/* Retransmit inserted at tail of queue ... */

	if (current_req != NULL) 
	{
		last_req->next = &rt;
		last_req = &rt;
	}
	else
	{
		current_req = &rt;
		last_req = &rt;
	}

	/* always restart driver (send_retransmit used in place of adb_start!)*/

	if (adb_state == idle)
		adb_start();

	restore_flags(flags);
	in_retransmit--;
}

/*
 * Queue an ADB request; start ADB transfer if necessary
 */
int adb_send_request(struct adb_request *req)
{
	unsigned long flags;

	req->next = 0;
	req->sent = 0;
	req->complete = 0;
	req->reply_len = 0;
	save_flags(flags); 
	cli();

	if (current_req != NULL) 
	{
		last_req->next = req;
		last_req = req;
	}
	else
	{
		current_req = req;
		last_req = req;
		if (adb_state == idle)
			adb_start();
	}

	restore_flags(flags);
	return 0;
}

/*
 * Start sending an ADB packet, IOP style
 *
 * There isn't much to do other than hand the packet over to the IOP
 * after encapsulating it in an adb_iopmsg.
 */

static void adb_start_iop(void)
{
	unsigned long flags;
	struct adb_request *req;
	struct adb_iopmsg amsg;
	int reply_expected,i,nb;

	/* get the packet to send */
	req = current_req;

	/* TODO: re-enable transmits when timeout handling is fixed */

	req->sent = 1;
	req->complete = 1;
	if (req->done) (*req->done)(req);
	return;

	/* assert adb_state == idle */
	if (adb_state != idle) {
		printk("adb-iop: adb_start_iop called while driver busy (%p %x)!\n",
			req, adb_state);
		return;
	}

	if (req->data[0] != ADB_PACKET) {
		printk("abb-iop: attempt to send non-ADB packet through IOP\n");
		return;
	}

	if (req == 0) return;
	save_flags(flags); 
	cli();
	
	/* Always expecting a reply could be fatal since it could clog */
	/* the receive channel, not to mention it won't reset the adb  */
	/* state back to idle since adb_iop_receive won't be called.   */
	/* It's best to play it safe; if we aren't sure then don't     */
	/* set reply_expected; it won't work right but at least it     */
	/* won't clog the ADB subsystem.                               */

	if ((req->data[1] & 0x0C) == AIF_TALK) {
		reply_expected = 1;
	} else {
		reply_expected = 0;
	}

	/* The IOP appears to take MacII-style packets, not CUDA packets */

	nb = req->nbytes - 2;
	if (nb > sizeof(amsg.data)) nb = sizeof(amsg.data);

	printk("adb-iop: sending packet, %d bytes:", nb);

	amsg.flags = ADB_IOP_EXPLICIT;
	amsg.cmd   = req->data[1];
	amsg.count = nb;

	printk(" %02X", (uint) amsg.cmd);

	i = 0;
	while(i < nb) {
		amsg.data[i] = req->data[i+2];
		printk(" %02X", (uint) amsg.data[i]);
		i++;
	}
	printk("\n");

	/* Now send it. The IOP manager will call */
	/* adb_iop_receive when  the reply comes  */
	/* or when the send is completed if no    */
	/* reply is expected.                     */

	iop_send_message(ADB_IOP, ADB_CHAN, req,
			 sizeof(amsg), (__u8 *) &amsg, adb_iop_receive);

	adb_state = sending;

	restore_flags(flags);
}

/*
 * Start sending ADB packet 
 */
static void adb_start(void)
{
	unsigned long flags;
	struct adb_request *req;

	if (macintosh_config->adb_type == MAC_ADB_IOP) {
		adb_start_iop();
		return;
	}

	/*
	 * We get here on three 'sane' conditions:
	 * 1) called from send_adb_request, if adb_state == idle
	 * 2) called from within adb_interrupt, if ade_state == idle 
	 *    (after receiving, or after sending a LISTEN) 
	 * 3) called from within adb_interrupt, if adb_state == sending 
	 *    and no reply is expected (immediate next command).
	 * Maybe we get here on SRQ as well ??
	 */

	/* get the packet to send */
	req = current_req;
	/* assert adb_state == idle */
	if (adb_state != idle) {
		printk("ADB: adb_start called while driver busy (%p %x %x)!\n",
			req, adb_state, via_read(via1, vBufB)&(ST_MASK|TREQ));
		return;
	}
	if (req == 0)
		return;
	save_flags(flags); 
	cli();
	
#if (ADBDEBUG & ADBDEBUG_START)
	if (console_loglevel == 10)
		printk("adb_start: request %p ", req);
#endif

#ifdef DEBUG_ADB_INTS
	nclock = 0;
	ndata  = 0;
#endif

	/* 
	 * IRQ signaled ?? (means ADB controller wants to send, or might 
	 * be end of packet if we were reading)
	 */
	if ((via_read(via1, vBufB) & TREQ) == 0) 
	{
		switch(macintosh_config->adb_type)
		{
			/*
			 *	FIXME - we need to restart this on a timer
			 *	or a collision at boot hangs us.
			 *	Never set adb_state to idle here, or adb_start 
			 *	won't be called again from send_request!
			 *	(need to re-check other cases ...)
			 */
			case MAC_ADB_CUDA:
				/* printk("device busy - fail\n"); */
				restore_flags(flags);
				/* a byte is coming in from the CUDA */
				return;
			case MAC_ADB_IISI:
				printk("adb_start: device busy - fail\n");
				retry_req = req;
				restore_flags(flags);
				return;
			case MAC_ADB_II:
				/*
				 * if the interrupt handler set the need_poll
				 * flag, it's hopefully a SRQ poll or re-Talk
				 * so we try to send here anyway
				 */
				if (!need_poll) {
					printk("device busy - retry %p state %d status %x!\n", 
						req, adb_state, via_read(via1, vBufB)&(ST_MASK|TREQ));
					retry_req = req;
					/* set ADB status here ? */
					restore_flags(flags);
					return;
				} else {
#if (ADBDEBUG & ADBDEBUG_START)
					if (console_loglevel == 10)
						printk("device busy - polling; state %d status %x!\n", 
							adb_state, via_read(via1, vBufB)&(ST_MASK|TREQ));
#endif
					need_poll = 0;
					break;
				}
		}
	}

#if 0
	/*
	 * Bus idle ?? Not sure about this one; SRQ might need ST_CMD here!
	 * OTOH: setting ST_CMD in the interrupt routine would make the 
	 * ADB contoller shift in before this routine starts shifting out ...
	 */
	if ((via_read(via1, vBufB)&ST_MASK) != ST_IDLE) 
	{
#if (ADBDEBUG & ADBDEBUG_STATE)
		if (console_loglevel == 10)
			printk("ADB bus not idle (%x), retry later!\n", 
				via_read(via1, vBufB)&(ST_MASK|TREQ));
#endif
		retry_req = req;
		restore_flags(flags);
		return;							
	}
#endif

	/*
	 * Another retry pending? (sanity check)
	 */
	if (retry_req) {
#if (ADBDEBUG & ADBDEBUG_RETRY)
		if (console_loglevel == 10)
		if (retry_req == req)
			/* new requests are appended at tail of request queue */
			printk("adb_start: retry %p pending ! \n", req);
		else
			/* poll requests are added to the head of queue */
			printk("adb_start: retry %p pending, req %p (poll?) current! \n",
				retry_req, req);
#endif
		retry_req = NULL;
	}

	/*
	 * Seems OK, go for it!
	 */
	switch(macintosh_config->adb_type)
	{
		case MAC_ADB_CUDA:
			/* store command byte (first byte is 'type' byte) */
			command_byte = req->data[1];
			/* set the shift register to shift out and send a byte */
			via_write(via1, vACR, via_read(via1, vACR)|SR_OUT); 
			via_write(via1, vSR, req->data[0]);
			via_write(via1, vBufB, via_read(via1, vBufB)&~TIP);
			break;
		case MAC_ADB_IISI:
			/* store command byte (first byte is 'type' byte) */
			command_byte = req->data[1];
			/* set ADB state to 'active' */
			via_write(via1, vBufB, via_read(via1, vBufB) | TIP);
			/* switch ACK off (in case it was left on) */
			via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
			/* set the shift register to shift out and send a byte */
			via_write(via1, vACR, via_read(via1, vACR) | SR_OUT); 
			via_write(via1, vSR, req->data[0]);
			/* signal 'byte ready' */
			via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
			break;
		case MAC_ADB_II:
			/* store command byte */
			command_byte = req->data[0];
			/* Output mode */
			via_write(via1, vACR, via_read(via1, vACR)|SR_OUT);
			/* Load data */
			via_write(via1, vSR, req->data[0]);
#ifdef USE_ORIG
			/* Turn off TIP/TACK - this should tell the external logic to
			   start the external shift clock */
/*			via_write(via1, vBufB, via_read(via1, vBufB)&~(TIP|TACK));*/
			via_write(via1, vBufB, via_read(via1, vBufB)|(TIP|TACK));
#else
			/* set ADB state to 'command' */
			via_write(via1, vBufB, (via_read(via1, vBufB)&~ST_MASK)|ST_CMD);
#endif
			break;
	}
	adb_state = sent_first_byte;
	data_index = 1;
#if (ADBDEBUG & ADBDEBUG_START)
	if (console_loglevel == 10)
		printk("sent first byte of %d: %x, (%x %x) ... ", 
			req->nbytes, req->data[0], adb_state, 
			(via_read(via1, vBufB) & (ST_MASK|TREQ)) );
#endif
	restore_flags(flags);
}

/*
 * Poll the ADB state (maybe obsolete now that interrupt-driven ADB runs)
 */
void adb_poll(void)
{
	unsigned char c;
	unsigned long flags;
	save_flags(flags);
	cli();
	c=via_read(via1, vIFR);
#if (ADBDEBUG & ADBDEBUG_POLL)
#ifdef DEBUG_ADB_INTS
	if (console_loglevel == 10) {
		printk("adb_poll: IFR %x state %x cl %d dat %d ", 
			c, adb_state, nclock, ndata);
		if (c & (SR_CLOCK|SR_DATA)) {
			if (c & SR_CLOCK)
				printk("adb clock event ");
			if (c & SR_DATA)
				printk("adb data event ");
		}
	}
#else
	if (console_loglevel == 10)
		printk("adb_poll: IFR %x state %x ", 
			c, adb_state);
#endif
	if (console_loglevel == 10)
		printk("\r");
#endif
	if (c & SR_INT)
	{
#if (ADBDEBUG & ADBDEBUG_POLL)
		if (console_loglevel == 10)
			printk("adb_poll: adb interrupt event\n");
#endif
		adb_interrupt(0, 0, 0);
	}
	restore_flags(flags);
}

#ifdef DEBUG_ADB_INTS
/*
 * Debugging gimmicks
 */
void adb_clock_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	nclock++;
}

void adb_data_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	ndata++;
}
#endif

/*
 * The notorious ADB interrupt handler - does all of the protocol handling, 
 * except for starting new send operations. Relies heavily on the ADB 
 * controller sending and receiving data, thereby generating SR interrupts
 * for us. This means there has to be always activity on the ADB bus, otherwise
 * the whole process dies and has to be re-kicked by sending TALK requests ...
 * CUDA-based Macs seem to solve this with the autopoll option, for MacII-type
 * ADB the problem isn't solved yet (retransmit of the latest active TALK seems
 * a good choice; either on timeout or on a timer interrupt).
 *
 * The basic ADB state machine was left unchanged from the original MacII code
 * by Alan Cox, which was based on the CUDA driver for PowerMac. 
 * The syntax of the ADB status lines seems to be totally different on MacII, 
 * though. MacII uses the states Command -> Even -> Odd -> Even ->...-> Idle for
 * sending, and Idle -> Even -> Odd -> Even ->...-> Idle for receiving. Start 
 * and end of a receive packet are signaled by asserting /IRQ on the interrupt
 * line. Timeouts are signaled by a sequence of 4 0xFF, with /IRQ asserted on 
 * every other byte. SRQ is probably signaled by 3 or more 0xFF tacked on the 
 * end of a packet. (Thanks to Guido Koerber for eavesdropping on the ADB 
 * protocol with a logic analyzer!!)
 * CUDA seems to use /TIP -> /TIP | TACK -> /TIP -> /TIP | TACK ... -> TIP|TACK
 * for sending, and /TIP -> /TIP | TACK -> /TIP -> /TIP | TACK ... -> TIP for
 * receiving. No clue how timeouts are handled; SRQ seems to be sent as a 
 * separate packet. Quite a few changes have been made outside the handshake 
 * code, so I don't know if the CUDA code still behaves as before. 
 *
 * Note: As of 21/10/97, the MacII ADB part works including timeout detection
 * and retransmit (Talk to the last active device). Cleanup of code and 
 * testing of the CUDA functionality is required, though. 
 * Note2: As of 13/12/97, CUDA support is definitely broken ...
 * Note3: As of 21/12/97, CUDA works on a P475. What was broken? The assumption
 * that Q700 and Q800 use CUDA :-(
 *
 * 27/01/98: IIsi driver implemented (thanks to Robert Thompson for the 
 * initial bits). See adb_cuda_interrupts ...
 *
 * Next TODO: implementation of IIsi ADB protocol (maybe the USE_ORIG 
 * conditionals can be a start?)
 */
void adb_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	int x, adbdir;
	unsigned long flags;
	struct adb_request *req;

	last_status = status;

	/* prevent races due to SCSI enabling ints */
	save_flags(flags);
	cli();

	if (driver_running) {
		restore_flags(flags);
		return;
	}

	driver_running = 1;
	
#ifdef USE_ORIG
	status = (~via_read(via1, vBufB) & (TIP|TREQ)) | (via_read(via1, vACR) & SR_OUT);
#else
	if (macintosh_config->adb_type==MAC_ADB_CUDA)
		status = (~via_read(via1, vBufB) & (TIP|TREQ)) | (via_read(via1, vACR) & SR_OUT);
	else
		/* status bits (0x8->0x20) and direction (0x10 ??) CLASH !! */
		status = (via_read(via1, vBufB) & (ST_MASK|TREQ)); 
#endif
	adbdir = (via_read(via1, vACR) & SR_OUT);
#if (ADBDEBUG & ADBDEBUG_INT)
	if (console_loglevel == 10)
		printk("adb_interrupt: state=%d status=%x last=%x direction=%x\n", 
			adb_state, status, last_status, adbdir);
#endif

	switch (adb_state) 
	{
		case idle:
			if(macintosh_config->adb_type==MAC_ADB_CUDA)
			{
				/* CUDA has sent us the first byte of data - unsolicited */
				if (status != TREQ)
					printk("cuda: state=idle, status=%x\n", status);
				x = via_read(via1, vSR); 
				via_write(via1, vBufB, via_read(via1,vBufB)&~TIP);
			}
			else if(macintosh_config->adb_type==MAC_ADB_IISI)
			{
				udelay(150);
				/* set SR to IN (??? no byte received else) */
				via_write(via1, vACR,via_read(via1, vACR)&~SR_OUT); 
		 		/* signal start of frame */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TIP);
				/* read first byte */
				x = via_read(via1, vSR);
				first_byte = x;
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_macIIsi : receiving unsol. packet: %x (%x %x) ", 
						x, adb_state, status);
#endif
				/* ACK adb chip */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				udelay(150);
		 		via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
			}
			else if(macintosh_config->adb_type==MAC_ADB_II)
			{
#if (ADBDEBUG & ADBDEBUG_STATUS)
				if (status == TREQ && !adbdir) 
					/* that's: not IRQ, idle, input -> weird */
					printk("adb_macII: idle, status=%x dir=%x\n",
						status, adbdir);
#endif
				x = via_read(via1, vSR);
				first_byte = x;
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_macII: receiving unsol. packet: %x (%x %x) ", 
						x, adb_state, status);
#endif
				/* set ADB state = even for first data byte */
				via_write(via1, vBufB, (via_read(via1, vBufB)&~ST_MASK)|ST_EVEN);
			}
			adb_state = reading;
			reply_ptr = cuda_rbuf;
			reply_len = 0;
			reading_reply = 0;
			prefix_len = 0;
			if (macintosh_config->adb_type==MAC_ADB_II) {
				*reply_ptr++ = ADB_PACKET;
				*reply_ptr++ = first_byte;
				*reply_ptr++ = command_byte; /*first_byte;*/
				reply_len    = 3;
				prefix_len   = 3;
			}
			break;

		case awaiting_reply:
			if(macintosh_config->adb_type==MAC_ADB_CUDA)
			{
				/* CUDA has sent us the first byte of data of a reply */
				if (status != TREQ)
					printk("cuda: state=awaiting_reply, status=%x\n", status);
				x = via_read(via1, vSR); 
				via_write(via1,vBufB, 
					via_read(via1, vBufB)&~TIP);
			} 
			else if(macintosh_config->adb_type==MAC_ADB_IISI)
			{
				/* set SR to IN */
				via_write(via1, vACR,via_read(via1, vACR)&~SR_OUT); 
		 		/* signal start of frame */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TIP);
				/* read first byte */
				x = via_read(via1, vSR);
				first_byte = x;
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_macIIsi: reading reply: %x (%x %x) ",
						x, adb_state, status);
#endif
#if 0
				if( via_read(via1,vBufB) & TREQ)
					ending = 1;
				else
					ending = 0;
#endif
				/* ACK adb chip */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				udelay(150);
		 		via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
			}
			else if(macintosh_config->adb_type==MAC_ADB_II) 
			{
				/* handshake etc. for II ?? */
				x = via_read(via1, vSR);
				first_byte = x;
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_macII: reading reply: %x (%x %x) ",
						x, adb_state, status);
#endif
				/* set ADB state = even for first data byte */
				via_write(via1, vBufB, (via_read(via1, vBufB)&~ST_MASK)|ST_EVEN);
			}
			adb_state = reading;			
			reply_ptr = current_req->reply;
			reading_reply = 1;
			reply_len  = 0;
			prefix_len = 0;
			if (macintosh_config->adb_type==MAC_ADB_II) {
				*reply_ptr++ = ADB_PACKET;
				*reply_ptr++ = first_byte;
				*reply_ptr++ = first_byte; /* should be command  byte */
				reply_len    = 3;
				prefix_len   = 3;
			}
			break;

		case sent_first_byte:
#if (ADBDEBUG & ADBDEBUG_WRITE)
			if (console_loglevel == 10)
				printk(" sending: %x (%x %x) ",
					current_req->data[1], adb_state, status);
#endif
			if(macintosh_config->adb_type==MAC_ADB_CUDA)
			{
				if (status == TREQ + TIP + SR_OUT) 
				{
					/* collision */
					via_write(via1, vACR,
						via_read(via1, vACR)&~SR_OUT); 
					x = via_read(via1, vSR); 
					via_write(via1, vBufB,
						via_read(via1,vBufB)|TIP|TACK); 
					adb_state = idle;
				}
				else
				{
					/* assert status == TIP + SR_OUT */
					if (status != TIP + SR_OUT)
						printk("cuda: state=sent_first_byte status=%x\n", status);
					via_write(via1,vSR,current_req->data[1]); 
					via_write(via1, vBufB,
						via_read(via1, vBufB)^TACK); 
					data_index = 2;
					adb_state = sending;
				}
			}
			else if(macintosh_config->adb_type==MAC_ADB_IISI)
			{
				/* switch ACK off */
				via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
				if ( !(via_read(via1, vBufB) & TREQ) ) 
				{
					/* collision */
#if (ADBDEBUG & ADBDEBUG_WRITE)
					if (console_loglevel == 10)
						printk("adb_macIIsi: send collison, aborting!\n");
#endif
					/* set shift in */
					via_write(via1, vACR,
						via_read(via1, vACR)&~SR_OUT); 
					/* clear SR int. */
					x = via_read(via1, vSR); 
					/* set ADB state to 'idle' */
					via_write(via1, vBufB,
						via_read(via1,vBufB) & ~(TIP|TACK)); 
					adb_state = idle;
				}
				else
				{
					/* delay */
					udelay(ADB_DELAY);
					/* set the shift register to shift out and send a byte */
#if 0
					via_write(via1, vACR, via_read(via1, vACR) | SR_OUT); 
#endif
					via_write(via1, vSR, current_req->data[1]);
					/* signal 'byte ready' */
					via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
					data_index=2;			
					adb_state = sending;
				}
			}
			else if(macintosh_config->adb_type==MAC_ADB_II)
			{
				/* how to detect a collision here ?? */
				/* maybe we're already done (Talk, or Poll)? */
				if (data_index >= current_req->nbytes) 
				{
					/* assert it's a Talk ?? */
					if ( (command_byte&0xc) != 0xc
					    && console_loglevel == 10 )
						printk("ADB: single byte command, no Talk: %x!\n", 
							command_byte);
#if (ADBDEBUG & ADBDEBUG_WRITE)
					if (console_loglevel == 10)
						printk(" -> end (%d of %d) (%x %x)!\n",
							data_index, current_req->nbytes, adb_state, status);
#endif
					current_req->sent = 1;
					if (current_req->reply_expected) 
					{
#if (ADBDEBUG & ADBDEBUG_WRITE)
						if (console_loglevel == 10)
							printk("ADB: reply expected on poll!\n");
#endif
						adb_state = awaiting_reply;
						reading_reply = 0;
					} else {
#if (ADBDEBUG & ADBDEBUG_WRITE)
						if (console_loglevel == 10)
							printk("ADB: no reply for poll, not calling done()!\n");
#endif
						req = current_req;
						current_req = req->next;
#if 0	/* XXX Not sure about that one ... probably better enabled */
						if (req->done)
							(*req->done)(req);
#endif
						adb_state = idle;
						reading_reply = 0;
					}
					/* set to shift in */
					via_write(via1, vACR,
						via_read(via1, vACR) & ~SR_OUT);
					x=via_read(via1, vSR);
					/* set ADB state idle - might get SRQ */
					via_write(via1, vBufB,
						(via_read(via1, vBufB)&~ST_MASK)|ST_IDLE);
					break;
				}
#if (ADBDEBUG & ADBDEBUG_STATUS)
				if(!(status==(ST_CMD|TREQ) && adbdir == SR_OUT))
					printk("adb_macII: sent_first_byte, weird status=%x dir=%x\n", 
					status, adbdir);
#endif
				/* SR already set to shift out; send byte */
				via_write(via1, vSR, current_req->data[1]);
				/* set state to ST_EVEN (first byte was: ST_CMD) */
				via_write(via1, vBufB,
					(via_read(via1, vBufB)&~ST_MASK)|ST_EVEN);
				data_index=2;			
				adb_state = sending;
			}
			break;

		case sending:
			req = current_req;
			if (data_index >= req->nbytes) 
			{
#if (ADBDEBUG & ADBDEBUG_WRITE)
				if (console_loglevel == 10)
					printk(" -> end (%d of %d) (%x %x)!\n",
						data_index-1, req->nbytes, adb_state, status);
#endif
				/* end of packet */
				if(macintosh_config->adb_type==MAC_ADB_CUDA)
				{
					via_write(via1, vACR,
						via_read(via1, vACR)&~SR_OUT); 
					x = via_read(via1, vSR); 
					via_write(via1, vBufB,
						via_read(via1,vBufB)|TACK|TIP); 
				}
				else if(macintosh_config->adb_type==MAC_ADB_IISI)
				{
					/* XXX maybe clear ACK here ??? */
					/* switch ACK off */
					via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
					/* delay */
					udelay(ADB_DELAY);
					/* set the shift register to shift in */
					via_write(via1, vACR, via_read(via1, vACR)|SR_OUT); 
					/* clear SR int. */
					x = via_read(via1, vSR); 
					/* set ADB state 'idle' (end of frame) */
					via_write(via1, vBufB,
						via_read(via1,vBufB) & ~(TACK|TIP)); 
				}
				else if(macintosh_config->adb_type==MAC_ADB_II)
				{
					/*
					 * XXX Not sure: maybe only switch to 
					 * input mode on Talk ??
					 */
					/* set to shift in */
					via_write(via1, vACR,
						via_read(via1, vACR) & ~SR_OUT);
					x=via_read(via1, vSR);
					/* set ADB state idle - might get SRQ */
					via_write(via1, vBufB,
						(via_read(via1, vBufB)&~ST_MASK)|ST_IDLE);
				}
				req->sent = 1;
				if (req->reply_expected) 
				{
					/* 
					 * maybe fake a reply here on Listen ?? 
					 * Otherwise, a Listen hangs on success
					 */
					if ( macintosh_config->adb_type==MAC_ADB_II
					     && ((req->data[0]&0xc) == 0xc) )
						adb_state = awaiting_reply;
					else if ( macintosh_config->adb_type != MAC_ADB_II
						  && ( req->data[0]      == 0x0) 
						  && ((req->data[1]&0xc) == 0xc) )
						adb_state = awaiting_reply;
					else {
						/*
						 * Reply expected, but none
						 * possible -> fake reply.
						 * Problem: sending next command
						 * should probably be done 
						 * without setting bus to 'idle'!
						 * (except if no more commands)
						 */
#if (ADBDEBUG & ADBDEBUG_PROT)
						printk("ADB: reply expected on Listen, faking reply\n");
#endif
						/* make it look weird */
						/* XXX: return reply_len -1? */
						/* XXX: fake ADB header? */
						req->reply[0] = req->reply[1] = req->reply[2] = 0xFF;  
						req->reply_len = 3;
						req->complete = 1;
						current_req = req->next;
						if (req->done)
							(*req->done)(req);
						/* 
						 * ready with this one, run 
						 * next command or repeat last
						 * Talk (=idle on II)
						 */
						/* set state to idle !! */
						adb_state = idle;
						if (current_req || retry_req)
							adb_start();
					}
				}
				else
				{
					current_req = req->next;
					if (req->done)
						(*req->done)(req);
					/* not sure about this */
					/* 
					 * MS: Must set idle, no new request 
					 *     started else !
					 */
					adb_state = idle;
					/* 
					 * requires setting ADB state to idle,
					 * maybe read a byte ! (done above)
					 */
					if (current_req || retry_req)
						adb_start();
				}
			}
			else 
			{
#if (ADBDEBUG & ADBDEBUG_WRITE)
				if (console_loglevel == 10)
					printk(" %x (%x %x) ",
						req->data[data_index], adb_state, status);
#endif
				if(macintosh_config->adb_type==MAC_ADB_CUDA)
				{
					via_write(via1, vSR, req->data[data_index++]); 
					via_write(via1, vBufB, 
						via_read(via1, vBufB)^TACK); 
				}
				else if(macintosh_config->adb_type==MAC_ADB_IISI)
				{
					/* switch ACK off */
					via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
					/* delay */
					udelay(ADB_DELAY);
					/* XXX: need to check for collision?? */
					/* set the shift register to shift out and send a byte */
#if 0
					via_write(via1, vACR, via_read(via1, vACR)|SR_OUT); 
#endif
					via_write(via1, vSR, req->data[data_index++]);
					/* signal 'byte ready' */
					via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				}
				else if(macintosh_config->adb_type==MAC_ADB_II)
				{
					via_write(via1, vSR, req->data[data_index++]);
					/* invert state bits, toggle ODD/EVEN */
					x = via_read(via1, vBufB);
					via_write(via1, vBufB,
						(x&~ST_MASK)|(~(x&ST_MASK) & ST_MASK));
				}
			}
			break;

		case reading:

			/* timeout / SRQ handling for II hw */
#ifdef POLL_ON_TIMEOUT
			if((reply_len-prefix_len)==3 && memcmp(reply_ptr-3,"\xFF\xFF\xFF",3)==0)
#else
			if( (first_byte == 0xFF && (reply_len-prefix_len)==2 
			     && memcmp(reply_ptr-2,"\xFF\xFF",2)==0) || 
			    ((reply_len-prefix_len)==3 
			     && memcmp(reply_ptr-3,"\xFF\xFF\xFF",3)==0))
#endif
			{
				/*
				 * possible timeout (in fact, most probably a 
				 * timeout, since SRQ can't be signaled without
				 * transfer on the bus).
				 * The last three bytes seen were FF, together 
				 * with the starting byte (in case we started
				 * on 'idle' or 'awaiting_reply') this probably
				 * makes four. So this is mostl likely #5!
				 * The timeout signal is a pattern 1 0 1 0 0..
				 * on /INT, meaning we missed it :-(
				 */
				x = via_read(via1, vSR);
				if (x != 0xFF)
					printk("ADB: mistaken timeout/SRQ!\n");

				/* 
				 * ADB status bits: either even or odd.
				 * adb_state: need to set 'idle' here.
				 * Maybe saner: set 'need_poll' or 
				 * 'need_resend' here, fall through to 
				 * read_done ??
				 */
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk(" -> read aborted: %x (%x %x)!\n", 
						x, adb_state, status);
#endif

#if 0	/* XXX leave status unchanged!! - need to check this again! */
	/* XXX Only touch status on II !!! */
				/* set ADB state to idle (required by adb_start()) */
				via_write(via1, vBufB,
					(via_read(via1, vBufB)&~ST_MASK)|ST_IDLE);
#endif

				/*
				 * What if the timeout happens on reading a 
				 * reply ?? Assemble error reply and call 
				 * current_request->done()? Keep request 
				 * on queue?
				 */

				/* prevent 'busy' in adb_start() */
				need_poll = 1;

				/*
				 * Timeout: /IRQ alternates high/low during 
				 *          4 'FF' bytes (1 0 1 0 0...)
				 *	    We're on byte 5, so we need one 
				 *	    more backlog here (TBI) ....
				 */
				if ((status&TREQ) != (last_status&TREQ)) {
#if (ADBDEBUG & ADBDEBUG_SRQ)
					if (console_loglevel == 10)
						printk("ADB: reply timeout, resending!\n");
#endif
					/*
					 * first byte received should be the 
					 * command byte timing out !!
					 */
					if (first_byte != 0xff)
						command_byte = first_byte;

					/*
					 * compute target for retransmit: if
					 * last_active is set, use that one, 
					 * else use command_byte
					 */
					if (last_active == -1)
						last_active = (command_byte&0xf0)>>4;
					adb_state = idle;
					/* resend if TALK, don't poll! */
					if (current_req)
						adb_start();
					else
					/* 
					 * XXX: need to count the timeouts ?? 
					 * restart last active TALK ??
					 * If no current_req, reuse old one!
					 */
						adb_retransmit(last_active);

				} else {
				/*
				 * SRQ: NetBSD suggests /IRQ is asserted!?
				 */
					if (status&TREQ)
						printk("ADB: SRQ signature w/o /INT!\n");
#if (ADBDEBUG & ADBDEBUG_SRQ)
					if (console_loglevel == 10)
						printk("ADB: empty SRQ packet!\n");
#endif
					/* Terminate the SRQ packet and poll */
					adb_state = idle;
					adb_queue_poll();
				}
				/*
				 * Leave ADB status lines unchanged (means /IRQ
				 * will still be low when entering adb_start!)
				 */
				break;
			}
			/* end timeout / SRQ handling for II hw. */
			if((reply_len-prefix_len)>3 && memcmp(reply_ptr-3,"\xFF\xFF\xFF",3)==0)
			{
				/* SRQ tacked on data packet */
				/* Check /IRQ here ?? */
#if (ADBDEBUG & ADBDEBUG_SRQ)
				if (console_loglevel == 10)
					printk("\nADB: Packet with SRQ!\n");
#endif
				/* Terminate the packet (SRQ never ends) */
				x = via_read(via1, vSR);
				adb_state = read_done;
				reply_len -= 3;
				reply_ptr -= 3;
				need_poll = 1;
				/* need to continue; next byte not seen else */
				/* 
				 * XXX: not at all sure here; maybe need to 
				 * send away the reply and poll immediately?
				 */
			} else {
				/* Sanity check */
				if(reply_len>15)
					reply_len=0;
				/* read byte */
				*reply_ptr = via_read(via1, vSR); 
				x = *reply_ptr;
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk(" %x (%x %x) ", 
						*reply_ptr, adb_state, status);
#endif
				reply_ptr++;
				reply_len++;
			}
			/* The usual handshake ... */
			if (macintosh_config->adb_type==MAC_ADB_CUDA)
			{
				if (status == TIP)
				{
					/* that's all folks */
					via_write(via1, vBufB,
						via_read(via1, vBufB)|TACK|TIP); 
					adb_state = read_done;
				}
				else 
				{
					/* assert status == TIP | TREQ */
					if (status != TIP + TREQ)
						printk("cuda: state=reading status=%x\n", status);
					via_write(via1, vBufB, 
						via_read(via1, vBufB)^TACK); 
				}
			}
			else if (macintosh_config->adb_type==MAC_ADB_IISI)
			{
				/* ACK adb chip (maybe check for end first?) */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				udelay(150);
		 		via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
				/* end of frame?? */
				if (status & TREQ)
				{
#if (ADBDEBUG & ADBDEBUG_READ)
					if (console_loglevel == 10)
						printk("adb_IIsi: end of frame!\n");
#endif
					/* that's all folks */
					via_write(via1, vBufB,
						via_read(via1, vBufB) & ~(TACK|TIP)); 
					adb_state = read_done;
					/* maybe process read_done here?? Handshake anyway?? */
				}
			}
			else if (macintosh_config->adb_type==MAC_ADB_II)
			{
				/*
				 * NetBSD hints that the next to last byte 
				 * is sent with IRQ !! 
				 * Guido found out it's the last one (0x0),
				 * but IRQ should be asserted already.
				 * Problem with timeout detection: First
				 * transition to /IRQ might be second 
				 * byte of timeout packet! 
				 * Timeouts are signaled by 4x FF.
				 */
				if(!(status&TREQ) && x == 0x00) /* != 0xFF */
				{
#if (ADBDEBUG & ADBDEBUG_READ)
					if (console_loglevel == 10)
						printk(" -> read done!\n");
#endif
#if 0		/* XXX: we take one more byte (why?), so handshake! */
					/* set ADB state to idle */
					via_write(via1, vBufB,
						(via_read(via1, vBufB)&~ST_MASK)|ST_IDLE);
#else
					/* invert state bits, toggle ODD/EVEN */
					x = via_read(via1, vBufB);
					via_write(via1, vBufB,
						(x&~ST_MASK)|(~(x&ST_MASK) & ST_MASK));
#endif
					/* adjust packet length */
					reply_len--;
					reply_ptr--;
					adb_state = read_done;
				}
				else
				{
#if (ADBDEBUG & ADBDEBUG_STATUS)
					if(status!=TIP+TREQ)
						printk("macII_adb: state=reading status=%x\n", status);
#endif
					/* not caught: ST_CMD */
					/* required for re-entry 'reading'! */
					if ((status&ST_MASK) == ST_IDLE) {
						/* (in)sanity check - set even */
						via_write(via1, vBufB,
							(via_read(via1, vBufB)&~ST_MASK)|ST_EVEN);
					} else {
						/* invert state bits, toggle ODD/EVEN */
						x = via_read(via1, vBufB);
						via_write(via1, vBufB,
							(x&~ST_MASK)|(~(x&ST_MASK) & ST_MASK));
					}
				}
			}
			break;

		case read_done:
			x = via_read(via1, vSR); 
#if (ADBDEBUG & ADBDEBUG_READ)
			if (console_loglevel == 10)
				printk("ADB: read done: %x (%x %x)!\n", 
					x, adb_state, status);
#endif
			if (reading_reply) 
			{
				req = current_req;
				req->reply_len = reply_ptr - req->reply;
				req->complete = 1;
				current_req = req->next;
				if (req->done)
					(*req->done)(req);
			}
			else
			{
				adb_input(cuda_rbuf, reply_ptr - cuda_rbuf, regs);
			}

			/*
			 * remember this device ID; it's the latest we got a 
			 * reply from!
			 */
			last_reply = command_byte;
			last_active = (command_byte&0xf0)>>4;

			/*
			 * Assert status = ST_IDLE ??
			 */
			/* 
			 * SRQ seen before, initiate poll now
			 */
			if (need_poll) {
#if (ADBDEBUG & ADBDEBUG_POLL)
				if (console_loglevel == 10)
					printk("ADB: initiate poll!\n");
#endif
				adb_state = idle;
				/*
				 * set ADB status bits?? (unchanged above!)
				 */
				adb_queue_poll();
				need_poll = 0;
				/* hope this is ok; queue_poll runs adb_start */
				break;
			}

			/*
			 * /IRQ seen, so the ADB controller has data for us
			 */
			if (!(status&TREQ)) 
			{
				/* set ADB state to idle */
				via_write(via1, vBufB,
					(via_read(via1, vBufB)&~ST_MASK)|ST_IDLE); 

				adb_state = reading;
				reply_ptr = cuda_rbuf;
				reply_len = 0;
				prefix_len = 0;
				if (macintosh_config->adb_type==MAC_ADB_II) {
					*reply_ptr++ = ADB_PACKET;
					*reply_ptr++ = command_byte;
					reply_len    = 2;
					prefix_len   = 2;
				}
				reading_reply = 0;
			}
			else 
			{
				/*
				 * no IRQ, send next packet or wait
				 */
				adb_state = idle;
				if (current_req)
					adb_start();
				else
					adb_retransmit(last_active);
			}
			break;

		default:
#if (ADBDEBUG & ADBDEBUG_STATE)
			printk("adb_interrupt: unknown adb_state %d?\n", adb_state);
#endif
	}
	/* reset mutex and interrupts */
	driver_running = 0;
	restore_flags(flags);
}

/*
 * Restart of CUDA support: please modify this interrupt handler while 
 * working at the Quadra etc. ADB driver. We can try to merge them later, or
 * remove the CUDA stuff from the MacII handler
 *
 * MSch 27/01/98: Implemented IIsi driver based on initial code by Robert 
 * Thompson and hints from the NetBSD driver. CUDA and IIsi seem more closely
 * related than to the MacII code, so merging all three might be a bad
 * idea.
 */

void adb_cuda_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	int x, status;
	struct adb_request *req;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if(macintosh_config->adb_type==MAC_ADB_CUDA)
		status = (~via_read(via1, vBufB) & (TIP|TREQ)) | (via_read(via1, vACR) & SR_OUT);
	else
		status = via_read(via1, vBufB) & (TIP|TREQ);

#if (ADBDEBUG & ADBDEBUG_INT)
	if (console_loglevel == 10)
		printk("adb_interrupt: state=%d status=%x\n", adb_state, status);
#endif

	switch (adb_state) 
	{
		case idle:
			first_byte = 0;
			if(macintosh_config->adb_type==MAC_ADB_CUDA)
			{
#if (ADBDEBUG & ADBDEBUG_STATUS)
				/* CUDA has sent us the first byte of data - unsolicited */
				if (status != TREQ)
					printk("cuda: state=idle, status=%x want=%x\n", 
						status, TREQ);
#endif
				x = via_read(via1, vSR); 
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_cuda: receiving unsol. packet: %x (%x %x) ", 
						x, adb_state, status);
#endif
				via_write(via1, vBufB, via_read(via1,vBufB)&~TIP);
			}
			else if(macintosh_config->adb_type==MAC_ADB_IISI)
			{
				udelay(150);
				/* set SR to IN */
				via_write(via1, vACR,via_read(via1, vACR)&~SR_OUT); 
		 		/* signal start of frame */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TIP);
				/* read first byte */
				x = via_read(via1, vSR);
				first_byte = x;
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_IIsi : receiving unsol. packet: %x (%x %x) ", 
						x, adb_state, status);
#endif
				/* ACK adb chip */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				udelay(150);
		 		via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
			}
			adb_state = reading;
			reply_ptr = cuda_rbuf;
			reply_len = 0;
			reading_reply = 0;
			break;

		case awaiting_reply:
			if(macintosh_config->adb_type==MAC_ADB_CUDA)
			{
				/* CUDA has sent us the first byte of data of a reply */
#if (ADBDEBUG & ADBDEBUG_STATUS)
				if (status != TREQ)
					printk("cuda: state=awaiting_reply, status=%x want=%x\n", 
						status, TREQ);
#endif
				x = via_read(via1, vSR); 
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_cuda: reading reply: %x (%x %x) ",
						x, adb_state, status);
#endif
				via_write(via1,vBufB, 
					via_read(via1, vBufB)&~TIP);
			}
			else if(macintosh_config->adb_type==MAC_ADB_IISI)
			{
				/* udelay(150);*/
				/* set SR to IN */
				via_write(via1, vACR,via_read(via1, vACR)&~SR_OUT); 
		 		/* signal start of frame */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TIP);
				/* read first byte */
				x = via_read(via1, vSR);
				first_byte = x;
#if (ADBDEBUG & ADBDEBUG_READ)
				if (console_loglevel == 10)
					printk("adb_IIsi: reading reply: %x (%x %x) ",
						x, adb_state, status);
#endif
#if 0
				if( via_read(via1,vBufB) & TREQ)
					ending = 1;
				else
					ending = 0;
#endif
				/* ACK adb chip */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				udelay(150);
		 		via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
			}
			adb_state = reading;			
			reply_ptr = current_req->reply;
			reading_reply = 1;
			reply_len = 0;
			break;

		case sent_first_byte:
			if(macintosh_config->adb_type==MAC_ADB_CUDA)
			{
#if (ADBDEBUG & ADBDEBUG_WRITE)
				if (console_loglevel == 10)
					printk(" sending: %x (%x %x) ",
						current_req->data[1], adb_state, status);
#endif
				if (status == TREQ + TIP + SR_OUT) 
				{
					/* collision */
					if (console_loglevel == 10)
						printk("adb_cuda: send collision!\n");
					via_write(via1, vACR,
						via_read(via1, vACR)&~SR_OUT); 
					x = via_read(via1, vSR); 
					via_write(via1, vBufB,
						via_read(via1,vBufB)|TIP|TACK); 
					adb_state = idle;
				}
				else
				{
					/* assert status == TIP + SR_OUT */
#if (ADBDEBUG & ADBDEBUG_STATUS)
					if (status != TIP + SR_OUT)
						printk("adb_cuda: state=sent_first_byte status=%x want=%x\n", 
							status, TIP + SR_OUT);
#endif
					via_write(via1,vSR,current_req->data[1]); 
					via_write(via1, vBufB,
						via_read(via1, vBufB)^TACK); 
					data_index = 2;
					adb_state = sending;
				}
			}
			else if(macintosh_config->adb_type==MAC_ADB_IISI)
			{
				/* switch ACK off */
				via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
				if ( !(via_read(via1, vBufB) & TREQ) ) 
				{
					/* collision */
#if (ADBDEBUG & ADBDEBUG_WRITE)
					if (console_loglevel == 10)
						printk("adb_macIIsi: send collison, aborting!\n");
#endif
					/* set shift in */
					via_write(via1, vACR,
						via_read(via1, vACR)&~SR_OUT); 
					/* clear SR int. */
					x = via_read(via1, vSR); 
					/* set ADB state to 'idle' */
					via_write(via1, vBufB,
						via_read(via1,vBufB) & ~(TIP|TACK)); 
					adb_state = idle;
				}
				else
				{
					/* delay */
					udelay(ADB_DELAY);
					/* set the shift register to shift out and send a byte */
					via_write(via1, vACR, via_read(via1, vACR) | SR_OUT); 
					via_write(via1, vSR, current_req->data[1]);
					/* signal 'byte ready' */
					via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
					data_index=2;			
					adb_state = sending;
				}
			}
			break;

		case sending:
			req = current_req;
			if (data_index >= req->nbytes) 
			{
#if (ADBDEBUG & ADBDEBUG_WRITE)
				if (console_loglevel == 10)
					printk(" -> end (%d of %d) (%x %x)!\n",
						data_index-1, req->nbytes, adb_state, status);
#endif
				if(macintosh_config->adb_type==MAC_ADB_CUDA)
				{
					via_write(via1, vACR,
						via_read(via1, vACR)&~SR_OUT); 
					x = via_read(via1, vSR); 
					via_write(via1, vBufB,
						via_read(via1,vBufB)|TACK|TIP); 
				}
				else if(macintosh_config->adb_type==MAC_ADB_IISI)
				{
					/* XXX maybe clear ACK here ??? */
					/* switch ACK off */
					via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
					/* delay */
					udelay(ADB_DELAY);
					/* set the shift register to shift in */
					via_write(via1, vACR, via_read(via1, vACR) & ~SR_OUT); 
					/* clear SR int. */
					x = via_read(via1, vSR); 
					/* set ADB state 'idle' (end of frame) */
					via_write(via1, vBufB,
						via_read(via1,vBufB) & ~(TACK|TIP)); 
				}
				req->sent = 1;
				if (req->reply_expected) 
				{
					/* 
					 * maybe fake a reply here on Listen ?? 
					 * Otherwise, a Listen hangs on success
					 * CUDA+IIsi: only ADB Talk considered
					 */
					if ( (req->data[0] == 0x0) && ((req->data[1]&0xc) == 0xc) )
						adb_state = awaiting_reply;
					else if ((req->data[0] == 0x1) && (req->data[1] == 0x3) )
						adb_state = awaiting_reply;
					else {
						/*
						 * Reply expected, but none
						 * possible -> fake reply.
						 */
#if (ADBDEBUG & ADBDEBUG_PROT)
						printk("ADB: reply expected on Listen, faking reply\n");
#endif
						/* make it look weird */
						/* XXX: return reply_len -1? */
						/* XXX: fake ADB header? */
						req->reply[0] = req->reply[1] = req->reply[2] = 0xFF;  
						req->reply_len = 3;
						req->complete = 1;
						current_req = req->next;
						if (req->done)
							(*req->done)(req);
						/* 
						 * ready with this one, run 
						 * next command !
						 */
						/* set state to idle !! */
						adb_state = idle;
						if (current_req || retry_req)
							adb_start();
					}
				}
				else
				{
					current_req = req->next;
					if (req->done)
						(*req->done)(req);
					/* not sure about this */
					adb_state = idle;
					adb_start();
				}
			}
			else 
			{
#if (ADBDEBUG & ADBDEBUG_WRITE)
				if (console_loglevel == 10)
					printk(" %x (%x %x) ",
						req->data[data_index], adb_state, status);
#endif
				if(macintosh_config->adb_type==MAC_ADB_CUDA)
				{
					via_write(via1, vSR, req->data[data_index++]); 
					via_write(via1, vBufB, 
						via_read(via1, vBufB)^TACK); 
				}
				else if(macintosh_config->adb_type==MAC_ADB_IISI)
				{
					/* switch ACK off */
					via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
					/* delay */
					udelay(ADB_DELAY);
					/* XXX: need to check for collision?? */
					/* set the shift register to shift out and send a byte */
#if 0
					via_write(via1, vACR, via_read(via1, vACR)|SR_OUT); 
#endif
					via_write(via1, vSR, req->data[data_index++]);
					/* signal 'byte ready' */
					via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				}
			}
			break;

		case reading:
			if(reply_len==3 && memcmp(reply_ptr-3,"\xFF\xFF\xFF",3)==0)
			{
				/* Terminate the SRQ packet */
#if (ADBDEBUG & ADBDEBUG_SRQ)
				if (console_loglevel == 10)
					printk("adb: Got an SRQ\n");
#endif
				adb_state = idle;
				adb_queue_poll();
				break;
			}
			/* Sanity check - botched in orig. code! */
			if(reply_len>15) {
				printk("adb_cuda: reply buffer overrun!\n");
				/* wrap buffer */
				reply_len=0;
				if (reading_reply)
					reply_ptr = current_req->reply;
				else
					reply_ptr = cuda_rbuf;
			}
			*reply_ptr = via_read(via1, vSR); 
#if (ADBDEBUG & ADBDEBUG_READ)
			if (console_loglevel == 10)
				printk(" %x (%x %x) ", 
					*reply_ptr, adb_state, status);
#endif
			reply_ptr++;
			reply_len++;
			if(macintosh_config->adb_type==MAC_ADB_CUDA)
			{
				if (status == TIP)
				{
					/* that's all folks */
					via_write(via1, vBufB,
						via_read(via1, vBufB)|TACK|TIP); 
					adb_state = read_done;
				}
				else 
				{
					/* assert status == TIP | TREQ */
#if (ADBDEBUG & ADBDEBUG_STATUS)
					if (status != TIP + TREQ)
						printk("cuda: state=reading status=%x want=%x\n", 
							status, TIP + TREQ);
#endif
					via_write(via1, vBufB, 
						via_read(via1, vBufB)^TACK); 
				}
			}
			else if (macintosh_config->adb_type==MAC_ADB_IISI)
			{
				/* ACK adb chip (maybe check for end first?) */
		 		via_write(via1, vBufB, via_read(via1, vBufB) | TACK);
				udelay(150);
		 		via_write(via1, vBufB, via_read(via1, vBufB) & ~TACK);
				/* end of frame?? */
				if (status & TREQ)
				{
#if (ADBDEBUG & ADBDEBUG_READ)
					if (console_loglevel == 10)
						printk("adb_IIsi: end of frame!\n");
#endif
					/* that's all folks */
					via_write(via1, vBufB,
						via_read(via1, vBufB) & ~(TACK|TIP)); 
					adb_state = read_done;
					/* XXX maybe process read_done here?? 
					   Handshake anyway?? */
				}
			}
			/* fall through for IIsi on end of frame */
			if (macintosh_config->adb_type != MAC_ADB_IISI
			    || adb_state != read_done)
				break;

		case read_done:
			x = via_read(via1, vSR); 
#if (ADBDEBUG & ADBDEBUG_READ)
			if (console_loglevel == 10)
				printk("adb: read done: %x (%x %x)!\n", 
					x, adb_state, status);
#endif
			if (reading_reply) 
			{
				req = current_req;
				req->reply_len = reply_ptr - req->reply;
				req->complete = 1;
				current_req = req->next;
				if (req->done)
					(*req->done)(req);
			}
			else
			{
				adb_input(cuda_rbuf, reply_ptr - cuda_rbuf, regs);
			}

			if (macintosh_config->adb_type==MAC_ADB_CUDA 
			      &&   status & TREQ) 
			{
				via_write(via1, vBufB,
					via_read(via1, vBufB)&~TIP); 
				adb_state = reading;
				reply_ptr = cuda_rbuf;
				reading_reply = 0;
			}
			else if  (macintosh_config->adb_type==MAC_ADB_IISI
			      && !(status & TREQ))
			{
				udelay(150);
				via_write(via1, vBufB,
					via_read(via1, vBufB) | TIP); 
				adb_state = reading;
				reply_ptr = cuda_rbuf;
				reading_reply = 0;
			}
			else 
			{
				adb_state = idle;
				adb_start();
			}
			break;

		default:
			printk("adb_cuda_interrupt: unknown adb_state %d?\n", adb_state);
	}

	restore_flags(flags);

}

static void adb_pm_interrupt(int irq, void *arg, struct pt_regs *regs)
{
	printk("adb_pm_interrupt called!\n");
}

/*
 *	The 'reply delivery' routine; determines which device sent the 
 *	request and calls the appropriate handler. 
 *	Reply data are expected in CUDA format (again, argh...) so we need
 *	to fake this in the interrupt handler for MacII.
 *
 *      Note!  The PowerPC code now expects data in ADB format, and
 *      has their CUDA handler throw non-ADB packets on the floor.  We
 *      are going to sync with them.  It is only a matter of time.
 *
 *	Only one handler per device ID is currently possible.
 *	XXX: is the ID field here representing the default or real ID?
 */
static void adb_input(unsigned char *buf, int nb, struct pt_regs *regs)
{
	int id;

	switch (buf[0]) 
	{
		case ADB_PACKET:
			/* what's in buf[1] ?? */
			/* according to via-cuda.c, buf[1] should tell
			   us whether to autopoll or not */
			id = buf[2] >> 4;
#if (ADBDEBUG & ADBDEBUG_INPUT)
			if (console_loglevel == 10) {
				int i;
				printk("adb_input: adb packet ");
				for (i = 0; i < nb; ++i)
					printk(" %x", buf[i]);
				printk(", id = %d\n", id);
			}
#endif
			if (adb_handler[id].handler != 0) 
			{
				(*adb_handler[id].handler)(buf, nb, regs);
			}
			break;

		default:
#if (ADBDEBUG & ADBDEBUG_INPUT)
			if (console_loglevel == 10) {
				int i;
				printk("adb_input: data from via (%d bytes):", nb);
				for (i = 0; i < nb; ++i)
					printk(" %.2x", buf[i]);
				printk("\n");
			}
#endif
	}
}

/* Ultimately this should return the number of devices with
   the given default id. */

int adb_register(int default_id,
	     void (*handler)(unsigned char *, int, struct pt_regs *))
{
	if (adb_handler[default_id].handler != 0)
		panic("Two handlers for ADB device %d\n", default_id);
	adb_handler[default_id].handler = handler;
	return 1;
}

/*
 * /dev/adb device driver.
 */

#define ADB_MAJOR	56	/* major number for /dev/adb */

#define ADB_MAX_MINOR	64	/* range of ADB minors */
#define ADB_TYPE_SHIFT	4	/* # bits for device ID/type in subdevices */

#define ADB_TYPE_RAW	0	/* raw device; unbuffered */
#define ADB_TYPE_BUFF	1	/* raw device; buffered */
#define ADB_TYPE_COOKED	2	/* 'cooked' device */


extern void adbdev_init(void);

struct adbdev_state {
	struct adb_request req;
};

static struct wait_queue *adb_wait;

static int adb_wait_reply(struct adbdev_state *state, struct file *file)
{
	int ret = 0;
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&adb_wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (!state->req.complete) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&adb_wait, &wait);

	return ret;
}

static void adb_write_done(struct adb_request *req)
{
	if (!req->complete) {
		req->reply_len = 0;
		req->complete = 1;
	}
	wake_up_interruptible(&adb_wait);
}

struct file_operations *adb_raw[16];
struct file_operations *adb_buffered[16];
struct file_operations *adb_cooked[16];

static int adb_open(struct inode *inode, struct file *file)
{
	int adb_subtype;
	struct adbdev_state *state;

	if (MINOR(inode->i_rdev) > ADB_MAX_MINOR)
		return -ENXIO;

	switch (MINOR(inode->i_rdev) >> ADB_TYPE_SHIFT) {
		case ADB_TYPE_RAW:
			/* see code below */
			break;
		case ADB_TYPE_BUFF:
			/* TBI */
			return -ENXIO;
		case ADB_TYPE_COOKED:
			/* subtypes such as kbd, mouse, ... */
			adb_subtype = MINOR(inode->i_rdev) & ~ADB_TYPE_SHIFT;
			if ((file->f_op = adb_cooked[adb_subtype]))
				return file->f_op->open(inode,file);
			else
				return -ENODEV;
	}

	state = kmalloc(sizeof(struct adbdev_state), GFP_KERNEL);
	if (state == 0)
		return -ENOMEM;
	file->private_data = state;
	state->req.reply_expected = 0;
	return 0;
}

static int adb_release(struct inode *inode, struct file *file)
{
	struct adbdev_state *state = file->private_data;

	if (state) {
		int ret;
		
		file->private_data = NULL;
		if (state->req.reply_expected && !state->req.complete)
			if ((ret = adb_wait_reply(state, file)))
				return ret;
		kfree(state);
	}
	return 0;
}

static loff_t adb_lseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t adb_read(struct file *file, char *buf,
			size_t count, loff_t *offset)
{
	int ret;
	struct adbdev_state *state = file->private_data;

	if (count < 2)
		return -EINVAL;
	if (count > sizeof(state->req.reply))
		count = sizeof(state->req.reply);
	ret = verify_area(VERIFY_WRITE, buf, count);
	if (ret)
		return ret;

	if (!state->req.reply_expected)
		return 0;

	ret = adb_wait_reply(state, file);
	if (ret)
		return ret;

	state->req.reply_expected = 0;
	ret = state->req.reply_len;
	copy_to_user(buf, state->req.reply, ret);

	return ret;
}

static ssize_t adb_write(struct file *file, const char *buf,
			 size_t count, loff_t *offset)
{
	int ret, i;
	struct adbdev_state *state = file->private_data;

	if (count < 2 || count > sizeof(state->req.data))
		return -EINVAL;
	ret = verify_area(VERIFY_READ, buf, count);
	if (ret)
		return ret;

	if (state->req.reply_expected && !state->req.complete) {
		/* A previous request is still being processed.
		   Wait for it to finish. */
		ret = adb_wait_reply(state, file);
		if (ret)
			return ret;
	}

	state->req.nbytes = count;
	state->req.done = adb_write_done;
	state->req.complete = 0;
	copy_from_user(state->req.data, buf, count);
#if 0
	switch (adb_hardware) {
	case ADB_NONE:
		return -ENXIO;
	case ADB_VIACUDA:
		state->req.reply_expected = 1;
		cuda_send_request(&state->req);
		break;
	default:
#endif
		if (state->req.data[0] != ADB_PACKET)
			return -EINVAL;
		for (i = 1; i < state->req.nbytes; ++i)
			state->req.data[i] = state->req.data[i+1];
		state->req.reply_expected =
			((state->req.data[0] & 0xc) == 0xc);
		adb_send_request(&state->req);
#if 0
		break;
	}
#endif

	return count;
}

static struct file_operations adb_fops = {
	adb_lseek,
	adb_read,
	adb_write,
	NULL,		/* no readdir */
	NULL,		/* no poll yet */
	NULL,		/* no ioctl yet */
	NULL,		/* no mmap */
	adb_open,
	NULL,		/* flush */
	adb_release,
	NULL,		/* fsync */
	NULL,		/* fasync */
	NULL,		/* check_media_change */
	NULL,		/* revalidate */
	NULL		/* lock */
};

int adbdev_register(int subtype, struct file_operations *fops)
{
	if (subtype < 0 || subtype > 15)
		return -EINVAL;
	if (adb_cooked[subtype])
		return -EBUSY;
	adb_cooked[subtype] = fops;
	return 0;
}

int adbdev_unregister(int subtype)
{
	if (subtype < 0 || subtype > 15)
		return -EINVAL;
	if (!adb_cooked[subtype])
		return -ENODEV;
	adb_cooked[subtype] = NULL;
	return 0;
}

void adbdev_init()
{
	if (register_chrdev(ADB_MAJOR, "adb", &adb_fops))
		printk(KERN_ERR "adb: unable to get major %d\n", ADB_MAJOR);
}

#if 0 /* old ADB device */

/*
 * Here are the file operations we export for /dev/adb.
 */

#define ADB_MINOR	140	/* /dev/adb is c 10 140 */

extern void adbdev_inits(void);

struct adbdev_state {
	struct adb_request req;
};

static struct wait_queue *adb_wait;

static int adb_wait_reply(struct adbdev_state *state, struct file *file)
{
	int ret = 0;
	struct wait_queue wait = { current, NULL };

#if (ADBDEBUG & ADBDEBUG_DEVICE)
	printk("ADB request: wait_reply (blocking ... \n");
#endif

	add_wait_queue(&adb_wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (!state->req.complete) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&adb_wait, &wait);

	return ret;
}

static void adb_write_done(struct adb_request *req)
{
	if (!req->complete) {
		req->reply_len = 0;
		req->complete = 1;
	}
	wake_up_interruptible(&adb_wait);
}

static int adb_open(struct inode *inode, struct file *file)
{
	struct adbdev_state *state;

	state = kmalloc(sizeof(struct adbdev_state), GFP_KERNEL);
	if (state == 0)
		return -ENOMEM;
	file->private_data = state;
	state->req.reply_expected = 0;
	return 0;
}

static void adb_release(struct inode *inode, struct file *file)
{
	struct adbdev_state *state = file->private_data;

	if (state) {
		file->private_data = NULL;
		if (state->req.reply_expected && !state->req.complete)
			if (adb_wait_reply(state, file))
				return;
		kfree(state);
	}
	return;
}

static int adb_lseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static int adb_read(struct file *file, char *buf, size_t count,
		    loff_t *offset)
{
	int ret;
	struct adbdev_state *state = file->private_data;

	if (count < 2)
		return -EINVAL;
	if (count > sizeof(state->req.reply))
		count = sizeof(state->req.reply);
	ret = verify_area(VERIFY_WRITE, buf, count);
	if (ret)
		return ret;

	if (!state->req.reply_expected)
		return 0;

	ret = adb_wait_reply(state, file);
	if (ret)
		return ret;

	ret = state->req.reply_len;
	memcpy_tofs(buf, state->req.reply, ret);
	state->req.reply_expected = 0;

	return ret;
}

static int adb_write(struct file *file, const char *buf,
		     size_t count, loff_t *offset)
{
	int ret;
	struct adbdev_state *state = file->private_data;

	if (count < 2 || count > sizeof(state->req.data))
		return -EINVAL;
	ret = verify_area(VERIFY_READ, buf, count);
	if (ret)
		return ret;

	if (state->req.reply_expected && !state->req.complete) {
		/* A previous request is still being processed.
		   Wait for it to finish. */
		ret = adb_wait_reply(state, file);
		if (ret)
			return ret;
	}

	state->req.nbytes = count;
	state->req.done = adb_write_done;
	memcpy_fromfs(state->req.data, buf, count);
	state->req.reply_expected = 1;
	state->req.complete = 0;
	adb_send_request(&state->req);

	return count;
}

static struct file_operations adb_fops = {
	adb_lseek,
	adb_read,
	adb_write,
	NULL,		/* no readdir */
	NULL,		/* no select */
	NULL,		/* no ioctl */
	NULL,		/* no mmap */
	adb_open,
	NULL,		/* flush */
	adb_release
};

static struct miscdevice adb_dev = {
	ADB_MINOR,
	"adb",
	&adb_fops
};

void adbdev_init(void)
{
	misc_register(&adb_dev);
}

#endif /* old ADB device */
