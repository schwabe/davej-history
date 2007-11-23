
/*
   md.c : Multiple Devices driver for Linux
          Copyright (C) 1994-96 Marc ZYNGIER
	  <zyngier@ufr-info-p7.ibp.fr> or
	  <maz@gloups.fdn.fr>

   A lot of inspiration came from hd.c ...

   kerneld support by Boris Tobotras <boris@xtalk.msk.su>

   RAID-1/RAID-5 extensions by:
        Ingo Molnar, Miguel de Icaza, Gadi Oxman
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/md.h>
#include <linux/hdreg.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/smp_lock.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif
#include <linux/errno.h>
/*
 * For kernel_thread()
 */
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER

#include <linux/blk.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

static struct hd_struct md_hd_struct[MAX_MD_DEV];
static int md_blocksizes[MAX_MD_DEV];
static struct md_thread md_threads[MAX_MD_THREADS];

int md_size[MAX_MD_DEV]={0, };

static void md_geninit (struct gendisk *);

static struct gendisk md_gendisk=
{
  MD_MAJOR,
  "md",
  0,
  1,
  MAX_MD_DEV,
  md_geninit,
  md_hd_struct,
  md_size,
  MAX_MD_DEV,
  NULL,
  NULL
};

static struct md_personality *pers[MAX_PERSONALITY]={NULL, };

struct md_dev md_dev[MAX_MD_DEV];

static struct gendisk *find_gendisk (kdev_t dev)
{
  struct gendisk *tmp=gendisk_head;

  while (tmp != NULL)
  {
    if (tmp->major==MAJOR(dev))
      return (tmp);
    
    tmp=tmp->next;
  }

  return (NULL);
}


char *partition_name (kdev_t dev)
{
  static char name[40];		/* This should be long
				   enough for a device name ! */
  struct gendisk *hd = find_gendisk (dev);

  if (!hd)
  {
    sprintf (name, "[dev %s]", kdevname(dev));
    return (name);
  }

  return disk_name (hd, MINOR(dev), name);  /* routine in genhd.c */
}


static void set_ra (void)
{
  int i, j, minra=INT_MAX;

  for (i=0; i<MAX_MD_DEV; i++)
  {
    if (!md_dev[i].pers)
      continue;
    
    for (j=0; j<md_dev[i].nb_dev; j++)
      if (read_ahead[MAJOR(md_dev[i].devices[j].dev)]<minra)
	minra=read_ahead[MAJOR(md_dev[i].devices[j].dev)];
  }
  
  read_ahead[MD_MAJOR]=minra;
}

static int legacy_raid_sb (int minor, int pnum)
{
	int i, factor;

	factor = 1 << FACTOR_SHIFT(FACTOR((md_dev+minor)));

	/*****
	 * do size and offset calculations.
	 */
	for (i=0; i<md_dev[minor].nb_dev; i++) {
		md_dev[minor].devices[i].size &= ~(factor - 1);
		md_size[minor] += md_dev[minor].devices[i].size;
		md_dev[minor].devices[i].offset=i ? (md_dev[minor].devices[i-1].offset + 
							md_dev[minor].devices[i-1].size) : 0;
	}
	return 0;
}

static void free_sb (struct md_dev *mddev)
{
	int i;
	struct real_dev *realdev;

	if (mddev->sb) {
		free_page((unsigned long) mddev->sb);
		mddev->sb = NULL;
	}
	for (i = 0; i <mddev->nb_dev; i++) {
		realdev = mddev->devices + i;
		if (realdev->sb) {
			free_page((unsigned long) realdev->sb);
			realdev->sb = NULL;
		}
	}
}

static int analyze_sb (int minor, int pnum)
{
	int i;
	struct md_dev *mddev = md_dev + minor;
	struct buffer_head *bh;
	kdev_t dev;
	struct real_dev *realdev;
	u32 sb_offset, device_size;
	md_superblock_t *sb = NULL;

	/*
	 * raid-0 and linear don't use a raid superblock
	 */
	if (pnum == RAID0 >> PERSONALITY_SHIFT || pnum == LINEAR >> PERSONALITY_SHIFT)
		return legacy_raid_sb(minor, pnum);
	
	/*
	 * Verify the raid superblock on each real device
	 */
	for (i = 0; i < mddev->nb_dev; i++) {
		realdev = mddev->devices + i;
		dev = realdev->dev;
		device_size = blk_size[MAJOR(dev)][MINOR(dev)];
		realdev->sb_offset = sb_offset = MD_NEW_SIZE_BLOCKS(device_size);
		set_blocksize(dev, MD_SB_BYTES);
		bh = bread(dev, sb_offset / MD_SB_BLOCKS, MD_SB_BYTES);
		if (bh) {
			sb = (md_superblock_t *) bh->b_data;
			if (sb->md_magic != MD_SB_MAGIC) {
				printk("md: %s: invalid raid superblock magic (%x) on block %u\n", kdevname(dev), sb->md_magic, sb_offset);
				goto abort;
			}
			if (!mddev->sb) {
				mddev->sb = (md_superblock_t *) __get_free_page(GFP_KERNEL);
				if (!mddev->sb)
					goto abort;
				memcpy(mddev->sb, sb, MD_SB_BYTES);
			}
			realdev->sb = (md_superblock_t *) __get_free_page(GFP_KERNEL);
			if (!realdev->sb)
				goto abort;
			memcpy(realdev->sb, bh->b_data, MD_SB_BYTES);

			if (memcmp(mddev->sb, sb, MD_SB_GENERIC_CONSTANT_WORDS * 4)) {
				printk(KERN_ERR "md: superblock inconsistenty -- run ckraid\n");
				goto abort;
			}
			/*
			 * Find the newest superblock version
			 */
			if (sb->utime != mddev->sb->utime) {
				printk(KERN_ERR "md: superblock update time inconsistenty -- using the most recent one\n");
				if (sb->utime > mddev->sb->utime)
					memcpy(mddev->sb, sb, MD_SB_BYTES);
			}
			realdev->size = sb->size;
		} else
			printk(KERN_ERR "md: disabled device %s\n", kdevname(dev));
	}
	if (!mddev->sb) {
		printk(KERN_ERR "md: couldn't access raid array %s\n", kdevname(MKDEV(MD_MAJOR, minor)));
		goto abort;
	}
	sb = mddev->sb;

	/*
	 * Check if we can support this raid array
	 */
	if (sb->major_version != MD_MAJOR_VERSION || sb->minor_version > MD_MINOR_VERSION) {
		printk("md: %s: unsupported raid array version %d.%d.%d\n", kdevname(MKDEV(MD_MAJOR, minor)),
		sb->major_version, sb->minor_version, sb->patch_version);
		goto abort;
	}
	if (sb->state != (1 << MD_SB_CLEAN)) {
		printk(KERN_ERR "md: %s: raid array is not clean -- run ckraid\n", kdevname(MKDEV(MD_MAJOR, minor)));
		goto abort;
	}
	switch (sb->level) {
		case 1:
			md_size[minor] = sb->size;
			break;
		case 4:
		case 5:
			md_size[minor] = sb->size * (sb->raid_disks - 1);
			break;
		default:
			printk(KERN_ERR "md: %s: unsupported raid level %d\n", kdevname(MKDEV(MD_MAJOR, minor)), sb->level);
			goto abort;
	}
	return 0;
abort:
	free_sb(mddev);
	return 1;
}

int md_update_sb(int minor)
{
	struct md_dev *mddev = md_dev + minor;
	struct buffer_head *bh;
	md_superblock_t *sb = mddev->sb;
	struct real_dev *realdev;
	kdev_t dev;
	int i;
	u32 sb_offset;

	sb->utime = CURRENT_TIME;
	for (i = 0; i < mddev->nb_dev; i++) {
		realdev = mddev->devices + i;
		if (!realdev->sb)
			continue;
		dev = realdev->dev;
		sb_offset = realdev->sb_offset;
		set_blocksize(dev, MD_SB_BYTES);
		printk("md: updating raid superblock on device %s, sb_offset == %u\n", kdevname(dev), sb_offset);
		bh = getblk(dev, sb_offset / MD_SB_BLOCKS, MD_SB_BYTES);
		if (bh) {
			sb = (md_superblock_t *) bh->b_data;
			memcpy(sb, mddev->sb, MD_SB_BYTES);
			memcpy(&sb->descriptor, sb->disks + realdev->sb->descriptor.number, MD_SB_DESCRIPTOR_WORDS * 4);
			mark_buffer_uptodate(bh, 1);
			mark_buffer_dirty(bh, 1);
			ll_rw_block(WRITE, 1, &bh);
			wait_on_buffer(bh);
			bforget(bh);
			fsync_dev(dev);
			invalidate_buffers(dev);
		} else
			printk(KERN_ERR "md: getblk failed for device %s\n", kdevname(dev));
	}
	return 0;
}

static int do_md_run (int minor, int repart)
{
  int pnum, i, min, factor, current_ra, err;

  if (!md_dev[minor].nb_dev)
    return -EINVAL;
  
  if (md_dev[minor].pers)
    return -EBUSY;

  md_dev[minor].repartition=repart;
  
  if ((pnum=PERSONALITY(&md_dev[minor]) >> (PERSONALITY_SHIFT))
      >= MAX_PERSONALITY)
    return -EINVAL;

  /* Only RAID-1 and RAID-5 can have MD devices as underlying devices */
  if (pnum != (RAID1 >> PERSONALITY_SHIFT) && pnum != (RAID5 >> PERSONALITY_SHIFT)){
	  for (i = 0; i < md_dev [minor].nb_dev; i++)
		  if (MAJOR (md_dev [minor].devices [i].dev) == MD_MAJOR)
			  return -EINVAL;
  }
  if (!pers[pnum])
  {
#ifdef CONFIG_KERNELD
    char module_name[80];
    sprintf (module_name, "md-personality-%d", pnum);
    request_module (module_name);
    if (!pers[pnum])
#endif
      return -EINVAL;
  }
  
  factor = min = 1 << FACTOR_SHIFT(FACTOR((md_dev+minor)));
  
  for (i=0; i<md_dev[minor].nb_dev; i++)
    if (md_dev[minor].devices[i].size<min)
    {
      printk ("Dev %s smaller than %dk, cannot shrink\n",
	      partition_name (md_dev[minor].devices[i].dev), min);
      return -EINVAL;
    }

  for (i=0; i<md_dev[minor].nb_dev; i++) {
    fsync_dev(md_dev[minor].devices[i].dev);
    invalidate_buffers(md_dev[minor].devices[i].dev);
  }
  
  /* Resize devices according to the factor. It is used to align
     partitions size on a given chunk size. */
  md_size[minor]=0;

  /*
   * Analyze the raid superblock
   */ 
  if (analyze_sb(minor, pnum))
    return -EINVAL;

  md_dev[minor].pers=pers[pnum];
  
  if ((err=md_dev[minor].pers->run (minor, md_dev+minor)))
  {
    md_dev[minor].pers=NULL;
    free_sb(md_dev + minor);
    return (err);
  }

  if (pnum != RAID0 >> PERSONALITY_SHIFT && pnum != LINEAR >> PERSONALITY_SHIFT)
  {
    md_dev[minor].sb->state &= ~(1 << MD_SB_CLEAN);
    md_update_sb(minor);
  }

  /* FIXME : We assume here we have blocks
     that are twice as large as sectors.
     THIS MAY NOT BE TRUE !!! */
  md_hd_struct[minor].start_sect=0;
  md_hd_struct[minor].nr_sects=md_size[minor]<<1;
  
  /* It would be better to have a per-md-dev read_ahead. Currently,
     we only use the smallest read_ahead among md-attached devices */
  
  current_ra=read_ahead[MD_MAJOR];
  
  for (i=0; i<md_dev[minor].nb_dev; i++)
    if (current_ra>read_ahead[MAJOR(md_dev[minor].devices[i].dev)])
      current_ra=read_ahead[MAJOR(md_dev[minor].devices[i].dev)];
  
  read_ahead[MD_MAJOR]=current_ra;
  
  return (0);
}


static int do_md_stop (int minor, struct inode *inode)
{
  int i;
  
  if (inode->i_count>1 || md_dev[minor].busy>1) /* ioctl : one open channel */
  {
    printk ("STOP_MD md%x failed : i_count=%ld, busy=%d\n", minor, inode->i_count, md_dev[minor].busy);
    return -EBUSY;
  }
  
  if (md_dev[minor].pers)
  {
    /*  The device won't exist anymore -> flush it now */
    fsync_dev (inode->i_rdev);
    invalidate_buffers (inode->i_rdev);
    if (md_dev[minor].sb)
    {
      md_dev[minor].sb->state |= 1 << MD_SB_CLEAN;
      md_update_sb(minor);
    }
    md_dev[minor].pers->stop (minor, md_dev+minor);
  }
  
  /* Remove locks. */
  if (md_dev[minor].sb)
    free_sb(md_dev + minor);
  for (i=0; i<md_dev[minor].nb_dev; i++)
    clear_inode (md_dev[minor].devices[i].inode);

  md_dev[minor].nb_dev=md_size[minor]=0;
  md_hd_struct[minor].nr_sects=0;
  md_dev[minor].pers=NULL;
  
  set_ra ();			/* calculate new read_ahead */
  
  return (0);
}


static int do_md_add (int minor, kdev_t dev)
{
  int i;

  if (md_dev[minor].nb_dev==MAX_REAL)
    return -EINVAL;
  
  if (!fs_may_mount (dev) || md_dev[minor].pers)
    return -EBUSY;

  i=md_dev[minor].nb_dev++;
  md_dev[minor].devices[i].dev=dev;
  
  /* Lock the device by inserting a dummy inode. This doesn't
     smell very good, but I need to be consistent with the
     mount stuff, specially with fs_may_mount. If someone have
     a better idea, please help ! */
  
  md_dev[minor].devices[i].inode=get_empty_inode ();
  md_dev[minor].devices[i].inode->i_dev=dev; /* don't care about
						other fields */
  insert_inode_hash (md_dev[minor].devices[i].inode);
  
  /* Sizes are now rounded at run time */
  
/*  md_dev[minor].devices[i].size=gen_real->sizes[MINOR(dev)]; HACKHACK*/

  if (blk_size[MAJOR(dev)][MINOR(dev)] == 0) {
	printk("md_add(): zero device size, huh, bailing out.\n");
  }

  md_dev[minor].devices[i].size=blk_size[MAJOR(dev)][MINOR(dev)];

  printk ("REGISTER_DEV %s to md%x done\n", partition_name(dev), minor);
  return (0);
}


static int md_ioctl (struct inode *inode, struct file *file,
                     unsigned int cmd, unsigned long arg)
{
  int minor, err;
  struct hd_geometry *loc = (struct hd_geometry *) arg;

  if (!suser())
    return -EACCES;

  if (((minor=MINOR(inode->i_rdev)) & 0x80) &&
      (minor & 0x7f) < MAX_PERSONALITY &&
      pers[minor & 0x7f] &&
      pers[minor & 0x7f]->ioctl)
    return (pers[minor & 0x7f]->ioctl (inode, file, cmd, arg));
  
  if (minor >= MAX_MD_DEV)
    return -EINVAL;

  switch (cmd)
  {
    case REGISTER_DEV:
      return do_md_add (minor, to_kdev_t ((dev_t) arg));

    case START_MD:
      return do_md_run (minor, (int) arg);

    case STOP_MD:
      return do_md_stop (minor, inode);
      
    case BLKGETSIZE:   /* Return device size */
    if  (!arg)  return -EINVAL;
    err=verify_area (VERIFY_WRITE, (long *) arg, sizeof(long));
    if (err)
      return err;
    put_user (md_hd_struct[MINOR(inode->i_rdev)].nr_sects, (long *) arg);
    break;

    case BLKFLSBUF:
    fsync_dev (inode->i_rdev);
    invalidate_buffers (inode->i_rdev);
    break;

    case BLKRASET:
    if (arg > 0xff)
      return -EINVAL;
    read_ahead[MAJOR(inode->i_rdev)] = arg;
    return 0;
    
    case BLKRAGET:
    if  (!arg)  return -EINVAL;
    err=verify_area (VERIFY_WRITE, (long *) arg, sizeof(long));
    if (err)
      return err;
    put_user (read_ahead[MAJOR(inode->i_rdev)], (long *) arg);
    break;

    /* We have a problem here : there is no easy way to give a CHS
       virtual geometry. We currently pretend that we have a 2 heads
       4 sectors (with a BIG number of cylinders...). This drives dosfs
       just mad... ;-) */
    
    case HDIO_GETGEO:
    if (!loc)  return -EINVAL;
    err = verify_area(VERIFY_WRITE, loc, sizeof(*loc));
    if (err)
      return err;
    put_user (2, (char *) &loc->heads);
    put_user (4, (char *) &loc->sectors);
    put_user (md_hd_struct[minor].nr_sects/8, (short *) &loc->cylinders);
    put_user (md_hd_struct[MINOR(inode->i_rdev)].start_sect,
		(long *) &loc->start);
    break;
    
    RO_IOCTLS(inode->i_rdev,arg);
    
    default:
    return -EINVAL;
  }

  return (0);
}


static int md_open (struct inode *inode, struct file *file)
{
  int minor=MINOR(inode->i_rdev);

  md_dev[minor].busy++;
  return (0);			/* Always succeed */
}


static void md_release (struct inode *inode, struct file *file)
{
  int minor=MINOR(inode->i_rdev);

  sync_dev (inode->i_rdev);
  md_dev[minor].busy--;
}


static int md_read (struct inode *inode, struct file *file,
		    char *buf, int count)
{
  int minor=MINOR(inode->i_rdev);

  if (!md_dev[minor].pers)	/* Check if device is being run */
    return -ENXIO;

  return block_read (inode, file, buf, count);
}

static int md_write (struct inode *inode, struct file *file,
		     const char *buf, int count)
{
  int minor=MINOR(inode->i_rdev);

  if (!md_dev[minor].pers)	/* Check if device is being run */
    return -ENXIO;

  return block_write (inode, file, buf, count);
}

static struct file_operations md_fops=
{
  NULL,
  md_read,
  md_write,
  NULL,
  NULL,
  md_ioctl,
  NULL,
  md_open,
  md_release,
  block_fsync
};

int md_map (int minor, kdev_t *rdev, unsigned long *rsector, unsigned long size)
{
  if ((unsigned int) minor >= MAX_MD_DEV)
  {
    printk ("Bad md device %d\n", minor);
    return (-1);
  }
  
  if (!md_dev[minor].pers)
  {
    printk ("Oops ! md%d not running, giving up !\n", minor);
    return (-1);
  }

  return (md_dev[minor].pers->map(md_dev+minor, rdev, rsector, size));
}
  
int md_make_request (int minor, int rw, struct buffer_head * bh)
{
	if (md_dev [minor].pers->make_request) {
		if (buffer_locked(bh))
			return 0;
		if (rw == WRITE || rw == WRITEA) {
			if (!buffer_dirty(bh))
				return 0;
			set_bit(BH_Lock, &bh->b_state);
		}
		if (rw == READ || rw == READA) {
			if (buffer_uptodate(bh))
				return 0;
			set_bit (BH_Lock, &bh->b_state);
		}
		return (md_dev[minor].pers->make_request(md_dev+minor, rw, bh));
	} else {
		make_request (MAJOR(bh->b_rdev), rw, bh);
		return 0;
	}
}

static void do_md_request (void)
{
  printk ("Got md request, not good...");
  return;
}  

/*
 * We run MAX_MD_THREADS from md_init() and arbitrate them in run time.
 * This is not so elegant, but how can we use kernel_thread() from within
 * loadable modules?
 */
struct md_thread *md_register_thread (void (*run) (void *), void *data)
{
	int i;
	for (i = 0; i < MAX_MD_THREADS; i++) {
		if (md_threads[i].run == NULL) {
			md_threads[i].run = run;
			md_threads[i].data = data;
			return md_threads + i;
		}
	}
	return NULL;
}


void md_unregister_thread (struct md_thread *thread)
{
	thread->run = NULL;
	thread->data = NULL;
	thread->flags = 0;
}

void md_wakeup_thread(struct md_thread *thread)
{
	set_bit(THREAD_WAKEUP, &thread->flags);
	wake_up(&thread->wqueue);
}

struct buffer_head *efind_buffer(kdev_t dev, int block, int size);

static struct symbol_table md_symbol_table=
{
#include <linux/symtab_begin.h>

  X(md_size),
  X(register_md_personality),
  X(unregister_md_personality),
  X(partition_name),
  X(md_dev),
  X(md_error),
  X(md_register_thread),
  X(md_unregister_thread),
  X(md_update_sb),
  X(md_map),
  X(md_wakeup_thread),
  X(efind_buffer),

#include <linux/symtab_end.h>
};

static void md_geninit (struct gendisk *gdisk)
{
  int i;
  
  for(i=0;i<MAX_MD_DEV;i++)
  {
    md_blocksizes[i] = 1024;
    md_gendisk.part[i].start_sect=-1; /* avoid partition check */
    md_gendisk.part[i].nr_sects=0;
    md_dev[i].pers=NULL;
  }

  blksize_size[MAJOR_NR] = md_blocksizes;
  register_symtab (&md_symbol_table);

  proc_register(&proc_root,
		&(struct proc_dir_entry)
	      {
		PROC_MD, 6, "mdstat",
		S_IFREG | S_IRUGO, 1, 0, 0,
	      });
}

int md_error (kdev_t mddev, kdev_t rdev)
{
    unsigned int minor = MINOR (mddev);
    if (MAJOR(mddev) != MD_MAJOR || minor > MAX_MD_DEV)
	panic ("md_error gets unknown device\n");
    if (!md_dev [minor].pers)
	panic ("md_error gets an error for an unknown device\n");
    if (md_dev [minor].pers->error_handler)
	return (md_dev [minor].pers->error_handler (md_dev+minor, rdev));
    return 0;
}

int get_md_status (char *page)
{
  int sz=0, i, j, size;

  sz+=sprintf( page+sz, "Personalities : ");
  for (i=0; i<MAX_PERSONALITY; i++)
    if (pers[i])
      sz+=sprintf (page+sz, "[%d %s] ", i, pers[i]->name);

  page[sz-1]='\n';

  sz+=sprintf (page+sz, "read_ahead ");
  if (read_ahead[MD_MAJOR]==INT_MAX)
    sz+=sprintf (page+sz, "not set\n");
  else
    sz+=sprintf (page+sz, "%d sectors\n", read_ahead[MD_MAJOR]);
  
  for (i=0; i<MAX_MD_DEV; i++)
  {
    sz+=sprintf (page+sz, "md%d : %sactive", i, md_dev[i].pers ? "" : "in");

    if (md_dev[i].pers)
      sz+=sprintf (page+sz, " %s", md_dev[i].pers->name);

    size=0;
    for (j=0; j<md_dev[i].nb_dev; j++)
    {
      sz+=sprintf (page+sz, " %s",
		   partition_name(md_dev[i].devices[j].dev));
      size+=md_dev[i].devices[j].size;
    }

    if (md_dev[i].nb_dev) {
      if (md_dev[i].pers)
        sz+=sprintf (page+sz, " %d blocks", md_size[i]);
      else
        sz+=sprintf (page+sz, " %d blocks", size);
    }

    if (!md_dev[i].pers)
    {
      sz+=sprintf (page+sz, "\n");
      continue;
    }

    if (md_dev[i].pers->max_invalid_dev)
      sz+=sprintf (page+sz, " maxfault=%ld", MAX_FAULT(md_dev+i));

    sz+=md_dev[i].pers->status (page+sz, i, md_dev+i);
    sz+=sprintf (page+sz, "\n");
  }

  return (sz);
}

int register_md_personality (int p_num, struct md_personality *p)
{
  int i=(p_num >> PERSONALITY_SHIFT);

  if (i >= MAX_PERSONALITY)
    return -EINVAL;

  if (pers[i])
    return -EBUSY;
  
  pers[i]=p;
  printk ("%s personality registered\n", p->name);
  return 0;
}

int unregister_md_personality (int p_num)
{
  int i=(p_num >> PERSONALITY_SHIFT);

  if (i >= MAX_PERSONALITY)
    return -EINVAL;

  printk ("%s personality unregistered\n", pers[i]->name);
  pers[i]=NULL;
  return 0;
} 

int md_thread(void * arg)
{
	struct md_thread *thread = arg;

	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "md_thread");

#ifdef __SMP__
	lock_kernel();
	syscall_count++;
#endif
	for (;;) {
		sti();
		clear_bit(THREAD_WAKEUP, &thread->flags);
		if (thread->run) {
			thread->run(thread->data);
			run_task_queue(&tq_disk);
		}
		current->signal = 0;
		cli();
		if (!test_bit(THREAD_WAKEUP, &thread->flags))
			interruptible_sleep_on(&thread->wqueue);
	}
}

void linear_init (void);
void raid0_init (void);
void raid1_init (void);
void raid5_init (void);

int md_init (void)
{
  int i;

  printk ("md driver %d.%d.%d MAX_MD_DEV=%d, MAX_REAL=%d\n",
    MD_MAJOR_VERSION, MD_MINOR_VERSION, MD_PATCHLEVEL_VERSION,
    MAX_MD_DEV, MAX_REAL);

  if (register_blkdev (MD_MAJOR, "md", &md_fops))
  {
    printk ("Unable to get major %d for md\n", MD_MAJOR);
    return (-1);
  }

  for (i = 0; i < MAX_MD_THREADS; i++) {
    md_threads[i].run = NULL;
    init_waitqueue(&md_threads[i].wqueue);
    md_threads[i].flags = 0;
    kernel_thread (md_thread, md_threads + i, 0);
  }

  blk_dev[MD_MAJOR].request_fn=DEVICE_REQUEST;
  blk_dev[MD_MAJOR].current_request=NULL;
  read_ahead[MD_MAJOR]=INT_MAX;
  memset(md_dev, 0, MAX_MD_DEV * sizeof (struct md_dev));
  md_gendisk.next=gendisk_head;

  gendisk_head=&md_gendisk;

#ifdef CONFIG_MD_LINEAR
  linear_init ();
#endif
#ifdef CONFIG_MD_STRIPED
  raid0_init ();
#endif
#ifdef CONFIG_MD_MIRRORING
  raid1_init ();
#endif
#ifdef CONFIG_MD_RAID5
  raid5_init ();
#endif
  
  return (0);
}
