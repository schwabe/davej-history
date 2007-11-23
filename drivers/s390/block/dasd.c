/* 
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Horst Hummel <Horst.Hummel@de.ibm.com>  
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 * History of changes (starts July 2000)
 * 07/03/00 Adapted code to compile with 2.2 and 2.4 kernels
 * 07/05/00 Added some missing cases when shutting down a device
 * 07/07/00 Fixed memory leak in ccw allocation
            Adapted request function to make it work on 2.4
 * 07/10/00 Added some code to the request function to dequeue requests
            that cannot be handled due to errors
 * 07/10/00 Moved linux/dasd.h and linux/ccwcache.h to asm/
 * 07/10/00 Fixed a bug when formatting a 'new' device       
 * 07/10/00 Removed an annoying message from dasd_format 
 * 07/11/00 Reanimated probeonly mode    
 * 07/11/00 Reanimated autodetection mode
 * 07/12/00 fixed a bug in module cleanup
 * 07/12/00 fixed a bug in dasd_devices_open when having 'unknown' devices
 * 07/13/00 fixed error message when having no device
 * 07/13/00 added code for dynamic device recognition
 * 07/14/00 reorganized the format process for better ERP
 * 07/17/00 fixed a race condition when sleeping on a request
 * 07/17/00 modified default ERP action to use TIC instead of NOP
 * 07/20/00 fixed proc filesystem for 2.4
 * 07/24/00 fixed missing interrupt handler
 * 08/01/00 fixed a race condition when sleeping on a request 
 * 09/15/00 fixed a race condition on dasd_do_chanq
 * 09/15/00 got rid of some paranoia
 * 09/18/00 fixed the state machine for duplicate devnos in dasd ranges
 * 10/26/00 fixed ITPM PL020141RSC load module to a kernel with static driver 
            are the fixes in dasd_init
 * 10/26/00 fixed ITPM PL020062RSC formatting r/o volume 
            are the fixes in dasd_format
 * 10/26/00 fixed ITPM PL010261EPA race condition when formatting  
            are the fixes in dasd_do_chanq
 * 11/21/00 fixed BLKFLSBUF ioctl and dasd_release to flush the buffers
 * 01/17/01 fixed PL020234MVE problem accessing DASD 68-127, 133-191,...
 * 01/23/01 fixed sleep_on_request to terminate when signal_pending
 * 01/25/01 added code for error recovery with PAC'0x1D' = long busy
 * 02/08/01 fixed PL020237RMI
 * 02/08/01 fixed PL020265TDI
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/tqueue.h>
#include <linux/timer.h>
#include <linux/malloc.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
#include <linux/spinlock.h>                      
#else 
#include <asm/spinlock.h>
#endif     

#include <asm/ccwcache.h>
#include <asm/dasd.h>
#include <linux/blk.h>
#include <asm/debug.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
#include <linux/devfs_fs_kernel.h>
#endif /* LINUX_IS_24 */

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif				/* CONFIG_PROC_FS */

#include <asm/atomic.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/s390_ext.h>
#include <asm/s390dyn.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))      
#include <asm/idals.h>
#endif /* LINUX_IS_24 */                               

#include "dasd.h"
#ifdef CONFIG_DASD_ECKD
#include "dasd_eckd.h"
#endif				/*  CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
#include "dasd_fba.h" 
#endif				/*  CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_MDSK
#include "dasd_diag.h" 
#endif				/*  CONFIG_DASD_MDSK */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static struct block_device_operations dasd_device_operations;
#endif /* VERSION_CODE */

MODULE_AUTHOR ("Holger Smolinski <Holger.Smolinski@de.ibm.com>");
MODULE_DESCRIPTION ("Linux on S/390 DASD device driver, Copyright 2000 IBM Corporation");
MODULE_SUPPORTED_DEVICE("dasd");
MODULE_PARM (dasd, "1-" __MODULE_STRING (256)"s");
EXPORT_SYMBOL (dasd_discipline_enq);
EXPORT_SYMBOL (dasd_discipline_deq);
EXPORT_SYMBOL (dasd_start_IO);
EXPORT_SYMBOL (dasd_int_handler);
EXPORT_SYMBOL (dasd_alloc_request);
EXPORT_SYMBOL (dasd_free_request);

/* SECTION: Constant definitions to be used within this file */
#undef PRINTK_HEADER
#define PRINTK_HEADER DASD_NAME":"

#define DASD_QUEUE_LIMIT 10
#define DASD_SSCH_RETRIES 5
#define QUEUE_SECTORS 128

/* SECTION: prototypes for static functions of dasd.c (try to eliminate!) */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static void do_dasd_request (request_queue_t *);
#else
static void do_dasd_request (void);
#endif /* LINUX_IS_24 */
static void dasd_do_chanq (void);
static void schedule_request_fn (void (*func) (void));
static int dasd_set_device_level (unsigned int, int, dasd_discipline_t *, int);

/* SECTION: managing setup of dasd_driver */
typedef struct dasd_range_t {
	unsigned int from;
	unsigned int to;
	char discipline[4];
        struct dasd_range_t *next;
} __attribute__ ((packed)) dasd_range_t;

typedef struct dasd_devreg_t {
        devreg_t devreg;
        struct dasd_devreg_t *next;
} dasd_devreg_t;

static int dasd_probeonly = 1;
static int dasd_autodetect = 1;
 
static dasd_range_t *dasd_range_head = NULL;
static dasd_devreg_t *dasd_devreg_head = NULL;
static spinlock_t dasd_open_count_lock;

static dasd_devreg_t *
dasd_create_devreg ( int devno ) {
        dasd_devreg_t *r = kmalloc ( sizeof(dasd_devreg_t), GFP_KERNEL);
        memset (r,0,sizeof(dasd_devreg_t));
        if ( r != NULL ) {
                r -> devreg.ci.devno = devno;
                r -> devreg.flag = DEVREG_TYPE_DEVNO;
                r -> devreg.oper_func = dasd_oper_handler;
        }
        return r;
}
 
static void
dasd_add_range (int from, int to)
{
        dasd_range_t *temp,*range;
        int i;

        range = (dasd_range_t *)kmalloc(sizeof(dasd_range_t),GFP_KERNEL);
        if ( range == NULL )
                return;
        memset(range,0,sizeof(dasd_range_t));
        range -> from = from;
        if (to == 0) { /* single devno ? */
                range -> to = from;
        }  else {
                range -> to = to;
        }
        
        /* chain current range to end of list */
        if ( dasd_range_head == NULL ) {
                dasd_range_head = range;
        } else {
                for ( temp = dasd_range_head; 
                      temp && temp->next; 
                      temp = temp->next );
                temp->next = range;
        }
        /* allocate and chain devreg infos for the devnos... */
        for ( i = range->from; i <= range->to; i ++ ){
                dasd_devreg_t *reg = dasd_create_devreg(i);
                s390_device_register(&reg->devreg);
                reg->next = dasd_devreg_head;
                dasd_devreg_head = reg;
        }
}

static int
dasd_strtoul (char *str, char **stra)
{
	char *temp = str;
	int val;
	if (*temp == '0') {
		temp++;		/* strip leading zero */
		if (*temp == 'x')
			temp++;	/* strip leading x */
	}
	val = simple_strtoul (temp, &temp, 16);		/* interpret anything as hex */
	*stra = temp;
	return val;
}

char *dasd[256] = {NULL,}; /* maximum of 256 ranges supplied on parmline */

#ifndef MODULE
static void 
dasd_split_parm_string ( char * str ) 
{
        char *tmp=str;
        int count = 0;
        do {
                char * end;
                int len;
                end = strchr(tmp,',');
                if ( end == NULL ) { 
                        len = strlen(tmp) + 1;
                } else {
                        len = (long) end - (long) tmp + 1;
                        *end = '\0';
                        end ++;
                }
                dasd[count] = kmalloc(len * sizeof(char),GFP_ATOMIC);
                if ( dasd == NULL ) {
                        printk (KERN_WARNING PRINTK_HEADER
                                "No memory to store dasd= parameter no %d\n",count+1);
                        break;
                }
                memset( dasd[count], 0, len * sizeof(char));
                memcpy( dasd[count], tmp, len * sizeof(char));
                count ++;
                tmp = end;
        } while ( tmp != NULL && *tmp != '\0' );
}

static char dasd_parm_string[1024] = {0,};

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,2,17))
static int
dasd_setup (char *str) 
{
        static int first_time = 1;
        if ( ! first_time ) {
                *(dasd_parm_string+strlen(dasd_parm_string))=',';
        }  else {
                first_time = 0;
        }
        memcpy(dasd_parm_string+strlen(dasd_parm_string),str,strlen(str)+1);
        return 1;
}
#else
void 
dasd_setup (char *str, int *ints) 
{
        static int first_time = 1;
        if ( ! first_time ) {
                *(dasd_parm_string+strlen(dasd_parm_string))=',';
        }  else {
                first_time = 0;
        }
        memcpy(dasd_parm_string+strlen(dasd_parm_string),str,strlen(str)+1);
}
#endif /* LINUX_IS_24 */


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,2,17))
__setup("dasd=", dasd_setup);
#endif /* LINUX_IS_24 */

#endif

void
dasd_parse (char **str) 
{
	char *temp;
	int from, to;
        
      	if ( *str ) { 
        dasd_probeonly = 0;
	}
        while (*str) {
                temp = *str;
		from = 0;
		to = 0;
                if ( strncmp ( *str,"autodetect",strlen("autodetect"))== 0) {
                        dasd_autodetect = 1;
                        printk (KERN_INFO "turning to autodetection mode\n");
                        break;
                } else if ( strncmp ( *str,"probeonly",strlen("probeonly"))== 0) {
                        dasd_probeonly = 1;
                        printk (KERN_INFO "turning to probeonly mode\n");
                        break;
                } else {
                        dasd_autodetect = 0;
                        from = dasd_strtoul (temp, &temp);          
                        if (*temp == '-') {
                                temp++;
                                to = dasd_strtoul (temp, &temp);
                        }
                        dasd_add_range (from, to);
                }
                str ++;
        }
}

int
devindex_from_devno (int devno)
{
	int devindex = 0;
        dasd_range_t *temp;
        for ( temp = dasd_range_head; temp; temp = temp->next ) {
		if (devno < temp -> from || devno > temp -> to) {
			devindex += temp -> to - temp -> from + 1;
		} else {
			devindex += devno - temp -> from;
			break;
		}
	}
        if ( temp == NULL ) 
                return -ENODEV;
	return devindex;
}

/* SECTION: ALl needed for multiple major numbers */

static major_info_t dasd_major_info[] =
{
	{
		next:NULL /* &dasd_major_info[1] */ ,
		request_fn:do_dasd_request,
		read_ahead:8,
		gendisk:
		{
			major:94,
			major_name:DASD_NAME,
			minor_shift:DASD_PARTN_BITS,
			max_p:1 << DASD_PARTN_BITS,
#if ! (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
			max_nr:DASD_PER_MAJOR,
#endif /* LINUX_IS_24 */
			nr_real:DASD_PER_MAJOR,
		}
	}
#if 0
        ,
	{
		next:NULL,
		request_fn:do_dasd_request,
		read_ahead:8,
		gendisk:
		{
			major:95,
			major_name:DASD_NAME,
			minor_shift:DASD_PARTN_BITS,
			max_p:1 << DASD_PARTN_BITS,
#if ! (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
			max_nr:DASD_PER_MAJOR,
#endif /* LINUX_IS_24 */
			nr_real:DASD_PER_MAJOR,
		}
	}
#endif
};

static dasd_device_t *
find_dasd_device (int devindex)
{
	major_info_t *major_info = dasd_major_info;
	while (major_info && devindex >= DASD_PER_MAJOR) {
		devindex -= DASD_PER_MAJOR;
		major_info = major_info->next;
	}
	if (!major_info)
                return NULL;
	return major_info->dasd_device[devindex];
}

static major_info_t *
major_info_from_devindex (int devindex)
{
	major_info_t *major_info = dasd_major_info;
	while (major_info && devindex >= DASD_PER_MAJOR) {
		devindex -= DASD_PER_MAJOR;
		major_info = major_info->next;
	}
	return major_info;
}

int
major_from_devindex (int devindex)
{
	major_info_t *major_info = major_info_from_devindex (devindex);
	return major_info->gendisk.major;
}

static int
devindex_from_kdev_t (kdev_t dev)
{
	int devindex = 0;
	major_info_t *major_info = dasd_major_info;
	while (major_info &&
	       MAJOR (dev) != major_info->gendisk.major) {
		devindex += (1 << (MINORBITS - DASD_PARTN_BITS));
		major_info = major_info->next;
	}
	if (!major_info)
		devindex = -ENODEV;
        devindex += MINOR(dev) >> DASD_PARTN_BITS;
	return devindex;
}

/* SECTION: managing dasd disciplines */

static dasd_discipline_t *dasd_disciplines = NULL;
static spinlock_t discipline_lock;

/* 
 * void dasd_discipline_enq (dasd_discipline_t * d)
 */

void
dasd_discipline_enq (dasd_discipline_t * d)
{
	spin_lock (&discipline_lock);
	d->next = dasd_disciplines;
	dasd_disciplines = d;
	spin_unlock (&discipline_lock);
}

/* 
 * int dasd_discipline_deq (dasd_discipline_t * d)
 */

int
dasd_discipline_deq (dasd_discipline_t * d)
{
	int rc = 0;
	spin_lock (&discipline_lock);
	if (dasd_disciplines == d) {
		dasd_disciplines = dasd_disciplines->next;
	} else {
		dasd_discipline_t *b;
		b = dasd_disciplines;
		while (b && b->next != d)
			b = b->next;
		if (b != NULL) {
			b->next = b->next->next;
		} else {
			rc = -ENOENT;
		}
	}
	spin_unlock (&discipline_lock);
	return rc;
}

/* SECTION: (de)queueing of requests to channel program queues */

/* 
 * void dasd_chanq_enq (dasd_chanq_t * q, ccw_req_t * cqr)
 */

void
dasd_chanq_enq (dasd_chanq_t * q, ccw_req_t * cqr)
{
	if (q->head != NULL) {
		q->tail->next = cqr;
	} else
		q->head = cqr;
	cqr->next = NULL;
	q->tail = cqr;
	q->queued_requests++;
	atomic_compare_and_swap_debug (&cqr->status, CQR_STATUS_FILLED, CQR_STATUS_QUEUED);
}

/* 
 * void dasd_chanq_enq_head (dasd_chanq_t * q, ccw_req_t * cqr)
 */

void
dasd_chanq_enq_head (dasd_chanq_t * q, ccw_req_t * cqr)
{
	cqr->next = q->head;
	q->head = cqr;
	if (q->tail == NULL)
		q->tail = cqr;
	q->queued_requests++;

        atomic_compare_and_swap_debug (&cqr->status, 
                                       CQR_STATUS_FILLED, 
                                       CQR_STATUS_QUEUED);
}

/* 
 * int dasd_chanq_deq (dasd_chanq_t * q, ccw_req_t * cqr)
 */

int
dasd_chanq_deq (dasd_chanq_t * q, ccw_req_t * cqr)
{
	ccw_req_t *prev;

	if (cqr == NULL)
		return -ENOENT;
	if (cqr == (ccw_req_t *) q->head) {
		q->head = cqr->next;
		if (q->head == NULL)
			q->tail = NULL;
	} else {
		prev = (ccw_req_t *) q->head;
		while (prev && prev->next != cqr)
			prev = prev->next;
		if (prev == NULL)
			return -ENOENT;
		prev->next = cqr->next;
		if (prev->next == NULL)
			q->tail = prev;
	}
/*	cqr->next = NULL; */
	q->queued_requests--;
	return 0;
}

/* SECTION: Handling of the queue of queues */

#ifdef CONFIG_SMP
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static spinlock_t cq_lock;		/* spinlock for cq_head */
#else
static spinlock_t cq_lock = SPIN_LOCK_UNLOCKED;		/* spinlock for cq_head */
#endif /* LINUX_IS_24 */
#else
int cq_lock;
#endif				/* __SMP__ */
static dasd_chanq_t *qlist_head = NULL;		/* head of queue of queues */

/* 
 * void qlist_enq (dasd_chanq_t * q)
 * queues argument to head of the queue of queues
 * and marks queue to be active
 */

static void
qlist_enq (dasd_chanq_t * q)
{
	long flags;
	if (q == NULL) {
		printk (KERN_WARNING PRINTK_HEADER " NULL queue to be queued to queue of queues\n");
		return;
	}
	spin_lock_irqsave (&cq_lock,flags);
	if (atomic_read (&q->flags) & DASD_CHANQ_ACTIVE) {
		printk (KERN_WARNING PRINTK_HEADER " Queue already active");
	}
	atomic_set_mask (DASD_CHANQ_ACTIVE, &q->flags);
	q->next_q = qlist_head;
	qlist_head = q;
	spin_unlock_irqrestore (&cq_lock,flags);
}

/* 
 * void qlist_deq (dasd_chanq_t * q)
 * dequeues argument from the queue of queues
 * and marks queue to be inactive
 */

static void
qlist_deq (dasd_chanq_t * q)
{
	long flags;
	if (qlist_head == NULL) {	
		printk (KERN_ERR PRINTK_HEADER "Channel queue is empty%s\n", "");
		return;
	}
	if (q == NULL) {
		printk (KERN_WARNING PRINTK_HEADER " NULL queue to be dequeued from queue of queues\n");
		return;
	}
	spin_lock_irqsave (&cq_lock,flags);
	if (!(atomic_read (&q->flags) & DASD_CHANQ_ACTIVE)) {
		printk (KERN_WARNING PRINTK_HEADER " Queue not active\n");
	} else if (qlist_head == q) {
		qlist_head = q->next_q;
	} else {
		dasd_chanq_t *c = qlist_head;
		while (c->next_q && c->next_q != q)
			c = c->next_q;
		if (c->next_q == NULL)
			printk (KERN_WARNING PRINTK_HEADER " Queue %p not in queue of queues\n", q);
		else
			c->next_q = q->next_q;
	}
	atomic_clear_mask (DASD_CHANQ_ACTIVE, &q->flags);
	q->next_q = NULL;
	spin_unlock_irqrestore (&cq_lock,flags);
}

/* SECTION: All the gendisk stuff */


static int
dasd_partn_detect (int devindex)
{
	int rc = 0;

	major_info_t *major_info = major_info_from_devindex (devindex);
	struct gendisk *dd = &major_info->gendisk;
	int minor = ( devindex & 
                      (( 1 << (MINORBITS-DASD_PARTN_BITS) ) - 1)) << dd->minor_shift;
	struct dasd_device_t *device = find_dasd_device (devindex);
        
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        register_disk(dd,
                      MKDEV (dd->major, minor),
                      1 << DASD_PARTN_BITS,
                      &dasd_device_operations,
                      (device->sizes.blocks << device->sizes.s2b_shift));
#else
        dd->sizes[minor] = (device->sizes.blocks << device->sizes.s2b_shift) >> 1;
        resetup_one_dev(dd,minor>>DASD_PARTN_BITS);
#endif /* LINUX_IS_24 */
	return rc;
}

/* SECTION: Managing wrappers for ccwcache */

#define DASD_EMERGENCY_REQUESTS 16

static ccw_req_t *dasd_emergency_req[DASD_EMERGENCY_REQUESTS]={NULL,};
static spinlock_t dasd_emergency_req_lock = SPIN_LOCK_UNLOCKED;

static void
dasd_init_emergency_req ( void ) 
{
        int i;
        for ( i = 0; i < DASD_EMERGENCY_REQUESTS; i++) {
          dasd_emergency_req[i] = (ccw_req_t*)get_free_page(GFP_KERNEL);
        }
}

static void
dasd_cleanup_emergency_req ( void ) 
{
        int i;
        for ( i = 0; i < DASD_EMERGENCY_REQUESTS; i++) {
                if (dasd_emergency_req[i])
                        free_page((long)(dasd_emergency_req[i]));
                else
                        printk (KERN_WARNING PRINTK_HEADER "losing one page for 'in-use' emergency request\n");
        }
}

ccw_req_t *
dasd_alloc_request (char *magic, int cplength, int datasize)
{
        ccw_req_t *rv = NULL;
        int i;
        if ( ( rv = ccw_alloc_request(magic,cplength,datasize )) != NULL ) {
                return rv;
        }
        if ( cplength * sizeof(ccw1_t) + datasize + sizeof(ccw_req_t) > PAGE_SIZE ) {
                return NULL;
        }
        spin_lock(&dasd_emergency_req_lock);
        for ( i = 0; i < DASD_EMERGENCY_REQUESTS; i++) {
                if ( dasd_emergency_req[i] != NULL ) {
                        rv = dasd_emergency_req[i];
                        dasd_emergency_req[i] = NULL;
			break;
                }
        }
        spin_unlock(&dasd_emergency_req_lock);
        if ( rv ) {
                memset (rv,0, PAGE_SIZE);
                rv -> cache = (kmem_cache_t *)(dasd_emergency_req + i);
                strncpy ( (char *)(&rv->magic), magic, 4);
                ASCEBC((char *)(&rv->magic),4);
                rv -> cplength = cplength;
                rv -> datasize = datasize;
                rv -> data = (void *)((long)rv + PAGE_SIZE - datasize);
                rv -> cpaddr = (ccw1_t *)((long)rv +  sizeof(ccw_req_t));
        }
        return rv;
}

void 
dasd_free_request (ccw_req_t * request)
{
        if ( request -> cache >= (kmem_cache_t *)dasd_emergency_req &&
             request -> cache <= (kmem_cache_t *)(dasd_emergency_req + DASD_EMERGENCY_REQUESTS) ) {
                *((ccw_req_t **)(request -> cache)) = request;
        } else {
                ccw_free_request(request);
        }
}

/* SECTION: Managing the device queues etc. */

static atomic_t bh_scheduled = ATOMIC_INIT (0);
static atomic_t request_fn_scheduled = ATOMIC_INIT (0);

static void
run_bh (void)
{
	atomic_set (&bh_scheduled, 0);
	dasd_do_chanq ();
}

void
dasd_schedule_bh ( void )
{
	static struct tq_struct bh_tq =
	{0,};
	/* Protect against rescheduling, when already running */
	if (atomic_compare_and_swap (0, 1, &bh_scheduled))
		return;
	bh_tq.routine = (void *) (void *) run_bh;
	queue_task (&bh_tq, &tq_immediate);
	mark_bh (IMMEDIATE_BH);
	return;
}

static void
try_request_fn (void)
{
        long flags;
        spin_lock_irqsave (&io_request_lock,flags);
	atomic_set (&request_fn_scheduled, 0);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        {
          major_info_t *mi;
          for (mi=dasd_major_info; mi != NULL; mi = mi->next ) {
            do_dasd_request(BLK_DEFAULT_QUEUE(mi->gendisk.major));
          }
        }
#else
        do_dasd_request ();
#endif /* LINUX_IS_24 */
        spin_unlock_irqrestore (&io_request_lock,flags);
}

static void
schedule_request_fn (void (*func) (void))
{
	static struct tq_struct req_tq =
	{0,};
	/* Protect against rescheduling, when already running */
	if (func != try_request_fn) {
		panic (PRINTK_HEADER "Programming error! must call schedule_request_fn (try_request_fn)\n");
	}
	if (atomic_compare_and_swap (0, 1, &request_fn_scheduled))
		return;
	req_tq.routine = (void *) (void *) func;
	queue_task (&req_tq, &tq_immediate);
	mark_bh (IMMEDIATE_BH);
	return;
}

int
dasd_start_IO (ccw_req_t * cqr)
{
	int rc = 0;
	int retries = DASD_SSCH_RETRIES;
	dasd_device_t *device = cqr->device;
	int irq, devno;
	int devindex;
	major_info_t *major_info;

	if (!cqr) {
		printk (KERN_WARNING PRINTK_HEADER "No request passed to start_io function");
		return -EINVAL;
	}
	irq = device->devinfo.irq;
	devno = device->devinfo.devno;
	devindex = devindex_from_devno (devno);
	major_info = major_info_from_devindex (devindex);

	if (strncmp ((char *) &cqr->magic, device->discipline->ebcname, 4)) {
		printk (KERN_WARNING PRINTK_HEADER
			"0x%04X on sch %d = /dev/%s (%d:%d)"
			" magic number of ccw_req_t 0x%08lX doesn't match"
			" discipline 0x%08lX\n",
			devno, irq, device->name,
			major_from_devindex (devindex),
			devindex << DASD_PARTN_BITS,
			cqr->magic, *(long *) device->discipline->name);
		return -EINVAL;
	}
	atomic_compare_and_swap_debug (&cqr->status, 
                                       CQR_STATUS_QUEUED, 
                                       CQR_STATUS_IN_IO);
	do {
		asm volatile ("STCK %0":"=m" (cqr->startclk));
		rc = do_IO (irq, cqr->cpaddr, (long) cqr, cqr->lpm, cqr->options);
		switch (rc) {
		case 0:
                        if (!(cqr->options & DOIO_WAIT_FOR_INTERRUPT)) {
				atomic_set_mask (DASD_CHANQ_BUSY, &device->queue.flags);
                        }
                        if ( cqr->expires ) {
                                cqr->expires += cqr->startclk;
                        }
			break;
		case -ENODEV:
			printk (KERN_WARNING PRINTK_HEADER
			    " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
				" appears not to be present %d retries left\n",
				devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS,
				retries);
			break;
		case -EIO:
			printk (KERN_WARNING PRINTK_HEADER
			    " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
				" I/O error %d retries left\n",
				devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS,
				retries);
			break;
		case -EBUSY:	/* set up timer, try later */
			printk (KERN_WARNING PRINTK_HEADER
			    " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
				" is busy %d retries left\n",
				devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS,
				retries);
			break;
		default:
			printk (KERN_WARNING PRINTK_HEADER
			    " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
				" unknown return code %d, %d retries left."
			  " Pls report this message to linux390@de.ibm.com\n",
				devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS,
				rc, retries);
			break;
		}
	} while (rc && retries--);
	if (rc) {
                atomic_compare_and_swap_debug (&cqr->status, 
                                               CQR_STATUS_IN_IO, 
                                               CQR_STATUS_FAILED);
	}
	return rc;
}


static int
sleep_on_req ( ccw_req_t * req )
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        DECLARE_WAITQUEUE (wait,current);
#else
        struct wait_queue wait = {current, NULL};
#endif /* LINUX_VERSION_CODE */
        unsigned long flags;
        int cs;
        int rc = 0;
        dasd_device_t *device = (dasd_device_t *)req->device;
        s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
        dasd_chanq_enq (&device->queue, req);
        if (!(atomic_read (&device->queue.flags) & DASD_CHANQ_ACTIVE)) {
                qlist_enq (&device->queue);
        }
        dasd_schedule_bh();
        add_wait_queue (&device->wait_q, &wait);
        do {
                current->state = TASK_INTERRUPTIBLE;
                s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
                schedule ();
                s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
                cs = atomic_read (&req->status);
		if ( signal_pending(current) ) {
			rc = -ERESTARTSYS;
			break;
		}
        } while ( ! (cs & CQR_STATUS_FINISHED) );
        /* was originally: while ((cs != CQR_STATUS_DONE) && (cs != CQR_STATUS_FAILED)); */ 
        remove_wait_queue (&device->wait_q, &wait);
        s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
        if  ( cs & CQR_STATUS_FAILED ) {
                rc = -EIO;
        }
        return rc;
}


static void
dasd_end_request (struct request *req, int uptodate)
{
	struct buffer_head *bh;
	while ((bh = req->bh) != NULL) {
                req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_end_io (bh, uptodate);
	}
	if (!end_that_request_first (req, uptodate, DASD_NAME)) {
#ifndef DEVICE_NO_RANDOM
		add_blkdev_randomness (MAJOR (req->rq_dev));
#endif
		end_that_request_last (req);
	}
	return;
}

#undef CURRENT
#define CURRENT (blk_dev[major].current_request)

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static void
do_dasd_request (request_queue_t * queue)
#else
static void
do_dasd_request (void)
#endif /* LINUX_IS_24 */
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	struct request *req;
        int go;
#else
	struct request *req, *next, *prev;
#endif /* LINUX_IS_24 */
	ccw_req_t *cqr;
	dasd_chanq_t *q;
	long flags;
	int devindex, irq, partn;
	int broken, busy;
	dasd_device_t *device;
	major_info_t *major_info;
	int devno;
        int major;


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        {
                if ( queue == NULL ) {
			printk(KERN_ERR PRINTK_HEADER "Null queue !!\n");
                        return;
                }
                go = 1;
                while (go && !list_empty (&queue->queue_head)) {
                        req = blkdev_entry_next_request (&queue->queue_head);
                        major = MAJOR(req->rq_dev);
                        for (major_info = dasd_major_info; major_info != NULL; major_info = major_info->next ) {
                                if ( major_info->gendisk.major == major )
                                        break;
                        }
                        if ( major_info == NULL ) {
                                printk (KERN_ERR PRINTK_HEADER "No major_info\n");
                                return;
                        }
#else
        for ( major_info = dasd_major_info; major_info != NULL; major_info = major_info->next ) {
                major = major_info->gendisk.major;
                prev = NULL;
                for (req = CURRENT; req != NULL; req = next) {
                        next = req->next;
                        if (req == &blk_dev[major].plug) { /* remove plug if applicable */
                                req->next = NULL;
                                if (prev) {
                                        prev->next = next;
                                } else {
                                        CURRENT = next;
                                }
                                continue;
                        }
#endif /* LINUX_IS_24 */
                        devindex = devindex_from_kdev_t(req->rq_dev);
                        if ( devindex < 0 ) {
                                printk ( KERN_WARNING PRINTK_HEADER 
                                         "requesting I/O on nonexistent device %d -> %d\n",
                                         devindex,req->rq_dev);
                                dasd_end_request(req,0);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                                blkdev_dequeue_request (req);
#else
                                req->next = NULL;
                                if (prev) {
                                        prev->next = next;
                                } else {
                                        CURRENT = next;
                                }
#endif /* LINUX_IS_24 */
                                continue;
                        }
                        device = find_dasd_device (devindex);
                        if ( device == NULL ) {
                                printk ( KERN_WARNING PRINTK_HEADER 
                                         "requesting I/O on nonexistent device\n");
				dasd_end_request(req,0);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                                blkdev_dequeue_request (req);
#else
                                req->next = NULL;
                                if (prev) {
                                        prev->next = next;
                                } else {
                                        CURRENT = next;
                                }
#endif /* LINUX_IS_24 */
                                continue;
                        }
                        irq = device->devinfo.irq;
                        s390irq_spin_lock_irqsave (irq, flags);
                        devno = device->devinfo.devno;
                        q = & device->queue;
                        busy = atomic_read (&q->flags) & DASD_CHANQ_BUSY;
                        broken = atomic_read (&q->flags) & DASD_REQUEST_Q_BROKEN;
                        partn = MINOR (req->rq_dev) & ((1 << major_info->gendisk.minor_shift) - 1);
                        if ( ! busy ||
                             ( ! broken &&
                               (req->nr_sectors >= QUEUE_SECTORS))) {
                                if (device->discipline == NULL) {
                                        printk (KERN_WARNING PRINTK_HEADER
                                                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                " is not assigned to a discipline\n",
                                                devno, irq, device->name, major, devindex << DASD_PARTN_BITS);
                                        dasd_end_request (req, 0);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                                        blkdev_dequeue_request (req);
#else
                                        req->next = NULL;
                                       	if (prev) {
                                                prev->next = next;
                         	        } else {
                                       	        CURRENT = next;
                                        }
#endif /* LINUX_IS_24 */	
					s390irq_spin_unlock_irqrestore (irq, flags);
                                	continue;
                                }
                                if (device->discipline->build_cp_from_req == NULL) {
                                        printk (KERN_WARNING PRINTK_HEADER
                                                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                " discipline %s hast no builder function\n",
                                                devno, irq, device->name,major, devindex << DASD_PARTN_BITS,
                                                device->discipline->name);
                                        dasd_end_request (req, 0);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                        	        blkdev_dequeue_request (req);
#else
                                	req->next = NULL;
                                	if (prev) {
                                        	prev->next = next;
                                	} else {
                                        	CURRENT = next;
                                	}
#endif /* LINUX_IS_24 */
					s390irq_spin_unlock_irqrestore (irq, flags);
					continue;                 
		                }
                                req->sector += major_info->gendisk.part[MINOR(req->rq_dev)].start_sect;
                                cqr = device->discipline->build_cp_from_req (device, req);
                                if (cqr == NULL) {
                                        atomic_set_mask (DASD_REQUEST_Q_BROKEN, &q->flags);
                                        printk (KERN_WARNING PRINTK_HEADER
                                                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                " Could not create channel program for request %p\n",
                                                devno, irq, device->name, major, devindex << DASD_PARTN_BITS, req);
                                /* put request back to queue*/
                                        req->sector -= major_info->gendisk.part[MINOR(req->rq_dev)].start_sect;
                                        s390irq_spin_unlock_irqrestore (irq, flags);
                                        continue;
                                } 
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                                blkdev_dequeue_request (req);
#else
                                req->next = NULL;
                                if (prev) {
                                        prev->next = next;
                                } else {
                                        CURRENT = next;
                                }
#endif /* LINUX_IS_24 */
                                dasd_chanq_enq (q, cqr);
                                if (!(atomic_read (&q->flags) & DASD_CHANQ_ACTIVE)) {
                                        qlist_enq (q);
                                }
                                if (!busy) {
                                        atomic_clear_mask (DASD_REQUEST_Q_BROKEN, &q->flags);
                                        if (atomic_read (&q->dirty_requests) == 0) {
                                                if (device->discipline->start_IO == NULL) {
                                                        printk (KERN_WARNING PRINTK_HEADER
                                                                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                                " dicsipline %s has no starter function\n",
                                                                devno, irq, device->name, major, devindex << DASD_PARTN_BITS,
                                                                device->discipline->name);
                                                } else {
                                                        if (device->discipline->start_IO (cqr) != 0) {
                                                                printk (KERN_DEBUG PRINTK_HEADER
                                                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                                        " starting of request from req_fn failed, postponing\n",
                                                                        devno, irq, device->name, major, devindex << DASD_PARTN_BITS);
                                                                dasd_schedule_bh ();	/* initiate bh to run */
                                                        }
                                                }
                                        } else {
						dasd_schedule_bh();
                                        } 
                                }
                        } else {
                                atomic_set_mask (DASD_REQUEST_Q_BROKEN, &q->flags);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                                go = 0;
#else
                                prev = req;
#endif /* LINUX_IS_24 */
                        }
                        s390irq_spin_unlock_irqrestore (irq, flags);
                }
        }
        return;
}

static void
dasd_do_chanq (void)
{
	dasd_chanq_t *qp = NULL;
	dasd_chanq_t *nqp;
	dasd_device_t *device;
	ccw_req_t *cqr, *next;
	long flags;
	int irq;
	int devno, devindex;
	int rc = -1;
	volatile int cqrstatus;

	for (qp = qlist_head; qp != NULL; qp = nqp) {

		device = (dasd_device_t *)((long)qp - (long)offsetof(dasd_device_t,queue));
		irq = device->devinfo.irq;

		s390irq_spin_lock_irqsave (irq, flags);

		/* Get first request */
		cqr = (ccw_req_t *) (qp->head);
		nqp = qp->next_q;
/* empty queue -> dequeue and proceed */
		if (!cqr) {
			qlist_deq (qp);
			s390irq_spin_unlock_irqrestore(irq,flags);
			continue;
		}
		s390irq_spin_unlock_irqrestore(irq,flags);
 
/* process all requests on that queue */
		do {
			dasd_discipline_t *discipline=device->discipline;
			next = NULL;

			if (strncmp ((char *) &cqr->magic, discipline->ebcname, 4)) {
				panic (PRINTK_HEADER
				       "in dasd_do_chanq: magic no mismatch %p -> 0x%lX\n",
				       cqr, cqr->magic);
			}
			if ( device != cqr->device ) {
                                panic (PRINTK_HEADER
                                       "in dasd_do_chanq: device mismatch %p -> %p(qcr) vs. %p\n",
                                       cqr, cqr->device,device);
			}
			devno = device->devinfo.devno;
			devindex = devindex_from_devno (devno);

			s390irq_spin_lock_irqsave (irq, 
                        	                   flags);
			cqrstatus = atomic_read (&cqr->status);

			switch (cqrstatus) {
			case CQR_STATUS_QUEUED:
				if (discipline->start_IO &&
				    ((rc = discipline->start_IO (cqr)) == 0)) {
                                } else {
					printk (KERN_WARNING PRINTK_HEADER
						" devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
						" Failing to start I/O operation with rc %d\n",
						devno, irq, device->name, major_from_devindex (devindex), 
                                                devindex << DASD_PARTN_BITS, rc);
					switch (rc) {
					case EBUSY:
						if (cqr->retries--) {
							printk (KERN_WARNING PRINTK_HEADER
								" devno 0x%04X on subchannel %d = /dev/%s (%d:%d) busy:"
								" retrying ... %d retries left\n",
								devno, irq, device->name, major_from_devindex (devindex), 
                                                                devindex << DASD_PARTN_BITS, cqr->retries);
							break;
						}
					default:{	/* Fallthrough ?? */
							printk (KERN_WARNING PRINTK_HEADER
								" devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
								" Giving up this request!\n",
								devno, irq, device->name, major_from_devindex (devindex),
                                                                devindex << DASD_PARTN_BITS);
							break;
						}
					}
				}
				break;
			case CQR_STATUS_IN_IO:{
                                unsigned long long now;
                                unsigned long long delta;
                                
                                asm volatile ("STCK %0":"=m" (now));
                                if (cqr->expires && cqr->startclk &&
                                    cqr->expires < now) {
                                        delta = cqr->expires - cqr->startclk;
                                        printk (KERN_ERR PRINTK_HEADER
                                                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                " I/O operation outstanding longer than 0x%08lx%08lx usecs on req %p\n",
                                                devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS, (long)(delta >> 44), (long)(delta >> 12), cqr);
                                        cqr->expires += delta;
#if 0
                                        if ( cqr->retries-- ) {
                                                printk (KERN_WARNING PRINTK_HEADER
                                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                        " waiting %d more times\n",
                                                        devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS, cqr->retries);
                                                cqr->expires += delta;
                                                break;
                                        } else {
                                                printk (KERN_WARNING PRINTK_HEADER
                                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                        " You should disable that device by issueing '@#?!'\n",		/* FIXME */
                                                        devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS);
                                                atomic_compare_and_swap_debug (&cqr->status, CQR_STATUS_IN_IO, CQR_STATUS_FAILED);
                                                halt_IO(irq,(unsigned long)cqr, DOIO_WAIT_FOR_INTERRUPT);
                                                break;
                                        }
#endif
                                }
                                break;
                        }
			case CQR_STATUS_ERROR:{
                                dasd_erp_action_fn_t erp_action;
                                ccw_req_t *erp_cqr = NULL;
                                
                                if (cqr->dstat->flag & DEVSTAT_HALT_FUNCTION) {
                                        atomic_compare_and_swap_debug(&cqr->status,
                                                                      CQR_STATUS_ERROR,
                                                                      CQR_STATUS_FAILED);
                                        next = cqr;
                                } else if (discipline -> erp_action != NULL                    &&
                                           (erp_action = discipline->erp_action (cqr)) != NULL &&
                                           (erp_cqr = erp_action (cqr)) != NULL                  ) {

                                        if (erp_cqr != cqr){
                                                
                                                dasd_chanq_enq_head (qp, erp_cqr);
                                        }
                                        cqr->retries--;
                                        next = erp_cqr;	/* prefer execution of erp ccw */

                                } else {
                                        atomic_compare_and_swap_debug (&cqr->status, 
                                                                       CQR_STATUS_ERROR, 
                                                                       CQR_STATUS_FAILED);
                                        next = cqr;
                                }
                                break;
                        }
			case CQR_STATUS_DONE:{
                                dasd_erp_postaction_fn_t erp_postaction;
                                next = cqr->next;
                                asm volatile ("STCK %0":"=m" (cqr->endclk));
                                if (cqr->refers && cqr->function) {	/* we deal with an ERP */
                                        if ( discipline->erp_postaction &&
                                             ((erp_postaction = discipline->erp_postaction (cqr)) != NULL)) {
                                                printk (KERN_WARNING PRINTK_HEADER
                                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                        " postprocessing successful ERA using %p\n",
                                                        devno, irq, device->name, major_from_devindex (devindex), 
                                                        devindex << DASD_PARTN_BITS, erp_postaction);
                                                cqr = erp_postaction (cqr);  
                                                next = cqr; 
                                        } else {
                                                printk (KERN_WARNING PRINTK_HEADER
                                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                        " No procedure to postprocess ERA - "
                                                        " giving up request",
                                                        devno, irq, device->name, major_from_devindex (devindex), 
                                                        devindex << DASD_PARTN_BITS);
                                                atomic_compare_and_swap_debug (&cqr->refers->status,
                                                                               CQR_STATUS_ERROR, 
                                                                               CQR_STATUS_FAILED);

                                                atomic_dec (&device->queue.dirty_requests);
                                                dasd_chanq_deq (&device->queue, cqr);
                                                dasd_free_request(cqr); /* Only free request if nobody is waiting on it */
                                        }
                                } else if ( cqr->req ) {
                                        dasd_end_request (cqr->req, 1);
#ifdef DASD_PROFILE
                                        dasd_profile_add (cqr);
#endif				/* DASD_PROFILE */
                                        dasd_chanq_deq (&device->queue, cqr);
                                        dasd_free_request(cqr); /* Only free request if nobody is waiting on it */
                                } else {
                                        /* during format we don't have the request structure */
                                        /* notify sleeping task about finished postprocessing */
                                        atomic_compare_and_swap_debug (&cqr->status, 
                                                                       CQR_STATUS_DONE, 
                                                                       CQR_STATUS_DONE | CQR_STATUS_FINISHED); 
                                        dasd_chanq_deq (&device->queue, cqr);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                                        wake_up (&device->wait_q);
#else
                                        if (device->wait_q) {
                                                wake_up (&device->wait_q);
                                        }
#endif /* LINUX_IS_24 */
                                }
                                break;
                        }
			case CQR_STATUS_FAILED:{
                                dasd_erp_postaction_fn_t erp_postaction;
                                next = cqr->next;
                                asm volatile ("STCK %0":"=m" (cqr->endclk));
                                if (cqr->refers && cqr->function) {	/* we deal with an ERP */
                                        if ( discipline->erp_postaction &&
                                             ((erp_postaction = discipline->erp_postaction (cqr)) != NULL)) {
                                                printk (KERN_WARNING PRINTK_HEADER
                                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                        " postprocessing unsuccessful ERA using %p\n",
                                                        devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS, erp_postaction);
                                                cqr = erp_postaction (cqr); 
                                                next = cqr; 
                                        } else {
                                                printk (KERN_WARNING PRINTK_HEADER
                                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                        " No procedure to postprocess unsuccessful ERA - "
                                                        " giving up request",
                                                        devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS);
                                                atomic_compare_and_swap_debug (&cqr->refers->status, 
                                                                               CQR_STATUS_ERROR, 
                                                                               CQR_STATUS_FAILED);
                                                
                                                atomic_dec (&device->queue.dirty_requests);
                                                dasd_chanq_deq (&device->queue, cqr);
                                                dasd_free_request(cqr); /* Only free request if nobody is waiting on it */
                                        }
                                } else if (cqr->req) {
                                        dasd_end_request (cqr->req, 0);
#ifdef DASD_PROFILE
                                        dasd_profile_add (cqr);
#endif				/* DASD_PROFILE */
                                        dasd_chanq_deq (&device->queue, cqr);
                                        dasd_free_request(cqr); /* Only free request if nobody is waiting on it */
                                } else {
                                        /* during format we don't have the request structure */
                                        /* notify sleeping task about finished postprocessing */
                                        atomic_compare_and_swap_debug (&cqr->status, 
                                                                       CQR_STATUS_FAILED, 
                                                                       CQR_STATUS_FAILED | CQR_STATUS_FINISHED); 
                                        
                                        dasd_chanq_deq (&device->queue, cqr);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                                        wake_up (&device->wait_q);
#else
                                        if (device->wait_q) {
                                                wake_up (&device->wait_q);
                                        }
#endif /* LINUX_IS_24 */
                                } 
                                break;
                        }

			case CQR_STATUS_PENDING:
                                /* just wait */
                                printk (KERN_WARNING PRINTK_HEADER
                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                        " CQR_STATUS_PENDING - just wait...\n",
                                        devno, irq, device->name, 
                                        major_from_devindex (devindex), 
                                        devindex << DASD_PARTN_BITS);
                                break;
                        
			default:
                                printk (KERN_WARNING PRINTK_HEADER
                                        "Internal error in " __FILE__ " on line %d."
                                        " inconsistent content of ccw_req_t"
                                        " cqrstatus = %d"
                                        " Pls send this message and your System.map to"
                                        " linux390@de.ibm.com\n",
                                        __LINE__, cqrstatus);
                                
			}
			s390irq_spin_unlock_irqrestore (irq, flags);
		} while ((cqr = next) != NULL);
	}
	schedule_request_fn (try_request_fn);
        return;
}

/*
 * DASD_HANDLE_STATE_CHANGE_PENDING 
 *
 * DESCRIPTION
 *   Handles the state change pending interrupt.
 *   Search for the device related request queue and check if the first 
 *   cqr in queue in in status 'CQR_STATUE_PENDING'.
 *   If so the status is set to 'CQR_STATUS_QUEUED' and the bh is
 *   scheduled.
 *
 *  PARAMETER
 *   stat               device status of state change pending interrupt.
 */
void 
dasd_handle_state_change_pending (devstat_t *stat)
{
        dasd_device_t *device;
        ccw_req_t     *cqr;
        int           devindex;

	devindex = devindex_from_devno (stat->devno);
	if (devindex < 0) {
                return;
	}

	device = find_dasd_device (devindex);
	if (device == NULL) {
                return; 
        }

        /* re-activate first request in queue */
        cqr = device->queue.head;    
        
        if (cqr != NULL                                     &&
            atomic_read(&cqr->status) == CQR_STATUS_PENDING   ) {
                dasd_device_t *device = cqr->device;

                DASD_MESSAGE (KERN_INFO, device,
                              "%s",
                              "device request queue restarted by "
                              "SCP interrupt\n");
                
                del_timer(&device->timer);
                atomic_compare_and_swap_debug (&cqr->status, 
                                               CQR_STATUS_PENDING, 
                                               CQR_STATUS_QUEUED);
                dasd_schedule_bh();
        }
} /* end dasd_handle_state_change_pending */

void
dasd_int_handler (int irq, void *ds, struct pt_regs *regs)
{
	devstat_t *stat = (devstat_t *) ds;
	int ip;
	ccw_req_t *cqr;
	int done_fast_io = 0;
	dasd_era_t era = dasd_era_fatal;
	dasd_device_t *device;
	int devno = -1, devindex = -1;
        
#ifdef ERP_DEBUG                /* #define ERP_DEBUG is done in dasd.h */
        static int counter = 0;
#endif

	if (!stat) {
		PRINT_ERR ("handler called without devstat");
		return;
	}

	ip = stat->intparm;
	if (!ip) {		/* no intparm: unsolicited interrupt */
		PRINT_INFO ("%04X caught unsolicited interrupt\n",
			    stat->devno);

                /* check for state change pending interrupt */
                if (stat->dstat & (DEV_STAT_ATTENTION | 
                                   DEV_STAT_DEV_END   |
                                   DEV_STAT_UNIT_EXCEP )) {
                        
                        dasd_handle_state_change_pending (stat);
                }

		return;
	}

	if (ip & 0x80000001) {
		PRINT_INFO ("%04X  caught spurious interrupt with parm %08x\n",
			    stat->devno, ip);
		return;
	}
	cqr = (ccw_req_t *) ip;
	device = (dasd_device_t *) cqr->device;
	devno = device->devinfo.devno;
	devindex = devindex_from_devno (devno);
	if (device == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
		     " IRQ on devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
			" belongs to NULL device\n",
			devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS);
	}
	if (device->devinfo.irq != irq) {
		printk (KERN_WARNING PRINTK_HEADER
		     " IRQ on devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
			" doesn't belong to device irq %d\n",
			devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS,
			device->devinfo.irq);
		return;
	}
	if (device->discipline == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			" devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
			" is not assigned to a discipline\n",
			devno, irq, device->name, major_from_devindex (devindex), devindex << DASD_PARTN_BITS);
	}
	if (strncmp (device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		printk (KERN_WARNING PRINTK_HEADER
			"0x%04X on sch %d : /dev/%s (%d:%d)"
			" magic number of ccw_req_t 0x%08lX doesn't match"
			" discipline 0x%08X\n",
			devno, irq, device->name,
			major_from_devindex (devindex),
			devindex << DASD_PARTN_BITS,
			cqr->magic, *(int *) (&device->discipline->name));
		return;
	}
	asm volatile ("STCK %0":"=m" (cqr->stopclk));

#ifdef ERP_DEBUG
                if ((++counter % 937 >= 100) &&
                    (  counter % 937 <= 110) &&
                    (  counter < 5000    ) &&
                    (  counter > 2000    )   ){
                        static int fake_count = 0;
                        printk ( KERN_INFO PRINTK_HEADER "***********************************************\n");
                        printk ( KERN_INFO PRINTK_HEADER "Faking I/O error to recover from; cntr=%i / %02X\n",counter,++fake_count);
                        printk ( KERN_INFO PRINTK_HEADER "***********************************************\n");
                        era = dasd_era_recover;
                        stat->flag |= DEVSTAT_FLAG_SENSE_AVAIL;
                        stat->dstat |= 0x02;
// reset notification
                        {
                                char *sense = stat->ii.sense.data;
                                char buffer[100];
				memset(sense,0,32);
                                sprintf(buffer,"CPU ALL TR IO %X INST INT CCW", devno);
                                cpcmd(buffer,NULL,NULL);
                                sense[0]=0x10;
                                sense[4]=0x21;
                                sense[7]=0x9;
                                sense[15]=0x4;
                                sense[16]=0xe5;
                                sense[18]=0x43;
                                sense[19]=0xfb;
                                sense[20]=0x54;
                                sense[22]=0x0f;
                                sense[23]=0x09;
                                sense[26]=0x40;
                                sense[27]=0xe0;
                        }
                }
#endif

	if ((!(stat->flag & DEVSTAT_HALT_FUNCTION) &&
	     stat->cstat == 0x00 &&
	     stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END)) ||
 	    (device->discipline->examine_error &&
	     (era = device->discipline->examine_error (cqr, stat)) == dasd_era_none)) {

		atomic_compare_and_swap_debug (&cqr->status, CQR_STATUS_IN_IO, CQR_STATUS_DONE);
                atomic_compare_and_swap (DASD_DEVICE_LEVEL_ANALYSIS_PENDING,
					 DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
                                         &device->level);
		if (cqr->next &&
		    (atomic_read (&cqr->next->status) ==
		     CQR_STATUS_QUEUED)) {
			if (dasd_start_IO (cqr->next) == 0) {
				done_fast_io = 1;
			}
		}
	} else {		/* only visited in case of error ! */
		if (cqr->dstat == NULL)
			cqr->dstat = kmalloc (sizeof (devstat_t), GFP_ATOMIC);
		if (cqr->dstat) {
			memcpy (cqr->dstat, stat, sizeof (devstat_t));
		} else {
			PRINT_ERR ("no memory for dstat\n");
		}
		if (device->discipline &&
		    device->discipline->dump_sense) {
			char *errmsg = device->discipline->dump_sense (device, cqr);
			if (errmsg != NULL) {
				printk ("%s", errmsg);
				free_page ((unsigned long) errmsg);
			} else {
				printk (KERN_WARNING PRINTK_HEADER
					"No memory to dump error message\n");
			}
		}
		atomic_inc (&device->queue.dirty_requests);
		/* errorprocessing */
		if (era == dasd_era_fatal) {
			PRINT_WARN ("ERP returned fatal error\n");
			atomic_compare_and_swap_debug (&cqr->status,
                                                       CQR_STATUS_IN_IO, CQR_STATUS_FAILED);
		} else {
			atomic_compare_and_swap_debug (&cqr->status,
                                                       CQR_STATUS_IN_IO, CQR_STATUS_ERROR);
		}
	}
	if (done_fast_io == 0)
		atomic_clear_mask (DASD_CHANQ_BUSY, &device->queue.flags);
        
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        wake_up (&device->wait_q);
#else
	if (device->wait_q) {
		wake_up (&device->wait_q);
	}
#endif /* LINUX_IS_24 */
	dasd_schedule_bh ();
}

/* SECTION: Some stuff related to error recovery */

ccw_req_t *
default_erp_action (ccw_req_t * cqr)
{
	ccw_req_t *erp = dasd_alloc_request ((char *) &cqr->magic, 1, 0);
	if ( !erp ) {
		printk(KERN_WARNING PRINTK_HEADER 
                       "unable to allocate ERP request\n");
		return NULL;
	}
        
	erp->cpaddr->cmd_code = CCW_CMD_TIC;
        erp->cpaddr->cda = (__u32)cqr -> cpaddr;
	erp->function = default_erp_action;
	erp->refers = cqr;
	erp->device = cqr->device;
        erp->magic = cqr->magic;
        erp->retries = 16;
	atomic_set (&erp->status, CQR_STATUS_FILLED);
        if ( cqr->startclk && cqr->expires ) {
         /*       cqr->expires -= cqr->startclk; */
	}
        return erp;
}

/*
 * DEFAULT_ERP_POSTACTION
 *
 * DESCRIPTION
 *   Frees all ERPs of the current ERP Chain and set the status
 *   of the original CQR either to CQR_STATUS_DONE if ERP was successful
 *   or to CQR_STATUS_FAILED if ERP was NOT successful.
 *
 * PARAMETER
 *   erp                current erp_head
 *
 * RETURN VALUES
 *   cqr                pointer to the original CQR
 */
ccw_req_t *
default_erp_postaction (ccw_req_t * erp)
{
	ccw_req_t *cqr = NULL, *free_erp = NULL;
	dasd_device_t *device = NULL;
	int success;

	device = (dasd_device_t *) (erp->device);

	if (atomic_read(&erp->status) == CQR_STATUS_DONE)
		success = 1;
	else
		success = 0;

#ifdef ERP_DEBUG

	/* print current erp_chain */
        printk (KERN_WARNING PRINTK_HEADER
                "default ERP postaction called for erp chain:\n");
        {
                ccw_req_t *temp_erp = NULL;
                for (temp_erp = erp; temp_erp != NULL; temp_erp = temp_erp->refers){
                        printk(KERN_WARNING PRINTK_HEADER 
                               "       erp %p refers to %p with erp function %p\n",
                               temp_erp,
                               temp_erp->refers,
                               temp_erp->function );
                }
        }

#endif /* ERP_DEBUG*/

	if (erp->refers == NULL || erp->function == NULL) {
		panic (PRINTK_HEADER "Programming error in ERP! Postaction called "
                       "for invalid ERPt\n");
	}

	/* free all ERPs - but NOT the original cqr */
	while (erp->refers != NULL) {
		free_erp = erp;
		erp = erp->refers;
		/* remove the request from the device queue */
		dasd_chanq_deq (&device->queue,	free_erp);
		/* free the finished erp request */
		dasd_free_request (free_erp);
	}

	/* save ptr to original cqr */
	cqr = erp;

	/* set corresponding status to original cqr */
	if (success) {
		atomic_compare_and_swap_debug (&cqr->status, 
                                               CQR_STATUS_ERROR, 
                                               CQR_STATUS_DONE);
	} else {
		atomic_compare_and_swap_debug (&cqr->status,
                                               CQR_STATUS_ERROR,
                                               CQR_STATUS_FAILED);
	}

#ifdef ERP_DEBUG
	/* print current erp_chain */
	printk (KERN_WARNING PRINTK_HEADER
		"default ERP postaction finished with remaining chain:\n");
	{
		ccw_req_t *temp_erp = NULL;
		for (temp_erp = cqr; temp_erp != NULL; temp_erp = temp_erp->refers) {
			printk (KERN_WARNING PRINTK_HEADER
				" erp %p refers to %p \n",
				temp_erp, temp_erp->refers);
		}
	}
#endif /* ERP_DEBUG */

	return cqr;
} /* end default_erp_postaction */

/* SECTION: The helpers of the struct file_operations */

/* 
 * int dasd_format ( device* device, format_data_t *fdata )
 * performs formatting of _device_ according to _fdata_
 * Note: The discipline's format_function is assumed to deliver formatting
 * commands to format a single unit of the device. In terms of the ECKD
 * devices this means CCWs are generated to format a single track.
 */

static int
dasd_format (dasd_device_t * device, format_data_t * fdata)
{
	int rc          = 0;
        int format_done = 0;  
	int devno       = device->devinfo.devno;
	int irq         = device->devinfo.irq;
	int devindex    = devindex_from_devno (devno);
        ccw_req_t *req  = NULL;
        
        spin_lock (&dasd_open_count_lock);
	if (device->open_count != 1) {
		printk (KERN_WARNING PRINTK_HEADER
			" devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                        " you shouldn't format a device that is already open\n",
			devno, 
                        irq, 
                        device->name, 
                        major_from_devindex (devindex), 
                        devindex << DASD_PARTN_BITS);
                spin_unlock (&dasd_open_count_lock);
		return -EINVAL;
	}
        device->open_count = -1;
        spin_unlock (&dasd_open_count_lock);
        printk (KERN_WARNING PRINTK_HEADER
                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                " Starting format process\n",
                        devno, 
                        irq, 
                        device->name, 
                        major_from_devindex (devindex), 
                        devindex << DASD_PARTN_BITS);
        
        dasd_set_device_level( device->devinfo.irq,
                               DASD_DEVICE_LEVEL_RECOGNIZED,
                               device->discipline,
                               0);

	if (device->discipline->format_device) {
                format_data_t temp = { 
                        fdata->start_unit, 
                        fdata->stop_unit, 
                        fdata->blksize, 
                        fdata->intensity};
                
                if ((fdata -> start_unit == DASD_FORMAT_DEFAULT_START_UNIT) &&
                    (fdata -> stop_unit  == DASD_FORMAT_DEFAULT_STOP_UNIT ) &&
                    (fdata -> intensity  == DASD_FORMAT_DEFAULT_INTENSITY )   ) {

                        format_data_t temp2 = {0,0,
                                               DASD_FORMAT_INVALIDATION_BS,
                                               fdata->intensity};
                        
                        printk (KERN_WARNING PRINTK_HEADER
                                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                " invalidating disk...\n",
                                devno, 
                                irq, 
                                device->name, 
                                major_from_devindex (devindex), 
                                devindex << DASD_PARTN_BITS);

                        req = device->discipline->format_device (device,&temp2);

                        if ( req ) {
                                rc = sleep_on_req(req);
                                dasd_free_request(req); /* request is no longer used */
                        } else {
                                rc = -EINVAL;
                        }
                        if ( rc ) {
                                printk (KERN_WARNING PRINTK_HEADER "Can't invalidate Track 0\n");
                        } else {
                                printk (KERN_WARNING PRINTK_HEADER
                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                        " ...invalidation done.\n",
                                        devno, 
                                        irq, 
                                        device->name, 
                                        major_from_devindex (devindex), 
                                        devindex << DASD_PARTN_BITS);
                        }
                        temp.start_unit++;
                }

                while ((!rc                                                               ) &&
                       ((req = device->discipline->format_device (device, &temp)) != NULL )   ) {
                        
                        format_done = 1;   /* at least one formatting cp was build */

                        if ( (rc=sleep_on_req(req)) != 0 ) {
                                printk (KERN_WARNING PRINTK_HEADER
                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                        " Formatting failed with rc = %d\n",
                                        devno, 
                                        irq, 
                                        device->name, 
                                        major_from_devindex (devindex), 
                                        devindex << DASD_PARTN_BITS, 
                                        rc);
                                break;
                        }
                        dasd_free_request(req); /* request is no longer used */
                        temp.start_unit++;
                }  /* end if no more requests */
                
                if (!rc          &&
                    req  == NULL   ) {
                        if ((fdata -> start_unit == DASD_FORMAT_DEFAULT_START_UNIT) &&
                            (fdata -> stop_unit  == DASD_FORMAT_DEFAULT_STOP_UNIT ) &&
                            (fdata -> intensity  == DASD_FORMAT_DEFAULT_INTENSITY )   ) {

                                format_data_t temp2 = {0,0,
                                                       fdata->blksize,
                                                       fdata->intensity};

                                printk (KERN_WARNING PRINTK_HEADER
                                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                        " revalidating disk...\n",
                                        devno, 
                                        irq, 
                                        device->name, 
                                        major_from_devindex (devindex), 
                                        devindex << DASD_PARTN_BITS);
                                
                                req = device->discipline->format_device (device, 
                                                                         &temp2);
                                if ( req ) {
                                        rc = sleep_on_req(req);
                                        dasd_free_request(req); /* request is no longer used */
                                } else {
                                        rc = -EINVAL;
                                }
                                if ( rc ) {
                                        printk (KERN_WARNING PRINTK_HEADER "Can't revalidate Track 0\n");
                                } else {

                                        printk (KERN_WARNING PRINTK_HEADER
                                                " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                                " ...revalidation done\n",
                                                devno, 
                                                irq, 
                                                device->name, 
                                                major_from_devindex (devindex), 
                                                devindex << DASD_PARTN_BITS);
                                }
                        }
                }

        } /* end if discipline->format_device */

        /* check if at least one format cp was build in discipline */
        if (!format_done) {
                rc = -EINVAL;
        }        

        if (!rc) {
                printk (KERN_WARNING PRINTK_HEADER
                        " devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                        " Formatting finished successfully\n",
                        devno, 
                        irq, 
                        device->name, 
                        major_from_devindex (devindex), 
                        devindex << DASD_PARTN_BITS);
        }

        /* 
         * re-analyse device if either no formatting was done 
         * (e.g. invalidation was not possible) or formatting was
         * successful.
         */
        if ((!format_done) ||
            (!rc         )   ) {
                
                dasd_set_device_level (device->devinfo.irq,
                                       DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
                                       device->discipline,
                                       0);
                udelay(1500000);
                
                dasd_set_device_level (device->devinfo.irq,
                                       DASD_DEVICE_LEVEL_ANALYSED,
                                       device->discipline,
                                       0);
        }
        
        spin_lock (&dasd_open_count_lock);
        device->open_count=1;
        spin_unlock (&dasd_open_count_lock);
	return rc;
} /* end dasd_format */

static int
do_dasd_ioctl (struct inode *inp, /* unsigned */ int no, unsigned long data)
{
	int rc = 0;
	int devindex = devindex_from_kdev_t (inp->i_rdev);
	dasd_device_t *device = find_dasd_device (devindex);
	major_info_t *major_info = major_info_from_devindex (devindex);
        
	if (!device) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as 0x%04x (%d)\n",
			inp->i_rdev, devindex);
		return -EINVAL;
	}
	if ((_IOC_DIR (no) != _IOC_NONE) && (data == 0)) {
		PRINT_DEBUG ("empty data ptr");
		return -EINVAL;
	}
#if 0
	printk (KERN_DEBUG PRINTK_HEADER
		"ioctl 0x%08x %s'0x%x'%d(%d) on /dev/%s (%d:%d,"
		" devno 0x%04X on irq %d) with data %8lx\n",
		no,
		_IOC_DIR (no) == _IOC_NONE ? "0" :
		_IOC_DIR (no) == _IOC_READ ? "r" :
		_IOC_DIR (no) == _IOC_WRITE ? "w" :
		_IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ? "rw" : "u",
		_IOC_TYPE (no), _IOC_NR (no), _IOC_SIZE (no),
		device->name, MAJOR (inp->i_rdev), MINOR (inp->i_rdev),
		device->devinfo.devno, device->devinfo.irq,
		data);
#endif
	switch (no) {
	case BLKGETSIZE:{	/* Return device size */
			int blocks = blk_size[MAJOR (inp->i_rdev)][MINOR (inp->i_rdev)] << 1;
			rc = copy_to_user ((long *) data, &blocks, sizeof (long));
			break;
		}
	case BLKFLSBUF:{
			rc = fsync_dev (inp->i_rdev);
			invalidate_buffers(inp->i_rdev);
			break;
		}
	case BLKRAGET:{
			rc = copy_to_user ((long *) data, read_ahead + MAJOR (inp->i_rdev), sizeof (long));
			break;
		}
	case BLKRASET:{
			rc = copy_from_user (read_ahead + MAJOR (inp->i_rdev), (long *) data, sizeof (long));
			break;
		}
	case BLKRRPART:{
			dasd_partn_detect (devindex);
			rc = 0;
			break;
		}
	case BLKGETBSZ:{
			rc = copy_to_user ((int *) data, &blksize_size[MAJOR (inp->i_rdev)][MINOR (inp->i_rdev)],
					   sizeof (int));
			break;
		}
	case HDIO_GETGEO:{
			struct hd_geometry geo =
			{0,};
			if (device->discipline->fill_geometry)
				device->discipline->fill_geometry (device, &geo);
			rc = copy_to_user ((struct hd_geometry *) data, &geo,
					   sizeof (struct hd_geometry));
			break;
		}
#if ! (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
		RO_IOCTLS (inp->i_rdev, data);
#endif /* LINUX_IS_24 */
	case BIODASDRSID:{
			rc = copy_to_user ((void *) data,
					   &(device->devinfo.sid_data),
					   sizeof (senseid_t));
			break;
		}
	case BIODASDRWTB:{
			int offset = 0;
			int xlt;

			rc = copy_from_user (&xlt, (void *) data,
					     sizeof (int));
			if (rc)
				break;
			offset = major_info->gendisk.part[MINOR (inp->i_rdev)].start_sect >>
			    device->sizes.s2b_shift;
			xlt += offset;
			rc = copy_to_user ((void *) data, &xlt,
					   sizeof (int));
			break;
		}
	case BIODASDFORMAT:{
			/* fdata == NULL is a valid arg to dasd_format ! */
			int partn;
                        format_data_t fdata = { 
                                DASD_FORMAT_DEFAULT_START_UNIT,
                                DASD_FORMAT_DEFAULT_STOP_UNIT,
                                DASD_FORMAT_DEFAULT_BLOCKSIZE,
                                DASD_FORMAT_DEFAULT_INTENSITY };
                        
			if (data) {
				rc = copy_from_user (&fdata, (void *) data,
						     sizeof (format_data_t));
				if (rc)
					break;
			}
			partn = MINOR (inp->i_rdev) & ((1 << major_info->gendisk.minor_shift) - 1);
			if (partn != 0) {
				printk (KERN_WARNING PRINTK_HEADER
					" devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
                                        " Cannot low-level format a partition\n",
					device->devinfo.devno, device->devinfo.irq, device->name,
                                        MAJOR (inp->i_rdev), MINOR (inp->i_rdev));
				return -EINVAL;
			}
			rc = dasd_format (device, &fdata);
			break;
		}
	case BIODASDEXCP:{
			printk (KERN_WARNING PRINTK_HEADER
				"Unsupported ioctl BIODASDEXCP\n");
                        break;
		}
        default:{
          printk (KERN_WARNING PRINTK_HEADER
                  "unknown ioctl 0x%08x %s'0x%x'%d(%d) on /dev/%s (%d:%d,"
                  " devno 0x%04X on irq %d) with data %8lx\n",
                  no,
                  _IOC_DIR (no) == _IOC_NONE ? "0" :
                  _IOC_DIR (no) == _IOC_READ ? "r" :
                  _IOC_DIR (no) == _IOC_WRITE ? "w" :
                  _IOC_DIR (no) == (_IOC_READ | _IOC_WRITE) ? "rw" : "u",
                  _IOC_TYPE (no), _IOC_NR (no), _IOC_SIZE (no),
                  device->name, MAJOR (inp->i_rdev), MINOR (inp->i_rdev),
                  device->devinfo.devno, device->devinfo.irq,
                  data);
          rc = -EINVAL;
          break;
		}
	}
	return rc;
}

/* SECTION: The members of the struct file_operations */

static int
dasd_ioctl (struct inode *inp, struct file *filp,
	    unsigned int no, unsigned long data)
{
	int rc = 0;
	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	rc = do_dasd_ioctl (inp, no, data);
	return rc;
}

static int
dasd_open (struct inode *inp, struct file *filp)
{
	int rc = 0;
	int devindex;
	int partn;
	dasd_device_t *device;
	major_info_t *major_info;

	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	if ( dasd_probeonly ) {
		printk ("\n" KERN_INFO PRINTK_HEADER "No access to device (%d:%d) due to probeonly mode\n",MAJOR(inp->i_rdev),MINOR(inp->i_rdev));
		return -EPERM;
	}
	devindex = devindex_from_kdev_t (inp->i_rdev);
	if (devindex < 0) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as %d\n", inp->i_rdev);
		return devindex;
	}
	device = find_dasd_device (devindex);
	major_info = major_info_from_devindex (devindex);
	partn = MINOR (inp->i_rdev) & ((1 << major_info->gendisk.minor_shift) - 1);
	if (device == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as %d\n", inp->i_rdev);
		return -EINVAL;
	}
	if (atomic_read (&device->level) < DASD_DEVICE_LEVEL_RECOGNIZED ||
	    device->discipline == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			" devno 0x%04X on subchannel %d = /dev/%s (%d:%d)"
			" Cannot open unrecognized device\n",
                        device->devinfo.devno, device->devinfo.irq, device->name,
			MAJOR (inp->i_rdev), MINOR (inp->i_rdev));
		return -EINVAL;
	}
        spin_lock (&dasd_open_count_lock);
        if (device->open_count == -1) {
            spin_unlock (&dasd_open_count_lock);
            return -EBUSY;
        }
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif				/* MODULE */
	device->open_count++;
        spin_unlock (&dasd_open_count_lock);
	return rc;
}

static int
dasd_release (struct inode *inp, struct file *filp)
{
	int rc = 0;
	dasd_device_t *device;
	int devindex;

	if ((!inp) || !(inp->i_rdev)) {
		return -EINVAL;
	}
	devindex = devindex_from_kdev_t (inp->i_rdev);
	device = find_dasd_device (devindex);
	if (device == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			"No device registered as %d:%d\n",
			MAJOR(inp->i_rdev),MINOR(inp->i_rdev));
		return -EINVAL;
	}
        spin_lock(&dasd_open_count_lock);
	if(device->open_count--) {
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif				/* MODULE */
	}
	if ( device->open_count == 0 ) {
		rc = fsync_dev (inp->i_rdev);
		invalidate_buffers(inp->i_rdev);
	}
        spin_unlock(&dasd_open_count_lock);
	return rc;
}

/* SECTION: All that stuff related to major numbers */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static struct
block_device_operations dasd_device_operations =
{
	ioctl:dasd_ioctl,
	open:dasd_open,
	release:dasd_release,
};
#else
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
#endif /* LINUX_IS_24 */

static major_info_t *
get_new_major_info (void)
{
	major_info_t *major_info = NULL;
	major_info = kmalloc (sizeof (major_info_t), GFP_KERNEL);
	if (major_info) {
                
		memset (major_info, 0, sizeof (major_info_t));
		major_info->read_ahead = 8;
		major_info->request_fn = do_dasd_request;
                
		major_info->gendisk.major_name = DASD_NAME;
		major_info->gendisk.minor_shift = DASD_PARTN_BITS;
		major_info->gendisk.max_p = 1 << DASD_PARTN_BITS;
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
		/* HSM changed for PL020234MVE */
		major_info->gendisk.max_nr= DASD_PER_MAJOR;
#endif /* LINUX_IS_24 */
		major_info->gendisk.nr_real=DASD_PER_MAJOR;
	}
	return major_info;
}

static int
dasd_register_major (major_info_t * major_info)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        request_queue_t *q;
#endif /* LINUX_IS_24 */
	int rc = 0;
	int major;
        int need_insert=0;

	if (major_info == NULL) {
		major_info = get_new_major_info ();
		if (!major_info) {
			printk (KERN_WARNING PRINTK_HEADER
				"Cannot get memory to allocate another major number\n");
			return -ENOMEM;
		} else {
			printk (KERN_INFO PRINTK_HEADER
				"Created another major number\n");
		}
                need_insert=1; //insert new major info into dasd_major_info later
	}
	major = major_info->gendisk.major;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	rc = devfs_register_blkdev (major, DASD_NAME, &dasd_device_operations);
#else
	rc = register_blkdev (major, DASD_NAME, &dasd_device_operations);
#endif /* LINUX_IS_24 */
	if (rc < 0) {
		printk (KERN_WARNING PRINTK_HEADER
		      "Cannot register to major no %d, rc = %d\n", major, rc);
		return rc;
	} else if (rc > 0) {
		if (major == 0) {
			major = rc;
			rc = 0;
		} else {
			printk (KERN_DEBUG PRINTK_HEADER
			    "Unknown condition when registering major number."
			  "Please report this line to Linux390@de.ibm.com\n");
		}
	} else {
		if (major == 0) {
			printk (KERN_DEBUG PRINTK_HEADER
				"Unknown condition when registering to dynamic major number."
			  "Please report this line to Linux390@de.ibm.com\n");

		}
	}
        /* Insert the new major info into dasd_major_info if needed */
        if (need_insert) {
            major_info_t *temp = dasd_major_info;
            while (temp->next)
                temp = temp->next;
            temp->next = major_info;
        }
        major_info->dasd_device = (dasd_device_t **) kmalloc( DASD_PER_MAJOR * sizeof(dasd_device_t*), 
                                                              GFP_ATOMIC);
        memset ( major_info->dasd_device ,0,DASD_PER_MAJOR * sizeof(dasd_device_t*));
        blk_size[major] = major_info->blk_size = 
                (int *) kmalloc( (1<<MINORBITS) * sizeof(int), GFP_ATOMIC);
        memset ( major_info->blk_size ,0,(1<<MINORBITS) * sizeof(int));
        blksize_size[major] = major_info->blksize_size = 
                (int *) kmalloc( (1<<MINORBITS) * sizeof(int), GFP_ATOMIC);
        memset ( major_info->blksize_size ,0,(1<<MINORBITS) * sizeof(int));
        hardsect_size[major] = major_info->hardsect_size = 
                (int *) kmalloc( (1<<MINORBITS) * sizeof(int), GFP_ATOMIC);
        memset ( major_info->hardsect_size ,0,(1<<MINORBITS) * sizeof(int));
        max_sectors[major] = major_info->max_sectors = 
                (int *) kmalloc( (1<<MINORBITS) * sizeof(int), GFP_ATOMIC);
        memset ( major_info->max_sectors ,0,(1<<MINORBITS) * sizeof(int));

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        q = BLK_DEFAULT_QUEUE (major);
        blk_init_queue (q, do_dasd_request);
        blk_queue_headactive (BLK_DEFAULT_QUEUE (major), 0);
#else
	blk_dev[major].request_fn = major_info->request_fn;
#endif /* LINUX_IS_24 */

        
	/* finally do the gendisk stuff */
        major_info->gendisk.part = kmalloc ((1 << MINORBITS) * 
                                            sizeof (struct hd_struct),
                                            GFP_ATOMIC);
        memset (major_info->gendisk.part,0,(1 << MINORBITS) * 
                sizeof (struct hd_struct));
	major_info->gendisk.major = major;
	major_info->gendisk.next = gendisk_head;
        major_info->gendisk.sizes = major_info->blk_size;
	gendisk_head = &major_info->gendisk;
	return major;
}

static int
dasd_unregister_major (major_info_t * major_info)
{
	int rc = 0;
	int major;
        struct gendisk *dd,*prev=NULL;

	if (major_info == NULL) {
                return -EINVAL;
	}
	major = major_info->gendisk.major;
	rc = unregister_blkdev (major, DASD_NAME);
	if (rc < 0) {
		printk (KERN_WARNING PRINTK_HEADER
                        "Cannot unregister from major no %d, rc = %d\n", major, rc);
		return rc;
	} 
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        blk_dev[major].request_fn = NULL;
#endif /* LINUX_IS_24 */
        blk_size[major]= NULL;
        blksize_size[major]= NULL;
        hardsect_size[major]= NULL;
        max_sectors[major]= NULL;

        kfree(major_info->dasd_device);
        kfree(major_info->blk_size);
        kfree(major_info->blksize_size);
        kfree(major_info->hardsect_size);
        kfree(major_info->max_sectors);
        kfree(major_info->gendisk.part);
        
	/* finally do the gendisk stuff */
        for (dd = gendisk_head; dd ; dd = dd -> next ) {
                if ( dd == &major_info->gendisk ) {
                        if ( prev )
                                prev->next = dd->next;
                        else 
                                gendisk_head = dd->next;
                        break;
                }
                prev = dd;
        }
        if ( dd == NULL ) {
                return -ENOENT;
        }
        if  ( major_info->gendisk.major > 128 )
                kfree(major_info);
	return rc;
}
/* SECTION: Management of device list */

/* This one is needed for naming 18000+ possible dasd devices */
int
dasd_device_name (char *str, int index, int partition, struct gendisk *hd)
{
	int len = 0;
        char first,second,third;

        if ( hd ) {
                index = devindex_from_kdev_t (MKDEV(hd->major,index<<hd->minor_shift));
        }
        third = index % 26;
        second = ((index-26) / 26) % 26;
	first = (((index-702) / 26) / 26) % 26;
        
	len = sprintf (str, "dasd");
	if (index>701) {
                len += sprintf (str + len, "%c", first + 'a' );
	}
	if (index>25) {
                len += sprintf (str + len, "%c", second + 'a' );
	}
        len += sprintf (str + len, "%c", third + 'a' );
	if (partition) {
		if (partition > 9) {
			return -EINVAL;
		} else {
			len += sprintf (str + len, "%d", partition);
		}
	}
	str[len] = '\0';
	return 0;
}

static void
dasd_not_oper_handler ( int irq, int status ) {
        dasd_device_t *device=NULL;
        major_info_t * major_info;
        int i,devno = -ENODEV;

        for ( major_info = dasd_major_info; major_info != NULL; major_info = major_info->next ) {
                for ( i = 0; i <= DASD_PER_MAJOR; i ++ ) {
                        device = major_info->dasd_device[i];
                        if ( device &&
                             device -> devinfo.irq == irq ) {
                                devno = device->devinfo.devno;
                                break;
                        }
                }
                if ( devno != -ENODEV )
                        break;
        }
        if ( devno < 0 ) {
          printk ( KERN_WARNING PRINTK_HEADER
                   "not_oper_handler called on irq %d no devno!\n", irq);
          return;
        }
        printk ( KERN_INFO PRINTK_HEADER
                 "not_oper_handler called on irq %d devno %04X\n", irq,devno);
	if ( device -> open_count != 0 ) {
		printk (KERN_ALERT PRINTK_HEADER
			"Device %04X detached has still been open. expect errors\n", devno);
	}
        dasd_set_device_level( irq, DASD_DEVICE_LEVEL_UNKNOWN, NULL, 0 );
}

static int
dasd_enable_single_volume ( int irq ) {
        int rc = 0;
        dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
                               NULL, 0);
	printk (KERN_INFO PRINTK_HEADER "waiting for response...\n");
        {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                static wait_queue_head_t wait_queue;
                init_waitqueue_head(&wait_queue);
#else
                static struct wait_queue *wait_queue = NULL;
#endif /* LINUX_IS_24 */
                interruptible_sleep_on_timeout (&wait_queue, (5 * HZ) >> 1 );
        }
        dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ANALYSED,
                               NULL, 0);
        return rc;
}

int
dasd_oper_handler ( int irq, devreg_t *devreg ) {
        int devno;
        int devindex;
        int rc;
        devno = get_devno_by_irq(irq);
        if ( devno == -ENODEV )
                return -ENODEV;
        do {
                devindex = devindex_from_devno(devno);
                if ( dasd_autodetect ) {
                        dasd_add_range(devno,0);
                } else {
                        return -ENODEV;
                }
        } while ( devindex == -ENODEV );
        rc = dasd_enable_single_volume(irq);
        return rc;
}

/* 
 * int
 * dasd_set_device_level (unsigned int irq, int desired_level,
 *                        dasd_discipline_t * discipline, int flags)
 */

static int
dasd_set_device_level (unsigned int irq, int desired_level,
		       dasd_discipline_t * discipline, int flags)
{
	int rc = 0;
	int devno;
	int devindex;
	dasd_device_t *device;
	int current_level;
	major_info_t *major_info = NULL;
	int i, minor;
	ccw_req_t *cqr = NULL;
        int ind;
        dasd_discipline_t *temp;
        struct gendisk *dd;

	devno = get_devno_by_irq (irq);
	if (devno < 0) {
		printk (KERN_WARNING PRINTK_HEADER " no device appears to be connected to SCH %d\n", irq);
		return -ENODEV;
	}
	devindex = devindex_from_devno (devno);
	if (devindex < 0) {
		printk (KERN_WARNING PRINTK_HEADER " device %d is not in list of known DASDs\n", irq);
		return -ENODEV;
	}
	device = find_dasd_device (devindex);
        while ( (major_info = major_info_from_devindex (devindex)) == NULL ) {
                if ((rc = dasd_register_major (major_info)) > 0) {
                        printk (KERN_INFO PRINTK_HEADER
                                "Registered successfully to another major number: %u\n", rc);
                } else {
                        printk (KERN_WARNING PRINTK_HEADER
                                "Couldn't register successfully to another major no\n");
                        return -ERANGE;
                }
        }
        ind = devindex & (DASD_PER_MAJOR-1);
        device = major_info->dasd_device[ind];
        if (!device) {		/* allocate device descriptor */
                device = kmalloc (sizeof (dasd_device_t), GFP_ATOMIC);
		if (!device) {
			printk (KERN_WARNING PRINTK_HEADER " No memory for device descriptor\n");
			goto nomem;
		}
		memset (device, 0, sizeof (dasd_device_t));
                major_info->dasd_device[ind] = device;
                dasd_device_name (device->name, devindex, 0,NULL);
	}
        device->kdev = MKDEV(major_info->gendisk.major,ind << DASD_PARTN_BITS);
        device->major_info = major_info;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        init_waitqueue_head(&device->wait_q);
#endif /* KERNEL_VERSION */
        init_timer(&device->timer);
        minor = MINOR(device->kdev);
	current_level = atomic_read (&device->level);
	if (desired_level > current_level) {
		switch (current_level) {
		case DASD_DEVICE_LEVEL_UNKNOWN:	/* Find a discipline */
			rc = get_dev_info_by_irq (irq, &device->devinfo);
			if (rc < 0) {
				break;
			}
                        for ( temp = dasd_disciplines; temp != NULL; temp = temp->next ) {
                                if ( discipline == NULL || temp == discipline ) {
                                        if (temp->id_check)
                                                if (temp->id_check (&device->devinfo))
                                                        continue;
                                        if (temp->check_characteristics) {
                                                if (temp->check_characteristics (device)) 
                                                        continue;
                                        }
                                        discipline = temp;
                                        break;
                                }
                        }
			if (discipline && !rc) {
				printk (KERN_INFO PRINTK_HEADER
					" devno 0x%04X on subchannel %d (%s) is /dev/%s (%d:%d)\n",
					devno, irq, discipline->name,
                                        device->name, major_from_devindex (devindex),
					(devindex % 64) << DASD_PARTN_BITS);
			} else {
				break;
			}
			device->discipline = discipline;
			if (device->discipline->int_handler) {
#ifdef CONFIG_DASD_DYNAMIC
                                s390_request_irq_special(irq, 
                                                         device->discipline->int_handler, 
                                                         dasd_not_oper_handler,
                                                         0, 
                                                         DASD_NAME, 
                                                         &device->dev_status);
#else /* !defined(CONFIG_DASD_DYNAMIC) */                             
                                request_irq(irq, 
                                            device->discipline->int_handler, 
                                            0, 
                                            DASD_NAME, 
                                            &device->dev_status);
#endif /* CONFIG_DASD_DYNAMIC */                             
			}
			atomic_compare_and_swap_debug (&device->level,
                                                       DASD_DEVICE_LEVEL_UNKNOWN,
                                                       DASD_DEVICE_LEVEL_RECOGNIZED);
			if (desired_level == DASD_DEVICE_LEVEL_RECOGNIZED)
				break;
		case DASD_DEVICE_LEVEL_RECOGNIZED:	/* Fallthrough ?? */
			if (device->discipline->init_analysis) {
				cqr = device->discipline->init_analysis (device);
                                if (cqr != NULL) {
                                        dasd_chanq_enq (&device->queue, cqr);
                                        if (device->discipline->start_IO) {
                                                long flags;
                                                s390irq_spin_lock_irqsave (irq, flags);
                                                device->discipline->start_IO (cqr);
                                                atomic_compare_and_swap_debug (&device->level,
                                                                               DASD_DEVICE_LEVEL_RECOGNIZED,
                                                                               DASD_DEVICE_LEVEL_ANALYSIS_PENDING);
                                                s390irq_spin_unlock_irqrestore (irq, flags);
                                        }
                                }
                        } else {
                                atomic_compare_and_swap_debug (& device->level,DASD_DEVICE_LEVEL_RECOGNIZED,
                                                               DASD_DEVICE_LEVEL_ANALYSIS_PREPARED);
                        }
			if (desired_level >= DASD_DEVICE_LEVEL_ANALYSIS_PENDING)
				break;
		case DASD_DEVICE_LEVEL_ANALYSIS_PENDING:	/* Fallthrough ?? */
			return -EAGAIN;
		case DASD_DEVICE_LEVEL_ANALYSIS_PREPARED:	/* Re-entering here ! */
                        if (device->discipline->do_analysis) 
                                if (device->discipline->do_analysis (device))
					return -ENODEV;
			switch (device->sizes.bp_block) {
			case 512:
			case 1024:
			case 2048:
			case 4096:
				break;
			default:
                                {
					printk (KERN_INFO PRINTK_HEADER
						"/dev/%s (devno 0x%04X): Detected invalid blocksize of %d bytes"
						" Did you format the drive?\n",
						device->name, devno, device->sizes.bp_block);
					return -EMEDIUMTYPE;
                                }
			}
			for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                                if (i == 0)
					major_info->blk_size[minor] = (device->sizes.blocks << device->sizes.s2b_shift) >> 1;
				else
					major_info->blk_size[minor + i] = 0;
				major_info->hardsect_size[minor + i] = device->sizes.bp_block;
				major_info->blksize_size[minor + i] = device->sizes.bp_block;
                                if (major_info->blksize_size[minor + i] < 1024 )
                                        major_info->blksize_size[minor + i] = 1024;
                                
				major_info->max_sectors[minor + i] = device->discipline->max_blocks << device->sizes.s2b_shift;	
			}
			atomic_compare_and_swap_debug (&device->level,
                                                       DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
                                                       DASD_DEVICE_LEVEL_ANALYSED);
                        dd = &major_info->gendisk;
                        dd->sizes[minor] = ( device->sizes.blocks << 
                                             device->sizes.s2b_shift) >> 1;
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
#ifndef MODULE
                        if ( flags & 0x80 )
#endif
#endif /* KERNEL_VERSION */
                                dasd_partn_detect(devindex);
			if (desired_level == DASD_DEVICE_LEVEL_ANALYSED)
				break;
		case DASD_DEVICE_LEVEL_ANALYSED:	/* Fallthrough ?? */

			break;
		default:
			printk (KERN_WARNING PRINTK_HEADER
				"Internal error in " __FILE__ " on line %d."
				" validate_dasd called from %p with "
				" desired_level = %d, current_level =%d"
				" Pls send this message and your System.map to"
				" linux390@de.ibm.com\n",
				__LINE__, __builtin_return_address (0),
				desired_level, current_level);
			break;
		}
	} else 	if (desired_level < current_level) {		/* donwgrade device status */
		switch (current_level) {
		case DASD_DEVICE_LEVEL_ANALYSED:	/* Fallthrough ?? */
			atomic_compare_and_swap_debug (&device->level,
                                                       DASD_DEVICE_LEVEL_ANALYSED,
                                                       DASD_DEVICE_LEVEL_ANALYSIS_PREPARED);
                        if (desired_level == DASD_DEVICE_LEVEL_ANALYSIS_PREPARED)
                                break;
		case DASD_DEVICE_LEVEL_ANALYSIS_PREPARED:
			for (i = 0; i < (1 << DASD_PARTN_BITS); i++) {
                                major_info->blk_size[minor] = 0;
                                major_info->hardsect_size[minor + i] = 0;
                                major_info->blksize_size[minor + i] = 0;
                                major_info->max_sectors[minor + i] = 0;
			}
                        memset( &device->sizes,0,sizeof(dasd_sizes_t));
                        atomic_compare_and_swap_debug (&device->level,
                                                       DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
                                                       DASD_DEVICE_LEVEL_ANALYSIS_PENDING);
			if (desired_level == DASD_DEVICE_LEVEL_ANALYSIS_PENDING)
				break;
		case DASD_DEVICE_LEVEL_ANALYSIS_PENDING:	/* Fallthrough ?? */
			atomic_compare_and_swap_debug (&device->level,
                                                       DASD_DEVICE_LEVEL_ANALYSIS_PENDING,
                                                       DASD_DEVICE_LEVEL_RECOGNIZED);
			if (desired_level == DASD_DEVICE_LEVEL_RECOGNIZED)
				break;
		case DASD_DEVICE_LEVEL_RECOGNIZED:	/* Fallthrough ?? */
                        if (device->discipline->int_handler) {
                                free_irq (irq, &device->dev_status);
			}
			device->discipline = NULL;
			atomic_compare_and_swap_debug (&device->level,
                                                       DASD_DEVICE_LEVEL_RECOGNIZED,
                                                       DASD_DEVICE_LEVEL_UNKNOWN);
			if (desired_level == DASD_DEVICE_LEVEL_UNKNOWN)
				break;
		case DASD_DEVICE_LEVEL_UNKNOWN:	
                        break;
		default:
			printk (KERN_WARNING PRINTK_HEADER
				"Internal error in " __FILE__ " on line %d."
				" validate_dasd called from %p with "
				" desired_level = %d, current_level =%d"
				" Pls send this message and your System.map to"
				" linux390@de.ibm.com\n",
				__LINE__, __builtin_return_address (0),
				desired_level, current_level);
			break;
		}
	}
	if (rc) {
		goto exit;
	}
      nomem:
	rc = -ENOMEM;
      exit:
	return 0;
}


/* SECTION: Procfs stuff */
typedef struct {
        char *data;
        int len;
} tempinfo_t;

void dasd_fill_inode (struct inode* inode, int fill) {
    if (fill)
        MOD_INC_USE_COUNT;
    else
        MOD_DEC_USE_COUNT;
}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
static struct proc_dir_entry *dasd_proc_root_entry = NULL;
#else
static struct proc_dir_entry dasd_proc_root_entry =
{
	low_ino:0,
	namelen:4,
	name:"dasd",
	mode:S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	nlink:1,
	uid:0,
	gid:0,
	size:0,
        fill_inode:dasd_fill_inode
};
#endif /* KERNEL_VERSION */
static struct proc_dir_entry* dasd_devices_entry;

	
static int
dasd_devices_open (struct inode* inode, struct file*  file )
{
	int rc = 0;
	int size = 0;
	int len = 0;
	major_info_t * temp = dasd_major_info;
        tempinfo_t *info;

	info = (tempinfo_t *)vmalloc(sizeof(tempinfo_t)); 
        if( info == NULL ) {
                printk ( KERN_WARNING "No memory available for data\n");
                return -ENOMEM;
	} else {
                file->private_data = (void *)info;
        }
        while ( temp ) {
                int i;
                for ( i = 0; i < 1 << (MINORBITS - DASD_PARTN_BITS); i ++ ) {
                        dasd_device_t *device = temp->dasd_device[i];
                        if ( device ) {
				size+=128;
			}
		}
		temp = temp->next;
	}
	temp = dasd_major_info;
	info->data=(char*)vmalloc(size); /* FIXME! determine space needed in a better way */
        if( size && info->data == NULL ) {
		printk ( KERN_WARNING "No memory available for data\n");
                vfree ( info );
		return -ENOMEM;
	}
	while ( temp ) {
		int i;
		for ( i = 0; i < 1 << (MINORBITS - DASD_PARTN_BITS); i ++ ) {
			dasd_device_t *device = temp->dasd_device[i];
			if ( device ) {
				len += sprintf ( info->data + len,
						 "%04X(%s) at (%d:%d) is %7s:",
						 device->devinfo.devno,
						 device->discipline ? device->discipline->name : "none",
						 temp->gendisk.major,i<<DASD_PARTN_BITS,
						 device->name);
                                switch ( atomic_read(&device->level) ) {
                                case DASD_DEVICE_LEVEL_UNKNOWN:
                                        len += sprintf ( info->data + len,"unknown\n");
                                        break;
                                case DASD_DEVICE_LEVEL_RECOGNIZED:
                                        len += sprintf ( info->data + len,"passive");
                                        len += sprintf ( info->data + len," at blocksize: %d, %d blocks, %d MB\n",
                                                         device->sizes.bp_block,
                                                         device->sizes.blocks,
                                                         ((device->sizes.bp_block>>9)*device->sizes.blocks)>>11);
                                        break;
                                case DASD_DEVICE_LEVEL_ANALYSIS_PENDING:
                                        len += sprintf ( info->data + len,"busy   \n");
                                        break;
				case DASD_DEVICE_LEVEL_ANALYSIS_PREPARED:
					len += sprintf ( info->data + len,"n/f    \n");
					break;
                                case DASD_DEVICE_LEVEL_ANALYSED:
                                        len += sprintf ( info->data + len,"active ");
                                        len += sprintf ( info->data + len," at blocksize: %d, %d blocks, %d MB\n",
                                                         device->sizes.bp_block,
                                                         device->sizes.blocks,
                                                         ((device->sizes.bp_block>>9)*device->sizes.blocks)>>11);
                                        break;
                                default:
                                        len += sprintf ( info->data + len,"no stat\n");
                                        break;
                                }
			}
		}
		temp = temp->next;
	}
        info->len=len;
#ifdef MODULE
        MOD_INC_USE_COUNT;
#endif
        return rc;
}

#define MIN(a,b) ((a)<(b)?(a):(b))

static ssize_t 
dasd_devices_read (  struct  file *file, char*   user_buf,  size_t  user_len, loff_t* offset )
{
        loff_t len;
        tempinfo_t* p_info = (tempinfo_t*)file->private_data;

        if(*offset >= p_info->len)
        {
                return 0; /* EOF */
        }
        else
        {
                len = MIN(user_len, (p_info->len - *offset));
                copy_to_user(user_buf, &(p_info->data[*offset]), len);
                (*offset) += len;
                return len;  /* number of bytes "read" */
        }
}

static ssize_t 
dasd_devices_write (  struct  file *file, const char*   user_buf,  size_t  user_len, loff_t* offset )
{
	char *buffer;

	if (user_len > PAGE_SIZE)
		return -EINVAL;

	buffer = vmalloc(user_len + 1);
        if ( buffer == NULL)
                return -ENOMEM;
        copy_from_user ( buffer, user_buf, user_len);
        buffer[user_len] = 0;
        printk ( KERN_INFO PRINTK_HEADER "Now executing %s\n",buffer);
	if ( ! strncmp(buffer,"add range",strlen("add_range"))) {

	} else if ( ! strncmp(buffer,"enable device",strlen("enable device"))) {

	} else if ( ! strncmp(buffer,"disable device",strlen("disable device"))) {

	} else {
		printk (KERN_WARNING PRINTK_HEADER "unknown command %s",
			buffer );
	}
        vfree(buffer);
        return user_len;
}

static int 
dasd_devices_close (struct inode* inode, struct file*  file)
{
        int rc = 0;
        tempinfo_t* p_info = (tempinfo_t*)file->private_data;
        if ( p_info ) {
                if ( p_info->data ) vfree(p_info->data);
                vfree(p_info);
        }
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
        return rc;
}


static struct file_operations dasd_devices_file_ops =
{
  read: dasd_devices_read,  /* read */
  write: dasd_devices_write,   /* write */
  open: dasd_devices_open,    /* open */
  release: dasd_devices_close,   /* close */
};

static struct inode_operations dasd_devices_inode_ops =
{
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
  default_file_ops: &dasd_devices_file_ops /* file ops */
#endif /* LINUX_IS_24 */
};

int
dasd_proc_init (void)
{
        int rc = 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        dasd_proc_root_entry = proc_mkdir("dasd",&proc_root);
        dasd_devices_entry = create_proc_entry("devices", 
                                               S_IFREG | S_IRUGO | S_IWUSR,
                                               dasd_proc_root_entry);
        dasd_devices_entry -> proc_fops = &dasd_devices_file_ops;
        dasd_devices_entry -> proc_iops = &dasd_devices_inode_ops;
#else
	proc_register (&proc_root, &dasd_proc_root_entry);
	dasd_devices_entry = (struct proc_dir_entry*)kmalloc(sizeof(struct proc_dir_entry), GFP_ATOMIC);
        if ( dasd_devices_entry) {
                memset(dasd_devices_entry, 0, sizeof(struct proc_dir_entry));
                dasd_devices_entry->name     = "devices";
                dasd_devices_entry->namelen  = strlen("devices");
                dasd_devices_entry->low_ino  = 0;
                dasd_devices_entry->mode     = (S_IFREG | S_IRUGO | S_IWUSR);
                dasd_devices_entry->nlink    = 1;
                dasd_devices_entry->uid      = 0;
                dasd_devices_entry->gid      = 0;
                dasd_devices_entry->size     = 0;
                dasd_devices_entry->get_info = NULL;
                dasd_devices_entry->ops      = &dasd_devices_inode_ops;
                proc_register(&dasd_proc_root_entry, dasd_devices_entry);
        }
#endif /* LINUX_IS_24 */
        return rc;
}

void
dasd_proc_cleanup (void)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        remove_proc_entry("devices", dasd_proc_root_entry);
        remove_proc_entry("dasd", &proc_root);
#else
        proc_unregister(&dasd_proc_root_entry, dasd_devices_entry->low_ino);
	kfree(dasd_devices_entry);
	proc_unregister (&proc_root, dasd_proc_root_entry.low_ino);
#endif /* LINUX_IS_24 */
}

/* SECTION: Initializing the driver */

int
dasd_init (void)
{
	int rc = 0;
	int irq;
	int j;
	major_info_t *major_info;
        dasd_range_t *range;

	printk (KERN_INFO PRINTK_HEADER "initializing...\n");
#ifndef MODULE
        dasd_split_parm_string(dasd_parm_string);
#endif /* ! MODULE */
        dasd_parse(dasd);

        dasd_init_emergency_req();
       
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        init_waitqueue_head(&watcher_queue);
        spin_lock_init(&cq_lock);
#endif /* LINUX_IS_24 */

	for (major_info = dasd_major_info; major_info; major_info = major_info->next) {
		if ((rc = dasd_register_major (major_info)) > 0) {
			printk (KERN_INFO PRINTK_HEADER
				"Registered successfully to major no %u\n", major_info->gendisk.major);
		} else {
			printk (KERN_WARNING PRINTK_HEADER
				"Couldn't register successfully to major no %d\n", major_info->gendisk.major);
                        /* revert registration of major infos */
                        goto major_failed;
		}
	}
        rc = dasd_proc_init();
        if ( rc ) {
                goto proc_failed;
        }

        genhd_dasd_name = dasd_device_name;
        
#ifdef CONFIG_DASD_ECKD
	rc = dasd_eckd_init ();
	if (rc==0)  {
		printk (KERN_INFO PRINTK_HEADER
			"Registered ECKD discipline successfully\n");
	} else {
                goto eckd_failed;
        }
#endif				/* CONFIG_DASD_ECKD */
#ifdef CONFIG_DASD_FBA
	rc = dasd_fba_init ();
	if (rc == 0) {
		printk (KERN_INFO PRINTK_HEADER
			"Registered FBA discipline successfully\n");
	} else {
                goto fba_failed;
	}
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_MDSK
        if ( MACHINE_IS_VM ) {
            rc = dasd_diag_init ();
            if (rc == 0)  {
		printk (KERN_INFO PRINTK_HEADER
			"Registered MDSK discipline successfully\n");
            } else {
                goto mdsk_failed;
            }
        }
#endif				/* CONFIG_DASD_MDSK */
	rc = 0;
	for (range = dasd_range_head; range; range= range->next) {
		for (j = range->from; j <= range->to; j++) {
			irq = get_irq_by_devno (j);
			if (irq >= 0)
				dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
						       NULL, 0);
		}
	}
        if ( dasd_autodetect ) {
                for ( irq = get_irq_first(); irq != -ENODEV; irq = get_irq_next(irq) ) {
                        int devno = get_devno_by_irq(irq);
                        int index = devindex_from_devno(devno);
                        if ( index == -ENODEV ) { /* not included in ranges */
                                dasd_add_range (devno,0);
				dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ANALYSIS_PREPARED,
						       NULL, 0);
                        }
                }
        }
	printk (KERN_INFO PRINTK_HEADER "waiting for responses...\n");
        {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                static wait_queue_head_t wait_queue;
                init_waitqueue_head(&wait_queue);
#else
                static struct wait_queue *wait_queue = NULL;
#endif /* LINUX_IS_24 */
                interruptible_sleep_on_timeout (&wait_queue,
                                                (20 * HZ) >> 1 );
        }
	for (range = dasd_range_head; range; range= range->next) {
		for (j = range->from; j <= range->to; j++) {
			irq = get_irq_by_devno (j);
			if (irq >= 0) {
				dasd_set_device_level (irq, DASD_DEVICE_LEVEL_ANALYSED,
						       NULL, 0);
                        }
		}
	}
        goto out;
#ifdef CONFIG_DASD_MDSK
 mdsk_failed:
	dasd_diag_cleanup ();
#endif				/* CONFIG_DASD_MDSK */
#ifdef CONFIG_DASD_FBA
 fba_failed:
	dasd_fba_cleanup ();
#endif				/* CONFIG_DASD_FBA */
#ifdef CONFIG_DASD_ECKD
 eckd_failed:
	dasd_eckd_cleanup ();
#endif				/* CONFIG_DASD_ECKD */
 proc_failed:
        dasd_proc_cleanup();
 major_failed: {
                for (major_info = dasd_major_info; 
                     major_info;
                     major_info = major_info->next) {
                        dasd_unregister_major(major_info);
                }
        }
        dasd_cleanup_emergency_req();
        printk (KERN_INFO PRINTK_HEADER "initialization not performed due to errors\n");
 out:
        printk (KERN_INFO PRINTK_HEADER "initialization finished\n");
        return rc;
}

void
cleanup_dasd (void)
{
        int j,rc=0;
        int irq;
        major_info_t *major_info;
        dasd_range_t *range,*next;
        dasd_devreg_t *reg;

	printk (KERN_INFO PRINTK_HEADER "shutting down\n");

        dasd_proc_cleanup();

	for (range = dasd_range_head; range; range= range->next) {
		for (j = range->from; j <= range->to; j++) {
			irq = get_irq_by_devno (j);
			if (irq >= 0) {
				dasd_set_device_level (irq, DASD_DEVICE_LEVEL_UNKNOWN,
						       NULL, 0);
                                kfree(find_dasd_device(devindex_from_devno(j)));
                        }
		}
	}
	for (major_info = dasd_major_info; major_info; major_info = major_info->next) {
		if ((rc = dasd_unregister_major (major_info)) == 0) {
			printk (KERN_INFO PRINTK_HEADER
				"Unregistered successfully from major no %u\n", major_info->gendisk.major);
		} else {
			printk (KERN_WARNING PRINTK_HEADER
				"Couldn't unregister successfully from major no %d rc = %d\n", major_info->gendisk.major,rc);
		}
	}
        dasd_cleanup_emergency_req();
        
        range = dasd_range_head;
        while ( range ) {
                next = range -> next;
                kfree (range);
                if ( next == NULL )
                        break;
                else
                        range = next;
        } 
        dasd_range_head = NULL;

        while ( dasd_devreg_head ) {
                reg = dasd_devreg_head->next;
                kfree ( dasd_devreg_head );
                dasd_devreg_head = reg;
        }
	printk (KERN_INFO PRINTK_HEADER "shutdown completed\n");
}

#ifdef MODULE
int
init_module ( void )
{
        return dasd_init(); 
}

void 
cleanup_module ( void ) 
{
        cleanup_dasd();
        return;
}
#endif

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
