/* 
   * File...........: linux/drivers/s390x/idals.c
   * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
   * Bugreports.to..: <Linux390@de.ibm.com>
   * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000a
   
   * History of changes
   * 07/24/00 new file
 */

#include <linux/malloc.h>
#include <asm/irq.h>
#include <asm/idals.h>
#include <asm/spinlock.h>

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif
#define PRINTK_HEADER "idals:"

/* a name template for the cache-names */
static char idal_name_template[] = "idalcache-\0\0\0\0"; /* fill name with zeroes! */
/* the cache's names */
static char idal_cache_name[IDAL_NUMBER_CACHES][sizeof(idal_name_template)+1]; 
/* the caches itself*/
static kmem_cache_t *idal_cache[IDAL_NUMBER_CACHES]; 

void
free_idal ( ccw1_t *cp )
{
	idaw_t *idal;
	unsigned long upper2k,lower2k;
	int nridaws,cacheind;
	if ( cp -> flags & CCW_FLAG_IDA ) {
		idal = cp -> cda;
		lower2k = *idal & 0xfffffffffffff800;
		upper2k = (*idal  + cp -> count - 1) & 0xfffffffffffff800;
		nridaws = ((upper2k - lower2k) >> 11) + 1;
		for ( cacheind = 0; (1 << cacheind) < nridaws ; cacheind ++ );
		kmem_cache_free ( idal_cache[cacheind], idal );
	}
}

int 
idal_support_init ( void ) 
{
	int rc=0;
	int cachind;
	
	for ( cachind = 0; cachind < IDAL_NUMBER_CACHES; cachind ++ ) {
		int slabsize = 8 << cachind;
		sprintf ( idal_cache_name[cachind], 
			  "%s%d%c", idal_name_template, slabsize, 0);
		idal_cache[cachind] = kmem_cache_create( idal_cache_name[cachind], 
							 slabsize, 0,
							 SLAB_HWCACHE_ALIGN | SLAB_DMA, 
							 NULL, NULL );
		if ( ! idal_cache [cachind] ) {
			printk (KERN_WARNING PRINTK_HEADER "Allocation of IDAL cache failed\n");
		}
	}
	
	return rc;
}

void idal_support_cleanup ( void )
{
	int cachind;

	/* Shrink the caches, if available */
	for ( cachind = 0; cachind < IDAL_NUMBER_CACHES; cachind ++ ) {
		if ( idal_cache[cachind] ) {
			if ( kmem_cache_shrink(idal_cache[cachind]) == 0 ) {
				idal_cache[cachind] = NULL;
			}
		}
	}

}
