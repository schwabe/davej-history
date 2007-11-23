/* 
 * File...........: linux/drivers/s390/block/dasd_eckd_erp.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include <linux/dasd.h>
#include "dasd_erp.h"

#define PRINTK_HEADER "dasd_erp(eckd)"

dasd_era_t
dasd_eckd_erp_examine (cqr_t * cqr, devstat_t * stat)
{

	if (stat->cstat == 0x00 &&
	    stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		return dasd_era_none;

	switch (dasd_info[cqr->devindex]->info.sid_data.cu_model) {
	case 0x3990:
		return dasd_3990_erp_examine (cqr, stat);
	case 0x9343:
		return dasd_9343_erp_examine (cqr, stat);
	default:
		return dasd_era_recover;
	}
}
