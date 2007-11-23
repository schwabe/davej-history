#ifndef _NFS_FS_i
#define _NFS_FS_I

#include <linux/list.h>
#include <linux/nfs.h>
#include <linux/pipe_fs_i.h>

/*
 * nfs fs inode data in memory
 */
struct nfs_inode_info {
	/*
	 * This is a place holder so named pipes on NFS filesystems
	 * work (more or less correctly). This must be first in the
	 * struct because the data is really accessed via inode->u.pipe_i.
	 */
	struct pipe_inode_info	pipeinfo;

	/*
	 * The 64bit fileid
	 */
	__u64 fsid;
	__u64 fileid;

	/*
	 * NFS file handle
	 */
	struct nfs_fh		fh;

	/*
	 * Various flags
	 */
	unsigned short		flags;

	/*
	 * read_cache_jiffies is when we started read-caching this inode,
	 * and read_cache_mtime is the mtime of the inode at that time.
	 * attrtimeo is for how long the cached information is assumed
	 * to be valid. A successful attribute revalidation doubles
	 * attrtimeo (up to acregmax/acdirmax), a failure resets it to
	 * acregmin/acdirmin.
	 *
	 * We need to revalidate the cached attrs for this inode if
	 *
	 *	jiffies - read_cache_jiffies > attrtimeo
	 *
	 * and invalidate any cached data/flush out any dirty pages if
	 * we find that
	 *
	 *	mtime != read_cache_mtime
	 */
	unsigned long		read_cache_jiffies;
	__u64			read_cache_ctime;
	__u64			read_cache_mtime;
	__u64			read_cache_atime;
	__u64			read_cache_isize;
	unsigned long		attrtimeo;
	unsigned long		attrtimeo_timestamp;

	/*
	 * This is the cookie verifier used for NFSv3 readdir
	 * operations
	 */
	__u32			cookieverf[2];

	/*
	 * This is the list of dirty pages.
	 */
	struct list_head	read;
	struct list_head	dirty;
	struct list_head	commit;
	struct list_head	writeback;

	unsigned int		nread,
				ndirty,
				ncommit,
				npages;

	/* Flush daemon info */
	struct inode		*hash_next,
				*hash_prev;
	unsigned long		nextscan;
};

/*
 * Legal inode flag values
 */
#define NFS_INO_STALE		0x0001		/* We suspect inode is stale */
#define NFS_INO_ADVISE_RDPLUS   0x0002          /* advise readdirplus */
#define NFS_INO_REVALIDATING    0x0004          /* in nfs_revalidate() */
#define NFS_IS_SNAPSHOT		0x0010		/* a snapshot file */
#define NFS_INO_FLUSH		0x0040		/* inode is due for flushing */

/*
 * NFS ACL info.
 * This information will be used by nfs_permission() in the obvious fashion,
 * but also helps the RPC engine to select whether to try the operation first
 * with the effective or real uid/gid first.
 *
 * For NFSv2, this info is obtained by just trying the operation in
 * question and updating the ACL info according to the result.
 * For NFSv3, the access() call is used to fill in the permission bits.
 *
 * Not yet used.
 */
struct nfs_acl_info {
	struct nfs_acl_info *	acl_next;
	unsigned long		acl_read_time;
	uid_t			acl_uid;
	gid_t			acl_gid;
	unsigned int		acl_bits;
};

/*
 * NFS lock info
 */
struct nfs_lock_info {
	u32		state;
	u32		flags;
	struct nlm_host	*host;
};

/*
 * Lock flag values
 */
#define NFS_LCK_GRANTED		0x0001		/* lock has been granted */

#endif
