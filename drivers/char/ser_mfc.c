
/* serial driver for the two serial ports of a Multiface Card III
 *
 * Might also work with a SerialMaster or Multiface II, as far as I know
 * the only difference is the frequency of the used quartz.
 * Test it, and let me know if it works. Changing the baud table isn't worth a
 * whole new driver.
 * (To hardware hackers: It should be possible to get 115200 Baud with the older 
 * card by replacing the 3.6864 MHz quartz with a 7.3728 MHz one. But beware!
 * The only one who knows that now effectively all baudrates are doubled is you
 * and not any application. Besides, you should know what you are doing and you
 * do it at your own risk.)
 *
 * Due to hardware constraints I decided to make the two ports of the card different.
 * Port0 (the 9-pin-one) is only able to handle the following baud rates:
 * 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400 
 * (MFC II and Serial Master can do all standard rates.)
 * Port1 (the 25-pin-one) can handle any approbriate custom baud rate.
 * Tell me if you would prefer it the other way and why. Perhaps I'll make it
 * configurable.
 * 
 * Another solution would be to handle both ports the same way.
 * But that would mean to support only standard baud rates or to get into
 * big difficulties. On the other hand, you have more standard rates available.
 *
 * MIDI is missed with 5%. Can anyone tell me how to figure out which quartz
 * is attached by software? I think the duart.device simply assumes 4.000MHz
 * when you select MIDI baudrate.
 *
 * Another Hardware lack: If you select 5 bit character size a halve stop bit 
 * is sent too much.
 *
 * Hint to BSC: If they had used the IP2/3 pins for DCD and IP4/5 for DSR,
 * I could use the change state interrupt feature of the duart instead of polling
 * the DCD every vblank-interrupt. (Or does someone want to be interrupted when
 * DSR changes state (i.e. modem switched on/off) instead of knowing when a 
 * connection is established/lost?)
 *
 * Note: The duart is able to handle the RTS/CTS-protocol in hardware.
 * I use this feature to prevent receiver overruns by letting the DUART drop RTS
 * when the FIFO is full
 *
 * created 15.11.95 Joerg Dorchain (dorchain@mpi-sb.mpg.de)
 *
 * adapted 11.12.95 for 1.2.13 Joerg Dorchain
 *
 * improved 6. 2.96 Joerg Dorchain
 *  -  fixed restart bug
 *
 */


#include <linux/module.h>

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/m68kserial.h>

#include <asm/setup.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>
#include <asm/irq.h>

#include "multiface.h"
#include "mc68681.h"

/* how many cards are to be supported */
#define MAX_CARD 5

/***************************** Prototypes *****************************/

static void mfc_interrupt(int irq, void *data, struct pt_regs *fp);
static void mfc_vbl_inter(int irq, void *data, struct pt_regs *fp);
static void mfc_init( struct m68k_async_struct *info );
static void mfc_deinit( struct m68k_async_struct *info, int leave_dtr );
static void mfc_enab_tx_int( struct m68k_async_struct *info, int enab_flag );
static int  mfc_check_custom_divisor( struct m68k_async_struct *info, int
				     baud_base, int divisor );
static void mfc_change_speed( struct m68k_async_struct *info );
static void mfc_throttle( struct m68k_async_struct *info, int status );
static void mfc_set_break( struct m68k_async_struct *info, int break_flag );
static void mfc_get_serial_info( struct m68k_async_struct *info,
				struct serial_struct *retinfo );
static unsigned int mfc_get_modem_info( struct m68k_async_struct *info );
static int mfc_set_modem_info( struct m68k_async_struct *info, int new_dtr,
			      int new_rts );
static void mfc_stop_receive(struct m68k_async_struct *info);
static int mfc_trans_empty(struct m68k_async_struct *info);

/************************* End of Prototypes **************************/



/* SERIALSWITCH structure for the Multiface serial ports */

static SERIALSWITCH mfc_ser_switch = {
    mfc_init, mfc_deinit, mfc_enab_tx_int,
    mfc_check_custom_divisor, mfc_change_speed,
    mfc_throttle, mfc_set_break,
    mfc_get_serial_info, mfc_get_modem_info,
    mfc_set_modem_info, NULL,
    mfc_stop_receive, mfc_trans_empty, NULL
};



/* These value go into the csr (Clock Select Registers) */
/* 0xff means not available, B0 is handle special anyway */
 static u_char fixed_baud_III[16] = {
 0, /* B0 */
 0xff, /* B50 */
 0xff, /* B75 */
 0xff, /* B110 */
 0xff, /* B134 */
 0xff, /* B150 */
 0xff, /* B200 */
 0x33, /* B300 */
 0x44, /* B600 */
 0x55, /* B1200 */
 0xff, /* B1800 */
 0x66, /* B2400 */
 0x88, /* B4800 */
 0x99, /* B9600 */
 0xbb, /* B19200 */
 0xcc  /* B38400 */
 };
/* These are precalculated timer values for standart rates */
 static u_short custom_baud_III[18] = {
 0, /* B0 */
 4608, /* B50 */
 3072, /* B75 */
 2095, /* B110 - a little bit off (0.02%) */
 1713, /* B134 - off by 0.0006% */
 1536, /* B150 */
 1152, /* B200 */
 768, /* B300 */
 384, /* B600 */
 192, /* B1200 */
 128, /* B1800 */
 96, /* B2400 */
 48, /* B4800 */
 24, /* B9600 */
 12, /* B19200 */
 6, /* B38400 */
 4, /* B57600 */
 2 /* B115200 */
 };

static int nr_mfc; /* nr of ports configured */
static int lines[MAX_CARD * 2]; /* accociated tty lines (index in rs_table) */
static unsigned int board_index[MAX_CARD]; /* nr. of zorro slot */
static unsigned char imask[MAX_CARD];
static unsigned char acmask[MAX_CARD];

static void mfc_fill_tx(struct m68k_async_struct *info)
{
struct duart *dp;
struct duarthalf *d;
int sta,i,mask,ch;

dp=info->board_base;
d=(struct duarthalf *)info->port;
i=(info->nr_uarts-1)/2;
mask=(((info->nr_uarts-1)%2)==0)?1:16;
sta=d->sr_csr.sr;
while ((sta & 4) != 0) {   /* fill the buffer */
  if ((ch = rs_get_tx_char( info )) >= 0)
    d->hr.thr = ch;
  else
    break;
  sta=d->sr_csr.sr;
}
if (rs_no_more_tx( info ))
  imask[i] &= ~mask;
else
  imask[i] |= mask;
dp->ir.imr = imask[i];
}

static u_char isr_entered = 0;

static void mfc_interrupt(int irq, void *data, struct pt_regs *fp)
{
int i;
int ireq;
struct duart *dp;
int ch;
int status;
int err;
int ipch;
u_short intenar;


intenar = (custom.intenar & IF_EXTER);
custom.intena = IF_EXTER;
if (isr_entered) {
  printk("Reentering MFC isr.\n");
  custom.intena = (intenar | IF_SETCLR);
  return;
}
isr_entered = 1;

custom.intena = (intenar | IF_SETCLR);

for (i = 0; i < nr_mfc; i += 2) {
  dp = rs_table[lines[i]].board_base;
  ireq = dp->ir.isr;
  if ((ireq & imask[i/2]) !=0) { /* if it is "our" interrupt */
    /* first the transmitter */
    mfc_fill_tx(rs_table + lines[i]);      /* channel A */
    mfc_fill_tx(rs_table + lines[i + 1]);  /* channel B */
    { /* test for receiver ready */
      {
	status = dp->pa.sr_csr.sr; /* empty the FIFO */
	while ((status & 1) != 0) {
	  ch = dp->pa.hr.rhr;
	  err = 0;
	  if ((status & SR_BREAK) != 0)
	    err |= TTY_BREAK;
	  if ((status & SR_FRAMING) != 0)
	    err |= TTY_FRAME;
	  if ((status & SR_PARITY) != 0)
	    err |= TTY_PARITY;
	  if ((status & SR_OVERRUN) != 0)
	    err |= TTY_OVERRUN;
	  rs_receive_char(rs_table + lines[i], ch, err);
	  status = dp->pa.sr_csr.sr;
	} 
      }
      {
	status = dp->pb.sr_csr.sr;
	while (( status & 1) != 0) {
	  ch = dp->pb.hr.rhr;
	  err = 0;
	  if ((status & SR_BREAK) != 0)
	    err |= TTY_BREAK;
	  if ((status & SR_FRAMING) != 0)
	    err |= TTY_FRAME;
	  if ((status & SR_PARITY) != 0)
	    err |= TTY_PARITY;
	  if ((status & SR_OVERRUN) != 0)
	    err |= TTY_OVERRUN;
	  rs_receive_char(rs_table + lines[i + 1], ch, err);
	  status = dp->pb.sr_csr.sr;
	} 
      }
    }
    { /* CTS changed */
     ipch = dp->ipcr_acr.ipcr;
     if ((ipch & 16) !=0)  /* port a */
       rs_check_cts(rs_table + lines[i], !(ipch & 1)); 
     if ((ipch & 32) !=0)
       rs_check_cts(rs_table + lines[i + 1], !(ipch & 2));
    }
  }
}
custom.intreq = IF_EXTER;
isr_entered = 0;
}

static u_char curr_dcd[MAX_CARD];

static void mfc_vbl_inter(int irq, void *data, struct pt_regs *fp)
{
int i;
struct duart *dp;
u_char bits;

for (i = 0; i < nr_mfc; i += 2) {
  if (rs_table[lines[i]].flags & ASYNC_INITIALIZED) {
    dp = rs_table[lines[i]].board_base;
    bits = dp->ipr_opcr.ipr & 48;
    if (bits ^ curr_dcd[i/2]) {
      if (((bits ^ curr_dcd[i/2]) & 16) != 0)
	rs_dcd_changed(rs_table + lines[i], !(bits & 16));
      if (((bits ^ curr_dcd[i/2]) & 32) != 0)
	rs_dcd_changed(rs_table + lines[i + 1], !(bits & 32));
    }
    curr_dcd[i/2] = bits;
  }
}
}

static void mfc_init( struct m68k_async_struct *info)
{
struct duart *dp;
struct duarthalf *d;

dp = info->board_base;
d = (struct duarthalf *)info->port;
if (((info->nr_uarts-1)%2) == 0) {  /* port A */
  dp->start_sopc.sopc = 5;		/* DTR & RTS on */
  imask[(info->nr_uarts-1)/2] |= 3|128; /* interrupts on */
  acmask[(info->nr_uarts-1)/2] |= 1;
}
else {
  dp->start_sopc.sopc = 10;
  imask[(info->nr_uarts-1)/2] |= 48|128;
  acmask[(info->nr_uarts-1)/2] |= 2;
}
d->cr = CR_RESET_RX;
d->cr = CR_RESET_TX;
d->cr = CR_RESET_ERR;
d->cr = CR_RESET_BREAK;
d->cr = CR_STOP_BREAK;
d->cr = CR_RX_ON;
d->cr = CR_TX_ON;
curr_dcd[(info->nr_uarts-1)/2] = dp->ipr_opcr.ipr & 48;
custom.intena = IF_EXTER;
/* enable interrupts */
dp->ir.imr = imask[(info->nr_uarts-1)/2];
dp->ipcr_acr.acr = acmask[(info->nr_uarts-1)/2];
custom.intena = (IF_EXTER | IF_SETCLR);
MOD_INC_USE_COUNT;
}

static void mfc_deinit( struct m68k_async_struct *info, int leave_dtr)
{
struct duart *dp;
struct duarthalf *d;
u_short intenar;

dp = info->board_base;
d = (struct duarthalf *)info->port;
/* turn off interrupts CTS and DTR if required */
if (((info->nr_uarts-1)%2) == 0) {
  dp->stop_ropc.ropc = 1;
  if (!leave_dtr)
    dp->stop_ropc.ropc = 4;
  imask[(info->nr_uarts-1)/2] &= ~3;
  acmask[(info->nr_uarts-1)/2] &= ~1;
}
else {
  dp->stop_ropc.ropc = 2;
  if (!leave_dtr)
    dp->stop_ropc.ropc = 8;
  imask[(info->nr_uarts-1)/2] &= ~48;
  acmask[(info->nr_uarts-1)/2] &= ~2;
}
intenar = (custom.intenar & IF_EXTER);
custom.intena = IF_EXTER;
dp->ir.imr = imask[(info->nr_uarts-1)/2];
dp->ipcr_acr.acr = acmask[(info->nr_uarts-1)/2];
custom.intena = (intenar | IF_SETCLR);
/* disable transmitter and receiver after current character */
d->cr = CR_RX_OFF;
d->cr = CR_TX_OFF;
MOD_DEC_USE_COUNT;
}

static void mfc_enab_tx_int(struct m68k_async_struct *info, int enab_flag)
{
struct duart *dp;

if (enab_flag)
  mfc_fill_tx(info); /* fills the tx buf and enables interrupts */
else { /* disable interrupts */
  /* we could also disable the transmitter at all */
  dp = info->board_base;
  if (((info->nr_uarts-1)%2) == 0) 
    imask[(info->nr_uarts-1)/2] &= ~1;
  else
    imask[(info->nr_uarts-1)/2] &= ~16;
  dp->ir.imr=imask[(info->nr_uarts-1)/2];
}
}

static int mfc_check_custom_divisor(struct m68k_async_struct *info,
      int baud_base, int divisor)
{
if (((info->nr_uarts-1)%2) == 0) 
  return 1;
if (baud_base != 230400) /* change for MFC II */
  return 1;
if (divisor < 2)
  return 1;
return 0;
}

static void mfc_change_speed(struct m68k_async_struct *info)
{
u_int cflag, baud, chsize, stopb, parity, aflags;
u_int ctrl = 0;
u_short div = 0;
struct duart *dp;
struct duarthalf *d;
int dummy;

if (!info->tty || ! info->tty->termios)
  return;
dp = info->board_base;
d = (struct duarthalf *)info->port;
cflag = info->tty->termios->c_cflag;
baud = cflag & CBAUD;
chsize = cflag & CSIZE;
stopb = cflag & CSTOPB;
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
  if (baud > 17)
    baud = 17;
  div = custom_baud_III[baud];
}

if (!div) { /* drop DTR */
  if (((info->nr_uarts-1)%2) == 0)
    dp->stop_ropc.ropc = 4;
  else
    dp->stop_ropc.ropc = 8;
  return;
}

if (((info->nr_uarts-1)%2) == 0) {
  dp->start_sopc.sopc = 4; /* set DTR */
  if (baud > 15)
    baud =15;
  if (fixed_baud_III[baud] != 0xff)
    d->sr_csr.csr = fixed_baud_III[baud];
  else
    printk("ttyS%d: ignored illegal baudrate %d\n", info->line,baud);
}
else {
  dp->start_sopc.sopc = 8;
  dp->ctu = div / 256;
  dp->ctl = div % 256;
  dummy = dp->start_sopc.start;
}
d->mr.mr2 = (stopb) ? MR2_2STOP: MR2_1STOP;

if (chsize == CS8)
  ctrl |= MR1_8BITS;
else if (chsize == CS7)
  ctrl |= MR1_7BITS;
else if (chsize == CS6)
  ctrl |= MR1_6BITS;
else if (chsize == CS5)
  ctrl |= MR1_5BITS;

if (parity & PARENB)
  ctrl |= MR1_PARITY_WITH;
else
  ctrl |= MR1_PARITY_NO;
if (parity & PARODD)
  ctrl |= MR1_PARITY_ODD;

d->cr = CR_RESET_MR;
d->mr.mr1 = ctrl|MR1_RxRTS_ON;
}

static void mfc_throttle (struct m68k_async_struct *info, int status)
{
struct duart *dp;

dp = info->board_base;
if (((info->nr_uarts-1)%2) == 0) {
  if (status)
    dp->stop_ropc.ropc = 1;
  else 
    dp->start_sopc.sopc = 1;
}
else {
  if (status)
    dp->stop_ropc.ropc = 2;
  else 
    dp->start_sopc.sopc = 2;
}
}

static void mfc_set_break (struct m68k_async_struct *info, int break_flag)
{
struct duarthalf *d;

d = (struct duarthalf *)info->port;
if (break_flag)
  d->cr = CR_START_BREAK;
else
  d->cr = CR_STOP_BREAK;
}

static void mfc_get_serial_info( struct m68k_async_struct *info, 
      struct serial_struct *retinfo)
{
if (((info->nr_uarts-1)%2) == 0) {
  retinfo->baud_base = 0;
  retinfo->custom_divisor = 0;
}
else {
  retinfo->baud_base = 230400;
  retinfo->custom_divisor = info->custom_divisor;
}
}

static unsigned int mfc_get_modem_info(struct m68k_async_struct *info)
{
struct duart *dp;
u_char inf;

dp = info->board_base;
inf = dp->ipr_opcr.ipr;
if (((info->nr_uarts-1)%2) == 0) 
  return ((inf & 1) ? 0 : TIOCM_CTS) |
         ((inf & 4) ? 0 : TIOCM_DSR) |
         ((inf & 16) ? 0 : TIOCM_CAR) |
         ((dp->pa.ri & 1) ? 0 : TIOCM_RNG);
else
  return ((inf & 2) ? 0 : TIOCM_CTS) |
         ((inf & 8) ? 0 : TIOCM_DSR) |
         ((inf & 32) ? 0 : TIOCM_CAR) |
         ((dp->pb.ri & 2) ? 0 : TIOCM_RNG);
/* No chance to check DTR & RTS easily */
}

static int mfc_set_modem_info(struct m68k_async_struct *info, int new_dtr, int new_rts)
{
struct duart *dp;

dp = info->board_base;
if (((info->nr_uarts-1)%2) == 0) {
  if (new_dtr == 0)
    dp->stop_ropc.ropc = 4;
  else if (new_dtr == 1)
    dp->start_sopc.sopc = 4;
  if (new_rts == 0)
    dp->stop_ropc.ropc = 1;
  else if (new_rts == 1)
    dp->start_sopc.sopc = 1;
}
else {
  if (new_dtr == 0)
    dp->stop_ropc.ropc = 8;
  else if (new_dtr == 1)
    dp->start_sopc.sopc = 8;
  if (new_rts == 0)
    dp->stop_ropc.ropc = 2;
  else if (new_rts == 1)
    dp->start_sopc.sopc = 2;
}
return 0;
}

static void mfc_stop_receive(struct m68k_async_struct *info)
{
struct duart *dp;

dp = info->board_base;
if (((info->nr_uarts-1)%2) == 0)
  imask[(info->nr_uarts-1)/2] &= ~2;
else
  imask[(info->nr_uarts-1)/2] &= ~32;
dp-> ir.imr = imask[(info->nr_uarts-1)/2];
}

static int mfc_trans_empty(struct m68k_async_struct *info)
{
struct duarthalf *dh;

dh = (struct duarthalf *)info->port;
return (dh->sr_csr.sr & 8);
}

int multiface_init(void)
{
unsigned int key;
int line1, line2;
struct duart *dp;
int dummy;
const struct ConfigDev *cd;
struct serial_struct req;
struct m68k_async_struct *info;

if (!MACH_IS_AMIGA)
  return -ENODEV;
  
nr_mfc = 0;
/* add MFC_II and serial master for test purposes */
while((key=zorro_find(ZORRO_PROD_BSC_MULTIFACE_III,1, 0))) {
  board_index[nr_mfc/2] = key;
  cd = zorro_get_board(key);
  dp = (struct duart *)ZTWO_VADDR((((u_char *)cd->cd_BoardAddr)+DUARTBASE));

  req.line = -1; /* first free ttyS? device */
  req.type = SER_MFC_III;
  req.port = (int)&dp->pa;
  if ((line1 = m68k_register_serial( &req )) < 0) {
      printk( "Cannot register MFC III serial port: no free device\n" );
      return -EBUSY;
  }
  lines[nr_mfc++] = line1;
  info = &rs_table[line1];
  info->sw = &mfc_ser_switch;
  info->nr_uarts = nr_mfc;
  info->board_base = dp;

  req.line = -1; /* first free ttyS? device */
  req.type = SER_MFC_III;
  req.port = (int)&dp->pb;
  if ((line2 = m68k_register_serial( &req )) < 0) {
      printk( "Cannot register MFC III serial port: no free device\n" );
	  m68k_unregister_serial( line1 );
      return -EBUSY;
  }
  lines[nr_mfc++] = line2;
  info = &rs_table[line2];
  info->sw = &mfc_ser_switch;
  info->nr_uarts = nr_mfc--;
  info->board_base = dp;

  if (nr_mfc < 4) {
      request_irq(IRQ_AMIGA_EXTER, mfc_interrupt, 0,
                  "Multiface III serial", mfc_interrupt);
      request_irq(IRQ_AMIGA_VERTB, mfc_vbl_inter, 0,
                  "Multiface III serial VBL", mfc_vbl_inter);
  }

  dp->ir.imr = 0; /* turn off all interrupts */
  imask[nr_mfc/2] = 0;
  dummy = dp->ir.isr; /* clear peding interrupts */
  dummy = dp->ipcr_acr.ipcr;
  dp->stop_ropc.ropc = 255; /* all outputs off */
  dp->ipr_opcr.opcr = 0; /* change for serial-master */
  dp->pa.cr = CR_RX_OFF; /* disable receiver */
  dp->pb.cr = CR_RX_OFF;
  dp->pa.cr = CR_TX_OFF; /* disable transmitter */
  dp->pb.cr = CR_TX_OFF;
  dp->pa.cr = CR_RESET_RX;
  dp->pb.cr = CR_RESET_RX;
  dp->pa.cr = CR_RESET_TX;
  dp->pb.cr = CR_RESET_TX;
  dp->pa.cr = CR_RESET_ERR;
  dp->pb.cr = CR_RESET_ERR;
  dp->pa.cr = CR_RESET_BREAK;
  dp->pb.cr = CR_RESET_BREAK;
  dp->pa.cr = CR_STOP_BREAK;
  dp->pb.cr = CR_STOP_BREAK;
  dp->pa.cr = CR_RESET_MR;
  dp->pb.cr = CR_RESET_MR;
  /* set default 9600  8 N 1 */
  dp->pa.mr.mr1 = MR1_8BITS|MR1_PARITY_NO|MR1_RxRTS_ON;
  dp->pb.mr.mr1 = MR1_8BITS|MR1_PARITY_NO|MR1_RxRTS_ON;
  dp->pa.mr.mr2 = MR2_1STOP;
  dp->pb.mr.mr2 = MR2_1STOP;

  dp->ipcr_acr.acr = 0xe0; /* BRG set 2,  timer 1X crystal */
  acmask[nr_mfc/2] = 0xe0;
  /* change the following baudrate stuff for MFC_II */
  dp->pa.sr_csr.csr = 0x99; /* 9600 from BRG */
  dp->pb.sr_csr.csr = 0xdd; /* from timer */
  dp->ctu = 0;
  dp->ctl = 24; /* values for 9600 */
  dummy = dp->start_sopc.start; /* load timer with new values */
  nr_mfc++;

  zorro_config_board(key,1);
}
return 0;
}

#ifdef MODULE
int init_module(void)
{
return multiface_init();
}

void cleanup_module(void)
{
int i;
struct duart *dp;

for (i = 0; i < nr_mfc; i += 2) {
  dp = rs_table[lines[i]].board_base;
  dp->pa.cr = CR_RX_OFF | CR_TX_OFF; /* disable duart */
  dp->pb.cr = CR_RX_OFF | CR_TX_OFF;
  dp->ir.imr = 0;   /* turn off interrupts */
  m68k_unregister_serial(lines[i]);
  m68k_unregister_serial(lines[i+1]);
  zorro_unconfig_board(board_index[i/2], 1);
}
free_irq(IRQ_AMIGA_EXTER, mfc_interrupt);
free_irq(IRQ_AMIGA_VERTB, mfc_vbl_inter);
}
#endif


