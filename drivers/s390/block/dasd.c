/* 
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                : Utz Bacher <utz.bacher@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999
 */

#include <linux/stddef.h>
#include <linux/kernel.h>

#ifdef MODULE
#include <linux/module.h>
#endif				/* MODULE */

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

#include "../../../arch/s390/kernel/irq.h"

#include "dasd.h"
#include <linux/blk.h>

#include "dasd_types.h"
#include "dasd_ccwstuff.h"

#define PRINTK_HEADER DASD_NAME":"

#define CCW_READ_DEVICE_CHARACTERISTICS  0x64

#define DASD_SSCH_RETRIES 5

/* This macro is a little tricky, but makes the code more easy to read... */
#define MATCH(info,ct,cm,dt,dm) ( \
( info -> sid_data.cu_type  ct ) && ( info -> sid_data.cu_model cm ) && \
( info -> sid_data.dev_type dt ) && ( info -> sid_data.dev_model dm ) )

/* Prototypes for the functions called from external */
static ssize_t dasd_read (struct file *, char *, size_t, loff_t *);
static ssize_t dasd_write (struct file *, const char *, size_t, loff_t *);
static int dasd_ioctl (struct inode *, struct file *, unsigned int, unsigned long);
static int dasd_open (struct inode *, struct file *);
static int dasd_fsync (struct file *, struct dentry *);
static int dasd_release (struct inode *, struct file *);
void dasd_debug (unsigned long tag);
void dasd_profile_add (cqr_t *cqr);
void dasd_proc_init (void);

static struct file_operations dasd_file_operations;

spinlock_t dasd_lock;		/* general purpose lock for the dasd driver */

/* All asynchronous I/O should waint on this wait_queue */
struct wait_queue *dasd_waitq = NULL;

static int dasd_autodetect = 1;
static int dasd_devno[DASD_MAX_DEVICES] =
{0,};
static int dasd_count = 0;

extern dasd_chanq_t *cq_head;

static int
dasd_get_hexdigit (char c)
{
	if ((c >= '0') && (c <= '9'))
		return c - '0';
	if ((c >= 'a') && (c <= 'f'))
		return c + 10 - 'a';
	if ((c >= 'A') && (c <= 'F'))
		return c + 10 - 'A';
	return -1;
}

/* sets the string pointer after the next comma */
static void
dasd_scan_for_next_comma (char **strptr)
{
	while (((**strptr) != ',') && ((**strptr)++))
		(*strptr)++;

	/* set the position AFTER the comma */
	if (**strptr == ',')
		(*strptr)++;
}

/*sets the string pointer after the next comma, if a parse error occured */
static int
dasd_get_next_int (char **strptr)
{
	int j, i = -1;		/* for cosmetic reasons first -1, then 0 */
	if (isxdigit (**strptr)) {
		for (i = 0; isxdigit (**strptr);) {
			i <<= 4;
			j = dasd_get_hexdigit (**strptr);
			if (j == -1) {
				PRINT_ERR ("no integer: skipping range.\n");
				dasd_scan_for_next_comma (strptr);
				i = -1;
				break;
			}
			i += j;
			(*strptr)++;
			if (i > 0xffff) {
				PRINT_ERR (" value too big, skipping range.\n");
				dasd_scan_for_next_comma (strptr);
				i = -1;
				break;
			}
		}
	}
	return i;
}

static inline int
devindex_from_devno (int devno)
{
	int i;
	for (i = 0; i < dasd_count; i++) {
		if (dasd_devno[i] == devno)
			return i;
	}
	if (dasd_autodetect) {
		if (dasd_count < DASD_MAX_DEVICES) {
			dasd_devno[dasd_count] = devno;
			return dasd_count++;
		}
		return -EOVERFLOW;
	}
	return -ENODEV;
}

/* returns 1, if dasd_no is in the specified ranges, otherwise 0 */
static inline int
dasd_is_accessible (int devno)
{
	return (devindex_from_devno (devno) >= 0);
}

/* dasd_insert_range skips ranges, if the start or the end is -1 */
static void
dasd_insert_range (int start, int end)
{
	int curr;
	FUNCTION_ENTRY ("dasd_insert_range");
	if (dasd_count >= DASD_MAX_DEVICES) {
		PRINT_ERR (" too many devices specified, ignoring some.\n");
		FUNCTION_EXIT ("dasd_insert_range");
		return;
	}
	if ((start == -1) || (end == -1)) {
		PRINT_ERR ("invalid format of parameter, skipping range\n");
		FUNCTION_EXIT ("dasd_insert_range");
		return;
	}
	if (end < start) {
		PRINT_ERR (" ignoring range from %x to %x - start value " \
			   "must be less than end value.\n", start, end);
		FUNCTION_EXIT ("dasd_insert_range");
		return;
	}
/* concurrent execution would be critical, but will not occur here */
	for (curr = start; curr <= end; curr++) {
		if (dasd_is_accessible (curr)) {
			PRINT_WARN (" %x is already in list as device %d\n",
				    curr, devindex_from_devno (curr));
		}
		dasd_devno[dasd_count] = curr;
		dasd_count++;
		if (dasd_count >= DASD_MAX_DEVICES) {
			PRINT_ERR (" too many devices specified, ignoring some.\n");
			break;
		}
	}
	PRINT_INFO (" added dasd range from %x to %x.\n",
		    start, dasd_devno[dasd_count - 1]);

	FUNCTION_EXIT ("dasd_insert_range");
}

void
dasd_setup (char *str, int *ints)
{
	int devno, devno2;

	FUNCTION_ENTRY ("dasd_setup");
	dasd_autodetect = 0;
	while (*str && *str != 1) {
		if (!isxdigit (*str)) {
			str++;	/* to avoid looping on two commas */
			PRINT_ERR (" kernel parameter in invalid format.\n");
			continue;
		}
		devno = dasd_get_next_int (&str);

		/* range was skipped? -> scan for comma has been done */
		if (devno == -1)
			continue;

		if (*str == ',') {
			str++;
			dasd_insert_range (devno, devno);
			continue;
		}
		if (*str == '-') {
			str++;
			devno2 = dasd_get_next_int (&str);
			if (devno2 == -1) {
				PRINT_ERR (" invalid character in " \
					   "kernel parameters.");
			} else {
				dasd_insert_range (devno, devno2);
			}
			dasd_scan_for_next_comma (&str);
			continue;
		}
		if (*str == 0) {
			dasd_insert_range (devno, devno);
			break;
		}
		PRINT_ERR (" unexpected character in kernel parameter, " \
			   "skipping range.\n");
	}
	FUNCTION_EXIT ("dasd_setup");
}

static void
dd_geninit (struct gendisk *ignored)
{
	FUNCTION_ENTRY ("dd_geninit");
	FUNCTION_EXIT ("dd_geninit");
}

static struct hd_struct dd_hdstruct[DASD_MAX_DEVICES << PARTN_BITS];
static int dd_blocksizes[DASD_MAX_DEVICES << PARTN_BITS];

struct gendisk dd_gendisk =
{
	MAJOR_NR,		/* Major number */
	"dd",			/* Major name */
	PARTN_BITS,		/* Bits to shift to get real from partn */
	1 << PARTN_BITS,	/* Number of partitions per real */
	DASD_MAX_DEVICES,	/* maximum number of real */
	dd_geninit,		/* init function */
	dd_hdstruct,		/* hd struct */
	dd_blocksizes,		/* block sizes */
	0,			/* number */
	NULL,			/* internal */
	NULL			/* next */

};

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

dasd_operations_t *dasd_disciplines[] =
{
#ifdef CONFIG_DASD_ECKD
	&dasd_eckd_operations
#endif				/* CONFIG_DASD_ECKD */
};

char *dasd_name[] =
{
#ifdef CONFIG_DASD_ECKD
	"ECKD"
#endif				/* CONFIG_DASD_ECKD */
};

dasd_information_t *dasd_info[DASD_MAX_DEVICES] =
{NULL,};

static int dasd_blks[256] =
{0,};
static int dasd_secsize[256] =
{0,};
static int dasd_blksize[256] =
{0,};
static int dasd_maxsecs[256] =
{0,};

void
fill_sizes (int di)
{
	int rc;
	int minor;
	rc = dasd_disciplines[dasd_info[di]->type]->fill_sizes (di);
	switch (rc) {
	case -EMEDIUMTYPE:
		dasd_info[di]->flags |= DASD_NOT_FORMATTED;
		break;
	}
	PRINT_INFO ("%ld kB <- 'soft'-block: %d, hardsect %d Bytes\n",
		    dasd_info[di]->sizes.kbytes,
		    dasd_info[di]->sizes.bp_block,
		    dasd_info[di]->sizes.bp_sector);
	switch (dasd_info[di]->type) {
#ifdef CONFIG_DASD_ECKD
	case dasd_eckd:
		dasd_info[di]->sizes.first_sector =
		    3 << dasd_info[di]->sizes.s2b_shift;
		break;
#endif				/* CONFIG_DASD_ECKD */
	default:
		INTERNAL_CHECK ("Unknown dasd type %d\n", dasd_info[di]->type);
	}
	minor = di << PARTN_BITS;
	dasd_blks[minor] = dasd_info[di]->sizes.kbytes;
	dasd_secsize[minor] = dasd_info[di]->sizes.bp_sector;
	dasd_blksize[minor] = dasd_info[di]->sizes.bp_block;
	dasd_maxsecs[minor] = 
                ((PAGE_SIZE/sizeof(ccw1_t))-4)<<dasd_info[di]->sizes.s2b_shift;
	dasd_blks[minor + 1] = dasd_info[di]->sizes.kbytes -
	    (dasd_info[di]->sizes.first_sector >> 1);
	dasd_secsize[minor + 1] = dasd_info[di]->sizes.bp_sector;
	dasd_blksize[minor + 1] = dasd_info[di]->sizes.bp_block;
	dasd_maxsecs[minor+1] = 
                ((PAGE_SIZE/sizeof(ccw1_t))-4)<<dasd_info[di]->sizes.s2b_shift;
}

int
dasd_format (int dev, format_data_t * fdata)
{
	int rc;
	int devindex = DEVICE_NR (dev);
	PRINT_INFO ("Format called with devno %x\n", dev);
	if (MINOR (dev) & (0xff >> (8 - PARTN_BITS))) {
		PRINT_WARN ("Can't format partition! minor %x %x\n",
			    MINOR (dev), 0xff >> (8 - PARTN_BITS));
		return -EINVAL;
	}
	down (&dasd_info[devindex]->sem);
	if (dasd_info[devindex]->open_count == 1) {
		rc = dasd_disciplines[dasd_info[devindex]->type]->
		    dasd_format (devindex, fdata);
		if (rc) {
			PRINT_WARN ("Formatting failed rc=%d\n", rc);
		}
	} else {
		PRINT_WARN ("device is open! %d\n", dasd_info[devindex]->open_count);
		rc = -EINVAL;
	}
	if (!rc) {
		fill_sizes (devindex);
		dasd_info[devindex]->flags &= ~DASD_NOT_FORMATTED;
	} else {
		dasd_info[devindex]->flags |= DASD_NOT_FORMATTED;
	}
	up (&dasd_info[devindex]->sem);
	return rc;
}

static inline int
do_dasd_ioctl (struct inode *inp, unsigned int no, unsigned long data)
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
			unsigned long blocks;
			if (inp->i_rdev & 0x01) {
				blocks = (dev->sizes.blocks - 3) <<
				    dev->sizes.s2b_shift;
			} else {
				blocks = dev->sizes.kbytes << dev->sizes.s2b_shift;
			}
			rc = copy_to_user ((long *) data, &blocks, sizeof (long));
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
			INTERNAL_CHECK ("BLKRPART not implemented%s", "");
			rc = -EINVAL;
			break;
		}
	case HDIO_GETGEO:{
			INTERNAL_CHECK ("HDIO_GETGEO not implemented%s", "");
			rc = -EINVAL;
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
                        PRINT_INFO("Xlating %d to",xlt);
			if (rc)
				break;
			if (MINOR (inp->i_rdev) & 1)
				offset = 3;
			xlt += offset;
                        printk(" %d \n",xlt);
			rc = copy_to_user ((void *) data, &xlt,
					   sizeof (int));
			break;
		}
	case BIODASDFORMAT:{
			/* fdata == NULL is a valid arg to dasd_format ! */
			format_data_t *fdata = NULL;
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
dasd_unregister_dasd (int irq, dasd_type_t dt, dev_info_t * info)
{
	int rc = 0;
	FUNCTION_ENTRY ("dasd_unregister_dasd");
	INTERNAL_CHECK ("dasd_unregister_dasd not implemented%s\n", "");
	FUNCTION_EXIT ("dasd_unregister_dasd");
	return rc;
}

/* Below you find the functions already cleaned up */
static dasd_type_t
check_type (dev_info_t * info)
{
	dasd_type_t type = dasd_none;

	FUNCTION_ENTRY ("check_type");
#ifdef CONFIG_DASD_ECKD
	if (MATCH (info, == 0x3990, ||1, == 0x3390, ||1) ||
	    MATCH (info, == 0x3990, ||1, == 0x3380, ||1)) {
		type = dasd_eckd;
	} else
#endif				/* CONFIG_DASD_ECKD */
	{
		type = dasd_none;
	}
	FUNCTION_EXIT ("check_type");
	return type;
}

static int
dasd_read_characteristics (dasd_information_t * info)
{
	int rc;
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
		break;
#endif				/*  CONFIG_DASD_ECKD */
	default:
		INTERNAL_ERROR ("don't know dasd type %d\n", dt);
	}
	rc = read_dev_chars (info->info.irq,
			     (void *) &(info->rdc_data), ct);
	if (rc) {
		PRINT_WARN ("RDC resulted in rc=%d\n", rc);
	}
	FUNCTION_EXIT ("read_characteristics");
	return rc;
}

/* How many sectors must be in a request to dequeue it ? */
#define QUEUE_BLOCKS 100
#define QUEUE_SECTORS (QUEUE_BLOCKS << dasd_info[di]->sizes.s2b_shift)

/* How often to retry an I/O before raising an error */
#define DASD_MAX_RETRIES 5

static atomic_t bh_scheduled = ATOMIC_INIT (0);

static inline void
schedule_bh (void (*func) (void))
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
	if (MINOR (req->rq_dev) & 0x1) {
		req->sector += info->sizes.first_sector;
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
		if (atomic_compare_and_swap (CQR_STATUS_EMPTY,
					     CQR_STATUS_FILLED,
					     &cqr->status)) {
			PRINT_WARN ("cqr from req stat changed %d\n",
				    atomic_read (&cqr->status));
		}
	}
	return cqr;
}

int
dasd_start_IO (cqr_t * cqr)
{
	int rc = 0;
	int retries = DASD_SSCH_RETRIES;
	int di, irq;

	if (!cqr) {
		PRINT_WARN ("(start_IO) no cqr passed\n");
		return -EINVAL;
	}
	if (cqr->magic != DASD_MAGIC) {
		PRINT_WARN ("(start_IO) magic number mismatch\n");
		return -EINVAL;
	}
	if (atomic_compare_and_swap (CQR_STATUS_QUEUED,
				     CQR_STATUS_IN_IO,
				     &cqr->status)) {
		PRINT_WARN ("start_IO: status changed %d\n",
			    atomic_read (&cqr->status));
		atomic_set (&cqr->status, CQR_STATUS_ERROR);
		return -EINVAL;
	}
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
		if (atomic_compare_and_swap (CQR_STATUS_IN_IO,
					     CQR_STATUS_ERROR,
					     &cqr->status)) {
			PRINT_WARN ("start_IO:(done) status changed %d\n",
				    atomic_read (&cqr->status));
		        atomic_set (&cqr->status, CQR_STATUS_ERROR);
		}
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
	printk (KERN_INFO PRINTK_HEADER
		"-------------------I/O Error-----------------------------\n");
	for (sl = 0; sl < 4; sl++) {
		printk (KERN_INFO PRINTK_HEADER "Sense:");
		for (sct = 0; sct < 8; sct++) {
			printk (" %2d:0x%02X", 8 * sl + sct,
				stat->ii.sense.data[8 * sl + sct]);
		}
		printk ("\n");
	}
}

void
dasd_do_chanq (void)
{
	dasd_chanq_t *qp = NULL;
	cqr_t *cqr;
        long flags;
        int irq,di;
        int tasks;
	atomic_set (&bh_scheduled, 0);
	dasd_debug (0x44445f69);	/* DD_i */
	while ((tasks = atomic_read(&chanq_tasks)) != 0) {
/* initialization and wraparound */
		if (qp == NULL) {
			dasd_debug (0x44445f68);        /* DD_h */
			qp = cq_head;
			if (!qp) {
                          dasd_debug (0x44445f45);        /* DD_E */
                          dasd_debug (tasks);      
                          PRINT_ERR("Mismatch of NULL queue pointer and "
                                    "still %d chanq_tasks to do!!\n"
                                    "Please send output of /proc/dasd/debug "
                                    "to Linux390@de.ibm.com\n", tasks);
                          atomic_set(&chanq_tasks,0);
                          break;
			}
		}
/* Get the irq number: Ouch, FIXME!!!!! */
#if 0
                di =(((unsigned long)qp)-
                     ((unsigned long)&(dasd_info[0]->queue)))
                        / sizeof(dasd_information_t);
                dasd_debug(di);
                irq = dasd_info[di]->info.irq;
                dasd_debug(irq);
#endif
/* Get first request */
		cqr = (cqr_t *) (qp->head);
		dasd_debug ((unsigned long) cqr);	/* cqr */
/* empty queue -> dequeue and proceed */
		if (!cqr) {
			dasd_chanq_t *nqp = qp->next_q;
			cql_deq (qp);
			qp = nqp;
			continue;
		}
/* process all requests on that queue */
		do {
			cqr_t *next;
			dasd_debug (0x44445f64);	/* DD_d */
			dasd_debug ((unsigned long) cqr);	/* cqr */
			if (cqr->magic != DASD_MAGIC) {
				dasd_debug (0x44445f65);	/* DD_e */
				PRINT_WARN ("do_cq:magic mismatch %p\n", cqr);
                                break;
			}
                        irq = dasd_info[cqr->devindex]->info.irq;
                        s390irq_spin_lock_irqsave (irq, flags);
			switch (atomic_read (&cqr->status)) {
			case CQR_STATUS_IN_IO:
                                dasd_debug (0x44445f48);	/* DD_I */
                                cqr = NULL;
                                break;
			case CQR_STATUS_QUEUED:
                                dasd_debug (0x44445f53);	/* DD_S */
                                if (dasd_start_IO (cqr) == 0) {
                                        atomic_dec (&chanq_tasks);
                                        cqr = NULL;
                                }
                                break;
			case CQR_STATUS_ERROR:
                                dasd_debug (0x44445f54);	/* DD_E */
                                dasd_dump_sense (cqr->dstat);
                                if (cqr->retries++ < 5) {
                                        atomic_set (&cqr->status,
                                                    CQR_STATUS_QUEUED);
                                        if (dasd_start_IO (cqr) == 0) {
                                                atomic_dec (&chanq_tasks);
                                                cqr = NULL;
                                        }
                                } else {
                                        atomic_set (&cqr->status,
                                                    CQR_STATUS_FAILED);
                                }
                                break;
			case CQR_STATUS_DONE:
                                next = cqr->next;
                                dasd_debug (0x44445f44);	/* DD_D */
                                dasd_end_cqr (cqr, 1);
                                atomic_dec (&chanq_tasks);
                                cqr = next;
                                break;
			case CQR_STATUS_FAILED:
                                next = cqr->next;
                                dasd_debug (0x44445f45);	/* DD_F */
                                dasd_end_cqr (cqr, 0);
                                atomic_dec (&chanq_tasks);
                                cqr = next;
                                break;
			default:
                                PRINT_WARN ("unknown cqrstatus\n");
                                cqr = NULL;
			}
                        s390irq_spin_unlock_irqrestore (irq, flags);
		} while (cqr);
		qp = qp->next_q;
	}
	spin_lock (&io_request_lock);
	do_dasd_request ();
	spin_unlock (&io_request_lock);
	dasd_debug (0x44445f6f);	/* DD_o */
}

/* 
   The request_fn is called from ll_rw_blk for any new request.
   We use it to feed the chanqs.
   This implementation assumes we are serialized by the io_request_lock.
 */
void
do_dasd_request (void)
{
	struct request *req;
	struct request *prev;
	struct request *next;
	char broken[DASD_MAX_DEVICES] =	{0,};
	char busy[DASD_MAX_DEVICES] = {0,};
	int di;
	cqr_t *cqr;
	long caller;
	dasd_chanq_t *q;
	long flags;
        int irq;
        
	__asm__ ("lr %0,14":"=d" (caller)); 
	dasd_debug (0x44525f69);	/* DR_i */
	dasd_debug ((unsigned long) caller);	/* calleraddres */
	prev = NULL;
	for (req = CURRENT; req != NULL; req = next) {
		next = req->next;
		di = DEVICE_NR (req->rq_dev);
		dasd_debug ((unsigned long) req);	/* req */
		dasd_debug (0x44520000 +	/* DR## */
                            ((((di/16)<9?(di/16)+'0':(di/16)+'a'))<<8) +
                            (((di%16)<9?(di%16)+'0':(di%16)+'a')));
                irq = dasd_info[di]->info.irq;
                s390irq_spin_lock_irqsave (irq, flags);
                q = &dasd_info[di]->queue;
                busy[di] = busy[di]||(atomic_read(&q->flags)&DASD_CHANQ_BUSY);
                if ( !busy[di] ||
                     ((!broken[di]) && (req->nr_sectors >= QUEUE_SECTORS))) {
                        if (prev) {
                                prev->next = next;
                        } else {
                                CURRENT = next;
                        }
			req->next = NULL;
			if (req == &blk_dev[MAJOR_NR].plug) {
				dasd_debug (0x44525f75);	/* DR_u */
				goto cont;
			}
			cqr = dasd_cqr_from_req (req);
			if (!cqr) {
				dasd_debug (0x44525f65);	/* DR_e */
				dasd_end_request (req, 0);
				goto cont;
                        }
                        dasd_debug (0x44525f71);	/* DR_q */
                        dasd_debug ((unsigned long) cqr);	/* cqr */
                        dasd_chanq_enq (q, cqr);
                        if (!(atomic_read (&q->flags) & DASD_CHANQ_ACTIVE)) {
                                cql_enq_head (q);
                        }
                        if (!busy[di]) {
                                if (dasd_start_IO (cqr) == 0)
                                        dasd_debug (0x44525f73); /* DR_s */
                                else {
                                        atomic_inc (&chanq_tasks);
					schedule_bh (dasd_do_chanq);
                                        busy[di] = 1;
                                }
                        }
                } else {
                        dasd_debug (0x44525f62);	/* DR_b */
                        broken[di] = 1;
			prev = req;
		}
        cont:
                s390irq_spin_unlock_irqrestore (irq, flags);
	}
	dasd_debug (0x44525f6f);	/* DR_o */
}

void
dasd_handler (int irq, void *ds, struct pt_regs *regs)
{
	devstat_t *stat = (devstat_t *) ds;
	int ip;
	cqr_t *cqr;
	int done_fast_io = 0;

	dasd_debug (0x44485f69);	/* DH_i */
	if (!stat)
		PRINT_ERR ("handler called without devstat");
	ip = stat->intparm;
	dasd_debug (ip);	/* intparm */
	switch (ip) {		/* filter special intparms... */
	case 0x00000000:	/* no intparm: unsolicited interrupt */
		dasd_debug (0x44485f30);	/* DH_0 */
		PRINT_INFO ("Unsolicited interrupt on device %04X\n",
			    stat->devno);
		dasd_dump_sense (stat);
		return;
	default:
		if (ip & 0x80000001) {
			PRINT_INFO ("Spurious interrupt %08x on device %04X\n",
				    ip, stat->devno);
			return;
		}
		cqr = (cqr_t *) ip;
		if (cqr->magic != DASD_MAGIC) {
			dasd_debug (0x44485f65);	/* DH_e */
			PRINT_WARN ("handler:magic mismatch on %p %08x\n",
				    cqr, cqr->magic);
			return;
		}
		asm volatile ("STCK %0":"=m" (cqr->stopclk));
		if (stat->cstat == 0x00 && stat->dstat == 0x0c) {
			dasd_debug (0x44486f6b);	/* DHok */
			if (atomic_compare_and_swap (CQR_STATUS_IN_IO,
						     CQR_STATUS_DONE,
						     &cqr->status)) {
				PRINT_WARN ("handler: cqrstat changed%d\n",
					    atomic_read (&cqr->status));
			}
			if (cqr->next) {
				dasd_debug (0x44485f6e);	/* DH_n */
				if (dasd_start_IO (cqr->next) == 0)
					done_fast_io = 1;
			}
 			break;
		}
		dasd_debug (0x44482121);	/* DH!! */
		if (!cqr->dstat)
			cqr->dstat = kmalloc (sizeof (devstat_t),
					      GFP_ATOMIC);
		if (cqr->dstat) {
			memcpy (cqr->dstat, stat, sizeof (devstat_t));
			dasd_dump_sense (cqr->dstat);
		} else {
			PRINT_ERR ("no memory for dtstat\n");
		}
		/* errorprocessing */
		if (cqr->retries < DASD_MAX_RETRIES) {
			dasd_debug (0x44485f72);	/* DH_r */
			atomic_set (&cqr->status, CQR_STATUS_ERROR);
		} else {
			dasd_debug (0x44485f6e);	/* DH_f */
			atomic_set (&cqr->status, CQR_STATUS_FAILED);
		}

	}
	if (done_fast_io == 0)
		atomic_clear_mask (DASD_CHANQ_BUSY,
				   &dasd_info[cqr->devindex]->
				   queue.flags);
        
	if (cqr->flags & DASD_DO_IO_SLEEP) {
		dasd_debug (0x44485f77);	/* DH_w */
		dasd_wakeup ();
	} else if (! (cqr->options & DOIO_WAIT_FOR_INTERRUPT) ){
                dasd_debug (0x44485f73);	/* DH_s */
                atomic_inc (&chanq_tasks);
                schedule_bh (dasd_do_chanq);
	} else {
                dasd_debug (cqr->flags);	/* DH_g */
                dasd_debug (0x44485f6f);	/* DH_g */
        }
}

static int
register_dasd (int irq, dasd_type_t dt, dev_info_t * info)
{
	int rc = 0;
	int di;
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
	atomic_set (&dasd_info[di]->queue.flags, 0);
	dasd_info[di]->type = dt;
	dasd_info[di]->irq = irq;
	dasd_info[di]->sem = MUTEX;
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
	rc = request_irq (irq, dasd_handler, 0, "dasd",
			  &(dasd_info[di]->dev_status));
	if (rc) {
#if DASD_PARANOIA > 0
		printk (KERN_WARNING PRINTK_HEADER
			"Cannot register irq %d, rc=%d\n",
			irq, rc);
#endif				/* DASD_DEBUG */
		rc = -ENODEV;
		goto unalloc;
	}
#if DASD_PARANOIA > 1
	if (!dasd_disciplines[dt]->fill_sizes) {
		INTERNAL_CHECK ("No fill_sizes for dt=%d\n", dt);
		goto unregister;
	}
#endif				/* DASD_PARANOIA */
	fill_sizes (di);
	goto exit;

      unregister:
	free_irq (irq, &(dasd_info[di]->dev_status));
      unalloc:
	kfree (dasd_info[di]);
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

	FUNCTION_ENTRY ("probe_for_dasd");

	rc = get_dev_info_by_irq (irq, &info);
	if (rc == -ENODEV) {	/* end of device list */
		return rc;
	}
#if DASD_PARANOIA > 2
	if (rc) {
		INTERNAL_CHECK ("unknown rc %d of get_dev_info", rc);
		return rc;
	}
#endif				/* DASD_PARANOIA */
	if ((info.status & DEVSTAT_NOT_OPER)) {
		return -ENODEV;
	}
	dt = check_type (&info);
	switch (dt) {
#ifdef CONFIG_DASD_ECKD
	case dasd_eckd:
#endif				/* CONFIG_DASD_ECKD */
		FUNCTION_CONTROL ("Probing devno %d...\n", info.devno);
		if (!dasd_is_accessible (info.devno)) {
			FUNCTION_CONTROL ("out of range...skip%s\n", "");
			return -ENODEV;
		}
		if (dasd_disciplines[dt]->ck_devinfo) {
			rc = dasd_disciplines[dt]->ck_devinfo (&info);
		}
#if DASD_PARANOIA > 1
		else {
			INTERNAL_ERROR ("no ck_devinfo function%s\n", "");
			return -ENODEV;
		}
#endif				/* DASD_PARANOIA */
		if (rc == -ENODEV) {
			return rc;
		}
#if DASD_PARANOIA > 2
		if (rc) {
			INTERNAL_CHECK ("unknown error rc=%d\n", rc);
			return -ENODEV;
		}
#endif				/* DASD_PARANOIA */
		rc = register_dasd (irq, dt, &info);
		if (rc) {
			PRINT_INFO ("devno %x not enabled as minor %d  due to errors\n",
				    info.devno,
				    devindex_from_devno (info.devno) <<
				    PARTN_BITS);
		} else {
			PRINT_INFO ("devno %x added as minor %d (%s)\n",
				    info.devno,
			       devindex_from_devno (info.devno) << PARTN_BITS,
				    dasd_name[dt]);
		}
	case dasd_none:
		break;
	default:
		PRINT_DEBUG ("unknown device type\n");
		break;
	}
	FUNCTION_EXIT ("probe_for_dasd");
	return rc;
}

static int
register_major (int major)
{
	int rc = 0;

	FUNCTION_ENTRY ("register_major");
	rc = register_blkdev (major, DASD_NAME, &dasd_file_operations);
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

static ssize_t
dasd_read (struct file *filp, char *b, size_t s, loff_t * o)
{
	ssize_t rc;
	FUNCTION_ENTRY ("dasd_read");
	rc = block_read (filp, b, s, o);
	FUNCTION_EXIT ("dasd_read");
	return rc;
}
static ssize_t
dasd_write (struct file *filp, const char *b, size_t s, loff_t * o)
{
	ssize_t rc;
	FUNCTION_ENTRY ("dasd_write");
	rc = block_write (filp, b, s, o);
	FUNCTION_EXIT ("dasd_write");
	return rc;
}
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
dasd_fsync (struct file *filp, struct dentry *d)
{
	int rc = 0;
	FUNCTION_ENTRY ("dasd_fsync");
	if (!filp) {
		return -EINVAL;
	}
	rc = block_fsync (filp, d);
	FUNCTION_EXIT ("dasd_fsync");
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
file_operations dasd_file_operations =
{
	NULL,			/* loff_t(*llseek)(struct file *,loff_t,int); */
	dasd_read,
	dasd_write,
	NULL,			/* int(*readdir)(struct file *,void *,filldir_t); */
	NULL,			/* u int(*poll)(struct file *,struct poll_table_struct *); */
	dasd_ioctl,
	NULL,			/* int (*mmap) (struct file *, struct vm_area_struct *); */
	dasd_open,
	NULL,			/* int (*flush) (struct file *) */
	dasd_release,
	dasd_fsync,
	NULL,			/* int (*fasync) (int, struct file *, int);
				   int (*check_media_change) (kdev_t dev);
				   int (*revalidate) (kdev_t dev);
				   int (*lock) (struct file *, int, struct file_lock *); */
};

int
dasd_init (void)
{
	int rc = 0;
	int i;

	FUNCTION_ENTRY ("dasd_init");
	PRINT_INFO ("initializing...\n");
	atomic_set (&chanq_tasks, 0);
	atomic_set (&bh_scheduled, 0);
	spin_lock_init (&dasd_lock);
	/* First register to the major number */
	rc = register_major (MAJOR_NR);
#if DASD_PARANOIA > 1
	if (rc) {
		PRINT_WARN ("registering major_nr returned rc=%d\n", rc);
		return rc;
	}
#endif	/* DASD_PARANOIA */ 
        read_ahead[MAJOR_NR] = 8;
        blk_size[MAJOR_NR] = dasd_blks;
	hardsect_size[MAJOR_NR] = dasd_secsize;
	blksize_size[MAJOR_NR] = dasd_blksize;
	max_sectors[MAJOR_NR] = dasd_maxsecs;
#ifdef CONFIG_PROC_FS
	dasd_proc_init ();
#endif				/* CONFIG_PROC_FS */
	/* Now scan the device list for DASDs */
	FUNCTION_CONTROL ("entering detection loop%s\n", "");
	for (i = 0; i < NR_IRQS; i++) {
		int irc;	/* Internal return code */
		LOOP_CONTROL ("Probing irq %d...\n", i);
		irc = probe_for_dasd (i);
		switch (irc) {
		case 0:
			LOOP_CONTROL ("Added DASD%s\n", "");
			break;
		case -ENODEV:
			LOOP_CONTROL ("No DASD%s\n", "");
			break;
		case -EMEDIUMTYPE:
			PRINT_WARN ("DASD not formatted%s\n", "");
			break;
		default:
			INTERNAL_CHECK ("probe_for_dasd: unknown rc=%d", irc);
			break;
		}
	}
	FUNCTION_CONTROL ("detection loop completed%s\n", "");
/* Finally do the genhd stuff */
#if 0				/* 2 b done */
	dd_gendisk.next = gendisk_head;
	gendisk_head = &dd_gendisk;
	for (i = 0; i < DASD_MAXDEVICES; i++) {
		LOOP_CONTROL ("Setting partitions of DASD %d\n", i);
		resetup_one_dev (&dd_gendisk, i);
	}
#endif				/* 0 */
	FUNCTION_EXIT ("dasd_init");
	return rc;
}

#ifdef MODULE
int
init_module (void)
{
	int rc = 0;

	FUNCTION_ENTRY ("init_module");
	PRINT_INFO ("trying to load module\n");
	rc = dasd_init ();
	if (rc == 0) {
		PRINT_INFO ("module loaded successfully\n");
	} else {
		PRINT_WARN ("warning: Module load returned rc=%d\n", rc);
	}
	FUNCTION_EXIT ("init_module");
	return rc;
}

void
cleanup_module (void)
{
	int rc = 0;

	FUNCTION_ENTRY ("cleanup_module");
	PRINT_INFO ("trying to unload module \n");

	/* FIXME: replace by proper unload functionality */
	INTERNAL_ERROR ("Modules not yet implemented %s", "");

	if (rc == 0) {
		PRINT_INFO ("module unloaded successfully\n");
	} else {
		PRINT_WARN ("module unloaded with errors\n");
	}
	FUNCTION_EXIT ("cleanup_module");
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
