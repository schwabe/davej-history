ccw_req_t *default_erp_action (ccw_req_t *);
int default_erp_postaction ( ccw_req_t *, int);
int dasd_start_IO (ccw_req_t * cqr);
void dasd_int_handler (int , void *, struct pt_regs *);
ccw_req_t * dasd_alloc_request (char *, int , int ) ;
void dasd_free_request(ccw_req_t *);
int (*genhd_dasd_name)(char*,int,int,struct gendisk*);



