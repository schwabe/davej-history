/* 
   * File...........: linux/drivers/s390/ccwcache.c
   * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
   * Bugreports.to..: <Linux390@de.ibm.com>
   * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000a
   
   * History of changes
   * 07/10/00 moved ccwcache.h from linux/ to asm/
 */

#include <linux/module.h>
#include <linux/malloc.h>
#include <asm/debug.h>
#include <asm/ccwcache.h>

#include <asm/ebcdic.h>
#include <asm/spinlock.h>

#undef  PRINTK_HEADER
#define PRINTK_HEADER "ccwcache"

MODULE_AUTHOR ("Holger Smolinski <Holger.Smolinski@de.ibm.com>");
MODULE_DESCRIPTION ("Linux on S/390 CCW cache,"
		    "Copyright 2000 IBM Corporation");
EXPORT_SYMBOL(ccw_alloc_request);
EXPORT_SYMBOL(ccw_free_request);


/* pointer to list of allocated requests */
static ccw_req_t *ccwreq_actual = NULL;
static spinlock_t ccwchain_lock = SPIN_LOCK_UNLOCKED;

/* pointer to debug area */
static debug_info_t *debug_area = NULL;

/* SECTION: Handling of the dynamically allocated kmem slabs */

/* a name template for the cache-names */
static char ccw_name_template[] = "ccwcache-\0\0\0\0"; /* fill name with zeroes! */
/* the cache's names */
static char ccw_cache_name[CCW_NUMBER_CACHES][sizeof(ccw_name_template)+1]; 
/* the caches itself*/
static kmem_cache_t *ccw_cache[CCW_NUMBER_CACHES]; 

/* SECTION: (de)allocation of ccw_req_t */

/* 
 * void dechain ( ccw_req_t *request )
 * dechains the request from the ringbuffer
 */

static void 
dechain ( ccw_req_t *request )
{
	long flags;

	/* Sanity checks */
	if ( request == NULL ) {
		printk( KERN_WARNING PRINTK_HEADER
			"Trying to deallocate NULL request\n");
		return;
	}

	spin_lock_irqsave(&ccwchain_lock,flags);
	/* first deallocate request from list of allocates requests */
	if ( request -> int_next && request -> int_prev ) {
		if ( request -> int_next == request -> int_prev ) {
			ccwreq_actual = NULL;
		} else {
			if ( ccwreq_actual == request ) {
				ccwreq_actual = request->int_next;
			}
			request->int_prev->int_next = request->int_next;
			request->int_next->int_prev = request->int_prev;
		}
	} else if ( request -> int_next || request -> int_prev ) {
	}
	spin_unlock_irqrestore(&ccwchain_lock,flags);
	return;
}

/* 
 * ccw_req_t *ccw_alloc_request ( int cplength, int datasize )
 * allocates a ccw_req_t, that 
 * - can hold a CP of cplength CCWS
 * - can hold additional data up to datasize 
 */
ccw_req_t *
ccw_alloc_request ( char *magic, int cplength, int datasize )
{
	ccw_req_t * request = NULL;
	int cachind = 0;
	int size_needed = 0;
	long flags;

	debug_text_event ( debug_area, 1, "ALLC");
	if ( magic ) {
		debug_text_event ( debug_area, 1, magic);
	}
	debug_int_event ( debug_area, 1, cplength);
	debug_int_event ( debug_area, 1, datasize);

	/* Sanity checks */
	if ( cplength == 0 ) {
		printk (KERN_DEBUG PRINTK_HEADER
			"called ccw_alloc_request with cplength of 0 from %p\n",
			     __builtin_return_address(0));
		return NULL;
	}
	if ( (cplength << 3 ) > PAGE_SIZE ) {
		printk(KERN_WARNING PRINTK_HEADER
		       "Channel program to large to fit on one page\n");
		return NULL;
	}
	if ( datasize > PAGE_SIZE ) {
		printk(KERN_WARNING PRINTK_HEADER
		       "Data size to large to fit on one page\n");
		return NULL;
	}
	
	/* Try to keep things together in memory */
	if ( sizeof (ccw_req_t) + 
	     (((datasize + 7) >> 3) << 3) +  /* align upper end of data to 8 */
	     (cplength << 3 ) < PAGE_SIZE ) {
		/* All fit on one page, is preferred. */
		size_needed = sizeof (ccw_req_t) + datasize + (cplength << 3 );
	} else if ( sizeof (ccw_req_t) + (cplength << 3 ) < PAGE_SIZE ) {
		/* Try to keep CCWs with request */
		size_needed = sizeof (ccw_req_t) + (cplength << 3 );
	} else if ( sizeof (ccw_req_t) + datasize < PAGE_SIZE ) {
		/* Try to keep at least the data with request */
		size_needed = sizeof (ccw_req_t) + datasize;
	} else {
		/* Fall back to hold only the request in cache */
		size_needed = sizeof (ccw_req_t);
	}
	/* determine cache index for the requested size */
	for (cachind = 0; cachind < CCW_NUMBER_CACHES; cachind ++ ) {
	   if ( size_needed < (SMALLEST_SLAB << cachind) ) 
			break;
	}
	/* Try to fulfill the request from a cache */
	while (  cachind < CCW_NUMBER_CACHES ) { /* Now try to get an entry from a cache above or equal to cachind */
	  if ( ccw_cache[cachind] == NULL ){
	    printk(KERN_WARNING PRINTK_HEADER "NULL cache found! cache=%p index %d\n",ccw_cache[cachind],cachind);
	  }
	  request = kmem_cache_alloc ( ccw_cache[cachind], GFP_ATOMIC );
	  if ( request != NULL ) {
	    memset ( request, 0, (SMALLEST_SLAB << cachind));
	    request->cache = ccw_cache[cachind];
	    break;
	  } else {
	    printk (KERN_DEBUG PRINTK_HEADER "Proceeding to next cache\n");
	  }
 	  cachind++;
	} 
	/* if no success, fall back to kmalloc */
	if ( request == NULL ) {
	  printk (KERN_DEBUG PRINTK_HEADER "Falling back to kmalloc\n");
		request = kmalloc ( sizeof(ccw_req_t), GFP_ATOMIC );
		if ( request != NULL ) {
			memset ( request, 0, sizeof(ccw_req_t));
		}
	}
	/* Initialize request */
	if ( request == NULL ) {  
		printk(KERN_WARNING PRINTK_HEADER "Couldn't allocate request\n");
	} else {
		if ( request -> cache != NULL ) {
			/* Three cases when coming from a cache */
			if ( sizeof (ccw_req_t) + 
			     (((datasize + 7) >> 3) << 3) +
			     (cplength << 3 ) < 
			     PAGE_SIZE ) {
				request->data= (void*)(request + 1);
				request->cpaddr= (ccw1_t *)(((((long)(request + 1))+ 
							     datasize + 7)
							    >> 3) << 3);
			} else if ( sizeof (ccw_req_t) + (cplength << 3 ) < 
				    PAGE_SIZE ) {
				request->cpaddr=(ccw1_t *)(((((long)(request)) + 
							     sizeof(ccw_req_t) + 7)
							    >> 3) << 3);
			} else if ( sizeof (ccw_req_t) + datasize < PAGE_SIZE ) {
				request->data= (void *)(request + 1);
			} else {} 
		}
		/* Have the rest be done by kmalloc */
		if ( request -> cpaddr == NULL ) {
			printk(KERN_DEBUG PRINTK_HEADER "Falling back to kmalloc for CCW area\n");
			request->cpaddr=(ccw1_t *) kmalloc(sizeof(ccw1_t) * cplength,
							  GFP_ATOMIC);
			if ( request -> cpaddr == NULL ) {
				printk(KERN_WARNING PRINTK_HEADER "Couldn't allocate ccw area\n");
				ccw_free_request(request);
				return NULL;
			}
		}
		if ( request -> data == NULL ) {
			printk(KERN_DEBUG PRINTK_HEADER"Falling back to kmalloc for data area\n");
			request->data=(void *)kmalloc(datasize, GFP_ATOMIC);
			if ( request -> data == NULL ) {
				printk(KERN_WARNING PRINTK_HEADER "Couldn't allocate data area\n");
				ccw_free_request(request);
				return NULL;
			}
		}
		memset ( request->data,0,datasize );
		memset ( request->cpaddr,0,cplength*sizeof(ccw1_t) );
		if ( magic ) {
			strncpy ( (char *)(&request->magic), magic, 4);
		}
		else {
			strncpy ( (char *)(&request->magic), "CCWC", 4);
		}
		ASCEBC((char *)(&request->magic),4);
		request -> cplength = cplength;
		request -> datasize = datasize;
		/* enqueue request to list of allocated requests */
		spin_lock_irqsave(&ccwchain_lock,flags);
		if ( ccwreq_actual == NULL ) { /* queue empty */
			ccwreq_actual = request;
			request->int_prev = ccwreq_actual;
			request->int_next = ccwreq_actual;
		} else {
			request->int_next = ccwreq_actual;
			request->int_prev = ccwreq_actual->int_prev;
			request->int_prev->int_next = request;
			request->int_next->int_prev = request;
		}
		spin_unlock_irqrestore(&ccwchain_lock,flags);
	}
	debug_int_event ( debug_area, 1, (long)request);
	return request;
}

/* 
 * void ccw_free_request ( ccw_req_t * )
 * deallocates the ccw_req_t, given as argument
 */

void
ccw_free_request ( ccw_req_t * request )
{
	int cachind;
	int slabsize;

	debug_text_event ( debug_area, 1, "FREE");
	debug_int_event ( debug_area, 1, (long)request);
	/* Sanity checks */
	if ( request == NULL ) {
		printk(KERN_DEBUG PRINTK_HEADER"Trying to deallocate NULL request\n");
		return;
	}
	if ( request -> cache == NULL ) {
		printk (KERN_WARNING PRINTK_HEADER "Deallocating uncached request\n");
		if ( request -> data ) {
			kfree ( request -> data );
		}
		if ( request ->cpaddr ) {
			kfree ( request -> cpaddr );
		}
		dechain (request);
		kfree ( request );
	} else {
		/* Find which area has been allocated by kmalloc */
		for (cachind = 0; cachind < CCW_NUMBER_CACHES; cachind ++ )
			if ( request->cache == ccw_cache[cachind] )
				break;
		if ( request->cache != ccw_cache[cachind] ) {
			printk(KERN_WARNING PRINTK_HEADER"Cannot find cache in list\n");
		}
		slabsize = SMALLEST_SLAB << cachind;
		if ( request -> data &&
		     (((unsigned long)(request -> data) >
		       ((unsigned long)request) + slabsize) ||
		      ((unsigned long)(request -> data) < 
		       ((unsigned long)request)))) {
			kfree ( request -> data );
		}
		if ( request ->cpaddr &&
		     (((unsigned long)(request -> cpaddr) >
		       ((unsigned long)request) + slabsize) ||
		      (unsigned long)(request -> cpaddr) < 
		      ((unsigned long)request))) {
			kfree ( request -> cpaddr );
		}
		dechain ( request);
		kmem_cache_free(request -> cache, request);
	}
}

/* SECTION: initialization and cleanup functions */

/* 
 * ccwcache_init
 * called as an initializer function for the ccw memory management
 */

int
ccwcache_init (void)
{
	int rc = 0;
	int cachind;

	/* allocate a debug area */
	debug_area = debug_register( "ccwcache", 2, 4,4);
	if ( ! debug_area ) {
		printk ( KERN_WARNING PRINTK_HEADER"cannot allocate debug area\n" );
	} else {
		printk (KERN_DEBUG PRINTK_HEADER "debug area is 0x%8p\n", debug_area );
	}
        debug_register_view(debug_area,&debug_hex_ascii_view);
	debug_text_event ( debug_area, 0, "INIT");
	
	/* First allocate the kmem caches */
	for ( cachind = 0; cachind < CCW_NUMBER_CACHES; cachind ++ ) {
		int slabsize = SMALLEST_SLAB << cachind;
		debug_text_event ( debug_area, 1, "allc");
		debug_int_event ( debug_area, 1, slabsize);
		sprintf ( ccw_cache_name[cachind], 
			  "%s%d%c", ccw_name_template, slabsize, 0);
		ccw_cache[cachind] = kmem_cache_create( ccw_cache_name[cachind], 
							slabsize, 0,
							SLAB_HWCACHE_ALIGN, 
							NULL, NULL );
		debug_int_event ( debug_area, 1, (long)ccw_cache[cachind]);
		if ( ! ccw_cache [cachind] ) {
			printk (KERN_WARNING PRINTK_HEADER "Allocation of CCW cache failed\n");
		}
	}
	return rc;
}

/* 
 * ccwcache_cleanup
 * called as a cleanup function for the ccw memory management
 */

void
ccwcache_cleanup (void)
{
	int cachind;

	/* Shrink the caches, if available */
	for ( cachind = 0; cachind < CCW_NUMBER_CACHES; cachind ++ ) {
		if ( ccw_cache[cachind] ) {
			if ( kmem_cache_shrink(ccw_cache[cachind]) == 0 ) {
				ccw_cache[cachind] = NULL;
			}
		}
	}
	debug_unregister( debug_area );
}

#ifdef MODULE
int
init_module ( void )
{
	int rc;
	int i;
	rc = ccwcache_init();
	for ( i = 0; i < 200; i ++) {
		PRINT_INFO ("allocated %p\n",ccw_alloc_request("test",i,200-i));
	}
	return rc;
}

void
cleanup_module ( void )
{
	while ( ccwreq_actual ) {
		PRINT_INFO ("freeing %p\n",ccwreq_actual -> int_next);
		ccw_free_request( ccwreq_actual -> int_next );
		if ( ccwreq_actual ) {
			PRINT_INFO ("freeing %p\n",ccwreq_actual -> int_prev);
			ccw_free_request( ccwreq_actual -> int_prev );
		}
		if ( ccwreq_actual ) {
			PRINT_INFO ("freeing %p\n",ccwreq_actual);
			ccw_free_request( ccwreq_actual);
		}
	}
	ccwcache_cleanup();
}
#endif /* MODULE */

