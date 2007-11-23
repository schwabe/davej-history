/*
   hsm.c : HSM RAID driver for Linux
              Copyright (C) 1998 Ingo Molnar

   HSM mode management functions.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#include <linux/module.h>

#include <linux/raid/md.h>
#include <linux/malloc.h>

#include <linux/raid/hsm.h>
#include <linux/blk.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY


#define DEBUG_HSM 1

#if DEBUG_HSM
#define dprintk(x,y...) printk(x,##y)
#else
#define dprintk(x,y...) do { } while (0)
#endif

void print_bh(struct buffer_head *bh)
{
	dprintk("bh %p: %lx %lx %x %x %lx %p %lx %p %x %p %x %lx\n", bh, 
		bh->b_blocknr, bh->b_size, bh->b_dev, bh->b_rdev,
		bh->b_rsector, bh->b_this_page, bh->b_state,
		bh->b_next_free, bh->b_count, bh->b_data,
		bh->b_list, bh->b_flushtime
	);
}

static int check_bg (pv_t *pv, pv_block_group_t * bg)
{
	int i, free = 0;

	dprintk("checking bg ...\n");

	for (i = 0; i < pv->pv_sb->pv_bg_size-1; i++) {
		if (pv_pptr_free(bg->blocks + i)) {
			free++;
			if (test_bit(i, bg->used_bitmap)) {
				printk("hm, bit %d set?\n", i);
			}
		} else {
			if (!test_bit(i, bg->used_bitmap)) {
				printk("hm, bit %d not set?\n", i);
			}
		}
	}
	dprintk("%d free blocks in bg ...\n", free);
	return free;
}

static void get_bg (pv_t *pv, pv_bg_desc_t *desc, int nr)
{
	unsigned int bg_pos = nr * pv->pv_sb->pv_bg_size + 2;
	struct buffer_head *bh;

	dprintk("... getting BG at %u ...\n", bg_pos);

        bh = bread (pv->dev, bg_pos, HSM_BLOCKSIZE);
	if (!bh) {
		MD_BUG();
		return;
	}
	desc->bg = (pv_block_group_t *) bh->b_data;
	desc->free_blocks = check_bg(pv, desc->bg);
}

static int find_free_block (lv_t *lv, pv_t *pv, pv_bg_desc_t *desc, int nr,
				unsigned int lblock, lv_lptr_t * index)
{
	int i;

	for (i = 0; i < pv->pv_sb->pv_bg_size-1; i++) {
		pv_pptr_t * bptr = desc->bg->blocks + i;
		if (pv_pptr_free(bptr)) {
			unsigned int bg_pos = nr * pv->pv_sb->pv_bg_size + 2;

			if (test_bit(i, desc->bg->used_bitmap)) {
				MD_BUG();
				continue;
			}
			bptr->u.used.owner.log_id = lv->log_id;
			bptr->u.used.owner.log_index = lblock;
			index->data.phys_nr = pv->phys_nr;
			index->data.phys_block = bg_pos + i + 1;
			set_bit(i, desc->bg->used_bitmap);
			desc->free_blocks--;
			dprintk(".....free blocks left in bg %p: %d\n",
					desc->bg, desc->free_blocks);
			return 0;
		}
	}
	return -ENOSPC;
}

static int __get_free_block (lv_t *lv, pv_t *pv,
					unsigned int lblock, lv_lptr_t * index)
{
	int i;

	dprintk("trying to get free block for lblock %d ...\n", lblock);

	for (i = 0; i < pv->pv_sb->pv_block_groups; i++) {
		pv_bg_desc_t *desc = pv->bg_array + i;

		dprintk("looking at desc #%d (%p)...\n", i, desc->bg);
		if (!desc->bg)
			get_bg(pv, desc, i);

		if (desc->bg && desc->free_blocks)
			return find_free_block(lv, pv, desc, i,
							lblock, index);
	}
	dprintk("hsm: pv %s full!\n", partition_name(pv->dev));
	return -ENOSPC;
}

static int get_free_block (lv_t *lv, unsigned int lblock, lv_lptr_t * index)
{
	int err;

	if (!lv->free_indices)
		return -ENOSPC;

 	/* fix me */
	err = __get_free_block(lv, lv->vg->pv_array + 0, lblock, index);

	if (err || !index->data.phys_block) {
		MD_BUG();
		return -ENOSPC;
	}

	lv->free_indices--;

	return 0;
}

/*
 * fix me: wordsize assumptions ...
 */
#define INDEX_BITS 8
#define INDEX_DEPTH (32/INDEX_BITS)
#define INDEX_MASK ((1<<INDEX_BITS) - 1)

static void print_index_list (lv_t *lv, lv_lptr_t *index)
{
	lv_lptr_t *tmp;
	int i;

	dprintk("... block <%u,%u,%x> [.", index->data.phys_nr,
		index->data.phys_block, index->cpu_addr);

	tmp = index_child(index);
	for (i = 0; i < HSM_LPTRS_PER_BLOCK; i++) {
		if (index_block(lv, tmp))
			dprintk("(%d->%d)", i, index_block(lv, tmp));
		tmp++;
	}
	dprintk(".]\n");
}

static int read_index_group (lv_t *lv, lv_lptr_t *index)
{
	lv_lptr_t *index_group, *tmp;
	struct buffer_head *bh;
	int i;

	dprintk("reading index group <%s:%d>\n",
		partition_name(index_dev(lv, index)), index_block(lv, index));

	bh = bread(index_dev(lv, index), index_block(lv, index), HSM_BLOCKSIZE);
	if (!bh) {
		MD_BUG();
		return -EIO;
	}
	if (!buffer_uptodate(bh))
		MD_BUG();

	index_group = (lv_lptr_t *) bh->b_data;
	tmp = index_group;
	for (i = 0; i < HSM_LPTRS_PER_BLOCK; i++) {
		if (index_block(lv, tmp)) {
			dprintk("index group has BLOCK %d, non-present.\n", i);
			tmp->cpu_addr = 0;
		}
		tmp++;
	}
	index->cpu_addr = ptr_to_cpuaddr(index_group);

	dprintk("have read index group %p at block %d.\n",
				index_group, index_block(lv, index));
	print_index_list(lv, index);

	return 0;
}

static int alloc_index_group (lv_t *lv, unsigned int lblock, lv_lptr_t * index)
{
	struct buffer_head *bh;
	lv_lptr_t * index_group;
	
	if (get_free_block(lv, lblock, index))
		return -ENOSPC;

	dprintk("creating block for index group <%s:%d>\n",
		partition_name(index_dev(lv, index)), index_block(lv, index));

	bh = getblk(index_dev(lv, index),
			 index_block(lv, index), HSM_BLOCKSIZE);

	index_group = (lv_lptr_t *) bh->b_data;
	md_clear_page(index_group);
	mark_buffer_uptodate(bh, 1);

	index->cpu_addr = ptr_to_cpuaddr(index_group);

	dprintk("allocated index group %p at block %d.\n",
				index_group, index_block(lv, index));
	return 0;
}

static lv_lptr_t * alloc_fixed_index (lv_t *lv, unsigned int lblock)
{
	lv_lptr_t * index = index_child(&lv->root_index);
	int idx, l;

	for (l = INDEX_DEPTH-1; l >= 0; l--) {
		idx = (lblock >> (INDEX_BITS*l)) & INDEX_MASK;
		index += idx;
		if (!l)
			break;
		if (!index_present(index)) {
			dprintk("no group, level %u, pos %u\n", l, idx);
			if (alloc_index_group(lv, lblock, index))
				return NULL;
		}
		index = index_child(index);
	}
	if (!index_block(lv,index)) {
		dprintk("no data, pos %u\n", idx);
		if (get_free_block(lv, lblock, index))
			return NULL;
		return index;
	}
	MD_BUG();
	return index;
}

static lv_lptr_t * find_index (lv_t *lv, unsigned int lblock)
{
	lv_lptr_t * index = index_child(&lv->root_index);
	int idx, l;

	for (l = INDEX_DEPTH-1; l >= 0; l--) {
		idx = (lblock >> (INDEX_BITS*l)) & INDEX_MASK;
		index += idx;
		if (!l)
			break;
		if (index_free(index))
			return NULL;
		if (!index_present(index))
			read_index_group(lv, index);
		if (!index_present(index)) {
			MD_BUG();
			return NULL;
		}
		index = index_child(index);
	}
	if (!index_block(lv,index))
		return NULL;
	return index;
}

static int read_root_index(lv_t *lv)
{
	int err;
	lv_lptr_t *index = &lv->root_index;

	if (!index_block(lv, index)) {
		printk("LV has no root index yet, creating.\n");

		err = alloc_index_group (lv, 0, index);
		if (err) {
			printk("could not create index group, err:%d\n", err);
			return err;
		}
		lv->vg->vg_sb->lv_array[lv->log_id].lv_root_idx =
					lv->root_index.data;
	} else {
		printk("LV already has a root index.\n");
		printk("... at <%s:%d>.\n",
			partition_name(index_dev(lv, index)),
			index_block(lv, index));

		read_index_group(lv, index);
	}
	return 0;
}

static int init_pv(pv_t *pv)
{
	struct buffer_head *bh;
	pv_sb_t *pv_sb;

        bh = bread (pv->dev, 0, HSM_BLOCKSIZE);
	if (!bh) {
		MD_BUG();
		return -1;
	}

	pv_sb = (pv_sb_t *) bh->b_data;
	pv->pv_sb = pv_sb;

	if (pv_sb->pv_magic != HSM_PV_SB_MAGIC) {
		printk("%s is not a PV, has magic %x instead of %x!\n",
			partition_name(pv->dev), pv_sb->pv_magic,
			HSM_PV_SB_MAGIC);
		return -1;
	}
	printk("%s detected as a valid PV (#%d).\n", partition_name(pv->dev),
							pv->phys_nr);
	printk("... created under HSM version %d.%d.%d, at %x.\n",
	    pv_sb->pv_major, pv_sb->pv_minor, pv_sb->pv_patch, pv_sb->pv_ctime);
	printk("... total # of blocks: %d (%d left unallocated).\n",
			 pv_sb->pv_total_size, pv_sb->pv_blocks_left);

	printk("... block size: %d bytes.\n", pv_sb->pv_block_size);
	printk("... block descriptor size: %d bytes.\n", pv_sb->pv_pptr_size);
	printk("... block group size: %d blocks.\n", pv_sb->pv_bg_size);
	printk("... # of block groups: %d.\n", pv_sb->pv_block_groups);

	if (pv_sb->pv_block_groups*sizeof(pv_bg_desc_t) > PAGE_SIZE) {
		MD_BUG();
		return 1;
	}
	pv->bg_array = (pv_bg_desc_t *)__get_free_page(GFP_KERNEL);
	if (!pv->bg_array) {
		MD_BUG();
		return 1;
	}
	memset(pv->bg_array, 0, PAGE_SIZE);

	return 0;
}

static int free_pv(pv_t *pv)
{
	struct buffer_head *bh;

	dprintk("freeing PV %d ...\n", pv->phys_nr);

	if (pv->bg_array) {
		int i;

		dprintk(".... freeing BGs ...\n");
		for (i = 0; i < pv->pv_sb->pv_block_groups; i++) {
			unsigned int bg_pos = i * pv->pv_sb->pv_bg_size + 2;
			pv_bg_desc_t *desc = pv->bg_array + i;

			if (desc->bg) {
				dprintk(".... freeing BG %d ...\n", i);
	        		bh = getblk (pv->dev, bg_pos, HSM_BLOCKSIZE);
				mark_buffer_dirty(bh, 1);
				brelse(bh);
				brelse(bh);
			}
		}
		free_page((unsigned long)pv->bg_array);
	} else
		MD_BUG();

        bh = getblk (pv->dev, 0, HSM_BLOCKSIZE);
	if (!bh) {
		MD_BUG();
		return -1;
	}
	mark_buffer_dirty(bh, 1);
	brelse(bh);
	brelse(bh);

	return 0;
}

struct semaphore hsm_sem = MUTEX;

#define HSM_SECTORS (HSM_BLOCKSIZE/512)

static int hsm_map (mddev_t *mddev, kdev_t dev, kdev_t *rdev,
			unsigned long *rsector, unsigned long bsectors)
{
	lv_t *lv = kdev_to_lv(dev);
	lv_lptr_t *index;
	unsigned int lblock = *rsector / HSM_SECTORS;
	unsigned int offset = *rsector % HSM_SECTORS;
	int err = -EIO;

	if (!lv) {
		printk("HSM: md%d not a Logical Volume!\n", mdidx(mddev));
		goto out;
	}
	if (offset + bsectors > HSM_SECTORS) {
		MD_BUG();
		goto out;
	}
	down(&hsm_sem);
	index = find_index(lv, lblock);
	if (!index) {
		printk("no block %u yet ... allocating\n", lblock);
		index = alloc_fixed_index(lv, lblock);
	}

	err = 0;

	printk(" %u <%s : %ld(%ld)> -> ", lblock,
		partition_name(*rdev), *rsector, bsectors);

	*rdev = index_dev(lv, index);
	*rsector = index_block(lv, index) * HSM_SECTORS + offset;

	printk(" <%s : %ld> %u\n",
		partition_name(*rdev), *rsector, index_block(lv, index));

	up(&hsm_sem);
out:
	return err;
}

static void free_index (lv_t *lv, lv_lptr_t * index)
{
	struct buffer_head *bh;

	printk("tryin to get cached block for index group <%s:%d>\n",
		partition_name(index_dev(lv, index)), index_block(lv, index));

	bh = getblk(index_dev(lv, index), index_block(lv, index),HSM_BLOCKSIZE);

	printk("....FREEING ");
	print_index_list(lv, index);

	if (bh) {
		if (!buffer_uptodate(bh))
			MD_BUG();
		if ((lv_lptr_t *)bh->b_data != index_child(index)) {
			printk("huh? b_data is %p, index content is %p.\n",
				bh->b_data, index_child(index));
		} else 
			printk("good, b_data == index content == %p.\n",
				index_child(index));
		printk("b_count == %d, writing.\n", bh->b_count);
		mark_buffer_dirty(bh, 1);
		brelse(bh);
		brelse(bh);
		printk("done.\n");
	} else {
		printk("FAILED!\n");
	}
	print_index_list(lv, index);
	index_child(index) = NULL;
}

static void free_index_group (lv_t *lv, int level, lv_lptr_t * index_0)
{
	char dots [3*8];
	lv_lptr_t * index;
	int i, nr_dots;

	nr_dots = (INDEX_DEPTH-level)*3;
 	memcpy(dots,"...............",nr_dots);
	dots[nr_dots] = 0;

	dprintk("%s level %d index group block:\n", dots, level);


	index = index_0;
	for (i = 0; i < HSM_LPTRS_PER_BLOCK; i++) {
		if (index->data.phys_block) {
			dprintk("%s block <%u,%u,%x>\n", dots,
				index->data.phys_nr,
				index->data.phys_block,
				index->cpu_addr);
			if (level && index_present(index)) {
				dprintk("%s==> deeper one level\n", dots);
				free_index_group(lv, level-1,
						index_child(index));
				dprintk("%s freeing index group block %p ...",
						dots, index_child(index));
				free_index(lv, index);
			}
		}
		index++;
	}
	dprintk("%s DONE: level %d index group block.\n", dots, level);
}

static void free_lv_indextree (lv_t *lv)
{
	dprintk("freeing LV %d ...\n", lv->log_id);
	dprintk("..root index: %p\n", index_child(&lv->root_index));
	dprintk("..INDEX TREE:\n");
	free_index_group(lv, INDEX_DEPTH-1, index_child(&lv->root_index));
	dprintk("..freeing root index %p ...", index_child(&lv->root_index));
	dprintk("root block <%u,%u,%x>\n", lv->root_index.data.phys_nr,
		lv->root_index.data.phys_block, lv->root_index.cpu_addr);
	free_index(lv, &lv->root_index);
	dprintk("..INDEX TREE done.\n");
	fsync_dev(lv->vg->pv_array[0].dev); /* fix me */
	lv->vg->vg_sb->lv_array[lv->log_id].lv_free_indices = lv->free_indices;
}

static void print_index_group (lv_t *lv, int level, lv_lptr_t * index_0)
{
	char dots [3*5];
	lv_lptr_t * index;
	int i, nr_dots;

	nr_dots = (INDEX_DEPTH-level)*3;
 	memcpy(dots,"...............",nr_dots);
	dots[nr_dots] = 0;

	dprintk("%s level %d index group block:\n", dots, level);


	for (i = 0; i < HSM_LPTRS_PER_BLOCK; i++) {
		index = index_0 + i;
		if (index->data.phys_block) {
			dprintk("%s block <%u,%u,%x>\n", dots,
				index->data.phys_nr,
				index->data.phys_block,
				index->cpu_addr);
			if (level && index_present(index)) {
				dprintk("%s==> deeper one level\n", dots);
				print_index_group(lv, level-1,
							index_child(index));
			}
		}
	}
	dprintk("%s DONE: level %d index group block.\n", dots, level);
}

static void print_lv (lv_t *lv)
{
	dprintk("printing LV %d ...\n", lv->log_id);
	dprintk("..root index: %p\n", index_child(&lv->root_index));
	dprintk("..INDEX TREE:\n");
	print_index_group(lv, INDEX_DEPTH-1, index_child(&lv->root_index));
	dprintk("..INDEX TREE done.\n");
}

static int map_lv (lv_t *lv)
{
	kdev_t dev = lv->dev;
	unsigned int nr = MINOR(dev);
	mddev_t *mddev = lv->vg->mddev;

	if (MAJOR(dev) != MD_MAJOR) {
		MD_BUG();
		return -1;
	}
	if (kdev_to_mddev(dev)) {
		MD_BUG();
		return -1;
	}
	md_hd_struct[nr].start_sect = 0;
	md_hd_struct[nr].nr_sects = md_size[mdidx(mddev)] << 1;
	md_size[nr] = md_size[mdidx(mddev)];
	add_mddev_mapping(mddev, dev, lv);

	return 0;
}

static int unmap_lv (lv_t *lv)
{
	kdev_t dev = lv->dev;
	unsigned int nr = MINOR(dev);

	if (MAJOR(dev) != MD_MAJOR) {
		MD_BUG();
		return -1;
	}
	md_hd_struct[nr].start_sect = 0;
	md_hd_struct[nr].nr_sects = 0;
	md_size[nr] = 0;
	del_mddev_mapping(lv->vg->mddev, dev);

	return 0;
}

static int init_vg (vg_t *vg)
{
	int i;
	lv_t *lv;
	kdev_t dev;
	vg_sb_t *vg_sb;
	struct buffer_head *bh;
	lv_descriptor_t *lv_desc;

	/*
	 * fix me: read all PVs and compare the SB
	 */
        dev = vg->pv_array[0].dev;
        bh = bread (dev, 1, HSM_BLOCKSIZE);
	if (!bh) {
		MD_BUG();
		return -1;
	}

	vg_sb = (vg_sb_t *) bh->b_data;
	vg->vg_sb = vg_sb;

	if (vg_sb->vg_magic != HSM_VG_SB_MAGIC) {
		printk("%s is not a valid VG, has magic %x instead of %x!\n",
			partition_name(dev), vg_sb->vg_magic,
			HSM_VG_SB_MAGIC);
		return -1;
	}

	vg->nr_lv = 0;
	for (i = 0; i < HSM_MAX_LVS_PER_VG; i++) {
		unsigned int id;
		lv_desc = vg->vg_sb->lv_array + i;

		id = lv_desc->lv_id;
		if (!id) {
			printk("... LV desc %d empty\n", i);
			continue;
		}
		if (id >= HSM_MAX_LVS_PER_VG) {
			MD_BUG();
			continue;
		}

		lv = vg->lv_array + id;
		if (lv->vg) {
			MD_BUG();
			continue;
		}
		lv->log_id = id;
		lv->vg = vg;
		lv->max_indices = lv_desc->lv_max_indices;
		lv->free_indices = lv_desc->lv_free_indices;
		lv->root_index.data = lv_desc->lv_root_idx;
		lv->dev = MKDEV(MD_MAJOR, lv_desc->md_id);

		vg->nr_lv++;

		map_lv(lv);
		if (read_root_index(lv)) {
			vg->nr_lv--;
			unmap_lv(lv);
			memset(lv, 0, sizeof(*lv));
		}
	}
	if (vg->nr_lv != vg_sb->nr_lvs)
		MD_BUG();

	return 0;
}

static int hsm_run (mddev_t *mddev)
{
	int i;
	vg_t *vg;
	mdk_rdev_t *rdev;

	MOD_INC_USE_COUNT;

	vg = kmalloc (sizeof (*vg), GFP_KERNEL);
	if (!vg)
		goto out;
	memset(vg, 0, sizeof(*vg));
	mddev->private = vg;
	vg->mddev = mddev;

	if (md_check_ordering(mddev)) {
		printk("hsm: disks are not ordered, aborting!\n");
		goto out;
	}

	set_blocksize (mddev_to_kdev(mddev), HSM_BLOCKSIZE);

	vg->nr_pv = mddev->nb_dev;
	ITERATE_RDEV_ORDERED(mddev,rdev,i) {
		pv_t *pv = vg->pv_array + i;

		pv->dev = rdev->dev;
		fsync_dev (pv->dev);
		set_blocksize (pv->dev, HSM_BLOCKSIZE);
		pv->phys_nr = i;
		if (init_pv(pv))
			goto out;
	}

	init_vg(vg);

	return 0;

out:
	if (vg) {
		kfree(vg);
		mddev->private = NULL;
	}
	MOD_DEC_USE_COUNT;

	return 1;
}

static int hsm_stop (mddev_t *mddev)
{
	lv_t *lv;
	vg_t *vg;
	int i;

	vg = mddev_to_vg(mddev);

	for (i = 0; i < HSM_MAX_LVS_PER_VG; i++) {
		lv = vg->lv_array + i;
		if (!lv->log_id)
			continue;
		print_lv(lv);
		free_lv_indextree(lv);
		unmap_lv(lv);
	}
	for (i = 0; i < vg->nr_pv; i++)
		free_pv(vg->pv_array + i);

	kfree(vg);

	MOD_DEC_USE_COUNT;

	return 0;
}


static int hsm_status (char *page, mddev_t *mddev)
{
	int sz = 0, i;
	lv_t *lv;
	vg_t *vg;

	vg = mddev_to_vg(mddev);

	for (i = 0; i < HSM_MAX_LVS_PER_VG; i++) {
		lv = vg->lv_array + i;
		if (!lv->log_id)
			continue;
		sz += sprintf(page+sz, "<LV%d %d/%d blocks used> ", lv->log_id,
			lv->max_indices - lv->free_indices, lv->max_indices);
	}
	return sz;
}


static mdk_personality_t hsm_personality=
{
	"hsm",
	hsm_map,
	NULL,
	NULL,
	hsm_run,
	hsm_stop,
	hsm_status,
	NULL,
	0,
	NULL,
	NULL,
	NULL,
	NULL
};

#ifndef MODULE

md__initfunc(void hsm_init (void))
{
	register_md_personality (HSM, &hsm_personality);
}

#else

int init_module (void)
{
	return (register_md_personality (HSM, &hsm_personality));
}

void cleanup_module (void)
{
	unregister_md_personality (HSM);
}

#endif

/*
 * This Linus-trick catches bugs via the linker.
 */

extern void __BUG__in__hsm_dot_c_1(void);
extern void __BUG__in__hsm_dot_c_2(void);
extern void __BUG__in__hsm_dot_c_3(void);
extern void __BUG__in__hsm_dot_c_4(void);
extern void __BUG__in__hsm_dot_c_5(void);
extern void __BUG__in__hsm_dot_c_6(void);
extern void __BUG__in__hsm_dot_c_7(void);
 
void bugcatcher (void)
{
        if (sizeof(pv_block_group_t) != HSM_BLOCKSIZE)
                __BUG__in__hsm_dot_c_1();
        if (sizeof(lv_index_block_t) != HSM_BLOCKSIZE)
                __BUG__in__hsm_dot_c_2();

        if (sizeof(pv_sb_t) != HSM_BLOCKSIZE)
                __BUG__in__hsm_dot_c_4();
        if (sizeof(lv_sb_t) != HSM_BLOCKSIZE)
                __BUG__in__hsm_dot_c_3();
	if (sizeof(vg_sb_t) != HSM_BLOCKSIZE)
                __BUG__in__hsm_dot_c_6();

	if (sizeof(lv_lptr_t) != 16)
                __BUG__in__hsm_dot_c_5();
	if (sizeof(pv_pptr_t) != 16)
                __BUG__in__hsm_dot_c_6();
}

