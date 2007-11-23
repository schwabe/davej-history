
/* rio_linux.c -- Linux driver for the Specialix RIO series cards. 
 *
 *
 *   (C) 1999 R.E.Wolff@BitWizard.nl
 *
 * Specialix pays for the development and support of this driver.
 * Please DO contact support@specialix.co.uk if you require
 * support. But please read the documentation (rio.txt) first.
 *
 *
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *      USA.
 *
 * Revision history:
 * $Log: rio.c,v $
 * Revision 1.1  1999/07/11 10:13:54  wolff
 * Initial revision
 *
 * */


#define RCS_ID "$Id: rio.c,v 1.1 1999/07/11 10:13:54 wolff Exp wolff $"
#define RCS_REV "$Revision: 1.1 $"


#include <linux/module.h>
#include <linux/config.h> 
#include <linux/kdev_t.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/serial.h>
#include <linux/fcntl.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/tqueue.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/malloc.h>
#include <linux/miscdevice.h>

#include <linux/compatmac.h>
#include <linux/generic_serial.h>


#include "linux_compat.h"
#include "typdef.h"
#include "pkt.h"
#include "daemon.h"
#include "rio.h"
#include "riospace.h"
#include "top.h"
#include "cmdpkt.h"
#include "map.h"
#include "riotypes.h"
#include "rup.h"
#include "port.h"
#include "riodrvr.h"
#include "rioinfo.h"
#include "func.h"
#include "errors.h"
#include "pci.h"

#include "parmmap.h"
#include "unixrup.h"
#include "board.h"
#include "host.h"
#include "error.h"
#include "phb.h"
#include "link.h"
#include "cmdblk.h"
#include "route.h"
#include "control.h"
#include "cirrus.h"
#include "rioioctl.h"
#include "param.h"
#include "list.h"
#include "sam.h"
#include "protsts.h"
#include "rioboard.h"


#include "rio_linux.h"

/* I don't think that this driver can handle more than 512 ports on
one machine.  Specialix specifies max 4 boards in one machine. I don't
know why. If you want to try anyway you'll have to increase the number
of boards in rio.h.  You'll have to allocate more majors if you need
more than 512 ports.... */


/* ************************************************************** */
/* * This section can be removed when 2.0 becomes outdated....  * */
/* ************************************************************** */

#if LINUX_VERSION_CODE < 0x020100    /* Less than 2.1.0 */
#define TWO_ZERO
#else
#if LINUX_VERSION_CODE < 0x020209   /* less than 2.2.x */
#warning "Please use a recent 2.2.x kernel. "
#endif
#endif


#ifdef TWO_ZERO

/* Here is the section that makes the 2.2 compatible driver source 
   work for 2.0 too! We mostly try to adopt the "new thingies" from 2.2, 
   and provide for compatibility stuff here if possible. */

#include <linux/bios32.h>

#define Get_user(a,b)                a = get_user(b)
#define Put_user(a,b)                0,put_user(a,b)
#define copy_to_user(a,b,c)          memcpy_tofs(a,b,c)

static inline int copy_from_user(void *to,const void *from, int c) 
{
  memcpy_fromfs(to, from, c);
  return 0;
}

#define pci_present                  pcibios_present
#define pci_read_config_word         pcibios_read_config_word
#define pci_read_config_dword        pcibios_read_config_dword

static inline unsigned char get_irq (unsigned char bus, unsigned char fn)
{
  unsigned char t; 
  pcibios_read_config_byte (bus, fn, PCI_INTERRUPT_LINE, &t);
  return t;
}

static inline void *ioremap(unsigned long base, long length)
{
  if (base < 0x100000) return (void *)base;
  return vremap (base, length);
}

#define my_iounmap(x, b)             (((long)x<0x100000)?0:vfree ((void*)x))

#define capable(x)                   suser()

#define queue_task                   queue_task_irq_off
#define tty_flip_buffer_push(tty)    queue_task(&tty->flip.tqueue, &tq_timer)
#define signal_pending(current)      (current->signal & ~current->blocked)
#define schedule_timeout(to)         do {current->timeout = jiffies + (to);schedule ();} while (0)
#define time_after(t1,t2)            (((long)t1-t2) > 0)


#define test_and_set_bit(nr, addr)   set_bit(nr, addr)
#define test_and_clear_bit(nr, addr) clear_bit(nr, addr)

/* Not yet implemented on 2.0 */
#define ASYNC_SPD_SHI  -1
#define ASYNC_SPD_WARP -1


/* Ugly hack: the driver_name doesn't exist in 2.0.x . So we define it
   to the "name" field that does exist. As long as the assignments are
   done in the right order, there is nothing to worry about. */
#define driver_name           name 

/* Should be in a header somewhere. They are in tty.h on 2.2 */
#define TTY_HW_COOK_OUT       14 /* Flag to tell ntty what we can handle */
#define TTY_HW_COOK_IN        15 /* in hardware - output and input       */

/* The return type of a "close" routine. */
#define INT                   void
#define NO_ERROR              /* Nothing */

#else

/* The 2.2.x compatibility section. */
#include <asm/uaccess.h>

#define Get_user(a,b)         get_user(a,b)
#define Put_user(a,b)         put_user(a,b)
#define get_irq(pdev)         pdev->irq

#define INT                   int
#define NO_ERROR              0              

#define my_iounmap(x,b)       (iounmap((char *)(b)))

#endif

/* ************************************************************** */
/* *                End of compatibility section..              * */
/* ************************************************************** */



/* Why the hell am I defining these here? */
#define RIO_TYPE_NORMAL 1
#define RIO_TYPE_CALLOUT 2

#ifndef RIO_NORMAL_MAJOR0
/* This allows overriding on the compiler commandline, or in a "major.h" 
   include or something like that */
#define RIO_NORMAL_MAJOR0  154
#define RIO_CALLOUT_MAJOR0 155
#define RIO_NORMAL_MAJOR1  156
#define RIO_CALLOUT_MAJOR1 157
#endif

#ifndef PCI_DEVICE_ID_SPECIALIX_SX_XIO_IO8
#define PCI_DEVICE_ID_SPECIALIX_SX_XIO_IO8 0x2000
#endif

#ifndef RIO_WINDOW_LEN 
#define RIO_WINDOW_LEN 0x10000
#endif


/* Configurable options: 
   (Don't be too sure that it'll work if you toggle them) */

/* Am I paranoid or not ? ;-) */
#undef RIO_PARANOIA_CHECK


/* 20 -> 2000 per second. The card should rate-limit interrupts at 1000
   Hz, but it is user configurable. I don't recommend going above 1000
   Hz. The interrupt ratelimit might trigger if the interrupt is
   shared with a very active other device. 
   undef this if you want to disable the check....
*/
#define IRQ_RATE_LIMIT 200

#if 0
/* Not implemented */
/* 
 * The following defines are mostly for testing purposes. But if you need
 * some nice reporting in your syslog, you can define them also.
 */
#define RIO_REPORT_FIFO
#define RIO_REPORT_OVERRUN
#endif 


/* These constants are derived from SCO Source */
static struct Conf
RIOConf =
{
  /* locator */         "RIO Config here",
  /* startuptime */     HZ*2,           /* how long to wait for card to run */
  /* slowcook */        0,              /* TRUE -> always use line disc. */
  /* intrpolltime */    1,              /* The frequency of OUR polls */
  /* breakinterval */   25,             /* x10 mS */
  /* timer */           10,             /* mS */
  /* RtaLoadBase */     0x7000,
  /* HostLoadBase */    0x7C00,
  /* XpHz */            5,              /* number of Xprint hits per second */
  /* XpCps */           120,            /* Xprint characters per second */
  /* XpOn */            "\033d#",       /* start Xprint for a wyse 60 */
  /* XpOff */           "\024",         /* end Xprint for a wyse 60 */
  /* MaxXpCps */        2000,           /* highest Xprint speed */
  /* MinXpCps */        10,             /* slowest Xprint speed */
  /* SpinCmds */        1,              /* non-zero for mega fast boots */
  /* First Addr */      0x0A0000,       /* First address to look at */
  /* Last Addr */       0xFF0000,       /* Last address looked at */
  /* BufferSize */      1024,           /* Bytes per port of buffering */
  /* LowWater */        256,            /* how much data left before wakeup */
  /* LineLength */      80,             /* how wide is the console? */
  /* CmdTimeout */      HZ,             /* how long a close command may take */
};




/* Function prototypes */

static void rio_disable_tx_interrupts (void * ptr); 
static void rio_enable_tx_interrupts (void * ptr); 
static void rio_disable_rx_interrupts (void * ptr); 
static void rio_enable_rx_interrupts (void * ptr); 
static int  rio_get_CD (void * ptr); 
static void rio_shutdown_port (void * ptr);
static int  rio_set_real_termios (void  *ptr);
static void rio_hungup (void  *ptr);
static void rio_close (void  *ptr);
static int rio_chars_in_buffer (void * ptr);
static int rio_fw_ioctl (struct inode *inode, struct file *filp,
		         unsigned int cmd, unsigned long arg);
static int rio_fw_open(struct inode *inode, struct file *filp);
static INT rio_fw_release(struct inode *inode, struct file *filp);
static int rio_init_drivers(void);


void my_hd (void *addr, int len);



static struct tty_driver rio_driver, rio_callout_driver;
static struct tty_driver rio_driver2, rio_callout_driver2;

static struct tty_struct * rio_table[RIO_NPORTS] = { NULL, };
static struct termios ** rio_termios;
static struct termios ** rio_termios_locked;

/* The name "p" is a bit non-descript. But that's what the rio-lynxos
sources use all over the place. */
struct rio_info *p;

/* struct rio_board boards[RIO_HOSTS]; */
struct rio_port *rio_ports;

int rio_refcount;
int rio_initialized = 0;
int rio_nports = 0;
int rio_debug = 0;


/* You can have the driver poll your card. 
    - Set rio_poll to 1 to poll every timer tick (10ms on Intel). 
      This is used when the card cannot use an interrupt for some reason.
*/
int rio_poll = 1;


/* These are the only open spaces in my computer. Yours may have more
   or less.... */
int rio_probe_addrs[]= {0xc0000, 0xd0000, 0xe0000};

#define NR_RIO_ADDRS (sizeof(rio_probe_addrs)/sizeof (int))


/* Set the mask to all-ones. This alas, only supports 32 interrupts. 
   Some architectures may need more. -- Changed to LONG to
   support up to 64 bits on 64bit architectures. -- REW 20/06/99 */
long rio_irqmask = -1;

#ifndef TWO_ZERO
#ifdef MODULE
MODULE_PARM(rio_poll, "i");
MODULE_PARM(rio_debug, "i");
MODULE_PARM(rio_irqmask, "i");
#endif
#endif

static struct real_driver rio_real_driver = {
  rio_disable_tx_interrupts,
  rio_enable_tx_interrupts,
  rio_disable_rx_interrupts,
  rio_enable_rx_interrupts,
  rio_get_CD,
  rio_shutdown_port, 
  rio_set_real_termios, 
  rio_chars_in_buffer,
  rio_close,
  rio_hungup,
  NULL
};


/* 
   This driver can spew a whole lot of debugging output at you. If you
   need maximum performance, you should disable the DEBUG define. To
   aid in debugging in the field, I'm leaving the compile-time debug
   features enabled, and disable them "runtime". That allows me to
   instruct people with problems to enable debugging without requiring
   them to recompile... 
*/
#define DEBUG

#ifdef DEBUG
#define rio_dprintk(f, str...) if (rio_debug & f) printk (str)
#else
#define rio_dprintk(f, str...) /* nothing */
#endif


#define func_enter() rio_dprintk (RIO_DEBUG_FLOW, "rio: enter " __FUNCTION__ "\n")
#define func_exit()  rio_dprintk (RIO_DEBUG_FLOW, "rio: exit  " __FUNCTION__ "\n")

#define func_enter2() rio_dprintk (RIO_DEBUG_FLOW, "rio: enter " __FUNCTION__ \
                                  "(port %d)\n", port->line)




/* 
 *  Firmware loader driver specific routines
 *
 */

static struct file_operations rio_fw_fops = {
	NULL,	/*	lseek	*/
	NULL,	/*	read	*/
	NULL,	/*	write	*/
	NULL,	/*	readdir	*/
	NULL,	/*	select	*/
	rio_fw_ioctl,
	NULL,	/*	mmap	*/
	rio_fw_open,
#ifndef TWO_ZERO
	NULL,	/*	flush	*/
#endif
	rio_fw_release,
	NULL,	/*	fsync	*/
	NULL,	/*	fasync	*/
	NULL,	/*	check_media_change	*/
	NULL,	/*	revalidate	*/
};

struct miscdevice rio_fw_device = {
	RIOCTL_MISC_MINOR, "rioctl", &rio_fw_fops
};





#ifdef RIO_PARANOIA_CHECK

/* This doesn't work. Who's paranoid around here? Not me! */

static inline int rio_paranoia_check(struct rio_port const * port,
				    kdev_t device, const char *routine)
{

  static const char *badmagic =
    KERN_ERR "rio: Warning: bad rio port magic number for device %s in %s\n";
  static const char *badinfo =
    KERN_ERR "rio: Warning: null rio port for device %s in %s\n";
 
  if (!port) {
    printk(badinfo, kdevname(device), routine);
    return 1;
  }
  if (port->magic != RIO_MAGIC) {
    printk(badmagic, kdevname(device), routine);
    return 1;
  }

  return 0;
}
#else
#define rio_paranoia_check(a,b,c) 0
#endif


#ifdef DEBUG
void my_hd (void *ad, int len)
{
  int i, j, ch;
  unsigned char *addr = ad;
  
  for (i=0;i<len;i+=16) {
    printk ("%08x ", (int) addr+i);
    for (j=0;j<16;j++) {
      printk ("%02x %s", addr[j+i], (j==7)?" ":"");
    }
    for (j=0;j<16;j++) {
      ch = addr[j+i];
      printk ("%c", (ch < 0x20)?'.':((ch > 0x7f)?'.':ch));
    }
    printk ("\n");
  }
}
#else
#define my_hd(ad,len) do{/* nothing*/ } while (0)
#endif


/* Delay a number of jiffies, allowing a signal to interrupt */ 
int RIODelay (struct Port *PortP, int njiffies)
{
  func_enter ();

  rio_dprintk (RIO_DEBUG_DELAY, "delaying %d jiffies\n", njiffies);  
  current->state = TASK_INTERRUPTIBLE;
  schedule_timeout(njiffies);
  current->state = TASK_RUNNING;
  func_exit();

  if (signal_pending(current))
    return RIO_FAIL;
  else
    return !RIO_FAIL;
}


/* Delay a number of jiffies, disallowing a signal to interrupt */ 
int RIODelay_ni (struct Port *PortP, int njiffies)
{
  func_enter ();

  rio_dprintk (RIO_DEBUG_DELAY, "delaying %d jiffies (ni)\n", njiffies);  
  current->state = TASK_UNINTERRUPTIBLE;
  schedule_timeout(njiffies);
  current->state = TASK_RUNNING;
  func_exit();
  return !RIO_FAIL;
}


int rio_minor (kdev_t device)
{
  return MINOR (device) + 
    256 * ((MAJOR (device) == RIO_NORMAL_MAJOR1) ||
	   (MAJOR (device) == RIO_CALLOUT_MAJOR1));
}


int rio_ismodem (kdev_t device)
{
  return (MAJOR (device) != RIO_NORMAL_MAJOR0) &&
         (MAJOR (device) != RIO_NORMAL_MAJOR1);
}


void rio_udelay (int usecs)
{
  udelay (usecs);
}


void rio_inc_mod_count (void)
{
  func_enter ();
  MOD_INC_USE_COUNT; 
  func_exit ();
}


void rio_dec_mod_count (void)
{
  func_enter ();
  MOD_DEC_USE_COUNT; 
  func_exit ();
}


static int rio_set_real_termios (void *ptr)
{
  int rv;
  func_enter();

  rv = RIOParam( (struct Port *) ptr, CONFIG, 0, 1);

  func_exit ();

  return rv;
}


void rio_reset_interrupt (struct Host *HostP)
{
  switch( HostP->Type ) {
  case RIO_AT:
  case RIO_MCA:
  case RIO_PCI:
    WBYTE(HostP->ResetInt , 0xff);
  }
}


static void rio_interrupt (int irq, void *ptr, struct pt_regs *regs)
{
  struct Host *HostP;

  HostP = &p->RIOHosts[(long)ptr];
  /*   func_enter ();  */
  rio_dprintk (RIO_DEBUG_IFLOW, "rio: enter rio_interrupt (%d/%d)\n", 
               irq, HostP->Ivec); 

  /* AAargh! The order in which to do these things is essential and
     not trivial. 
     
     - Rate limit goes before "recursive". Otherwise a series of
       recursive calls will hang the machine in the interrupt routine. 

     - hardware twiddling goes before "recursive". Otherwise when we
       poll the card, and a recursive interrupt happens, we wont
       ack the card, so it might keep on interrupting us. (especially
       level sensitive interrupt systems like PCI).

     - Rate limit goes before hardware twiddling. Otherwise we won't
       catch a card that has gone bonkers.

     - The "initialized" test goes after the hardware twiddling. Otherwise
       the card will stick us in the interrupt routine again.

     - The initialized test goes before recursive. 
  */



#ifdef IRQ_RATE_LIMIT
  /* Aaargh! I'm ashamed. This costs more lines-of-code than the
     actual interrupt routine!. (Well, used to when I wrote that comment) */
  {
    static int lastjif;
    static int nintr=0;

    if (lastjif == jiffies) {
      if (++nintr > IRQ_RATE_LIMIT) {
        free_irq (HostP->Ivec, ptr);
        printk (KERN_ERR "rio: Too many interrupts. Turning off interrupt %d.\n", 
                HostP->Ivec);
      }
    } else {
      lastjif = jiffies;
      nintr = 0;
    }
  }
#endif

  if (HostP->Ivec == irq) {
    /* Tell the card we've noticed the interrupt. */
    rio_reset_interrupt (HostP);
  }

  if ((HostP->Flags & RUN_STATE) != RC_RUNNING) return;

  if (test_and_set_bit (RIO_BOARD_INTR_LOCK, &HostP->locks)) {
    printk (KERN_ERR "Recursive interrupt! (host %d/irq%d)\n", 
            (int) ptr, HostP->Ivec);
    return;
  }

  RIOServiceHost(p, HostP, irq);

  rio_dprintk ( RIO_DEBUG_IFLOW, "riointr() doing host %d type %d\n", 
                (int) ptr, HostP->Type);

  clear_bit (RIO_BOARD_INTR_LOCK, &HostP->locks);
  rio_dprintk (RIO_DEBUG_IFLOW, "rio: exit rio_interrupt (%d/%d)\n", 
               irq, HostP->Ivec); 
  /*  func_exit ();  */
}


static void rio_pollfunc (unsigned long data)
{
  func_enter ();

  rio_interrupt (0, (void *)data, NULL);
  p->RIOHosts[data].timer.expires = jiffies + rio_poll;
  add_timer (&p->RIOHosts[data].timer);

  func_exit ();
}


/* ********************************************************************** *
 *                Here are the routines that actually                     *
 *              interface with the generic_serial driver                  *
 * ********************************************************************** */

/* Ehhm. I don't know how to fiddle with interrupts on the Specialix 
   cards. ....   Hmm. Ok I figured it out. You don't.  -- REW */

static void rio_disable_tx_interrupts (void * ptr) 
{
  func_enter();

  /*  port->gs.flags &= ~GS_TX_INTEN; */

  func_exit();
}


static void rio_enable_tx_interrupts (void * ptr) 
{
  struct Port *PortP = ptr;
  /* int hn; */

  func_enter();

  /* hn = PortP->HostP - p->RIOHosts;

     rio_dprintk (RIO_DEBUG_TTY, "Pushing host %d\n", hn);
     rio_interrupt (-1,(void *) hn, NULL); */

  RIOTxEnable((char *) PortP);

  /* 
   * In general we cannot count on "tx empty" interrupts, although
   * the interrupt routine seems to be able to tell the difference. 
   */
  PortP->gs.flags &= ~GS_TX_INTEN;

  func_exit();
}


static void rio_disable_rx_interrupts (void * ptr) 
{
  func_enter();
  func_exit();
}

static void rio_enable_rx_interrupts (void * ptr) 
{
  /*  struct rio_port *port = ptr; */
  func_enter();
  func_exit();
}


/* Jeez. Isn't this simple?  */
static int rio_get_CD (void * ptr) 
{
  struct Port *PortP = ptr;
  int rv;

  func_enter();
  rv = (PortP->ModemState & MSVR1_CD) != 0;

  rio_dprintk (RIO_DEBUG_INIT, "Getting CD status: %d\n", rv);
  
  func_exit();  
  return rv;
}


/* Jeez. Isn't this simple? Actually, we can sync with the actual port
   by just pushing stuff into the queue going to the port... */
static int rio_chars_in_buffer (void * ptr) 
{
  func_enter();

  func_exit();  
  return 0;
}


/* Nothing special here... */
static void rio_shutdown_port (void * ptr) 
{
  func_enter();

#if 0
  port->gs.flags &= ~ GS_ACTIVE;
  if (!port->gs.tty) {
    printk ("No tty.\n");
    return;
  }
  if (!port->gs.tty->termios) {
    printk ("No termios.\n");
    return;
  }
  if (port->gs.tty->termios->c_cflag & HUPCL) {
    rio_setsignals (port, 0, 0);
  }
#endif

  func_exit();
}



/* ********************************************************************** *
 *                Here are the routines that actually                     *
 *               interface with the rest of the system                    *
 * ********************************************************************** */


static int rio_fw_open(struct inode *inode, struct file *filp)
{
  func_enter ();
  rio_inc_mod_count ();
  func_exit ();
  return 0;
}


static INT rio_fw_release(struct inode *inode, struct file *filp)
{
  func_enter ();
  rio_dec_mod_count ();
  func_exit ();
  return NO_ERROR;
}


/* I haven't the foggiest why the decrement use count has to happen
   here. The whole linux serial drivers stuff needs to be redesigned.
   My guess is that this is a hack to minimize the impact of a bug
   elsewhere. Thinking about it some more. (try it sometime) Try
   running minicom on a serial port that is driven by a modularized
   driver. Have the modem hangup. Then remove the driver module. Then
   exit minicom.  I expect an "oops".  -- REW */
static void rio_hungup (void *ptr)
{
  func_enter ();
  /* rio_dec_mod_count (); */
  func_exit ();
}


/* The standard serial_close would become shorter if you'd wrap it like
   this. 
   rs_close (...){save_flags;cli;real_close();dec_use_count;restore_flags;}
 */
static void rio_close (void *ptr)
{
  func_enter ();
  riotclose (ptr);
  rio_dec_mod_count ();
  func_exit ();
}



static int rio_fw_ioctl (struct inode *inode, struct file *filp,
		         unsigned int cmd, unsigned long arg)
{
  int rc = 0;
  func_enter();

  /* The "dev" argument isn't used. */
  rc = -riocontrol (p, 0, cmd, (void *)arg, suser ());

  func_exit ();
  return rc;
}


static int rio_ioctl (struct tty_struct * tty, struct file * filp, 
                     unsigned int cmd, unsigned long arg)
{
#if 0
  int rc;
  struct rio_port *port = tty->driver_data;
  int ival;

  /* func_enter2(); */


  rc  = 0;
  switch (cmd) {
  case TIOCGSOFTCAR:
    rc = Put_user(((tty->termios->c_cflag & CLOCAL) ? 1 : 0),
                  (unsigned int *) arg);
    break;
  case TIOCSSOFTCAR:
    if ((rc = verify_area(VERIFY_READ, (void *) arg,
                          sizeof(int))) == 0) {
      Get_user(ival, (unsigned int *) arg);
      tty->termios->c_cflag =
        (tty->termios->c_cflag & ~CLOCAL) |
        (ival ? CLOCAL : 0);
    }
    break;
  case TIOCGSERIAL:
    if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
                          sizeof(struct serial_struct))) == 0)
      gs_getserial(&port->gs, (struct serial_struct *) arg);
    break;
  case TIOCSSERIAL:
    if ((rc = verify_area(VERIFY_READ, (void *) arg,
                          sizeof(struct serial_struct))) == 0)
      rc = gs_setserial(&port->gs, (struct serial_struct *) arg);
    break;
  case TIOCMGET:
    if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
                          sizeof(unsigned int))) == 0) {
      ival = rio_getsignals(port);
      put_user(ival, (unsigned int *) arg);
    }
    break;
  case TIOCMBIS:
    if ((rc = verify_area(VERIFY_READ, (void *) arg,
                          sizeof(unsigned int))) == 0) {
      Get_user(ival, (unsigned int *) arg);
      rio_setsignals(port, ((ival & TIOCM_DTR) ? 1 : -1),
                           ((ival & TIOCM_RTS) ? 1 : -1));
    }
    break;
  case TIOCMBIC:
    if ((rc = verify_area(VERIFY_READ, (void *) arg,
                          sizeof(unsigned int))) == 0) {
      Get_user(ival, (unsigned int *) arg);
      rio_setsignals(port, ((ival & TIOCM_DTR) ? 0 : -1),
                           ((ival & TIOCM_RTS) ? 0 : -1));
    }
    break;
  case TIOCMSET:
    if ((rc = verify_area(VERIFY_READ, (void *) arg,
                          sizeof(unsigned int))) == 0) {
      Get_user(ival, (unsigned int *) arg);
      rio_setsignals(port, ((ival & TIOCM_DTR) ? 1 : 0),
                           ((ival & TIOCM_RTS) ? 1 : 0));
    }
    break;

  default:
    rc = -ENOIOCTLCMD;
    break;
  }
  /* func_exit(); */
  return rc;
#else
  return -ENOIOCTLCMD;
#endif

}


/* The throttle/unthrottle scheme for the Specialix card is different
 * from other drivers and deserves some explanation. 
 * The Specialix hardware takes care of XON/XOFF
 * and CTS/RTS flow control itself.  This means that all we have to
 * do when signalled by the upper tty layer to throttle/unthrottle is
 * to make a note of it here.  When we come to read characters from the
 * rx buffers on the card (rio_receive_chars()) we look to see if the
 * upper layer can accept more (as noted here in rio_rx_throt[]). 
 * If it can't we simply don't remove chars from the cards buffer. 
 * When the tty layer can accept chars, we again note that here and when
 * rio_receive_chars() is called it will remove them from the cards buffer.
 * The card will notice that a ports buffer has drained below some low
 * water mark and will unflow control the line itself, using whatever
 * flow control scheme is in use for that port. -- Simon Allen
 */

static void rio_throttle (struct tty_struct * tty)
{
  struct Port *port = (struct Port *)tty->driver_data;
  
  func_enter();
  /* If the port is using any type of input flow
   * control then throttle the port.
   */

  if((tty->termios->c_cflag & CRTSCTS) || (I_IXOFF(tty)) ) {
    port->State |= RIO_THROTTLE_RX;
  }

  func_exit();
}


static void rio_unthrottle (struct tty_struct * tty)
{
  struct Port *port = (struct Port *)tty->driver_data;

  func_enter();
  /* Always unthrottle even if flow control is not enabled on
   * this port in case we disabled flow control while the port
   * was throttled
   */

  port->State &= ~RIO_THROTTLE_RX;

  func_exit();
  return;
}





/* ********************************************************************** *
 *                    Here are the initialization routines.               *
 * ********************************************************************** */


struct vpd_prom *get_VPD_PROM (struct Host *hp)
{
  static struct vpd_prom vpdp;
  char *p;
  int i;

  func_enter();
  rio_dprintk (RIO_DEBUG_PROBE, "Going to verify vpd prom at %p.\n", 
              hp->Caddr + RIO_VPD_ROM);

  p = (char *) &vpdp;
  for (i=0;i< sizeof (struct vpd_prom);i++)
    *p++ = readb (hp->Caddr+RIO_VPD_ROM + i*2);
      /* read_rio_byte (hp, RIO_VPD_ROM + i*2); */

  /* Terminate the identifier string. 
     *** requires one extra byte in struct vpd_prom *** */
  *p++=0; 

  if (rio_debug & RIO_DEBUG_PROBE)
    my_hd ((char *)&vpdp, 0x20);

  return &vpdp;
}



static int rio_init_drivers(void)
{
  int error;
  
  func_enter();

  memset(&rio_driver, 0, sizeof(rio_driver));
  rio_driver.magic = TTY_DRIVER_MAGIC;
  rio_driver.driver_name = "specialix_rio";
  rio_driver.name = "ttySR";
  rio_driver.major = RIO_NORMAL_MAJOR0;
  rio_driver.num = 256;
  rio_driver.type = TTY_DRIVER_TYPE_SERIAL;
  rio_driver.subtype = RIO_TYPE_NORMAL;
  rio_driver.init_termios = tty_std_termios;
  rio_driver.init_termios.c_cflag =
    B9600 | CS8 | CREAD | HUPCL | CLOCAL;
  rio_driver.flags = TTY_DRIVER_REAL_RAW;
  rio_driver.refcount = &rio_refcount;
  rio_driver.table = rio_table;
  rio_driver.termios = rio_termios;
  rio_driver.termios_locked = rio_termios_locked;

  rio_driver.open  = riotopen;
  rio_driver.close = gs_close;
  rio_driver.write = gs_write;
  rio_driver.put_char = gs_put_char;
  rio_driver.flush_chars = gs_flush_chars;
  rio_driver.write_room = gs_write_room;
  rio_driver.chars_in_buffer = gs_chars_in_buffer;
  rio_driver.flush_buffer = gs_flush_buffer;
  rio_driver.ioctl = rio_ioctl;
  rio_driver.throttle = rio_throttle;
  rio_driver.unthrottle = rio_unthrottle;
  rio_driver.set_termios = gs_set_termios;
  rio_driver.stop = gs_stop;
  rio_driver.start = gs_start;
  rio_driver.hangup = gs_hangup;

  rio_driver2 = rio_driver;
  rio_driver.major = RIO_NORMAL_MAJOR1;

  rio_callout_driver = rio_driver;
  rio_callout_driver.name = "cusr";
  rio_callout_driver.major = RIO_CALLOUT_MAJOR0;
  rio_callout_driver.subtype = RIO_TYPE_CALLOUT;

  rio_callout_driver2 = rio_callout_driver;
  rio_callout_driver2.major = RIO_CALLOUT_MAJOR1;

  rio_dprintk (RIO_DEBUG_INIT, "set_termios = %p\n", gs_set_termios);

  if ((error = tty_register_driver(&rio_driver))) goto bad1;
  if ((error = tty_register_driver(&rio_driver2))) goto bad2;
  if ((error = tty_register_driver(&rio_callout_driver))) goto bad3;
  if ((error = tty_register_driver(&rio_callout_driver2))) goto bad4;

  func_exit();
  return 0;
  /* 
 bad5:tty_unregister_driver (&rio_callout_driver2); */
 bad4:tty_unregister_driver (&rio_callout_driver);
 bad3:tty_unregister_driver (&rio_driver2);
 bad2:tty_unregister_driver (&rio_driver);
 bad1:printk(KERN_ERR "rio: Couldn't register a rio driver, error = %d\n",
             error);
  return 1;
}


static void * ckmalloc (int size)
{
  void *p;

  p = kmalloc(size, GFP_KERNEL);
  if (p) 
    memset(p, 0, size);
  return p;
}



static int rio_init_datastructures (void)
{
  int i;
  struct Port *port;
  func_enter();

  /* Many drivers statically allocate the maximum number of ports
     There is no reason not to allocate them dynamically. Is there? -- REW */
  /* However, the RIO driver allows users to configure their first
     RTA as the ports numbered 504-511. We therefore need to allocate 
     the whole range. :-(   -- REW */

#define RI_SZ   sizeof(struct rio_info)
#define HOST_SZ sizeof(struct Host)
#define PORT_SZ sizeof(struct Port *)
#define TMIO_SZ sizeof(struct termios *)
  rio_dprintk (RIO_DEBUG_INIT, "getting : %d %d %d %d %d bytes\n", 
               RI_SZ, 
               RIO_HOSTS * HOST_SZ,
               RIO_PORTS * PORT_SZ,
               RIO_PORTS * TMIO_SZ,
               RIO_PORTS * TMIO_SZ);
  
  if (!(p                  = ckmalloc (              RI_SZ))) goto free0;
  if (!(p->RIOHosts        = ckmalloc (RIO_HOSTS * HOST_SZ))) goto free1;
  if (!(p->RIOPortp        = ckmalloc (RIO_PORTS * PORT_SZ))) goto free2;
  if (!(rio_termios        = ckmalloc (RIO_PORTS * TMIO_SZ))) goto free3;
  if (!(rio_termios_locked = ckmalloc (RIO_PORTS * TMIO_SZ))) goto free4;
  p->RIOConf = RIOConf;
  rio_dprintk (RIO_DEBUG_INIT, "Got : %p %p %p %p %p\n", 
               p, p->RIOHosts, p->RIOPortp, rio_termios, rio_termios);

  /* Adjust the values in the "driver" */
  rio_driver.termios = rio_termios;
  rio_driver.termios_locked = rio_termios_locked;
  
#if 1
  for (i = 0; i < RIO_PORTS; i++) {
    port = p->RIOPortp[i] = ckmalloc (sizeof (struct Port));
    if (!port) {
      goto free6;
    }
    rio_dprintk (RIO_DEBUG_INIT, "initing port %d (%d)\n", i, port->Mapped);
    port->PortNum = i;
    port->gs.callout_termios = tty_std_termios;
    port->gs.normal_termios  = tty_std_termios;
    port->gs.magic = RIO_MAGIC;
    port->gs.close_delay = HZ/2;
    port->gs.closing_wait = 30 * HZ;
    port->gs.rd = &rio_real_driver;
  }
#else
  /* We could postpone initializing them to when they are configured. */
#endif

  if (rio_debug & RIO_DEBUG_INIT) {
    my_hd (&rio_real_driver, sizeof (rio_real_driver));
  }

  
  func_exit();
  return 0;

 free6:for (i--;i>=0;i--)
        kfree (p->RIOPortp[i]);
/*free5: */
       kfree (rio_termios_locked); 
 free4:kfree (rio_termios);
 free3:kfree (p->RIOPortp);
 free2:kfree (p->RIOHosts);
 free1:kfree (p);
 free0:
  rio_dprintk (RIO_DEBUG_INIT, "Not enough memory! %p %p %p %p %p\n", 
               p, p->RIOHosts, p->RIOPortp, rio_termios, rio_termios);
  return -ENOMEM;
}


static void rio_release_drivers(void)
{
  func_enter();
  tty_unregister_driver (&rio_callout_driver2);
  tty_unregister_driver (&rio_callout_driver);
  tty_unregister_driver (&rio_driver2);
  tty_unregister_driver (&rio_driver);
  func_exit();
}

#ifdef TWO_ZERO
#define PDEV unsigned char pci_bus, unsigned pci_fun
#define pdev pci_bus, pci_fun
#else
#define PDEV   struct pci_dev *pdev
#endif


#ifdef CONFIG_PCI
 /* This was written for SX, but applies to RIO too...
    (including bugs....)

    There is another bit besides Bit 17. Turning that bit off
    (on boards shipped with the fix in the eeprom) results in a 
    hang on the next access to the card. 
 */

 /******************************************************** 
 * Setting bit 17 in the CNTRL register of the PLX 9050  * 
 * chip forces a retry on writes while a read is pending.*
 * This is to prevent the card locking up on Intel Xeon  *
 * multiprocessor systems with the NX chipset.    -- NV  *
 ********************************************************/

/* Newer cards are produced with this bit set from the configuration
   EEprom.  As the bit is read/write for the CPU, we can fix it here,
   if we detect that it isn't set correctly. -- REW */

void fix_rio_pci (PDEV)
{
  unsigned int hwbase;
  unsigned long rebase;
  unsigned int t;

#define CNTRL_REG_OFFSET        0x50
#define CNTRL_REG_GOODVALUE     0x00260000

  pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &hwbase);
  hwbase &= PCI_BASE_ADDRESS_MEM_MASK;
  rebase =  (ulong) ioremap(hwbase, 0x80);
  t = readl (rebase + CNTRL_REG_OFFSET);
  if (t != CNTRL_REG_GOODVALUE) {
    printk (KERN_DEBUG "rio: performing cntrl reg fix: %08x -> %08x\n", 
            t, CNTRL_REG_GOODVALUE); 
    writel (CNTRL_REG_GOODVALUE, rebase + CNTRL_REG_OFFSET);  
  }
  my_iounmap (hwbase, rebase);
}
#endif


#ifdef MODULE
#define rio_init init_module
#endif

extern int gs_debug;

int rio_init(void) 
{
  int found = 0;
  int i;
  struct Host *hp;
  int retval;
  struct vpd_prom *vpdp;
  int okboard;


#ifdef CONFIG_PCI
#ifndef TWO_ZERO
  struct pci_dev *pdev = NULL;
#else
  unsigned char pci_bus, pci_fun;
  /* in 2.2.x pdev is a pointer defining a PCI device. In 2.0 its the bus/fn */
#endif
  unsigned int tint;
  unsigned short tshort;
#endif

  func_enter();
  rio_dprintk (RIO_DEBUG_INIT, "Initing rio module... (rio_debug=%d)\n", 
	       rio_debug);
  gs_debug = rio_debug >> 24;

  if (abs ((long) (&rio_debug) - rio_debug) < 0x10000) {
    printk (KERN_WARNING "rio: rio_debug is an address, instead of a value. "
            "Assuming -1. Was %x/%p.\n", rio_debug, &rio_debug);
    rio_debug=-1;
  }

  retval = rio_init_datastructures ();
  if (retval < 0) return retval;

#ifdef CONFIG_PCI
  if (pci_present ()) {
    /* First look for the JET devices: */
#ifndef TWO_ZERO
    while ((pdev = pci_find_device (PCI_VENDOR_ID_SPECIALIX, 
                                    PCI_DEVICE_ID_SPECIALIX_SX_XIO_IO8, 
                                    pdev))) {
#else
    for (i=0;i< RIO_NBOARDS;i++) {
      if (pcibios_find_device (PCI_VENDOR_ID_SPECIALIX, 
			       PCI_DEVICE_ID_SPECIALIX_SX_XIO_IO8, i,
			       &pci_bus, &pci_fun)) break;
#endif
      /* Specialix has a whole bunch of cards with
         0x2000 as the device ID. They say its because
         the standard requires it. Stupid standard. */
      /* It seems that reading a word doesn't work reliably on 2.0.
         Also, reading a non-aligned dword doesn't work. So we read the
         whole dword at 0x2c and extract the word at 0x2e (SUBSYSTEM_ID)
         ourselves */
      /* I don't know why the define doesn't work, constant 0x2c does --REW */ 
      pci_read_config_dword (pdev, 0x2c, &tint);
      tshort = (tint >> 16) & 0xffff;
      rio_dprintk (RIO_DEBUG_PROBE, "Got a specialix card: %x.\n", tint);
      /* rio_dprintk (RIO_DEBUG_PROBE, "pdev = %d/%d  (%x)\n", pdev, tint); */ 
      if (tshort != 0x0100) {
        rio_dprintk (RIO_DEBUG_PROBE, "But it's not a RIO card (%d)...\n", 
                    tshort);
        continue;
      }
      rio_dprintk (RIO_DEBUG_PROBE, "cp1\n");

      pci_read_config_dword(pdev, PCI_BASE_ADDRESS_2, &tint);

      hp = &p->RIOHosts[p->RIONumHosts];
      hp->PaddrP =  tint & PCI_BASE_ADDRESS_MEM_MASK;
      hp->Ivec = get_irq (pdev);
      if (((1 << hp->Ivec) & rio_irqmask) == 0) hp->Ivec = 0;
      hp->CardP	= (struct DpRam *)
      hp->Caddr = ioremap(p->RIOHosts[p->RIONumHosts].PaddrP, RIO_WINDOW_LEN);
      hp->Type  = RIO_PCI;
      hp->Copy  = rio_pcicopy; 
      hp->Mode  = RIO_PCI_DEFAULT_MODE;
 
      rio_dprintk (RIO_DEBUG_PROBE, "Going to test it (%p/%p).\n",
                   (void *)p->RIOHosts[p->RIONumHosts].PaddrP,
                   p->RIOHosts[p->RIONumHosts].Caddr);
      if (RIOBoardTest( p->RIOHosts[p->RIONumHosts].PaddrP,
                        p->RIOHosts[p->RIONumHosts].Caddr, 
                        RIO_PCI, 0 ) == RIO_SUCCESS) {
        WBYTE(p->RIOHosts[p->RIONumHosts].ResetInt, 0xff);
        p->RIOHosts[p->RIONumHosts].UniqueNum  =
          ((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[0]) &0xFF)<< 0)|
          ((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[1]) &0xFF)<< 8)|
          ((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[2]) &0xFF)<<16)|
          ((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[3]) &0xFF)<<24);
        rio_dprintk (RIO_DEBUG_PROBE, "Hmm Tested ok, uniqid = %x.\n",
                   p->RIOHosts[p->RIONumHosts].UniqueNum);

#if 1
        fix_rio_pci (pdev);
#endif
        p->RIOLastPCISearch = RIO_SUCCESS;
        p->RIONumHosts++;
        found++;
      } else {
        my_iounmap (p->RIOHosts[p->RIONumHosts].PaddrP, 
                    p->RIOHosts[p->RIONumHosts].Caddr);
      }

#ifdef TWO_ZERO
    }  /* We have two variants with the opening brace, so to prevent */
#else
    }  /* Emacs from getting confused we have two closing braces too. */
#endif

  for (i=0;i<NR_RIO_ADDRS;i++) {
    hp = &p->RIOHosts[p->RIONumHosts];
    hp->PaddrP = rio_probe_addrs[i];
    hp->Ivec = 0;
    hp->CardP = (struct DpRam *)
    hp->Caddr = ioremap(p->RIOHosts[p->RIONumHosts].PaddrP, RIO_WINDOW_LEN);
    hp->Type = RIO_AT;
    hp->Copy = rio_pcicopy;
    hp->Mode = 0;

    vpdp = get_VPD_PROM (hp);

    okboard = 0;
    if ((strncmp (vpdp->identifier, RIO_ISA_IDENT, 16) == 0) ||
        (strncmp (vpdp->identifier, RIO_ISA2_IDENT, 16) == 0) ||
        (strncmp (vpdp->identifier, RIO_ISA3_IDENT, 16) == 0)) {
      /* Board is present... */
      if (RIOBoardTest (hp->PaddrP, 
                        hp->Caddr, RIO_AT, 0) == RIO_SUCCESS) {
        /* ... and feeling fine!!!! */
        rio_dprintk (RIO_DEBUG_PROBE, "Hmm Tested ok, uniqid = %x.\n",
                   p->RIOHosts[p->RIONumHosts].UniqueNum);
        if (RIOAssignAT(p, hp->PaddrP, hp->Caddr, 0)) {        
          rio_dprintk (RIO_DEBUG_PROBE, "Hmm Tested ok, host%d uniqid = %x.\n",
                       p->RIONumHosts, 
                       p->RIOHosts[p->RIONumHosts-1].UniqueNum);
          okboard++;
          found++;
        }
      }

    if (!okboard)
      my_iounmap (hp->PaddrP, hp->Caddr);
    }
  }



#if 0 
#ifndef TWO_ZERO

  /* These older PCI cards have problems (only byte-mode access is
     supported), which makes them a bit hard to support. Please
     contact Specialix or BitWizard if you have one of these. We'll
     see what we can work out.... */

  /* This section hasn't been tested/fixed. It won't work 
     out-of-the-box. */

    /* Then look for the older RIO/PCI devices: */
    while ((pdev = pci_find_device (PCI_VENDOR_ID_SPECIALIX, 
                                    PCI_DEVICE_ID_SPECIALIX_RIO, 
                                    pdev))) {
#else
    for (i=0;i< RIO_NBOARDS;i++) {
      if (pcibios_find_device (PCI_VENDOR_ID_SPECIALIX, 
			       PCI_DEVICE_ID_SPECIALIX_RIO, i,
			       &pci_bus, &pci_fun)) break;
#endif
      /* Specialix has a whole bunch of cards with
         0x2000 as the device ID. They say its because
         the standard requires it. Stupid standard. */
      /* It seems that reading a word doesn't work reliably on 2.0.
         Also, reading a non-aligned dword doesn't work. So we read the
         whole dword at 0x2c and extract the word at 0x2e (SUBSYSTEM_ID)
         ourselves */
      /* I don't know why the define doesn't work, constant 0x2c does --REW */ 
      pci_read_config_dword (pdev, 0x2c, &tint);
      tshort = (tint >> 16) & 0xffff;
      rio_dprintk (RIO_DEBUG_PROBE, "Got a specialix card: %x.\n", tint);
      /* rio_dprintk (RIO_DEBUG_PROBE, "pdev = %d/%d  (%x)\n", pdev, tint); */ 
      if (tshort != 0x0800) {
        rio_dprintk (RIO_DEBUG_PROBE, "But it's not a RIO card (%d)...\n", 
                    tshort);
        continue;
      }
      board = &boards[found];
      
      pci_read_config_dword(pdev, PCI_BASE_ADDRESS_2, &tint);
      board->hw_base = tint & PCI_BASE_ADDRESS_MEM_MASK;
      board->base =  (ulong) ioremap(board->hw_base, RIO_WINDOW_LEN);
      board->irq  = get_irq (pdev);
      board->flags &= ~RIO_BOARD_TYPE;
      board->flags |=  RIO_PCI_BOARD;

      rio_dprintk (RIO_DEBUG_PROBE, "Got a specialix card: %x/%x(%d).\n", 
                  tint, boards[found].base, board->irq);

      if (found_rio (board)) {
        found++;
      } else 
        my_iounmap (board->hw_base, board->base);
#ifdef TWO_ZERO      
    }
#else
    }
#endif
#endif 

  }
#endif



  for (i=0;i<p->RIONumHosts;i++) {
    hp = &p->RIOHosts[i];
    if (hp->Ivec) {
      if (request_irq (hp->Ivec, rio_interrupt, SA_SHIRQ, "rio", (void *)i)) {
        printk(KERN_ERR "rio: Cannot allocate irq %d.\n", hp->Ivec);
        hp->Ivec = 0;
      }
      rio_dprintk (RIO_DEBUG_INIT, "Got irq %d.\n", hp->Ivec);
    }
    /* Init the timer "always" to make sure that it can safely be 
       deleted when we unload... */

    init_timer (&hp->timer);
    if (!hp->Ivec) {
      rio_dprintk (RIO_DEBUG_INIT, "Starting polling at %dj intervals.\n", 
                   rio_poll);
      hp->timer.data = i;
      hp->timer.function = rio_pollfunc;
      hp->timer.expires = jiffies + rio_poll;
      add_timer (&hp->timer);
    }
  }

  if (found) {
    printk (KERN_INFO "rio: total of %d boards detected.\n", found);

    if (misc_register(&rio_fw_device) < 0) {
      printk(KERN_ERR "RIO: Unable to register firmware loader driver.\n");
      return -EIO;
    }
    rio_init_drivers ();
  }

  func_exit();
  return found?0:-EIO;
}


#ifdef MODULE
void cleanup_module(void)
{
  int i; 
  struct Host *hp;
  
  func_enter();

  for (i=0,hp=p->RIOHosts;i<p->RIONumHosts;i++, hp++) {
    RIOHostReset (hp->Type, hp->CardP, hp->Slot);
    if (hp->Ivec) {
      free_irq (hp->Ivec, (void *)i);
      rio_dprintk (RIO_DEBUG_INIT, "freed irq %d.\n", hp->Ivec);
    }
    /* It is safe/allowed to del_timer a non-active timer */
    del_timer (&hp->timer);
  }

  if (misc_deregister(&rio_fw_device) < 0) {
    printk (KERN_INFO "rio: couldn't deregister control-device\n");
  }


  rio_dprintk (RIO_DEBUG_CLEANUP, "Cleaning up drivers\n");

  rio_release_drivers ();

  /* Release dynamically allocated memory */
  kfree (rio_termios_locked); 
  kfree (rio_termios);
  kfree (p->RIOPortp);
  kfree (p->RIOHosts);
  kfree (p);

  func_exit();
}
#endif



/*
 * Anybody who knows why this doesn't work for me, please tell me -- REW.
 * Snatched from scsi.c (fixed one spelling error):
 * Overrides for Emacs so that we follow Linus' tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local Variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */