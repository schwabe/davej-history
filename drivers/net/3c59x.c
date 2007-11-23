/* EtherLinkXL.c: A 3Com EtherLink PCI III/XL ethernet driver for linux. */
/*
	Written 1996-1998 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the 3Com "Vortex" and "Boomerang" series ethercards.
	Members of the series include Fast EtherLink 3c590/3c592/3c595/3c597
	and the EtherLink XL 3c900 and 3c905 cards.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771
*/

/*
    15Apr00, Andrew Morton <andrewm@uow.edu.au>
    - Don't set RxComplete in boomerang interrupt enable reg
    - Added standard spinlocking.
    - Removed setting/clearing of dev->interrupt
    - Removed vp->in_interrupt
    - spinlock in vortex_timer to protect mdio functions
    - disable local interrupts around call to vortex_interrupt in
      vortex_tx_timeout() (So vortex_interrupt can use spin_lock())
    - Removed global save/restore_flags() and cli() from get_stats
      and vortex_start_xmit()
    - Select window 3 in vortex_timer()'s write to Wn3_MAC_Ctrl
    - In vortex_start_xmit(), move the lock to _after_ we've altered
      vp->cur_tx and vp->tx_full.  This defeats the race between
      vortex_start_xmit() and vortex_interrupt which was identified
      by Bogdan Costescu.
    - Merged back support for six new cards from various sources
    - Tell it that 3c905C has NWAY
    - Fix handling of SetStatusEnd in 'Too much work..' code, as
      per 2.3.99's 3c575_cb (Dave Hinds).  Added vp->deferred for this.

    24May00 <2.2.16-pre4> andrewm
    - Replace union wn3_config with BFINS/BFEXT manipulation for
      sparc64 (Pete Zaitcev, Peter Jones)
    - Use insl_ns/outsl_ns for __powerpc__ (Andreas Tobler)
    - In vortex_error, do_tx_reset and vortex_tx_timeout(Vortex): clear
      tbusy and force a BH rerun to better recover from errors.

    24Jun00 <2.2.16> andrewm
    - Better handling of shared interrupts
    - Reset the transmitter in vortex_error() on both maxcollisions and Tx reclaim error
    - Split the ISR into vortex_interrupt and boomerang_interrupt.  This is
      to fix once-and-for-all the dubious testing of vortex status bits on
      boomerang/hurricane/cyclone/tornado NICs.
    - Fixed crash under OOM during vortex_open() (Mark Hemment)
    - Fix Rx cessation problem during OOM (help from Mark Hemment)

    01Aug00 <2.2.17-pre14> andrewm
    - Added 3c556 support (Fred Maciel)

    16Aug00 <2.2.17-pre17> andrem
    - In vortex_error: don't reset the Tx after txReclaim or maxCollisions errors
    - In vortex_error(do_tx_reset): only reset dev->tbusy for vortex-style NICs.
    - In vortex_open(), set vp->tx_full to zero (else we get errors if the device was
      closed with a full Tx ring).

	17Oct00 <2.2.18-pre16> andrewm
	- Added support for the 3c556B Laptop Hurricane (Louis Gerbarg)
    - Backported transceiver options handling from 2.4.  This changes the semantics
      of forcing full duplex in the `options' parm!  (It's better to use
      `full_duplex' anyway).  See Documentation/vortex.txt (Maciej Rozycki).
    - Set PCI latency timer to maximum for the 3c590 (From Donald's driver)
    - Removed all the CARDBUS code (it's never used).
    - Added INVERT_MII_PWR, EEPROM_8BIT, EEPROM_OFFSET.  Use them.
    - Use EEPROM_8BIT for the 3c555
    - Merged ACPI WOL support from Donald's drivers.
    - Sort-of backported Donald's new medialock code. Called it duplexlock
      and we now prefer to use `full_duplex=[-1,0,1]' to force duplex mode.
    - Merged the rx_oom_timer from 2.4.  This gives better handling of OOM
      conditions.  Backed out the previous way of handling this.
    - Replace suser() with capable(CAP_NET_ADMIN) in ioctl().

    07Jan01 <2.2.19-pre6> andrewm
    - Add HAS_NWAY to "3c900 Cyclone 10Mbps TPO"
    - Added and used wait_for_completion().  3c905CX problems.
    - Removed the code for older kernel versions.
    - Add HAS_NWAY to 3cSOHO100-TX (Brett Frankenberger)
    - Search for phy 24 first for 3c905CX. (D Becker)
    - Don't free skbs we don't own on oom path in vortex_open().
    - Added explicit `medialock' flag so we can truly
      lock the media type down with `options'.
    - In vortex_error(), only reset the up/down load engine, not all the
      interface stuff.
    - Added and used EEPROM_NORESET for 3c556B PM resumes.
    - Enable WOL with the `enable_wol' module parm
    - Give the 3c980 HAS_NWAY

    - See http://www.uow.edu.au/~andrewm/linux/#3c59x-2.2 for more details.
*/

static char version[] =
"3c59x.c 18Feb01 Donald Becker and others http://www.scyld.com/network/vortex.html\n";

/* "Knobs" that adjust features and parameters. */
/* Set the copy breakpoint for the copy-only-tiny-frames scheme.
   Setting to > 1512 effectively disables this feature. */
static const int rx_copybreak = 200;
/* Allow setting MTU to a larger size, bypassing the normal ethernet setup. */
static const int mtu = 1500;
/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 20;

/* Put out somewhat more debugging messages. (0: no msg, 1 minimal .. 6). */
#define vortex_debug debug
#ifdef VORTEX_DEBUG
static int vortex_debug = VORTEX_DEBUG;
#else
static int vortex_debug = 1;
#endif

/* Some values here only for performance evaluation and path-coverage
   debugging. */
static int rx_nocopy = 0, rx_copy = 0, queued_packet = 0, rx_csumhits;

/* A few values that may be tweaked. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  ((400*HZ)/1000)

/* Keep the ring sizes a power of two for efficiency. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <asm/irq.h>			/* For NR_IRQS only. */
#include <asm/bitops.h>
#include <asm/io.h>

/* Kernel compatibility defines, some common to David Hinds' PCMCIA package.
   This is only in the support-all-kernels source code. */

#define RUN_AT(x) (jiffies + (x))


MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("3Com 3c590/3c900 series Vortex/Boomerang driver");
MODULE_PARM(debug, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(enable_wol, "1-" __MODULE_STRING(8) "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(compaq_ioaddr, "i");
MODULE_PARM(compaq_irq, "i");
MODULE_PARM(compaq_device_id, "i");

/* Operational parameter that usually are not changed. */

/* The Vortex size is twice that of the original EtherLinkIII series: the
   runtime register window, window 1, is now always mapped in.
   The Boomerang size is twice as large as the Vortex -- it has additional
   bus master control registers. */
#define VORTEX_TOTAL_SIZE 0x20
#define BOOMERANG_TOTAL_SIZE 0x40

/* Set iff a MII transceiver on any interface requires mdio preamble.
   This only set with the original DP83840 on older 3c905 boards, so the extra
   code size of a per-interface flag is not worthwhile. */
static char mii_preamble_required = 0;

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the 3Com FastEtherLink and FastEtherLink
XL, 3Com's PCI to 10/100baseT adapters.  It also works with the 10Mbs
versions of the FastEtherLink cards.  The supported product IDs are
  3c590, 3c592, 3c595, 3c597, 3c900, 3c905

The related ISA 3c515 is supported with a separate driver, 3c515.c, included
with the kernel source or available from
    cesdis.gsfc.nasa.gov:/pub/linux/drivers/3c515.html

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS should be set to assign the
PCI INTA signal to an otherwise unused system IRQ line.

The EEPROM settings for media type and forced-full-duplex are observed.
The EEPROM media type should be left at the default "autoselect" unless using
10base2 or AUI connections which cannot be reliably detected.

III. Driver operation

The 3c59x series use an interface that's very similar to the previous 3c5x9
series.  The primary interface is two programmed-I/O FIFOs, with an
alternate single-contiguous-region bus-master transfer (see next).

The 3c900 "Boomerang" series uses a full-bus-master interface with separate
lists of transmit and receive descriptors, similar to the AMD LANCE/PCnet,
DEC Tulip and Intel Speedo3.  The first chip version retains a compatible
programmed-I/O interface that has been removed in 'B' and subsequent board
revisions.

One extension that is advertised in a very large font is that the adapters
are capable of being bus masters.  On the Vortex chip this capability was
only for a single contiguous region making it far less useful than the full
bus master capability.  There is a significant performance impact of taking
an extra interrupt or polling for the completion of each transfer, as well
as difficulty sharing the single transfer engine between the transmit and
receive threads.  Using DMA transfers is a win only with large blocks or
with the flawed versions of the Intel Orion motherboard PCI controller.

The Boomerang chip's full-bus-master interface is useful, and has the
currently-unused advantages over other similar chips that queued transmit
packets may be reordered and receive buffer groups are associated with a
single frame.

With full-bus-master support, this driver uses a "RX_COPYBREAK" scheme.
Rather than a fixed intermediate receive buffer, this scheme allocates
full-sized skbuffs as receive buffers.  The value RX_COPYBREAK is used as
the copying breakpoint: it is chosen to trade-off the memory wasted by
passing the full-sized skbuff to the queue layer for all frames vs. the
copying cost of copying a frame to a correctly-sized skbuff.


IIIC. Synchronization
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

IV. Notes

Thanks to Cameron Spitzer and Terry Murphy of 3Com for providing development
3c590, 3c595, and 3c900 boards.
The name "Vortex" is the internal 3Com project name for the PCI ASIC, and
the EISA version is called "Demon".  According to Terry these names come
from rides at the local amusement park.

The new chips support both ethernet (1.5K) and FDDI (4.5K) packet sizes!
This driver only supports ethernet packets because of the skbuff allocation
limit of 4K.
*/

/* This table drives the PCI probe routines.  It's mostly boilerplate in all
   of the drivers, and will likely be provided by some future kernel.
*/
enum pci_flags_bit {
	PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
	PCI_ADDR0=0x10<<0, PCI_ADDR1=0x10<<1, PCI_ADDR2=0x10<<2, PCI_ADDR3=0x10<<3,
};
struct pci_id_info {
	const char *name;
	u16	vendor_id, device_id, device_id_mask, flags;
	int drv_flags, io_size;
	struct device *(*probe1)(int pci_bus, int pci_devfn, struct device *dev,
							 long ioaddr, int irq, int chip_idx, int fnd_cnt);
};

enum { IS_VORTEX=1, IS_BOOMERANG=2, IS_CYCLONE=4, IS_TORNADO=8,
	   HAS_PWR_CTRL=0x10, HAS_MII=0x20, HAS_NWAY=0x40, HAS_CB_FNS=0x80,
	   INVERT_MII_PWR=0x100, EEPROM_8BIT=0x200, EEPROM_OFFSET=0x400,
	   EEPROM_NORESET=0x800};
static struct device *vortex_probe1(int pci_bus, int pci_devfn,
									struct device *dev, long ioaddr,
									int irq, int dev_id, int card_idx);
static struct pci_id_info pci_tbl[] = {
	{"3c590 Vortex 10Mbps",			0x10B7, 0x5900, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_VORTEX, 32, vortex_probe1},
	{"3c592 EISA 10mbps Demon/Vortex", 0x10B7, 0x5920, 0xffff,		/* AKPM: from Don's 3c59x_cb.c 0.49H */
	 PCI_USES_IO|PCI_USES_MASTER, IS_VORTEX, 32, vortex_probe1},
	{"3c597 EISA Fast Demon/Vortex", 0x10B7, 0x5970, 0xffff,		/* AKPM: from Don's 3c59x_cb.c 0.49H */
	 PCI_USES_IO|PCI_USES_MASTER, IS_VORTEX, 32, vortex_probe1},
	{"3c595 Vortex 100baseTx",		0x10B7, 0x5950, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_VORTEX, 32, vortex_probe1},
	{"3c595 Vortex 100baseT4",		0x10B7, 0x5951, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_VORTEX, 32, vortex_probe1},
	{"3c595 Vortex 100base-MII",	0x10B7, 0x5952, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_VORTEX, 32, vortex_probe1},

#define EISA_TBL_OFFSET 6			/* AKPM: the offset of this entry */
	{"3Com Vortex",					0x10B7, 0x5900, 0xff00,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG, 64, vortex_probe1},
	{"3c900 Boomerang 10baseT",		0x10B7, 0x9000, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG, 64, vortex_probe1},
	{"3c900 Boomerang 10Mbps Combo", 0x10B7, 0x9001, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG, 64, vortex_probe1},
	{"3c900 Cyclone 10Mbps Combo", 0x10B7, 0x9005, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE, 128, vortex_probe1},
	{"3c900B-FL Cyclone 10base-FL",	0x10B7, 0x900A, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE, 128, vortex_probe1},
	{"3c900 Cyclone 10Mbps TPO",	0x10B7, 0x9004, 0xffff,			/* AKPM: from Don's 0.99M */
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY, 128, vortex_probe1},
	{"3c900 Cyclone 10Mbps TPC",	0x10B7, 0x9006, 0xffff,			/* AKPM: from Don's 0.99M */
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE, 128, vortex_probe1},
	{"3c905 Boomerang 100baseTx",	0x10B7, 0x9050, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG|HAS_MII, 64, vortex_probe1},
	{"3c905 Boomerang 100baseT4",	0x10B7, 0x9051, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG|HAS_MII, 64, vortex_probe1},
	{"3c905B Cyclone 100baseTx",	0x10B7, 0x9055, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY, 128, vortex_probe1},
	{"3c905B Cyclone 10/100/BNC",	0x10B7, 0x9058, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY, 128, vortex_probe1},
	{"3c905B-FX Cyclone 100baseFx",	0x10B7, 0x905A, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE, 128, vortex_probe1},
	{"3c905C Tornado",	0x10B7, 0x9200, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_TORNADO|HAS_NWAY, 128, vortex_probe1},
	{"3c980 Cyclone",	0x10B7, 0x9800, 0xfff0,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY, 128, vortex_probe1},
	{"3cSOHO100-TX Hurricane",	0x10B7, 0x7646, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY, 128, vortex_probe1},
	{"3c555 Laptop Hurricane",	0x10B7, 0x5055, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|EEPROM_8BIT, 128, vortex_probe1},
	{"3c556 10/100 Mini PCI Adapter",	0x10B7, 0x6055, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_TORNADO|HAS_NWAY|EEPROM_8BIT|HAS_CB_FNS|INVERT_MII_PWR, 128, vortex_probe1},
	{"3c556B Laptop Hurricane",	0x10B7, 0x6056, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_TORNADO|HAS_NWAY|EEPROM_OFFSET|HAS_CB_FNS|INVERT_MII_PWR|EEPROM_NORESET, 128, vortex_probe1},
	{"3c575 Boomerang CardBus",		0x10B7, 0x5057, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG|HAS_MII, 64, vortex_probe1},
	{"3CCFE575 Cyclone CardBus",	0x10B7, 0x5157, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY|HAS_CB_FNS,
	 128, vortex_probe1},
	{"3CCFE656 Cyclone CardBus",	0x10B7, 0x6560, 0xffff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY|HAS_CB_FNS,
	 128, vortex_probe1},
	{"3CCFEM656 Cyclone CardBus",	0x10B7, 0x6562, 0xffff,			/* AKPM: From the 2.3 driver ? */
	 PCI_USES_IO|PCI_USES_MASTER, IS_CYCLONE|HAS_NWAY|HAS_CB_FNS,
	 128, vortex_probe1},
	{"3c575 series CardBus (unknown version)", 0x10B7, 0x5057, 0xf0ff,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG|HAS_MII, 64, vortex_probe1},
	{"3c450 HomePNA Tornado", 0x10B7, 0x4500, 0xffff,				/* AKPM: from Don's 0.99P */
	 PCI_USES_IO|PCI_USES_MASTER, IS_TORNADO|HAS_NWAY, 128, vortex_probe1},
	{"3Com Boomerang (unknown version)",	0x10B7, 0x9000, 0xff00,
	 PCI_USES_IO|PCI_USES_MASTER, IS_BOOMERANG, 64, vortex_probe1},
	{0,},						/* 0 terminated list. */
};

/* Operational definitions.
   These are not used by other compilation units and thus are not
   exported in a ".h" file.

   First the windows.  There are eight register windows, with the command
   and status registers available in each.
   */
#define EL3WINDOW(win_num) outw(SelectWindow + (win_num), ioaddr + EL3_CMD)
#define EL3_CMD 0x0e
#define EL3_STATUS 0x0e

/* The top five bits written to EL3_CMD are a command, the lower
   11 bits are the parameter, if applicable.
   Note that 11 parameters bits was fine for ethernet, but the new chip
   can handle FDDI length frames (~4500 octets) and now parameters count
   32-bit 'Dwords' rather than octets. */

enum vortex_cmd {
	TotalReset = 0<<11, SelectWindow = 1<<11, StartCoax = 2<<11,
	RxDisable = 3<<11, RxEnable = 4<<11, RxReset = 5<<11,
	UpStall = 6<<11, UpUnstall = (6<<11)+1,
	DownStall = (6<<11)+2, DownUnstall = (6<<11)+3,
	RxDiscard = 8<<11, TxEnable = 9<<11, TxDisable = 10<<11, TxReset = 11<<11,
	FakeIntr = 12<<11, AckIntr = 13<<11, SetIntrEnb = 14<<11,
	SetStatusEnb = 15<<11, SetRxFilter = 16<<11, SetRxThreshold = 17<<11,
	SetTxThreshold = 18<<11, SetTxStart = 19<<11,
	StartDMAUp = 20<<11, StartDMADown = (20<<11)+1, StatsEnable = 21<<11,
	StatsDisable = 22<<11, StopCoax = 23<<11, SetFilterBit = 25<<11,};

/* The SetRxFilter command accepts the following classes: */
enum RxFilter {
	RxStation = 1, RxMulticast = 2, RxBroadcast = 4, RxProm = 8 };

/* Bits in the general status register. */
enum vortex_status {
	IntLatch = 0x0001, HostError = 0x0002, TxComplete = 0x0004,
	TxAvailable = 0x0008, RxComplete = 0x0010, RxEarly = 0x0020,
	IntReq = 0x0040, StatsFull = 0x0080,
	DMADone = 1<<8, DownComplete = 1<<9, UpComplete = 1<<10,
	DMAInProgress = 1<<11,			/* DMA controller is still busy.*/
	CmdInProgress = 1<<12,			/* EL3_CMD is still busy.*/
};

/* Register window 1 offsets, the window used in normal operation.
   On the Vortex this window is always mapped at offsets 0x10-0x1f. */
enum Window1 {
	TX_FIFO = 0x10,  RX_FIFO = 0x10,  RxErrors = 0x14,
	RxStatus = 0x18,  Timer=0x1A, TxStatus = 0x1B,
	TxFree = 0x1C, /* Remaining free bytes in Tx buffer. */
};
enum Window0 {
	Wn0EepromCmd = 10,		/* Window 0: EEPROM command register. */
	Wn0EepromData = 12,		/* Window 0: EEPROM results register. */
	IntrStatus=0x0E,		/* Valid in all windows. */
};
enum Win0_EEPROM_bits {
	EEPROM_Read = 0x80, EEPROM_WRITE = 0x40, EEPROM_ERASE = 0xC0,
	EEPROM_EWENB = 0x30,		/* Enable erasing/writing for 10 msec. */
	EEPROM_EWDIS = 0x00,		/* Disable EWENB before 10 msec timeout. */
};
/* EEPROM locations. */
enum eeprom_offset {
	PhysAddr01=0, PhysAddr23=1, PhysAddr45=2, ModelID=3,
	EtherLink3ID=7, IFXcvrIO=8, IRQLine=9,
	NodeAddr01=10, NodeAddr23=11, NodeAddr45=12,
	DriverTune=13, Checksum=15};

enum Window2 {			/* Window 2. */
	Wn2_ResetOptions=12,
};
enum Window3 {			/* Window 3: MAC/config bits. */
	Wn3_Config=0, Wn3_MAC_Ctrl=6, Wn3_Options=8,
};

#define BFEXT(value, offset, bitcount)  \
    ((((unsigned long)(value)) >> (offset)) & ((1 << (bitcount)) - 1))

#define BFINS(lhs, rhs, offset, bitcount)			\
    (((lhs) & ~((((1 << (bitcount)) - 1)) << (offset))) |	\
        (((rhs) & ((1 << (bitcount)) - 1)) << (offset)))

#define RAM_SIZE(v)		BFEXT(v, 0, 3)
#define RAM_WIDTH(v)	BFEXT(v, 3, 1)
#define RAM_SPEED(v)	BFEXT(v, 4, 2)
#define ROM_SIZE(v)		BFEXT(v, 6, 2)
#define RAM_SPLIT(v)	BFEXT(v, 16, 2)
#define XCVR(v)			BFEXT(v, 20, 4)
#define AUTOSELECT(v)	BFEXT(v, 24, 1)

enum Window4 {		/* Window 4: Xcvr/media bits. */
	Wn4_FIFODiag = 4, Wn4_NetDiag = 6, Wn4_PhysicalMgmt=8, Wn4_Media = 10,
};
enum Win4_Media_bits {
	Media_SQE = 0x0008,		/* Enable SQE error counting for AUI. */
	Media_10TP = 0x00C0,	/* Enable link beat and jabber for 10baseT. */
	Media_Lnk = 0x0080,		/* Enable just link beat for 100TX/100FX. */
	Media_LnkBeat = 0x0800,
};
enum Window7 {					/* Window 7: Bus Master control. */
	Wn7_MasterAddr = 0, Wn7_MasterLen = 6, Wn7_MasterStatus = 12,
};
/* Boomerang bus master control registers. */
enum MasterCtrl {
	PktStatus = 0x20, DownListPtr = 0x24, FragAddr = 0x28, FragLen = 0x2c,
	TxFreeThreshold = 0x2f, UpPktStatus = 0x30, UpListPtr = 0x38,
};

/* The Rx and Tx descriptor lists.
   Caution Alpha hackers: these types are 32 bits!  Note also the 8 byte
   alignment contraint on tx_ring[] and rx_ring[]. */
#define LAST_FRAG  0x80000000			/* Last Addr/Len pair in descriptor. */
struct boom_rx_desc {
	u32 next;					/* Last entry points to 0.   */
	s32 status;
	u32 addr;					/* Up to 63 addr/len pairs possible. */
	s32 length;					/* Set LAST_FRAG to indicate last pair. */
};
/* Values for the Rx status entry. */
enum rx_desc_status {
	RxDComplete=0x00008000, RxDError=0x4000,
	/* See boomerang_rx() for actual error bits */
	IPChksumErr=1<<25, TCPChksumErr=1<<26, UDPChksumErr=1<<27,
	IPChksumValid=1<<29, TCPChksumValid=1<<30, UDPChksumValid=1<<31,
};

struct boom_tx_desc {
	u32 next;					/* Last entry points to 0.   */
	s32 status;					/* bits 0:12 length, others see below.  */
	u32 addr;
	s32 length;
};

/* Values for the Tx status entry. */
enum tx_desc_status {
	CRCDisable=0x2000, TxDComplete=0x8000,
	AddIPChksum=0x02000000, AddTCPChksum=0x04000000, AddUDPChksum=0x08000000,
	TxIntrUploaded=0x80000000,		/* IRQ when in FIFO, but maybe not sent. */
};

/* Chip features we care about in vp->capabilities, read from the EEPROM. */
enum ChipCaps { CapBusMaster=0x20, CapPwrMgmt=0x2000 };

struct vortex_private {
	/* The Rx and Tx rings should be quad-word-aligned. */
	struct boom_rx_desc rx_ring[RX_RING_SIZE];
	struct boom_tx_desc tx_ring[TX_RING_SIZE];
	/* The addresses of transmit- and receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	struct device *next_module;
	void *priv_addr;
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	struct net_device_stats stats;
	struct sk_buff *tx_skb;				/* Packet being eaten by bus master ctrl.  */

	/* PCI configuration space information. */
	u8 pci_bus, pci_devfn;				/* PCI bus location, for power management. */
	char *cb_fn_base;					/* CardBus function status addr space. */
	int chip_id;

	/* The remainder are related to chip state, mostly media selection. */
	struct timer_list timer;			/* Media selection timer. */
	struct timer_list rx_oom_timer;		/* Rx skb allocation retry timer */
	int options;						/* User-settable misc. driver options. */
	unsigned int media_override:4, 		/* Passed-in media type. */
		default_media:4,				/* Read from the EEPROM/Wn3_Config. */
		full_duplex:1,					/* User wants FD (or we're running at FD) */
		duplexlock:1,					/* User has forced duplex */
		autoselect:1,					/* Can use NWAY negotiation */
		bus_master:1,					/* Vortex can only do a fragment bus-m. */
		full_bus_master_tx:1, full_bus_master_rx:2, /* Boomerang  */
		tx_full:1,
		enable_wol:1,
		medialock:1;
	u16 status_enable;
	u16 intr_enable;
	u16 available_media;				/* From Wn3_Options. */
	u16 capabilities, info1, info2;		/* Various, from EEPROM. */
	u16 advertising;					/* NWay media advertisement */
	unsigned char phys[2];				/* MII device addresses. */
	u16 deferred;						/* Resend these interrupts when we
										 * bale from the ISR */
	spinlock_t lock;
};

/* The action to take with a media selection timer tick.
   Note that we deviate from the 3Com order by checking 10base2 before AUI.
 */
enum xcvr_types {
	XCVR_10baseT=0, XCVR_AUI, XCVR_10baseTOnly, XCVR_10base2, XCVR_100baseTx,
	XCVR_100baseFx, XCVR_MII=6, XCVR_NWAY=8, XCVR_ExtMII=9, XCVR_Default=10,
};

static struct media_table {
	char *name;
	unsigned int media_bits:16,		/* Bits to set in Wn4_Media register. */
		mask:8,				/* The transceiver-present bit in Wn3_Config.*/
		next:8;				/* The media type to try next. */
	int wait;			/* Time before we check media status. */
} media_tbl[] = {
  {	"10baseT",   Media_10TP,0x08, XCVR_10base2, (14*HZ)/10},
  { "10Mbs AUI", Media_SQE, 0x20, XCVR_Default, (1*HZ)/10},
  { "undefined", 0,			0x80, XCVR_10baseT, 10000},
  { "10base2",   0,			0x10, XCVR_AUI,		(1*HZ)/10},
  { "100baseTX", Media_Lnk, 0x02, XCVR_100baseFx, (14*HZ)/10},
  { "100baseFX", Media_Lnk, 0x04, XCVR_MII,		(14*HZ)/10},
  { "MII",		 0,			0x41, XCVR_10baseT, 3*HZ },
  { "undefined", 0,			0x01, XCVR_10baseT, 10000},
  { "Autonegotiate", 0,		0x41, XCVR_10baseT, 3*HZ},
  { "MII-External",	 0,		0x41, XCVR_10baseT, 3*HZ },
  { "Default",	 0,			0xFF, XCVR_10baseT, 10000},
};

static int vortex_scan(struct device *dev, struct pci_id_info pci_tbl[]);
static int vortex_open(struct device *dev);
static void mdio_sync(long ioaddr, int bits);
static int mdio_read(long ioaddr, int phy_id, int location);
static void mdio_write(long ioaddr, int phy_id, int location, int value);
static void vortex_timer(unsigned long arg);
static void rx_oom_timer(unsigned long arg);
static int vortex_start_xmit(struct sk_buff *skb, struct device *dev);
static int boomerang_start_xmit(struct sk_buff *skb, struct device *dev);
static int vortex_rx(struct device *dev);
static int boomerang_rx(struct device *dev);
static void vortex_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void boomerang_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int vortex_close(struct device *dev);
static void update_stats(long ioaddr, struct device *dev);
static struct net_device_stats *vortex_get_stats(struct device *dev);
static void set_rx_mode(struct device *dev);
static int vortex_ioctl(struct device *dev, struct ifreq *rq, int cmd);
static void acpi_wake(int pci_bus, int pci_devfn);
static void acpi_set_WOL(struct device *dev);


#if 0
#warning dev_alloc_skb_debug is defined!
#define dev_alloc_skb dev_alloc_skb_debug
#endif

/* This driver uses 'options' to pass the media type, full-duplex flag, etc. */
/* Option count limit only -- unlimited interfaces are supported. */
#define MAX_UNITS 8
static int options[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int enable_wol[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};
/* A list of all installed Vortex devices, for removing the driver module. */
static struct device *root_vortex_dev = NULL;

#ifdef MODULE
/* Variables to work-around the Compaq PCI BIOS32 problem. */
static int compaq_ioaddr = 0, compaq_irq = 0, compaq_device_id = 0x5900;

int init_module(void)
{
	if (vortex_debug)
		printk(KERN_INFO "%s", version);
	return vortex_scan(0, pci_tbl);
}

#else
int tc59x_probe(struct device *dev)
{
	static int scanned=0;
	if(scanned++)
		return -ENODEV;
	printk(KERN_INFO "%s", version);
	return vortex_scan(dev, pci_tbl);
}
#endif  /* not MODULE */

static void wait_for_completion(struct device *dev, int cmd)
{
	int i;

	outw(cmd, dev->base_addr + EL3_CMD);
	for (i = 0; i < 2000; i++) {
		if (!(inw(dev->base_addr + EL3_STATUS) & CmdInProgress))
			return;
	}

	/* OK, that didn't work.  Do it the slow way.  One second */
	for (i = 0; i < 100000; i++) {
		if (!(inw(dev->base_addr + EL3_STATUS) & CmdInProgress)) {
			if (vortex_debug > 1)
				printk(KERN_INFO "%s: command 0x%04x took %d usecs!\n",
					   dev->name, cmd, i * 10);
			return;
		}
		udelay(10);
	}
	printk(KERN_ERR "%s: command 0x%04x did not complete! Status=0x%x\n",
			   dev->name, cmd, inw(dev->base_addr + EL3_STATUS));
}

static int vortex_scan(struct device *dev, struct pci_id_info pci_tbl[])
{
	int cards_found = 0;

	/* Allow an EISA-only driver. */
#if defined(CONFIG_PCI) || (defined(MODULE) && !defined(NO_PCI))
	/* Ideally we would detect all cards in slot order.  That would
	   be best done a central PCI probe dispatch, which wouldn't work
	   well with the current structure.  So instead we detect 3Com cards
	   in slot order. */
	if (pcibios_present()) {
		static int pci_index = 0;
		unsigned char pci_bus, pci_device_fn;

		for (;pci_index < 0xff; pci_index++) {
			u16 vendor, device, pci_command, new_command, pwr_cmd;
			int chip_idx, irq;
			long ioaddr;

			if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8, pci_index,
									&pci_bus, &pci_device_fn) != PCIBIOS_SUCCESSFUL)
				break;
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_VENDOR_ID, &vendor);
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_DEVICE_ID, &device);
			for (chip_idx = 0; pci_tbl[chip_idx].vendor_id; chip_idx++)
				if (vendor == pci_tbl[chip_idx].vendor_id
					&& (device & pci_tbl[chip_idx].device_id_mask) ==
					pci_tbl[chip_idx].device_id)
					break;
			if (pci_tbl[chip_idx].vendor_id == 0) 		/* Compiled out! */
				continue;

			/* The Cyclone requires config space re-write if powered down. */
			acpi_wake(pci_bus, pci_device_fn);

			{
				struct pci_dev *pdev = pci_find_slot(pci_bus, pci_device_fn);
				ioaddr = pdev->base_address[0] & ~3;
				irq = pdev->irq;
			}

			/* Power-up the card. */
			pcibios_read_config_word(pci_bus, pci_device_fn,
										 0xe0, &pwr_cmd);
			if (pwr_cmd & 0x3) {
				/* Save the ioaddr and IRQ info! */
				printk(KERN_INFO "  A 3Com network adapter is powered down!"
					   "  Setting the power state %4.4x->%4.4x.\n",
					   pwr_cmd, pwr_cmd & ~3);
				pcibios_write_config_word(pci_bus, pci_device_fn,
										  0xe0, pwr_cmd & ~3);
				printk(KERN_INFO "  Setting the IRQ to %d, IOADDR to %#lx.\n",
					   irq, ioaddr);
				pcibios_write_config_byte(pci_bus, pci_device_fn,
										 PCI_INTERRUPT_LINE, irq);
				pcibios_write_config_dword(pci_bus, pci_device_fn,
										  PCI_BASE_ADDRESS_0, ioaddr);
			}

			if (ioaddr == 0) {
				printk(KERN_WARNING "  A 3Com network adapter has been found, "
					   "however it has not been assigned an I/O address.\n"
					   "  You may need to power-cycle the machine for this "
					   "device to work!\n");
				continue;
			}

			if (check_region(ioaddr, pci_tbl[chip_idx].io_size))
				continue;

			/* Activate the card. */
			pcibios_read_config_word(pci_bus, pci_device_fn,
									 PCI_COMMAND, &pci_command);
			new_command = pci_command | PCI_COMMAND_MASTER|PCI_COMMAND_IO;
			if (pci_command != new_command) {
				printk(KERN_INFO "  The PCI BIOS has not enabled the device "
					   "at %d/%d. Updating PCI command %4.4x->%4.4x.\n",
					   pci_bus, pci_device_fn, pci_command, new_command);
				pcibios_write_config_word(pci_bus, pci_device_fn,
										  PCI_COMMAND, new_command);
			}

			dev = vortex_probe1(pci_bus, pci_device_fn, dev, ioaddr, irq,
								chip_idx, cards_found);

			if (dev) {
				if ((device & 0xff00) == 0x5900) {
					/* Get and check the latency values.  On the 3c590 series
					   the latency timer must be set to the maximum value to avoid
					   data corruption that occurs when the timer expires during
					   a transfer -- a bug in the Vortex chip only. */
					u8 pci_latency;
					u8 new_latency = 248;
					
					pcibios_read_config_byte(pci_bus, pci_device_fn, PCI_LATENCY_TIMER, &pci_latency);
					if (pci_latency < new_latency) {
						printk(KERN_INFO "%s: Overriding PCI latency"
							   " timer (CFLT) setting of %d, new value is %d.\n",
							   dev->name, pci_latency, new_latency);
						pcibios_write_config_byte(pci_bus, pci_device_fn, PCI_LATENCY_TIMER, new_latency);
					}
				}
				dev = 0;
				cards_found++;
			}
		}
	}
#endif /* NO_PCI */

	/* Now check all slots of the EISA bus. */
	if (EISA_bus) {
		static long ioaddr = 0x1000;
		for ( ; ioaddr < 0x9000; ioaddr += 0x1000) {
			int device_id;
			if (check_region(ioaddr, VORTEX_TOTAL_SIZE))
				continue;
			/* Check the standard EISA ID register for an encoded '3Com'. */
			if (inw(ioaddr + 0xC80) != 0x6d50)
				continue;
			/* Check for a product that we support, 3c59{2,7} any rev. */
			device_id = (inb(ioaddr + 0xC82)<<8) + inb(ioaddr + 0xC83);
			if ((device_id & 0xFF00) != 0x5900)
				continue;
			vortex_probe1(0, 0, dev, ioaddr, inw(ioaddr + 0xC88) >> 12,
						  EISA_TBL_OFFSET, cards_found);
			dev = 0;
			cards_found++;
		}
	}

#ifdef MODULE
	/* Special code to work-around the Compaq PCI BIOS32 problem. */
	if (compaq_ioaddr) {
		vortex_probe1(0, 0, dev, compaq_ioaddr, compaq_irq,
					  compaq_device_id, cards_found++);
		dev = 0;
	}
#endif

	return cards_found ? 0 : -ENODEV;
}

static struct device *vortex_probe1(int pci_bus, int pci_devfn,
									struct device *dev, long ioaddr,
									int irq, int chip_idx, int card_idx)
{
	struct vortex_private *vp;
	int option;
	unsigned int eeprom[0x40], checksum = 0;		/* EEPROM contents */
	int i;

	dev = init_etherdev(dev, 0);
	if (dev == NULL) {
		printk(KERN_EMERG "3c59x: init_etherdev failed\n");
		return NULL;
	}

	printk(KERN_INFO "%s: 3Com %s at 0x%lx, ",
		   dev->name, pci_tbl[chip_idx].name, ioaddr);

	dev->base_addr = ioaddr;
	dev->irq = irq;
	dev->mtu = mtu;

	/* Make certain the descriptor lists are aligned. */
	{
		void *mem = kmalloc(sizeof(*vp) + 15, GFP_KERNEL);
		if (!mem) {
			printk(KERN_EMERG "3c59x: out of memory for dev->priv\n");
			unregister_netdev(dev);
			kfree(dev);
			return NULL;
		}
		vp =  (void *)(((long)mem + 15) & ~15);
		memset(vp, 0, sizeof(*vp));
		vp->priv_addr = mem;
	}
	dev->priv = vp;

	vp->next_module = root_vortex_dev;
	root_vortex_dev = dev;

	spin_lock_init(&vp->lock);
	vp->chip_id = chip_idx;
	vp->pci_bus = pci_bus;
	vp->pci_devfn = pci_devfn;

	/* The lower four bits are the media type. */
	option = -1;
	if (dev->mem_start)
		option = dev->mem_start;
	else if (card_idx < MAX_UNITS)
		option = options[card_idx];

	if (option >= 0) {
		vp->media_override = ((option & 7) == 2)  ?  0  :  option & 15;
		if (vp->media_override != 7)
			vp->medialock = 1;
		if (option & 0x200) {
			vp->full_duplex = 1;
			vp->duplexlock = 1;
			printk(	"\n" KERN_WARNING
					"%s: forcing duplex via options is deprecated - use `full_duplex'.\n",
					dev->name);
		}
		vp->bus_master = (option & 16) ? 1 : 0;
	} else {
		vp->media_override = 7;
		vp->full_duplex = 0;
		vp->bus_master = 0;
	}
	if (card_idx < MAX_UNITS) {
		if (enable_wol[card_idx] > 0)
			vp->enable_wol = 1;
		if (full_duplex[card_idx] == 0) {	/* full_duplex=0 : force half duplex */
			vp->duplexlock = 1;
		}
		if (full_duplex[card_idx] > 0) {	/* full_duplex=1: force full duplex */
			vp->duplexlock = 1;
			vp->full_duplex = 1;
		}
		/* full_duplex=-1: duplex is not forced */
	}
	vp->options = option;

	/* Read the station address from the EEPROM. */
	EL3WINDOW(0);
	{
		int base;

		if (pci_tbl[chip_idx].drv_flags & EEPROM_8BIT)
			base = 0x230;
		else if (pci_tbl[chip_idx].drv_flags & EEPROM_OFFSET)
			base = EEPROM_Read + 0x30;
		else
			base = EEPROM_Read;

		for (i = 0; i < 0x40; i++) {
			int timer;
			outw(base + i, ioaddr + Wn0EepromCmd);
			/* Pause for at least 162 us. for the read to take place. */
			for (timer = 10; timer >= 0; timer--) {
				udelay(162);
				if ((inw(ioaddr + Wn0EepromCmd) & 0x8000) == 0)
					break;
			}
			eeprom[i] = inw(ioaddr + Wn0EepromData);
		}
	}

	for (i = 0; i < 0x18; i++)
		checksum ^= eeprom[i];
	checksum = (checksum ^ (checksum >> 8)) & 0xff;
	if (checksum != 0x00) {		/* Grrr, needless incompatible change 3Com. */
		while (i < 0x21)
			checksum ^= eeprom[i++];
		checksum = (checksum ^ (checksum >> 8)) & 0xff;
	}
	if (checksum != 0x00 && !(pci_tbl[chip_idx].drv_flags & IS_TORNADO))
		printk(" ***INVALID CHECKSUM %4.4x*** ", checksum);

	for (i = 0; i < 3; i++)
		((u16 *)dev->dev_addr)[i] = htons(eeprom[i + 10]);
	for (i = 0; i < 6; i++)
		printk("%c%2.2x", i ? ':' : ' ', dev->dev_addr[i]);
#ifdef __sparc__
	printk(", IRQ %s\n", __irq_itoa(dev->irq));
#else
	printk(", IRQ %d\n", dev->irq);
	/* Tell them about an invalid IRQ. */
	if (vortex_debug && (dev->irq <= 0 || dev->irq >= NR_IRQS))
		printk(KERN_WARNING " *** Warning: IRQ %d is unlikely to work! ***\n",
			   dev->irq);
#endif

	if (pci_tbl[vp->chip_id].drv_flags & HAS_CB_FNS) {
		u32 fn_st_addr;			/* Cardbus function status space */
		u16 n;
		pcibios_read_config_dword(pci_bus, pci_devfn, PCI_BASE_ADDRESS_2,
								  &fn_st_addr);
		if (fn_st_addr)
			vp->cb_fn_base = ioremap(fn_st_addr & ~3, 128);
		printk("%s: CardBus functions mapped %8.8x->%p (PCMCIA committee"
			   " brain-damage).\n", dev->name, fn_st_addr, vp->cb_fn_base);
		EL3WINDOW(2);

		n = inw(ioaddr + Wn2_ResetOptions) & ~0x4010;
		n |= 0x10;
		if (pci_tbl[chip_idx].drv_flags & INVERT_MII_PWR)
			n |= 0x4000;
		outw(n, ioaddr + Wn2_ResetOptions);
	}

	/* Extract our information from the EEPROM data. */
	vp->info1 = eeprom[13];
	vp->info2 = eeprom[15];
	vp->capabilities = eeprom[16];

	if (!vp->duplexlock && (vp->info1 & 0x8000))
		vp->full_duplex = 1;

	{
		char *ram_split[] = {"5:3", "3:1", "1:1", "3:5"};
		unsigned int config;
		EL3WINDOW(3);
		vp->available_media = inw(ioaddr + Wn3_Options);
		if ((vp->available_media & 0xff) == 0)		/* Broken 3c916 */
			vp->available_media = 0x40;
		config = inl(ioaddr + Wn3_Config);
		if (vortex_debug > 1)
			printk(KERN_DEBUG "  Internal config register is %4.4x, "
				   "transceivers %#x.\n", config, inw(ioaddr + Wn3_Options));
		printk(KERN_INFO "  %dK %s-wide RAM %s Rx:Tx split, %s%s interface.\n",
			   8 << RAM_SIZE(config),
			   RAM_WIDTH(config) ? "word" : "byte",
			   ram_split[RAM_SPLIT(config)],
			   AUTOSELECT(config) ? "autoselect/" : "",
			   XCVR(config) > XCVR_ExtMII ? "<invalid transceiver>" :
			   media_tbl[XCVR(config)].name);
		vp->default_media = XCVR(config);
		vp->autoselect = AUTOSELECT(config);
	}

	if (vp->media_override != 7) {
		printk(KERN_INFO "  Media override to transceiver type %d (%s).\n",
			   vp->media_override, media_tbl[vp->media_override].name);
		dev->if_port = vp->media_override;
	} else
		dev->if_port = vp->default_media;

	if (dev->if_port == XCVR_MII || dev->if_port == XCVR_NWAY) {
		int phy, phy_idx = 0;
		EL3WINDOW(4);
		mii_preamble_required++;
		mii_preamble_required++;
		mdio_read(ioaddr, 24, 1);
		for (phy = 0; phy < 32 && phy_idx < 1; phy++) {
			int mii_status, phyx;

			/*
			 * For the 3c905CX we look at index 24 first, because it bogusly
			 * reports an external PHY at all indices
			 */
			if (phy == 0)
				phyx = 24;
			else if (phy <= 24)
				phyx = phy - 1;
			else
				phyx = phy;
			mii_status = mdio_read(ioaddr, phyx, 1);
			if (mii_status  &&  mii_status != 0xffff) {
				vp->phys[phy_idx++] = phyx;
				printk(KERN_INFO "  MII transceiver found at address %d,"
					   " status %4x.\n", phyx, mii_status);
				if ((mii_status & 0x0040) == 0)
					mii_preamble_required++;
			}
		}
		mii_preamble_required--;
		if (phy_idx == 0) {
			printk(KERN_WARNING"  ***WARNING*** No MII transceivers found!\n");
			vp->phys[0] = 24;
		} else {
			vp->advertising = mdio_read(ioaddr, vp->phys[0], 4);
			if (vp->full_duplex) {
				/* Only advertise the FD media types. */
				vp->advertising &= ~0x02A0;
				mdio_write(ioaddr, vp->phys[0], 4, vp->advertising);
			}
		}
	}

	if (vp->enable_wol && (vp->capabilities & CapPwrMgmt))
		acpi_set_WOL(dev);

	if (vp->capabilities & CapBusMaster) {
		vp->full_bus_master_tx = 1;
		printk(KERN_INFO"  Enabling bus-master transmits and %s receives.\n",
			   (vp->info2 & 1) ? "early" : "whole-frame" );
		vp->full_bus_master_rx = (vp->info2 & 1) ? 1 : 2;
	}

	/* We do a request_region() to register /proc/ioports info. */
	request_region(ioaddr, pci_tbl[chip_idx].io_size, dev->name);

	/* The 3c59x-specific entries in the device structure. */
	dev->open = vortex_open;
	dev->hard_start_xmit = vp->full_bus_master_tx ?
				boomerang_start_xmit : vortex_start_xmit;
	dev->stop = vortex_close;
	dev->get_stats = vortex_get_stats;
	dev->do_ioctl = vortex_ioctl;
	dev->set_multicast_list = set_rx_mode;

	return dev;
}


static int
vortex_open(struct device *dev)
{
	long ioaddr = dev->base_addr;
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	unsigned int config;
	int i, retval;

	MOD_INC_USE_COUNT;

	if (vp->enable_wol)
		acpi_wake(vp->pci_bus, vp->pci_devfn);

	/* Before initializing select the active media port. */
	EL3WINDOW(3);
	config = inl(ioaddr + Wn3_Config);

	if (vp->media_override != 7) {
		if (vortex_debug > 1)
			printk(KERN_INFO "%s: Media override to transceiver %d (%s).\n",
				   dev->name, vp->media_override,
				   media_tbl[vp->media_override].name);
		dev->if_port = vp->media_override;
	} else if (vp->autoselect && pci_tbl[vp->chip_id].drv_flags & HAS_NWAY) {
		if (vortex_debug > 1)
			printk(KERN_INFO "%s: using NWAY from config\n", dev->name);
		dev->if_port = XCVR_NWAY;
	} else if (vp->autoselect) {
		/* Find first available media type, starting with 100baseTx. */
		dev->if_port = XCVR_100baseTx;
		while (! (vp->available_media & media_tbl[dev->if_port].mask))
			dev->if_port = media_tbl[dev->if_port].next;
	} else
		dev->if_port = vp->default_media;

	printk(KERN_INFO "%s: Initial media type %s.\n", dev->name, media_tbl[dev->if_port].name);

	if (!vp->duplexlock && (vp->info1 & 0x8000))
		vp->full_duplex = 1;

	config = BFINS(config, dev->if_port, 20, 4);
	outl(config, ioaddr + Wn3_Config);

	if (dev->if_port == XCVR_MII || dev->if_port == XCVR_NWAY) {
		int mii_reg1, mii_reg5;
		EL3WINDOW(4);
		/* Read BMSR (reg1) only to clear old status. */
		mii_reg1 = mdio_read(ioaddr, vp->phys[0], 1);
		mii_reg5 = mdio_read(ioaddr, vp->phys[0], 5);
		if (mii_reg5 == 0xffff  ||  mii_reg5 == 0x0000)
			;					/* No MII device or no link partner report */
		else if (!vp->duplexlock &&
				((mii_reg5 & 0x0100) != 0			/* 100baseTx-FD */
				|| (mii_reg5 & 0x00C0) == 0x0040))		/* 10T-FD, but not 100-HD */
			vp->full_duplex = 1;
		printk(KERN_INFO "%s: MII #%d status %4.4x, link partner capability %4.4x,"
			   " setting %s-duplex.\n", dev->name, vp->phys[0],
			   mii_reg1, mii_reg5, vp->full_duplex ? "full" : "half");
		EL3WINDOW(3);
	}

	/* Set the full-duplex bit. */
	outb((vp->full_duplex ? 0x20 : 0) | (dev->mtu > 1500 ? 0x40 : 0), ioaddr + Wn3_MAC_Ctrl);

	if (vortex_debug > 1) {
		printk(KERN_DEBUG "%s: vortex_open() InternalConfig %8.8x.\n", dev->name, config);
	}

	wait_for_completion(dev, TxReset);
	wait_for_completion(dev, RxReset);

	outw(SetStatusEnb | 0x00, ioaddr + EL3_CMD);

	/* Use the now-standard shared IRQ implementation. */
	if ((retval = request_irq(dev->irq, vp->full_bus_master_tx ?
					&boomerang_interrupt : &vortex_interrupt,
					SA_SHIRQ, dev->name, dev))) {
		printk(KERN_ERR "%s: Cannot allocate IRQ #%d\n", dev->name, dev->irq);
		goto out;
	}

	if (vortex_debug > 1) {
		EL3WINDOW(4);
		printk(KERN_DEBUG "%s: vortex_open() irq %d media status %4.4x.\n",
			   dev->name, dev->irq, inw(ioaddr + Wn4_Media));
	}

	/* Set the station address and mask in window 2 each time opened. */
	EL3WINDOW(2);
	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], ioaddr + i);
	for (; i < 12; i+=2)
		outw(0, ioaddr + i);

	if (dev->if_port == XCVR_10base2)
		/* Start the thinnet transceiver. We should really wait 50ms...*/
		outw(StartCoax, ioaddr + EL3_CMD);
	if (dev->if_port != XCVR_NWAY) {
		EL3WINDOW(4);
		outw((inw(ioaddr + Wn4_Media) & ~(Media_10TP|Media_SQE)) |
			 media_tbl[dev->if_port].media_bits, ioaddr + Wn4_Media);
	}

	/* Switch to the stats window, and clear all stats by reading. */
	outw(StatsDisable, ioaddr + EL3_CMD);
	EL3WINDOW(6);
	for (i = 0; i < 10; i++)
		inb(ioaddr + i);
	inw(ioaddr + 10);
	inw(ioaddr + 12);
	/* New: On the Vortex we must also clear the BadSSD counter. */
	EL3WINDOW(4);
	inb(ioaddr + 12);
	/* ..and on the Boomerang we enable the extra statistics bits. */
	outw(0x0040, ioaddr + Wn4_NetDiag);

	/* Switch to register set 7 for normal use. */
	EL3WINDOW(7);

	if (vp->full_bus_master_rx) { /* Boomerang bus master. */
		vp->cur_rx = vp->dirty_rx = 0;
		/* Initialize the RxEarly register as recommended. */
		outw(SetRxThreshold + (1536>>2), ioaddr + EL3_CMD);
		outl(0x0020, ioaddr + PktStatus);
		if (vortex_debug > 2)
			printk(KERN_DEBUG "%s:  Filling in the Rx ring.\n", dev->name);
		for (i = 0; i < RX_RING_SIZE; i++) {
			struct sk_buff *skb;
			vp->rx_ring[i].next = cpu_to_le32(virt_to_bus(&vp->rx_ring[i+1]));
			vp->rx_ring[i].status = 0;	/* Clear complete bit. */
			vp->rx_ring[i].length = cpu_to_le32(PKT_BUF_SZ | LAST_FRAG);
			skb = dev_alloc_skb(PKT_BUF_SZ);
			vp->rx_skbuff[i] = skb;
			if (skb == NULL)
				break;			/* Bad news!  */
			skb->dev = dev;			/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			vp->rx_ring[i].addr = cpu_to_le32(virt_to_bus(skb->tail));
		}
		if (i != RX_RING_SIZE) {
			int j;
			printk(KERN_EMERG "%s: no memory for rx ring\n", dev->name);
			for (j = 0; j < i; j++) {
				if (vp->rx_skbuff[j])
					dev_kfree_skb(vp->rx_skbuff[j]);
			}
			retval = -ENOMEM;
			goto out_free_irq;
		}
		/* Wrap the ring. */
		vp->rx_ring[i-1].next = cpu_to_le32(virt_to_bus(&vp->rx_ring[0]));
		outl(virt_to_bus(&vp->rx_ring[0]), ioaddr + UpListPtr);
	}
	if (vp->full_bus_master_tx) { 		/* Boomerang bus master Tx. */
		vp->cur_tx = vp->dirty_tx = 0;
		outb(PKT_BUF_SZ>>8, ioaddr + TxFreeThreshold); /* Room for a packet. */
		/* Clear the Tx ring. */
		for (i = 0; i < TX_RING_SIZE; i++)
			vp->tx_skbuff[i] = 0;
		outl(0, ioaddr + DownListPtr);
	}
	/* Set receiver mode: presumably accept b-case and phys addr only. */
	set_rx_mode(dev);
	outw(StatsEnable, ioaddr + EL3_CMD); /* Turn on statistics. */

	dev->tbusy = 0;
	dev->start = 1;
	vp->tx_full = 0;

	outw(RxEnable, ioaddr + EL3_CMD); /* Enable the receiver. */
	outw(TxEnable, ioaddr + EL3_CMD); /* Enable transmitter. */
	/* Allow status bits to be seen. */
	vp->status_enable = SetStatusEnb | HostError|IntReq|StatsFull|TxComplete|
		(vp->full_bus_master_tx ? DownComplete : TxAvailable) |
		(vp->full_bus_master_rx ? UpComplete : RxComplete) |
		(vp->bus_master ? DMADone : 0);
	vp->intr_enable = SetIntrEnb | IntLatch | TxAvailable |
		(vp->full_bus_master_rx ? 0 : RxComplete) |
		StatsFull | HostError | TxComplete | IntReq
		| (vp->bus_master ? DMADone : 0) | UpComplete | DownComplete;
	outw(vp->status_enable, ioaddr + EL3_CMD);
	/* Ack all pending events, and set active indicator mask. */
	outw(AckIntr | IntLatch | TxAvailable | RxEarly | IntReq,
		 ioaddr + EL3_CMD);
	outw(vp->intr_enable, ioaddr + EL3_CMD);
	if (vp->cb_fn_base)			/* The PCMCIA people are idiots.  */
		writel(0x8000, vp->cb_fn_base + 4);

	init_timer(&vp->timer);
	vp->timer.expires = RUN_AT(media_tbl[dev->if_port].wait);
	vp->timer.data = (unsigned long)dev;
	vp->timer.function = &vortex_timer;		/* timer handler */
	add_timer(&vp->timer);

	init_timer(&vp->rx_oom_timer);
	vp->rx_oom_timer.data = (unsigned long)dev;
	vp->rx_oom_timer.function = rx_oom_timer;

	return 0;

out_free_irq:
	free_irq(dev->irq, dev);
out:
	return retval;
}

static void vortex_timer(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 60 * HZ;
	int ok = 0;
	int media_status, mii_status, old_window;

	if (vortex_debug > 1)
		printk(KERN_DEBUG "%s: Media selection timer tick happened, %s.\n",
			   dev->name, media_tbl[dev->if_port].name);

	if (vp->medialock)
		goto leave_media_alone;
	disable_irq(dev->irq);
	old_window = inw(ioaddr + EL3_CMD) >> 13;
	EL3WINDOW(4);
	media_status = inw(ioaddr + Wn4_Media);
	switch (dev->if_port) {
	  case XCVR_10baseT:  case XCVR_100baseTx:  case XCVR_100baseFx:
		if (media_status & Media_LnkBeat) {
		  ok = 1;
		  if (vortex_debug > 1)
			printk(KERN_DEBUG "%s: Media %s has link beat, %x.\n",
				   dev->name, media_tbl[dev->if_port].name, media_status);
		} else if (vortex_debug > 1)
		  printk(KERN_DEBUG "%s: Media %s is has no link beat, %x.\n",
				   dev->name, media_tbl[dev->if_port].name, media_status);
		break;
	  case XCVR_MII: case XCVR_NWAY:
		{
		  unsigned long flags;
		  spin_lock_irqsave(&vp->lock, flags);
		  mii_status = mdio_read(ioaddr, vp->phys[0], 1);
		  ok = 1;
		  if (debug > 1)
			  printk(KERN_DEBUG "%s: MII transceiver has status %4.4x.\n",
					 dev->name, mii_status);
		  if (mii_status & 0x0004) {
			  int mii_reg5 = mdio_read(ioaddr, vp->phys[0], 5);
			  if (!vp->duplexlock && mii_reg5 != 0xffff) {
				  int duplex = (mii_reg5&0x0100) ||
					  (mii_reg5 & 0x01C0) == 0x0040;
				  if (vp->full_duplex != duplex) {
					  vp->full_duplex = duplex;
					  printk(KERN_INFO "%s: Setting %s-duplex based on MII "
							 "#%d link partner capability of %4.4x.\n",
							 dev->name, vp->full_duplex ? "full" : "half",
							 vp->phys[0], mii_reg5);
					  /* Set the full-duplex bit. */
					  EL3WINDOW(3);
					  outb((vp->full_duplex ? 0x20 : 0) |
						   (dev->mtu > 1500 ? 0x40 : 0),
						   ioaddr + Wn3_MAC_Ctrl);
				  }
				  next_tick = 60*HZ;
			  }
		  }
		  spin_unlock_irqrestore(&vp->lock, flags);
		}
		break;
	  default:					/* Other media types handled by Tx timeouts. */
		if (vortex_debug > 1)
		  printk(KERN_DEBUG "%s: Media %s has no indication, %x.\n",
				 dev->name, media_tbl[dev->if_port].name, media_status);
		ok = 1;
	}
	if ( ! ok) {
		unsigned int config;

		do {
			dev->if_port = media_tbl[dev->if_port].next;
		} while ( ! (vp->available_media & media_tbl[dev->if_port].mask));
		if (dev->if_port == XCVR_Default) { /* Go back to default. */
		  dev->if_port = vp->default_media;
		  if (vortex_debug > 1)
			printk(KERN_DEBUG "%s: Media selection failing, using default "
				   "%s port.\n",
				   dev->name, media_tbl[dev->if_port].name);
		} else {
		  if (vortex_debug > 1)
			printk(KERN_DEBUG "%s: Media selection failed, now trying "
				   "%s port.\n",
				   dev->name, media_tbl[dev->if_port].name);
		  next_tick = media_tbl[dev->if_port].wait;
		}
		outw((media_status & ~(Media_10TP|Media_SQE)) |
			 media_tbl[dev->if_port].media_bits, ioaddr + Wn4_Media);

		EL3WINDOW(3);
		config = inl(ioaddr + Wn3_Config);
		config = BFINS(config, dev->if_port, 20, 4);
		outl(config, ioaddr + Wn3_Config);

		outw(dev->if_port == XCVR_10base2 ? StartCoax : StopCoax,
			 ioaddr + EL3_CMD);
	}
	EL3WINDOW(old_window);
	enable_irq(dev->irq);

leave_media_alone:
	if (vortex_debug > 2)
	  printk(KERN_DEBUG "%s: Media selection timer finished, %s.\n",
			 dev->name, media_tbl[dev->if_port].name);

	vp->timer.expires = RUN_AT(next_tick);
	add_timer(&vp->timer);
	if (vp->deferred)
		outw(FakeIntr, ioaddr + EL3_CMD);
	return;
}

static void vortex_tx_timeout(struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;

	printk(KERN_ERR "%s: transmit timed out, tx_status %2.2x status %4.4x.\n",
		   dev->name, inb(ioaddr + TxStatus),
		   inw(ioaddr + EL3_STATUS));
	/* Slight code bloat to be user friendly. */
	if ((inb(ioaddr + TxStatus) & 0x88) == 0x88)
		printk(KERN_ERR "%s: Transmitter encountered 16 collisions --"
			   " network cable problem?\n", dev->name);
	if (inw(ioaddr + EL3_STATUS) & IntLatch) {
		printk(KERN_ERR "%s: Interrupt posted but not delivered --"
			   " IRQ blocked by another device?\n", dev->name);
		/* Bad idea here.. but we might as well handle a few events. */
		{
			/*
			 * AKPM: block interrupts because vortex_interrupt
			 * does a bare spin_lock()
			 */
			unsigned long flags;
			__save_flags(flags);
			__cli();
			if (vp->full_bus_master_tx)
				boomerang_interrupt(dev->irq, dev, 0);
			else
				vortex_interrupt(dev->irq, dev, 0);
			__restore_flags(flags);
		}
	}

	if (vp->full_bus_master_tx) {
		int i;
		printk(KERN_DEBUG "  Flags; bus-master %d, full %d; dirty %d "
			   "current %d.\n",
			   vp->full_bus_master_tx, vp->tx_full, vp->dirty_tx, vp->cur_tx);
		printk(KERN_DEBUG "  Transmit list %8.8x vs. %p.\n",
			   inl(ioaddr + DownListPtr),
			   &vp->tx_ring[vp->dirty_tx % TX_RING_SIZE]);
		for (i = 0; i < TX_RING_SIZE; i++) {
			printk(KERN_DEBUG "  %d: @%p  length %8.8x status %8.8x\n", i,
				   &vp->tx_ring[i],
				   le32_to_cpu(vp->tx_ring[i].length),
				   le32_to_cpu(vp->tx_ring[i].status));
		}
	}
	wait_for_completion(dev, TxReset);
	vp->stats.tx_errors++;
	if (vp->full_bus_master_tx) {
		if (vortex_debug > 0)
			printk(KERN_DEBUG "%s: Resetting the Tx ring pointer.\n",
				   dev->name);
		if (vp->cur_tx - vp->dirty_tx > 0  &&  inl(ioaddr + DownListPtr) == 0)
			outl(virt_to_bus(&vp->tx_ring[vp->dirty_tx % TX_RING_SIZE]),
				 ioaddr + DownListPtr);
		if (vp->tx_full && (vp->cur_tx - vp->dirty_tx <= TX_RING_SIZE - 1)) {
			vp->tx_full = 0;
			clear_bit(0, (void*)&dev->tbusy);
		}
		outb(PKT_BUF_SZ>>8, ioaddr + TxFreeThreshold);
		outw(DownUnstall, ioaddr + EL3_CMD);
	} else {
		vp->stats.tx_dropped++;
		clear_bit(0, (void*)&dev->tbusy);
	}

	/* Issue Tx Enable */
	outw(TxEnable, ioaddr + EL3_CMD);
	dev->trans_start = jiffies;

	/* Switch to register set 7 for normal use. */
	EL3WINDOW(7);
}

/*
 * Handle uncommon interrupt sources.  This is a separate routine to minimize
 * the cache impact.
 */
static void
vortex_error(struct device *dev, int status)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int do_tx_reset = 0;

	if (status & TxComplete) {			/* Really "TxError" for us. */
		unsigned char tx_status = inb(ioaddr + TxStatus);
		/* Presumably a tx-timeout. We must merely re-enable. */
		if (vortex_debug > 2
			|| (tx_status != 0x88 && vortex_debug > 0))
			printk(KERN_DEBUG"%s: Transmit error, Tx status register %2.2x.\n",
				   dev->name, tx_status);
		if (tx_status & 0x14)  vp->stats.tx_fifo_errors++;
		if (tx_status & 0x38)  vp->stats.tx_aborted_errors++;
		outb(0, ioaddr + TxStatus);
		if (tx_status & 0x30)	/* txJabber or txUnderrun */
			do_tx_reset = 1;
		else					/* Merely re-enable the transmitter. */
			outw(TxEnable, ioaddr + EL3_CMD);
	}
	if (status & RxEarly) {				/* Rx early is unused. */
		vortex_rx(dev);
		outw(AckIntr | RxEarly, ioaddr + EL3_CMD);
	}
	if (status & StatsFull) {			/* Empty statistics. */
		static int DoneDidThat = 0;
		if (vortex_debug > 4)
			printk(KERN_DEBUG "%s: Updating stats.\n", dev->name);
		update_stats(ioaddr, dev);
		/* HACK: Disable statistics as an interrupt source. */
		/* This occurs when we have the wrong media type! */
		if (DoneDidThat == 0  &&
			inw(ioaddr + EL3_STATUS) & StatsFull) {
			printk(KERN_WARNING "%s: Updating statistics failed, disabling "
				   "stats as an interrupt source.\n", dev->name);
			EL3WINDOW(5);
			outw(SetIntrEnb | (inw(ioaddr + 10) & ~StatsFull), ioaddr + EL3_CMD);
			EL3WINDOW(7);
			DoneDidThat++;
		}
	}
	if (status & IntReq) {		/* Restore all interrupt sources.  */
		outw(vp->status_enable, ioaddr + EL3_CMD);
		outw(vp->intr_enable, ioaddr + EL3_CMD);
	}
	if (status & HostError) {
		u16 fifo_diag;
		EL3WINDOW(4);
		fifo_diag = inw(ioaddr + Wn4_FIFODiag);
		if (vortex_debug > 0)
			printk(KERN_ERR "%s: Host error, FIFO diagnostic register %4.4x.\n",
				   dev->name, fifo_diag);
		/* Adapter failure requires Tx/Rx reset and reinit. */
		if (vp->full_bus_master_tx) {
			wait_for_completion(dev, TotalReset|0xff);
			/* Re-enable the receiver. */
			outw(RxEnable, ioaddr + EL3_CMD);
			outw(TxEnable, ioaddr + EL3_CMD);
		} else if (fifo_diag & 0x0400)
			do_tx_reset = 1;
		if (fifo_diag & 0x3000) {
			wait_for_completion(dev, RxReset);
			/* Set the Rx filter to the current state. */
			set_rx_mode(dev);
			outw(RxEnable, ioaddr + EL3_CMD); /* Re-enable the receiver. */
			outw(AckIntr | HostError, ioaddr + EL3_CMD);
		}
	}
	if (do_tx_reset) {
		wait_for_completion(dev, TxReset);
		outw(TxEnable, ioaddr + EL3_CMD);
		if (!vp->full_bus_master_tx) {
			clear_bit(0, (void*)&dev->tbusy);
			mark_bh(NET_BH);
		}
	}

}

static int
vortex_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		if (jiffies - dev->trans_start >= TX_TIMEOUT)
			vortex_tx_timeout(dev);
		return 1;
	}

	/* Put out the doubleword header... */
	outl(skb->len, ioaddr + TX_FIFO);
	if (vp->bus_master) {
		/* Set the bus-master controller to transfer the packet. */
		outl(virt_to_bus(skb->data), ioaddr + Wn7_MasterAddr);
		outw((skb->len + 3) & ~3, ioaddr + Wn7_MasterLen);
		vp->tx_skb = skb;
		outw(StartDMADown, ioaddr + EL3_CMD);
		/* dev->tbusy will be cleared at the DMADone interrupt. */
	} else {
		/* ... and the packet rounded to a doubleword. */
#ifdef __powerpc__
		outsl_ns(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);
#else
		outsl(ioaddr + TX_FIFO, skb->data, (skb->len + 3) >> 2);
#endif
		dev_kfree_skb(skb);
		if (inw(ioaddr + TxFree) > 1536) {
			clear_bit(0, (void*)&dev->tbusy);
		} else
			/* Interrupt us when the FIFO has room for max-sized packet. */
			outw(SetTxThreshold + (1536>>2), ioaddr + EL3_CMD);
	}

	dev->trans_start = jiffies;

	/* Clear the Tx status stack. */
	{
		int tx_status;
		int i = 32;

		while (--i > 0	&&	(tx_status = inb(ioaddr + TxStatus)) > 0) {
			if (tx_status & 0x3C) {		/* A Tx-disabling error occurred.  */
				if (vortex_debug > 2)
				  printk(KERN_DEBUG "%s: Tx error, status %2.2x.\n",
						 dev->name, tx_status);
				if (tx_status & 0x04) vp->stats.tx_fifo_errors++;
				if (tx_status & 0x38) vp->stats.tx_aborted_errors++;
				if (tx_status & 0x30)
					wait_for_completion(dev, TxReset);
				outw(TxEnable, ioaddr + EL3_CMD);
			}
			outb(0x00, ioaddr + TxStatus); /* Pop the status stack. */
		}
	}
	vp->stats.tx_bytes += skb->len;
	return 0;
}

static int
boomerang_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		if (jiffies - dev->trans_start >= TX_TIMEOUT)
			vortex_tx_timeout(dev);
		return 1;
	} else {
		/* Calculate the next Tx descriptor entry. */
		int entry = vp->cur_tx % TX_RING_SIZE;
		struct boom_tx_desc *prev_entry =
			&vp->tx_ring[(vp->cur_tx-1) % TX_RING_SIZE];
		unsigned long flags;

		if (vortex_debug > 3)
			printk(KERN_DEBUG "%s: Trying to send a boomerang packet, Tx index %d.\n",
				   dev->name, vp->cur_tx);
		if (vp->tx_full) {
			if (vortex_debug > 0)
				printk(KERN_WARNING "%s: Tx Ring full, refusing to send buffer.\n",
					   dev->name);
			return 1;
		}
		vp->tx_skbuff[entry] = skb;
		vp->tx_ring[entry].next = 0;
		vp->tx_ring[entry].addr = cpu_to_le32(virt_to_bus(skb->data));
		vp->tx_ring[entry].length = cpu_to_le32(skb->len | LAST_FRAG);
		vp->tx_ring[entry].status = cpu_to_le32(skb->len | TxIntrUploaded);

		spin_lock_irqsave(&vp->lock, flags);
		wait_for_completion(dev, DownStall);
		prev_entry->next = cpu_to_le32(virt_to_bus(&vp->tx_ring[entry]));
		if (inl(ioaddr + DownListPtr) == 0) {
			outl(virt_to_bus(&vp->tx_ring[entry]), ioaddr + DownListPtr);
			queued_packet++;
		}

		vp->cur_tx++;
		if (vp->cur_tx - vp->dirty_tx > TX_RING_SIZE - 1)
			vp->tx_full = 1;
		else {		/* Clear previous interrupt enable. */
			prev_entry->status &= cpu_to_le32(~TxIntrUploaded);
			clear_bit(0, (void*)&dev->tbusy);
		}
		outw(DownUnstall, ioaddr + EL3_CMD);
		spin_unlock_irqrestore(&vp->lock, flags);
		dev->trans_start = jiffies;
		vp->stats.tx_bytes += skb->len;
		return 0;
	}
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */

/*
 * This is the ISR for the vortex series chips.
 * full_bus_master_tx == 0 && full_bus_master_rx == 0
 */

static void vortex_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = dev_id;
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr;
	int status;
	int work_done = max_interrupt_work;

	ioaddr = dev->base_addr;
	spin_lock(&vp->lock);
	status = inw(ioaddr + EL3_STATUS);
	if ((status & IntLatch) == 0) {
		if (vortex_debug > 5)
			printk(KERN_DEBUG "%s: no vortex interrupt pending\n", dev->name);
		goto no_int;	/* Happens during shared interrupts */
	}

	if (status & IntReq) {
		status |= vp->deferred;
		vp->deferred = 0;
	}

	if (vortex_debug > 4)
		printk(KERN_DEBUG "%s: vortex_interrupt, status %4.4x, latency %d ticks.\n",
			   dev->name, status, inb(ioaddr + Timer));

	do {
		if (vortex_debug > 5)
				printk(KERN_DEBUG "%s: In interrupt loop, status %4.4x.\n",
					   dev->name, status);

		if (status & RxComplete)
			vortex_rx(dev);

		if (status & TxAvailable) {
			if (vortex_debug > 5)
				printk(KERN_DEBUG "	TX room bit was handled.\n");
			/* There's room in the FIFO for a full-sized packet. */
			outw(AckIntr | TxAvailable, ioaddr + EL3_CMD);
			clear_bit(0, (void*)&dev->tbusy);
			mark_bh(NET_BH);
		}

		if (status & DMADone) {
			if (inw(ioaddr + Wn7_MasterStatus) & 0x1000) {
				outw(0x1000, ioaddr + Wn7_MasterStatus); /* Ack the event. */
				dev_kfree_skb(vp->tx_skb); /* Release the transfered buffer */
				if (inw(ioaddr + TxFree) > 1536) {
					clear_bit(0, (void*)&dev->tbusy);
					mark_bh(NET_BH);
				} else /* Interrupt when FIFO has room for max-sized packet. */
					outw(SetTxThreshold + (1536>>2), ioaddr + EL3_CMD);
			}
		}

		/* Check for all uncommon interrupts at once. */
		if (status & (HostError | RxEarly | StatsFull | TxComplete | IntReq)) {
			if (status == 0xffff)
				break;
			vortex_error(dev, status);
		}

		if (--work_done < 0) {
			printk(KERN_WARNING "%s: Too much work in interrupt, status "
				   "%4.4x.\n", dev->name, status);
			/* Disable all pending interrupts. */
			do {
				vp->deferred |= status;
				outw(SetStatusEnb | (~vp->deferred & vp->status_enable),
					 ioaddr + EL3_CMD);
				outw(AckIntr | (vp->deferred & 0x7ff), ioaddr + EL3_CMD);
			} while ((status = inw(ioaddr + EL3_CMD)) & IntLatch);
			/* The timer will reenable interrupts. */
			mod_timer(&vp->timer, RUN_AT(1));
			break;
		}

		/* Acknowledge the IRQ. */
		outw(AckIntr | IntReq | IntLatch, ioaddr + EL3_CMD);
	} while ((status = inw(ioaddr + EL3_STATUS)) & (IntLatch | RxComplete));

	if (vortex_debug > 4)
		printk(KERN_DEBUG "%s: exiting interrupt, status %4.4x.\n",
			   dev->name, status);

no_int:
	spin_unlock(&vp->lock);
}

/*
 * This is the ISR for the boomerang/cyclone/hurricane/tornado series chips.
 * full_bus_master_tx == 1 && full_bus_master_rx == 1
 */

static void boomerang_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = dev_id;
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr;
	int status;
	int work_done = max_interrupt_work;

	ioaddr = dev->base_addr;
	spin_lock(&vp->lock);
	status = inw(ioaddr + EL3_STATUS);
	if ((status & IntLatch) == 0) {
		if (vortex_debug > 5)
			printk(KERN_DEBUG "%s: no boomerang interrupt pending\n", dev->name);
		goto no_int;	/* Happens during shared interrupts */
	}

	if (status & IntReq) {
		status |= vp->deferred;
		vp->deferred = 0;
	}

	if (vortex_debug > 4)
		printk(KERN_DEBUG "%s: interrupt, status %04x, latency %d, cur_rx %d, dirty_rx %d\n",
			   dev->name, status, inb(ioaddr + Timer), vp->cur_rx, vp->dirty_rx);

	do {
		if (vortex_debug > 5)
				printk(KERN_DEBUG "%s: In interrupt loop, status %4.4x.\n",
					   dev->name, status);

		if (status & UpComplete) {
			outw(AckIntr | UpComplete, ioaddr + EL3_CMD);
			boomerang_rx(dev);
		}

		if (status & DownComplete) {
			unsigned int dirty_tx = vp->dirty_tx;

			while (vp->cur_tx - dirty_tx > 0) {
				int entry = dirty_tx % TX_RING_SIZE;
				if (inl(ioaddr + DownListPtr) ==
					virt_to_bus(&vp->tx_ring[entry]))
					break;			/* It still hasn't been processed. */
				if (vp->tx_skbuff[entry]) {
					dev_kfree_skb(vp->tx_skbuff[entry]);
					vp->tx_skbuff[entry] = 0;
				}
				/* vp->stats.tx_packets++;  Counted below. */
				dirty_tx++;
			}
			vp->dirty_tx = dirty_tx;
			outw(AckIntr | DownComplete, ioaddr + EL3_CMD);
			if (vp->tx_full && (vp->cur_tx - dirty_tx <= TX_RING_SIZE - 1)) {
				vp->tx_full = 0;
				clear_bit(0, (void*)&dev->tbusy);
				mark_bh(NET_BH);
			}
		}

		/* Check for all uncommon interrupts at once. */
		if (status & (HostError | RxEarly | StatsFull | TxComplete | IntReq)) {
			if (status == 0xffff)
				break;
			vortex_error(dev, status);
		}

		if (--work_done < 0) {
			printk(KERN_WARNING "%s: Too much work in interrupt, status "
				   "%4.4x.\n", dev->name, status);
			/* Disable all pending interrupts. */
			do {
				vp->deferred |= status;
				outw(SetStatusEnb | (~vp->deferred & vp->status_enable),
					 ioaddr + EL3_CMD);
				outw(AckIntr | (vp->deferred & 0x7ff), ioaddr + EL3_CMD);
			} while ((status = inw(ioaddr + EL3_CMD)) & IntLatch);
			/* The timer will reenable interrupts. */
			mod_timer(&vp->timer, RUN_AT(1));
			break;
		}

		/* Acknowledge the IRQ. */
		outw(AckIntr | IntReq | IntLatch, ioaddr + EL3_CMD);
		if (vp->cb_fn_base)			/* The PCMCIA people are idiots.  */
			writel(0x8000, vp->cb_fn_base + 4);

	} while ((status = inw(ioaddr + EL3_STATUS)) & IntLatch);

	if (vortex_debug > 4)
		printk(KERN_DEBUG "%s: exiting interrupt, status %4.4x.\n",
			   dev->name, status);

no_int:
	spin_unlock(&vp->lock);
}

static int vortex_rx(struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;
	short rx_status;

	if (vortex_debug > 5)
		printk(KERN_DEBUG"   In vortex_rx(), status %4.4x, rx_status %4.4x.\n",
			   inw(ioaddr+EL3_STATUS), inw(ioaddr+RxStatus));
	while ((rx_status = inw(ioaddr + RxStatus)) > 0) {
		if (rx_status & 0x4000) { /* Error, update stats. */
			unsigned char rx_error = inb(ioaddr + RxErrors);
			if (vortex_debug > 2)
				printk(KERN_DEBUG " Rx error: status %2.2x.\n", rx_error);
			vp->stats.rx_errors++;
			if (rx_error & 0x01)  vp->stats.rx_over_errors++;
			if (rx_error & 0x02)  vp->stats.rx_length_errors++;
			if (rx_error & 0x04)  vp->stats.rx_frame_errors++;
			if (rx_error & 0x08)  vp->stats.rx_crc_errors++;
			if (rx_error & 0x10)  vp->stats.rx_length_errors++;
		} else {
			/* The packet length: up to 4.5K!. */
			int pkt_len = rx_status & 0x1fff;
			struct sk_buff *skb;

			skb = dev_alloc_skb(pkt_len + 5);
			if (vortex_debug > 4)
				printk(KERN_DEBUG "Receiving packet size %d status %4.4x.\n",
					   pkt_len, rx_status);
			if (skb != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
				/* 'skb_put()' points to the start of sk_buff data area. */
				if (vp->bus_master &&
					! (inw(ioaddr + Wn7_MasterStatus) & 0x8000)) {
					outl(virt_to_bus(skb_put(skb, pkt_len)),
						 ioaddr + Wn7_MasterAddr);
					outw((skb->len + 3) & ~3, ioaddr + Wn7_MasterLen);
					outw(StartDMAUp, ioaddr + EL3_CMD);
					while (inw(ioaddr + Wn7_MasterStatus) & 0x8000)
						;
				} else {
#ifdef __powerpc__
					insl_ns(ioaddr + RX_FIFO, skb_put(skb, pkt_len),
						 (pkt_len + 3) >> 2);
#else
					insl(ioaddr + RX_FIFO, skb_put(skb, pkt_len),
						 (pkt_len + 3) >> 2);
#endif
				}
				outw(RxDiscard, ioaddr + EL3_CMD); /* Pop top Rx packet. */
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				dev->last_rx = jiffies;
				vp->stats.rx_packets++;
				vp->stats.rx_bytes += skb->len;
				/* Wait a limited time to go to next packet. */
				for (i = 200; i >= 0; i--)
					if ( ! (inw(ioaddr + EL3_STATUS) & CmdInProgress))
						break;
				continue;
			} else if (vortex_debug)
				printk(KERN_NOTICE "%s: No memory to allocate a sk_buff of "
					   "size %d.\n", dev->name, pkt_len);
		}
		vp->stats.rx_dropped++;
		wait_for_completion(dev, RxDiscard);
	}

	return 0;
}

static int
boomerang_rx(struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	int entry = vp->cur_rx % RX_RING_SIZE;
	long ioaddr = dev->base_addr;
	int rx_status;
	int rx_work_limit = vp->dirty_rx + RX_RING_SIZE - vp->cur_rx;

	if (vortex_debug > 5)
		printk(KERN_DEBUG "  In boomerang_rx(), status %4.4x, rx_status "
			   "%4.4x.\n",
			   inw(ioaddr+EL3_STATUS), inw(ioaddr+RxStatus));
	while ((rx_status = le32_to_cpu(vp->rx_ring[entry].status)) & RxDComplete){
		if (--rx_work_limit < 0)
			break;
		if (rx_status & RxDError) { /* Error, update stats. */
			unsigned char rx_error = rx_status >> 16;
			if (vortex_debug > 2)
				printk(KERN_DEBUG " Rx error: status %2.2x.\n", rx_error);
			vp->stats.rx_errors++;
			if (rx_error & 0x01)  vp->stats.rx_over_errors++;
			if (rx_error & 0x02)  vp->stats.rx_length_errors++;
			if (rx_error & 0x04)  vp->stats.rx_frame_errors++;
			if (rx_error & 0x08)  vp->stats.rx_crc_errors++;
			if (rx_error & 0x10)  vp->stats.rx_length_errors++;
		} else {
			/* The packet length: up to 4.5K!. */
			int pkt_len = rx_status & 0x1fff;
			struct sk_buff *skb;

			vp->stats.rx_bytes += pkt_len;
			if (vortex_debug > 4)
				printk(KERN_DEBUG "Receiving packet size %d status %4.4x.\n",
					   pkt_len, rx_status);

			/* Check if the packet is long enough to just accept without
			   copying to a properly sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != 0) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
				/* 'skb_put()' points to the start of sk_buff data area. */
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(le32_to_cpu(vp->rx_ring[entry].addr)),
					   pkt_len);
				rx_copy++;
			} else {
				void *temp;
				/* Pass up the skbuff already on the Rx ring. */
				skb = vp->rx_skbuff[entry];
				vp->rx_skbuff[entry] = NULL;
				temp = skb_put(skb, pkt_len);
				/* Remove this checking code for final release. */
				if (bus_to_virt(le32_to_cpu(vp->rx_ring[entry].addr)) != temp)
					printk(KERN_ERR "%s: Warning -- the skbuff addresses do not match"
						   " in boomerang_rx: %p vs. %p.\n", dev->name,
						   bus_to_virt(le32_to_cpu(vp->rx_ring[entry].addr)),
						   temp);
				rx_nocopy++;
			}
			skb->protocol = eth_type_trans(skb, dev);
			{					/* Use hardware checksum info. */
				int csum_bits = rx_status & 0xee000000;
				if (csum_bits &&
					(csum_bits == (IPChksumValid | TCPChksumValid) ||
					 csum_bits == (IPChksumValid | UDPChksumValid))) {
					skb->ip_summed = CHECKSUM_UNNECESSARY;
					rx_csumhits++;
				}
			}
			netif_rx(skb);
			dev->last_rx = jiffies;
			vp->stats.rx_packets++;
		}
		entry = (++vp->cur_rx) % RX_RING_SIZE;
	}
	/* Refill the Rx ring buffers. */
	for (; vp->cur_rx - vp->dirty_rx > 0; vp->dirty_rx++) {
		struct sk_buff *skb;
		entry = vp->dirty_rx % RX_RING_SIZE;
		if (vp->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(PKT_BUF_SZ);
			if (skb == NULL) {
				static unsigned long last_jif;
				if ((jiffies - last_jif) > 10 * HZ) {
					printk(KERN_WARNING "%s: memory shortage\n", dev->name);
					last_jif = jiffies;
				}
				if ((vp->cur_rx - vp->dirty_rx) == RX_RING_SIZE)
					mod_timer(&vp->rx_oom_timer, RUN_AT(HZ * 1));
				break;			/* Bad news!  */
			}
			skb->dev = dev;			/* Mark as being used by this device. */
			skb_reserve(skb, 2);	/* Align IP on 16 byte boundaries */
			vp->rx_ring[entry].addr = cpu_to_le32(virt_to_bus(skb->tail));
			vp->rx_skbuff[entry] = skb;
		}
		vp->rx_ring[entry].status = 0;	/* Clear complete bit. */
		outw(UpUnstall, ioaddr + EL3_CMD);
	}
	return 0;
}

/*
 * If we've hit a total OOM refilling the Rx ring we poll once a second
 * for some memory.  Otherwise there is no way to restart the rx process.
 */
static void
rx_oom_timer(unsigned long arg)
{
	struct device *dev = (struct device *)arg;
	struct vortex_private *vp = (struct vortex_private *)dev->priv;

	spin_lock_irq(&vp->lock);
	if ((vp->cur_rx - vp->dirty_rx) == RX_RING_SIZE)	/* This test is redundant, but makes me feel good */
		boomerang_rx(dev);
	if (vortex_debug > 1) {
		printk(KERN_DEBUG "%s: rx_oom_timer %s\n", dev->name,
			((vp->cur_rx - vp->dirty_rx) != RX_RING_SIZE) ? "succeeded" : "retrying");
	}
	spin_unlock_irq(&vp->lock);
}

static int
vortex_close(struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	dev->start = 0;
	dev->tbusy = 1;

	if (vortex_debug > 1) {
		printk(KERN_DEBUG"%s: vortex_close() status %4.4x, Tx status %2.2x.\n",
			   dev->name, inw(ioaddr + EL3_STATUS), inb(ioaddr + TxStatus));
		printk(KERN_DEBUG "%s: vortex close stats: rx_nocopy %d rx_copy %d"
			   " tx_queued %d Rx pre-checksummed %d.\n",
			   dev->name, rx_nocopy, rx_copy, queued_packet, rx_csumhits);
	}

	del_timer(&vp->timer);
	del_timer(&vp->rx_oom_timer);

	/* Turn off statistics ASAP.  We update vp->stats below. */
	outw(StatsDisable, ioaddr + EL3_CMD);

	/* Disable the receiver and transmitter. */
	outw(RxDisable, ioaddr + EL3_CMD);
	outw(TxDisable, ioaddr + EL3_CMD);

	if (dev->if_port == XCVR_10base2)
		/* Turn off thinnet power.  Green! */
		outw(StopCoax, ioaddr + EL3_CMD);

	free_irq(dev->irq, dev);

	outw(SetIntrEnb | 0x0000, ioaddr + EL3_CMD);

	update_stats(ioaddr, dev);
	if (vp->full_bus_master_rx) { /* Free Boomerang bus master Rx buffers. */
		outl(0, ioaddr + UpListPtr);
		for (i = 0; i < RX_RING_SIZE; i++)
			if (vp->rx_skbuff[i]) {
				dev_kfree_skb(vp->rx_skbuff[i]);
				vp->rx_skbuff[i] = 0;
			}
	}
	if (vp->full_bus_master_tx) { /* Free Boomerang bus master Tx buffers. */
		outl(0, ioaddr + DownListPtr);
		for (i = 0; i < TX_RING_SIZE; i++)
			if (vp->tx_skbuff[i]) {
				dev_kfree_skb(vp->tx_skbuff[i]);
				vp->tx_skbuff[i] = 0;
			}
	}

	if (vp->enable_wol && (vp->capabilities & CapPwrMgmt))
		acpi_set_WOL(dev);
	MOD_DEC_USE_COUNT;

	return 0;
}

static struct net_device_stats *vortex_get_stats(struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	unsigned long flags;

	if (dev->start) {
		spin_lock_irqsave(&vp->lock, flags);
		update_stats(dev->base_addr, dev);
		spin_unlock_irqrestore(&vp->lock, flags);
	}
	return &vp->stats;
}

/*  Update statistics.
	Unlike with the EL3 we need not worry about interrupts changing
	the window setting from underneath us, but we must still guard
	against a race condition with a StatsUpdate interrupt updating the
	table.  This is done by checking that the ASM (!) code generated uses
	atomic updates with '+='.
	*/
static void update_stats(long ioaddr, struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;

	/* Unlike the 3c5x9 we need not turn off stats updates while reading. */
	/* Switch to the stats window, and read everything. */
	EL3WINDOW(6);
	vp->stats.tx_carrier_errors		+= inb(ioaddr + 0);
	vp->stats.tx_heartbeat_errors	+= inb(ioaddr + 1);
	/* Multiple collisions. */		inb(ioaddr + 2);
	vp->stats.collisions			+= inb(ioaddr + 3);
	vp->stats.tx_window_errors		+= inb(ioaddr + 4);
	vp->stats.rx_fifo_errors		+= inb(ioaddr + 5);
	vp->stats.tx_packets			+= inb(ioaddr + 6);
	vp->stats.tx_packets			+= (inb(ioaddr + 9)&0x30) << 4;
	/* Rx packets	*/				inb(ioaddr + 7);   /* Must read to clear */
	/* Tx deferrals */				inb(ioaddr + 8);
	/* Don't bother with register 9, an extension of registers 6&7.
	   If we do use the 6&7 values the atomic update assumption above
	   is invalid. */
	inw(ioaddr + 10);	/* Total Rx and Tx octets. */
	inw(ioaddr + 12);
	/* New: On the Vortex we must also clear the BadSSD counter. */
	EL3WINDOW(4);
	inb(ioaddr + 12);

	/* We change back to window 7 (not 1) with the Vortex. */
	EL3WINDOW(7);
	return;
}

static int vortex_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u16 *data = (u16 *)&rq->ifr_data;
	int phy = vp->phys[0] & 0x1f;
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&vp->lock, flags);

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = phy;
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		EL3WINDOW(4);
		data[3] = mdio_read(ioaddr, data[0] & 0x1f, data[1] & 0x1f);
		retval = 0;
		break;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN)) {
			retval = -EPERM;
		} else {
			EL3WINDOW(4);
			mdio_write(ioaddr, data[0] & 0x1f, data[1] & 0x1f, data[2]);
			retval = 0;
		}
		break;
	default:
		retval = -EOPNOTSUPP;
	}

	spin_unlock_irqrestore(&vp->lock, flags);
	return retval;
}

/* Pre-Cyclone chips have no documented multicast filter, so the only
   multicast setting is to receive all multicast frames.  At least
   the chip has a very clean way to set the mode, unlike many others. */
static void set_rx_mode(struct device *dev)
{
	long ioaddr = dev->base_addr;
	int new_mode;

	if (dev->flags & IFF_PROMISC) {
		if (vortex_debug > 0)
			printk(KERN_NOTICE "%s: Setting promiscuous mode.\n", dev->name);
		new_mode = SetRxFilter|RxStation|RxMulticast|RxBroadcast|RxProm;
	} else	if ((dev->mc_list)  ||  (dev->flags & IFF_ALLMULTI)) {
		new_mode = SetRxFilter|RxStation|RxMulticast|RxBroadcast;
	} else
		new_mode = SetRxFilter | RxStation | RxBroadcast;

	outw(new_mode, ioaddr + EL3_CMD);
}


/* MII transceiver control section.
   Read and write the MII registers using software-generated serial
   MDIO protocol.  See the MII specifications or DP83840A data sheet
   for details. */

/* The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues. */
#define mdio_delay() inl(mdio_addr)

#define MDIO_SHIFT_CLK	0x01
#define MDIO_DIR_WRITE	0x04
#define MDIO_DATA_WRITE0 (0x00 | MDIO_DIR_WRITE)
#define MDIO_DATA_WRITE1 (0x02 | MDIO_DIR_WRITE)
#define MDIO_DATA_READ	0x02
#define MDIO_ENB_IN		0x00

/* Generate the preamble required for initial synchronization and
   a few older transceivers. */
static void mdio_sync(long ioaddr, int bits)
{
	long mdio_addr = ioaddr + Wn4_PhysicalMgmt;

	/* Establish sync by sending at least 32 logic ones. */
	while (-- bits >= 0) {
		outw(MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outw(MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
}

static int mdio_read(long ioaddr, int phy_id, int location)
{
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	unsigned int retval = 0;
	long mdio_addr = ioaddr + Wn4_PhysicalMgmt;

	if (mii_preamble_required)
		mdio_sync(ioaddr, 32);

	/* Shift the read command bits out. */
	for (i = 14; i >= 0; i--) {
		int dataval = (read_cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
		outw(dataval, mdio_addr);
		mdio_delay();
		outw(dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		outw(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inw(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		outw(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
#if 0
	return (retval>>1) & 0x1ffff;
#else
	return retval & 0x20000 ? 0xffff : retval>>1 & 0xffff;
#endif
}

static void mdio_write(long ioaddr, int phy_id, int location, int value)
{
	int write_cmd = 0x50020000 | (phy_id << 23) | (location << 18) | value;
	long mdio_addr = ioaddr + Wn4_PhysicalMgmt;
	int i;

	if (mii_preamble_required)
		mdio_sync(ioaddr, 32);

	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (write_cmd&(1<<i)) ? MDIO_DATA_WRITE1 : MDIO_DATA_WRITE0;
		outw(dataval, mdio_addr);
		mdio_delay();
		outw(dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Leave the interface idle. */
	for (i = 1; i >= 0; i--) {
		outw(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		outw(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}

	return;
}

/* ACPI: Advanced Configuration and Power Interface. */
/* Set Wake-On-LAN mode and put the board into D3 (power-down) state. */
static void acpi_set_WOL(struct device *dev)
{
	struct vortex_private *vp = (struct vortex_private *)dev->priv;
	long ioaddr = dev->base_addr;

	/* Power up on: 1==Downloaded Filter, 2==Magic Packets, 4==Link Status. */
	EL3WINDOW(7);
	outw(2, ioaddr + 0x0c);
	/* The RxFilter must accept the WOL frames. */
	outw(SetRxFilter|RxStation|RxMulticast|RxBroadcast, ioaddr + EL3_CMD);
	outw(RxEnable, ioaddr + EL3_CMD);
	/* Change the power state to D3; RxEnable doesn't take effect. */
	pcibios_write_config_word(vp->pci_bus, vp->pci_devfn, 0xe0, 0x8103);
}

/* Change from D3 (sleep) to D0 (active).
   Problem: The Cyclone forgets all PCI config info during the transition! */
static void acpi_wake(int bus, int devfn)
{
	u32 base0, base1, romaddr;
	u16 pci_command, pwr_command;
	u8  pci_latency, pci_cacheline, irq;

	pcibios_read_config_word(bus, devfn, 0xe0, &pwr_command);
	if ((pwr_command & 3) == 0)
		return;
	pcibios_read_config_word( bus, devfn, PCI_COMMAND, &pci_command);
	pcibios_read_config_dword(bus, devfn, PCI_BASE_ADDRESS_0, &base0);
	pcibios_read_config_dword(bus, devfn, PCI_BASE_ADDRESS_1, &base1);
	pcibios_read_config_dword(bus, devfn, PCI_ROM_ADDRESS, &romaddr);
	pcibios_read_config_byte( bus, devfn, PCI_LATENCY_TIMER, &pci_latency);
	pcibios_read_config_byte( bus, devfn, PCI_CACHE_LINE_SIZE, &pci_cacheline);
	pcibios_read_config_byte( bus, devfn, PCI_INTERRUPT_LINE, &irq);

	pcibios_write_config_word( bus, devfn, 0xe0, 0x0000);
	pcibios_write_config_dword(bus, devfn, PCI_BASE_ADDRESS_0, base0);
	pcibios_write_config_dword(bus, devfn, PCI_BASE_ADDRESS_1, base1);
	pcibios_write_config_dword(bus, devfn, PCI_ROM_ADDRESS, romaddr);
	pcibios_write_config_byte( bus, devfn, PCI_INTERRUPT_LINE, irq);
	pcibios_write_config_byte( bus, devfn, PCI_LATENCY_TIMER, pci_latency);
	pcibios_write_config_byte( bus, devfn, PCI_CACHE_LINE_SIZE, pci_cacheline);
	pcibios_write_config_word( bus, devfn, PCI_COMMAND, pci_command | 5);
}


#ifdef MODULE
void cleanup_module(void)
{
	struct device *next_dev;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_vortex_dev) {
		struct vortex_private *vp=(void *)(root_vortex_dev->priv);
		int drv_flags = pci_tbl[vp->chip_id].drv_flags;
		next_dev = vp->next_module;
		unregister_netdev(root_vortex_dev);
		outw((drv_flags & EEPROM_NORESET) ? (TotalReset|0x10) : TotalReset,
				root_vortex_dev->base_addr + EL3_CMD);
		release_region(root_vortex_dev->base_addr,
					   pci_tbl[vp->chip_id].io_size);
		kfree(root_vortex_dev);
		kfree(vp->priv_addr);
		root_vortex_dev = next_dev;
	}
}

#endif  /* MODULE */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
