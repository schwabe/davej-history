#ifndef _TRANSLUCENT_H
#define _TRANSLUCENT_H

#include <linux/raid/md.h>

typedef struct dev_info dev_info_t;

struct dev_info {
	kdev_t		dev;
	int		size;
};

struct translucent_private_data
{
	dev_info_t		disks[MD_SB_DISKS];
};


typedef struct translucent_private_data translucent_conf_t;

#define mddev_to_conf(mddev) ((translucent_conf_t *) mddev->private)

#endif
