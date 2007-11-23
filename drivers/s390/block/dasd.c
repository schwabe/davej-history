/* 
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 */

#include <linux/stddef.h>
#include <linux/kernel.h>

#include <linux/tqueue.h>
#include <linux/timer.h>
#include <linux/malloc.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <asm/io.h>
#include <asm/spinlock.h>
#include <asm/semaphore.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>

#include <asm/irq.h>
#include <asm/s390_ext.h>

#include <linux/dasd.h>
#include <linux/blk.h>

#include "dasd_erp.h"
#include "dasd_types.h"
#include "dasd_ccwstuff.h"

#include "../../../arch/s390/kernel/debug.h"

#define PRINTK_HEADER DASD_NAME":"

#define DASD_SSCH_RETRIES 2

/* This macro is a little tricky, but makes the code more easy to read... */
#define MATCH(info,ct,cm,dt,dm) ( \
(( info -> sid_data.cu_type  ct ) && ( info -> sid_data.cu_model cm )) && \
(( info -> sid_data.dev_type dt ) && ( info -> sid_data.dev_model dm )) )

#ifdef MODULE
#include <linux/module.h>

char *dasd[DASD_MAX_DEVICES] =
{NULL,};
#ifdef CONFIG_DASD_MDSK
char *dasd_force_mdsk[DASD_MAX_DEVICES] =
{NULL,};
#endif

kdev_t ROOT_DEV;

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR ("Holger Smolinski <Holger.Smolinski@de.ibm.com>");
MODULE_DESCRIPTION ("Linux on S/390 DASD device driver, Copyright 2000 IBM Corporation");
MODULE_PARM (dasd, "1-" __MODULE_STRING (DASD_MAX_DEVICES) "s");
#ifdef CONFIG_DASD_MDSK
MODULE_PARM (dasd_force_mdsk, "1-" __MODULE_STRING (DASD_MAX_DEVICES) "s");
#endif
#endif

/* Prototypes for the functions called from external */
void dasd_partn_detect (int di);
int devindex_from_devno (int devno);
int dasd_is_accessible (int devno);
void dasd_add_devno_to_ranges (int devno);

#ifdef CONFIG_DASD_MDSK
extern int dasd_force_mdsk_flag[DASD_MAX_DEVICES];
extern void do_dasd_mdsk_interrupt (struct pt_regs *regs, __u16 code);
extern int dasd_parse_module_params (void);
extern void (**ext_mdisk_int) (void);
#endif

void dasd_debug (unsigned long tag);
void dasd_profile_add (cqr_t *cqr);
void dasd_proc_init (void);

static int dasd_format (int dev, format_data_t * fdata);

static struct file_operations dasd_device_operations;

spinlock_t dasd_lock;		/* general purpose lock for the dasd driver */

/* All asynchronous I/O should waint on this wait_queue */
struct wait_queue *dasd_waitq = NULL;

extern dasd_chanq_t *cq_head;
extern int dasd_probeonly;

debug_info_t *dasd_debug_info;

extern dasd_information_t **dasd_information;

dasd_information_t *dasd_info[DASD_MAX_DEVICES] =
{NULL,};
static struct hd_struct dd_hdstruct[DASD_MAX_DEVICES << PARTN_BITS];
static int dasd_blks[256] =
{0,};
static int dasd_secsize[256] =
{0,};
static int dasd_blksize[256] =
{0,};
static int dasd_maxsecs[256] =
{0,};

void
dasd_geninit (struct gendisk *dd)
{
}

struct gendisk dd_gendisk =
{
	major:MAJOR_NR,		/* Major number */
	major_name:"dasd",	/* Major name */
	minor_shift:PARTN_BITS,	/* Bits to shift to get real from partn */
	max_p:1 << PARTN_BITS,	/* Number of partitions per real */
	max_nr:0,		/* number */
	init:dasd_geninit,
	part:dd_hdstruct,	/* hd struct */
	sizes:dasd_blks,	/* sizes in blocks */
	nr_real:0,
	real_devices:NULL,	/* internal */
	next:NULL		/* next */
};

static atomic_t bh_scheduled = ATOMIC_INIT (0);

void
dasd_schedule_bh (void (*func) (void))
{
	static struct tq_struct dasd_tq =
	{0,};
	/* Protect against rescheduling, when already running */
	if (atomic_compare_and_swap (0, 1, &bh_scheduled))
		return;
	dasd_tq.routine = (void *) (void *) func;
	queue_task (&dasd_tq, &tq_immediate);
	mark_bh (IMMEDIATE_BH);
		return;
	}

void
sleep_done (struct semaphore *sem)
{
	if (sem != NULL) {
		up (sem);
	}
}

void
sleep (int timeout)
{
	struct semaphore sem = MUTEX_LOCKED;
	struct timer_list timer;

	init_timer (&timer);
	timer.data = (unsigned long) &sem;
	timer.expires = jiffies + timeout;
	timer.function = (void (*)(unsigned long)) sleep_done;
	printk (KERN_DEBUG PRINTK_HEADER
		"Sleeping for timer tics %d\n", timeout);
	add_timer (&timer);
	down (&sem);
	del_timer (&timer);
}

#ifdef CONFIG_DASD_ECKD
extern dasd_operations_t dasd_eckd_operations;
#endif				/* CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
extern dasd_operations_t dasd_fba_operations;
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_MDSK
extern dasd_operations_t dasd_mdsk_operations;
#endif				/* CONFIG_DASD_MDSK */

dasd_operations_t *dasd_disciplines[] =
{
#ifdef CONFIG_DASD_ECKD
	&dasd_eckd_operations,
#endif				/* CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
	&dasd_fba_operations,
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_MDSK
	&dasd_mdsk_operations,
#endif				/* CONFIG_DASD_MDSK */
#ifdef CONFIG_DASD_CKD
	&dasd_ckd_operations,
#endif				/* CONFIG_DASD_CKD */
	NULL
};

char *dasd_name[] =
{
#ifdef CONFIG_DASD_ECKD
	"ECKD",
#endif				/* CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
	"FBA",
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_MDSK
	"MDSK",
#endif				/* CONFIG_DASD_MDSK */
#ifdef CONFIG_DASD_CKD
	"CKD",
#endif				/* CONFIG_DASD_CKD */
	"END"
};

static int
do_dasd_ioctl (struct inode *inp, /* unsigned */ int no, unsigned long data)
{
	int rc;
	int di;
	dasd_information_t *dev;

	di = DEVICE_NR (inp->i_rdev);
	if (!dasd_info[di]) {
		PRINT_WARN ("No device registered as %d\n", inp->i_rdev);
		return -EINVAL;
	}
	if ((_IOC_DIR (no) != _IOC_NONE) && (data == 0)) {
		PRINT_DEBUG ("empty data ptr");
		return -EINVAL;
	}
	dev = dasd_info[di];
	if (!dev) {
		PRINT_WARN ("No device registered as %d\n", inp->i_rdev);
		return -EINVAL;
	}
	PRINT_INFO ("ioctl 0x%08x %s'0x%x'%d(%d) on dev %d/%d (%d) with data %8lx\n", no,
		    _IOC_DIR (no) == _IOC_NONE ? "0" :
		    _IOC_DIR (no) == _IOC_READ ? "r" :
		    _IOC_DIR (no) == _IOC_WRITE ? "w" :
		    _IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ? "rw" : "u",
		    _IOC_TYPE (no), _IOC_NR (no), _IOC_SIZE (no),
		    MAJOR (inp->i_rdev), MINOR (inp->i_rdev), di, data);

	switch (no) {
	case BLKGETSIZE:{	/* Return device size */
			int blocks = dasd_blks[MINOR (inp->i_rdev)] << 1;
			rc = copy_to_user ((long *) data,
					   &blocks,
					   sizeof (long));
			break;
		}
	case BLKFLSBUF:{
			rc = fsync_dev (inp->i_rdev);
			break;
		}
	case BLKRAGET:{
			rc = copy_to_user ((long *) data,
					read_ahead + MAJOR_NR, sizeof (long));
			break;
		}
	case BLKRASET:{
			rc = copy_from_user (read_ahead + MAJOR_NR,
					     (long *) data, sizeof (long));
			break;
		}
	case BLKRRPART:{
			dasd_partn_detect (di);
			rc = 0;
			break;
		}
	case BIODASDRLB:{
			rc = copy_to_user ((int *) data,
					   &dasd_info[di]->sizes.label_block,
					   sizeof (int));
			break;
		}
	case BLKGETBSZ:{
			rc = copy_to_user ((int *) data,
					   &dasd_info[di]->sizes.bp_block,
					   sizeof (int));
			break;
		}
	case HDIO_GETGEO:{
			struct hd_geometry geo;
			dasd_disciplines[dev->type]->fill_geometry (di, &geo);
			rc = copy_to_user ((struct hd_geometry *) data, &geo,
					   sizeof (struct hd_geometry));
			break;
		}
		RO_IOCTLS (inp->i_rdev, data);
	case BIODASDRSID:{
			rc = copy_to_user ((void *) data,
					   &(dev->info.sid_data),
					   sizeof (senseid_t));
			break;
		}
	case BIODASDRWTB:{
			int offset = 0;
			int xlt;
			rc = copy_from_user (&xlt, (void *) data,
					     sizeof (int));
#if 0
                        PRINT_INFO("Xlating %d to",xlt);
#endif
			if (rc)
				break;
			offset = dd_gendisk.part[MINOR (inp->i_rdev)].start_sect >>
			    dev->sizes.s2b_shift;
			xlt += offset;
#if 0
                        printk(" %d \n",xlt);
#endif
			rc = copy_to_user ((void *) data, &xlt,
					   sizeof (int));
			break;
		}
	case BIODASDFORMAT:{
			/* fdata == NULL is a valid arg to dasd_format ! */
			format_data_t *fdata = NULL;
			PRINT_WARN ("called format ioctl\n");
			if (data) {
				fdata = kmalloc (sizeof (format_data_t),
						 GFP_ATOMIC);
				if (!fdata) {
					rc = -ENOMEM;
					break;
				}
				rc = copy_from_user (fdata, (void *) data,
						     sizeof (format_data_t));
				if (rc)
					break;
			}
			rc = dasd_format (inp->i_rdev, fdata);
			if (fdata) {
				kfree (fdata);
			}
			break;
		}
	default:
		PRINT_WARN ("unknown ioctl number %08x %08lx\n", no, BIODASDFORMAT);
		rc = -EINVAL;
		break;
	}
	return rc;
}

static void
dasd_end_request (struct request *req, int uptodate)
{
	struct buffer_head *bh;
	FUNCTION_ENTRY ("dasd_end_request");
#if DASD_PARANOIA > 2
	if (!req) {
		INTERNAL_CHECK ("end_request called with zero arg%s\n", "");
	}
#endif				/* DASD_PARANOIA */
	while ((bh = req->bh) != NULL) {
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_end_io (bh, uptodate);
	}
	if (!end_that_request_first (req, uptodate, DEVICE_NAME)) {
#ifndef DEVICE_NO_RANDOM
		add_blkdev_randomness (MAJOR (req->rq_dev));
#endif
		DEVICE_OFF (req->rq_dev);
		end_that_request_last (req);
	}
	FUNCTION_EXIT ("dasd_end_request");
	return;
}

void
dasd_wakeup (void)
{
	wake_up (&dasd_waitq);
}


int
dasd_watch_volume (int di)
{
        int rc = 0;

        return rc;
}

void 
dasd_watcher (void) 
{
        int i = 0;
        int rc; 
        do {
                for ( i = 0; i < DASD_MAX_DEVICES; i++ ) {
                        if ( dasd_info [i] ) {
                                rc = dasd_watch_volume ( i );
                        }
                }
                interruptible_sleep_on(&dasd_waitq);
        } while(1);
}

int
dasd_unregister_dasd (int di)
{
	int rc = 0;
	int minor;
	int i;

	minor = di << PARTN_BITS;
	if (!dasd_info[di]) {	/* devindex is not free */
		INTERNAL_CHECK ("trying to free unallocated device %d\n", di);
		return -ENODEV;
	}
	/* delete all that partition stuff */
	for (i = 0; i < (1 << PARTN_BITS); i++) {
		dasd_blks[minor] = 0;
		dasd_secsize[minor + i] = 0;
		dasd_blksize[minor + i] = 0;
		dasd_maxsecs[minor + i] = 0;
	}
	/* reset DASD to unknown statuss */
	atomic_set (&dasd_info[di]->status, DASD_INFO_STATUS_UNKNOWN);

	free_irq (dasd_info[di]->info.irq, &(dasd_info[di]->dev_status));
	if (dasd_info[di]->rdc_data)
		kfree (dasd_info[di]->rdc_data);
	kfree (dasd_info[di]);
	PRINT_INFO ("%04d deleted from list of valid DASDs\n",
		    dasd_info[di]->info.devno);
	return rc;
}

/* Below you find the functions already cleaned up */
static dasd_type_t
check_type (dev_info_t * info)
{
	dasd_type_t type = dasd_none;
	int di;

	FUNCTION_ENTRY ("check_type");
	di = devindex_from_devno (info->devno);

#ifdef CONFIG_DASD_MDSK
	if (MACHINE_IS_VM && dasd_force_mdsk_flag[di] == 1) {
		type = dasd_mdsk;
	} else
#endif				/* CONFIG_DASD_MDSK */

#ifdef CONFIG_DASD_ECKD
	if (MATCH (info, == 0x3990, ||1, == 0x3390, ||1) ||
	    MATCH (info, == 0x9343, ||1, == 0x9345, ||1) ||
	    MATCH (info, == 0x3990, ||1, == 0x3380, ||1)) {
		type = dasd_eckd;
	} else
#endif				/* CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
	if (MATCH (info, == 0x6310, ||1, == 0x9336, ||1)) {
		type = dasd_fba;
	} else
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_MDSK
	if (MACHINE_IS_VM) {
		type = dasd_mdsk;
	} else
#endif				/* CONFIG_DASD_MDSK */
	{
		type = dasd_none;
	}

	FUNCTION_EXIT ("check_type");
	return type;
}

static int
dasd_read_characteristics (dasd_information_t * info)
{
	int rc = 0;
	int ct = 0;
	dev_info_t *di;
	dasd_type_t dt;

	FUNCTION_ENTRY ("read_characteristics");
	if (info == NULL) {
		return -ENODEV;
	}
	di = &(info->info);
	if (di == NULL) {
		return -ENODEV;
	}
	dt = check_type (di);
	/* Some cross-checks, if the cu supports RDC */
	if (MATCH (di, == 0x2835, ||1, ||1, ||1) ||
	    MATCH (di, == 0x3830, ||1, ||1, ||1) ||
	    MATCH (di, == 0x3830, ||1, ||1, ||1) ||
	    MATCH (di, == 0x3990, <=0x03, == 0x3380, <=0x0d)) {
		PRINT_WARN ("Device %d (%x/%x at %x/%x) supports no RDC\n",
			    info->info.irq,
			    di->sid_data.dev_type,
			    di->sid_data.dev_model,
			    di->sid_data.cu_type,
			    di->sid_data.cu_model);
		return -EINVAL;
	}
	switch (dt) {
#ifdef CONFIG_DASD_ECKD
	case dasd_eckd:
		ct = 64;
		rc = read_dev_chars (info->info.irq,
				     (void *) &(info->rdc_data), ct);
		break;
#endif				/*  CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
	case dasd_fba:
		ct = 32;
		rc = read_dev_chars (info->info.irq,
				     (void *) &(info->rdc_data), ct);
		break;
#endif				/*  CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_MDSK
	case dasd_mdsk:
		ct = 0;
		break;
#endif				/*  CONFIG_DASD_FBA */
	default:
		INTERNAL_ERROR ("don't know dasd type %d\n", dt);
	}
	if (rc) {
		PRINT_WARN ("RDC resulted in rc=%d\n", rc);
	}
	FUNCTION_EXIT ("read_characteristics");
	return rc;
}

/* How many sectors must be in a request to dequeue it ? */
#define QUEUE_BLOCKS 25
#define QUEUE_SECTORS (QUEUE_BLOCKS << dasd_info[di]->sizes.s2b_shift)

/* How often to retry an I/O before raising an error */
#define DASD_MAX_RETRIES 5

static inline
 cqr_t *
dasd_cqr_from_req (struct request *req)
{
	cqr_t *cqr = NULL;
	int di;
	dasd_information_t *info;

	if (!req) {
		PRINT_ERR ("No request passed!");
		return NULL;
	}
	di = DEVICE_NR (req->rq_dev);
	info = dasd_info[di];
	if (!info)
		return NULL;
	/* if applicable relocate block */
	if (MINOR (req->rq_dev) & ((1 << PARTN_BITS) - 1)) {
		req->sector +=
		    dd_gendisk.part[MINOR (req->rq_dev)].start_sect;
	}
	/* Now check for consistency */
	if (!req->nr_sectors) {
		PRINT_WARN ("req: %p dev: %08x sector: %ld nr_sectors: %ld bh: %p\n",
		     req, req->rq_dev, req->sector, req->nr_sectors, req->bh);
		return NULL;
	}
	if (((req->sector + req->nr_sectors) >> 1) > info->sizes.kbytes) {
		printk (KERN_ERR PRINTK_HEADER
			"Requesting I/O past end of device %d\n",
			di);
		return NULL;
	}
	cqr = dasd_disciplines[info->type]->get_req_ccw (di, req);
	if (!cqr) {
		PRINT_WARN ("empty CQR generated\n");
	} else {
		cqr->req = req;
		cqr->int4cqr = cqr;
		cqr->devindex = di;
#ifdef DASD_PROFILE
		asm volatile ("STCK %0":"=m" (cqr->buildclk));
#endif				/* DASD_PROFILE */
		ACS (cqr->status, CQR_STATUS_EMPTY, CQR_STATUS_FILLED);
	}
	return cqr;
}

int
dasd_start_IO (cqr_t * cqr)
{
	int rc = 0;
	int retries = DASD_SSCH_RETRIES;
	int di, irq;

	dasd_debug ((unsigned long) cqr);	/* cqr */

	if (!cqr) {
		PRINT_WARN ("(start_IO) no cqr passed\n");
		return -EINVAL;
	}
#ifdef CONFIG_DASD_MDSK
	if (cqr->magic == MDSK_MAGIC) {
		return dasd_mdsk_start_IO (cqr);
	}
#endif				/* CONFIG_DASD_MDSK */
	if (cqr->magic != DASD_MAGIC && cqr->magic != ERP_MAGIC) {
		PRINT_ERR ("(start_IO) magic number mismatch\n");
		return -EINVAL;
	}
	ACS (cqr->status, CQR_STATUS_QUEUED, CQR_STATUS_IN_IO);
        di = cqr->devindex;
	irq = dasd_info[di]->info.irq;
	do {
		asm volatile ("STCK %0":"=m" (cqr->startclk));
		rc = do_IO (irq, cqr->cpaddr, (long) cqr, 0x00, cqr->options);
		switch (rc) {
		case 0:
			if (!(cqr->options & DOIO_WAIT_FOR_INTERRUPT))
                        	atomic_set_mask (DASD_CHANQ_BUSY,
                                	         &dasd_info[di]->queue.flags);
			break;
		case -ENODEV:
			PRINT_WARN ("cqr %p: 0x%04x error, %d retries left\n",
				    cqr, dasd_info[di]->info.devno, retries);
			break;
		case -EIO:
			PRINT_WARN ("cqr %p: 0x%04x I/O, %d retries left\n",
				    cqr, dasd_info[di]->info.devno, retries);
			break;
		case -EBUSY:	/* set up timer, try later */

			PRINT_WARN ("cqr %p: 0x%04x busy, %d retries left\n",
				    cqr, dasd_info[di]->info.devno, retries);
			break;
		default:

			PRINT_WARN ("cqr %p: 0x%04x %d, %d retries left\n",
				    cqr, rc, dasd_info[di]->info.devno,
                                    retries);
			break;
		}
	} while (rc && --retries);
	if (rc) {
		ACS (cqr->status, CQR_STATUS_IN_IO, CQR_STATUS_ERROR);
        }
	return rc;
}

static inline
void
dasd_end_cqr (cqr_t * cqr, int uptodate)
{
	struct request *req = cqr->req;
	asm volatile ("STCK %0":"=m" (cqr->endclk));
#ifdef DASD_PROFILE
	dasd_profile_add (cqr);
#endif				/* DASD_PROFILE */
	dasd_chanq_deq (&dasd_info[cqr->devindex]->queue, cqr);
        if (req) {
                dasd_end_request (req, uptodate);
        }
}

void
dasd_dump_sense (devstat_t * stat)
{
	int sl, sct;
	if (!stat->flag | DEVSTAT_FLAG_SENSE_AVAIL) {
		PRINT_INFO ("I/O status w/o sense data\n");
	} else {
	printk (KERN_INFO PRINTK_HEADER
			"-------------------I/O result:-----------\n");
	for (sl = 0; sl < 4; sl++) {
		printk (KERN_INFO PRINTK_HEADER "Sense:");
		for (sct = 0; sct < 8; sct++) {
			printk (" %2d:0x%02X", 8 * sl + sct,
				stat->ii.sense.data[8 * sl + sct]);
		}
		printk ("\n");
	}
}
}

int
register_dasd_last (int di)
{
	int rc = 0;
	int minor;
	int i;

	rc = dasd_disciplines[dasd_info[di]->type]->fill_sizes_last (di);
	if (!rc) {
		ACS (dasd_info[di]->status,
		     DASD_INFO_STATUS_DETECTED, DASD_INFO_STATUS_FORMATTED);
	} else {		/* -EMEDIUMTYPE: */
		ACS (dasd_info[di]->status,
		     DASD_INFO_STATUS_DETECTED, DASD_INFO_STATUS_ANALYSED);
	}
	PRINT_INFO ("%04X (dasd%c):%ld kB <- block: %d on sector %d B\n",
		    dasd_info[di]->info.devno,
		    'a' + di,
		    dasd_info[di]->sizes.kbytes,
		    dasd_info[di]->sizes.bp_block,
		    dasd_info[di]->sizes.bp_sector);
	minor = di << PARTN_BITS;
	dasd_blks[minor] = dasd_info[di]->sizes.kbytes;
	for (i = 0; i < (1 << PARTN_BITS); i++) {
		dasd_secsize[minor + i] = dasd_info[di]->sizes.bp_sector;
		dasd_blksize[minor + i] = dasd_info[di]->sizes.bp_block;
		dasd_maxsecs[minor + i] = 252 << dasd_info[di]->sizes.s2b_shift;
	}
	return rc;
}

void
dasd_partn_detect (int di)
{
        int minor = di << PARTN_BITS;
	while (atomic_read (&dasd_info[di]->status) !=
	       DASD_INFO_STATUS_FORMATTED) {
                interruptible_sleep_on(&dasd_info[di]->wait_q);
	}
	dd_gendisk.part[minor].nr_sects = dasd_info[di]->sizes.kbytes << 1;
	resetup_one_dev (&dd_gendisk, di);
}

void
dasd_do_chanq (void)
{
	dasd_chanq_t *qp = NULL;
	cqr_t *cqr, *next;
        long flags;
        int irq;
        int tasks;

	atomic_set (&bh_scheduled, 0);
	dasd_debug (0xc4c40000);	/* DD */
	for (qp = cq_head; qp != NULL;) {
/* Get first request */
		dasd_debug ((unsigned long) qp);
		cqr = (cqr_t *) (qp->head);
/* empty queue -> dequeue and proceed */
		if (!cqr) {
			dasd_chanq_t *nqp = qp->next_q;
			cql_deq (qp);
			qp = nqp;
			continue;
		}
/* process all requests on that queue */
		do {
                        next = NULL;
			dasd_debug ((unsigned long) cqr);	/* cqr */
			if (cqr->magic != DASD_MAGIC &&
			    cqr->magic != MDSK_MAGIC &&
			    cqr->magic != ERP_MAGIC) {
				dasd_debug (0xc4c46ff2);	/* DD?2 */
				panic ( PRINTK_HEADER "do_cq:"
					"magic mismatch %p -> %x\n", 
					cqr, cqr -> magic);
                                break;
			}
                        irq = dasd_info[cqr->devindex]->info.irq;
                        s390irq_spin_lock_irqsave (irq, flags);
			switch (atomic_read (&cqr->status)) {
			case CQR_STATUS_IN_IO:
                                dasd_debug (0xc4c4c9d6);	/* DDIO */
                                break;
			case CQR_STATUS_QUEUED:
                                dasd_debug (0xc4c4e2e3);	/* DDST */
				if (dasd_start_IO (cqr) != 0) {
                                  PRINT_WARN("start_io failed\n");
                                }
                                break;
			case CQR_STATUS_ERROR:{
                                dasd_debug (0xc4c4c5d9);	/* DDER */
                                if ( ++ cqr->retries  < 2 ) {
                                        atomic_set (&cqr->status,
                                                    CQR_STATUS_QUEUED);
                                        dasd_debug (0xc4c4e2e3);
                                        if (dasd_start_IO (cqr) == 0) {
                                                atomic_dec (&qp->
                                                            dirty_requests);
                                                break;
                                        }
                                }
                                ACS (cqr->status,
                                     CQR_STATUS_ERROR,
                                     CQR_STATUS_FAILED);
                                break;
                        }
			case CQR_STATUS_ERP_PEND:{
					/* This case is entered, when an interrupt
					   ended with a condittion */
					dasd_erp_action_t erp_action;
					erp_t *erp = NULL;

                                        if ( cqr -> magic != ERP_MAGIC ) {
                                                erp = request_er ();
                                                if (erp == NULL) {
                                                        PRINT_WARN ("No memory for ERP%s\n", "");
                                                        break;
                                                }
                                                memset (erp, 0, sizeof (erp_t));
                                                erp->cqr.magic = ERP_MAGIC;
                                                erp->cqr.int4cqr = cqr;
                                                erp->cqr.devindex= cqr->devindex;
                                                erp_action = dasd_erp_action (cqr);
                                                if (erp_action) {
                                                        PRINT_WARN ("Taking ERP action %p\n", erp_action);
                                                        erp_action (erp);
                                                }
                                                dasd_chanq_enq_head(qp, (cqr_t *) erp);
                                                next = (cqr_t *) erp;
                                } else {
                                                PRINT_WARN("ERP_ACTION failed\n");
                                                ACS (cqr->status,
                                                     CQR_STATUS_ERP_PEND,
                                                    CQR_STATUS_FAILED);
                                }
                                break;
                        }
			case CQR_STATUS_ERP_ACTIVE:
				break;
			case CQR_STATUS_DONE:{
                                next = cqr->next;
                                if (cqr->magic == DASD_MAGIC) {
                                        dasd_debug (0xc4c49692);
                                } else if (cqr->magic == ERP_MAGIC) {
                                        dasd_erp_action_t erp_postaction;
                                        erp_t *erp = (erp_t *) cqr;
                                        erp_postaction =
                                                dasd_erp_postaction (erp);
                                        if (erp_postaction)
                                                erp_postaction (erp);
                                        atomic_dec (&qp->dirty_requests);
                                } else if (cqr->magic == MDSK_MAGIC) {
                                } else {
                                        PRINT_WARN ("unknown magic%s\n", "");
                                }
                                dasd_end_cqr (cqr, 1);
                                break;
                        }
			case CQR_STATUS_FAILED: {
                                next = cqr->next;
                                if (cqr->magic == DASD_MAGIC) {
                                        dasd_debug (0xc4c49692);
                                } else if (cqr->magic == ERP_MAGIC) {
                                        dasd_erp_action_t erp_postaction;
                                        erp_t *erp = (erp_t *) cqr;
                                        erp_postaction =
                                                dasd_erp_postaction (erp);
                                        if (erp_postaction)
                                                erp_postaction (erp);
                                } else if (cqr->magic == MDSK_MAGIC) {
                                } else {
                                        PRINT_WARN ("unknown magic%s\n", "");
                                }
                                dasd_end_cqr (cqr, 0);
                                atomic_dec (&qp->dirty_requests);
                                break;
                        }
			default:
                                PRINT_WARN ("unknown cqrstatus\n");
			}
                        s390irq_spin_unlock_irqrestore (irq, flags);
		} while ((cqr = next) != NULL);
		qp = qp->next_q;
	}
	spin_lock (&io_request_lock);
	do_dasd_request ();
	spin_unlock (&io_request_lock);
	dasd_debug (0xc4c46d6d);	/* DD__ */
}

/* 
   The request_fn is called from ll_rw_blk for any new request.
   We use it to feed the chanqs.
   This implementation assumes we are serialized by the io_request_lock.
 */

#define QUEUE_THRESHOLD 5

void
do_dasd_request (void)
{
	struct request *req, *next, *prev;
	cqr_t *cqr;
	dasd_chanq_t *q;
	long flags;
	int di, irq;
	int broken, busy;
        
	dasd_debug (0xc4d90000);	/* DR */
	dasd_debug ((unsigned long) __builtin_return_address (0));
	prev = NULL;
	for (req = CURRENT; req != NULL; req = next) {
		next = req->next;
		di = DEVICE_NR (req->rq_dev);
		dasd_debug ((unsigned long) req);	/* req */
		dasd_debug (0xc4d90000 +	/* DR## */
                            ((((di/16)<9?(di/16)+0xf0:(di/16)+0xc1))<<8) +
                            (((di%16)<9?(di%16)+0xf0:(di%16)+0xc1)));
                irq = dasd_info[di]->info.irq;
                s390irq_spin_lock_irqsave (irq, flags);
                q = &dasd_info[di]->queue;
		busy = atomic_read (&q->flags) & DASD_CHANQ_BUSY;
		broken = atomic_read (&q->flags) & DASD_REQUEST_Q_BROKEN;
		if (!busy ||
		    (!broken &&
		     (req->nr_sectors >= QUEUE_SECTORS))) {
                        if (prev) {
                                prev->next = next;
                        } else {
                                CURRENT = next;
                        }
			req->next = NULL;
			if (req == &blk_dev[MAJOR_NR].plug) {
					dasd_debug (0xc4d99787); /* DRpg */
				goto cont;
			}
			cqr = dasd_cqr_from_req (req);
			if (!cqr) {
					dasd_debug (0xc4d96ff1); /* DR?1 */
				dasd_end_request (req, 0);
				goto cont;
                        }
                        dasd_debug ((unsigned long) cqr);	/* cqr */
                        dasd_chanq_enq (q, cqr);
			if (!(atomic_read (&q->flags) & DASD_CHANQ_ACTIVE)) {
                                cql_enq_head (q);
                        }
			if (!busy) {
				atomic_clear_mask (DASD_REQUEST_Q_BROKEN,
						   &q->flags);
				if ( atomic_read (&q->dirty_requests) == 0 ) {
					if (dasd_start_IO (cqr) == 0) {
					} else {
						dasd_schedule_bh (dasd_do_chanq);
                                }
                        }
			}
                } else {
                        dasd_debug (0xc4d9c2d9);	/* DRBR */
			atomic_set_mask (DASD_REQUEST_Q_BROKEN, &q->flags);
			prev = req;
		}
        cont:
                s390irq_spin_unlock_irqrestore (irq, flags);
	}
	dasd_debug (0xc4d96d6d);	/* DR__ */
}

void
dasd_handler (int irq, void *ds, struct pt_regs *regs)
{
	devstat_t *stat = (devstat_t *) ds;
	int ip;
	cqr_t *cqr;
	int done_fast_io = 0;
	dasd_era_t era;
        static int counter = 0; 

	dasd_debug (0xc4c80000);	/* DH */
	if (!stat) {
		PRINT_ERR ("handler called without devstat");
		return;
	}
	ip = stat->intparm;
	dasd_debug (ip);	/* intparm */
	if (!ip) {		/* no intparm: unsolicited interrupt */
		dasd_debug (0xc4c8a489);	/* DHui */
		PRINT_INFO ("%04X caught unsolicited interrupt\n",
			    stat->devno);
		return;
	}
		if (ip & 0x80000001) {
			dasd_debug (0xc4c8a489);	/* DHui */
		PRINT_INFO ("%04X  caught spurious interrupt with parm %08x\n",
			    stat->devno, ip);
			return;
		}
		cqr = (cqr_t *) ip;
	if (cqr->magic == DASD_MAGIC || cqr->magic == ERP_MAGIC) {
		asm volatile ("STCK %0":"=m" (cqr->stopclk));
		if ((stat->cstat == 0x00 &&
		     stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END)) ||
		    ((era = dasd_erp_examine (cqr, stat)) == dasd_era_none)) {
			dasd_debug (0xc4c89692);	/* DHok */
#if 0
                        if ( counter < 20 || cqr -> magic == ERP_MAGIC) {
                                counter ++;
#endif
                                ACS (cqr->status, CQR_STATUS_IN_IO, CQR_STATUS_DONE);
#if 0
                        } else {
                                counter=0;
                                PRINT_WARN ("Faking I/O error\n");
                                ACS (cqr->status, CQR_STATUS_IN_IO, CQR_STATUS_ERP_PEND);
                                atomic_inc (&dasd_info[cqr->devindex]->
                                            queue.dirty_requests);
                        }
#endif
			if (atomic_read (&dasd_info[cqr->devindex]->status) ==
			    DASD_INFO_STATUS_DETECTED) {
				register_dasd_last (cqr->devindex);
                                if ( dasd_info[cqr->devindex]->wait_q ) {
                                        wake_up( &dasd_info[cqr->devindex]->
                                                wait_q);
                                }
			}
			if (cqr->next &&
			    (atomic_read (&cqr->next->status) ==
			     CQR_STATUS_QUEUED)) {
				dasd_debug (0xc4c8e2e3);	/* DHST */
				if (dasd_start_IO (cqr->next) == 0) {
					done_fast_io = 1;
				} else {
			}
		}
		} else {	/* only visited in case of error ! */
		dasd_debug (0xc4c8c5d9);	/* DHER */
                        dasd_dump_sense (stat);
		if (!cqr->dstat)
			cqr->dstat = kmalloc (sizeof (devstat_t),
					      GFP_ATOMIC);
		if (cqr->dstat) {
			memcpy (cqr->dstat, stat, sizeof (devstat_t));
		} else {
				PRINT_ERR ("no memory for dstat\n");
		}
                        atomic_inc (&dasd_info[cqr->devindex]->
                                    queue.dirty_requests);
		/* errorprocessing */
			if (era == dasd_era_fatal) {
				PRINT_WARN ("ERP returned fatal error\n");
				ACS (cqr->status,
				     CQR_STATUS_IN_IO, CQR_STATUS_FAILED);
			} else {
                                ACS (cqr->status,
                                     CQR_STATUS_IN_IO, CQR_STATUS_ERP_PEND);
                        }
	}
	if (done_fast_io == 0)
		atomic_clear_mask (DASD_CHANQ_BUSY,
				   &dasd_info[cqr->devindex]->
				   queue.flags);
        
	if (cqr->flags & DASD_DO_IO_SLEEP) {
		dasd_debug (0xc4c8a6a4);	/* DHwu */
		dasd_wakeup ();
	} else if (! (cqr->options & DOIO_WAIT_FOR_INTERRUPT) ){
                dasd_debug (0xc4c8a293);	/* DHsl */
                        dasd_schedule_bh (dasd_do_chanq);
	} else {
                dasd_debug (0x64686f6f);	/* DH_g */
                dasd_debug (cqr->flags);	/* DH_g */
        }
        } else {
                dasd_debug (0xc4c86ff1);	/* DH?1 */
                PRINT_ERR ("handler:magic mismatch on %p %08x\n",
                           cqr, cqr->magic);
                return;
        }
	dasd_debug (0xc4c86d6d);	/* DHwu */
}

static int
dasd_format (int dev, format_data_t * fdata)
{
	int rc;
	int devindex = DEVICE_NR (dev);
	dasd_chanq_t *q;
	cqr_t *cqr;
	int irq;
	long flags;
	PRINT_INFO ("%04X called format on %x\n",
		    dasd_info[devindex]->info.devno, dev);
	if (MINOR (dev) & (0xff >> (8 - PARTN_BITS))) {
		PRINT_WARN ("Can't format partition! minor %x %x\n",
			    MINOR (dev), 0xff >> (8 - PARTN_BITS));
		return -EINVAL;
	}
	down (&dasd_info[devindex]->sem);
	atomic_set (&dasd_info[devindex]->status,
		    DASD_INFO_STATUS_UNKNOWN);
	if (dasd_info[devindex]->open_count == 1) {
		rc = dasd_disciplines[dasd_info[devindex]->type]->
		    dasd_format (devindex, fdata);
		if (rc) {
			PRINT_WARN ("Formatting failed rc=%d\n", rc);
			up (&dasd_info[devindex]->sem);
			return rc;
		}
	} else {
		PRINT_WARN ("device is open! %d\n", dasd_info[devindex]->open_count);
		up (&dasd_info[devindex]->sem);
		return -EINVAL;
	}
#if DASD_PARANOIA > 1
	if (!dasd_disciplines[dasd_info[devindex]->type]->fill_sizes_first) {
		INTERNAL_CHECK ("No fill_sizes for dt=%d\n", dasd_info[devindex]->type);
	} else
#endif				/* DASD_PARANOIA */
	{
		ACS (dasd_info[devindex]->status,
		     DASD_INFO_STATUS_UNKNOWN, DASD_INFO_STATUS_DETECTED);
		irq = dasd_info[devindex]->info.irq;
		PRINT_INFO ("%04X reacessing, irq %x, index %d\n",
			    get_devno_by_irq (irq), irq, devindex);
		s390irq_spin_lock_irqsave (irq, flags);
		q = &dasd_info[devindex]->queue;
		cqr = dasd_disciplines[dasd_info[devindex]->type]->
		    fill_sizes_first (devindex);
		dasd_chanq_enq (q, cqr);
		if (!(atomic_read (&q->flags) & DASD_CHANQ_ACTIVE)) {
			cql_enq_head (q);
		}
		dasd_schedule_bh (dasd_do_chanq);
		s390irq_spin_unlock_irqrestore (irq, flags);
	}
	up (&dasd_info[devindex]->sem);
	return rc;
}

static int
register_dasd (int irq, dasd_type_t dt, dev_info_t * info)
{
	int rc = 0;
	int di;
	unsigned long flags;
	dasd_chanq_t *q;
	cqr_t *cqr;
	static spinlock_t register_lock = SPIN_LOCK_UNLOCKED;
	spin_lock (&register_lock);
	FUNCTION_ENTRY ("register_dasd");
	di = devindex_from_devno (info->devno);
	if (di < 0) {
		INTERNAL_CHECK ("Can't get index for devno %d\n", info->devno);
		return -ENODEV;
	}
	if (dasd_info[di]) {	/* devindex is not free */
		INTERNAL_CHECK ("reusing allocated deviceindex %d\n", di);
		return -ENODEV;
	}
	dasd_info[di] = (dasd_information_t *)
	    kmalloc (sizeof (dasd_information_t), GFP_ATOMIC);
	if (dasd_info[di] == NULL) {
		PRINT_WARN ("No memory for dasd_info_t on irq %d\n", irq);
		return -ENOMEM;
	}
	memset (dasd_info[di], 0, sizeof (dasd_information_t));
	memcpy (&(dasd_info[di]->info), info, sizeof (dev_info_t));
	spin_lock_init (&dasd_info[di]->queue.f_lock);
	spin_lock_init (&dasd_info[di]->queue.q_lock);
	dasd_info[di]->type = dt;
	dasd_info[di]->irq = irq;
	dasd_info[di]->sem = MUTEX;
#ifdef CONFIG_DASD_MDSK
	if (dt == dasd_mdsk) {
		dasd_info[di]->rdc_data = kmalloc (
				 sizeof (dasd_characteristics_t), GFP_ATOMIC);
		if (!dasd_info[di]->rdc_data) {
			PRINT_WARN ("No memory for char on irq %d\n", irq);
			goto unalloc;
		}
		dasd_info[di]->rdc_data->mdsk.dev_nr = dasd_info[di]->
		    info.devno;
		dasd_info[di]->rdc_data->mdsk.rdc_len =
		    sizeof (dasd_mdsk_characteristics_t);
	} else
#endif				/* CONFIG_DASD_MDSK */
	rc = dasd_read_characteristics (dasd_info[di]);
	if (rc) {
		PRINT_WARN ("RDC returned error %d\n", rc);
		rc = -ENODEV;
		goto unalloc;
	}
#if DASD_PARANOIA > 1
	if (dasd_disciplines[dt]->ck_characteristics)
#endif				/* DASD_PARANOIA */
		rc = dasd_disciplines[dt]->
		    ck_characteristics (dasd_info[di]->rdc_data);

	if (rc) {
		INTERNAL_CHECK ("Discipline returned non-zero when"
				"checking device characteristics%s\n", "");
		rc = -ENODEV;
		goto unalloc;
	}
#ifdef CONFIG_DASD_MDSK
	if (dt == dasd_mdsk) {

	} else
#endif				/* CONFIG_DASD_MDSK */
	rc = request_irq (irq, dasd_handler, 0, "dasd",
			  &(dasd_info[di]->dev_status));
	ACS (dasd_info[di]->status,
	     DASD_INFO_STATUS_UNKNOWN, DASD_INFO_STATUS_DETECTED);
	if (rc) {
#if DASD_PARANOIA > 0
		printk (KERN_WARNING PRINTK_HEADER
			"Cannot register irq %d, rc=%d\n",
			irq, rc);
#endif				/* DASD_PARANOIA */
		rc = -ENODEV;
		goto unalloc;
	}
#if DASD_PARANOIA > 1
	if (!dasd_disciplines[dt]->fill_sizes_first) {
		INTERNAL_CHECK ("No fill_sizes for dt=%d\n", dt);
		goto unregister;
	}
#endif				/* DASD_PARANOIA */
	irq = dasd_info[di]->info.irq;
	PRINT_INFO ("%04X trying to access, irq %x, index %d\n",
		    get_devno_by_irq (irq), irq, di);
	s390irq_spin_lock_irqsave (irq, flags);
	q = &dasd_info[di]->queue;
	cqr = dasd_disciplines[dt]->fill_sizes_first (di);
	dasd_chanq_enq (q, cqr);
	if (!(atomic_read (&q->flags) & DASD_CHANQ_ACTIVE)) {
		cql_enq_head (q);
	}
	if (dasd_start_IO (cqr) != 0) {
		dasd_schedule_bh (dasd_do_chanq);
	}
	s390irq_spin_unlock_irqrestore (irq, flags);

	goto exit;

      unregister:
	free_irq (irq, &(dasd_info[di]->dev_status));
      unalloc:
	kfree (dasd_info[di]);
	dasd_info[di] = NULL;
      exit:
	spin_unlock (&register_lock);
	FUNCTION_EXIT ("register_dasd");
	return rc;
}

static int
probe_for_dasd (int irq)
{
	int rc;
	dev_info_t info;
	dasd_type_t dt;

	rc = get_dev_info_by_irq (irq, &info);

	if (rc == -ENODEV) {	/* end of device list */
		return rc;
	} else if ((info.status & DEVSTAT_DEVICE_OWNED)) {
		return -EBUSY;
	} else if ((info.status & DEVSTAT_NOT_OPER)) {
		return -ENODEV;
	}
#if DASD_PARANOIA > 2
	else {
		INTERNAL_CHECK ("unknown rc %d of get_dev_info", rc);
		return rc;
	}
#endif				/* DASD_PARANOIA */

	dt = check_type (&info);	/* make a first guess */

	if (dt == dasd_none) {
		return -ENODEV;
	}
		if (!dasd_is_accessible (info.devno)) {
			return -ENODEV;
		}
	if (!dasd_disciplines[dt]->ck_devinfo) {
			INTERNAL_ERROR ("no ck_devinfo function%s\n", "");
			return -ENODEV;
		}
	rc = dasd_disciplines[dt]->ck_devinfo (&info);
	if (rc) {
			return rc;
		}
	if (dasd_probeonly) {
		PRINT_INFO ("%04X not enabled due to probeonly mode\n",
			    info.devno);
		dasd_add_devno_to_ranges (info.devno);
			return -ENODEV;
	} else {
		rc = register_dasd (irq, dt, &info);
	}
		if (rc) {
		PRINT_WARN ("%04X not enabled due to errors\n",
			    info.devno);
		} else {
		PRINT_INFO ("%04X is (dasd%c) minor %d (%s)\n",
				    info.devno,
			    'a' + devindex_from_devno (info.devno),
			       devindex_from_devno (info.devno) << PARTN_BITS,
				    dasd_name[dt]);
		}

	return rc;
}

static int
register_major (int major)
{
	int rc = 0;

	FUNCTION_ENTRY ("register_major");
	rc = register_blkdev (major, DASD_NAME, &dasd_device_operations);
#if DASD_PARANOIA > 1
	if (rc) {
		PRINT_WARN ("registering major -> rc=%d aborting... \n", rc);
		return rc;
	}
#endif				/* DASD_PARANOIA */
	blk_dev[major].request_fn = do_dasd_request;
	FUNCTION_CONTROL ("successfully registered major: %d\n", major);
	FUNCTION_EXIT ("register_major");
	return rc;
}

/* 
   Below you find functions which are called from outside. Some of them may be
   static, because they are called by their function pointers only. Thus static
   modifier is to make sure, that they are only called via the kernel's methods
 */

static int
dasd_ioctl (struct inode *inp, struct file *filp,
	    unsigned int no, unsigned long data)
{
	int rc = 0;
	FUNCTION_ENTRY ("dasd_ioctl");
	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	rc = do_dasd_ioctl (inp, no, data);
	FUNCTION_EXIT ("dasd_ioctl");
	return rc;
}

static int
dasd_open (struct inode *inp, struct file *filp)
{
	int rc = 0;
	dasd_information_t *dev;
	FUNCTION_ENTRY ("dasd_open");
	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	dev = dasd_info[DEVICE_NR (inp->i_rdev)];
	if (!dev) {
		PRINT_DEBUG ("No device registered as %d (%d)\n",
			    inp->i_rdev, DEVICE_NR (inp->i_rdev));
		return -EINVAL;
	}
	down (&dev->sem);
	up (&dev->sem);
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif				/* MODULE */
#if DASD_PARANOIA > 2
	if (dev->open_count < 0) {
		INTERNAL_ERROR ("open count cannot be less than 0: %d",
				dev->open_count);
		return -EINVAL;
	}
#endif				/* DASD_PARANOIA */
	dev->open_count++;
	FUNCTION_EXIT ("dasd_open");
	return rc;
}

static int
dasd_release (struct inode *inp, struct file *filp)
{
	int rc = 0;
	dasd_information_t *dev;
	FUNCTION_ENTRY ("dasd_release");
	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	dev = dasd_info[DEVICE_NR (inp->i_rdev)];
	if (!dev) {
		PRINT_WARN ("No device registered as %d\n", inp->i_rdev);
		return -EINVAL;
	}
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif				/* MODULE */
#if DASD_PARANOIA > 2
	if (!dev->open_count) {
		PRINT_WARN ("device %d has not been opened before:\n",
			    inp->i_rdev);
	}
#endif				/* DASD_PARANOIA */
	dev->open_count--;
#if DASD_PARANOIA > 2
	if (dev->open_count < 0) {
		INTERNAL_ERROR ("open count cannot be less than 0: %d",
				dev->open_count);
		return -EINVAL;
	}
#endif				/* DASD_PARANOIA */
	FUNCTION_EXIT ("dasd_release");
	return rc;
}

static struct
file_operations dasd_device_operations =
{
	read:block_read,
	write:block_write,
	fsync:block_fsync,
	ioctl:dasd_ioctl,
	open:dasd_open,
	release:dasd_release,
};

int
dasd_init (void)
{
	int rc = 0;
	int i;

	PRINT_INFO ("initializing...\n");
	atomic_set (&bh_scheduled, 0);
	spin_lock_init (&dasd_lock);
#ifdef CONFIG_DASD_MDSK
	/*
	 * enable service-signal external interruptions,
	 * Control Register 0 bit 22 := 1
	 * (besides PSW bit 7 must be set to 1 somewhere for external
	 * interruptions)
	 */
	ctl_set_bit (0, 9);
	register_external_interrupt (0x2603, do_dasd_mdsk_interrupt);
#endif
	dasd_debug_info = debug_register ("dasd", 1, 4);
	/* First register to the major number */

	rc = register_major (MAJOR_NR);
#if DASD_PARANOIA > 1
	if (rc) {
		PRINT_WARN ("registering major_nr returned rc=%d\n", rc);
		return rc;
	}
#endif	/* DASD_PARANOIA */ read_ahead[MAJOR_NR] = 8;
        blk_size[MAJOR_NR] = dasd_blks;
	hardsect_size[MAJOR_NR] = dasd_secsize;
	blksize_size[MAJOR_NR] = dasd_blksize;
	max_sectors[MAJOR_NR] = dasd_maxsecs;
#ifdef CONFIG_PROC_FS
	dasd_proc_init ();
#endif				/* CONFIG_PROC_FS */
	/* Now scan the device list for DASDs */
	for (i = 0; i < NR_IRQS; i++) {
		int irc;	/* Internal return code */
		irc = probe_for_dasd (i);
		switch (irc) {
		case 0:
			break;
		case -ENODEV:
		case -EBUSY:
			break;
		case -EMEDIUMTYPE:
			PRINT_WARN ("DASD not formatted%s\n", "");
			break;
		default:
			INTERNAL_CHECK ("probe_for_dasd: unknown rc=%d", irc);
			break;
		}
	}
	FUNCTION_CONTROL ("detection loop completed %s partn check...\n", "");
/* Finally do the genhd stuff */
	dd_gendisk.next = gendisk_head;
	gendisk_head = &dd_gendisk;
	dasd_information = dasd_info;	/* to enable genhd to know about DASD */
        tod_wait (1000000);

	/* wait on root filesystem before detecting partitions */
	if (MAJOR (ROOT_DEV) == DASD_MAJOR) {
		int count = 10;
		i = DEVICE_NR (ROOT_DEV);
		if (dasd_info[i] == NULL) {
			panic ("root device not accessible\n");
		}
                while ((atomic_read (&dasd_info[i]->status) !=
                        DASD_INFO_STATUS_FORMATTED) &&
                       count ) {
                        PRINT_INFO ("Waiting on root volume...%d seconds left\n", count);
			tod_wait (1000000);
			count--;
		}
		if (count == 0) {
			panic ("Waiting on root volume...giving up!\n");
		}
	}
	for (i = 0; i < DASD_MAX_DEVICES; i++) {
		if (dasd_info[i]) {
			if (atomic_read (&dasd_info[i]->status) ==
			    DASD_INFO_STATUS_FORMATTED) {
				dasd_partn_detect (i);
			} else {	/* start kernel thread for devices not ready now */
				kernel_thread (dasd_partn_detect, (void *) i,
                                               CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
			}
		}
	}
	return rc;
}

#ifdef MODULE

int
init_module (void)
{
	int rc = 0;

	PRINT_INFO ("Initializing module\n");
	rc = dasd_parse_module_params ();
	if (rc == 0) {
		PRINT_INFO ("module parameters parsed successfully\n");
	} else {
		PRINT_WARN ("parsing parameters returned rc=%d\n", rc);
	}
	rc = dasd_init ();
	if (rc == 0) {
		PRINT_INFO ("module initialized successfully\n");
	} else {
		PRINT_WARN ("initializing module returned rc=%d\n", rc);
	}
	FUNCTION_EXIT ("init_module");
	return rc;
}

void
cleanup_module (void)
{
	int rc = 0;
	struct gendisk *genhd = gendisk_head, *prev = NULL;

	PRINT_INFO ("trying to unload module \n");

	/* unregister gendisk stuff */
	for (genhd = gendisk_head; genhd; prev = genhd, genhd = genhd->next) {
		if (genhd == dd_gendisk) {
			if (prev)
				prev->next = genhd->next;
			else {
				gendisk_head = genhd->next;
			}
			break;
		}
	}
	/* unregister devices */
	for (i = 0; i = DASD_MAX_DEVICES; i++) {
		if (dasd_info[i])
			dasd_unregister_dasd (i);
	}

	if (rc == 0) {
		PRINT_INFO ("module unloaded successfully\n");
	} else {
		PRINT_WARN ("module unloaded with errors\n");
	}
}
#endif				/* MODULE */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
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
