/* 
 * QNX4 file system, Linux implementation.
 * 
 * Version : 0.2.1
 * 
 * Using parts of the xiafs filesystem.
 * 
 * History :
 * 
 * 28-05-1998 by Richard Frowijn : first release.
 * 21-06-1998 by Frank Denis : ugly changes to make it compile on Linux 2.1.99+
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/qnx4_fs.h>
#include <linux/stat.h>

#include <asm/segment.h>
#include <asm/uaccess.h>

static int qnx4_readlink(struct dentry *, char *, int);
static struct dentry *qnx4_follow_link(struct dentry *, struct dentry *, unsigned int follow);

/*
 * symlinks can't do much...
 */
struct inode_operations qnx4_symlink_inode_operations =
{
	readlink:	qnx4_readlink,
	follow_link:	qnx4_follow_link,
};

static struct dentry *qnx4_follow_link(struct dentry *dentry,
				       struct dentry *base, unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh;

	if ( !inode ) {
		return ERR_PTR(-ENOENT);
	}
	if ( !( bh = qnx4_bread( inode, 0, 0 ) ) ) {
		dput( base );
		return ERR_PTR(-EIO);
	}
	UPDATE_ATIME( inode );
	base = lookup_dentry( bh->b_data, base, follow );
	brelse( bh );
	return base;
}

static int qnx4_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh;
	int i;
	char c;

	QNX4DEBUG(("qnx4: qnx4_readlink() called\n"));

	if (buffer == NULL || inode == NULL || !S_ISLNK(inode->i_mode)) {
		return -EINVAL;
	}
	if (buflen > 1023) {
		buflen = 1023;
	}
	bh = qnx4_bread( inode, 0, 0 );
	if (bh == NULL) {
		QNX4DEBUG(("qnx4: NULL symlink bh\n"));
		return 0;
	}
	QNX4DEBUG(("qnx4: qnx4_bread sym called -> [%s]\n",
		   bh->b_data));
	i = 0;
	while (i < buflen && (c = bh->b_data[i])) {
		i++;
		put_user(c, buffer++);
	}
	brelse(bh);
	return i;
}
