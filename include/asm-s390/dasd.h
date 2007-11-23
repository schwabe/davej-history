
#ifndef DASD_H
#define DASD_H

/* First of all the external stuff */
#include <linux/ioctl.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <asm/ccwcache.h>
/* #include <linux/blkdev.h> */
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
#include <linux/devfs_fs_kernel.h>
#endif /* LINUX_IS_24 */

#define IOCTL_LETTER 'D'
/* Format the volume or an extent */
#define BIODASDFORMAT  _IOW(IOCTL_LETTER,0,format_data_t) 
/* Disable the volume (for Linux) */
#define BIODASDDISABLE _IO(IOCTL_LETTER,1) 
/* Enable the volume (for Linux) */
#define BIODASDENABLE  _IO(IOCTL_LETTER,2) 
/* Stuff for reading and writing the Label-Area to/from user space */
#define BIODASDGTVLBL  _IOR(IOCTL_LETTER,3,dasd_volume_label_t)
#define BIODASDSTVLBL  _IOW(IOCTL_LETTER,4,dasd_volume_label_t)
#define BIODASDRWTB    _IOWR(IOCTL_LETTER,5,int)
#define BIODASDRSID    _IOR(IOCTL_LETTER,6,senseid_t)
#define BIODASDRLB     _IOR(IOCTL_LETTER,7,int)
#define BLKGETBSZ      _IOR(IOCTL_LETTER,8,int)
#define BIODASDEXCP    _IOWR(IOCTL_LETTER,9,ccw_req_t)

#define DASD_NAME "dasd"
#define DASD_PARTN_BITS 2
#define PARTN_BITS DASD_PARTN_BITS
#define DASD_PER_MAJOR ( 1U<<(MINORBITS-DASD_PARTN_BITS))

/* 
 * struct format_data_t
 * represents all data necessary to format a dasd
 */
typedef struct format_data_t {
	int start_unit; /* from track */
	int stop_unit;  /* to track */
	int blksize;    /* sectorsize */
        int intensity;  /* 0: normal, 1:record zero, 3:home address */
} __attribute__ ((packed)) format_data_t;

#define DASD_FORMAT_DEFAULT_START_UNIT 0
#define DASD_FORMAT_DEFAULT_STOP_UNIT -1
#define DASD_FORMAT_DEFAULT_BLOCKSIZE -1
#define DASD_FORMAT_DEFAULT_INTENSITY -1

#ifdef __KERNEL__

typedef enum {
	dasd_era_fatal = -1,	/* no chance to recover              */
	dasd_era_none = 0,	/* don't recover, everything alright */
	dasd_era_msg = 1,	/* don't recover, just report...     */
	dasd_era_recover = 2	/* recovery action recommended       */
} dasd_era_t;

/* 
 * struct dasd_sizes_t
 * represents all data needed to access dasd with properly set up sectors
 */
typedef
struct dasd_sizes_t {
	unsigned int blocks;
	unsigned int bp_block;
	unsigned int s2b_shift;
} __attribute__ ((packed)) dasd_sizes_t;

/* 
 * struct dasd_chanq_t 
 * represents a queue of channel programs related to a single device
 */
typedef
struct dasd_chanq_t {
	ccw_req_t *head;
	ccw_req_t *tail;
	struct dasd_chanq_t *next_q;	/* pointer to next queue */
	int queued_requests;
	atomic_t flags;
	atomic_t dirty_requests;
} __attribute__ ((packed)) dasd_chanq_t;

#define DASD_CHANQ_BUSY 0x01
#define DASD_CHANQ_ACTIVE 0x02
#define DASD_REQUEST_Q_BROKEN 0x04

struct dasd_device_t;

typedef ccw_req_t *(*dasd_erp_action_fn_t) (ccw_req_t * cqr);
typedef int (*dasd_erp_postaction_fn_t) (ccw_req_t * cqr, int);

typedef int (*dasd_ck_id_fn_t) (dev_info_t *);
typedef int (*dasd_ck_characteristics_fn_t) (struct dasd_device_t *);
typedef int (*dasd_fill_geometry_fn_t) (struct dasd_device_t *, struct hd_geometry *);
typedef ccw_req_t *(*dasd_format_fn_t) (struct dasd_device_t *, struct format_data_t *);
typedef ccw_req_t *(*dasd_init_analysis_fn_t) (struct dasd_device_t *);
typedef int (*dasd_do_analysis_fn_t) (struct dasd_device_t *);
typedef int (*dasd_io_starter_fn_t) (ccw_req_t *);
typedef void (*dasd_int_handler_fn_t)(int irq, void *, struct pt_regs *);
typedef dasd_era_t (*dasd_error_examine_fn_t) (ccw_req_t *, devstat_t * stat);
typedef dasd_erp_action_fn_t (*dasd_error_analyse_fn_t) (ccw_req_t *);
typedef dasd_erp_postaction_fn_t (*dasd_erp_analyse_fn_t) (ccw_req_t *);
typedef ccw_req_t *(*dasd_cp_builder_fn_t)(struct dasd_device_t *,struct request *);
typedef char *(*dasd_dump_sense_fn_t)(struct dasd_device_t *,ccw_req_t *);

typedef struct dasd_discipline_t {
	char ebcname[8]; /* a name used for tagging and printks */
        char name[8];		/* a name used for tagging and printks */

	struct dasd_discipline_t *next;	/* used for list of disciplines */

	dasd_ck_id_fn_t id_check;	/* to check sense data */
	dasd_ck_characteristics_fn_t check_characteristics;	/* to check the characteristics */
	dasd_init_analysis_fn_t init_analysis;	/* to start the analysis of the volume */
	dasd_do_analysis_fn_t do_analysis;	/* to complete the analysis of the volume */
	dasd_fill_geometry_fn_t fill_geometry;	/* to set up hd_geometry */
	dasd_io_starter_fn_t start_IO;
        dasd_format_fn_t format_device;		/* to format the device */
	dasd_error_examine_fn_t examine_error;
	dasd_error_analyse_fn_t erp_action;
	dasd_erp_analyse_fn_t erp_postaction;
        dasd_cp_builder_fn_t build_cp_from_req;
        dasd_dump_sense_fn_t dump_sense;
        dasd_int_handler_fn_t int_handler;
} __attribute__   ((packed)) dasd_discipline_t;

typedef struct major_info_t {
	struct major_info_t *next;
	struct dasd_device_t **dasd_device;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	void (*request_fn) (request_queue_t *);
#else
	void (*request_fn) (void);
#endif /* LINUX_IS_24 */
	int read_ahead;
	int *blk_size;
	int *hardsect_size;
	int *blksize_size;
	int *max_sectors;
	struct gendisk gendisk;
} __attribute__ ((packed)) major_info_t;

typedef struct dasd_device_t {
	dev_info_t devinfo;
	dasd_discipline_t *discipline;
	atomic_t level;
        int open_count;
        kdev_t kdev;
        major_info_t *major_info;
	struct dasd_chanq_t queue;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        wait_queue_head_t wait_q;
        request_queue_t *request_queue;
#else
        struct wait_queue wait;
        struct wait_queue *wait_q;
#endif /* LINUX_IS_24 */
        dasd_sizes_t sizes;
	devstat_t dev_status;
	char *characteristics;
        char name[16]; /* The name of the device in /dev */
	char *private;	/* to be used by the discipline internally */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
        devfs_handle_t devfs_entry;
#endif /* LINUX_IS_24 */
}  dasd_device_t;

/* dasd_device_t.level can be: */
#define DASD_DEVICE_LEVEL_UNKNOWN 0x00
#define DASD_DEVICE_LEVEL_RECOGNIZED 0x01
#define DASD_DEVICE_LEVEL_ANALYSIS_PENDING 0x02
#define DASD_DEVICE_LEVEL_ANALYSIS_PREPARED 0x04
#define DASD_DEVICE_LEVEL_ANALYSED 0x08
#define DASD_DEVICE_LEVEL_PARTITIONED 0x10

typedef
union {
	char bytes[512];
	struct {
		/* 80 Bytes of Label data */
		char identifier[4];	/* e.g. "LNX1", "VOL1" or "CMS1" */
		char label[6];	/* Given by user */
		char security;
		char vtoc[5];	/* Null in "LNX1"-labelled partitions */
		char reserved0[5];
		long ci_size;
		long blk_per_ci;
		long lab_per_ci;
		char reserved1[4];
		char owner[0xe];
		char no_part;
		char reserved2[0x1c];
		/* 16 Byte of some information on the dasd */
		short blocksize;
		char nopart;
		char unused;
		long unused2[3];
		/* 7*10 = 70 Bytes of partition data */
		struct {
			char type;
			long start;
			long size;
			char unused;
		} part[7];
	} __attribute__ ((packed)) label;
} dasd_volume_label_t;

typedef union {
	struct {
		unsigned long no;
		unsigned int ct;
	} __attribute__ ((packed)) input;
	struct {
		unsigned long noct;
	} __attribute__ ((packed)) output;
} __attribute__ ((packed)) dasd_xlate_t;

int dasd_init (void);
/* int dasd_device_name (char *, int, int, struct gendisk *); */
void dasd_discipline_enq (dasd_discipline_t *);
int dasd_discipline_deq(dasd_discipline_t *);
int dasd_start_IO (ccw_req_t *);
void dasd_int_handler (int , void *, struct pt_regs *);

#endif /* __KERNEL__ */

#endif				/* DASD_H */

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
