#ifndef _RAID5_H
#define _RAID5_H

#include <linux/md.h>
#include <asm/atomic.h>

struct disk_info {
	kdev_t	dev;
	int	operational;
	int	number;
	int	raid_disk;
};

struct raid5_data {
	struct md_dev		*mddev;
	struct md_thread	*thread;
	struct disk_info	disks[MD_SB_DISKS];
	int			buffer_size;
	int			chunk_size, level, algorithm;
	int			raid_disks, working_disks, failed_disks;
	int			sector_count;
	unsigned long		next_sector;
	atomic_t		nr_handle;
};

/*
 * Our supported algorithms
 */
#define ALGORITHM_LEFT_ASYMMETRIC	0
#define ALGORITHM_RIGHT_ASYMMETRIC	1
#define ALGORITHM_LEFT_SYMMETRIC	2
#define ALGORITHM_RIGHT_SYMMETRIC	3

#endif
