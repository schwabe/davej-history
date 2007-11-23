/*
 *  arch/s390/kernel/s390dyn.c
 *   S/390 dynamic device attachment
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp_lock.h>

#include <asm/irq.h>
#include <asm/s390io.h>
#include <asm/s390dyn.h>

static devreg_t   *devreg_anchor = NULL;
static spinlock_t  dyn_lock      = SPIN_LOCK_UNLOCKED;

int s390_device_register( devreg_t *drinfo )
{
	unsigned long  flags;

	int            ret     = 0;
	devreg_t      *pdevreg = devreg_anchor;

	if ( drinfo == NULL )
		return( -EINVAL );

	spin_lock_irqsave( &dyn_lock, flags ); 	

	while ( (pdevreg != NULL) && (ret ==0) )
	{
		if ( pdevreg == drinfo )
		{
			ret = -EINVAL;
		}
		else
		{
			if (    (    (pdevreg->flag & DEVREG_TYPE_DEVNO)
			          && (pdevreg->ci.devno                ) )
			     && (    (drinfo->flag & DEVREG_TYPE_DEVNO )
			          && (drinfo->ci.devno                 ) ) )
			{
				ret = -EBUSY;
			}           	
			else if (    (pdevreg->flag & DEVREG_EXACT_MATCH)
			          && (drinfo->flag & DEVREG_EXACT_MATCH ) )
			{
				if ( memcmp( &pdevreg->ci.hc, &drinfo->ci.hc, 6) )
					ret = -EBUSY;
			}      				
			else if (    (pdevreg->flag & DEVREG_MATCH_DEV_TYPE)
			          && (drinfo->flag & DEVREG_MATCH_DEV_TYPE ) )
			{
				if (   (pdevreg->ci.hc.dtype == drinfo->ci.hc.dtype)
				    && (pdevreg->ci.hc.dmode == drinfo->ci.hc.dmode) )
					ret = -EBUSY;
			}      				
			else if (    (pdevreg->flag & DEVREG_MATCH_CU_TYPE)
			          && (drinfo->flag & DEVREG_MATCH_CU_TYPE ) )
			{
				if (   (pdevreg->ci.hc.ctype == drinfo->ci.hc.ctype)
				    && (pdevreg->ci.hc.cmode == drinfo->ci.hc.cmode) )
					ret = -EBUSY;
			}      				
			else if (    (pdevreg->flag & DEVREG_NO_CU_INFO)
			          && (drinfo->flag & DEVREG_NO_CU_INFO ) )
			{
				if (   (pdevreg->ci.hnc.dtype == drinfo->ci.hnc.dtype)
				    && (pdevreg->ci.hnc.dmode == drinfo->ci.hnc.dmode) )
					ret = -EBUSY;
			}      				

			pdevreg = pdevreg->next;

		} /* endif */

	} /* endwhile */          	

	/*
	 * only enqueue if no collision was found ...	
	 */	
   if ( ret == 0 )
	{
		drinfo->next = devreg_anchor;
		drinfo->prev = NULL;

		if ( devreg_anchor != NULL )
		{
			devreg_anchor->prev = drinfo;       	

		} /* endif */

	} /* endif */

	spin_unlock_irqrestore( &dyn_lock, flags ); 	
 	
	return( ret);
}


int s390_device_unregister( devreg_t *dreg )
{
	unsigned long  flags;

	int            ret     = -EINVAL;
	devreg_t      *pdevreg = devreg_anchor;

	if ( dreg == NULL )
		return( -EINVAL );

	spin_lock_irqsave( &dyn_lock, flags ); 	

	while (    (pdevreg != NULL )
	        && (    ret != 0    ) )
	{
		if ( pdevreg == dreg )
		{
			devreg_t *dprev = pdevreg->prev;
			devreg_t *dnext = pdevreg->next;

			if ( (dprev != NULL) && (dnext != NULL) )
			{
				dnext->prev = dprev;
				dprev->next = dnext;
			}
			if ( (dprev != NULL) && (dnext == NULL) )
			{
				dprev->next = NULL;
			}
			if ( (dprev == NULL) && (dnext != NULL) )
			{
				dnext->prev = NULL;

			} /* else */

			ret = 0;
		}
		else
		{
			pdevreg = pdevreg->next;

		} /* endif */

	} /* endwhile */          	

	spin_unlock_irqrestore( &dyn_lock, flags ); 	
 	
	return( ret);
}


devreg_t * s390_search_devreg( ioinfo_t *ioinfo )
{
	unsigned long  flags;

	devreg_t *pdevreg = devreg_anchor;

	if ( ioinfo == NULL )
		return( NULL );

	spin_lock_irqsave( &dyn_lock, flags ); 	

	while ( pdevreg != NULL )
	{
		if (    (pdevreg->flag & DEVREG_TYPE_DEVNO )
		     && (ioinfo->ui.flags.dval == 1        )
		     && (ioinfo->devno == pdevreg->ci.devno) )
		{
			break;
		}           	
		else if ( pdevreg->flag & DEVREG_EXACT_MATCH )
		{
			if ( memcmp( &pdevreg->ci.hc,
			             &ioinfo->senseid.cu_type, 6 ) )
				break;
		}      				
		else if ( pdevreg->flag & DEVREG_MATCH_DEV_TYPE )
		{
			if (   (pdevreg->ci.hc.dtype == ioinfo->senseid.dev_type )
			    && (pdevreg->ci.hc.dmode == ioinfo->senseid.dev_model) )
				break;
		}      				
		else if ( pdevreg->flag & DEVREG_MATCH_CU_TYPE )
		{
			if (   (pdevreg->ci.hc.ctype == ioinfo->senseid.cu_type )
			    && (pdevreg->ci.hc.cmode == ioinfo->senseid.cu_model) )
				break;
		}      				
		else if ( pdevreg->flag & DEVREG_NO_CU_INFO )
		{
			if (   (pdevreg->ci.hnc.dtype == ioinfo->senseid.dev_type )
			    && (pdevreg->ci.hnc.dmode == ioinfo->senseid.dev_model) )
				break;
		}      				

		pdevreg = pdevreg->next;

	} /* endwhile */          	

	spin_unlock_irqrestore( &dyn_lock, flags ); 	
 	
	return( pdevreg);
}

EXPORT_SYMBOL(s390_device_register);
EXPORT_SYMBOL(s390_device_unregister);
