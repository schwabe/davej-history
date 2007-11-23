/*
 *  file.c
 *
 *  Copyright (C) 1995, 1996, 1997 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 * Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>

#include <linux/smbno.h>
#include <linux/smb_fs.h>

#include "smb_debug.h"


static inline void
smb_unlock_page(struct page *page)
{
	clear_bit(PG_locked, &page->flags);
	wake_up(&page->wait);
}

static int
smb_fsync(struct file *file, struct dentry * dentry)
{
	VERBOSE("sync file %s/%s\n", DENTRY_PATH(dentry));
	return 0;
}

/*
 * Read a page synchronously.
 */
static int
smb_readpage_sync(struct dentry *dentry, struct page *page)
{
	char *buffer = (char *) page_address(page);
	unsigned long offset = page->offset;
	int rsize = smb_get_rsize(server_from_dentry(dentry));
	int count = PAGE_SIZE;
	int result;

	clear_bit(PG_error, &page->flags);

	VERBOSE("file %s/%s, count=%d@%ld, rsize=%d\n",
		DENTRY_PATH(dentry), count, offset, rsize);
	result = smb_open(dentry, SMB_O_RDONLY);
	if (result < 0)
	{
		PARANOIA("%s/%s open failed, error=%d\n",
			 DENTRY_PATH(dentry), result);
		goto io_error;
	}

	do {
		if (count < rsize)
			rsize = count;

		result = smb_proc_read(dentry, offset, rsize, buffer);
		if (result < 0)
			goto io_error;

		count -= result;
		offset += result;
		buffer += result;
		dentry->d_inode->i_atime = CURRENT_TIME;
		if (result < rsize)
			break;
	} while (count);

	memset(buffer, 0, count);
	flush_dcache_page(page_address(page));
	set_bit(PG_uptodate, &page->flags);
	result = 0;

io_error:
	smb_unlock_page(page);
	return result;
}

int
smb_readpage(struct file *file, struct page *page)
{
	struct dentry *dentry = file->f_dentry;
	int		error;

	DEBUG1("SMB: smb_readpage %08lx\n", page_address(page));
#ifdef SMBFS_PARANOIA
	if (test_bit(PG_locked, &page->flags))
		PARANOIA("page already locked!\n");
#endif
	set_bit(PG_locked, &page->flags);
	atomic_inc(&page->count);
	error = smb_readpage_sync(dentry, page);
	free_page(page_address(page));
	return error;
}

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int
smb_writepage_sync(struct dentry *dentry, struct page *page,
		   unsigned long offset, unsigned int count)
{
	struct inode *inode = dentry->d_inode;
	u8 *buffer = (u8 *) page_address(page) + offset;
	int wsize = smb_get_wsize(server_from_dentry(dentry));
	int result, written = 0;

	offset += page->offset;
	VERBOSE("file %s/%s, count=%d@%ld, wsize=%d\n",
		DENTRY_PATH(dentry), count, offset, wsize);

	do {
		if (count < wsize)
			wsize = count;

		result = smb_proc_write(dentry, offset, wsize, buffer);
		if (result < 0)
			break;
		/* N.B. what if result < wsize?? */
#ifdef SMBFS_PARANOIA
		if (result < wsize)
			printk(KERN_DEBUG "short write, wsize=%d, result=%d\n",
			       wsize, result);
#endif
		buffer += wsize;
		offset += wsize;
		written += wsize;
		count -= wsize;
		/*
		 * Update the inode now rather than waiting for a refresh.
		 */
		inode->i_mtime = inode->i_atime = CURRENT_TIME;
		if (offset > inode->i_size)
			inode->i_size = offset;
		inode->u.smbfs_i.cache_valid |= SMB_F_LOCALWRITE;
	} while (count);
	return written ? written : result;
}

/*
 * Write a page to the server. This will be used for NFS swapping only
 * (for now), and we currently do this synchronously only.
 */
static int
smb_writepage(struct file *file, struct page *page)
{
	struct dentry *dentry = file->f_dentry;
	int 	result;

#ifdef SMBFS_PARANOIA
	if (test_bit(PG_locked, &page->flags))
		PARANOIA("page already locked!\n");
#endif
	set_bit(PG_locked, &page->flags);
	atomic_inc(&page->count);
	result = smb_writepage_sync(dentry, page, 0, PAGE_SIZE);
	smb_unlock_page(page);
	free_page(page_address(page));
	return result;
}

static int
smb_updatepage(struct file *file, struct page *page, unsigned long offset, unsigned int count, int sync)
{
	struct dentry *dentry = file->f_dentry;

	DEBUG1("(%s/%s %d@%ld, sync=%d)\n",
	       DENTRY_PATH(dentry), count, page->offset+offset, sync);

	return smb_writepage_sync(dentry, page, offset, count);
}

static ssize_t
smb_file_read(struct file * file, char * buf, size_t count, loff_t *ppos)
{
	struct dentry * dentry = file->f_dentry;
	ssize_t	status;

	VERBOSE("file %s/%s, count=%lu@%lu\n", DENTRY_PATH(dentry),
		(unsigned long) count, (unsigned long) *ppos);

	status = smb_revalidate_inode(dentry);
	if (status)
	{
		PARANOIA("%s/%s validation failed, error=%d\n",
			 DENTRY_PATH(dentry), status);
		goto out;
	}

	VERBOSE("before read, size=%ld, pages=%ld, flags=%x, atime=%ld\n",
		dentry->d_inode->i_size, dentry->d_inode->i_nrpages,
		dentry->d_inode->i_flags, dentry->d_inode->i_atime);

	status = generic_file_read(file, buf, count, ppos);
out:
	return status;
}

static int
smb_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct dentry * dentry = file->f_dentry;
	int	status;

	VERBOSE("file %s/%s, address %lu - %lu\n",
		DENTRY_PATH(dentry), vma->vm_start, vma->vm_end);

	status = smb_revalidate_inode(dentry);
	if (status)
	{
		PARANOIA("%s/%s validation failed, error=%d\n",
			 DENTRY_PATH(dentry), status);
		goto out;
	}
	status = generic_file_mmap(file, vma);
out:
	return status;
}

/* 
 * Write to a file (through the page cache).
 */
static ssize_t
smb_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct dentry * dentry = file->f_dentry;
	ssize_t	result;

	VERBOSE("file %s/%s, count=%lu@%lu, pages=%ld\n", DENTRY_PATH(dentry),
		(unsigned long) count, (unsigned long) *ppos,
		dentry->d_inode->i_nrpages);

	result = smb_revalidate_inode(dentry);
	if (result)
	{
		PARANOIA("%s/%s validation failed, error=%d\n",
			 DENTRY_PATH(dentry), result);
		goto out;
	}

	result = smb_open(dentry, SMB_O_WRONLY);
	if (result)
		goto out;

	if (count > 0)
	{
		result = generic_file_write(file, buf, count, ppos);
		VERBOSE("pos=%ld, size=%ld, mtime=%ld, atime=%ld\n",
			(long) file->f_pos, dentry->d_inode->i_size,
			dentry->d_inode->i_mtime, dentry->d_inode->i_atime);
	}
out:
	return result;
}

static int
smb_file_open(struct inode *inode, struct file * file)
{
	VERBOSE("opening %s/%s, d_count=%d\n",
		DENTRY_PATH(file->f_dentry), file->f_dentry->d_count);
	return 0;
}

static int
smb_file_release(struct inode *inode, struct file * file)
{
	struct dentry * dentry = file->f_dentry;

	VERBOSE("closing %s/%s, d_count=%d\n",
		DENTRY_PATH(dentry), dentry->d_count);

	if (dentry->d_count == 1)
		smb_close(inode);

	return 0;
}

/*
 * Check whether the required access is compatible with
 * an inode's permission. SMB doesn't recognize superuser
 * privileges, so we need our own check for this.
 */
static int
smb_file_permission(struct inode *inode, int mask)
{
	int mode = inode->i_mode;
	int error = 0;

	VERBOSE("mode=%x, mask=%x\n", mode, mask);

	/* Look at user permissions */
	mode >>= 6;
	if ((mode & 7 & mask) != mask)
		error = -EACCES;
	return error;
}

static struct file_operations smb_file_operations =
{
	NULL,			/* lseek - default */
	smb_file_read,		/* read */
	smb_file_write,		/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	smb_ioctl,		/* ioctl */
	smb_file_mmap,		/* mmap(struct file*, struct vm_area_struct*) */
	smb_file_open,		/* open(struct inode*, struct file*) */
	NULL,			/* flush */
	smb_file_release,	/* release(struct inode*, struct file*) */
	smb_fsync,		/* fsync(struct file*, struct dentry*) */
	NULL,			/* fasync(struct file*, int) */
	NULL,			/* check_media_change(kdev_t dev) */
	NULL,			/* revalidate(kdev_t dev) */
	NULL			/* lock(struct file*, int, struct file_lock*) */
};

struct inode_operations smb_file_inode_operations =
{
	&smb_file_operations,	/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	smb_readpage,		/* readpage */
	smb_writepage,		/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	smb_file_permission,	/* permission */
	NULL,			/* smap */
	smb_updatepage,		/* updatepage */
	smb_revalidate_inode,	/* revalidate */
};
