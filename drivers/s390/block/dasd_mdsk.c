#include <linux/dasd.h>
#include <linux/malloc.h>
#include <linux/ctype.h>
#include "dasd_types.h"
#include "dasd_ccwstuff.h"
#include "dasd_mdsk.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(mdsk):"

int dasd_is_accessible (int devno);
void dasd_insert_range (int start, int end);

/*
 * The device characteristics function
 */
static __inline__ int
dia210 (void *devchar)
{
	int rc;

	asm volatile ("    lr    2,%1\n"
		      "    .long 0x83200210\n"
		      "    ipm   %0\n"
		      "    srl   %0,28"
		      :"=d" (rc)
		      :"d" ((void *) __pa (devchar))
		      :"2");
	return rc;
}

static __inline__ int
dia250 (void *iob, int cmd)
{
	int rc;

	asm volatile ("    lr    2,%1\n"
		      "    lr    3,%2\n"
		      "    .long 0x83230250\n"
		      "    lr    %0,3"
		      :"=d" (rc)
		      :"d" ((void *) __pa (iob)), "d" (cmd)
		      :"2", "3");
	return rc;
}

/*
 * Init of minidisk device
 */

static __inline__ int
mdsk_init_io (int di, int blocksize, int offset, int size)
{
	mdsk_init_io_t *iib = &(dasd_info[di]->private.mdsk.iib);
	int rc;

	memset (iib, 0, sizeof (mdsk_init_io_t));

	iib->dev_nr = dasd_info[di]->info.devno;
	iib->block_size = blocksize;
	iib->offset = offset;
	iib->start_block = 0;
	iib->end_block = size;

	rc = dia250 (iib, INIT_BIO);

	return rc;
}

/*
 * release of minidisk device
 */

static __inline__ int
mdsk_term_io (int di)
{
	mdsk_init_io_t *iib = &(dasd_info[di]->private.mdsk.iib);
	int rc;

	memset (iib, 0, sizeof (mdsk_init_io_t));
	iib->dev_nr = dasd_info[di]->info.devno;
	rc = dia250 (iib, TERM_BIO);
	return rc;
}

void dasd_do_chanq (void);
void dasd_schedule_bh (void (*func) (void));
int register_dasd_last (int di);

int
dasd_mdsk_start_IO (cqr_t * cqr)
{
	int rc;
	mdsk_rw_io_t *iob = &(dasd_info[cqr->devindex]->private.mdsk.iob);

	iob->dev_nr = dasd_info[cqr->devindex]->info.devno;
	iob->key = 0;
	iob->flags = 2;
	iob->block_count = cqr->cplength >> 1;
	iob->interrupt_params = (u32) cqr;
	iob->bio_list = __pa (cqr->cpaddr);

	asm volatile ("STCK %0":"=m" (cqr->startclk));
	rc = dia250 (iob, RW_BIO);
	if (rc > 8) {
		PRINT_WARN ("dia250 returned CC %d\n", rc);
		ACS (cqr->status, CQR_STATUS_QUEUED, CQR_STATUS_ERROR);
	} else {
		ACS (cqr->status, CQR_STATUS_QUEUED, CQR_STATUS_IN_IO);
		atomic_dec (&chanq_tasks);
	}
	return rc;
}

void 
do_dasd_mdsk_interrupt (struct pt_regs *regs, __u16 code)
{
	int intparm = S390_lowcore.ext_params;
	char status = *((char *) S390_lowcore.ext_params + 5);
	cqr_t *cqr = (cqr_t *) intparm;
	if (!intparm)
		return;
	if (cqr->magic != MDSK_MAGIC) {
		panic ("unknown magic number\n");
	}
	asm volatile ("STCK %0":"=m" (cqr->stopclk));
	if (atomic_read (&dasd_info[cqr->devindex]->status) ==
	    DASD_INFO_STATUS_DETECTED) {
		register_dasd_last (cqr->devindex);
	}
	switch (status) {
	case 0x00:
		ACS (cqr->status, CQR_STATUS_IN_IO, CQR_STATUS_DONE);
		break;
	case 0x01:
	case 0x02:
	case 0x03:
	default:
		ACS (cqr->status, CQR_STATUS_IN_IO, CQR_STATUS_FAILED);
		atomic_inc (&dasd_info[cqr->devindex]->
			    queue.dirty_requests);
		break;
	}
	atomic_inc (&chanq_tasks);
	dasd_schedule_bh (dasd_do_chanq);
}

cqr_t *
dasd_mdsk_build_req (int devindex,
		     struct request *req)
{
	cqr_t *rw_cp = NULL;
	struct buffer_head *bh;
	int rw_cmd;
	dasd_information_t *info = dasd_info[devindex];
	int noblk = req->nr_sectors >> info->sizes.s2b_shift;
	int byt_per_blk = info->sizes.bp_block;
	int block;
	mdsk_bio_t *bio;
	int bhct;
	long size;

	if (!noblk) {
		PRINT_ERR ("No blocks to write...returning\n");
		return NULL;
	}
	if (req->cmd == READ) {
		rw_cmd = MDSK_READ_REQ;
	} else
#if DASD_PARANOIA > 2
	if (req->cmd == WRITE)
#endif				/* DASD_PARANOIA */
	{
		rw_cmd = MDSK_WRITE_REQ;
	}
#if DASD_PARANOIA > 2
	else {
		PRINT_ERR ("Unknown command %d\n", req->cmd);
		return NULL;
	}
#endif				/* DASD_PARANOIA */
	bhct = 0;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
		if (bh->b_size > byt_per_blk)
			for (size = 0; size < bh->b_size; size += byt_per_blk)
				bhct++;
		else
			bhct++;
	}
	/* Build the request */
	rw_cp = request_cqr (MDSK_BIOS (bhct), 0);
	if (!rw_cp) {
		return NULL;
	}
	rw_cp->magic = MDSK_MAGIC;
	bio = (mdsk_bio_t *) (rw_cp->cpaddr);

	block = req->sector >> info->sizes.s2b_shift;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
		if (bh->b_size >= byt_per_blk) {
			memset (bio, 0, sizeof (mdsk_bio_t));
			for (size = 0; size < bh->b_size; size += byt_per_blk) {
				bio->type = rw_cmd;
				bio->block_number = block + 1;
				bio->buffer = __pa (bh->b_data + size);
				bio++;
				block++;
			}
		} else {
			PRINT_WARN ("Cannot fulfill request smaller than block\n");
			release_cqr (rw_cp);
			return NULL;
		}
	}
	return rw_cp;
}

int
dasd_mdsk_ck_devinfo (dev_info_t * info)
{
	int rc = 0;

	dasd_mdsk_characteristics_t devchar =
	{0,};

	devchar.dev_nr = info->devno;
	devchar.rdc_len = sizeof (dasd_mdsk_characteristics_t);

	if (dia210 (&devchar) != 0) {
		return -ENODEV;
	}
	if (devchar.vdev_class == DEV_CLASS_FBA ||
	    devchar.vdev_class == DEV_CLASS_ECKD ||
	    devchar.vdev_class == DEV_CLASS_CKD) {
		return 0;
	} else {
		return -ENODEV;
	}
	return rc;
}

int
dasd_mdsk_ck_characteristics (dasd_characteristics_t * dchar)
{
	int rc = 0;
	dasd_mdsk_characteristics_t *devchar =
	(dasd_mdsk_characteristics_t *) dchar;

	if (dia210 (devchar) != 0) {
		return -ENODEV;
	}
	if (devchar->vdev_class != DEV_CLASS_FBA &&
	    devchar->vdev_class != DEV_CLASS_ECKD &&
	    devchar->vdev_class != DEV_CLASS_CKD) {
		return -ENODEV;
	}
	return rc;
}

cqr_t *
dasd_mdsk_fill_sizes_first (int di)
{
	cqr_t *cqr = NULL;
	dasd_information_t *info = dasd_info[di];
	mdsk_bio_t *bio;
	int bsize;
	int rc;
	/* Figure out position of label block */
	if (info->rdc_data->mdsk.vdev_class == DEV_CLASS_FBA) {
		info->sizes.label_block = 1;
	} else if (info->rdc_data->mdsk.vdev_class == DEV_CLASS_ECKD ||
		   info->rdc_data->mdsk.vdev_class == DEV_CLASS_CKD) {
		dasd_info[di]->sizes.label_block = 2;
	} else {
		return NULL;
	}

	/* figure out blocksize of device */
	mdsk_term_io (di);
	for (bsize = 512; bsize <= PAGE_SIZE; bsize = bsize << 1) {
		rc = mdsk_init_io (di, bsize, 0, 64);
		if (rc <= 4) {
			break;
		}
	}
	if (bsize > PAGE_SIZE) {
		PRINT_INFO ("Blocksize larger than 4096??\n");
		rc = mdsk_term_io (di);
		return NULL;
	}
	dasd_info[di]->sizes.bp_sector = bsize;

	info->private.mdsk.label = (long *) get_free_page (GFP_KERNEL);
	cqr = request_cqr (MDSK_BIOS (1), 0);
	cqr->magic = MDSK_MAGIC;
	bio = (mdsk_bio_t *) (cqr->cpaddr);
	memset (bio, 0, sizeof (mdsk_bio_t));
	bio->type = MDSK_READ_REQ;
	bio->block_number = info->sizes.label_block + 1;
	bio->buffer = __pa (info->private.mdsk.label);
	cqr->devindex = di;
	atomic_set (&cqr->status, CQR_STATUS_FILLED);

	return cqr;
}

int
dasd_mdsk_fill_sizes_last (int di)
{
	int sb;
	dasd_information_t *info = dasd_info[di];
	long *label = info->private.mdsk.label;
	int bs = info->private.mdsk.iib.block_size;

	info->sizes.s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < bs; sb = sb << 1)
		info->sizes.s2b_shift++;

	if (label[3] != bs) {
		PRINT_WARN ("%04X mismatching blocksizes\n", info->info.devno);
		atomic_set (&dasd_info[di]->status,
			    DASD_INFO_STATUS_DETECTED);
		return -EINVAL;
	}
	if (label[0] != 0xc3d4e2f1) {	/* CMS1 */
		PRINT_WARN ("%04X is not CMS formatted\n", info->info.devno);
	}
	if (label[13] == 0) {
		PRINT_WARN ("%04X is not reserved\n", info->info.devno);
	}
	/* defaults for first partition */
	info->private.mdsk.setup.size =
	    (label[7] - 1 - label[13]) * (label[3] >> 9) >> 1;
	info->private.mdsk.setup.blksize = label[3];
	info->private.mdsk.setup.offset = label[13] + 1;

	/* real size of the volume */
	info->sizes.bp_block = label[3];
	info->sizes.kbytes = label[7] * (label[3] >> 9) >> 1;

	if (info->sizes.s2b_shift >= 1)
		info->sizes.blocks = info->sizes.kbytes >>
		    (info->sizes.s2b_shift - 1);
	else
		info->sizes.blocks = info->sizes.kbytes <<
		    (-(info->sizes.s2b_shift - 1));

	PRINT_INFO ("%ld kBytes in %d blocks of %d Bytes\n",
		    info->sizes.kbytes,
		    info->sizes.blocks,
		    info->sizes.bp_sector);
	return 0;

}

dasd_operations_t dasd_mdsk_operations =
{
	ck_devinfo:dasd_mdsk_ck_devinfo,
	get_req_ccw:dasd_mdsk_build_req,
	ck_characteristics:dasd_mdsk_ck_characteristics,
	fill_sizes_first:dasd_mdsk_fill_sizes_first,
	fill_sizes_last:dasd_mdsk_fill_sizes_last,
	dasd_format:NULL
};
