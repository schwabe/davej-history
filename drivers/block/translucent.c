/*
   translucent.c : Translucent RAID driver for Linux
              Copyright (C) 1998 Ingo Molnar

   Translucent mode management functions.

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

#include <linux/raid/translucent.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY

static int translucent_run (mddev_t *mddev)
{
	translucent_conf_t *conf;
	mdk_rdev_t *rdev;
	int i;

	MOD_INC_USE_COUNT;

	conf = kmalloc (sizeof (*conf), GFP_KERNEL);
	if (!conf)
		goto out;
	mddev->private = conf;

	if (mddev->nb_dev != 2) {
		printk("translucent: this mode needs 2 disks, aborting!\n");
		goto out;
	}

	if (md_check_ordering(mddev)) {
		printk("translucent: disks are not ordered, aborting!\n");
		goto out;
	}

	ITERATE_RDEV_ORDERED(mddev,rdev,i) {
		dev_info_t *disk = conf->disks + i;

		disk->dev = rdev->dev;
		disk->size = rdev->size;
	}

	return 0;

out:
	if (conf)
		kfree(conf);

	MOD_DEC_USE_COUNT;
	return 1;
}

static int translucent_stop (mddev_t *mddev)
{
	translucent_conf_t *conf = mddev_to_conf(mddev);
  
	kfree(conf);

	MOD_DEC_USE_COUNT;

	return 0;
}


static int translucent_map (mddev_t *mddev, kdev_t dev, kdev_t *rdev,
		       unsigned long *rsector, unsigned long size)
{
	translucent_conf_t *conf = mddev_to_conf(mddev);
  
	*rdev = conf->disks[0].dev;

	return 0;
}

static int translucent_status (char *page, mddev_t *mddev)
{
	int sz = 0;
  
	sz += sprintf(page+sz, " %d%% full", 10);
	return sz;
}


static mdk_personality_t translucent_personality=
{
	"translucent",
	translucent_map,
	NULL,
	NULL,
	translucent_run,
	translucent_stop,
	translucent_status,
	NULL,
	0,
	NULL,
	NULL,
	NULL,
	NULL
};

#ifndef MODULE

md__initfunc(void translucent_init (void))
{
	register_md_personality (TRANSLUCENT, &translucent_personality);
}

#else

int init_module (void)
{
	return (register_md_personality (TRANSLUCENT, &translucent_personality));
}

void cleanup_module (void)
{
	unregister_md_personality (TRANSLUCENT);
}

#endif

