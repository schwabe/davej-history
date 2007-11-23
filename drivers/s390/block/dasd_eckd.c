/* 
 * File...........: linux/drivers/s390/block/dasd_eckd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000

 * History of changes (starts July 2000)
 * 07/11/00 Enabled rotational position sensing
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <asm/debug.h>

#include <linux/malloc.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO                      */
#include <linux/blk.h>
#include <asm/ccwcache.h>
#include <asm/dasd.h>

#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "dasd.h"
#include "dasd_eckd.h"


#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER DASD_NAME"(eckd):"

#define ECKD_C0(i) (i->home_bytes)
#define ECKD_F(i) (i -> formula)
#define ECKD_F1(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f1):(i->factors.f_0x02.f1))
#define ECKD_F2(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f2):(i->factors.f_0x02.f2))
#define ECKD_F3(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f3):(i->factors.f_0x02.f3))
#define ECKD_F4(i) (ECKD_F(i)==0x02?(i->factors.f_0x02.f4):0)
#define ECKD_F5(i) (ECKD_F(i)==0x02?(i->factors.f_0x02.f5):0)
#define ECKD_F6(i) (i -> factor6)
#define ECKD_F7(i) (i -> factor7)
#define ECKD_F8(i) (i -> factor8)

#define DASD_ECKD_CCW_WRITE 0x05
#define DASD_ECKD_CCW_READ 0x06
#define DASD_ECKD_CCW_WRITE_HOME_ADDRESS 0x09
#define DASD_ECKD_CCW_READ_HOME_ADDRESS 0x0a
#define DASD_ECKD_CCW_READ_COUNT 0x12
#define DASD_ECKD_CCW_WRITE_RECORD_ZERO 0x15
#define DASD_ECKD_CCW_READ_RECORD_ZERO 0x16
#define DASD_ECKD_CCW_WRITE_CKD 0x1d
#define DASD_ECKD_CCW_READ_CKD 0x1e
#define DASD_ECKD_CCW_LOCATE_RECORD 0x47
#define DASD_ECKD_CCW_DEFINE_EXTENT 0x63
#define DASD_ECKD_CCW_WRITE_MT 0x85
#define DASD_ECKD_CCW_READ_MT 0x86
#define DASD_ECKD_CCW_READ_CKD_MT 0x9e
#define DASD_ECKD_CCW_WRITE_CKD_MT 0x9d

dasd_discipline_t dasd_eckd_discipline;

typedef struct
dasd_eckd_private_t {
        dasd_eckd_characteristics_t rdc_data;
        dasd_eckd_confdata_t conf_data;
        eckd_count_t count_area;
} dasd_eckd_private_t;

static inline unsigned int
round_up_multiple (unsigned int no, unsigned int mult)
{
	int rem = no % mult;
	return (rem ? no - rem + mult : no);
}

static inline unsigned int
ceil_quot (unsigned int d1, unsigned int d2)
{
	return (d1 + (d2 - 1)) / d2;
}

static inline int
bytes_per_record (dasd_eckd_characteristics_t * rdc,
		  int kl,	/* key length */
		  int dl /* data length */ )
{
	int bpr = 0;
	switch (rdc->formula) {
	case 0x01:{
			unsigned int fl1, fl2;
			fl1 = round_up_multiple (ECKD_F2 (rdc) + dl,
						 ECKD_F1 (rdc));
			fl2 = round_up_multiple (kl ? ECKD_F2 (rdc) + kl : 0,
						 ECKD_F1 (rdc));
			bpr = fl1 + fl2;
			break;
		}
	case 0x02:{
			unsigned int fl1, fl2, int1, int2;
			int1 = ceil_quot (dl + ECKD_F6 (rdc),
					  ECKD_F5 (rdc) << 1);
			int2 = ceil_quot (kl + ECKD_F6 (rdc),
					  ECKD_F5 (rdc) << 1);
			fl1 = round_up_multiple (ECKD_F1 (rdc) *
						 ECKD_F2 (rdc) +
						 (dl + ECKD_F6 (rdc) +
						  ECKD_F4 (rdc) * int1),
						 ECKD_F1 (rdc));
			fl2 = round_up_multiple (ECKD_F1 (rdc) *
						 ECKD_F3 (rdc) +
						 (kl + ECKD_F6 (rdc) +
						  ECKD_F4 (rdc) * int2),
						 ECKD_F1 (rdc));
			bpr = fl1 + fl2;
			break;
		}
	default:
		INTERNAL_ERROR ("unknown formula%d\n", rdc->formula);
	}
	return bpr;
}

static inline unsigned int
bytes_per_track (dasd_eckd_characteristics_t * rdc)
{
	return *(unsigned int *) (rdc->byte_per_track) >> 8;
}

static inline unsigned int
recs_per_track (dasd_eckd_characteristics_t * rdc,
		unsigned int kl, unsigned int dl)
{
	int rpt = 0;
	int dn;
        switch ( rdc -> dev_type ) {
	case 0x3380: 
		if (kl)
			return 1499 / (15 +
				       7 + ceil_quot (kl + 12, 32) +
				       ceil_quot (dl + 12, 32));
		else
			return 1499 / (15 + ceil_quot (dl + 12, 32));
	case 0x3390: 
		dn = ceil_quot (dl + 6, 232) + 1;
		if (kl) {
			int kn = ceil_quot (kl + 6, 232) + 1;
			return 1729 / (10 +
				       9 + ceil_quot (kl + 6 * kn, 34) +
				       9 + ceil_quot (dl + 6 * dn, 34));
		} else
			return 1729 / (10 +
				       9 + ceil_quot (dl + 6 * dn, 34));
	case 0x9345: 
	        dn = ceil_quot (dl + 6, 232) + 1;
                if (kl) {
                        int kn = ceil_quot (kl + 6, 232) + 1;
                        return 1420 / (18 +
                                       7 + ceil_quot (kl + 6 * kn, 34) +
                                       ceil_quot (dl + 6 * dn, 34));
                } else
                        return 1420 / (18 +
                                       7 + ceil_quot (dl + 6 * dn, 34));
	}
	return rpt;
}

static inline void
define_extent (ccw1_t * de_ccw,
	       DE_eckd_data_t * data,
	       int trk,
	       int totrk,
	       int cmd,
	       dasd_device_t *device)
{
	ch_t geo, beg, end;
        dasd_eckd_private_t *private = (dasd_eckd_private_t *)device->private;

	geo.cyl = private->rdc_data.no_cyl;
	geo.head = private->rdc_data.trk_per_cyl;
	beg.cyl = trk / geo.head;
	beg.head = trk % geo.head;
	end.cyl = totrk / geo.head;
	end.head = totrk % geo.head;

	memset (de_ccw, 0, sizeof (ccw1_t));
	de_ccw->cmd_code = DASD_ECKD_CCW_DEFINE_EXTENT;
	de_ccw->count = 16;
	de_ccw->cda = (void *) __pa (data);

	memset (data, 0, sizeof (DE_eckd_data_t));
	switch (cmd) {
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
	case DASD_ECKD_CCW_READ_CKD:	/* Fallthrough */
	case DASD_ECKD_CCW_READ_CKD_MT:
	case DASD_ECKD_CCW_READ_COUNT:
		data->mask.perm = 0x1;
                data->attributes.operation = 0x3; /* enable seq. caching */
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
		data->mask.perm = 0x02;
                data->attributes.operation = 0x3; /* enable seq. caching */
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		data->attributes.operation = 0x1;	/* format through cache */
		break;
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		data->mask.perm = 0x3;
		data->mask.auth = 0x1;
		data->attributes.operation = 0x1;	/* format through cache */
		break;
	default:
		INTERNAL_ERROR ("unknown opcode 0x%x\n", cmd);
		break;
	}
	data->attributes.mode = 0x3;
	data->beg_ext.cyl = beg.cyl;
	data->beg_ext.head = beg.head;
	data->end_ext.cyl = end.cyl;
	data->end_ext.head = end.head;
}

static inline void
locate_record (ccw1_t * lo_ccw,
	       LO_eckd_data_t * data,
	       int trk,
	       int rec_on_trk,
	       int no_rec,
	       int cmd,
	       dasd_device_t * device)
{
        dasd_eckd_private_t *private = (dasd_eckd_private_t *)device->private;
	ch_t geo = {private->rdc_data.no_cyl,
                    private->rdc_data.trk_per_cyl};
	ch_t seek ={trk / (geo.head), trk % (geo.head)};
	int reclen = device->sizes.bp_block;
        int sector;

	memset (lo_ccw, 0, sizeof (ccw1_t));
	lo_ccw->cmd_code = DASD_ECKD_CCW_LOCATE_RECORD;
	lo_ccw->count = 16;
	lo_ccw->cda = (void *) __pa (data);

	memset (data, 0, sizeof (LO_eckd_data_t));
	switch (cmd) {
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		data->operation.orientation = 0x1;
		data->operation.operation = 0x03;
		data->count++;
		break;
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		data->count++;
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x01;
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x06;
		break;
	case DASD_ECKD_CCW_READ_CKD:
	case DASD_ECKD_CCW_READ_CKD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_READ_COUNT:
		data->operation.operation = 0x06;
                break;
	default:
		INTERNAL_ERROR ("unknown opcode 0x%x\n", cmd);
	}
        switch ( private -> rdc_data.dev_type == 0x3390 ) {
        case 0x3390: {
                int dn,d;
                dn = ceil_quot(reclen + 6,232);
                d = 9 + ceil_quot(reclen + 6 * (dn + 1),34);
                sector = (49 + (rec_on_trk - 1) * ( 10 + d ))/8;
                break;
        }
        case 0x3380: {
                int d;
                d = 7 + ceil_quot(reclen + 12, 32);
                sector = (39 + (rec_on_trk - 1) * ( 8 + d ))/7;
                break;
        }
        case 0x9345:
        default:
                sector = 0;
        }
        data -> sector = sector;
	memcpy (&(data->seek_addr), &seek, sizeof (ch_t));
	memcpy (&(data->search_arg), &seek, sizeof (ch_t));
	data->search_arg.record = rec_on_trk;
	data->count += no_rec;
}

static int 
dasd_eckd_id_check ( dev_info_t *info )
{
        if ( info->sid_data.cu_type == 0x3990 )
                if ( info->sid_data.dev_type == 0x3390 )
                        return 0;
        if ( info->sid_data.cu_type == 0x3990 )
                if ( info->sid_data.dev_type == 0x3380 )
                        return 0;
        if ( info->sid_data.cu_type == 0x9343 )
                if ( info->sid_data.dev_type == 0x9345 )
                        return 0;
        return -ENODEV;
		}

static int
dasd_eckd_check_characteristics (struct dasd_device_t *device)
{
        int rc = -ENODEV;
        void *conf_data;
        void *rdc_data;
        int conf_len;
        dasd_eckd_private_t *private;

        if ( device == NULL ) {
                printk ( KERN_WARNING PRINTK_HEADER
                         "Null device pointer passed to characteristics checker\n");
                return -ENODEV;
        }
        if ( device->private == NULL ) {
          device->private = kmalloc(sizeof(dasd_eckd_private_t),GFP_KERNEL);
          if ( device->private == NULL ) {
            printk ( KERN_WARNING PRINTK_HEADER
                     "memory allocation failed for private data\n");
            return -ENOMEM;
          }
        }
        private = (dasd_eckd_private_t *)device->private;
        rdc_data = (void *)&(private->rdc_data);
        rc = read_dev_chars (device->devinfo.irq, &rdc_data , 64);
        if ( rc ) {
                printk ( KERN_WARNING PRINTK_HEADER
                         "Read device characteristics returned error %d\n",rc);
                return rc;
	}
	printk ( KERN_INFO PRINTK_HEADER 
                 "%04X on sch %d: %04X/%02X(CU:%04X/%02X) Cyl:%d Head:%d Sec:%d\n",
                 device->devinfo.devno, device->devinfo.irq,
                 private->rdc_data.dev_type, private->rdc_data.dev_model,
                 private->rdc_data.cu_type, private->rdc_data.cu_model.model,
                 private->rdc_data.no_cyl, private->rdc_data.trk_per_cyl,
                 private->rdc_data.sec_per_trk);
        rc = read_conf_data(device->devinfo.irq, &conf_data, &conf_len, LPM_ANYPATH );
        if ( rc ) {
                if ( rc == -EOPNOTSUPP )
                        return 0;
                printk ( KERN_WARNING PRINTK_HEADER
                         "Read configuration data returned error %d\n",rc);
                return rc;
	}
        if ( conf_len != sizeof(dasd_eckd_confdata_t)) {
                printk ( KERN_WARNING PRINTK_HEADER
                         "sizes of configuration data mismatch %d (read) vs %ld (expected)\n",
                         conf_len, sizeof(dasd_eckd_confdata_t));
                return rc;
        }
        if ( conf_data == NULL ) {
                printk ( KERN_WARNING PRINTK_HEADER
                         "No configuration data retrieved\n");
                return -ENOMEM;
        }
        memcpy (&private->conf_data,conf_data,sizeof(dasd_eckd_confdata_t));
	printk ( KERN_INFO PRINTK_HEADER 
                 "%04X on sch %d: %04X/%02X (CU: %04X/%02X): Configuration data read\n",
                 device->devinfo.devno, device->devinfo.irq,
                 private->rdc_data.dev_type, private->rdc_data.dev_model,
                 private->rdc_data.cu_type, private->rdc_data.cu_model.model);
        device->sizes.bp_block = 4096;
        device->sizes.s2b_shift = 3;
        device->sizes.blocks = ( private->rdc_data.no_cyl * 
                                 private->rdc_data.trk_per_cyl *
                                 recs_per_track (&private->rdc_data, 
                                                 0, device->sizes.bp_block));
        return 0;
}

static ccw_req_t *
dasd_eckd_init_analysis (struct dasd_device_t *device)
{
	ccw_req_t *cqr = NULL;
	ccw1_t *ccw;
	DE_eckd_data_t *DE_data;
	LO_eckd_data_t *LO_data;
	eckd_count_t *count_data = &(((dasd_eckd_private_t *)(device->private))->count_area);

        cqr = ccw_alloc_request (dasd_eckd_discipline.name, 3, sizeof (DE_eckd_data_t) + sizeof (LO_eckd_data_t));
        if ( cqr == NULL ) {
                printk ( KERN_WARNING PRINTK_HEADER
                         "No memory to allocate initialization request\n");
                return NULL;
        }
	DE_data = cqr->data;
	LO_data = cqr->data + sizeof (DE_eckd_data_t);
	ccw = cqr->cpaddr;
	define_extent (ccw, DE_data, 0, 0, DASD_ECKD_CCW_READ_COUNT, device);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	locate_record (ccw, LO_data, 0, 1, 1, DASD_ECKD_CCW_READ_COUNT, device);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	ccw->cmd_code = DASD_ECKD_CCW_READ_COUNT;
	ccw->count = 8;
	ccw->cda = (void *) __pa (count_data);
	cqr->device = device;
	atomic_set (&cqr->status, CQR_STATUS_FILLED);
        
        return cqr;
}

static int
dasd_eckd_do_analysis (struct dasd_device_t *device)
{
        int rc = 0;
	int sb, rpt;
        dasd_eckd_private_t *private = (dasd_eckd_private_t *)device->private;
	int bs = private->count_area.dl;

        memset (&(device->sizes), 0, sizeof (dasd_sizes_t));
        switch ( bs ) {
        case 512:
        case 1024:
        case 2048:
        case 4096:
		device->sizes.bp_block = bs;
                break;
        default:
                printk ( KERN_INFO PRINTK_HEADER 
                         "/dev/%s (%04X): invalid blocksize %d\n"
                         KERN_INFO PRINTK_HEADER 
                         "/dev/%s (%04X): capacity (at 4kB blks): %dkB at %dkB/trk\n",
                         device->name, device->devinfo.devno,bs,
                         device->name, device->devinfo.devno,
                         ( private->rdc_data.no_cyl * private->rdc_data.trk_per_cyl *
                          recs_per_track (&private->rdc_data, 0, 4096)),
                        recs_per_track (&private->rdc_data, 0, 4096));
		return -EMEDIUMTYPE;
	}
	device->sizes.s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < bs; sb = sb << 1)
		device->sizes.s2b_shift++;
        
	rpt = recs_per_track (&private->rdc_data, 0, device->sizes.bp_block);
        device->sizes.blocks = ( private->rdc_data.no_cyl * private->rdc_data.trk_per_cyl *
                                 recs_per_track (&private->rdc_data, 0, device->sizes.bp_block));
                
        printk ( KERN_INFO PRINTK_HEADER 
                 "/dev/%s (%04X): capacity (%dkB blks): %dkB at %dkB/trk\n",
                 device->name, device->devinfo.devno,
                 (device->sizes.bp_block>>10),
                 ( private->rdc_data.no_cyl * private->rdc_data.trk_per_cyl *
                   recs_per_track (&private->rdc_data, 0, device->sizes.bp_block)*
                   (device->sizes.bp_block>>9))>>1,
                 ( recs_per_track (&private->rdc_data, 0, device->sizes.bp_block)*device->sizes.bp_block) >> 10);
	return 0;
        
        return rc;
}

static int
dasd_eckd_fill_geometry(struct dasd_device_t *device, struct hd_geometry *geo)
{
        int rc = 0;
        dasd_eckd_private_t *private = (dasd_eckd_private_t *)device->private;
        switch(device->sizes.bp_block) {
        case 512:
        case 1024:
        case 2048:
        case 4096:
                break;
        default:
                return -EINVAL;
        }
	geo->cylinders = private->rdc_data.no_cyl;
	geo->heads = private->rdc_data.trk_per_cyl;
	geo->sectors = recs_per_track (&(private->rdc_data), 0, device->sizes.bp_block);
	geo->start = 2;
        return rc;
}

static inline int
dasd_eckd_format_track (dasd_device_t *device, int trk, int bs, int flags)
{
	int rc = 0;
	int i;
	ccw_req_t *fcp = NULL;
	DE_eckd_data_t *DE_data = NULL;
	LO_eckd_data_t *LO_data = NULL;
	eckd_count_t *ct_data = NULL;
	eckd_count_t *r0_data = NULL;
        eckd_home_t *ha_data = NULL;
	ccw1_t *last_ccw = NULL;
        void * last_data = NULL;
        dasd_eckd_private_t *private = (dasd_eckd_private_t *)device->private;
	int retries;

	int rpt = recs_per_track (&(private->rdc_data), 0, bs);
	int cyl = trk / private->rdc_data.trk_per_cyl;
	int head = trk % private->rdc_data.trk_per_cyl;
        int wrccws = rpt;
        int datasize = sizeof (DE_eckd_data_t) + sizeof (LO_eckd_data_t);
        
        switch ( flags ) {
        case 0x00:
        case 0x01:
        case 0x03:
          break;
        default:
          return -EINVAL;
        }
        
        if ( flags & 0x1 ) {
          wrccws++;
          datasize += sizeof(eckd_count_t);
        }
        if ( flags & 0x2 ) {
          wrccws++;
          datasize += sizeof(eckd_home_t);
        }
	fcp = ccw_alloc_request (dasd_eckd_discipline.name, 
                                 wrccws + 2,
                                 datasize+rpt*sizeof(eckd_count_t));
	fcp->device = device;

        last_data = fcp->data;
	DE_data = (DE_eckd_data_t *) last_data;
        last_data = (void*)(DE_data +1);
	LO_data = (LO_eckd_data_t *) last_data;
        last_data = (void*)(LO_data +1);
        if ( flags & 0x2 ) {
          ha_data = (eckd_home_t *) last_data;
          last_data = (void*)(ha_data +1);
        }
        if ( flags & 0x1 ) {
          r0_data = (eckd_count_t *) last_data;
          last_data = (void*)(r0_data +1);
        }
	ct_data = (eckd_count_t *)last_data;

	last_ccw = fcp->cpaddr;

	switch (flags) {
	case 0x03:
		define_extent (last_ccw, DE_data, trk, trk,
                               DASD_ECKD_CCW_WRITE_HOME_ADDRESS, device);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		locate_record (last_ccw, LO_data, trk, 0, wrccws,
			       DASD_ECKD_CCW_WRITE_HOME_ADDRESS, device);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		break;
	case 0x01:
		define_extent (last_ccw, DE_data, trk, trk,
			       DASD_ECKD_CCW_WRITE_RECORD_ZERO, device);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		locate_record (last_ccw, LO_data, trk, 0, wrccws,
			       DASD_ECKD_CCW_WRITE_RECORD_ZERO, device);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		memset (r0_data, 0, sizeof (eckd_count_t));
		break;
	case 0x00:
		define_extent (last_ccw, DE_data, trk, trk,
			       DASD_ECKD_CCW_WRITE_CKD, device);
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		locate_record (last_ccw, LO_data, trk, 0, wrccws,
			       DASD_ECKD_CCW_WRITE_CKD, device);
                LO_data->length = bs;
		last_ccw->flags = CCW_FLAG_CC;
		last_ccw++;
		break;
	default:
		PRINT_WARN ("Unknown format flags...%d\n", flags);
		return -EINVAL;
	}
	if (flags & 0x02) {
		PRINT_WARN ("Unsupported format flag...%d\n", flags);
		return -EINVAL;
	}
	if (flags & 0x01) {	/* write record zero */
		r0_data->cyl = cyl;
		r0_data->head = head;
		r0_data->record = 0;
		r0_data->kl = 0;
		r0_data->dl = 8;
		last_ccw->cmd_code = DASD_ECKD_CCW_WRITE_RECORD_ZERO;
		last_ccw->count = 8;
		last_ccw->flags = CCW_FLAG_CC | CCW_FLAG_SLI;
		last_ccw->cda = (void *) __pa (r0_data);
		last_ccw++;
	}
	/* write remaining records */
	for (i = 0; i < rpt; i++, last_ccw++) {
		memset (ct_data + i, 0, sizeof (eckd_count_t));
		(ct_data + i)->cyl = cyl;
		(ct_data + i)->head = head;
		(ct_data + i)->record = i + 1;
		(ct_data + i)->kl = 0;
		(ct_data + i)->dl = bs;
		last_ccw->cmd_code = DASD_ECKD_CCW_WRITE_CKD;
		last_ccw->flags = CCW_FLAG_CC | CCW_FLAG_SLI;
		last_ccw->count = 8;
		last_ccw->cda = (void *) __pa (ct_data + i);
	}
	(last_ccw - 1)->flags &= ~(CCW_FLAG_CC | CCW_FLAG_DC);
	fcp->device = device;
        do {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
                DECLARE_WAITQUEUE (wait,current);
#else
                struct wait_queue wait = {current, NULL};
#endif /* LINUX_VERSION_CODE */
                unsigned long flags;
                int cs;

                retries = 1;
		s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
                atomic_set(&fcp->status,CQR_STATUS_QUEUED);
                do {
                        rc = dasd_eckd_discipline.start_IO (fcp);
                } while ( rc && retries-- );
                if ( rc && retries == 0 )
                        break;
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
		add_wait_queue (&device->wait_q, &wait);
#endif /* LINUX_VERSION_CODE */
		do {
			current->state = TASK_INTERRUPTIBLE;
			s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
                        schedule ();
			s390irq_spin_lock_irqsave (device->devinfo.irq, flags);
                        cs = atomic_read (&fcp->status);
		} while ((cs != CQR_STATUS_DONE) && (cs != CQR_STATUS_ERROR));
#if !(LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
		remove_wait_queue (&device->wait_q, &wait);
#endif /* LINUX_VERSION_CODE */
		s390irq_spin_unlock_irqrestore (device->devinfo.irq, flags);
	} while ( (rc || (atomic_read(&fcp->status) != CQR_STATUS_DONE)) &&
                 retries--);
	if ( retries == 0 )
                rc = -EIO;
	ccw_free_request (fcp);
	return rc;
}

static int
dasd_eckd_format_device (struct dasd_device_t *device, struct format_data_t *fdata)
{
        int rc = 0;
	int i;
	format_data_t fd;
        dasd_eckd_private_t *private = (dasd_eckd_private_t *)device->private;
        int last_track =private->rdc_data.no_cyl*private->rdc_data.trk_per_cyl-1;
        int intensity;
        kdev_t kdev = device->kdev;
        int nr_tracks, blocksize;

	if (fdata==NULL) {
		fd.start_unit = 0;
		fd.stop_unit = last_track;
		fd.blksize = 4096;
	} else {
		memcpy (&fd, fdata, sizeof (format_data_t));
		if (fd.stop_unit == -1) {
			fd.stop_unit = last_track;
		}
		if (fd.blksize == 0) {
			fd.blksize = 4096;
		}
	}
	if (fd.start_unit > fd.stop_unit) {
		return -EINVAL;
	}
	if (fd.start_unit > last_track ) {
		return -EINVAL;
	}
	if (fd.stop_unit > last_track ) {
		return -EINVAL;
	}
        switch(fd.blksize) {
        case 512:
        case 1024:
        case 2048:
        case 4096:
                break;
        default:
                return -EINVAL;
        }
        fd.intensity = 0x0;
        intensity = fd.intensity;
        set_blocksize(kdev, fd.blksize);
	printk (KERN_INFO PRINTK_HEADER
                "Formatting device %04X from track %d to %d with bs %d\n",
                device->devinfo.devno,fd.start_unit, fd.stop_unit, fd.blksize);
        nr_tracks = fd.stop_unit-fd.start_unit+1;
	for (i = 0; i <= nr_tracks; i++) {
		/* print 20 messages per format cmd at all */
                if ( i % (nr_tracks / 20) == 0 ) { 
                        printk (KERN_INFO PRINTK_HEADER
                                "Format %04X Cylinder: %d Track %d Intensity %d\n", 
                                device->devinfo.devno,
                                (i+fd.start_unit) / private->rdc_data.trk_per_cyl,
                                (i+fd.start_unit) % private->rdc_data.trk_per_cyl,
                                intensity);
                }
                do {
                        if ( i == 0 ) {
                                blocksize = 8;
                        } else {
                                blocksize = fd.blksize;
                        }
                        rc = dasd_eckd_format_track (device, 
                                                     (i % nr_tracks) + fd.start_unit , 
                                                     blocksize, intensity);
                                /* fix VM controlled minidisk */
                        if ( rc ) {
                                if ( intensity ) {
                                        intensity = intensity >> 1;
                                }
                                printk (KERN_WARNING PRINTK_HEADER
                                        "decreasing format intensity to %d\n", 
                                        intensity);
                        }
                } while ( rc && intensity > 0);
                if (rc) {
                        printk (KERN_WARNING PRINTK_HEADER
                                "Formatting of device %04X Cylinder %d Track %d failed...exiting\n",
                                device->devinfo.devno,
                                i / private->rdc_data.trk_per_cyl,
                                i % private->rdc_data.trk_per_cyl);
                        break;
		}
	}
	printk ( KERN_INFO PRINTK_HEADER
                 "Formatting of device %04X completed from track %d to %d with bs %d\n",
                 device->devinfo.devno, fd.start_unit, fd.stop_unit, fd.blksize);
        return rc;
}

static dasd_era_t
dasd_eckd_examine_error  (ccw_req_t *cqr, devstat_t * stat)
{
        dasd_device_t *device = (dasd_device_t *)cqr->device;
        if (stat->cstat == 0x00 &&
	    stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		return dasd_era_none;
        
	switch (device->devinfo.sid_data.cu_model) {
	case 0x3990:
		return dasd_3990_erp_examine (cqr, stat);
	case 0x9343:
		return dasd_9343_erp_examine (cqr, stat);
	default:
		return dasd_era_recover;
	}
}

static dasd_erp_action_fn_t 
dasd_eckd_erp_action ( ccw_req_t * cqr ) 
{
        return default_erp_action;
}

static dasd_erp_postaction_fn_t
dasd_eckd_erp_postaction (ccw_req_t * cqr)
{
        if ( cqr -> function == default_erp_action)
                return default_erp_postaction;
        printk ( KERN_WARNING PRINTK_HEADER
                 "unknown ERP action %p\n",
                 cqr -> function);
	return NULL;
}

static ccw_req_t *
dasd_eckd_build_cp_from_req (dasd_device_t *device, struct request *req)
{
        ccw_req_t *rw_cp = NULL;
	int rw_cmd;
	int bhct;
	long size;
        ccw1_t *ccw;
	DE_eckd_data_t *DE_data;
	LO_eckd_data_t *LO_data;
	struct buffer_head *bh;
        dasd_eckd_private_t *private = (dasd_eckd_private_t *)device->private;
	int byt_per_blk = device->sizes.bp_block;
        int shift = device->sizes.s2b_shift;
	int blk_per_trk = recs_per_track (&(private->rdc_data), 0, byt_per_blk);
	int btrk = (req->sector >> shift) / blk_per_trk;
	int etrk = ((req->sector + req->nr_sectors - 1) >> shift) / blk_per_trk;

	if (req->cmd == READ) {
		rw_cmd = DASD_ECKD_CCW_READ_MT;
	} else if (req->cmd == WRITE) {
		rw_cmd = DASD_ECKD_CCW_WRITE_MT;
	} else {
		PRINT_ERR ("Unknown command %d\n", req->cmd);
		return NULL;
	}
	/* Build the request */
	/* count bhs to prevent errors, when bh smaller than block */
	bhct = 0;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
		if (bh->b_size > byt_per_blk)
			for (size = 0; size < bh->b_size; size += byt_per_blk)
				bhct++;
		else
			bhct++;
	}

	rw_cp = dasd_alloc_request (dasd_eckd_discipline.name,
                                   2 + bhct,
			     sizeof (DE_eckd_data_t) +
			     sizeof (LO_eckd_data_t));
        if ( ! rw_cp ) {
          return NULL;
        }
	DE_data = rw_cp->data;
	LO_data = rw_cp->data + sizeof (DE_eckd_data_t);
	ccw = rw_cp->cpaddr;

	define_extent (ccw, DE_data, btrk, etrk, rw_cmd, device);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	locate_record (ccw, LO_data, btrk, (req->sector >> shift) % blk_per_trk + 1,
		       req->nr_sectors >> shift, rw_cmd, device);
	ccw->flags = CCW_FLAG_CC;
	for (bh = req->bh; bh != NULL;) {
		if (bh->b_size > byt_per_blk) {
		for (size = 0; size < bh->b_size; size += byt_per_blk) {
                        ccw++;
                        ccw->flags = CCW_FLAG_CC;
                        ccw->cmd_code = rw_cmd;
                        ccw->count = byt_per_blk;
				ccw->cda = (void *) __pa (bh->b_data + size);
		}
			bh = bh->b_reqnext;
		} else {	/* group N bhs to fit into byt_per_blk */
			for (size = 0; bh != NULL && size < byt_per_blk;) {
				ccw++;
				ccw->flags = CCW_FLAG_DC;
				ccw->cmd_code = rw_cmd;
				ccw->count = bh->b_size;
				ccw->cda = (void *) __pa (bh->b_data);
				size += bh->b_size;
				bh = bh->b_reqnext;
	}
			if (size != byt_per_blk) {
				PRINT_WARN ("Cannot fulfill small request %d vs. %d (%d sects)\n", size, byt_per_blk, req->nr_sectors);
				ccw_free_request (rw_cp);
		return NULL;
	}
			ccw->flags = CCW_FLAG_CC;
		}
	}
	ccw->flags &= ~(CCW_FLAG_DC | CCW_FLAG_CC);
        rw_cp->device = device;
        rw_cp->expires = 5 * 0xf424000; /* 5 seconds */
        rw_cp->req = req;
        atomic_compare_and_swap_debug(&rw_cp->status,CQR_STATUS_EMPTY,CQR_STATUS_FILLED);
	return rw_cp;
}

static char *
dasd_eckd_dump_sense(struct dasd_device_t *device, ccw_req_t *req)
{
        char *page = (char *)get_free_page(GFP_KERNEL);
        devstat_t *stat = &device->dev_status;
	char *sense = stat->ii.sense.data;
        int len,sl,sct;

        if ( page == NULL ) {
                return NULL;
        }
        len = sprintf ( page, KERN_WARNING PRINTK_HEADER 
                        "device %04X on irq %d: I/O status report:\n",
                        device->devinfo.devno,device->devinfo.irq);
        len += sprintf ( page + len, KERN_WARNING PRINTK_HEADER 
                         "in req: %p CS: 0x%02X DS: 0x%02X\n",
                         req,stat->cstat,stat->dstat);
        len += sprintf ( page + len, KERN_WARNING PRINTK_HEADER 
                         "Failing CCW: %p\n", (void *)stat->cpa);
        if ( stat->flag & DEVSTAT_FLAG_SENSE_AVAIL ) {
                for (sl = 0; sl < 4; sl++) {
                        len += sprintf ( page + len, KERN_WARNING PRINTK_HEADER 
                                         "Sense:");
                        for (sct = 0; sct < 8; sct++) {
                                len += sprintf ( page + len," %2d:0x%02x",
                                                 8 * sl + sct, sense[8 * sl + sct]);
        }
                        len += sprintf ( page + len,"\n");
                }
                if (sense[27] & 0x80) {	
                        /* 24 Byte Sense Data */
                        len += sprintf ( page + len, KERN_WARNING PRINTK_HEADER 
                                         "24 Byte: %x MSG %x, %s MSGb to SYSOP\n",
                                         sense[7] >> 4, sense[7] & 0x0f,
                                         sense[1] & 0x10 ? "" : "no");
                } else {		
                        /* 32 Byte Sense Data */
                        len += sprintf ( page + len, KERN_WARNING PRINTK_HEADER 
                                         "32 Byte: Format: %x Exception class %x\n",
                                         sense[6] & 0x0f, sense[22] >> 4);
		}
	}
        return page;
}

dasd_discipline_t dasd_eckd_discipline = {
	name :                          "ECKD",
	ebcname :                       "ECKD",
        id_check:                       dasd_eckd_id_check,          
        check_characteristics:          dasd_eckd_check_characteristics,
        init_analysis:                  dasd_eckd_init_analysis,
        do_analysis:                    dasd_eckd_do_analysis,          
        fill_geometry:                  dasd_eckd_fill_geometry,
        start_IO:                       dasd_start_IO,           
        format_device:                  dasd_eckd_format_device,          
        examine_error:                  dasd_eckd_examine_error,          
        erp_action:                     dasd_eckd_erp_action,             
        erp_postaction:                 dasd_eckd_erp_postaction,         
        build_cp_from_req:              dasd_eckd_build_cp_from_req,      
        dump_sense:                     dasd_eckd_dump_sense,            
        int_handler:                    dasd_int_handler            
};

int
dasd_eckd_init( void ) {
        int rc = 0;
        printk ( KERN_INFO PRINTK_HEADER
                 "%s discipline initializing\n", dasd_eckd_discipline.name);
        ASCEBC(dasd_eckd_discipline.ebcname,4);
        dasd_discipline_enq(&dasd_eckd_discipline);
        
        return rc;
}

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
