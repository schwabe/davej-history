/* 
 * File...........: linux/drivers/s390/block/dasd_3990_erp.c
 * Author(s)......: Horst  Hummel    <Horst.Hummel@de.ibm.com> 
 *                  Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include <linux/blkdev.h>
#include <asm/ccwcache.h>
#include <asm/idals.h>
#include <asm/dasd.h>
#include <asm/s390io.h>
#include <linux/timer.h>
#include "dasd_eckd.h"
#include "dasd_3990_erp.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd_erp(3990): "


/*
 ***************************************************************************** 
 * SECTION ERP EXAMINATION
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_EXAMINE_24 
 *
 * DESCRIPTION
 *   Checks only for fatal (unrecoverable) error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 *   Each bit configuration leading to an action code 2 (Exit with
 *   programming error or unusual condition indication)
 *   are handled as fatal error´s.
 * 
 *   All other configurations are handled as recoverable errors.
 *
 * RETURN VALUES
 *   dasd_era_fatal     for all fatal (unrecoverable errors)
 *   dasd_era_recover   for all others.
 */
dasd_era_t
dasd_3990_erp_examine_24 (char *sense)
{

	/* check for 'Command Recejct' which is always a fatal error  */
	if (sense[0] & SNS0_CMD_REJECT) {
		if (sense[2] & SNS2_ENV_DATA_PRESENT) {
			return dasd_era_recover;
		} else {
			return dasd_era_fatal;
		}
	}
	/* check for 'Invalid Track Format'                           */
	if (sense[1] & SNS1_INV_TRACK_FORMAT) {
		if (sense[2] & SNS2_ENV_DATA_PRESENT) {
			return dasd_era_recover;
		} else {
			return dasd_era_fatal;
		}
	}
	/* check for 'No Record Found'                                */
	if (sense[1] & SNS1_NO_REC_FOUND) {
		return dasd_era_fatal;
	}
	/* return recoverable for all others                          */
	return dasd_era_recover;

} /* END dasd_3990_erp_examine_24 */

/*
 * DASD_3990_ERP_EXAMINE_32 
 *
 * DESCRIPTION
 *   Checks only for fatal/no/recoverable error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 * RETURN VALUES
 *   dasd_era_none      no error 
 *   dasd_era_fatal     for all fatal (unrecoverable errors)
 *   dasd_era_recover   for recoverable others.
 */
dasd_era_t
dasd_3990_erp_examine_32 (char *sense)
{

	switch (sense[25]) {
	case 0x00:
		return dasd_era_none;
	case 0x01:
		return dasd_era_fatal;
	default:
		return dasd_era_recover;
	}

}				/* end dasd_3990_erp_examine_32 */

/*
 * DASD_3990_ERP_EXAMINE 
 *
 * DESCRIPTION
 *   Checks only for fatal/no/recover error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 *   The logic is based on the 'IBM 3990 Storage Control  Reference' manual
 *   'Chapter 7. Error Recovery Procedures'.
 *
 * RETURN VALUES
 *   dasd_era_none      no error 
 *   dasd_era_fatal     for all fatal (unrecoverable errors)
 *   dasd_era_recover   for all others.
 */
dasd_era_t
dasd_3990_erp_examine (ccw_req_t * cqr, devstat_t * stat)
{

	char *sense = stat->ii.sense.data;

	/* check for successful execution first */
	if (stat->cstat == 0x00 &&
	    stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		return dasd_era_none;

	/* distinguish between 24 and 32 byte sense data */
	if (sense[27] & DASD_SENSE_BIT_0) {

		/* examine the 24 byte sense data */
		return dasd_3990_erp_examine_24 (sense);

	} else {

		/* examine the 32 byte sense data */
		return dasd_3990_erp_examine_32 (sense);

	} /* end distinguish between 24 and 32 byte sense data */

} /* END dasd_3990_erp_examine */

/*
 ***************************************************************************** 
 * SECTION ERP HANDLING
 ***************************************************************************** 
 */
/*
 ***************************************************************************** 
 * 24 and 32 byte sense ERP functions
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_BLOCK_QUEUE 
 *
 * DESCRIPTION
 *   Block the given device request queue to prevent from further
 *   processing until the started timer has expired or an related
 *   interrupt was received.
 *
 *  PARAMETER
 *   erp                request to be blocked
 *   expires            time to wait until restart (in seconds) 
 *
 * RETURN VALUES
 *   void               
 */
void
dasd_3990_erp_block_queue (ccw_req_t     *erp,
                           unsigned long expires)
{

	dasd_device_t *device = erp->device;

        DASD_MESSAGE (KERN_INFO, device,
                      "blocking request queue for %is",
                      (int) expires);

        atomic_compare_and_swap_debug (&erp->status,
                                       CQR_STATUS_ERROR,
                                       CQR_STATUS_PENDING);

        /* restart queue after some time */
        device->timer.function = dasd_3990_erp_restart_queue; 
        device->timer.data = (unsigned long) erp;
        device->timer.expires = jiffies + (expires * HZ);
        add_timer(&device->timer); 

        /* restart queue using int handler routing - just for testing - TDB */
        /* insert "void dasd_handle_state_change_pending (devstat_t *stat);" to dasd.h */
//        device->timer.function = dasd_handle_state_change_pending;
//        device->timer.data = (unsigned long) &device->dev_status;
//        device->timer.expires = jiffies + (expires * HZ);
//        add_timer(&device->timer); 

} /* end dasd_3990_erp_block_queue */ 

/*
 * DASD_3990_ERP_RESTART_QUEUE 
 *
 * DESCRIPTION
 *   Restarts request currently in status PENDING.
 *   This has to be done if either an related interrupt has received, or 
 *   a timer has expired.
 *   
 *
 *  PARAMETER
 *   erp                pointer to the PENDING ERP
 *
 * RETURN VALUES
 *   void               
 *
 */
void
dasd_3990_erp_restart_queue (unsigned long erp)
{
        ccw_req_t     *cqr    = (void *) erp;
	dasd_device_t *device = cqr->device;
	unsigned long flags;
        
        /* TBD delete */
        DASD_MESSAGE (KERN_INFO, device,
                      "%s irq %d erp %p",
                      "dasd_3990_erp_restart_queue entered",device->devinfo.irq, erp);
        /* get the needed locks to modify the request queue */
	s390irq_spin_lock_irqsave (device->devinfo.irq, 
                                   flags);

        /* 'restart' the device queue */
        if (atomic_read(&cqr->status) == CQR_STATUS_PENDING){
                
                DASD_MESSAGE (KERN_INFO, device,
                              "%s",
                              "request queue restarted by MIH");

                atomic_compare_and_swap_debug (&cqr->status,
                                               CQR_STATUS_PENDING,
                                               CQR_STATUS_QUEUED);
}

        /* release the lock */
        s390irq_spin_unlock_irqrestore (device->devinfo.irq, 
                                        flags);
        dasd_schedule_bh();

} /* end dasd_3990_erp_restart_queue */

/*
 * DASD_3990_ERP_ALTERNATE_PATH 
 *
 * DESCRIPTION
 *   Repeat the operation on a different channel path.
 *   If all alternate paths have been tried, the request is posted with a
 *   permanent error.
 *
 *  PARAMETER
 *   erp                pointer to the current ERP
 *
 * RETURN VALUES
 *   erp                modified pointer to the ERP
 *
 */
void
dasd_3990_erp_alternate_path (ccw_req_t *erp)
{

	dasd_device_t *device = erp->device;
        int irq = device->devinfo.irq;

	/* dissable current channel path - this causes the use of an other 
	   channel path if there is one.. */

	DASD_MESSAGE (KERN_WARNING, device,
                      "disable lpu %x",
                      erp->dstat->lpum);

        /* try alternate valid path */
        erp->lpm     &= ~(erp->dstat->lpum);
        erp->options |= DOIO_VALID_LPM;		/* use LPM for DO_IO */

	if ((erp->lpm & ioinfo[irq]->opm) != 0x00) {

		DASD_MESSAGE (KERN_WARNING, device,
			"try alternate lpm %x",
			erp->lpm);

		/* reset status to queued to handle the request again... */
		atomic_compare_and_swap_debug (&erp->status,
                                               CQR_STATUS_ERROR,
                                               CQR_STATUS_QUEUED);

                erp->retries = 1;
                
	} else {
         
                DASD_MESSAGE (KERN_WARNING, device,
                              "%s",      
                              "No alternate channel path left -> "
                              "permanent error");
                
                /* post request with permanent error */
                atomic_compare_and_swap_debug (&erp->status,
                                               CQR_STATUS_ERROR,
                                               CQR_STATUS_FAILED);

        }
        
} /* end dasd_3990_erp_alternate_path */

/*
 * DASD_3990_ERP_ACTION_1 
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 1 (see Reference manual).
 *   Repeat the operation on a different channel path.
 *   If all alternate paths have been tried, the request is posted with a
 *   permanent error.
 *   Note: duplex handling is not implemented (yet).
 *
 *  PARAMETER
 *   erp                pointer to the current ERP
 *
 * RETURN VALUES
 *   erp                pointer to the ERP
 *
 */
ccw_req_t *
dasd_3990_erp_action_1 (ccw_req_t * erp)
{
        
        erp->function = dasd_3990_erp_action_1;
        
 //       cpcmd ("TRACE IO 153 INST INT CCW", NULL, 0);  

        dasd_3990_erp_alternate_path (erp);

	return erp;

} /* end dasd_3990_erp_action_1 */

/*
 * DASD_3990_ERP_ACTION_4 
 *
 * DESCRIPTION
 *   Setup ERP to do the ERP action 4 (see Reference manual).
 *   Set the current request to PENDING to block the CQR queue for that device
 *   until the state change interrupt appears.
 *   Use a timer (20 seconds) to retry the cqr if the interrupt is still missing.
 *
 *  PARAMETER
 *   sense              sense data of the actual error
 *   erp                pointer to the current ERP
 *
 * RETURN VALUES
 *   erp                pointer to the ERP
 *
 */
ccw_req_t *
dasd_3990_erp_action_4 (ccw_req_t *erp,
			char      *sense)
{
	dasd_device_t *device = erp->device;

        /* first time set initial retry counter and erp_function    */
        /* and retry once without waiting for state change pending  */
        /* interrupt (this enables easier enqueing of the cqr)      */
        if (erp->function != dasd_3990_erp_action_4) {
                erp->retries  = 255; /* TBD 255 */
                erp->function = dasd_3990_erp_action_4;

        } else {

                if (sense[25] & 0x1D) {	/* state change pending */
                        
                        DASD_MESSAGE (KERN_WARNING, device,
                                      "%s",
                                      "waiting for state change pending "
                                      "int");
                        
                        dasd_3990_erp_block_queue (erp,
                                                   60);    /* TBD change to 30 */
                        
                } else {
                        /* no state change pending - retry */
                        printk (KERN_WARNING PRINTK_HEADER
                                "no state change pending - retry\n");
                        
                        atomic_compare_and_swap_debug (&erp->status,
                                                       CQR_STATUS_ERROR,
                                                       CQR_STATUS_QUEUED);
                }
        }

	return erp;

} /* end dasd_3990_erp_action_4 */

/*
 ***************************************************************************** 
 * 24 byte sense ERP functions (only)
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_EQUIP_CHECK
 *
 * DESCRIPTION
 *   Handles 24 byte 'Equipment Check' error.
 *
 * PARAMETER
 *   erp                current erp_head
 * RETURN VALUES
 *   erp                new erp_head - pointer to new ERP
 */
ccw_req_t *
dasd_3990_erp_equip_check (ccw_req_t * erp,
			   char *sense)
{
	dasd_device_t *device = erp->device;

	erp->function = dasd_3990_erp_equip_check;

	if (sense[2] & SNS2_ENV_DATA_PRESENT) {

		DASD_MESSAGE (KERN_WARNING, device,
                              "%s",
                              "Equipment Check - environmental data present");

		erp = dasd_3990_erp_action_4 (erp,
					      sense);
	}

	return erp;

} /* end dasd_3990_erp_equip_check */

/*
 * DASD_3990_ERP_DATA_CHECK
 *
 * DESCRIPTION
 *   Handles 24 byte 'Data Check' error.
 *
 * PARAMETER
 *   erp                current erp_head
 * RETURN VALUES
 *   erp                new erp_head - pointer to new ERP
 */
ccw_req_t *
dasd_3990_erp_data_check (ccw_req_t * erp,
			  char *sense)
{
	dasd_device_t *device = erp->device;

	erp->function = dasd_3990_erp_data_check;

	if (sense[2] & SNS2_ENV_DATA_PRESENT) {
                
		DASD_MESSAGE (KERN_WARNING, device,
                              "%s",
                              "Uncorrectable data check recovered secondary "
                              "addr of duplex pair");

		erp = dasd_3990_erp_action_4 (erp,
					      sense);
	}

	return erp;

} /* end dasd_3990_erp_data_check */


/*
 * DASD_3990_ERP_INV_FORMAT
 *
 * DESCRIPTION
 *   Handles 24 byte 'Invalid Track Format' error.
 *
 * PARAMETER
 *   erp                current erp_head
 * RETURN VALUES
 *   erp                new erp_head - pointer to new ERP
 */
ccw_req_t *
dasd_3990_erp_inv_format (ccw_req_t * erp,
			  char *sense)
{
	dasd_device_t *device = erp->device;

	erp->function = dasd_3990_erp_inv_format;

	if (sense[2] & SNS2_ENV_DATA_PRESENT) {

		DASD_MESSAGE (KERN_WARNING, device,
                              "%s",
                              "Track format error when destaging or "
                              "staging data");

		erp = dasd_3990_erp_action_4 (erp,
					      sense);
	}

	return erp;

} /* end dasd_3990_erp_inv_format */

/*
 * DASD_3990_ERP_ENV_DATA
 *
 * DESCRIPTION
 *   Handles 24 byte 'Environmental-Data Present' error.
 *
 * PARAMETER
 *   erp                current erp_head
 * RETURN VALUES
 *   erp                new erp_head - pointer to new ERP
 */
ccw_req_t *
dasd_3990_erp_env_data (ccw_req_t * erp,
			char *sense)
{
	dasd_device_t *device = erp->device;

	erp->function = dasd_3990_erp_env_data;

        DASD_MESSAGE (KERN_WARNING, device,
                      "%s",
                      "Environmental data present");

	erp = dasd_3990_erp_action_4 (erp,
				      sense);

	return erp;

}				/* end dasd_3990_erp_env_data */

/*
 * DASD_3990_ERP_INSPECT_24 
 *
 * DESCRIPTION
 *   Does a detailed inspection of the 24 byte sense data
 *   and sets up a related error recovery action.  
 *
 * PARAMETER
 *   sense              sense data of the actual error
 *   erp                pointer to the currently created default ERP
 *
 * RETURN VALUES
 *   erp                pointer to the (addtitional) ERP
 */
ccw_req_t *
dasd_3990_erp_inspect_24 ( ccw_req_t * erp,
                           char *sense)
{
	ccw_req_t *erp_filled = NULL;

	/* Check sense for ....    */
	/* 'Equipment Check'       */
	if ((erp_filled == NULL) &&
	    (sense[0] & SNS0_EQUIPMENT_CHECK)) {
		erp_filled = dasd_3990_erp_equip_check (erp,
							sense);
	}
	/* 'Data Check'            */
	if ((erp_filled == NULL) &&
	    (sense[0] & SNS0_DATA_CHECK)) {
		erp_filled = dasd_3990_erp_data_check (erp,
						       sense);
	}
	/* 'Invalid Track Format'  */
	if ((erp_filled == NULL) &&
	    (sense[1] & SNS1_INV_TRACK_FORMAT)) {
		erp_filled = dasd_3990_erp_inv_format (erp,
						       sense);
	}
	/* 'Environmental Data'    */
	if ((erp_filled == NULL) &&
	    (sense[2] & SNS2_ENV_DATA_PRESENT)) {
		erp_filled = dasd_3990_erp_env_data (erp,
						     sense);
	}

	/* other (unknown/not jet implemented) error - do default ERP */
	if (erp_filled == NULL) {

                printk (KERN_WARNING PRINTK_HEADER
                        "default ERP taken\n");

		erp_filled = erp;	
	}

	return erp_filled;

} /* END dasd_3990_erp_inspect_24 */

/*
 ***************************************************************************** 
 * 32 byte sense ERP functions (only)
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_ACTION_1B_32
 *
 * DESCRIPTION
 *   Handles 32 byte 'Action 1B' of Single Program Action Codes.
 *   A write operation could not be finished because of an unexpected 
 *   condition.
 *   The already created 'default erp' is used to get the link to 
 *   the erp chain, but it can not be used for this recovery 
 *   action because it contains no DE/LO data space.
 *
 * PARAMETER
 *   default_erp        already created default erp.
 *   sense              current sense data 
 * RETURN VALUES
 *   erp                new erp or default_erp in case of error
 */
ccw_req_t *
dasd_3990_erp_action_1B_32 (ccw_req_t *default_erp,
                            char      *sense)
{
	dasd_device_t  *device     = default_erp->device;
        ccw_req_t      *cqr;
	ccw_req_t      *erp;
        __u32          cpa;
	DE_eckd_data_t *DE_data;
	char           *LO_data;   /* LO_eckd_data_t */
        ccw1_t         *ccw;

	DASD_MESSAGE (KERN_WARNING, device,
                      "%s",
                      "Write not finsihed because of unexpected condition");
        
        /* determine the original cqr */
        cqr = default_erp; 
        while (cqr->refers != NULL){
                cqr = cqr->refers;
        }
         
        /* determine the address of the CCW to be restarted */
        cpa = default_erp->refers->dstat->cpa;
                
        if (cpa == 0) {
                cpa = (__u32) cqr->cpaddr;
        }
        
        if (cpa == 0) {
                
                DASD_MESSAGE (KERN_WARNING, device,
                              "%s",
                              "Unable to determine address of the CCW "
                              "to be restarted");
                
                atomic_compare_and_swap_debug (&default_erp->status,
                                               CQR_STATUS_FILLED,
                                               CQR_STATUS_FAILED);
                
                return default_erp;
        }
        
	/* Build new ERP request including DE/LO */
	erp = dasd_alloc_request ((char *) &cqr->magic,
                                  2 + 1,                    /* DE/LO + TIC */
                                  sizeof (DE_eckd_data_t) +
                                  sizeof (LO_eckd_data_t));
	if ( !erp ) {
                DASD_MESSAGE (KERN_WARNING, device,
                              "%s",
                              "Unable to allocate ERP");
                
                atomic_compare_and_swap_debug (&default_erp->status,
                                               CQR_STATUS_FILLED,
                                               CQR_STATUS_FAILED);
                
                return default_erp;
	}
        
        /* use original DE */
	DE_data = erp->data;
        memcpy (DE_data, 
                cqr->data, 
                sizeof (DE_eckd_data_t));
        
        /* create LO */
	LO_data = erp->data + sizeof (DE_eckd_data_t);
        
        if ((sense[3]  == 0x01) &&
            (LO_data[1] & 0x01)   ){
                DASD_MESSAGE (KERN_WARNING, device,
                              "%s",
                              "BUG - this should not happen");
                //BUG();    /* check for read count suffixing n.a. */
        }

        LO_data[0] = sense[7];  /* operation */
        LO_data[1] = sense[8];  /* auxiliary */
        LO_data[2] = sense[9];  
        LO_data[3] = sense[3];  /* count */ 
        LO_data[4] = sense[29]; /* seek_addr.cyl */
        LO_data[5] = sense[30]; /* seek_addr.cyl 2nd byte */
        LO_data[7] = sense[31]; /* seek_addr.head 2nd byte */  

        memcpy (&(LO_data[8]), &(sense[11]), 8);   

        /* create DE ccw */    
        ccw = erp->cpaddr;
	memset (ccw, 0, sizeof (ccw1_t));
	ccw->cmd_code = DASD_ECKD_CCW_DEFINE_EXTENT;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 16;
	ccw->cda = (__u32) __pa (DE_data);

        /* create LO ccw */    
        ccw++;
	memset (ccw, 0, sizeof (ccw1_t));
	ccw->cmd_code = DASD_ECKD_CCW_LOCATE_RECORD;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 16;
	ccw->cda = (__u32) __pa (LO_data);
        
        /* TIC to the failed ccw */
        ccw++;
	ccw->cmd_code = CCW_CMD_TIC;
	ccw->cda = cpa;

        /* fill erp related fields */
        erp->function = dasd_3990_erp_action_1B_32;
	erp->refers   = default_erp->refers;
	erp->device   = device;
	erp->magic    = default_erp->magic;
	erp->lpm      = 0xFF;
	erp->expires  = 0;
	erp->retries  = 2;

	atomic_set(&erp->status, 
                   CQR_STATUS_FILLED);
        
        /* remove the default erp */
        dasd_free_request (default_erp);
        
	return erp;
        
} /* end dasd_3990_erp_action_1B_32 */


/*
 * DASD_3990_ERP_INSPECT_32 
 *
 * DESCRIPTION
 *   Does a detailed inspection of the 32 byte sense data
 *   and sets up a related error recovery action.  
 *
 * PARAMETER
 *   sense              sense data of the actual error
 *   erp                pointer to the currently created default ERP
 *
 * RETURN VALUES
 *   erp_filled         pointer to the ERP
 *
 */
ccw_req_t *
dasd_3990_erp_inspect_32 ( ccw_req_t *erp,
                           char      *sense )
{
	dasd_device_t *device = erp->device;

	erp->function = dasd_3990_erp_inspect_32;

	if (sense[25] & DASD_SENSE_BIT_0) {

		/* compound program action codes (byte25 bit 0 == '1') */
                printk (KERN_WARNING PRINTK_HEADER
                        "default ERP taken\n");

	} else {

		/* single program action codes (byte25 bit 0 == '0') */
		switch (sense[25]) {

		case 0x1B:	/* unexpected condition during write */

                        erp = dasd_3990_erp_action_1B_32 (erp,
                                                          sense);
#ifdef ERP_DEBUG
                        /* TBD */
//                        cpcmd ("STOP", NULL, 0);
#endif
                        break;

		case 0x1D:	/* state-change pending */
                        DASD_MESSAGE (KERN_WARNING, device, 
                                      "%s",
                                      "A State change pending condition exists "
                                      "for the subsystem or device");

                        erp = dasd_3990_erp_action_4 (erp,
                                                      sense);
//                        cpcmd ("STOP", NULL, 0);
			break;

		default:	/* all others errors */
			printk (KERN_WARNING PRINTK_HEADER
			 "default ERP taken\n");
		}
	}

	return erp;

} /* end dasd_3990_erp_inspect_32 */

/*
 ***************************************************************************** 
 * main ERP control fuctions (24 and 32 byte sense)
 ***************************************************************************** 
 */

/*
 * DASD_3990_ERP_INSPECT
 *
 * DESCRIPTION
 *   Does a detailed inspection for sense data by calling either
 *   the 24-byte or the 32-byte inspection routine.
 *
 * PARAMETER
 *   erp                pointer to the currently created default ERP
 * RETURN VALUES
 *   erp_new            contens was possibly modified 
 */
ccw_req_t *
dasd_3990_erp_inspect (ccw_req_t * erp)
{
	ccw_req_t *erp_new = NULL;
	/* sense data are located in the refers record of the */
	/* already set up new ERP !                           */
	char *sense = erp->refers->dstat->ii.sense.data;

	/* distinguish between 24 and 32 byte sense data */
	if (sense[27] & DASD_SENSE_BIT_0) {

		/* inspect the 24 byte sense data */
		erp_new = dasd_3990_erp_inspect_24 (erp,
                                                    sense);

	} else {

		/* inspect the 32 byte sense data */
		erp_new = dasd_3990_erp_inspect_32 (erp,
                                                    sense);

	} /* end distinguish between 24 and 32 byte sense data */

	return erp_new;

} /* END dasd_3990_erp_inspect */

/*
 * DASD_3990_ERP_ADD_ERP
 * 
 * DESCRIPTION
 *   This funtion adds an additional request block (ERP) to the head of
 *   the given cqr (or erp).
 *   This erp is initialized as an default erp (retry TIC)
 *
 * PARAMETER
 *   cqr                head of the current ERP-chain (or single cqr if 
 *                      first error)
 * RETURN VALUES
 *   erp                pointer to new ERP-chain head
 */
ccw_req_t *
dasd_3990_erp_add_erp (ccw_req_t * cqr)
{
	/* allocate additional request block */
	ccw_req_t *erp = dasd_alloc_request ((char *) &cqr->magic, 1, 0);
	if ( !erp ) {
		printk( KERN_WARNING PRINTK_HEADER
			"unable to allocate ERP request\n" );
                return NULL;
	}

	/* initialize request with default TIC to current ERP/CQR */
	erp->cpaddr->cmd_code = CCW_CMD_TIC;
	erp->cpaddr->cda      = ((__u32) cqr->cpaddr);
	erp->function = dasd_3990_erp_add_erp;
	erp->refers   = cqr;
	erp->device   = cqr->device;
	erp->magic    = cqr->magic;
	erp->lpm      = 0xFF;
	erp->expires  = 0;
	erp->retries  = 255;

	atomic_set(&erp->status, 
                   CQR_STATUS_FILLED);

	return erp;
}

/*
 * DASD_3990_ERP_ADDITIONAL_ERP 
 * 
 * DESCRIPTION
 *   An additional ERP is needed to handle the current error.
 *   Add ERP to the head of the ERP-chain containing the ERP processing
 *   determined based on the sense data.
 *
 * PARAMETER
 *   cqr                head of the current ERP-chain (or single cqr if 
 *                      first error)
 *
 * RETURN VALUES
 *   erp                pointer to new ERP-chain head
 */
ccw_req_t *
dasd_3990_erp_additional_erp (ccw_req_t * cqr)
{

	ccw_req_t *erp = NULL;

	/* add erp and initialize with default TIC */
	erp = dasd_3990_erp_add_erp (cqr);

	/* inspect sense, determine specific ERP if possible */
	erp = dasd_3990_erp_inspect (erp);

	return erp;

}				/* end dasd_3990_erp_additional_erp */

/*
 * DASD_3990_ERP_ERROR_MATCH
 *
 * DESCRIPTION
 *   check if the the device status of the given cqr is the same.
 *   This means that the failed CCW and the relevant sense data
 *   must match.
 *   I don't distinguish between 24 and 32 byte sense becaus in case of
 *   24 byte sense byte 25 and 27 is set as well.
 *
 * PARAMETER
 *   cqr1               first cqr, which will be compared with the 
 *   cqr2               second cqr.
 *
 * RETURN VALUES
 *   match              'boolean' for match found
 *                      returns 1 if match found, otherwise 0.
 */
int
dasd_3990_erp_error_match (ccw_req_t * cqr1,
			   ccw_req_t * cqr2)
{
	/* check failed CCW */
	if (cqr1->dstat->cpa !=
	    cqr2->dstat->cpa) {
		return 0;	/* CCW doesn't match */

	}
	/* check sense data; byte 0-2,25,27 */
	if (!((strncmp (cqr1->dstat->ii.sense.data,
			cqr2->dstat->ii.sense.data,
			3) == 0) &&
	      (cqr1->dstat->ii.sense.data[27] ==
	       cqr2->dstat->ii.sense.data[27]   ) &&
	      (cqr1->dstat->ii.sense.data[25] ==
	       cqr2->dstat->ii.sense.data[25]   )   )) {

		return 0;	/* sense doesn't match */
	}
	return 1;		/* match */

}				/* end dasd_3990_erp_error_match */

/*
 * DASD_3990_ERP_IN_ERP
 *
 * DESCRIPTION
 *   check if the current error already happened before.
 *   quick exit if current cqr is not an ERP (cqr->refers=NULL)
 *
 * PARAMETER
 *   cqr                failed cqr (either original cqr or already an erp)
 *
 * RETURN VALUES
 *   erp                erp-pointer to the already defined error recovery procedure OR
 *                      NULL if a 'new' error occurred.
 */
ccw_req_t *
dasd_3990_erp_in_erp (ccw_req_t * cqr)
{
	ccw_req_t *erp_head = cqr,	/* save erp chain head */
	         *erp_match = NULL;	/* save erp chain head */
	int match = 0;		/* 'boolean' for matching error found */

	if (cqr->refers == NULL) {	/* return if not in erp */
		return NULL;
	}
	/* check the erp/cqr chain for current error */
	do {
		match = dasd_3990_erp_error_match (erp_head,
						   cqr->refers);
		erp_match = cqr;	/* save possible matching erp  */
		cqr = cqr->refers;	/* check next erp/cqr in queue */
	} while ((cqr->refers != NULL) &&
		 (match == 0));

	if (match) {
                /* TBD */
                printk(KERN_WARNING PRINTK_HEADER 
                       "dasd_3990_erp_in_erp: return matching erp\n");
		return erp_match;	/* retrun address of matching erp */
	} else {
                printk(KERN_WARNING PRINTK_HEADER 
                       "dasd_3990_erp_in_erp: return no match\n");
		return NULL;	/* return NULL to indicate that no match
				   was found */
	}

}				/* END dasd_3990_erp_in_erp */

/*
 * DASD_3990_ERP_FURTHER_ERP (24 & 32 byte sense)
 *
 * DESCRIPTION
 *   No retry is left for the current ERP. Check what has to be done 
 *   with the ERP.
 *     - do further defined ERP action or
 *     - wait for interrupt or  
 *     - exit with permanent error
 *
 * PARAMETER
 *   erp                ERP which is in progress wiht no retry left
 *
 * RETURN VALUES
 *   erp                modified/additional ERP
 */
ccw_req_t *
dasd_3990_erp_further_erp (ccw_req_t * erp)
{
        dasd_device_t     *device = erp->device;
        
        /* check for 24 byte sense ERP */
	if ((erp->function == dasd_3990_erp_action_1) ||
            (erp->function == dasd_3990_erp_action_4)   ){
                
                erp = dasd_3990_erp_action_1 (erp);
                
	} else {
                /* no retry left and no additional special handling necessary */
                DASD_MESSAGE (KERN_WARNING, device,
                              "no retries left for erp %p - "
                              "set status to FAILED",
                              erp);

		atomic_compare_and_swap_debug (&erp->status,
                                               CQR_STATUS_ERROR,
                                               CQR_STATUS_FAILED);
	}

	return erp; 

} /* end dasd_3990_erp_further_erp */

/*
 * DASD_3990_ERP_HANDLE_MATCH_ERP 
 *
 * DESCRIPTION
 *   An error occurred again and an ERP has been detected which is already
 *   used to handle this error (e.g. retries). 
 *   All prior ERP's are set to status DONE and the retry counter is
 *   decremented.
 *   If retry counter is already 0, it has to checked if further action
 *   is needed (besides retry) or if the ERP has failed.
 *
 * PARAMETER
 *   erp_head           first ERP in ERP-chain
 *   erp_match          ERP that handles the actual error.
 *
 * RETURN VALUES
 *   none                
 */
void
dasd_3990_erp_handle_match_erp (ccw_req_t * erp_head,
				ccw_req_t * erp_match)
{

	dasd_device_t *device   = erp_head->device;
	ccw_req_t     *erp_done = erp_head;

	/* loop over successful ERPs and remove them from chanq */
	while ((erp_done != erp_match) &&
	       (erp_done != NULL)) {

#ifdef ERP_DEBUG
		printk (KERN_WARNING PRINTK_HEADER
			"successful ERP - dequeue and free request %p\n",
			(void *) erp_done);
#endif
		atomic_compare_and_swap_debug (&erp_done->status,
                                               CQR_STATUS_ERROR,
                                               CQR_STATUS_DONE);

		/* remove the request from the device queue */
		dasd_chanq_deq (&device->queue,
				erp_done);

		/* free the finished erp request */
		dasd_free_request (erp_done);

		erp_done = erp_done->refers;
	}

	if (erp_done == NULL) 	/* erp_done should never be NULL! */
		panic (PRINTK_HEADER "Programming error in ERP! The original "
                       "request was lost\n");

#ifdef ERP_DEBUG
	/* handle matching ERP */
        printk (KERN_WARNING PRINTK_HEADER
                "handle matching erp %p\n",
                (void *) erp_done);
#endif
        
        if (erp_done->retries > 0) {
                
                /* check for special retries */
                if (erp_done->function == dasd_3990_erp_action_4) {
                        char *sense = erp_done->dstat->ii.sense.data;
                        erp_done = dasd_3990_erp_action_4 (erp_done,
                                                           sense);
                        
                } else {
                        /* simple retry   */
                        printk (KERN_WARNING PRINTK_HEADER
                                "%i retries left for erp %p\n",
                                erp_done->retries,
                                (void *) erp_done);
                        
                        /* handle the request again... */
                        atomic_compare_and_swap_debug (&erp_done->status,
                                                       CQR_STATUS_ERROR,
                                                       CQR_STATUS_QUEUED);
                }
        } else {
                /* no retry left - check for further necessary action    */
                /* if no further actions, handle rest as permanent error */
                erp_done = dasd_3990_erp_further_erp (erp_done);
	}

        erp_head = erp_done;

} /* end dasd_3990_erp_handle_match_erp */

/*
 * DASD_3990_ERP_ACTION
 *
 * DESCRIPTION
 *   controll routine for 3990 erp actions.
 *   Has to be called with the queue lock (namely the s390_irq_lock) acquired.
 *
 * PARAMETER
 *   cqr                failed cqr (either original cqr or already an erp)
 *
 * RETURN VALUES
 *   erp                erp-pointer to the head of the ERP action chain.
 *                      This means:
 *                       - either a ptr to an additional ERP cqr or
 *                       - the original given cqr (which's status might be modified)
 */
ccw_req_t *
dasd_3990_erp_action (ccw_req_t *cqr)
{
	ccw_req_t *erp = NULL;
	dasd_device_t *device = cqr->device;

#ifdef ERP_DEBUG 

	printk (KERN_WARNING PRINTK_HEADER
		"entering 3990 ERP for "
		"0x%04X on sch %d = /dev/%s \n",
		device->devinfo.devno,
		device->devinfo.irq,
		device->name);

	/* print current erp_chain */
        printk(KERN_WARNING PRINTK_HEADER 
               "ERP chain at BEGINNING of ERP-ACTION\n");
        {
                ccw_req_t *temp_erp = NULL;
                for (temp_erp = cqr; temp_erp != NULL; temp_erp = temp_erp->refers){
                        printk(KERN_WARNING PRINTK_HEADER 
                               "      erp %p refers to %p \n",
                               temp_erp,
                               temp_erp->refers);
                }
        } 
#endif

	/* double-check if current erp/cqr was successfull */
	if ((cqr->dstat->cstat == 0x00) &&
	    (cqr->dstat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))) {
                DASD_MESSAGE (KERN_WARNING, device,
                              "ERP called for successful request %p"
                              " - NO ERP necessary",
                              cqr);
                
                atomic_compare_and_swap_debug (&erp->status,
                                               CQR_STATUS_ERROR,
                                               CQR_STATUS_DONE);

		return cqr;
	}
	/* check if sense data are available */
	if (!cqr->dstat->ii.sense.data) {
		DASD_MESSAGE (KERN_WARNING, device,
			"ERP called witout sense data avail ..."
			"request %p - NO ERP possible",
			cqr);

                atomic_compare_and_swap_debug (&erp->status,
                                               CQR_STATUS_ERROR,
                                               CQR_STATUS_FAILED);

		return cqr; 

	}

	/* check if error happened before */
	erp = dasd_3990_erp_in_erp (cqr);

	if (erp == NULL) {
		/* no matching erp found - set up erp */
		erp = dasd_3990_erp_additional_erp (cqr);
	} else {
		/* matching erp found - set all leading erp's to DONE */
		dasd_3990_erp_handle_match_erp (cqr, erp);
		erp = cqr;
	}

#ifdef ERP_DEBUG
	/* print current erp_chain */
        printk(KERN_WARNING PRINTK_HEADER 
               "ERP chain at END of ERP-ACTION\n");
        {
                ccw_req_t *temp_erp = NULL;
                for (temp_erp = erp; temp_erp != NULL; temp_erp = temp_erp->refers){
                        printk(KERN_WARNING PRINTK_HEADER 
                               "      erp %p refers to %p \n",
                               temp_erp,
                               temp_erp->refers);
                }
        }
#endif

	return erp;

} /* end dasd_3990_erp_action */

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
