/*
  Structure of the proc filesystem:
  /proc/dasd/
  /proc/dasd/devices                  # List of devices
  /proc/dasd/ddabcd                   # Device node for devno abcd            
  /proc/dasd/ddabcd1                  # Device node for partition abcd  
  /proc/dasd/abcd                     # Device information for devno abcd
*/

#include <linux/proc_fs.h>

#include <linux/dasd.h>

#include "dasd_types.h"

int dasd_proc_read_devices ( char *, char **, off_t, int, int);
#ifdef DASD_PROFILE
extern int dasd_proc_read_statistics ( char *, char **, off_t, int, int);
extern int dasd_proc_read_debug ( char *, char **, off_t, int, int);
#endif /* DASD_PROFILE */

struct proc_dir_entry dasd_proc_root_entry =
{
	low_ino:0,
	namelen:4,
	name:"dasd",
	mode:S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	nlink:1,
	uid:0,
	gid:0,
	size:0
};

struct proc_dir_entry dasd_proc_devices_entry =
{
	low_ino:0,
	namelen:7,
	name:"devices",
	mode:S_IFREG | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	nlink:1,
	uid:0,
	gid:0,
	size:0,
	get_info:&dasd_proc_read_devices,
};

#ifdef DASD_PROFILE
struct proc_dir_entry dasd_proc_stats_entry =
{
	low_ino:0,
	namelen:10,
	name:"statistics",
	mode:S_IFREG | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	nlink:1,
	uid:0,
	gid:0,
	size:0,
	get_info:&dasd_proc_read_statistics
};

struct proc_dir_entry dasd_proc_debug_entry =
{
	low_ino:0,
	namelen:5,
	name:"debug",
	mode:S_IFREG | S_IRUGO | S_IXUGO | S_IWUSR | S_IWGRP,
	nlink:1,
	uid:0,
	gid:0,
	size:0,
	get_info:&dasd_proc_read_debug
};
#endif /* DASD_PROFILE */

struct proc_dir_entry dasd_proc_device_template =
{
	0,
	6,"dd????",
	S_IFBLK | S_IRUGO | S_IWUSR | S_IWGRP,
	1,0,0,
	0,
	NULL,
};

void
dasd_proc_init ( void )
{
	proc_register( & proc_root, & dasd_proc_root_entry);
	proc_register( & dasd_proc_root_entry, & dasd_proc_devices_entry);
#ifdef DASD_PROFILE
 	proc_register( & dasd_proc_root_entry, & dasd_proc_stats_entry); 
 	proc_register( & dasd_proc_root_entry, & dasd_proc_debug_entry); 
#endif /* DASD_PROFILE */
}

int 
dasd_proc_read_devices ( char * buf, char **start, off_t off, int len, int d)
{
	int i;
	len = sprintf ( buf, "dev# MAJ minor node        Format\n");
	for ( i = 0; i < DASD_MAX_DEVICES; i++ ) {
		dasd_information_t *info = dasd_info[i];
		if ( ! info ) 
			continue;
		if ( len >= PAGE_SIZE - 80 )
			len += sprintf ( buf + len, "terminated...\n");
		len += sprintf ( buf + len,
				 "%04X %3d %5d /dev/dasd%c",
				 dasd_info[i]->info.devno,
				 DASD_MAJOR,
				 i << PARTN_BITS,
				 'a' + i );
		switch (atomic_read (&info->status)) {
		case DASD_INFO_STATUS_UNKNOWN:
			len += sprintf (buf + len, " unknown");
			break;
		case DASD_INFO_STATUS_DETECTED:
			len += sprintf (buf + len, "   avail");
			break;
		case DASD_INFO_STATUS_ANALYSED:
			len += sprintf (buf + len, "     n/f");
			break;
		default:
			len += sprintf (buf + len, " %7d",
					 info->sizes.bp_block);
		}
		len += sprintf ( buf + len, "\n");
	} 
	return len;
}

void 
dasd_proc_add_node (int di) 
{
}
