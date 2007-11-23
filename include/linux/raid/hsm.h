#ifndef _LVM_H
#define _LVM_H

#include <linux/raid/md.h>

#if __alpha__
#error fix cpu_addr on Alpha first
#endif

#include <linux/raid/hsm_p.h>

#define index_pv(lv,index) ((lv)->vg->pv_array+(index)->data.phys_nr)
#define index_dev(lv,index) index_pv((lv),(index))->dev
#define index_block(lv,index) (index)->data.phys_block
#define index_child(index) ((lv_lptr_t *)((index)->cpu_addr))

#define ptr_to_cpuaddr(ptr) ((__u32) (ptr))


typedef struct pv_bg_desc_s {
	unsigned int		free_blocks;
	pv_block_group_t 	*bg;
} pv_bg_desc_t;

typedef struct pv_s pv_t;
typedef struct vg_s vg_t;
typedef struct lv_s lv_t;

struct pv_s
{
	int			phys_nr;
	kdev_t			dev;
	pv_sb_t			*pv_sb;
	pv_bg_desc_t	 	*bg_array;
};

struct lv_s
{
	int		log_id;
	vg_t		*vg;

	unsigned int	max_indices;
	unsigned int	free_indices;
	lv_lptr_t	root_index;

	kdev_t		dev;
};

struct vg_s
{
	int		nr_pv;
	pv_t		pv_array [MD_SB_DISKS];

	int		nr_lv;
	lv_t		lv_array [LVM_MAX_LVS_PER_VG];

	vg_sb_t		*vg_sb;
	mddev_t		*mddev;
};

#define kdev_to_lv(dev) ((lv_t *) mddev_map[MINOR(dev)].data)
#define mddev_to_vg(mddev) ((vg_t *) mddev->private)

#endif

