/*
 *  linux/fs/nfs/symlink.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  Jun 7 1999, cache symlink lookups in the page cache.  -DaveM
 *
 *  nfs symlink handling code
 */

#define NFS_NEED_XDR_TYPES
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <asm/pgtable.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/string.h>

#include <asm/uaccess.h>

static int nfs_readlink(struct dentry *, char *, int);
static struct dentry *nfs_follow_link(struct dentry *, struct dentry *, unsigned int);

/*
 * symlinks can't do much...
 */
struct inode_operations nfs_symlink_inode_operations = {
	readlink:		nfs_readlink,
	follow_link:		nfs_follow_link,
	revalidate:		nfs_revalidate,
};

/* Symlink caching in the page cache is even more simplistic
 * and straight-forward than readdir caching.
 */
static int nfs_symlink_filler(struct inode *inode, struct page *page)
{
	void * buffer = (void *)page_address(page);
	unsigned int error;

	/* We place the length at the beginning of the page,
	 * in client byte order, followed by the string.
	 */
	error = NFS_PROTO(inode)->readlink(inode, buffer,
					   PAGE_CACHE_SIZE-sizeof(u32)-4);
	if (error < 0)
		goto error;
	flush_dcache_page(page_address(page)); /* Is this correct? */
	set_bit(PG_uptodate, &page->flags);
	nfs_unlock_page(page);
	return 0;
error:
	set_bit(PG_error, &page->flags);
	nfs_unlock_page(page);
	return -EIO;
}

static int nfs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct page *page;
	u32 *p, len;

	/* Caller revalidated the directory inode already. */
	page = read_cache_page(inode, 0,
				(filler_t *)nfs_symlink_filler, inode);
	if (IS_ERR(page))
		goto read_failed;

	p = (u32 *) page_address(page);
	len = *p++;
	if (len > buflen)
		len = buflen;
	copy_to_user(buffer, p, len);
	page_cache_release(page);
	return len;
read_failed:
	return PTR_ERR(page);
}

static struct dentry *
nfs_follow_link(struct dentry *dentry, struct dentry *base, unsigned int follow)
{
	struct dentry *result;
	struct inode *inode = dentry->d_inode;
	struct page *page;
	u32 *p;

	/* Caller revalidated the directory inode already. */
	page = read_cache_page(inode, 0,
				(filler_t *)nfs_symlink_filler, inode);
	if (IS_ERR(page))
		goto read_failed;

	p = (u32 *) page_address(page);
	result = lookup_dentry((char *) (p + 1), base, follow);
	page_cache_release(page);
	return result;
read_failed:
	return (struct dentry *)page;
}
