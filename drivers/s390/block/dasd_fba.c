
#include <linux/stddef.h>
#include <linux/kernel.h>

#ifdef MODULE
#include <linux/module.h>
#endif				/* MODULE */

#include <linux/malloc.h>
#include <linux/dasd.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO                      */

#include <asm/io.h>

#include <asm/irq.h>

#include "dasd_types.h"
#include "dasd_ccwstuff.h"

#define DASD_FBA_CCW_LOCATE 0x43
#define DASD_FBA_CCW_READ   0x42
#define DASD_FBA_CCW_WRITE  0x41

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(fba):"

typedef
struct {
	struct {
		unsigned char perm:2;	/* Permissions on this extent */
		unsigned char zero:2;	/* Must be zero */
		unsigned char da:1;	/* usually zero */
		unsigned char diag:1;	/* allow diagnose */
		unsigned char zero2:2;	/* zero */
	} __attribute__ ((packed)) mask;
	unsigned char zero;	/* Must be zero */
	unsigned short blk_size;	/* Blocksize */
	unsigned long ext_loc;	/* Extent locator */
	unsigned long ext_beg;	/* logical number of block 0 in extent */
	unsigned long ext_end;	/* logocal number of last block in extent */
} __attribute__ ((packed, aligned (32)))

DE_fba_data_t;

typedef
struct {
	struct {
		unsigned char zero:4;
		unsigned char cmd:4;
	} __attribute__ ((packed)) operation;
	unsigned char auxiliary;
	unsigned short blk_ct;
	unsigned long blk_nr;
} __attribute__ ((packed, aligned (32)))

LO_fba_data_t;

static void
define_extent (ccw1_t * ccw, DE_fba_data_t * DE_data, int rw,
	       int blksize, int beg, int nr)
{
	memset (DE_data, 0, sizeof (DE_fba_data_t));
	ccw->cmd_code = CCW_DEFINE_EXTENT;
	ccw->count = 16;
	ccw->cda = (void *) __pa (DE_data);
	if (rw == WRITE)
		(DE_data->mask).perm = 0x0;
	else if (rw == READ)
		(DE_data->mask).perm = 0x1;
	else
		DE_data->mask.perm = 0x2;
	DE_data->blk_size = blksize;
	DE_data->ext_loc = beg;
	DE_data->ext_end = nr - 1;
}

static void
locate_record (ccw1_t * ccw, LO_fba_data_t * LO_data, int rw, int block_nr,
	       int block_ct)
{
	memset (LO_data, 0, sizeof (LO_fba_data_t));
	ccw->cmd_code = DASD_FBA_CCW_LOCATE;
	ccw->count = 8;
	ccw->cda = (void *) __pa (LO_data);
	if (rw == WRITE)
		LO_data->operation.cmd = 0x5;
	else if (rw == READ)
		LO_data->operation.cmd = 0x6;
	else
		LO_data->operation.cmd = 0x8;
	LO_data->blk_nr = block_nr;
	LO_data->blk_ct = block_ct;
}

int
dasd_fba_ck_devinfo (dev_info_t * info)
{
	return 0;
}

cqr_t *
dasd_fba_build_req (int devindex,
		    struct request * req)
{
	cqr_t *rw_cp = NULL;
	ccw1_t *ccw;

	DE_fba_data_t *DE_data;
	LO_fba_data_t *LO_data;
	struct buffer_head *bh;
	int rw_cmd;
	int byt_per_blk = dasd_info[devindex]->sizes.bp_block;
	int bhct;
	long size;

	if (!req->nr_sectors) {
		PRINT_ERR ("No blocks to write...returning\n");
		return NULL;
	}
	if (req->cmd == READ) {
		rw_cmd = DASD_FBA_CCW_READ;
	} else
#if DASD_PARANOIA > 2
	if (req->cmd == WRITE)
#endif				/* DASD_PARANOIA */
	{
		rw_cmd = DASD_FBA_CCW_WRITE;
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
	rw_cp = request_cqr (2 + bhct,
			     sizeof (DE_fba_data_t) +
			     sizeof (LO_fba_data_t));
	if (!rw_cp) {
		return NULL;
	}
	DE_data = rw_cp->data;
	LO_data = rw_cp->data + sizeof (DE_fba_data_t);
	ccw = rw_cp->cpaddr;

	define_extent (ccw, DE_data, req->cmd, byt_per_blk,
		       req->sector, req->nr_sectors);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	locate_record (ccw, LO_data, req->cmd, 0, req->nr_sectors);
	ccw->flags = CCW_FLAG_CC;
	for (bh = req->bh; bh;) {
		if (bh->b_size > byt_per_blk) {
			for (size = 0; size < bh->b_size; size += byt_per_blk) {
				ccw++;
				if (dasd_info[devindex]->rdc_data->fba.mode.bits.data_chain) {
					ccw->flags = CCW_FLAG_DC;
				} else {
					ccw->flags = CCW_FLAG_CC;
				}
				ccw->cmd_code = rw_cmd;
				ccw->count = byt_per_blk;
				ccw->cda = (void *) __pa (bh->b_data + size);
			}
			bh = bh->b_reqnext;
		} else {	/* group N bhs to fit into byt_per_blk */
			for (size = 0; bh != NULL && size < byt_per_blk;) {
				ccw++;
				if (dasd_info[devindex]->rdc_data->fba.mode.bits.data_chain) {
					ccw->flags = CCW_FLAG_DC;
				} else {
					PRINT_WARN ("Cannot chain chunks smaller than one block\n");
					release_cqr (rw_cp);
					return NULL;
				}
				ccw->cmd_code = rw_cmd;
				ccw->count = bh->b_size;
				ccw->cda = (void *) __pa (bh->b_data);
				size += bh->b_size;
				bh = bh->b_reqnext;
			}
			ccw->flags = CCW_FLAG_CC;
			if (size != byt_per_blk) {
				PRINT_WARN ("Cannot fulfill request smaller than block\n");
				release_cqr (rw_cp);
				return NULL;
			}
		}
	}
	ccw->flags &= ~(CCW_FLAG_DC | CCW_FLAG_CC);
	return rw_cp;
}

void
dasd_fba_print_char (dasd_characteristics_t * ct)
{
	dasd_fba_characteristics_t *c =
	(dasd_fba_characteristics_t *) ct;
	PRINT_INFO ("%d blocks of %d bytes %d MB\n",
		    c->blk_bdsa, c->blk_size,
		    (c->blk_bdsa * (c->blk_size >> 9)) >> 11);
	PRINT_INFO ("%soverrun, %s mode, data chains %sallowed\n"
		    "%sremovable, %sshared\n",
		    (c->mode.bits.overrunnable ? "" : "no "),
		    (c->mode.bits.burst_byte ? "burst" : "byte"),
		    (c->mode.bits.data_chain ? "" : "not "),
		    (c->features.bits.removable ? "" : "not "),
		    (c->features.bits.shared ? "" : "not "));
};

int
dasd_fba_ck_char (dasd_characteristics_t * rdc)
{
	int rc = 0;
	dasd_fba_print_char (rdc);
	return rc;
}

cqr_t *
dasd_fba_fill_sizes_first (int di)
{
	cqr_t *rw_cp = NULL;
	ccw1_t *ccw;
	DE_fba_data_t *DE_data;
	LO_fba_data_t *LO_data;
	dasd_information_t *info = dasd_info[di];
	static char buffer[8];

	dasd_info[di]->sizes.label_block = 1;

	rw_cp = request_cqr (3,
			     sizeof (DE_fba_data_t) +
			     sizeof (LO_fba_data_t));
	DE_data = rw_cp->data;
	LO_data = rw_cp->data + sizeof (DE_fba_data_t);
	ccw = rw_cp->cpaddr;
	define_extent (ccw, DE_data, READ, info->sizes.bp_block, 1, 1);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	locate_record (ccw, LO_data, READ, 0, 1);
	ccw->flags = CCW_FLAG_CC;
	ccw++;
	ccw->cmd_code = DASD_FBA_CCW_READ;
	ccw->flags = CCW_FLAG_SLI;
	ccw->count = 8;
	ccw->cda = (void *) __pa (buffer);
	rw_cp->devindex = di;
	atomic_set (&rw_cp->status, CQR_STATUS_FILLED);
	return rw_cp;
}

int
dasd_fba_fill_sizes_last (int devindex)
{
	int rc = 0;
	int sb;
	dasd_information_t *info = dasd_info[devindex];

	info->sizes.bp_sector = info->rdc_data->fba.blk_size;
	info->sizes.bp_block = info->sizes.bp_sector;

	info->sizes.s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < info->sizes.bp_sector; sb = sb << 1)
		info->sizes.s2b_shift++;

	info->sizes.blocks = (info->rdc_data->fba.blk_bdsa);

	if (info->sizes.s2b_shift >= 1)
		info->sizes.kbytes = info->sizes.blocks <<
		    (info->sizes.s2b_shift - 1);
	else
		info->sizes.kbytes = info->sizes.blocks >>
		    (-(info->sizes.s2b_shift - 1));

	return rc;
}

int
dasd_fba_format (int devindex, format_data_t * fdata)
{
	int rc = 0;
	return rc;
}

void
dasd_fba_fill_geometry (int di, struct hd_geometry *geo)
{
	int bfactor, nr_sectors, sec_size;
	int trk_cap, trk_low, trk_high, tfactor, nr_trks, trk_size;
	int cfactor, nr_cyls, cyl_size;
	int remainder;

	dasd_information_t *info = dasd_info[di];
	PRINT_INFO ("FBA has no geometry! Faking one...\n%s", "");

	/* determine the blocking factor of sectors */
	for (bfactor = 8; bfactor > 0; bfactor--) {
		remainder = info->rdc_data->fba.blk_bdsa % bfactor;
		PRINT_INFO ("bfactor %d remainder %d\n", bfactor, remainder);
		if (!remainder)
			break;
	}
	nr_sectors = info->rdc_data->fba.blk_bdsa / bfactor;
	sec_size = info->rdc_data->fba.blk_size * bfactor;
	
	geo -> sectors = bfactor;

	/* determine the nr of sectors per track */
	trk_cap = (64 * 1 << 10) / sec_size;	/* 64k in sectors */
	trk_low = trk_cap * 2 / 3;
	trk_high = trk_cap * 4 / 3;
	for (tfactor = trk_high; tfactor > trk_low; tfactor--) {
		PRINT_INFO ("remainder %d\n", remainder);
		remainder = nr_sectors % bfactor;
		if (!remainder)
			break;
	}
	nr_trks = nr_sectors / tfactor;
	trk_size = sec_size * tfactor;

	/* determine the nr of trks per cylinder */
	for (cfactor = 31; cfactor > 0; cfactor--) {
		PRINT_INFO ("remainder %d\n", remainder);
		remainder = nr_trks % bfactor;
		if (!remainder)
			break;
	}
	nr_cyls = nr_trks / cfactor;
	sec_size = info->rdc_data->fba.blk_size * bfactor;

	geo -> heads = nr_trks;
	geo -> cylinders = nr_cyls;
	geo -> start = info->sizes.label_block + 1;
}

dasd_operations_t dasd_fba_operations =
{
	ck_devinfo:dasd_fba_ck_devinfo,
	get_req_ccw:dasd_fba_build_req,
	ck_characteristics:dasd_fba_ck_char,
	fill_sizes_first:dasd_fba_fill_sizes_first,
	fill_sizes_last:dasd_fba_fill_sizes_last,
	dasd_format:dasd_fba_format,
	fill_geometry:dasd_fba_fill_geometry
};
