/*
 * cpia_pp CPiA Parallel Port driver
 *
 * Supports CPiA based parallel port Video Camera's.
 *
 * (C) Copyright 1999 Bas Huisman <bhuism@cs.utwente.nl>
 * (C) Copyright 1999-2000 Scott J. Bertin <sbertin@mindspring.com>,
 * (C) Copyright 1999-2000 Peter Pregler <Peter_Pregler@email.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
#undef CONFIG_VIDEO_CPIA_PP_DMA
#endif

#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/parport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>

#ifdef CONFIG_VIDEO_CPIA_PP_DMA
#include <asm/dma.h>
#endif

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
/* If this is a module and parport_pc is not, some parport_pc_* functions
 * are not directly availbale.  parport.h messes this up.
 * This fixes what we need.
 */
#if defined(MODULE) && !defined(CONFIG_PARPORT_PC_MODULE)
#define PARPORT_NEED_GENERIC_OPS
#undef parport_enable_irq
#undef parport_disable_irq
#undef parport_read_fifo
#define parport_read_fifo(p)		(p)->ops->read_fifo(p)
#endif

/* These should be defined in linux/parport.h, but aren't.  The #ifndef
 * parport_enable_irq is in case they are at some future time.
 */
#ifdef PARPORT_NEED_GENERIC_OPS
#ifndef parport_enable_irq
#define parport_enable_irq(p) (p)->ops->enable_irq(p)
#define parport_disable_irq(p) (p)->ops->disable_irq(p)
#endif				/* parport_enable_irq */
#else				/* !PARPORT_NEED_GENERIC_OPS */
#ifndef parport_enable_irq
#define parport_enable_irq(p) parport_pc_enable_irq(p)
#define parport_disable_irq(p) parport_pc_disable_irq(p)
#endif				/* parport_enable_irq */
#endif				/* !PARPORT_NEED_GENERIC_OPS */
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)) */

/* #define _CPIA_DEBUG_		define for verbose debug output */
#include "cpia.h"

static int cpia_pp_open(void *privdata);
static int cpia_pp_registerCallback(void *privdata, void (*cb) (void *cbdata),
                                    void *cbdata);
static int cpia_pp_transferCmd(void *privdata, u8 *command, u8 *data);
static int cpia_pp_streamStart(void *privdata);
static int cpia_pp_streamStop(void *privdata);
static int cpia_pp_streamRead(void *privdata, u8 *buffer, int noblock);
static int cpia_pp_close(void *privdata);

#define ABOUT "Parallel port driver for Vision CPiA based cameras"

/* IEEE 1284 Compatiblity Mode signal names 	*/
#define nStrobe		PARPORT_CONTROL_STROBE	  /* inverted */
#define nAutoFd		PARPORT_CONTROL_AUTOFD	  /* inverted */
#define nInit		PARPORT_CONTROL_INIT
#define nSelectIn	PARPORT_CONTROL_SELECT
#define IntrEnable	PARPORT_CONTROL_INTEN	  /* normally zero for no IRQ */
#define DirBit		PARPORT_CONTROL_DIRECTION /* 0 = Forward, 1 = Reverse	*/

#define nFault		PARPORT_STATUS_ERROR
#define Select		PARPORT_STATUS_SELECT
#define PError		PARPORT_STATUS_PAPEROUT
#define nAck		PARPORT_STATUS_ACK
#define Busy		PARPORT_STATUS_BUSY	  /* inverted */	

/* some more */
#define HostClk		nStrobe
#define HostAck		nAutoFd
#define nReverseRequest	nInit
#define Active_1284	nSelectIn
#define nPeriphRequest	nFault
#define XFlag		Select
#define nAckReverse	PError
#define PeriphClk	nAck
#define PeriphAck	Busy

/* these can be used to correct for the inversion on some bits */
#define STATUS_INVERSION_MASK	(Busy)
#define CONTROL_INVERSION_MASK	(nStrobe|nAutoFd|nSelectIn)

#define ECR_empty	0x01
#define ECR_full	0x02
#define ECR_serviceIntr 0x04
#define ECR_dmaEn	0x08
#define ECR_nErrIntrEn	0x10

#define ECR_mode_mask	0xE0
#define ECR_SPP_mode	0x00
#define ECR_PS2_mode	0x20
#define ECR_FIFO_mode	0x40
#define ECR_ECP_mode	0x60

#define ECP_FIFO_SIZE	16
#define DMA_BUFFER_SIZE               PAGE_SIZE
	/* for 16bit DMA make sure DMA_BUFFER_SIZE is 16 bit aligned */
#define PARPORT_CHUNK_SIZE	PAGE_SIZE/* >=2.3.x */
				/* we read this many bytes at once */

#define GetECRMasked(port,mask)	(parport_read_econtrol(port) & (mask))
#define GetStatus(port)		((parport_read_status(port)^STATUS_INVERSION_MASK)&(0xf8))
#define SetStatus(port,val)	parport_write_status(port,(val)^STATUS_INVERSION_MASK)
#define GetControl(port)	((parport_read_control(port)^CONTROL_INVERSION_MASK)&(0x3f))
#define SetControl(port,val)	parport_write_control(port,(val)^CONTROL_INVERSION_MASK)

#define GetStatusMasked(port,mask)	(GetStatus(port) & (mask))
#define GetControlMasked(port,mask)	(GetControl(port) & (mask))
#define SetControlMasked(port,mask)	SetControl(port,GetControl(port) | (mask));
#define ClearControlMasked(port,mask)	SetControl(port,GetControl(port)&~(mask));
#define FrobControlBit(port,mask,value)	SetControl(port,(GetControl(port)&~(mask))|((value)&(mask)));

#define PACKET_LENGTH 	8

/* Magic numbers for defining port-device mappings */
#define PPCPIA_PARPORT_UNSPEC -4
#define PPCPIA_PARPORT_AUTO -3
#define PPCPIA_PARPORT_OFF -2
#define PPCPIA_PARPORT_NONE -1

#ifdef MODULE
static int parport_nr[PARPORT_MAX] = {[0 ... PARPORT_MAX - 1] = PPCPIA_PARPORT_UNSPEC};
static char *parport[PARPORT_MAX] = {NULL,};

MODULE_AUTHOR("B. Huisman <bhuism@cs.utwente.nl> & Peter Pregler <Peter_Pregler@email.com>");
MODULE_DESCRIPTION("Parallel port driver for Vision CPiA based cameras");
MODULE_PARM(parport, "1-" __MODULE_STRING(PARPORT_MAX) "s");
MODULE_PARM_DESC(parport, "'auto' or a list of parallel port numbers. Just like lp.");
#else
static int parport_nr[PARPORT_MAX] __initdata =
	{[0 ... PARPORT_MAX - 1] = PPCPIA_PARPORT_UNSPEC};
static int parport_ptr = 0;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
enum comstates { CPIA_FORWARD, CPIA_REVERSE };	//fixme
enum camstates {
	CPIA_PHASE_idle = 0,
	CPIA_PHASE_neg2s,
	CPIA_PHASE_setup,
	CPIA_PHASE_f2rev,
	CPIA_PHASE_r2for,
	CPIA_PHASE_term,
	CPIA_PHASE_secpread,
	CPIA_PHASE_ecpread,
	CPIA_PHASE_secpwrite,
};
#ifdef _CPIA_DEBUG_
static char camstatesstr[12][40] =
{
	"CPIA_PHASE_idle",
	"CPIA_PHASE_neg2s",
	"CPIA_PHASE_setup",
	"CPIA_PHASE_f2rev",
	"CPIA_PHASE_r2for",
	"CPIA_PHASE_term",
	"CPIA_PHASE_secpread",
	"CPIA_PHASE_ecpread",
	"CPIA_PHASE_secpwrite",
};
#endif
#endif

struct pp_cam_entry {
	struct pardevice *pdev;
	struct parport *port;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	enum comstates state;
	enum camstates camstate;
#endif
	struct tq_struct cb_task;
	int open_count;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	struct wait_queue *wq_stream;
#else
	wait_queue_head_t wq_stream;
#endif	
	/* image state flags */
	int image_ready;	/* we got an interrupt */
	int image_complete;	/* we have seen 4 EOI */

	int streaming; /* we are in streaming mode */
	int stream_irq;
	
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
	/* dma stuff */
	int dma_on;
	int dma_intr;
	int block_size; /* read block_size bytes per DMA */
	unsigned char *dma_buf;	/* into dma_buf */
	struct wait_queue *wq_dma;
	struct tq_struct dma_task;
	/* counters used in dma-interrupt handler */
	unsigned char *buf; /* current position in read buffer */
	int readbytes; /* # bytes read */
	int bytes; /* maximum # of bytes to read */
	int endseen; /* number of EOI read */
#endif
};

static struct cpia_camera_ops cpia_pp_ops = 
{
	cpia_pp_open,
	cpia_pp_registerCallback,
	cpia_pp_transferCmd,
	cpia_pp_streamStart,
	cpia_pp_streamStop,
	cpia_pp_streamRead,
	cpia_pp_close,
	1
};

static struct cam_data *cam_list;

#ifdef _CPIA_DEBUG_
#define DEB_PORT(port) { \
u8 controll = GetControl(port); \
u8 statusss = GetStatus(port); \
DBG("nsel %c per %c naut %c nstrob %c nak %c busy %c nfaul %c sel %c init %c dir %c\n",\
((controll & nSelectIn)	? 'U' : 'D'), \
((statusss & PError)	? 'U' : 'D'), \
((controll & nAutoFd)	? 'U' : 'D'), \
((controll & nStrobe)	? 'U' : 'D'), \
((statusss & nAck)	? 'U' : 'D'), \
((statusss & Busy)	? 'U' : 'D'), \
((statusss & nFault)	? 'U' : 'D'), \
((statusss & Select)	? 'U' : 'D'), \
((controll & nInit)	? 'U' : 'D'), \
((controll & DirBit)	? 'R' : 'F')  \
); }
#else
#define DEB_PORT(port) {}
#endif

#define WHILE_OUT_TIMEOUT (HZ/10)
#define DMA_TIMEOUT 10*HZ

/* FIXME */
static void cpia_parport_enable_irq( struct parport *port ) {
	parport_enable_irq(port);
	mdelay(10);
	return;
}

static void cpia_parport_disable_irq( struct parport *port ) {
	parport_disable_irq(port);
	mdelay(10);
	return;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
static int while_out(struct pp_cam_entry *cam)
{
	struct parport *port = cam->port;
	unsigned long endjif;

	if (GetECRMasked(port, ECR_full)) {
		return 1;
	}

	endjif = jiffies + WHILE_OUT_TIMEOUT;
	while( jiffies<endjif ) {
		schedule();
		if (GetECRMasked(port, ECR_full)) {
			return 1;
		}
	}
	DBG("WhileoutError at line waited %d\n", WHILE_OUT_TIMEOUT);
	return 0;
}

static int my_wait_peripheral(struct parport *port, int mask, int result)
{
	unsigned long oldjiffies = jiffies;

	result &= mask;

	if (GetStatusMasked(port, mask) == result) {
		return 0;
	}

	while (jiffies < oldjiffies + (HZ / 3)) {
		if (GetStatusMasked(port, mask) == result) {
			return 0;
		}
	}

	DBG("failed 0x%x should be 0x%x waited %ld jiffies\n", GetStatus(port) & mask, result, jiffies - oldjiffies);
	return 1;
}

static void EmptyFifo(struct parport *port)
{
	while (!GetECRMasked(port, ECR_empty)) {
		parport_read_fifo(port);
	}
}

/****************************************************************************
 *
 *  Negotiate2Setup
 *
 ***************************************************************************/
static int Negotiate2SetupPhase(struct pp_cam_entry *cam, int extensibility)
{
	struct parport *port = cam->port;
	u8 status = GetStatus(port);
	u8 control = GetControl(port);

	cam->camstate = CPIA_PHASE_neg2s;

	if (control & nSelectIn) DBG("nSelIn should be low at beginning\n");
	if (status & PError) DBG("PError should be low at beginning\n");
	if (!(control & nAutoFd)) DBG("nAutFb should be high at beginning\n");
	if (!(control & nStrobe)) DBG("nStrobe should be high at beginning\n");

	/*
	   a high nack idicates a new image is ready (see the docs)
	   this line can be used for a ISR.
	 */
	//if (!(status & nAck)) DBG("nAck should be high at beginning\n");

	if (!(status & Busy)) DBG("Busy should be high at beginning\n");
	// FIXME ?if (status & nFault) DBG("nFault should be low at beginning\n");
	if (!(status & Select)) DBG("Select should be high at beginning\n");
	if (!(control & nInit)) DBG("nInit should be high at beginning\n");

	if (cam->state != CPIA_FORWARD)
		DBG("port was not in CPIA_FORWARD direction\n");

	parport_write_data(port, extensibility ? 0x80 : 0x10);

	FrobControlBit(port, nSelectIn | nAutoFd, nSelectIn);	//A nAutoFd low,nSelectin high

	if (my_wait_peripheral(port, PError | nFault | Select | nAck, PError | nFault | Select)) {	//B
		DBG("B failed\n");
		return -1;
	}

	ClearControlMasked(port, nStrobe);
	ClearControlMasked(port, nStrobe);
	ClearControlMasked(port, nStrobe);
	ClearControlMasked(port, nStrobe);

	SetControlMasked(port, nAutoFd | nStrobe);	// C

	if (my_wait_peripheral(port, nAck | Select | PError, nAck | Select)) {	// D niet perror+busy low ? nfault high ?
		DBG("D failed nAck,nInit should become high, Perror low\n");
		return -1;
	}

	if (extensibility) {
		parport_write_data(port, 8 | 3);
		ClearControlMasked(port, nStrobe);
		if (my_wait_peripheral(port, nAck, 0)) {
			DBG("nAck should go low in asking for extensibility mode\n");
			DEB_PORT(port);
			return -1;
		}
		SetControlMasked(port, nStrobe);
		if (my_wait_peripheral(port, nAck | Select, nAck | Select)) {
			DBG("nAck,Xflag should go high in asking for extensibility mode\n");
			DEB_PORT(port);
			return -1;
		}
	}
	return 0;
}

/****************************************************************************
 *
 *  EcpSetupPhase
 *
 ***************************************************************************/
static int ECPSetupPhase(struct pp_cam_entry *cam)
{
	cam->camstate = CPIA_PHASE_setup;
	ClearControlMasked(cam->port, nAutoFd);	// E
	//if (my_wait_peripheral(cam->port,PError,PError)) //pport style

	if (my_wait_peripheral(cam->port, PError | nAck | Busy, PError | nAck)) {	// F
		DBG("F failed PError,nAck high Busy Low\n");
		DEB_PORT(cam->port);
		return -1;
	}
	EmptyFifo(cam->port);
	return 0;
}

/****************************************************************************
 *
 *  Forward2Reverse
 *
 ***************************************************************************/
static int Forward2Reverse(struct pp_cam_entry *cam)
{
	struct parport *port = cam->port;

	cam->camstate = CPIA_PHASE_f2rev;

	SetControlMasked(port, DirBit);		// door ClearControlBit(port,nReverseRequest) komt de eerste byte beter als DirBit hier wordt gezet
	//mdelay(3);

	ClearControlMasked(port, nReverseRequest);	// G
	//if (my_wait_peripheral(port,PError,0)) //pportq style

	if (my_wait_peripheral(port, PError | Busy | nFault | Select, Busy | Select)) {	//H 
		DBG("H failed\n");
		DEB_PORT(port);
		ClearControlMasked(port, DirBit);	// DirBit repareren;
		//mdelay(3);

		return -1;
	}
	cam->state = CPIA_REVERSE;
	return 0;
}


/****************************************************************************
 *
 *  Reverse2Forward
 *
 ***************************************************************************/
static void Reverse2Forward(struct pp_cam_entry *cam)
{
	struct parport *port = cam->port;

	cam->camstate = CPIA_PHASE_r2for;

	if (cam->state != CPIA_REVERSE)
		DBG("port was not in CPIA_REVERSE direction\n");

	SetControlMasked(port, nReverseRequest);
	if (my_wait_peripheral(port, nAckReverse, nAckReverse))
		DBG("nAckReverse should fo high in Reverse2Forward\n");

	ClearControlMasked(port, DirBit);
	//mdelay(3);

	cam->state = CPIA_FORWARD;
}


/****************************************************************************
 *
 *  Valid1284Termination
 *
 ***************************************************************************/
static void Valid1284Termination(struct pp_cam_entry *cam)
{
	struct parport *port = cam->port;

	cam->camstate = CPIA_PHASE_term;

	if (cam->state != CPIA_FORWARD)
		DBG("port was not in CPIA_FORWARD direction\n");

	FrobControlBit(port, nSelectIn | nAutoFd | nStrobe | nInit, nAutoFd | nStrobe | nInit);		//20

	if (my_wait_peripheral(port, nAck, 0)) {	//21
		DBG("nAck should go low in Valid1284Termination\n");
		DEB_PORT(port);
	}

	ClearControlMasked(port, nAutoFd);	//22

	if (my_wait_peripheral(port, nAck, nAck))	//23
		DBG("nAck should go high again in Valid1284Termination\n");

	SetControlMasked(port, nAutoFd);	//24

	if (my_wait_peripheral(port, PError | nAck | Busy | nFault | Select,
	                       nAck | Busy | Select))
		DBG("status bit did not go to the correct value\n");
	
	cam->camstate = CPIA_PHASE_idle;
}

/****************************************************************************
 *
 *  SimECPReadBuffer (does termination)
 *
 ***************************************************************************/
static int SimECPReadBuffer(struct pp_cam_entry *cam, u8 *buf, int bytes)
{
	int readbytes = 0;
	struct parport *port = cam->port;

	cam->camstate = CPIA_PHASE_secpread;

	if (cam->state != CPIA_REVERSE) DBG("cam not in CPIA_REVERSE\n");
	if (!GetControlMasked(port, DirBit))
		DBG("parport is in forward mode, "
		    "guess you won't be reading much\n");

	while (readbytes < bytes) {
		ClearControlMasked(port, nAutoFd);	//moet hier staan: als de while loop exit moet nAutoFd high blijven ...

		if (my_wait_peripheral(port, nAck, 0)) {
			DBG("nAck didn't went down after read %d bytes no more data ?\n", readbytes);
			break;
		}
		*buf++ = parport_read_data(port);
		SetControlMasked(port, nAutoFd);

		readbytes++;
		if (my_wait_peripheral(port, nAck, nAck)) {
			DBG("nAck didn't went up after read %d bytes\n", readbytes);
			break;
		}
	}
	return readbytes;
}

/****************************************************************************
 *
 *  ECPReadBuffer_PIO
 *
 ***************************************************************************/
static int ECPReadBuffer_PIO(struct pp_cam_entry *cam, u8 *buf, int bytes)
{
	int readbytes = 0, endseen = 0, j;
	struct parport *port = cam->port;

	if (my_wait_peripheral(port, nAck, 0)) {
		DBG("nAck didn't went down after read %d bytes no more data ?\n", readbytes);
		DEB_PORT(port);
		goto WhileoutError;
	}

	*buf = parport_read_data(port);	// we take the first byte manually
	if (*buf == EOI) {
		endseen++;
	} else {
		endseen = 0;
	}
	buf++;
	readbytes++;

	/* acknowledge for this BYTE */
	SetControlMasked(port, nAutoFd);
	if (my_wait_peripheral(port, nAck, nAck)) {
		DBG("nAck didn't went up after read %d bytes\n", readbytes);
		goto WhileoutError;
	}

	parport_frob_econtrol(port,
		ECR_mode_mask|ECR_serviceIntr|ECR_dmaEn|ECR_nErrIntrEn,
		ECR_ECP_mode|ECR_serviceIntr|ECR_nErrIntrEn);

	while ((((bytes - readbytes) / ECP_FIFO_SIZE) > 0) && (endseen < 4)) {
		if( current->need_resched ) {
			schedule();
		}
		if (while_out(cam) <= 0) {
			goto WhileoutError;
		}
		for (j = 0; j < ECP_FIFO_SIZE; j++) {
			*buf = parport_read_fifo(port);
			if (*buf == EOI) {
				endseen++;
			} else {
				endseen = 0;
			}
			buf++;
			readbytes++;
		}
		if( current->need_resched ) schedule();
	}

	/* switch off automatic filling of the FIFO */
	ClearControlMasked(port, nAutoFd);

	while ((!GetECRMasked(port, ECR_empty)) && (readbytes < bytes))	{
		*buf = parport_read_fifo(port);
		if (*buf == EOI) {
			endseen++;
		} else {
			endseen = 0;
		}
		buf++;
		readbytes++;
	}

WhileoutError:

	parport_write_econtrol(port, PARPORT_MODE_PCECR);
	parport_frob_econtrol(port,
	                      ECR_serviceIntr|ECR_dmaEn|ECR_nErrIntrEn,
	                      ECR_serviceIntr|ECR_nErrIntrEn);
	
	if( endseen > 3 ) {
		cam->image_complete=1;
	}
	
	return readbytes;
}

#ifdef CONFIG_VIDEO_CPIA_PP_DMA
/****************************************************************************
 *
 * ECPReadBuffer_DMA
 *
 ****************************************************************************/

int ECPReadBuffer_DMA(struct pp_cam_entry *cam, u8 *buf, int bytes)
{
	struct parport *port = cam->port;
	unsigned long flags;
	int channel = port->dma;
	int end_jiffies;

	cam->buf=buf;
	cam->bytes=bytes;
	cam->endseen=0;
	cam->readbytes=0;
	cam->image_complete=0;

	if (my_wait_peripheral(port,nAck,0)) {
		DBG("nAck didn't went down after read %d bytes no more data ?\n",cam->readbytes);
		DEB_PORT(port);
		return cam->readbytes;
	}

	*(cam->buf) = parport_read_data(port); // we take the first byte manually
	if (*(cam->buf) == EOI) {
		cam->endseen++;
	} else {
		cam->endseen = 0;
	}
	cam->buf++;
	cam->readbytes++;

	/* acknowledge for this BYTE */
	SetControlMasked( port, nAutoFd );	  				
	if (my_wait_peripheral(port,nAck,nAck)) {
		DBG("nAck didn't went up after read %d bytes\n",cam->readbytes);
		return cam->readbytes;
	}

	/* switch port to EPC mode, disable all interrupts for now */
	parport_frob_econtrol(port, ECR_mode_mask, ECR_ECP_mode);
	parport_frob_econtrol(port,
			      ECR_nErrIntrEn|ECR_serviceIntr|ECR_dmaEn,
			      ECR_nErrIntrEn|ECR_serviceIntr);

	/* prepare controller for dma - align for 16bit DMA */
	cam->block_size=
		bytes<DMA_BUFFER_SIZE?(bytes+(bytes%2)):DMA_BUFFER_SIZE;

	flags=claim_dma_lock();
	disable_dma(channel);
	clear_dma_ff(channel);
	set_dma_mode(channel, DMA_MODE_READ);
	set_dma_addr(channel, virt_to_bus(cam->dma_buf));
	set_dma_count(channel, cam->block_size);
	enable_dma(channel);

	cam->dma_on=1;
	cam->dma_intr = 0;

	/* switch on dma transfer */
	parport_frob_econtrol(port, ECR_dmaEn, ECR_dmaEn);
	parport_frob_econtrol(port,ECR_serviceIntr, 0);

	release_dma_lock(flags);

	end_jiffies = jiffies+DMA_TIMEOUT;
	while(cam->dma_on && !cam->image_complete &&
	      jiffies < end_jiffies
	      /*&& !signal_pending(current)*/) {
		interruptible_sleep_on(&cam->wq_dma);
	}

	/* we are done with dma */
	flags=claim_dma_lock();
	if(cam->dma_on) {
		cam->dma_on=0;
		interruptible_sleep_on(&cam->wq_dma);
	}

	disable_dma(channel);

	parport_frob_econtrol(port,ECR_mode_mask, ECR_PS2_mode);
	parport_frob_econtrol(port,ECR_serviceIntr, ECR_serviceIntr);
	parport_frob_econtrol(port, ECR_dmaEn, 0);

	release_dma_lock(flags);
	return cam->readbytes;
}
#endif /* CONFIG_VIDEO_CPIA_PP_DMA */

/****************************************************************************
 *
 * ECPReadBuffer (does termination) (stops when reading EOI)
 *
 ****************************************************************************/

int ECPReadBuffer(struct pp_cam_entry *cam, u8 *buf, int bytes)
{
	struct parport *port = cam->port;
	int readbytes;

	cam->camstate = CPIA_PHASE_ecpread;

	if (cam->state != CPIA_REVERSE) DBG("cam not in CPIA_REVERSE\n");
	if (!GetControlMasked(port,DirBit)) DBG("parport is in forward mode, guess you won't be reading much\n");

#ifdef CONFIG_VIDEO_CPIA_PP_DMA
	if(cam->dma_buf) {
		readbytes = ECPReadBuffer_DMA(cam, buf, bytes);
	} else {
#endif
		readbytes = ECPReadBuffer_PIO(cam, buf, bytes);
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
	}
#endif

	return readbytes;
};

/****************************************************************************
 *
 *  SimECPWriteBuffer (does termination)
 *
 ***************************************************************************/
static int SimECPWriteBuffer(struct pp_cam_entry *cam, const u8 *buf, int size)
{
	int written = 0;
	struct parport *port = cam->port;

	cam->camstate = CPIA_PHASE_secpwrite;

	if (cam->state != CPIA_FORWARD) DBG("cam not in CPIA_FORWARD\n");
	if (GetControlMasked(port, DirBit))
		DBG("parport is in reverse mode, "
		    "guess you won't be writing much\n");

	parport_write_data(port, *buf);
	SetControlMasked(port, HostAck);
	for (written = 0; written < size; written++) {
		parport_write_data(port, *buf++);
		ClearControlMasked(port, HostClk);
		if (my_wait_peripheral(port, PeriphAck, PeriphAck)) {
			DBG("PeriphAck never went up signaling read a byte\n");
			break;
		}
		//DBG("written byte: %02x\n",*(u8*)(buf-1));
		SetControlMasked(port, HostClk);
		if (my_wait_peripheral(port, PeriphAck, 0)) {
			DBG("PeriphAck never went down signaling ready for next byte\n");
			break;
		}
	}

	if (written != size)
		DBG("failed; written %db should be %db\n", written, size);

	return written;
}
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)) */

/****************************************************************************
 *
 *  EndTransferMode
 *
 ***************************************************************************/
static void EndTransferMode(struct pp_cam_entry *cam)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	if (cam->state == CPIA_REVERSE)  Reverse2Forward(cam);
	Valid1284Termination(cam);
	if(cam->stream_irq) cpia_parport_enable_irq(cam->port);
#else
	parport_negotiate(cam->port, IEEE1284_MODE_COMPAT);
#endif
}

/****************************************************************************
 *
 *  ForwardSetup
 *
 ***************************************************************************/
static int ForwardSetup(struct pp_cam_entry *cam)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	if(cam->stream_irq) cpia_parport_disable_irq(cam->port);
	if (!Negotiate2SetupPhase(cam, 0)) {
		if (!ECPSetupPhase(cam)) {
			return 0;
		} else {
			DBG("could not finish setup phase\n");
		}
	} else {
		DBG("could not negotiate for setup phase\n");
	}
	EndTransferMode(cam);
	return -1;
#else
	int retry;
	
	/* After some commands the camera needs extra time before
	 * it will respond again, so we try up to 3 times */
	for(retry=0; retry<3; ++retry) {
		if(!parport_negotiate(cam->port, IEEE1284_MODE_ECP)) {
			break;
		}
	}
	if(retry == 3) {
		DBG("Unable to negotiate ECP mode\n");
		return -1;
	}
	return 0;
#endif
}

/****************************************************************************
 *
 *  ReverseSetup
 *
 ***************************************************************************/
static int ReverseSetup(struct pp_cam_entry *cam, int extensibility)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	if(cam->stream_irq) cpia_parport_disable_irq(cam->port);
	if (!Negotiate2SetupPhase(cam, extensibility)) {
		if (!ECPSetupPhase(cam)) {
			if (!Forward2Reverse(cam)) {
				return 0;	/* don't terminate */
			} else {
				DBG("could not forward to reverse the port in order to read anything\n");
			}
		} else {
			DBG("could not finish setup phase\n");
		}
	} else {
		DBG("could not negotiate for setup phase\n");
	}
	EndTransferMode(cam);
	return -1;
#else
	int retry;
	int mode = IEEE1284_MODE_ECP;
	if(extensibility) mode = 8|3|IEEE1284_EXT_LINK;

	/* After some commands the camera needs extra time before
	 * it will respond again, so we try up to 3 times */
	for(retry=0; retry<3; ++retry) {
		if(!parport_negotiate(cam->port, mode)) {
			break;
		}
	}
	if(retry == 3) {
		if(extensibility)
			DBG("Unable to negotiate extensibility mode\n");
		else
			DBG("Unable to negotiate ECP mode\n");
		return -1;
	}
	if(extensibility) cam->port->ieee1284.mode = IEEE1284_MODE_ECP;
	return 0;
#endif
}

#ifdef CONFIG_VIDEO_CPIA_PP_DMA
/****************************************************************************
 *
 *  DMA routines
 *
 ****************************************************************************/

/****************************************************************************
 *
 *  Initiate a DMA read buffer
 *
 ****************************************************************************/

static inline void receive_buf_dma(struct pp_cam_entry *cam, int num_bytes)
{
	int channel = cam->port->dma;

	disable_dma(channel);
	clear_dma_ff(channel);
	set_dma_mode(channel, DMA_MODE_READ);
	set_dma_addr(channel, virt_to_bus(cam->dma_buf));
	set_dma_count(channel, num_bytes);
	enable_dma(channel);

	parport_frob_econtrol(cam->port,ECR_serviceIntr, 0);

	return;
}

static inline void stop_receive_buf_dma(struct pp_cam_entry *cam)
{
	unsigned long flags;

	flags=claim_dma_lock();
	disable_dma(cam->port->dma);
	cam->dma_on=0;

	parport_frob_econtrol(cam->port,ECR_mode_mask, ECR_PS2_mode);
	parport_frob_econtrol(cam->port,ECR_serviceIntr,
                	      ECR_serviceIntr);
	parport_frob_econtrol(cam->port, ECR_dmaEn, 0);

	if( waitqueue_active(&cam->wq_dma) ) {
        	wake_up_interruptible(&cam->wq_dma);
        }
	release_dma_lock(flags);

	return;
}

/****************************************************************************
 *
 *  DMA bottom half
 *
 ****************************************************************************/

static void dma_handler(void *handle)
{
	struct pp_cam_entry *cam = handle;
	int i;
	unsigned char *dma_ptr, *buf_ptr;
	unsigned long flags;

	flags=claim_dma_lock();
	disable_dma(cam->port->dma);
	parport_frob_econtrol(cam->port,ECR_serviceIntr,
                	      ECR_serviceIntr);
	release_dma_lock(flags);

	cam->dma_intr--;
	if(cam->dma_intr) {
        	DBG("extra interrupt: %d\n", cam->dma_intr);
        	stop_receive_buf_dma(cam);
        	return;
        }
	if(cam->dma_on==0 || cam->image_complete) {
        	return;
        }
	flags=claim_dma_lock();
	clear_dma_ff(cam->port->dma);
	if((i=get_dma_residue(cam->port->dma))!=0) {
        	DBG("dma_residue: %d\n", i);
        	stop_receive_buf_dma(cam);
        	release_dma_lock(flags);
        	return;
        }
	release_dma_lock(flags);
	if((i=cam->readbytes+cam->block_size)>cam->bytes) {
        	DBG("read too many bytes: %d/%d/%d\n",
		    i, cam->readbytes, cam->bytes);
        	stop_receive_buf_dma(cam);
        	return;
        }
	dma_ptr=cam->dma_buf;
	buf_ptr=cam->buf;
	i=-1;
	while(++i<cam->block_size && cam->endseen<4) {
        	if(*dma_ptr==EOI) {
                	cam->endseen++;
                } else {
                	cam->endseen=0;
                }
        	(*buf_ptr++)=(*dma_ptr++);
        }
	cam->readbytes+=i;
	cam->buf+=i;
	if( cam->endseen==4 ) {
        	cam->image_complete=1;
        	stop_receive_buf_dma(cam);
        	return;
        }

	if( cam->bytes-cam->readbytes <= DMA_BUFFER_SIZE ) {
		cam->block_size=cam->bytes-cam->readbytes;
		cam->block_size=cam->block_size-(cam->block_size%2);
        	if( cam->block_size==0 ) {
                	stop_receive_buf_dma(cam);
                	return;
                }
        }

	if( cam->dma_on) {
        	receive_buf_dma(cam, cam->block_size);
        } else {
        	stop_receive_buf_dma(cam);
        }
	return;
}
#endif /* CONFIG_VIDEO_CPIA_PP_DMA */

/****************************************************************************
 *
 *  IRQ handler
 *
 ***************************************************************************/

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
static void cpia_pp_irq_handler(int irq, void *handle, struct pt_regs *b)
{
	struct pp_cam_entry *cam = handle;
	if (cam==NULL) {
		return;
	}
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
	if(cam->dma_on) {
		cam->dma_intr++;
		if( cam->dma_intr == 1 ) {
			queue_task(&cam->dma_task, &tq_immediate);
			mark_bh(IMMEDIATE_BH);
		} else {
			DBG("%d\n", cam->dma_intr);
		}
		return;
	}
#endif /* CONFIG_VIDEO_CPIA_PP_DMA */
	if (cam->camstate == CPIA_PHASE_ecpread) return;
	if( cam->camstate != CPIA_PHASE_idle )
		DBG("got IRQ(%d) when in %s\n",
		    irq, camstatesstr[cam->camstate]);

	cam->image_ready++;
	if(cam->image_ready > 1) {
		cam->image_ready=0;
		DBG("image skipped?\n");
	}
	if(cam->cb_task.routine!=0) {
		queue_task(&cam->cb_task, &tq_scheduler);
	}

	if(waitqueue_active(&cam->wq_stream)) {
        	wake_up_interruptible(&cam->wq_stream);
        }
	return;
}
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)) */

/****************************************************************************
 *
 *  WritePacket
 *
 ***************************************************************************/
static int WritePacket(struct pp_cam_entry *cam, const u8 *packet, size_t size)
{
	int retval=0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
	int size_written;
#endif
	if (packet == NULL) {
		return -EINVAL;
	}
	if (ForwardSetup(cam)) {
		DBG("Write failed in setup\n");
		return -EIO;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	if (SimECPWriteBuffer(cam, packet, size) != size) {
		retval = -EIO;
	}
#else
	size_written = parport_write(cam->port, packet, size);
	if(size_written != size) {
		DBG("Write failed, wrote %d/%d\n", size_written, size);
		retval = -EIO;
	}
#endif
	EndTransferMode(cam);
	return retval;
}

/****************************************************************************
 *
 *  ReadPacket
 *
 ***************************************************************************/
static int ReadPacket(struct pp_cam_entry *cam, u8 *packet, size_t size)
{
	int retval=0;
	if (packet == NULL) {
		return -EINVAL;
	}
	if (ReverseSetup(cam, 0)) {
		return -EIO;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	if (SimECPReadBuffer(cam, packet, size) != size) {
		retval = -EIO;
	}
#else
	if(parport_read(cam->port, packet, size) != size) {
		retval = -EIO;
	}
#endif
	EndTransferMode(cam);
	return retval;
}

/****************************************************************************
 *
 *  cpia_pp_streamStart
 *
 ***************************************************************************/
static int cpia_pp_streamStart(void *privdata)
{
	struct pp_cam_entry *cam = privdata;
	DBG("\n");
	cam->streaming=1;
	cam->image_ready=0;
	//if (ReverseSetup(cam,1)) return -EIO;
	if(cam->stream_irq) cpia_parport_enable_irq(cam->port);
	return 0;
}

/****************************************************************************
 *
 *  cpia_pp_streamStop
 *
 ***************************************************************************/
static int cpia_pp_streamStop(void *privdata)
{
	struct pp_cam_entry *cam = privdata;

	DBG("\n");
	cam->streaming=0;
	cpia_parport_disable_irq(cam->port);
	//EndTransferMode(cam);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
static int cpia_pp_read(struct parport *port, u8 *buffer, int len)
{
	int bytes_read, new_bytes;
	for(bytes_read=0; bytes_read<len; bytes_read += new_bytes) {
		new_bytes = parport_read(port, buffer+bytes_read,
			                 len-bytes_read);
		if(new_bytes < 0) break;
	}
	return bytes_read;
}

#endif

/****************************************************************************
 *
 *  cpia_pp_streamRead
 *
 ***************************************************************************/
static int cpia_pp_streamRead(void *privdata, u8 *buffer, int noblock)
{
	struct pp_cam_entry *cam = privdata;
	int read_bytes = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
	int i, endseen, block_size, new_bytes;
#endif

	if(cam == NULL) {
		DBG("Internal driver error: cam is NULL\n");
		return -EINVAL;
	}
	if(buffer == NULL) {
		DBG("Internal driver error: buffer is NULL\n");
		return -EINVAL;
	}
	//if(cam->streaming) DBG("%d / %d\n", cam->image_ready, noblock);
	if( cam->stream_irq ) {
		DBG("%d\n", cam->image_ready);
		cam->image_ready--;
	}
	cam->image_complete=0;
	if (0/*cam->streaming*/) {
		if(!cam->image_ready) {
			if(noblock) return -EWOULDBLOCK;
			interruptible_sleep_on(&cam->wq_stream);
			if( signal_pending(current) ) return -EINTR;
			DBG("%d\n", cam->image_ready);
		}
	} else {
		if (ReverseSetup(cam, 1)) {
			DBG("unable to ReverseSetup\n");
			return -EIO;
		}
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	read_bytes = ECPReadBuffer(cam, buffer, CPIA_MAX_IMAGE_SIZE);
	if( 1/*!cam->streaming*/) {
		EndTransferMode(cam);
	}
	if(!cam->image_complete) {
		if( !signal_pending(current) )
			DBG("incomplete image: %ld / %d / %d\n",
			    jiffies, cam->image_complete, read_bytes);
	}
#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)) */
	endseen = 0;
	block_size = PARPORT_CHUNK_SIZE;
	while( !cam->image_complete ) {
		if(current->need_resched)  schedule();
		
		new_bytes = cpia_pp_read(cam->port, buffer, block_size );
		if( new_bytes <= 0 ) {
			break;
		}
		i=-1;
		while(++i<new_bytes && endseen<4) {
	        	if(*buffer==EOI) {
	                	endseen++;
	                } else {
	                	endseen=0;
	                }
			buffer++;
		}
		read_bytes += i;
		if( endseen==4 ) {
			cam->image_complete=1;
			break;
		}
		if( CPIA_MAX_IMAGE_SIZE-read_bytes <= PARPORT_CHUNK_SIZE ) {
			block_size=CPIA_MAX_IMAGE_SIZE-read_bytes;
		}
	}
	EndTransferMode(cam);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)) */
	return cam->image_complete ? read_bytes : -EIO;
}

/****************************************************************************
 *
 *  cpia_pp_transferCmd
 *
 ***************************************************************************/
static int cpia_pp_transferCmd(void *privdata, u8 *command, u8 *data)
{
	int err;
	int retval=0;
	int databytes;
	struct pp_cam_entry *cam = privdata;

	if(cam == NULL) {
		DBG("Internal driver error: cam is NULL\n");
		return -EINVAL;
	}
	if(command == NULL) {
		DBG("Internal driver error: command is NULL\n");
		return -EINVAL;
	}
	databytes = (((int)command[7])<<8) | command[6];
	if ((err = WritePacket(cam, command, PACKET_LENGTH)) < 0) {
		DBG("Error writing command\n");
		return err;
	}
	if(command[0] == DATA_IN) {
		u8 buffer[8];
		if(data == NULL) {
			DBG("Internal driver error: data is NULL\n");
			return -EINVAL;
		}
		if((err = ReadPacket(cam, buffer, 8)) < 0) {
			return err;
			DBG("Error reading command result\n");
		}
		memcpy(data, buffer, databytes);
	} else if(command[0] == DATA_OUT) {
		if(databytes > 0) {
			if(data == NULL) {
				DBG("Internal driver error: data is NULL\n");
				retval = -EINVAL;
			} else {
				if((err=WritePacket(cam, data, databytes)) < 0){
					DBG("Error writing command data\n");
					return err;
				}
			}
		}
	} else {
		DBG("Unexpected first byte of command: %x\n", command[0]);
		retval = -EINVAL;
	}
	return retval;
}

/****************************************************************************
 *
 *  cpia_pp_open
 *
 ***************************************************************************/
static int cpia_pp_open(void *privdata)
{
	struct pp_cam_entry *cam = (struct pp_cam_entry *)privdata;
	
	if (cam == NULL)
		return -EINVAL;
	
	if(cam->open_count == 0) {
		if (parport_claim(cam->pdev)) {
			DBG("failed to claim the port\n");
			return -EBUSY;
		}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		parport_write_econtrol(cam->port, PARPORT_MODE_PCECR);
		parport_frob_econtrol(cam->port,
		                      ECR_serviceIntr|ECR_dmaEn|ECR_nErrIntrEn,
		                      ECR_serviceIntr|ECR_nErrIntrEn);
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
		cam->dma_task.routine=dma_handler;
		cam->dma_task.data=cam;
#endif
		cam->stream_irq=0;
		cpia_parport_disable_irq(cam->port);
#else
		parport_negotiate(cam->port, IEEE1284_MODE_COMPAT);
		parport_data_forward(cam->port);
		parport_write_control(cam->port, PARPORT_CONTROL_SELECT);
		udelay(50);
		parport_write_control(cam->port,
		                      PARPORT_CONTROL_SELECT
		                      | PARPORT_CONTROL_INIT);
#endif
	}
	
	++cam->open_count;
	
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
	return 0;
}

/****************************************************************************
 *
 *  cpia_pp_registerCallback
 *
 ***************************************************************************/
static int cpia_pp_registerCallback(void *privdata, void (*cb)(void *cbdata), void *cbdata)
{
	struct pp_cam_entry *cam = privdata;
	int retval = 0;
	
	if(cam->port->irq != PARPORT_IRQ_NONE) {
		cam->cb_task.routine = cb;
		cam->cb_task.data = cbdata;
	} else {
		retval = -1;
	}
	return retval;
}

/****************************************************************************
 *
 *  cpia_pp_close
 *
 ***************************************************************************/
static int cpia_pp_close(void *privdata)
{
	struct pp_cam_entry *cam = privdata;
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
	unsigned int flags;
#endif
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
	if (--cam->open_count == 0) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
		if (cam->port->irq != PARPORT_IRQ_NONE) {
			cpia_parport_disable_irq(cam->port);
		}
		
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
		save_flags(flags);
		cli();
		if (cam->dma_on) {
			cam->dma_on=0;
			interruptible_sleep_on(&cam->wq_dma);
		}
		restore_flags(flags);
#endif /* CONFIG_VIDEO_CPIA_PP_DMA */
		if (waitqueue_active(&cam->wq_stream)) { /* FIXME */
			wake_up(&cam->wq_stream);
		}
		
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)) */
		parport_release(cam->pdev);
	}
	return 0;
}

/****************************************************************************
 *
 *  cpia_pp_register
 *
 ***************************************************************************/
static int cpia_pp_register(struct parport *port)
{
	struct pardevice *pdev = NULL;
	struct pp_cam_entry *cam;
	struct cam_data *cpia;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	if (!(port->modes & PARPORT_MODE_PCECP)) {
#else
	if (!(port->modes & PARPORT_MODE_ECP) &&
	    !(port->modes & PARPORT_MODE_TRISTATE)) {
#endif
		LOG("port is not ECP capable\n");
		return -ENXIO;
	}

	cam = kmalloc(sizeof(struct pp_cam_entry), GFP_KERNEL);
	if (cam == NULL) {
		LOG("failed to allocate camera structure\n");
		return -ENOMEM;
	}
	memset(cam,0,sizeof(struct pp_cam_entry));
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	pdev = parport_register_device(port, "cpia_pp", NULL, NULL,
	                               cpia_pp_irq_handler, 0, cam);
#else
	pdev = parport_register_device(port, "cpia_pp", NULL, NULL,
	                               NULL, 0, cam);
#endif

	if (!pdev) {
		LOG("failed to parport_register_device\n");
		kfree(cam);
		return -ENXIO;
	}

	cam->pdev = pdev;
	cam->port = port;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	cam->state = CPIA_FORWARD;
	cam->camstate = CPIA_PHASE_idle;
	init_waitqueue(&cam->wq_stream);
#else
	init_waitqueue_head(&cam->wq_stream);
#endif

	cam->streaming = 0;
	cam->stream_irq = 0;

#ifdef CONFIG_VIDEO_CPIA_PP_DMA
	if (pdev->port->irq != PARPORT_IRQ_NONE &&
	    pdev->port->dma != PARPORT_DMA_NONE) {
		if(request_dma(pdev->port->dma, "cpia_pp")) {
			LOG("failed to register dma %d\n", pdev->port->dma);
			parport_unregister_device(pdev);
			kfree(cam);
			return -ENXIO;
		}
		cam->dma_buf=kmalloc(DMA_BUFFER_SIZE, GFP_DMA);
		if(cam->dma_buf == NULL) {
			free_dma(cam->pdev->port->dma);
			LOG("failed to allocate dma buffer, using PIO mode\n");
		} else {
			init_waitqueue(&cam->wq_dma);
			printk(KERN_INFO "  using DMA mode (irq %d, DMA %d)\n", pdev->port->irq, pdev->port->dma);
		}
		memset(cam->dma_buf, 0, DMA_BUFFER_SIZE);
	} else {
		printk(KERN_INFO "  using PIO mode\n");
	}
#endif

	if((cpia = cpia_register_camera(&cpia_pp_ops, cam)) == NULL) {
		LOG("failed to cpia_register_camera\n");
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
		if (cam->dma_buf) {
			free_dma(cam->pdev->port->dma);
			kfree(cam->dma_buf);
		}
#endif
		parport_unregister_device(pdev);
		kfree(cam);
		return -ENXIO;
	}
	ADD_TO_LIST(cam_list, cpia);

	return 0;
}

static void cpia_pp_detach (struct parport *port)
{
	struct cam_data *cpia;

	for(cpia = cam_list; cpia != NULL; cpia = cpia->next) {
		struct pp_cam_entry *cam = cpia->lowlevel_data;
		if (cam && cam->port->number == port->number) {
			REMOVE_FROM_LIST(cpia);
			
#ifdef CONFIG_VIDEO_CPIA_PP_DMA
	                if (cam->dma_buf) {
	                        free_dma(cam->pdev->port->dma);
        	                kfree(cam->dma_buf);
                	}
#endif

			cpia_unregister_camera(cpia);
			
			if(cam->open_count > 0) {
				cpia_pp_close(cam);
			}

			parport_unregister_device(cam->pdev);
		
			kfree(cam);
			cpia->lowlevel_data = NULL;
			break;
		}
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
static void cpia_pp_attach (struct parport *port)
{
	unsigned int i;

	switch (parport_nr[0])
	{
	case PPCPIA_PARPORT_UNSPEC:
	case PPCPIA_PARPORT_AUTO:
		if (port->probe_info[0].class != PARPORT_CLASS_MEDIA ||
		    port->probe_info[0].cmdset == NULL ||
		    strncmp(port->probe_info[0].cmdset, "CPIA_1", 6) != 0)
			return;

		cpia_pp_register(port);

		break;

	default:
		for (i = 0; i < PARPORT_MAX; ++i) {
			if (port->number == parport_nr[i]) {
				cpia_pp_register(port);
				break;
			}
		}
		break;
	}
}

static struct parport_driver cpia_pp_driver = {
	"cpia_pp",
	cpia_pp_attach,
	cpia_pp_detach,
	NULL
};
#endif

int cpia_pp_init(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	struct parport *port;
	int i, count = 0;
#endif

	printk(KERN_INFO "%s v%d.%d.%d\n",ABOUT, 
	       CPIA_PP_MAJ_VER,CPIA_PP_MIN_VER,CPIA_PP_PATCH_VER);

	if(parport_nr[0] == PPCPIA_PARPORT_OFF) {
		printk("  disabled\n");
		return 0;
	}
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
	switch (parport_nr[0]) {
	case PPCPIA_PARPORT_UNSPEC:
	case PPCPIA_PARPORT_AUTO:
		for (port = parport_enumerate(); port; port = port->next) {

#if defined(CONFIG_PNP_PARPORT) || \
    (defined(MODULE) && defined(CONFIG_PNP_PARPORT_MODULE))
			if(port->probe_info.model) {
				if (port->probe_info.class != PARPORT_CLASS_MEDIA ||
				    port->probe_info.cmdset == NULL ||
				    strncmp(port->probe_info.cmdset, "CPIA_1", 6) != 0){
					continue;
				}
			}
#endif
			if (!cpia_pp_register(port)) {
				++count;
			}
		}
		break;

	default:
		for (i = 0; i < PARPORT_MAX; i++) {
			for (port = parport_enumerate(); port;
			     port = port->next) {
				if (port->number == parport_nr[i]) {
					if (!cpia_pp_register(port)) {
						count++;
					}
					break;
				}
			}
		}
		break;
	}

	printk("  %d camera(s) found\n", count);
	if (count == 0) {
		return -ENODEV;
	}
#else /* kernel version >= 2.3.0 */
	if (parport_register_driver (&cpia_pp_driver)) {
		LOG ("unable to register with parport\n");
		return -EIO;
	}
#endif /* kernel version >= 2.3.0 */

	return 0;
}

#ifdef MODULE
int init_module(void)
{
	if (parport[0]) {
		/* The user gave some parameters.  Let's see what they were. */
		if (!strncmp(parport[0], "auto", 4)) {
			parport_nr[0] = PPCPIA_PARPORT_AUTO;
		} else {
			int n;
			for (n = 0; n < PARPORT_MAX && parport[n]; n++) {
				if (!strncmp(parport[n], "none", 4)) {
					parport_nr[n] = PPCPIA_PARPORT_NONE;
				} else {
					char *ep;
					unsigned long r = simple_strtoul(parport[n], &ep, 0);
					if (ep != parport[n]) {
						parport_nr[n] = r;
					} else {
						LOG("bad port specifier `%s'\n", parport[n]);
						return -ENODEV;
					}
				}
			}
		}
	}
#if defined(CONFIG_KMOD) && defined(CONFIG_PNP_PARPORT_MODULE)
	if(parport_enumerate() && !parport_enumerate()->probe_info.model) {
		request_module("parport_probe");
	}
#endif
	return cpia_pp_init();
}

void cleanup_module(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
	parport_unregister_driver (&cpia_pp_driver);
#else
	while(cam_list != NULL) {
		struct pp_cam_entry *cam = cam_list->lowlevel_data;
		cpia_pp_detach(cam->port);
	}
#endif
	return;
}

#else /* !MODULE */

__initfunc(void cpia_pp_setup(char *str, int *ints))
{
	if (!str) {
		if (ints[0] == 0 || ints[1] == 0) {
			/* disable driver on "cpia_pp=" or "cpia_pp=0" */
			parport_nr[0] = PPCPIA_PARPORT_OFF;
		}
	} else if (!strncmp(str, "parport", 7)) {
		int n = simple_strtoul(str + 7, NULL, 10);
		if (parport_ptr < PARPORT_MAX) {
			parport_nr[parport_ptr++] = n;
		} else {
			LOG("too many ports, %s ignored.\n", str);
		}
	} else if (!strcmp(str, "auto")) {
		parport_nr[0] = PPCPIA_PARPORT_AUTO;
	} else if (!strcmp(str, "none")) {
		parport_nr[parport_ptr++] = PPCPIA_PARPORT_NONE;
	}
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0))
__setup("cpia_pp=", cpia_pp_setup);
#endif

#endif /* !MODULE */
