#ifndef __irq_h
#define __irq_h

#include <asm/hardirq.h>

/*
 * How many IRQ's for S390 ?!?
 */
#define __MAX_SUBCHANNELS 65536
#define NR_IRQS           __MAX_SUBCHANNELS

#define INVALID_STORAGE_AREA ((void *)(-1 - 0x3FFF ))

extern int disable_irq(unsigned int);
extern int enable_irq(unsigned int);

/*
 * Interrupt controller descriptor. This is all we need
 * to describe about the low-level hardware.
 */
struct hw_interrupt_type {
        const char   *typename;
        int         (*handle)(unsigned int irq,
                              int cpu,
                              struct pt_regs * regs);
        int         (*enable) (unsigned int irq);
        int         (*disable)(unsigned int irq);
};

/*
 * Status: reason for being disabled: somebody has
 * done a "disable_irq()" or we must not re-enter the
 * already executing irq..
 */
#define IRQ_INPROGRESS  1
#define IRQ_DISABLED    2
#define IRQ_PENDING     4

/*
 * path management control word
 */
typedef struct {
      unsigned long  intparm;      /* interruption parameter */
      unsigned int   res0 : 2;     /* reserved zeros */
      unsigned int   isc  : 3;     /* interruption sublass */
      unsigned int   res5 : 3;     /* reserved zeros */
      unsigned int   ena  : 1;     /* enabled */
      unsigned int   lm   : 2;     /* limit mode */
      unsigned int   mme  : 2;     /* measurement-mode enable */
      unsigned int   mp   : 1;     /* multipath mode */
      unsigned int   tf   : 1;     /* timing facility */
      unsigned int   dnv  : 1;     /* device number valid */
      unsigned int   dev  : 16;    /* device number */
      unsigned char  lpm;          /* logical path mask */
      unsigned char  pnom;         /* path not operational mask */
      unsigned char  lpum;         /* last path used mask */
      unsigned char  pim;          /* path installed mask */
      unsigned short mbi;          /* measurement-block index */
      unsigned char  pom;          /* path operational mask */
      unsigned char  pam;          /* path available mask */
      unsigned char  chpid[8];     /* CHPID 0-7 (if available) */
      unsigned int   unused1 : 8;  /* reserved zeros */
      unsigned int   st      : 3;  /* subchannel type */
      unsigned int   unused2 : 20; /* reserved zeros */
      unsigned int   csense  : 1;  /* concurrent sense; can be enabled ...*/
                                   /*  ... per MSCH, however, if facility */
                                   /*  ... is not installed, this results */
                                   /*  ... in an operand exception.       */
   } pmcw_t;

/*
 * subchannel status word
 */
typedef struct {
      unsigned int   key  : 4; /* subchannel key */
      unsigned int   sctl : 1; /* suspend control */
      unsigned int   eswf : 1; /* ESW format */
      unsigned int   cc   : 2; /* deferred condition code */
      unsigned int   fmt  : 1; /* format */
      unsigned int   pfch : 1; /* prefetch */
      unsigned int   isic : 1; /* initial-status interruption control */
      unsigned int   alcc : 1; /* address-limit checking control */
      unsigned int   ssi  : 1; /* supress-suspended interruption */
      unsigned int   zcc  : 1; /* zero condition code */
      unsigned int   ectl : 1; /* extended control */
      unsigned int   pno  : 1;     /* path not operational */
      unsigned int   res  : 1;     /* reserved */
      unsigned int   fctl : 3;     /* function control */
      unsigned int   actl : 7;     /* activity control */
      unsigned int   stctl : 5;    /* status control */
      unsigned long  cpa;          /* channel program address */
      unsigned int   dstat : 8;    /* device status */
      unsigned int   cstat : 8;    /* subchannel status */
      unsigned int   count : 16;   /* residual count */
   } scsw_t;

#define SCSW_FCTL_CLEAR_FUNC     0x1
#define SCSW_FCTL_HALT_FUNC      0x2
#define SCSW_FCTL_START_FUNC     0x4

#define SCSW_ACTL_SUSPENDED      0x1
#define SCSW_ACTL_DEVACT         0x2
#define SCSW_ACTL_SCHACT         0x4
#define SCSW_ACTL_CLEAR_PEND     0x8
#define SCSW_ACTL_HALT_PEND      0x10
#define SCSW_ACTL_START_PEND     0x20
#define SCSW_ACTL_RESUME_PEND    0x40

#define SCSW_STCTL_STATUS_PEND   0x1
#define SCSW_STCTL_SEC_STATUS    0x2
#define SCSW_STCTL_PRIM_STATUS   0x4
#define SCSW_STCTL_INTER_STATUS  0x8
#define SCSW_STCTL_ALERT_STATUS  0x10

#define DEV_STAT_ATTENTION       0x80
#define DEV_STAT_STAT_MOD        0x40
#define DEV_STAT_CU_END          0x20
#define DEV_STAT_BUSY            0x10
#define DEV_STAT_CHN_END         0x08
#define DEV_STAT_DEV_END         0x04
#define DEV_STAT_UNIT_CHECK      0x02
#define DEV_STAT_UNIT_EXCEP      0x01

#define SCHN_STAT_PCI            0x80
#define SCHN_STAT_INCORR_LEN     0x40
#define SCHN_STAT_PROG_CHECK     0x20
#define SCHN_STAT_PROT_CHECK     0x10
#define SCHN_STAT_CHN_DATA_CHK   0x08
#define SCHN_STAT_CHN_CTRL_CHK   0x04
#define SCHN_STAT_INTF_CTRL_CHK  0x02
#define SCHN_STAT_CHAIN_CHECK    0x01

/*
 * subchannel information block
 */
typedef struct {
      pmcw_t pmcw;             /* path management control word */
      scsw_t scsw;             /* subchannel status word */
      char mda[12];            /* model dependent area */
   } schib_t;

typedef struct {
      char            cmd_code;/* command code */
      char            flags;   /* flags, like IDA adressing, etc. */
      unsigned short  count;   /* byte count */
      void           *cda;     /* data address */
   } ccw1_t __attribute__ ((aligned(8)));

#define CCW_FLAG_DC             0x80
#define CCW_FLAG_CC             0x40
#define CCW_FLAG_SLI            0x20
#define CCW_FLAG_SKIP           0x10
#define CCW_FLAG_PCI            0x08
#define CCW_FLAG_IDA            0x04
#define CCW_FLAG_SUSPEND        0x02

#define CCW_CMD_BASIC_SENSE     0x04
#define CCW_CMD_TIC             0x08
#define CCW_CMD_SENSE_ID        0xE4
#define CCW_CMD_NOOP            0x03
#define CCW_CMD_RDC             0x64
#define CCW_CMD_READ_IPL        0x02

#define SENSE_MAX_COUNT         0x20

/*
 * architectured values for first sense byte
 */
#define SNS0_CMD_REJECT         0x80
#define SNS_CMD_REJECT          SNS0_CMD_REJECT
#define SNS0_INTERVENTION_REQ   0x40
#define SNS0_BUS_OUT_CHECK      0x20
#define SNS0_EQUIPMENT_CHECK    0x10
#define SNS0_DATA_CHECK         0x08
#define SNS0_OVERRUN            0x04

/*
 * operation request block
 */
typedef struct {
      unsigned long  intparm;  /* interruption parameter */
      unsigned int   key  : 4; /* flags, like key, suspend control, etc. */
      unsigned int   spnd : 1; /* suspend control */
      unsigned int   res1 : 3; /* reserved */
      unsigned int   fmt  : 1; /* format control */
      unsigned int   pfch : 1; /* prefetch control */
      unsigned int   isic : 1; /* initial-status-interruption control */
      unsigned int   alcc : 1; /* address-limit-checking control */
      unsigned int   ssic : 1; /* suppress-suspended-interr. control */
      unsigned int   res2 : 3; /* reserved */
      unsigned int   lpm  : 8; /* logical path mask */
      unsigned int   ils  : 1; /* incorrect length */
      unsigned int   zero : 7; /* reserved zeros */
      ccw1_t        *cpa;      /* channel program address */
   }  __attribute__ ((packed,aligned(4))) orb_t;

typedef struct {
      unsigned int res0  : 4;  /* reserved */
      unsigned int pvrf  : 1;  /* path-verification-required flag */
      unsigned int cpt   : 1;  /* channel-path timeout */
      unsigned int fsavf : 1;  /* Failing storage address validity flag */
      unsigned int cons  : 1;  /* concurrent-sense */
      unsigned int res8  : 2;  /* reserved */
      unsigned int scnt  : 6;  /* sense count if cons == 1 */
      unsigned int res16 : 16; /* reserved */
   } erw_t;

/*
 * subchannel logout area
 */
typedef struct {
      unsigned int res0  : 1;  /* reserved */
      unsigned int esf   : 7;  /* extended status flags */
      unsigned int lpum  : 8;  /* last path used mask */
      unsigned int res16 : 1;  /* reserved */
      unsigned int fvf   : 5;  /* field-validity flags */
      unsigned int sacc  : 2;  /* storage access code */
      unsigned int termc : 2;  /* termination code */
      unsigned int devsc : 1;  /* device-status check */
      unsigned int serr  : 1;  /* secondary error */
      unsigned int ioerr : 1;  /* i/o-error alert */
      unsigned int seqc  : 3;  /* sequence code */
   } sublog_t ;

/*
 * Format 0 Extended Status Word (ESW)
 */
typedef struct {
      sublog_t      sublog;    /* subchannel logout */
      erw_t         erw;       /* extended report word */
      void         *faddr;     /* failing address */
      unsigned int  zeros[2];  /* 2 fullwords of zeros */
   } esw0_t;

/*
 * Format 1 Extended Status Word (ESW)
 */
typedef struct {
      unsigned char  zero0;    /* reserved zeros */
      unsigned char  lpum;     /* last path used mask */
      unsigned short zero16;   /* reserved zeros */
      erw_t          erw;      /* extended report word */
      unsigned int   zeros[3]; /* 2 fullwords of zeros */
   } esw1_t;

/*
 * Format 2 Extended Status Word (ESW)
 */
typedef struct {
      unsigned char  zero0;    /* reserved zeros */
      unsigned char  lpum;     /* last path used mask */
      unsigned short dcti;     /* device-connect-time interval */
      erw_t          erw;      /* extended report word */
      unsigned int   zeros[3]; /* 2 fullwords of zeros */
   } esw2_t;

/*
 * Format 3 Extended Status Word (ESW)
 */
typedef struct {
      unsigned char  zero0;    /* reserved zeros */
      unsigned char  lpum;     /* last path used mask */
      unsigned short res;      /* reserved */
      erw_t          erw;      /* extended report word */
      unsigned int   zeros[3]; /* 2 fullwords of zeros */
   } esw3_t;

typedef union {
      esw0_t esw0;
      esw1_t esw1;
      esw2_t esw2;
      esw3_t esw3;
   } esw_t;

/*
 * interruption response block
 */
typedef struct {
      scsw_t scsw;             /* subchannel status word */
      esw_t  esw;              /* extended status word */
      char   ecw[32];          /* extended control word */
   } irb_t __attribute__ ((aligned(4)));

/*
 * TPI info structure
 */
typedef struct {
      unsigned int res : 16;   /* reserved 0x00000001 */
      unsigned int irq : 16;   /* aka. subchannel number */
      unsigned int intparm;    /* interruption parameter */
   } tpi_info_t;


/*
 * This is the "IRQ descriptor", which contains various information
 * about the irq, including what kind of hardware handling it has,
 * whether it is disabled etc etc.
 *
 * Pad this out to 32 bytes for cache and indexing reasons.
 */
typedef struct {
      unsigned int              status;    /* IRQ status - IRQ_INPROGRESS, IRQ_DISABLED */
      struct hw_interrupt_type *handler;   /* handle/enable/disable functions */
      struct irqaction         *action;    /* IRQ action list */
      unsigned int              unused[3];
      spinlock_t                irq_lock;
   } irq_desc_t;

//
// command information word  (CIW) layout
//
typedef struct _ciw {
   unsigned int et       :  2; // entry type
   unsigned int reserved :  2; // reserved
   unsigned int ct       :  4; // command type
   unsigned int cmd      :  8; // command
   unsigned int count    : 16; // count
   } ciw_t;

#define CIW_TYPE_RCD    0x0    // read configuration data
#define CIW_TYPE_SII    0x1    // set interface identifier
#define CIW_TYPE_RNI    0x2    // read node identifier

//
// sense-id response buffer layout
//
typedef struct {
  /* common part */
      unsigned char  reserved;     /* always 0x'FF' */
      unsigned short cu_type;      /* control unit type */
      unsigned char  cu_model;     /* control unit model */
      unsigned short dev_type;     /* device type */
      unsigned char  dev_model;    /* device model */
      unsigned char  unused;       /* padding byte */
  /* extended part */
      ciw_t    ciw[62];            /* variable # of CIWs */
   }  __attribute__ ((packed,aligned(4))) senseid_t;

/*
 * sense data
 */
typedef struct {
      unsigned char res[32];   /* reserved   */
      unsigned char data[32];  /* sense data */
   } sense_t;

/*
 * device status area, to be provided by the device driver
 *  when calling request_irq() as parameter "dev_id", later
 *  tied to the "action" control block.
 *
 * Note : No data area must be added after union ii or the
 *         effective devstat size calculation will fail !
 */
typedef struct {
     unsigned int  devno;    /* device number, aka. "cuu" from irb */
     unsigned int  intparm;  /* interrupt parameter */
     unsigned char cstat;    /* channel status - accumulated */
     unsigned char dstat;    /* device status - accumulated */
     unsigned char lpum;     /* last path used mask from irb */
     unsigned char unused;   /* not used - reserved */
     unsigned int  flag;     /* flag : see below */
     unsigned long cpa;      /* CCW address from irb at primary status */
     unsigned int  rescnt;   /* res. count from irb at primary status */
     unsigned int  scnt;     /* sense count, if DEVSTAT_FLAG_SENSE_AVAIL */
     union {
        irb_t   irb;         /* interruption response block */
        sense_t sense;       /* sense information */
        } ii;                /* interrupt information */
  } devstat_t;

#define DEVSTAT_FLAG_SENSE_AVAIL   0x00000001
#define DEVSTAT_NOT_OPER           0x00000002
#define DEVSTAT_START_FUNCTION     0x00000004
#define DEVSTAT_HALT_FUNCTION      0x00000008
#define DEVSTAT_STATUS_PENDING     0x00000010
#define DEVSTAT_DEVICE_OWNED       0x00000080
#define DEVSTAT_FINAL_STATUS       0x80000000

/*
 * Flags used as input parameters for do_IO()
 */
#define DOIO_EARLY_NOTIFICATION 0x01    /* allow for I/O completion ... */
                                        /* ... notification after ... */
                                        /* ... primary interrupt status */
#define DOIO_RETURN_CHAN_END       DOIO_EARLY_NOTIFICATION
#define DOIO_VALID_LPM          0x02    /* LPM input parameter is valid */
#define DOIO_WAIT_FOR_INTERRUPT 0x04    /* wait synchronously for interrupt */
#define DOIO_REPORT_ALL         0x08    /* report all interrupt conditions */
#define DOIO_ALLOW_SUSPEND      0x10    /* allow for channel prog. suspend */
#define DOIO_DENY_PREFETCH      0x20    /* don't allow for CCW prefetch */
#define DOIO_SUPPRESS_INTER     0x40    /* suppress intermediate inter. */
                                        /* ... for suspended CCWs */

/*
 * do_IO()
 *
 * Start a S/390 channel program. When the interrupt arrives
 *  handle_IRQ_event() is called, which eventually calls the
 *  IRQ handler, either immediately, delayed (dev-end missing,
 *  or sense required) or never (no IRQ handler registered -
 *  should never occur, as the IRQ (subchannel ID) should be
 *  disabled if no handler is present. Depending on the action
 *  taken, do_IO() returns :  0      - Success
 *                           -EIO    - Status pending
 *                                        see : action->dev_id->cstat
 *                                              action->dev_id->dstat
 *                           -EBUSY  - Device busy
 *                           -ENODEV - Device not operational
 */
int do_IO( int            irq,          /* IRQ aka. subchannel number */
           ccw1_t        *cpa,          /* logical channel program address */
           unsigned long  initparm,     /* interruption parameter */
           unsigned char  lpm,          /* logical path mask */
           unsigned long  flag);        /* flags : see above */

int start_IO( int            irq,       /* IRQ aka. subchannel number */
              ccw1_t        *cpa,       /* logical channel program address */
              unsigned long  intparm,   /* interruption parameter */
              unsigned char  lpm,       /* logical path mask */
              unsigned long  flag);     /* flags : see above */

int resume_IO( int irq);                /* IRQ aka. subchannel number */

int halt_IO( int           irq,      /* IRQ aka. subchannel number */
             unsigned long intparm,  /* dummy intparm */
             unsigned int  flag);    /* possible DOIO_WAIT_FOR_INTERRUPT */


int process_IRQ( struct pt_regs regs,
                 unsigned int   irq,
                 unsigned int   intparm);


int enable_cpu_sync_isc ( int irq );
int disable_cpu_sync_isc( int irq );

typedef struct {
     int          irq;                  /* irq, aka. subchannel */
     unsigned int devno;                /* device number */
     unsigned int status;               /* device status */
     senseid_t    sid_data;             /* senseID data */
     } dev_info_t;

int get_dev_info( int irq, dev_info_t *);   /* to be eliminated - don't use */

int get_dev_info_by_irq  ( int irq, dev_info_t *pdi);
int get_dev_info_by_devno( unsigned int devno, dev_info_t *pdi);

int          get_irq_by_devno( unsigned int devno );
unsigned int get_devno_by_irq( int irq );

int get_irq_first( void );
int get_irq_next ( int irq );

int read_dev_chars( int irq, void **buffer, int length );
int read_conf_data( int irq, void **buffer, int *length );

extern int handle_IRQ_event(unsigned int, int cpu, struct pt_regs *);

extern int set_cons_dev(int irq);
extern int reset_cons_dev(int irq);
extern int wait_cons_dev(int irq);

/*
 * Some S390 specific IO instructions as inline
 */

extern __inline__ int stsch(int irq, volatile schib_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "STSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int msch(int irq, volatile schib_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "MSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int msch_err(int irq, volatile schib_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "    lr   1,%1\n"
                "    msch 0(%2)\n"
                "0:  ipm  %0\n"
                "    srl  %0,28\n"
                "1:\n"
                ".section .fixup,\"ax\"\n"
                "2:  l    %0,%3\n"
                "    bras 1,3f\n"
                "    .long 1b\n"
                "3:  l    1,0(1)\n"
                "    br   1\n"
                ".previous\n"
                ".section __ex_table,\"a\"\n"
                "   .align 4\n"
                "   .long 0b,2b\n"
                ".previous"
                : "=d" (ccode)
                : "r" (irq | 0x10000L), "a" (addr), "i" (__LC_PGM_ILC)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int tsch(int irq, volatile irb_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "TSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int tpi( volatile tpi_info_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "TPI 0(%1)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int ssch(int irq, volatile orb_t *addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "SSCH 0(%2)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L), "a" (addr)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int rsch(int irq)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "RSCH\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int csch(int irq)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "CSCH\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int hsch(int irq)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                "HSCH\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "r" (irq | 0x10000L)
                : "cc", "1" );
        return ccode;
}

extern __inline__ int iac( void)
{
        int ccode;

        __asm__ __volatile__(
                "IAC 1\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : : "cc", "1" );
        return ccode;
}

typedef struct {
     unsigned int vrdcdvno : 16;   /* device number (input) */
     unsigned int vrdclen  : 16;   /* data block length (input) */
     unsigned int vrdcvcla : 8;    /* virtual device class (output) */
     unsigned int vrdcvtyp : 8;    /* virtual device type (output) */
     unsigned int vrdcvsta : 8;    /* virtual device status (output) */
     unsigned int vrdcvfla : 8;    /* virtual device flags (output) */
     unsigned int vrdcrccl : 8;    /* real device class (output) */
     unsigned int vrdccrty : 8;    /* real device type (output) */
     unsigned int vrdccrmd : 8;    /* real device model (output) */
     unsigned int vrdccrft : 8;    /* real device feature (output) */
     } __attribute__ ((packed,aligned(4))) diag210_t;

void VM_virtual_device_info( unsigned int  devno, /* device number */
                             senseid_t    *ps );  /* ptr to senseID data */

extern __inline__ int diag210( diag210_t * addr)
{
        int ccode;

        __asm__ __volatile__(
                "LR 1,%1\n\t"
                ".long 0x83110210\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "a" (addr)
                : "cc", "1" );
        return ccode;
}

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

void mask_irq(unsigned int irq);
void unmask_irq(unsigned int irq);

#define MAX_IRQ_SOURCES 128

extern spinlock_t irq_controller_lock;

#ifdef __SMP__

#include <asm/atomic.h>

static inline void irq_enter(int cpu, unsigned int irq)
{
        hardirq_enter(cpu);
        while (test_bit(0,&global_irq_lock)) {
                eieio();
        }
}

static inline void irq_exit(int cpu, unsigned int irq)
{
        hardirq_exit(cpu);
        release_irqlock(cpu);
}


#else

#define irq_enter(cpu, irq)     (++local_irq_count[cpu])
#define irq_exit(cpu, irq)      (--local_irq_count[cpu])

#endif

#define __STR(x) #x
#define STR(x) __STR(x)

#ifdef __SMP__

/*
 *      SMP has a few special interrupts for IPI messages
 */

#endif /* __SMP__ */

/*
 * x86 profiling function, SMP safe. We might want to do this in
 * assembly totally?
 */
extern unsigned long _stext;
static inline void s390_do_profile (unsigned long addr)
{
        if (prof_buffer && current->pid) {
                addr &= 0x7fffffff;
                addr -= (unsigned long) &_stext;
                addr >>= prof_shift;
                /*
                 * Don't ignore out-of-bounds EIP values silently,
                 * put them into the last histogram slot, so if
                 * present, they will show up as a sharp peak.
                 */
                if (addr > prof_len-1)
                        addr = prof_len-1;
                atomic_inc((atomic_t *)&prof_buffer[addr]);
        }
}

#include <asm/s390io.h>

#define s390irq_spin_lock(irq) \
        spin_lock(&(ioinfo[irq]->irq_desc.irq_lock))

#define s390irq_spin_unlock(irq) \
        spin_unlock(&(ioinfo[irq]->irq_desc.irq_lock))

#define s390irq_spin_lock_irqsave(irq,flags) \
        spin_lock_irqsave(&(ioinfo[irq]->irq_desc.irq_lock), flags)
#define s390irq_spin_unlock_irqrestore(irq,flags) \
        spin_unlock_irqrestore(&(ioinfo[irq]->irq_desc.irq_lock), flags)
#endif

