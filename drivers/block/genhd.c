/*
 *  Code extracted from
 *  linux/kernel/hd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 *
 *  Support for DiskManager v6.0x added by Mark Lord,
 *  with information provided by OnTrack.  This now works for linux fdisk
 *  and LILO, as well as loadlin and bootln.  Note that disks other than
 *  /dev/hda *must* have a "DOS" type 0x51 partition in the first slot (hda1).
 *
 *  More flexible handling of extended partitions - aeb, 950831
 *
 *  Check partition table on IDE disks for common CHS translations
 *
 *  Added needed MAJORS for new pairs, {hdi,hdj}, {hdk,hdl}
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/init.h>

#ifdef CONFIG_ARCH_S390
#include <asm/dasd.h>
#endif /* CONFIG_ARCH_S390 */

#include <asm/system.h>
#include <asm/byteorder.h>

/*
 * Many architectures don't like unaligned accesses, which is
 * frequently the case with the nr_sects and start_sect partition
 * table entries.
 */
#include <asm/unaligned.h>

#define SYS_IND(p)	(get_unaligned(&p->sys_ind))
#define NR_SECTS(p)	({ __typeof__(p->nr_sects) __a =	\
				get_unaligned(&p->nr_sects);	\
				le32_to_cpu(__a); \
			})

#define START_SECT(p)	({ __typeof__(p->start_sect) __a =	\
				get_unaligned(&p->start_sect);	\
				le32_to_cpu(__a); \
			})

#define MSDOS_LABEL_MAGIC1		0x55
#define MSDOS_LABEL_MAGIC2		0xAA

static inline int
msdos_magic_present(unsigned char *p) {
	return (p[0] == MSDOS_LABEL_MAGIC1 && p[1] == MSDOS_LABEL_MAGIC2);
}

struct gendisk *gendisk_head = NULL;

static int current_minor = 0;
extern int *blk_size[];
extern void rd_load(void);
extern void initrd_load(void);

extern int chr_dev_init(void);
extern int blk_dev_init(void);
extern int i2o_init(void);
#ifdef CONFIG_BLK_DEV_DAC960
extern void DAC960_Initialize(void);
#endif
extern int scsi_dev_init(void);
extern int net_dev_init(void);

#ifdef CONFIG_PPC
extern void note_bootable_part(kdev_t dev, int part, int goodness);
#endif

static char *raid_name (struct gendisk *hd, int minor, int major_base,
                        char *buf)
{
        int ctlr = hd->major - major_base;
        int disk = minor >> hd->minor_shift;
        int part = minor & (( 1 << hd->minor_shift) - 1);
        if (part == 0)
                sprintf(buf, "%s/c%dd%d", hd->major_name, ctlr, disk);
        else
                sprintf(buf, "%s/c%dd%dp%d", hd->major_name, ctlr, disk,
                        part);
        return buf;
}

#ifdef CONFIG_ARCH_S390
int (*genhd_dasd_name)(char*,int,int,struct gendisk*) = NULL;
#endif

/*
 * disk_name() is used by genhd.c and md.c.
 * It formats the devicename of the indicated disk
 * into the supplied buffer, and returns a pointer
 * to that same buffer (for convenience).
 */
char *disk_name (struct gendisk *hd, int minor, char *buf)
{
	unsigned int part;
	const char *maj = hd->major_name;
	int unit = (minor >> hd->minor_shift) + 'a';

#ifdef CONFIG_ARCH_S390
	if ( strncmp ( hd->major_name,"dasd",4) == 0 ){
		part = minor & ((1 << hd->minor_shift) - 1);
		if ( genhd_dasd_name )
			genhd_dasd_name(buf,minor>>hd->minor_shift,part,hd);
		return buf;
	} 
#endif
	/*
	 * IDE devices use multiple major numbers, but the drives
	 * are named as:  {hda,hdb}, {hdc,hdd}, {hde,hdf}, {hdg,hdh}..
	 * This requires special handling here.
	 *
	 * MD devices are named md0, md1, ... md15, fix it up here.
	 */
	switch (hd->major) {
		case IDE5_MAJOR:
			unit += 2;
		case IDE4_MAJOR:
			unit += 2;
		case IDE3_MAJOR:
			unit += 2;
		case IDE2_MAJOR:
			unit += 2;
		case IDE1_MAJOR:
			unit += 2;
		case IDE0_MAJOR:
			maj = "hd";
			break;
		case MD_MAJOR:
			unit -= 'a'-'0';
	}
	part = minor & ((1 << hd->minor_shift) - 1);
	if (hd->major >= SCSI_DISK1_MAJOR && hd->major <= SCSI_DISK7_MAJOR) {
		unit = unit + (hd->major - SCSI_DISK1_MAJOR + 1) * 16;
		if (unit > 'z') {
			unit -= 'z' + 1;
			sprintf(buf, "sd%c%c", 'a' + unit / 26, 'a' + unit % 26);
			if (part)
				sprintf(buf + 4, "%d", part);
			return buf;
		}
	}
	if (hd->major >= COMPAQ_CISS_MAJOR && hd->major <=
            COMPAQ_CISS_MAJOR+7) {
          return raid_name (hd, minor, COMPAQ_CISS_MAJOR, buf);
        }
	if (hd->major >= COMPAQ_SMART2_MAJOR && hd->major <=
            COMPAQ_SMART2_MAJOR+7) {
          return raid_name (hd, minor, COMPAQ_SMART2_MAJOR, buf);
 	}
	if (hd->major >= DAC960_MAJOR && hd->major <= DAC960_MAJOR+7) {
          return raid_name (hd, minor, DAC960_MAJOR, buf);
 	}
	if (part)
		sprintf(buf, "%s%c%d", maj, unit, part);
	else
		sprintf(buf, "%s%c", maj, unit);
	return buf;
}

static void add_partition (struct gendisk *hd, int minor,
					int start, int size, int type)
{
	char buf[MAX_DISKNAME_LEN];
	struct hd_struct *p = hd->part+minor;

	p->start_sect = start;
	p->nr_sects = size;
	p->type = type;

	printk(" %s", disk_name(hd, minor, buf));
}

static inline int is_extended_partition(struct partition *p)
{
	return (SYS_IND(p) == DOS_EXTENDED_PARTITION ||
		SYS_IND(p) == WIN98_EXTENDED_PARTITION ||
		SYS_IND(p) == LINUX_EXTENDED_PARTITION);
}

static unsigned int get_ptable_blocksize(kdev_t dev)
{
	int ret = 1024;

	/*
	 * See whether the low-level driver has given us a minumum blocksize.
	 * If so, check to see whether it is larger than the default of 1024.
	 */
	if (!blksize_size[MAJOR(dev)])
		return ret;

	/*
	 * Check for certain special power of two sizes that we allow.
	 * With anything larger than 1024, we must force the blocksize up to
	 * the natural blocksize for the device so that we don't have to try
	 * and read partial sectors.  Anything smaller should be just fine.
	 */

	switch( blksize_size[MAJOR(dev)][MINOR(dev)] ) {
	case 2048:
		ret = 2048;
		break;

	case 4096:
		ret = 4096;
		break;

	case 8192:
		ret = 8192;
		break;

	case 1024:
	case 512:
	case 256:
	case 0:
		/*
		 * These are all OK.
		 */
		break;

	default:
		panic("Strange blocksize for partition table\n");
	}

	return ret;
}

#ifdef CONFIG_MSDOS_PARTITION
/*
 * Create devices for each logical partition in an extended partition.
 * The logical partitions form a linked list, with each entry being
 * a partition table with two entries.  The first entry
 * is the real data partition (with a start relative to the partition
 * table start).  The second is a pointer to the next logical partition
 * (with a start relative to the entire extended partition).
 * We do not create a Linux partition for the partition tables, but
 * only for the actual data partitions.
 */

static void extended_partition(struct gendisk *hd, kdev_t dev, int sector_size)
{
	struct buffer_head *bh;
	struct partition *p;
	unsigned long first_sector, first_size, this_sector, this_size;
	int mask = (1 << hd->minor_shift) - 1;
	int i;
	int loopct = 0; 	/* number of links followed
				   without finding a data partition */

	first_sector = hd->part[MINOR(dev)].start_sect;
	first_size = hd->part[MINOR(dev)].nr_sects;
	this_sector = first_sector;

	while (1) {
		if (++loopct > 100)
			return;
		if ((current_minor & mask) == 0)
			return;
		if (!(bh = bread(dev,0,get_ptable_blocksize(dev))))
			return;
	  /*
	   * This block is from a device that we're about to stomp on.
	   * So make sure nobody thinks this block is usable.
	   */
		bh->b_state = 0;

		if (!(msdos_magic_present(bh->b_data + 510)))
			goto done;

		p = (struct partition *) (bh->b_data + 0x1be);

		this_size = hd->part[MINOR(dev)].nr_sects;

		/*
		 * Usually, the first entry is the real data partition,
		 * the 2nd entry is the next extended partition, or empty,
		 * and the 3rd and 4th entries are unused.
		 * However, DRDOS sometimes has the extended partition as
		 * the first entry (when the data partition is empty),
		 * and OS/2 seems to use all four entries.
		 */

		/* 
		 * First process the data partition(s)
		 */
		for (i=0; i<4; i++, p++) {
			if (!NR_SECTS(p) || is_extended_partition(p))
				continue;

			/* Check the 3rd and 4th entries -
			   these sometimes contain random garbage */
			if (i >= 2
				&& START_SECT(p) + NR_SECTS(p) > this_size
				&& (this_sector + START_SECT(p) < first_sector ||
				    this_sector + START_SECT(p) + NR_SECTS(p) >
				     first_sector + first_size))
				continue;

			add_partition(hd, current_minor, this_sector+START_SECT(p)*sector_size, 
				      NR_SECTS(p)*sector_size, ptype(SYS_IND(p)));
			current_minor++;
			if ((current_minor & mask) == 0)
				goto done;
			loopct = 0;
		}
		/*
		 * Next, process the (first) extended partition, if present.
		 * (So far, there seems to be no reason to make
		 *  extended_partition()  recursive and allow a tree
		 *  of extended partitions.)
		 * It should be a link to the next logical partition.
		 * Create a minor for this just long enough to get the next
		 * partition table.  The minor will be reused for the next
		 * data partition.
		 */
		p -= 4;
		for (i=0; i<4; i++, p++)
			if(NR_SECTS(p) && is_extended_partition(p))
				break;
		if (i == 4)
			goto done;	 /* nothing left to do */

		hd->part[current_minor].nr_sects = NR_SECTS(p) * sector_size; /* JSt */
		hd->part[current_minor].start_sect =
			first_sector + START_SECT(p) * sector_size;
		this_sector = first_sector + START_SECT(p) * sector_size;
		dev = MKDEV(hd->major, current_minor);
		brelse(bh);
	}
done:
	brelse(bh);
}

#ifdef CONFIG_SOLARIS_X86_PARTITION
static void
solaris_x86_partition(struct gendisk *hd, kdev_t dev, long offset) {

	struct buffer_head *bh;
	struct solaris_x86_vtoc *v;
	struct solaris_x86_slice *s;
	int mask = (1 << hd->minor_shift) - 1;
	int i;

	if(!(bh = bread(dev, 0, get_ptable_blocksize(dev))))
		return;
	v = (struct solaris_x86_vtoc *)(bh->b_data + 512);
	if(v->v_sanity != SOLARIS_X86_VTOC_SANE) {
		brelse(bh);
		return;
	}
	printk(" <solaris:");
	if(v->v_version != 1) {
		printk("  cannot handle version %ld vtoc>", v->v_version);
		brelse(bh);
		return;
	}
	for(i=0; i<SOLARIS_X86_NUMSLICE; i++) {
		if ((current_minor & mask) == 0)
			break;

		s = &v->v_slice[i];

		if (s->s_size == 0)
			continue;

		printk(" [s%d]", i);
		/* solaris partitions are relative to current MS-DOS
		 * one but add_partition starts relative to sector
		 * zero of the disk.  Therefore, must add the offset
		 * of the current partition */
		add_partition(hd, current_minor,
					s->s_start+offset, s->s_size, 0);
		current_minor++;
	}
	brelse(bh);
	printk(" >");
}
#endif

#ifdef CONFIG_BSD_DISKLABEL
static void
check_and_add_bsd_partition(struct gendisk *hd, struct bsd_partition *bsd_p,
			    int baseminor)
{
	int i, bsd_start, bsd_size;

	bsd_start = le32_to_cpu(bsd_p->p_offset);
	bsd_size = le32_to_cpu(bsd_p->p_size);

	/* check relative position of already allocated partitions */
	for (i = baseminor+1; i < current_minor; i++) {
		int start = hd->part[i].start_sect;
		int size = hd->part[i].nr_sects;

		if (start+size <= bsd_start || start >= bsd_start+bsd_size)
			continue;       /* no overlap */

		if (start == bsd_start && size == bsd_size)
			return;         /* equal -> no need to add */

		if (start <= bsd_start && start+size >= bsd_start+bsd_size) {
			/* bsd living within dos partition */
#ifdef DEBUG_BSD_DISKLABEL
			printk("w: %d %ld+%ld,%d+%d", 
			       i, start, size, bsd_start, bsd_size);
#endif
			break;		/* ok */
		}

		/* ouch: bsd and linux overlap */
#ifdef DEBUG_BSD_DISKLABEL
		printk("???: %d %ld+%ld,%d+%d",
		       i, start, size, bsd_start, bsd_size);
#endif
		printk("???");
		return;
	}

	add_partition(hd, current_minor, bsd_start, bsd_size, 0);
	current_minor++;
}
/* 
 * Create devices for BSD partitions listed in a disklabel, under a
 * dos-like partition. See extended_partition() for more information.
 */
static void bsd_disklabel_partition(struct gendisk *hd, kdev_t dev, 
   int max_partitions)
{
	struct buffer_head *bh;
	struct bsd_disklabel *l;
	struct bsd_partition *p;
	int mask = (1 << hd->minor_shift) - 1;
	int baseminor = (MINOR(dev) & ~mask);

	if (!(bh = bread(dev,0,get_ptable_blocksize(dev))))
		return;
	bh->b_state = 0;
	l = (struct bsd_disklabel *) (bh->b_data+512);
	if (l->d_magic != BSD_DISKMAGIC) {
		brelse(bh);
		return;
	}

	if (l->d_npartitions < max_partitions)
		max_partitions = l->d_npartitions;
	for (p = l->d_partitions; p - l->d_partitions <  max_partitions; p++) {
		if ((current_minor & mask) == 0)
			break;
		if (p->p_fstype != BSD_FS_UNUSED)
			check_and_add_bsd_partition(hd, p, baseminor);
	}
	brelse(bh);
}
#endif

#ifdef CONFIG_UNIXWARE_DISKLABEL
/*
 * Create devices for Unixware partitions listed in a disklabel, under a
 * dos-like partition. See extended_partition() for more information.
 */
static void unixware_partition(struct gendisk *hd, kdev_t dev)
{
	struct buffer_head *bh;
	struct unixware_disklabel *l;
	struct unixware_slice *p;
	int mask = (1 << hd->minor_shift) - 1;

	if (!(bh = bread(dev, 14, get_ptable_blocksize(dev))))
		return;
	bh->b_state = 0;
	l = (struct unixware_disklabel *) (bh->b_data+512);
	if (le32_to_cpu(l->d_magic) != UNIXWARE_DISKMAGIC ||
	    le32_to_cpu(l->vtoc.v_magic) != UNIXWARE_DISKMAGIC2) {
		brelse(bh);
		return;
	}
	printk(" <unixware:");
	p = &l->vtoc.v_slice[1];
	/* I omit the 0th slice as it is the same as whole disk. */
	while (p - &l->vtoc.v_slice[0] < UNIXWARE_NUMSLICE) {
		if ((current_minor & mask) == 0)
			break;

		if (p->s_label != UNIXWARE_FS_UNUSED) {
			add_partition(hd, current_minor, START_SECT(p), NR_SECTS(p), 0);
			current_minor++;
		}
		p++;
	}
	brelse(bh);
	printk(" >");
}
#endif
		
#ifdef CONFIG_MINIX_SUBPARTITION
/*
 * Minix 2.0.0/2.0.2 subpartition support.
 * Anand Krishnamurthy <anandk@wiproge.med.ge.com>
 * Rajeev V. Pillai    <rajeevvp@yahoo.com>
 */
#define MINIX_PARTITION         0x81  /* Minix Partition ID */
#define MINIX_NR_SUBPARTITIONS  4
static void minix_partition(struct gendisk *hd, kdev_t dev)
{
	struct buffer_head *bh;
	struct partition *p;
	int mask = (1 << hd->minor_shift) - 1;
	int i;

	if (!(bh = bread(dev, 0, get_ptable_blocksize(dev))))
		return;
	bh->b_state = 0;

	p = (struct partition *)(bh->b_data + 0x1be);

	/* The first sector of a Minix partition can have either
	 * a secondary MBR describing its subpartitions, or
	 * the normal boot sector. */
	if (msdos_magic_present(bh->b_data + 510) &&
	    SYS_IND(p) == MINIX_PARTITION) { /* subpartition table present */

		printk(" <");
		for (i = 0; i < MINIX_NR_SUBPARTITIONS; i++, p++) {
			if ((current_minor & mask) == 0) 
				break;
			/* add each partition in use */
			if (SYS_IND(p) == MINIX_PARTITION) {
				add_partition(hd, current_minor,
					      START_SECT(p), NR_SECTS(p), 0);
				current_minor++;
			}
		}
		printk(" >");
	}
	brelse(bh);
}
#endif /* CONFIG_MINIX_SUBPARTITION */
 
static int msdos_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	int i, minor = current_minor;
	struct buffer_head *bh;
	struct partition *p;
	unsigned char *data;
	int mask = (1 << hd->minor_shift) - 1;
	int sector_size;
        int disk_number_of_sects;
#ifdef CONFIG_BSD_DISKLABEL
	/* no bsd disklabel as a default */
	kdev_t bsd_kdev = 0;
	int bsd_maxpart = BSD_MAXPARTITIONS;
#endif
#ifdef CONFIG_BLK_DEV_IDE
	int tested_for_xlate = 0;

read_mbr:
#endif
	if (!(bh = bread(dev,0,get_ptable_blocksize(dev)))) {
		printk(" unable to read partition table\n");
		return -1;
	}

	/* in some cases (backwards compatibility) we'll adjust the
         * sector_size below
         */
        if (hardsect_size[MAJOR(dev)] != NULL)
               sector_size=hardsect_size[MAJOR(dev)][MINOR(dev)]/512;
        else
               sector_size=1;

	data = bh->b_data;
	/* In some cases we modify the geometry    */
	/*  of the drive (below), so ensure that   */
	/*  nobody else tries to re-use this data. */
	bh->b_state = 0;
#ifdef CONFIG_BLK_DEV_IDE
check_table:
#endif
	if (!(msdos_magic_present(data + 510))) {
		brelse(bh);
		return 0;
	}
	p = (struct partition *) (data + 0x1be);

#ifdef CONFIG_BLK_DEV_IDE
	if (!tested_for_xlate++) {	/* Do this only once per disk */
		/*
		 * Look for various forms of IDE disk geometry translation
		 */
		extern int ide_xlate_1024(kdev_t, int, const char *);
		unsigned int sig = le16_to_cpu(*(unsigned short *)(data + 2));
		if (SYS_IND(p) == EZD_PARTITION) {
			/*
			 * The remainder of the disk must be accessed using
			 * a translated geometry that reduces the number of 
			 * apparent cylinders to less than 1024 if possible.
			 *
			 * ide_xlate_1024() will take care of the necessary
			 * adjustments to fool fdisk/LILO and partition check.
			 */
			if (ide_xlate_1024(dev, -1, " [EZD]")) {
				data += 512;
				goto check_table;
			}
		} else if (SYS_IND(p) == DM6_PARTITION) {

			/*
			 * Everything on the disk is offset by 63 sectors,
			 * including a "new" MBR with its own partition table,
			 * and the remainder of the disk must be accessed using
			 * a translated geometry that reduces the number of 
			 * apparent cylinders to less than 1024 if possible.
			 *
			 * ide_xlate_1024() will take care of the necessary
			 * adjustments to fool fdisk/LILO and partition check.
			 */
			if (ide_xlate_1024(dev, 1, " [DM6:DDO]")) {
				brelse(bh);
				goto read_mbr;	/* start over with new MBR */
			}
		} else if (sig <= 0x1ae &&
			   *(unsigned short *)(data + sig) == cpu_to_le16(0x55AA) &&
			   (1 & *(unsigned char *)(data + sig + 2))) {
			/* DM6 signature in MBR, courtesy of OnTrack */
			(void) ide_xlate_1024 (dev, 0, " [DM6:MBR]");
		} else if (SYS_IND(p) == DM6_AUX1PARTITION || SYS_IND(p) == DM6_AUX3PARTITION) {
			/*
			 * DM6 on other than the first (boot) drive
			 */
			(void) ide_xlate_1024(dev, 0, " [DM6:AUX]");
		} else {
			/*
			 * Examine the partition table for common translations.
			 * This is useful for drives in situations where the
			 * translated geometry is unavailable from the BIOS.
			 */
			for (i = 0; i < 4; i++) {
				struct partition *q = &p[i];
				if (NR_SECTS(q)
				   && (q->sector & 63) == 1
				   && (q->end_sector & 63) == 63) {
					unsigned int heads = q->end_head + 1;
					if (heads == 32 || heads == 64 ||
					    heads == 128 || heads == 240 ||
					    heads == 255) {
						(void) ide_xlate_1024(dev, heads, " [PTBL]");
						break;
					}
				}
			}
		}
	}
#endif	/* CONFIG_BLK_DEV_IDE */

	/*
         * The Linux code now honours the rules the MO people set and
         * is 'DOS compatible' - sizes are scaled by the media block
	 * size not 512 bytes. The following is a backwards
	 * compatibility check. If a 1k or greater sectorsize disk
	 * (1024, 2048, etc) was created under a pre 2.2 kernel,
	 * the partition table wrongly used units of 512 instead of
         * units of sectorsize (1024, 2048, etc.) The below check attempts
         * to detect a partition table created under the older kernel, and
         * if so detected, it will reset the a sector scale factor to 1 (i.e.
         * no scaling).
	 */
        disk_number_of_sects = NR_SECTS((&hd->part[MINOR(dev)]));
	for (i = 0; i < 4; i++) {
		struct partition *q = &p[i];
		if (NR_SECTS(q)) {
		        if (((first_sector+(START_SECT(q)+NR_SECTS(q))*sector_size) >
                             (disk_number_of_sects+1)) &&
		            ((first_sector+(START_SECT(q)+NR_SECTS(q)) <=
                             disk_number_of_sects))) {
	                        char buf[MAX_DISKNAME_LEN];
	                        printk(" %s: RESETTINGING SECTOR SCALE from %d to 1",
                                       disk_name(hd, MINOR(dev), buf), sector_size);
		                sector_size=1;
                                break;
		        }
		}
	}

	current_minor += 4;  /* first "extra" minor (for extended partitions) */
	for (i=1 ; i<=4 ; minor++,i++,p++) {
		if (!NR_SECTS(p))
			continue;
               add_partition(hd, minor, first_sector+START_SECT(p)*sector_size,
			     NR_SECTS(p)*sector_size, ptype(SYS_IND(p)));
		if (is_extended_partition(p)) {
			printk(" <");
			/*
			 * If we are rereading the partition table, we need
			 * to set the size of the partition so that we will
			 * be able to bread the block containing the extended
			 * partition info.
			 */
			hd->sizes[minor] = hd->part[minor].nr_sects 
			  	>> (BLOCK_SIZE_BITS - 9);
			extended_partition(hd, MKDEV(hd->major, minor), sector_size);
			printk(" >");
			/* prevent someone doing mkfs or mkswap on an
			   extended partition, but leave room for LILO */
			if (hd->part[minor].nr_sects > 2)
				hd->part[minor].nr_sects = 2;
		}
#ifdef CONFIG_BSD_DISKLABEL
			/* tag first disklabel for late recognition */
		if (SYS_IND(p) == BSD_PARTITION || SYS_IND(p) == NETBSD_PARTITION) {
			printk("!");
			if (!bsd_kdev)
				bsd_kdev = MKDEV(hd->major, minor);
		} else if (SYS_IND(p) == OPENBSD_PARTITION) {
			printk("!");
			if (!bsd_kdev) {
				bsd_kdev = MKDEV(hd->major, minor);
				bsd_maxpart = OPENBSD_MAXPARTITIONS;
			}
		}
#endif
#ifdef CONFIG_MINIX_SUBPARTITION
		if (SYS_IND(p) == MINIX_PARTITION) {
			printk("@");	/* Minix partitions are indicated by '@' */
			minix_partition(hd, MKDEV(hd->major, minor));
		}
#endif
#ifdef CONFIG_UNIXWARE_DISKLABEL
		if (SYS_IND(p) == UNIXWARE_PARTITION)
			unixware_partition(hd, MKDEV(hd->major, minor));
#endif
#ifdef CONFIG_SOLARIS_X86_PARTITION

		/* james@bpgc.com: Solaris has a nasty indicator: 0x82
		 * which also means linux swap.  For that reason, all
		 * of the prints are done inside the
		 * solaris_x86_partition routine */

		if(SYS_IND(p) == SOLARIS_X86_PARTITION) {
			solaris_x86_partition(hd, MKDEV(hd->major, minor),
					      first_sector+START_SECT(p));
		}
#endif
	}
#ifdef CONFIG_BSD_DISKLABEL
	if (bsd_kdev) {
		printk(" <");
		bsd_disklabel_partition(hd, bsd_kdev, bsd_maxpart);
		printk(" >");
	}
#endif
	/*
	 *  Check for old-style Disk Manager partition table
	 */
	if (msdos_magic_present(data + 0xfc)) {
		p = (struct partition *) (0x1be + data);
		for (i = 4 ; i < 16 ; i++, current_minor++) {
			p--;
			if ((current_minor & mask) == 0)
				break;
			if (!(START_SECT(p) && NR_SECTS(p)))
				continue;
			add_partition(hd, current_minor, START_SECT(p),
					NR_SECTS(p), 0);
		}
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif /* CONFIG_MSDOS_PARTITION */

#ifdef CONFIG_OSF_PARTITION

static int osf_partition(struct gendisk *hd, unsigned int dev, unsigned long first_sector)
{
	int i;
	int mask = (1 << hd->minor_shift) - 1;
	struct buffer_head *bh;
	struct disklabel {
		u32 d_magic;
		u16 d_type,d_subtype;
		u8 d_typename[16];
		u8 d_packname[16];
		u32 d_secsize;
		u32 d_nsectors;
		u32 d_ntracks;
		u32 d_ncylinders;
		u32 d_secpercyl;
		u32 d_secprtunit;
		u16 d_sparespertrack;
		u16 d_sparespercyl;
		u32 d_acylinders;
		u16 d_rpm, d_interleave, d_trackskew, d_cylskew;
		u32 d_headswitch, d_trkseek, d_flags;
		u32 d_drivedata[5];
		u32 d_spare[5];
		u32 d_magic2;
		u16 d_checksum;
		u16 d_npartitions;
		u32 d_bbsize, d_sbsize;
		struct d_partition {
			u32 p_size;
			u32 p_offset;
			u32 p_fsize;
			u8  p_fstype;
			u8  p_frag;
			u16 p_cpg;
		} d_partitions[8];
	} * label;
	struct d_partition * partition;
#define DISKLABELMAGIC (0x82564557UL)

	if (!(bh = bread(dev,0,get_ptable_blocksize(dev)))) {
		printk("unable to read partition table\n");
		return -1;
	}
	label = (struct disklabel *) (bh->b_data+64);
	partition = label->d_partitions;
	if (label->d_magic != DISKLABELMAGIC) {
		brelse(bh);
		return 0;
	}
	if (label->d_magic2 != DISKLABELMAGIC) {
		brelse(bh);
		return 0;
	}
	for (i = 0 ; i < label->d_npartitions; i++, partition++) {
		if ((current_minor & mask) == 0)
		        break;
		if (partition->p_size)
			add_partition(hd, current_minor,
				first_sector+partition->p_offset,
				partition->p_size, 0);
		current_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif /* CONFIG_OSF_PARTITION */

#ifdef CONFIG_SUN_PARTITION

static int sun_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	int i, csum;
	unsigned short *ush;
	struct buffer_head *bh;
	struct sun_disklabel {
		unsigned char info[128];   /* Informative text string */
		unsigned char spare0[14];
		struct sun_disklabelinfo {
			unsigned char spare1;
			unsigned char id;
			unsigned char spare2;
			unsigned char flags;
		} infos[8];
		unsigned char spare1[246];  /* Boot information etc. */
		unsigned short rspeed;     /* Disk rotational speed */
		unsigned short pcylcount;  /* Physical cylinder count */
		unsigned short sparecyl;   /* extra sects per cylinder */
		unsigned char spare2[4];   /* More magic... */
		unsigned short ilfact;     /* Interleave factor */
		unsigned short ncyl;       /* Data cylinder count */
		unsigned short nacyl;      /* Alt. cylinder count */
		unsigned short ntrks;      /* Tracks per cylinder */
		unsigned short nsect;      /* Sectors per track */
		unsigned char spare3[4];   /* Even more magic... */
		struct sun_partition {
			__u32 start_cylinder;
			__u32 num_sectors;
		} partitions[8];
		unsigned short magic;      /* Magic number */
		unsigned short csum;       /* Label xor'd checksum */
	} * label;		
	struct sun_partition *p;
	unsigned long spc;
#define SUN_LABEL_MAGIC          0xDABE

	if(!(bh = bread(dev, 0, get_ptable_blocksize(dev)))) {
		printk("Dev %s: unable to read partition table\n",
		       kdevname(dev));
		return -1;
	}
	label = (struct sun_disklabel *) bh->b_data;
	p = label->partitions;
	if (be16_to_cpu(label->magic) != SUN_LABEL_MAGIC) {
#if 0
		/* There is no error here - it is just not a sunlabel. */
		printk("Dev %s Sun disklabel: bad magic %04x\n",
		       kdevname(dev), be16_to_cpu(label->magic));
#endif
		brelse(bh);
		return 0;
	}
	/* Look at the checksum */
	ush = ((unsigned short *) (label+1)) - 1;
	for(csum = 0; ush >= ((unsigned short *) label);)
		csum ^= *ush--;
	if(csum) {
		printk("Dev %s Sun disklabel: Csum bad, label corrupted\n",
		       kdevname(dev));
		brelse(bh);
		return 0;
	}
	/* All Sun disks have 8 partition entries */
	spc = be16_to_cpu(label->ntrks) * be16_to_cpu(label->nsect);
	for(i=0; i < 8; i++, p++) {
		unsigned long st_sector;
		int num_sectors;

		st_sector = first_sector + be32_to_cpu(p->start_cylinder) * spc;
		num_sectors = be32_to_cpu(p->num_sectors);
		if (num_sectors)
			add_partition(hd, current_minor, st_sector,
					num_sectors, ptype(label->infos[i].id));
		current_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif /* CONFIG_SUN_PARTITION */

#ifdef CONFIG_SGI_PARTITION

static int sgi_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	int i, csum, magic;
	unsigned int *ui, start, blocks, cs;
	struct buffer_head *bh;
	struct sgi_disklabel {
		int magic_mushroom;         /* Big fat spliff... */
		short root_part_num;        /* Root partition number */
		short swap_part_num;        /* Swap partition number */
		char boot_file[16];         /* Name of boot file for ARCS */
		unsigned char _unused0[48]; /* Device parameter useless crapola.. */
		struct sgi_volume {
			char name[8];       /* Name of volume */
			int  block_num;     /* Logical block number */
			int  num_bytes;     /* How big, in bytes */
		} volume[15];
		struct sgi_partition {
			int num_blocks;     /* Size in logical blocks */
			int first_block;    /* First logical block */
			int type;           /* Type of this partition */
		} partitions[16];
		int csum;                   /* Disk label checksum */
		int _unused1;               /* Padding */
	} *label;
	struct sgi_partition *p;
#define SGI_LABEL_MAGIC 0x0be5a941

	if(!(bh = bread(dev, 0, get_ptable_blocksize(dev)))) {
		printk("Dev %s: unable to read partition table\n", kdevname(dev));
		return -1;
	}
	label = (struct sgi_disklabel *) bh->b_data;
	p = &label->partitions[0];
	magic = label->magic_mushroom;
	if(be32_to_cpu(magic) != SGI_LABEL_MAGIC) {
#if 0
		/* There is no error here - it is just not an sgilabel. */
		printk("Dev %s SGI disklabel: bad magic %08x\n",
		       kdevname(dev), magic);
#endif
		brelse(bh);
		return 0;
	}
	ui = ((unsigned int *) (label + 1)) - 1;
	for(csum = 0; ui >= ((unsigned int *) label);) {
		cs = *ui--;
		csum += be32_to_cpu(cs);
	}
	if(csum) {
		printk("Dev %s SGI disklabel: csum bad, label corrupted\n",
		       kdevname(dev));
		brelse(bh);
		return 0;
	}
	/* All SGI disk labels have 16 partitions, disks under Linux only
	 * have 15 minor's.  Luckily there are always a few zero length
	 * partitions which we don't care about so we never overflow the
	 * current_minor.
	 */
	for(i = 0; i < 16; i++, p++) {
		blocks = be32_to_cpu(p->num_blocks);
		start  = be32_to_cpu(p->first_block);
		if(!blocks)
			continue;
		add_partition(hd, current_minor, start, blocks, 0);
		current_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}

#endif

#ifdef CONFIG_AMIGA_PARTITION
#include <linux/affs_hardblocks.h>

static __inline__ u32
checksum_block(u32 *m, int size)
{
	u32 sum = 0;

	while (size--)
		sum += be32_to_cpu(*m++);
	return sum;
}

static int
amiga_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	struct buffer_head	*bh;
	struct RigidDiskBlock	*rdb;
	struct PartitionBlock	*pb;
	int			 start_sect;
	int			 nr_sects;
	int			 blk;
	int			 part, res;
	int			 old_blocksize;
	int			 blocksize;

	old_blocksize = get_ptable_blocksize(dev);
	if (hardsect_size[MAJOR(dev)] != NULL)
		blocksize = hardsect_size[MAJOR(dev)][MINOR(dev)];
	else
		blocksize = 512;

	set_blocksize(dev,blocksize);
	res = 0;

	for (blk = 0; blk < RDB_ALLOCATION_LIMIT; blk++) {
		if(!(bh = bread(dev,blk,blocksize))) {
			printk("Dev %s: unable to read RDB block %d\n",
			       kdevname(dev),blk);
			goto rdb_done;
		}
		if (*(u32 *)bh->b_data == cpu_to_be32(IDNAME_RIGIDDISK)) {
			rdb = (struct RigidDiskBlock *)bh->b_data;
			if (checksum_block((u32 *)bh->b_data,be32_to_cpu(rdb->rdb_SummedLongs) & 0x7F)) {
				/* Try again with 0xdc..0xdf zeroed, Windows might have
				 * trashed it.
				 */
				*(u32 *)(&bh->b_data[0xdc]) = 0;
				if (checksum_block((u32 *)bh->b_data,
						be32_to_cpu(rdb->rdb_SummedLongs) & 0x7F)) {
					brelse(bh);
					printk("Dev %s: RDB in block %d has bad checksum\n",
					       kdevname(dev),blk);
					continue;
				}
				printk("Warning: Trashed word at 0xd0 in block %d "
					"ignored in checksum calculation\n",blk);
			}
			printk(" RDSK");
			blk = be32_to_cpu(rdb->rdb_PartitionList);
			brelse(bh);
			for (part = 1; blk > 0 && part <= 16; part++) {
				if (!(bh = bread(dev,blk,blocksize))) {
					printk("Dev %s: unable to read partition block %d\n",
						       kdevname(dev),blk);
					goto rdb_done;
				}
				pb  = (struct PartitionBlock *)bh->b_data;
				blk = be32_to_cpu(pb->pb_Next);
				if (pb->pb_ID == cpu_to_be32(IDNAME_PARTITION) && checksum_block(
				    (u32 *)pb,be32_to_cpu(pb->pb_SummedLongs) & 0x7F) == 0 ) {

					/* Tell Kernel about it */

					if (!(nr_sects = (be32_to_cpu(pb->pb_Environment[10]) + 1 -
							  be32_to_cpu(pb->pb_Environment[9])) *
							 be32_to_cpu(pb->pb_Environment[3]) *
							 be32_to_cpu(pb->pb_Environment[5]))) {
						brelse(bh);
						continue;
					}
					start_sect = be32_to_cpu(pb->pb_Environment[9]) *
						     be32_to_cpu(pb->pb_Environment[3]) *
						     be32_to_cpu(pb->pb_Environment[5]);
					add_partition(hd,current_minor,
							start_sect,nr_sects,0);
					current_minor++;
					res = 1;
				}
				brelse(bh);
			}
			printk("\n");
			break;
		}
		else
			brelse(bh);
	}

rdb_done:
	set_blocksize(dev,old_blocksize);
	return res;
}
#endif /* CONFIG_AMIGA_PARTITION */

#ifdef CONFIG_MAC_PARTITION
#include <linux/ctype.h>

/*
 * Code to understand MacOS partition tables.
 */

#define MAC_PARTITION_MAGIC	0x504d

/* type field value for A/UX or other Unix partitions */
#define APPLE_AUX_TYPE	"Apple_UNIX_SVR2"

struct mac_partition {
	__u16	signature;	/* expected to be MAC_PARTITION_MAGIC */
	__u16	res1;
	__u32	map_count;	/* # blocks in partition map */
	__u32	start_block;	/* absolute starting block # of partition */
	__u32	block_count;	/* number of blocks in partition */
	char	name[32];	/* partition name */
	char	type[32];	/* string type description */
	__u32	data_start;	/* rel block # of first data block */
	__u32	data_count;	/* number of data blocks */
	__u32	status;		/* partition status bits */
	__u32	boot_start;
	__u32	boot_size;
	__u32	boot_load;
	__u32	boot_load2;
	__u32	boot_entry;
	__u32	boot_entry2;
	__u32	boot_cksum;
	char	processor[16];	/* identifies ISA of boot */
	/* there is more stuff after this that we don't need */
};

#define MAC_STATUS_BOOTABLE	8	/* partition is bootable */

#define MAC_DRIVER_MAGIC	0x4552

/* Driver descriptor structure, in block 0 */
struct mac_driver_desc {
	__u16	signature;	/* expected to be MAC_DRIVER_MAGIC */
	__u16	block_size;
	__u32	block_count;
    /* ... more stuff */
};

static inline void mac_fix_string(char *stg, int len)
{
	int i;

	for (i = len - 1; i >= 0 && stg[i] == ' '; i--)
		stg[i] = 0;
}

static int mac_partition(struct gendisk *hd, kdev_t dev, unsigned long fsec)
{
	struct buffer_head *bh;
	int blk, blocks_in_map;
	int dev_bsize, dev_pos, pos;
	unsigned secsize;
#ifdef CONFIG_PPC
	int found_root = 0;
	int found_root_goodness = 0;
#endif
	struct mac_partition *part;
	struct mac_driver_desc *md;

	dev_bsize = get_ptable_blocksize(dev);
	dev_pos = 0;
	/* Get 0th block and look at the first partition map entry. */
	if ((bh = bread(dev, 0, dev_bsize)) == 0) {
	    printk("%s: error reading partition table\n",
		   kdevname(dev));
	    return -1;
	}
	md = (struct mac_driver_desc *) bh->b_data;
	if (be16_to_cpu(md->signature) != MAC_DRIVER_MAGIC) {
		brelse(bh);
		return 0;
	}
	secsize = be16_to_cpu(md->block_size);
	if (secsize >= dev_bsize) {
		brelse(bh);
		dev_pos = secsize;
		if ((bh = bread(dev, secsize/dev_bsize, dev_bsize)) == 0) {
			printk("%s: error reading partition table\n",
			       kdevname(dev));
			return -1;
		}
	}
	part = (struct mac_partition *) (bh->b_data + secsize - dev_pos);
	if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC) {
		brelse(bh);
		return 0;		/* not a MacOS disk */
	}
	blocks_in_map = be32_to_cpu(part->map_count);
	for (blk = 1; blk <= blocks_in_map; ++blk) {
		pos = blk * secsize;
		if (pos >= dev_pos + dev_bsize) {
			brelse(bh);
			dev_pos = pos;
			if ((bh = bread(dev, pos/dev_bsize, dev_bsize)) == 0) {
				printk("%s: error reading partition table\n",
				       kdevname(dev));
				return -1;
			}
		}
		part = (struct mac_partition *) (bh->b_data + pos - dev_pos);
		if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
			break;
		blocks_in_map = be32_to_cpu(part->map_count);
		add_partition(hd, current_minor,
			fsec + be32_to_cpu(part->start_block) * (secsize/512),
			be32_to_cpu(part->block_count) * (secsize/512), 0);

#ifdef CONFIG_PPC
		/*
		 * If this is the first bootable partition, tell the
		 * setup code, in case it wants to make this the root.
		 */
		if (_machine == _MACH_Pmac) {
			int goodness = 0;

			mac_fix_string(part->processor, 16);
			mac_fix_string(part->name, 32);
			mac_fix_string(part->type, 32);					
		    
			if ((be32_to_cpu(part->status) & MAC_STATUS_BOOTABLE)
			    && strcasecmp(part->processor, "powerpc") == 0)
				goodness++;

			if (strcasecmp(part->type, "Apple_UNIX_SVR2") == 0
			    || (strnicmp(part->type, "Linux", 5) == 0
			        && strcasecmp(part->type, "Linux_swap") != 0)) {
				int i, l;

				goodness++;
				l = strlen(part->name);
				if (strcmp(part->name, "/") == 0)
					goodness++;
				for (i = 0; i <= l - 4; ++i) {
					if (strnicmp(part->name + i, "root",
						     4) == 0) {
						goodness += 2;
						break;
					}
				}
				if (strnicmp(part->name, "swap", 4) == 0)
					goodness--;
			}

			if (goodness > found_root_goodness) {
				found_root = blk;
				found_root_goodness = goodness;
			}
		}
#endif /* CONFIG_PPC */

		++current_minor;
	}
#ifdef CONFIG_PPC
	if (found_root_goodness)
		note_bootable_part(dev, found_root, found_root_goodness);
#endif
	brelse(bh);
	printk("\n");
	return 1;
}

#endif /* CONFIG_MAC_PARTITION */

#ifdef CONFIG_ATARI_PARTITION
#include <linux/atari_rootsec.h>

/* ++guenther: this should be settable by the user ("make config")?.
 */
#define ICD_PARTS

static int atari_partition (struct gendisk *hd, kdev_t dev,
			    unsigned long first_sector)
{
  int minor = current_minor, m_lim = current_minor + hd->max_p;
  struct buffer_head *bh;
  struct rootsector *rs;
  struct partition_info *pi;
  ulong extensect;
#ifdef ICD_PARTS
  int part_fmt = 0; /* 0:unknown, 1:AHDI, 2:ICD/Supra */
#endif

  bh = bread (dev, 0, get_ptable_blocksize(dev));
  if (!bh)
    {
      printk (" unable to read block 0\n");
      return -1;
    }

  rs = (struct rootsector *) bh->b_data;
  pi = &rs->part[0];
  printk (" AHDI");
  for (; pi < &rs->part[4] && minor < m_lim; minor++, pi++)
    {
      if (pi->flg & 1)
	/* active partition */
	{
	  if (memcmp (pi->id, "XGM", 3) == 0)
	    /* extension partition */
	    {
	      struct rootsector *xrs;
	      struct buffer_head *xbh;
	      ulong partsect;

#ifdef ICD_PARTS
	      part_fmt = 1;
#endif
	      printk(" XGM<");
	      partsect = extensect = be32_to_cpu(pi->st);
	      while (1)
		{
		  xbh = bread (dev, partsect / 2, get_ptable_blocksize(dev));
		  if (!xbh)
		    {
		      printk (" block %ld read failed\n", partsect);
		      brelse(bh);
		      return 0;
		    }
		  if (partsect & 1)
		    xrs = (struct rootsector *) &xbh->b_data[512];
		  else
		    xrs = (struct rootsector *) &xbh->b_data[0];

		  /* ++roman: sanity check: bit 0 of flg field must be set */
		  if (!(xrs->part[0].flg & 1)) {
		    printk( "\nFirst sub-partition in extended partition is not valid!\n" );
		    break;
		  }

		  add_partition(hd, minor, partsect + be32_to_cpu(xrs->part[0].st),
				be32_to_cpu(xrs->part[0].siz), 0);

		  if (!(xrs->part[1].flg & 1)) {
		    /* end of linked partition list */
		    brelse( xbh );
		    break;
		  }
		  if (memcmp( xrs->part[1].id, "XGM", 3 ) != 0) {
		    printk( "\nID of extended partition is not XGM!\n" );
		    brelse( xbh );
		    break;
		  }

		  partsect = be32_to_cpu(xrs->part[1].st) + extensect;
		  brelse (xbh);
		  minor++;
		  if (minor >= m_lim) {
		    printk( "\nMaximum number of partitions reached!\n" );
		    break;
		  }
		}
	      printk(" >");
	    }
	  else
	    {
	      /* we don't care about other id's */
	      printk(" %c%c%c(%d)", pi->id[0], pi->id[1], pi->id[2], be32_to_cpu(pi->siz)/2048);
	      add_partition (hd, minor, be32_to_cpu(pi->st), be32_to_cpu(pi->siz), 0);
	    }
	}
    }
#ifdef ICD_PARTS
  if ( part_fmt!=1 ) /* no extended partitions -> test ICD-format */
  {
    pi = &rs->icdpart[0];
    /* sanity check: no ICD format if first partition invalid */
    if (memcmp (pi->id, "GEM", 3) == 0 ||
        memcmp (pi->id, "BGM", 3) == 0 ||
        memcmp (pi->id, "LNX", 3) == 0 ||
        memcmp (pi->id, "SWP", 3) == 0 ||
        memcmp (pi->id, "RAW", 3) == 0 )
    {
      printk(" ICD<");
      for (; pi < &rs->icdpart[8] && minor < m_lim; minor++, pi++)
      {
        /* accept only GEM,BGM,RAW,LNX,SWP partitions */
        if (pi->flg & 1 && 
            (memcmp (pi->id, "GEM", 3) == 0 ||
             memcmp (pi->id, "BGM", 3) == 0 ||
             memcmp (pi->id, "LNX", 3) == 0 ||
             memcmp (pi->id, "SWP", 3) == 0 ||
             memcmp (pi->id, "RAW", 3) == 0) )
        {
          part_fmt = 2;
	  add_partition (hd, minor, pi->st, pi->siz, 0);
        }
      }
      printk(" >");
    }
  }
#endif
  brelse (bh);

  printk ("\n");

  return 1;
}
#endif /* CONFIG_ATARI_PARTITION */

#ifdef CONFIG_ULTRIX_PARTITION

static int ultrix_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector)
{
	int i, minor = current_minor;
	struct buffer_head *bh;
	struct ultrix_disklabel {
		long	pt_magic;	/* magic no. indicating part. info exits */
		int	pt_valid;	/* set by driver if pt is current */
		struct  pt_info {
			int		pi_nblocks; /* no. of sectors */
			unsigned long	pi_blkoff;  /* block offset for start */
		} pt_part[8];
	} *label;

#define PT_MAGIC	0x032957	/* Partition magic number */
#define PT_VALID	1		/* Indicates if struct is valid */

#define	SBLOCK	((unsigned long)((16384 - sizeof(struct ultrix_disklabel)) \
                  /get_ptable_blocksize(dev)))

	bh = bread (dev, SBLOCK, get_ptable_blocksize(dev));
	if (!bh) {
		printk (" unable to read block 0x%lx\n", SBLOCK);
		return -1;
	}
	
	label = (struct ultrix_disklabel *)(bh->b_data
                                            + get_ptable_blocksize(dev)
                                            - sizeof(struct ultrix_disklabel));

	if (label->pt_magic == PT_MAGIC && label->pt_valid == PT_VALID) {
		for (i=0; i<8; i++, minor++)
			if (label->pt_part[i].pi_nblocks)
				add_partition(hd, minor, 
					      label->pt_part[i].pi_blkoff,
					      label->pt_part[i].pi_nblocks);
		brelse(bh);
		printk ("\n");
		return 1;
	} else {
		brelse(bh);
		return 0;
	}
}

#endif /* CONFIG_ULTRIX_PARTITION */

#ifdef CONFIG_ARCH_S390
#include <linux/malloc.h>
#include <linux/hdreg.h>
#include <linux/ioctl.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>

typedef enum {
  ibm_partition_none = 0,
  ibm_partition_lnx1 = 1,
  ibm_partition_vol1 = 3,
  ibm_partition_cms1 = 4
} ibm_partition_t;

static ibm_partition_t
get_partition_type ( char * type )
{
        static char lnx[5]="LNX1";
        static char vol[5]="VOL1";
        static char cms[5]="CMS1";
        if ( ! strncmp ( lnx, "LNX1",4 ) ) {
                ASCEBC(lnx,4);
                ASCEBC(vol,4);
                ASCEBC(cms,4);
        }
        if ( ! strncmp (type,lnx,4) ||
             ! strncmp (type,"LNX1",4) )
                return ibm_partition_lnx1;
        if ( ! strncmp (type,vol,4) )
                return ibm_partition_vol1;
        if ( ! strncmp (type,cms,4) )
                return ibm_partition_cms1;
        return ibm_partition_none;
}

int 
ibm_partition (struct gendisk *hd, kdev_t dev, int first_sector)
{
	struct buffer_head *bh;
	ibm_partition_t partition_type;
	char type[5] = {0,};
	char name[7] = {0,};
	struct hd_geometry geo;
	mm_segment_t old_fs;
	int blocksize;
	struct file *filp = NULL;
	struct inode *inode = NULL;
	int offset, size;
	int rc;

	blocksize = hardsect_size[MAJOR(dev)][MINOR(dev)];
	if ( blocksize <= 0 ) {
		return 0;
	}
	set_blocksize(dev, blocksize);  /* OUCH !! */

	/* find out offset of volume label (partn table) */
	filp = (struct file *)kmalloc (sizeof(struct file),GFP_KERNEL);
	if ( filp == NULL ) {
		printk (KERN_WARNING __FILE__ " ibm_partition: kmalloc failed for filp\n");
		return 0;
	}
	memset(filp,0,sizeof(struct file));
	filp ->f_mode = 1; /* read only */
	inode = get_empty_inode();
	inode -> i_rdev = dev;
	rc = blkdev_open(inode,filp);
	if ( rc ) {
		return 0;
	}
	old_fs=get_fs();
	set_fs(KERNEL_DS);
	rc = filp->f_op->ioctl (inode, filp, HDIO_GETGEO, (unsigned long)(&geo));
	set_fs(old_fs);
	if ( rc ) {
		return 0;
	}
	blkdev_release(inode);
	
	size = hd -> sizes[MINOR(dev)]<<1;
	if ( ( bh = bread( dev, geo.start, blocksize) ) != NULL ) {
		strncpy ( type,bh -> b_data, 4);
		strncpy ( name,bh -> b_data + 4, 6);
        } else {
		return 0;
	}
	if ( (*(char *)bh -> b_data) & 0x80 ) {
		EBCASC(name,6);
	}
	switch ( partition_type = get_partition_type(type) ) {
	case ibm_partition_lnx1: 
		offset = (geo.start + 1);
		printk ( "(LNX1)/%6s:",name);
		break;
	case ibm_partition_vol1:
		offset = 0;
		size = 0;
		printk ( "(VOL1)/%6s:",name);
		break;
	case ibm_partition_cms1:
		printk ( "(CMS1)/%6s:",name);
		if (* (((long *)bh->b_data) + 13) == 0) {
			/* disk holds a CMS filesystem */
			offset = (geo.start + 1);
			printk ("(CMS)");
		} else {
			/* disk is reserved minidisk */
			// mdisk_setup_data.size[i] =
			// (label[7] - 1 - label[13]) *
			// (label[3] >> 9) >> 1;
			long *label=(long*)bh->b_data;
			blocksize = label[3];
			offset = label[13];
			size = (label[7]-1)*(blocksize>>9); 
			printk ("(MDSK)");
		}
		break;
	case ibm_partition_none:
		printk ( "(nonl)/      :");
		offset = (geo.start+1);
		break;
	default:
		offset = 0;
		size = 0;
		
	}
	add_partition( hd, MINOR(dev), 0,size,0);
	add_partition( hd, MINOR(dev) + 1, offset * (blocksize >> 9),
		       size-offset*(blocksize>>9) ,0 );
	printk ( "\n" );
	bforget(bh);
	return 1;
}
#endif

static void check_partition(struct gendisk *hd, kdev_t dev)
{
	static int first_time = 1;
	unsigned long first_sector;
	char buf[MAX_DISKNAME_LEN];

	if (first_time)
		printk(KERN_INFO "Partition check:\n");
	first_time = 0;
	first_sector = hd->part[MINOR(dev)].start_sect;

	/*
	 * This is a kludge to allow the partition check to be
	 * skipped for specific drives (e.g. IDE CD-ROM drives)
	 */
	if ((int)first_sector == -1) {
		hd->part[MINOR(dev)].start_sect = 0;
		return;
	}

	printk(KERN_INFO " %s:", disk_name(hd, MINOR(dev), buf));
#ifdef CONFIG_MSDOS_PARTITION
	if (msdos_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_OSF_PARTITION
	if (osf_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_SUN_PARTITION
	if(sun_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_AMIGA_PARTITION
	if(amiga_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_MAC_PARTITION
	if (mac_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_SGI_PARTITION
	if(sgi_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_ULTRIX_PARTITION
	if(ultrix_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_ATARI_PARTITION
	if(atari_partition(hd, dev, first_sector))
		return;
#endif
#ifdef CONFIG_ARCH_S390
	if (ibm_partition (hd, dev, first_sector))
		return;
#endif
	printk(" unknown partition table\n");
}

/* This function is used to re-read partition tables for removable disks.
   Much of the cleanup from the old partition tables should have already been
   done */

/* This function will re-read the partition tables for a given device,
and set things back up again.  There are some important caveats,
however.  You must ensure that no one is using the device, and no one
can start using the device while this function is being executed. */

void resetup_one_dev(struct gendisk *dev, int drive)
{
	int i;
	int first_minor	= drive << dev->minor_shift;
	int end_minor	= first_minor + dev->max_p;

	blk_size[dev->major] = NULL;
	current_minor = 1 + first_minor;
	check_partition(dev, MKDEV(dev->major, first_minor));

 	/*
 	 * We need to set the sizes array before we will be able to access
 	 * any of the partitions on this device.
 	 */
	if (dev->sizes != NULL) {	/* optional safeguard in ll_rw_blk.c */
		for (i = first_minor; i < end_minor; i++)
			dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
		blk_size[dev->major] = dev->sizes;
	}
}

static inline void setup_dev(struct gendisk *dev)
{
	int i, drive;
	int end_minor	= dev->max_nr * dev->max_p;

	blk_size[dev->major] = NULL;
	for (i = 0 ; i < end_minor; i++) {
		dev->part[i].start_sect = 0;
		dev->part[i].nr_sects = 0;
	}
	if ( dev->init != NULL )
		dev->init(dev);	
	for (drive = 0 ; drive < dev->nr_real ; drive++) {
		int first_minor	= drive << dev->minor_shift;
		current_minor = 1 + first_minor;
		/* If we really have a device check partition table */
		if ( blksize_size[dev->major] == NULL ||
		     blksize_size[dev->major][current_minor])
			check_partition(dev, MKDEV(dev->major, first_minor));
	}
	if (dev->sizes != NULL) {	/* optional safeguard in ll_rw_blk.c */
		for (i = 0; i < end_minor; i++) 
			dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
		blk_size[dev->major] = dev->sizes;
	}
}

__initfunc(void device_setup(void))
{
	extern void console_map_init(void);
	extern void cpqarray_init(void);
#ifdef CONFIG_BLK_CPQ_CISS_DA
	extern int  cciss_init(void);
#endif 
#ifdef CONFIG_PARPORT
	extern int parport_init(void);
#endif
#ifdef CONFIG_MD_BOOT
        extern void md_setup_drive(void) __init;
#endif
#ifdef CONFIG_FC4_SOC
	extern int soc_probe(void);
#endif
	struct gendisk *p;

#ifdef CONFIG_PARPORT
	parport_init();
#endif
	chr_dev_init();
	blk_dev_init();
	sti();
#ifdef CONFIG_I2O
	i2o_init();
#endif
#ifdef CONFIG_BLK_DEV_DAC960
	DAC960_Initialize();
#endif
#ifdef CONFIG_FC4_SOC
	/* This has to be done before scsi_dev_init */
	soc_probe();
#endif
#ifdef CONFIG_SCSI
	scsi_dev_init();
#endif
#ifdef CONFIG_BLK_CPQ_DA
	cpqarray_init();
#endif
#ifdef CONFIG_BLK_CPQ_CISS_DA
        cciss_init();
#endif
#ifdef CONFIG_NET
	net_dev_init();
#endif
#ifdef CONFIG_VT
	console_map_init();
#endif

	for (p = gendisk_head ; p ; p=p->next)
		setup_dev(p);

#ifdef CONFIG_BLK_DEV_RAM
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && mount_initrd) initrd_load();
	else
#endif
	rd_load();
#endif
#ifdef CONFIG_MD_BOOT
        md_setup_drive();
#endif
}

#ifdef CONFIG_PROC_FS
int get_partition_list(char *page, char **start, off_t offset, int count)
{
	struct gendisk *p;
	char buf[MAX_DISKNAME_LEN];
	int n, len;

	len = sprintf(page, "major minor  #blocks  name\n\n");
	for (p = gendisk_head; p; p = p->next) {
		for (n=0; n < (p->nr_real << p->minor_shift); n++) {
			if (p->part[n].nr_sects) {
				len += sprintf(page+len,
					       "%4d  %4d %10d %s\n",
					       p->major, n, p->sizes[n],
					       disk_name(p, n, buf));
				if (len < offset) 
					offset -= len, len = 0;
				else if (len >= offset + count)
					goto leave_loops;
			}
		}
	}
leave_loops:
	*start = page + offset;
	len -= offset;
	if (len < 0)
		len = 0;
	return len > count ? count : len;
}
#endif
