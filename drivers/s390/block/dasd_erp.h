/* 
 * File...........: linux/drivers/s390/block/dasd_erp.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#ifndef DASD_ERP_H
#define DASD_ERP_H

typedef enum {
	dasd_era_fatal = -1,	/* no chance to recover              */
	dasd_era_none = 0,	/* don't recover, everything alright */
	dasd_era_msg = 1,	/* don't recover, just report...     */
	dasd_era_recover = 2	/* recovery action recommended       */
} dasd_era_t;

#include "dasd_types.h"

typedef struct erp_t {
	struct cqr_t cqr;
} __attribute__ ((packed)) 

erp_t;

typedef void (*dasd_erp_action_t) (erp_t *);

dasd_era_t dasd_erp_examine (struct cqr_t *, devstat_t *);
dasd_erp_action_t dasd_erp_action (struct cqr_t *);
dasd_erp_action_t dasd_erp_postaction (struct erp_t *);

#endif				/* DASD_ERP_H */
