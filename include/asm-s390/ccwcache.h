/* 
   * File...........: linux/drivers/s390/block/ccwcache.c
   * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
   * Bugreports.to..: <Linux390@de.ibm.com>
   * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */
#ifndef CCWCACHE_H
#define CCWCACHE_H
#include <linux/slab.h>
#include <asm/irq.h>

#ifndef __KERNEL__
#define kmem_cache_t void
#endif /* __KERNEL__ */
/* 
 * smalles slab to be allocated (DASD_MINIMUM_CCW CCWs + metadata) 
 */

typedef struct ccw_req_t {
	/* these identify the request */
	unsigned long magic;	/* magic number */
	atomic_t status;	/* reflecting the status of this request */
	unsigned long long expires;	/* clock value, when request expires */

	/* these are important for executing the requests */
	ccw1_t *cpaddr;		/* address of channel program */
	void *data;		/* pointer to data area */
	void *req;		/* pointer to originating request */
	struct ccw_req_t *next;	/* pointer to next ccw_req_t in queue */

	int options;		/* options for execution */
	int cplength;		/* length of the channel program in CCWs */
	int datasize;		/* amount of additional data in bytes */
	devstat_t *dstat;	/* The device status in case of an error */

	/* these are important for recovering erroneous requests */
	int retries;		/* A retry counter to be set when filling */
	devstat_t *devstat;	/* Keeping the device status */
	struct ccw_req_t *refers;	/* Does this request refer to another one? */
	void *function; /* refers to the originating ERP action */ ;

	/* these are for profiling purposes */
	unsigned long long buildclk;	/* TOD-clock of request generation */
	unsigned long long startclk;	/* TOD-clock of request start */

	unsigned long long stopclk;	/* TOD-clock of request interrupt */
	unsigned long long endclk;	/* TOD-clock of request termination */

	/* these are for internal use */
	struct ccw_req_t *int_next;	/* for internal queueing */
	struct ccw_req_t *int_prev;	/* for internal queueing */
	kmem_cache_t *cache;	/* the cache this data comes from */
	void *device;		/* index of the device the req is for */

} __attribute__ ((packed,aligned(4))) ccw_req_t;

/* 
 * ccw_req_t -> status can be:
 */
#define CQR_STATUS_EMPTY  0x00	/* request is empty */
#define CQR_STATUS_FILLED 0x01	/* request is ready to be preocessed */
#define CQR_STATUS_QUEUED 0x02	/* request is queued to be processed */
#define CQR_STATUS_IN_IO  0x04	/* request is currently in IO */
#define CQR_STATUS_DONE   0x08	/* request is completed sucessfully */
#define CQR_STATUS_ERROR  0x10	/* request is completed with error */
#define CQR_STATUS_FAILED 0x20	/* request is finally failed */

#ifdef __KERNEL__
#define SMALLEST_SLAB (sizeof(struct ccw_req_t) <= 128 ? 128 :\
 sizeof(struct ccw_req_t) <= 256 ? 256 :\
 sizeof(struct ccw_req_t) <= 512 ? 512 :\
 sizeof(struct ccw_req_t) <= 1024 ? 1024 : 2048)

/* SMALLEST_SLAB(1),... PAGE_SIZE(CCW_NUMBER_CACHES) */
#define CCW_NUMBER_CACHES (sizeof(struct ccw_req_t) <= 128 ? 6 :\
 sizeof(struct ccw_req_t) <= 256 ? 5 :\
 sizeof(struct ccw_req_t) <= 512 ? 4 :\
 sizeof(struct ccw_req_t) <= 1024 ? 3 : 2)

int ccwcache_init (void);

ccw_req_t *ccw_alloc_request (char *magic, int cplength, int datasize);
void ccw_free_request (ccw_req_t * request);
#endif /* __KERNEL__ */
#endif				/* CCWCACHE_H */



