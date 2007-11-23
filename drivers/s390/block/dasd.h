ccw_req_t *default_erp_action (ccw_req_t *);
int dasd_start_IO (ccw_req_t * cqr);
void dasd_int_handler (int , void *, struct pt_regs *);
ccw_req_t * dasd_alloc_request (char *, int , int ) ;
void dasd_free_request(ccw_req_t *);
int (*genhd_dasd_name)(char*,int,int,struct gendisk*);
int dasd_oper_handler ( int irq, devreg_t *devreg );

dasd_era_t dasd_3370_erp_examine (ccw_req_t * cqr, devstat_t * stat);
dasd_era_t dasd_3990_erp_examine (ccw_req_t * cqr, devstat_t * stat);
dasd_era_t dasd_9336_erp_examine (ccw_req_t * cqr, devstat_t * stat);
dasd_era_t dasd_9343_erp_examine (ccw_req_t * cqr, devstat_t * stat);

