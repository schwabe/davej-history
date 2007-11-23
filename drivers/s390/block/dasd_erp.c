/* 
 * File...........: linux/drivers/s390/block/dasd_erp.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include <asm/irq.h>
#include <linux/dasd.h>
#include "dasd_erp.h"

#define PRINTK_HEADER "dasd_erp"

dasd_era_t
dasd_erp_examine (cqr_t * cqr, devstat_t * stat)
{
	int rc;
	if (stat->cstat == 0x00 &&
	    stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END)) {
		PRINT_WARN ("No error detected\n");
		rc = dasd_era_none;
	} else if (!(stat->flag & DEVSTAT_FLAG_SENSE_AVAIL)) {
		PRINT_WARN ("No sense data available, try to recover anyway\n");
		rc = dasd_era_recover;
	} else
#if DASD_PARANOIA > 1
	if (!dasd_disciplines[dasd_info[cqr->devindex]->type]->
		 erp_examine) {
		INTERNAL_CHECK ("No erp_examinator for dt=%d\n",
				dasd_info[cqr->devindex]->type);
		rc = dasd_era_fatal;
	} else
#endif
	{
		PRINT_WARN ("calling examinator\n");
		rc = dasd_disciplines[dasd_info[cqr->devindex]->type]->
			erp_examine (cqr, stat);
	}
	PRINT_WARN("ERP action code = %d\n",rc);
	return rc;
}

void
default_erp_action (erp_t * erp)
{
	cqr_t *cqr = erp->cqr.int4cqr;
	ccw1_t *cpa = request_cp(1,0);

	memset (cpa,0,sizeof(ccw1_t));

	cpa -> cmd_code = CCW_CMD_NOOP;
	
	((cqr_t *) erp)->cpaddr = cpa;
	if (cqr->retries++ <= 16) {
		ACS (cqr->status,
		     CQR_STATUS_ERP_PEND,
		     CQR_STATUS_QUEUED);
	} else {
		PRINT_WARN ("ERP retry count exceeded\n");
		ACS (cqr->status,
		     CQR_STATUS_ERP_PEND,
		     CQR_STATUS_FAILED);
	}
	atomic_set (&(((cqr_t *) erp)->status), CQR_STATUS_FILLED);
}

dasd_erp_action_t
dasd_erp_action (struct cqr_t *cqr)
{
	return default_erp_action;
}

dasd_erp_action_t
dasd_erp_postaction (struct erp_t * erp)
{
	return NULL;
}
