/*
 * Generic Macintosh NCR5380 driver
 *
 * Copyright 1998, Michael Schmitz <mschmitz@lbl.gov>
 *
 * Pseudo-DMA, Ove Edlund <ove.edlund@sm.luth.se>, June 2000
 *
 * derived in part from:
 */
/*
 * Generic Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

/*
 * $Log: mac_NCR5380.c,v $
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/ctype.h>
#include <linux/delay.h>

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blk.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/machw.h>
#include <asm/mac_via.h>

#include "scsi.h"
#include "hosts.h"
#include "mac_scsi.h"
#include "NCR5380.h"
#include "constants.h"

#if 0
#define NDEBUG (NDEBUG_INTR | NDEBUG_PSEUDO_DMA | NDEBUG_ARBITRATION | NDEBUG_SELECTION | NDEBUG_RESELECTION)
#else
#define NDEBUG (NDEBUG_ABORT)
#endif

#define	ENABLE_IRQ()	mac_enable_irq( IRQ_MAC_SCSI ); 
#define	DISABLE_IRQ()	mac_disable_irq( IRQ_MAC_SCSI );

extern void via_scsi_clear(void);

static void mac_scsi_reset_boot(struct Scsi_Host *instance);
static char macscsi_read(struct Scsi_Host *instance, int reg);
static void macscsi_write(struct Scsi_Host *instance, int reg, int value);

static int setup_can_queue = -1;
static int setup_cmd_per_lun = -1;
static int setup_sg_tablesize = -1;
static int setup_use_tagged_queuing = -1;
static int setup_hostid = -1;

/* Time (in jiffies) to wait after a reset; the SCSI standard calls for 250ms,
 * we usually do 0.5s to be on the safe side. But Toshiba CD-ROMs once more
 * need ten times the standard value... */
#define TOSHIBA_DELAY

#ifdef TOSHIBA_DELAY
#define	AFTER_RESET_DELAY	(5*HZ/2)
#else
#define	AFTER_RESET_DELAY	(HZ/2)
#endif

static struct proc_dir_entry proc_scsi_mac5380 = {
	PROC_SCSI_MAC, 13, "Mac 5380 SCSI", S_IFDIR | S_IRUGO, S_IXUGO, 2
};

/* Must move these into a per-host thingy once the DuoDock is supported */
static volatile unsigned char *mac_scsi_regp = NULL;
static volatile unsigned char *mac_scsi_drq  = NULL;
static volatile unsigned char *mac_scsi_nodrq = NULL;

/*
 * Function : mac_scsi_setup(char *str, int *ints)
 *
 * Purpose : booter command line initialization of the overrides array,
 *
 * Inputs : str - unused, ints - array of integer parameters with ints[0]
 *	equal to the number of ints.
 *
 */

void mac_scsi_setup(char *str, int *ints)
{
	/* Format of mac5380 parameter is:
	 *   mac5380=<can_queue>,<cmd_per_lun>,<sg_tablesize>,<hostid>,<use_tags>
	 * Negative values mean don't change.
	 */
	
	/* Grmbl... the standard parameter parsing can't handle negative numbers
	 * :-( So let's do it ourselves!
	 */

	int i = ints[0]+1, fact;

	while( str && (isdigit(*str) || *str == '-') && i <= 10) {
		if (*str == '-')
			fact = -1, ++str;
		else
			fact = 1;
		ints[i++] = simple_strtoul( str, NULL, 0 ) * fact;
		if ((str = strchr( str, ',' )) != NULL)
			++str;
	}
	ints[0] = i-1;
	
	if (ints[0] < 1) {
		printk( "mac_scsi_setup: no arguments!\n" );
		return;
	}

	if (ints[0] >= 1) {
		if (ints[1] > 0)
			/* no limits on this, just > 0 */
			setup_can_queue = ints[1];
	}
	if (ints[0] >= 2) {
		if (ints[2] > 0)
			setup_cmd_per_lun = ints[2];
	}
	if (ints[0] >= 3) {
		if (ints[3] >= 0) {
			setup_sg_tablesize = ints[3];
			/* Must be <= SG_ALL (255) */
			if (setup_sg_tablesize > SG_ALL)
				setup_sg_tablesize = SG_ALL;
		}
	}
	if (ints[0] >= 4) {
		/* Must be between 0 and 7 */
		if (ints[4] >= 0 && ints[4] <= 7)
			setup_hostid = ints[4];
		else if (ints[4] > 7)
			printk( "mac_scsi_setup: invalid host ID %d !\n", ints[4] );
	}
	if (ints[0] >= 5) {
		if (ints[5] >= 0)
			setup_use_tagged_queuing = !!ints[5];
	}
}

/*
 * XXX: status debug
 */
static struct Scsi_Host *default_instance;

/*
 * Function : int macscsi_detect(Scsi_Host_Template * tpnt)
 *
 * Purpose : initializes mac NCR5380 driver based on the
 *	command line / compile time port and irq definitions.
 *
 * Inputs : tpnt - template for this SCSI adapter.
 *
 * Returns : 1 if a host adapter was found, 0 if not.
 *
 */
 
int macscsi_detect(Scsi_Host_Template * tpnt)
{
    static int called = 0;
    int flags = 0;
    struct Scsi_Host *instance;

    if (!MACH_IS_MAC || called)
	return( 0 );

    if (macintosh_config->scsi_type != MAC_SCSI_OLD)
	return( 0 );

    tpnt->proc_dir = &proc_scsi_mac5380;

    /* setup variables */
    tpnt->can_queue =
	(setup_can_queue > 0) ? setup_can_queue : CAN_QUEUE;
    tpnt->cmd_per_lun =
	(setup_cmd_per_lun > 0) ? setup_cmd_per_lun : CMD_PER_LUN;
    tpnt->sg_tablesize = 
	(setup_sg_tablesize >= 0) ? setup_sg_tablesize : SG_TABLESIZE;

    if (setup_hostid >= 0)
	tpnt->this_id = setup_hostid;
    else {
	/* use 7 as default */
	tpnt->this_id = 7;
    }

    if (setup_use_tagged_queuing < 0)
	setup_use_tagged_queuing = USE_TAGGED_QUEUING;

    /* Once we support multiple 5380s (e.g. DuoDock) we'll do
       something different here */
    instance = scsi_register (tpnt, sizeof(struct NCR5380_hostdata));
    default_instance = instance;
    
    if (macintosh_config->ident == MAC_MODEL_IIFX) {
	mac_scsi_regp  = via1+0x8000;
	mac_scsi_drq   = via1+0xE000;
	mac_scsi_nodrq = via1+0xC000;
	flags = FLAG_NO_PSEUDO_DMA;
    } else {
	mac_scsi_regp  = via1+0x10000;
	mac_scsi_drq   = via1+0x6000;
	mac_scsi_nodrq = via1+0x12000;
    }

    instance->io_port = (unsigned long) mac_scsi_regp;
    instance->irq = IRQ_MAC_SCSI;

    /* Turn off pseudo DMA and IRQs for II, IIx, IIcx, and SE/30.
     * XXX: This is a kludge until we can figure out why interrupts 
     * won't work.
     */
   
    if (macintosh_config->via_type == MAC_VIA_II) {
        flags = FLAG_NO_PSEUDO_DMA;
        instance->irq = IRQ_NONE;
    }
   
    mac_scsi_reset_boot(instance);

    NCR5380_init(instance, flags);

    instance->n_io_port = 255;

    ((struct NCR5380_hostdata *)instance->hostdata)->ctrl = 0;

    if (instance->irq != IRQ_NONE)
	if (request_irq(instance->irq, macscsi_intr, 0, "MacSCSI-5380",
			(void *) mac_scsi_regp)) {
	    printk("scsi%d: IRQ%d not free, interrupts disabled\n",
		   instance->host_no, instance->irq);
	    instance->irq = IRQ_NONE;
	}

    printk("scsi%d: generic 5380 at port %lX irq", instance->host_no, instance->io_port);
    if (instance->irq == IRQ_NONE)
	printk ("s disabled");
    else
	printk (" %d", instance->irq);
    printk(" options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
	   instance->can_queue, instance->cmd_per_lun, MACSCSI_PUBLIC_RELEASE);
    printk("\nscsi%d:", instance->host_no);
    NCR5380_print_options(instance);
    printk("\n");
    called = 1;
    return 1;
}

int macscsi_release (struct Scsi_Host *shpnt)
{
	if (shpnt->irq != IRQ_NONE)
		free_irq (shpnt->irq, NULL);

	return 0;
}

/*
 * Our 'bus reset on boot' function
 */

static void mac_scsi_reset_boot(struct Scsi_Host *instance)
{
	unsigned long end;

	NCR5380_local_declare();
	NCR5380_setup(instance);
	
	/*
	 * Do a SCSI reset to clean up the bus during initialization. No messing
	 * with the queues, interrupts, or locks necessary here.
	 */

	printk( "Macintosh SCSI: resetting the SCSI bus..." );

	/* switch off SCSI IRQ - catch an interrupt without IRQ bit set else */
	DISABLE_IRQ()

	/* get in phase */
	NCR5380_write( TARGET_COMMAND_REG,
		      PHASE_SR_TO_TCR( NCR5380_read(STATUS_REG) ));

	/* assert RST */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_RST );
	/* The min. reset hold time is 25us, so 40us should be enough */
	udelay( 50 );
	/* reset RST and interrupt */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE );
	NCR5380_read( RESET_PARITY_INTERRUPT_REG );

	for( end = jiffies + AFTER_RESET_DELAY; jiffies < end; )
		barrier();

	/* switch on SCSI IRQ again */
	ENABLE_IRQ()

	printk( " done\n" );
}

/*
 * NCR 5380 register access functions
 */

#define CTRL(p,v) (*ctrl = (v))

static char macscsi_read(struct Scsi_Host *instance, int reg)
{
  int iobase = instance->io_port;
  int i;
  int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;

  CTRL(iobase, 0);
  i = inb(iobase + (reg<<4));
  CTRL(iobase, 0x40);

  return i;
}

static void macscsi_write(struct Scsi_Host *instance, int reg, int value)
{
  int iobase = instance->io_port;
  int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;

  CTRL(iobase, 0);
  outb(value, iobase + (reg<<4));
  CTRL(iobase, 0x40);
}

/* Pseudo-DMA stuff */

static inline void cp_io_to_mem(volatile unsigned char *s, volatile unsigned char *d, int len)
{
  asm volatile("   cmp.w  #4,%2; \
                   ble    8f; \
                   move.w %1,%%d0; \
                   neg.b  %%d0; \
                   and.w  #3,%%d0; \
                   sub.w  %%d0,%2; \
                   bra    2f; \
                1: move.b (%0),(%1)+; \
                2: dbf    %%d0,1b; \
                   move.w %2,%%d0; \
                   lsr.w  #5,%%d0; \
                   bra    4f; \
                3: move.l (%0),(%1)+; \
                   move.l (%0),(%1)+; \
                   move.l (%0),(%1)+; \
                   move.l (%0),(%1)+; \
                   move.l (%0),(%1)+; \
                   move.l (%0),(%1)+; \
                   move.l (%0),(%1)+; \
                   move.l (%0),(%1)+; \
                4: dbf    %%d0,3b; \
                   move.w %2,%%d0; \
                   lsr.w  #2,%%d0; \
                   and.w  #7,%%d0; \
                   bra    6f; \
                5: move.l (%0),(%1)+; \
                6: dbf    %%d0,5b; \
                   and.w  #3,%2
                   bra    8f; \
                7: move.b (%0),(%1)+; \
                8: dbf    %2,7b"
               :: "a" (s), "a" (d), "d" (len)
               :  "%%d0");
}

static inline int macscsi_pread (struct Scsi_Host *instance,
				 unsigned char *dst, int len)
{
   unsigned char *d;
   volatile unsigned char *s;

   NCR5380_local_declare();
   NCR5380_setup(instance);

   s = mac_scsi_drq+0x60;
   d = dst;

/* These conditions are derived from MacOS. (How to read CONTROL_STATUS_REG?) */

   while (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) 
         && !(NCR5380_read(STATUS_REG) & SR_REQ))
      ;
   if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) 
         && (NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH)) {
      printk("Error in macscsi_pread\n");
      return 5;
   }

   cp_io_to_mem(s, d, len);
   
   return 0;
}



static inline void cp_mem_to_io(volatile unsigned char *s, volatile unsigned char *d, int len)
{
  asm volatile("   cmp.w  #4,%2; \
                   ble    8f; \
                   move.w %0,%%d0; \
                   neg.b  %%d0; \
                   and.w  #3,%%d0; \
                   sub.w  %%d0,%2; \
                   bra    2f; \
                1: move.b (%0)+,(%1); \
                2: dbf    %%d0,1b; \
                   move.w %2,%%d0; \
                   lsr.w  #5,%%d0; \
                   bra    4f; \
                3: move.l (%0)+,(%1); \
                   move.l (%0)+,(%1); \
                   move.l (%0)+,(%1); \
                   move.l (%0)+,(%1); \
                   move.l (%0)+,(%1); \
                   move.l (%0)+,(%1); \
                   move.l (%0)+,(%1); \
                   move.l (%0)+,(%1); \
                4: dbf    %%d0,3b; \
                   move.w %2,%%d0; \
                   lsr.w  #2,%%d0; \
                   and.w  #7,%%d0; \
                   bra    6f; \
                5: move.l (%0)+,(%1); \
                6: dbf    %%d0,5b; \
                   and.w  #3,%2
                   bra    8f; \
                7: move.b (%0)+,(%1); \
                8: dbf    %2,7b"
               :: "a" (s), "a" (d), "d" (len)
               :  "%%d0");
}

static inline int macscsi_pwrite (struct Scsi_Host *instance,
				  unsigned char *src, int len)
{
   unsigned char *d;

   NCR5380_local_declare();
   NCR5380_setup(instance);

   d = mac_scsi_drq;
   
/* These conditions are derived from MacOS. (How to read CONTROL_STATUS_REG?) */

   while (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) 
         && !(NCR5380_read(STATUS_REG) & SR_REQ) 
         && (NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH)) 
      ;
   if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ)) {
      printk("Error in macscsi_pwrite\n");
      return 5;
   }

   cp_mem_to_io(src, d, len);   

   return 0;
}

/* These control the behaviour of the generic 5380 core */
#define AUTOSENSE
#undef DMA_WORKS_RIGHT
#define PSEUDO_DMA

#include "NCR5380.c"

#ifdef MODULE

Scsi_Host_Template driver_template = MAC_NCR5380;

#include "scsi_module.c"
#endif
