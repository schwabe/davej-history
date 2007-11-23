/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

#ifdef __KERNEL__
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/in.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <linux/sunrpc/sched.h>
#endif /* __KERNEL__ */

#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs_xdr.h>

/*
 * Enable debugging support for nfs client.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define NFS_DEBUG
#endif

#define NFS_MAX_FILE_IO_BUFFER_SIZE	32768
#define NFS_DEF_FILE_IO_BUFFER_SIZE	4096

/*
 * The upper limit on timeouts for the exponential backoff algorithm.
 */
#define NFS_READ_DELAY			(2*HZ)
#define NFS_WRITEBACK_DELAY		(5*HZ)
#define NFS_WRITEBACK_LOCKDELAY		(60*HZ)
#define NFS_COMMIT_DELAY		(5*HZ)

/*
 * superblock magic number for NFS
 */
#define NFS_SUPER_MAGIC			0x6969
#define NFS_FILE_MAGIC			0xA4F0

#ifdef __KERNEL__
/*
 * Convenience macros
 */
#define NFS_FH(inode)			(&(inode)->u.nfs_i.fh)
#define NFS_SERVER(inode)		(&(inode)->i_sb->u.nfs_sb.s_server)
#define NFS_CLIENT(inode)		(NFS_SERVER(inode)->client)
#define NFS_PROTO(inode)		(NFS_SERVER(inode)->rpc_ops)
#define NFS_REQUESTLIST(inode)		(NFS_SERVER(inode)->rw_requests)
#define NFS_ADDR(inode)			(RPC_PEERADDR(NFS_CLIENT(inode)))
#define NFS_CONGESTED(inode)		(RPC_CONGESTED(NFS_CLIENT(inode)))
#define NFS_COOKIEVERF(inode)		((inode)->u.nfs_i.cookieverf)
#define NFS_READTIME(inode)		((inode)->u.nfs_i.read_cache_jiffies)
#define NFS_CACHE_CTIME(inode)		((inode)->u.nfs_i.read_cache_ctime)
#define NFS_CACHE_MTIME(inode)		((inode)->u.nfs_i.read_cache_mtime)
#define NFS_CACHE_ATIME(inode)		((inode)->u.nfs_i.read_cache_atime)
#define NFS_CACHE_ISIZE(inode)		((inode)->u.nfs_i.read_cache_isize)
#define NFS_NEXTSCAN(inode)		((inode)->u.nfs_i.nextscan)
#define NFS_CACHEINV(inode) \
do { \
	NFS_READTIME(inode) = jiffies - NFS_MAXATTRTIMEO(inode) - 1; \
} while (0)
#define NFS_ATTRTIMEO(inode)		((inode)->u.nfs_i.attrtimeo)
#define NFS_MINATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmin \
			       : NFS_SERVER(inode)->acregmin)
#define NFS_MAXATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmax \
			       : NFS_SERVER(inode)->acregmax)
#define NFS_ATTRTIMEO_UPDATE(inode)	((inode)->u.nfs_i.attrtimeo_timestamp)

#define NFS_FLAGS(inode)		((inode)->u.nfs_i.flags)
#define NFS_REVALIDATING(inode)		(NFS_FLAGS(inode) & NFS_INO_REVALIDATING)
#define NFS_STALE(inode)		(NFS_FLAGS(inode) & NFS_INO_STALE)

#define NFS_FILEID(inode)	((inode)->u.nfs_i.fileid)
#define NFS_FSID(inode)		((inode)->u.nfs_i.fsid)

/* Inode Flags */
#define NFS_USE_READDIRPLUS(inode)	((NFS_FLAGS(inode) & NFS_INO_ADVISE_RDPLUS) ? 1 : 0)

/*
 * These are the default flags for swap requests
 */
#define NFS_RPC_SWAPFLAGS		(RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS)

/* Flags in the RPC client structure */
#define NFS_CLNTF_BUFSIZE	0x0001	/* readdir buffer in longwords */

#define NFS_RW_SYNC		0x0001	/* O_SYNC handling */
#define NFS_RW_SWAP		0x0002	/* This is a swap request */

/*
 * When flushing a cluster of dirty pages, there can be different
 * strategies:
 */
#define FLUSH_AGING		0	/* only flush old buffers */
#define FLUSH_SYNC		1	/* file being synced, or contention */
#define FLUSH_WAIT		2	/* wait for completion */
#define FLUSH_STABLE		4	/* commit to stable storage */


static inline
unsigned long nfs_page_offset(struct page *page)
{
        return page->offset;
}

static inline
unsigned long page_index(struct page *page)
{
	return page->offset >> PAGE_CACHE_SHIFT;
}

/*
 * linux/fs/nfs/inode.c
 */
extern int nfs_inode_is_stale(struct inode *, struct nfs_fh *,
			       struct nfs_fattr *);
extern struct inode *nfs_fhget(struct dentry *, struct nfs_fh *,
			       struct nfs_fattr *);
extern struct super_block *nfs_read_super(struct super_block *, void *, int);
extern int	init_nfs_fs(void);
extern void	nfs_zap_caches(struct inode *);
extern int	nfs_revalidate(struct dentry *);
extern int	nfs_open(struct inode *, struct file *);
extern int	nfs_release(struct inode *, struct file *);
extern int	__nfs_revalidate_inode(struct nfs_server *, struct inode *);
extern int	nfs_refresh_inode(struct inode *, struct nfs_fattr *);
extern int	nfs_wait_on_inode(struct inode *, int flag);
extern void	nfs_unlock_inode(struct inode *);

/*
 * linux/fs/nfs/file.c
 */
extern struct inode_operations nfs_file_inode_operations;
extern int	nfs_permission(struct inode *i, int msk);

/*
 * linux/fs/nfs/dir.c
 */
extern struct inode_operations nfs_dir_inode_operations;
extern struct dentry_operations nfs_dentry_operations;
extern struct nfs_fh *nfs_fh_alloc(void);
extern void nfs_fh_free(struct nfs_fh *);


/*
 * linux/fs/nfs/symlink.c
 */
extern struct inode_operations nfs_symlink_inode_operations;

/*
 * linux/fs/nfs/locks.c
 */
extern int nfs_lock(struct file *, int, struct file_lock *);

/*
 * linux/fs/nfs/unlink.c
 */
extern int  nfs_async_unlink(struct dentry *);
extern void nfs_complete_unlink(struct dentry *);

/*
 * linux/fs/nfs/write.c
 */
extern int  nfs_writepage(struct file *, struct page *);
extern int  nfs_flush_incompatible(struct file *, struct page *);
extern int  nfs_updatepage(struct file *, struct page *,
                        unsigned long, unsigned int, int);
/*
 * Try to write back everything synchronously (but check the
 * return value!)
 */
extern int  nfs_sync_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_flush_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_commit_file(struct inode *, struct file *, unsigned long, unsigned int, int);
extern int  nfs_flush_timeout(struct inode *, int);
extern int  nfs_commit_timeout(struct inode *, int);

static inline int
nfs_have_read(struct inode *inode)
{
	return !list_empty(&inode->u.nfs_i.read);
}

static inline int
nfs_have_writebacks(struct inode *inode)
{
	return !list_empty(&inode->u.nfs_i.writeback);
}

static inline int
nfs_wb_all(struct inode *inode)
{
	int error = nfs_sync_file(inode, 0, 0, 0, FLUSH_WAIT);
	return (error < 0) ? error : 0;
}

/*
 * Write back all requests on one page - we do this before reading it.
 */
static inline int
nfs_wb_page(struct inode *inode, struct page* page)
{
	int error = nfs_sync_file(inode, 0, page_index(page), 1, FLUSH_WAIT | FLUSH_STABLE);
	return (error < 0) ? error : 0;
}

/*
 * Write back all pending writes for one user.. 
 */
static inline int
nfs_wb_file(struct inode *inode, struct file *file)
{
	int error = nfs_sync_file(inode, file, 0, 0, FLUSH_WAIT);
	return (error < 0) ? error : 0;
}

/*
 * Lock the page of an asynchronous request
 */
static __inline__ int
nfs_lock_page(struct page *page)
{
	return !test_and_set_bit(PG_locked, &page->flags);
}

static __inline__ void
nfs_unlock_page(struct page *page)
{
        clear_bit(PG_locked, &page->flags);
        wake_up(&page->wait);
}

/*
 * linux/fs/nfs/read.c
 */
extern int  nfs_readpage(struct file *, struct page *);
extern int  nfs_pagein_inode(struct inode *, unsigned long, unsigned int);
extern int  nfs_pagein_timeout(struct inode *);

/*
 * linux/fs/nfs2xdr.c
 */
extern u32 *nfs_decode_dirent(u32 *, struct nfs_entry *, int);

/*
 * linux/fs/nfs2xdr.c
 */
extern u32 *nfs3_decode_dirent(u32 *, struct nfs_entry *, int);

/*
 * linux/fs/mount_clnt.c
 * (Used only by nfsroot module)
 */
extern int  nfs_mount(struct sockaddr_in *, char *, struct nfs3_fh *);
extern int  nfs3_mount(struct sockaddr_in *, char *, struct nfs3_fh *);

/*
 * inline functions
 */
static inline int
nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	if (time_before(jiffies,NFS_READTIME(inode)+NFS_ATTRTIMEO(inode)))
		return 0;
	return __nfs_revalidate_inode(server, inode);
}

static inline off_t
nfs_size_to_off_t(__u64 size)
{
	return (size > (__u64)LONG_MAX) ? (off_t)LONG_MAX : (off_t) size;
}

static inline ino_t
nfs_fileid_to_ino_t(u64 fileid)
{
	ino_t ino = (unsigned long) fileid;
	if (sizeof(unsigned long) < sizeof(u64))
		ino ^= fileid >> (sizeof(u64)-sizeof(unsigned long)) * 8;
	return ino;
}

static inline time_t
nfs_time_to_secs(__u64 time)
{
	return (time_t)(time >> 32);
}

static __inline__ struct rpc_cred *
nfs_file_cred(struct file *file)
{
	return (struct rpc_cred *)(file->private_data);
}

/* NFS root */

extern int nfs_root_mount(struct super_block *sb);

#define nfs_wait_event(clnt, wq, condition)				\
({									\
	int __retval = 0;						\
	if (clnt->cl_intr) {						\
		sigset_t oldmask;					\
		rpc_clnt_sigmask(clnt, &oldmask);			\
		__retval = wait_event_interruptible(wq, condition);	\
		rpc_clnt_sigunmask(clnt, &oldmask);			\
	} else								\
		wait_event(wq, condition);				\
	__retval;							\
})

#endif /* __KERNEL__ */

/*
 * NFS debug flags
 */
#define NFSDBG_VFS		0x0001
#define NFSDBG_DIRCACHE		0x0002
#define NFSDBG_LOOKUPCACHE	0x0004
#define NFSDBG_PAGECACHE	0x0008
#define NFSDBG_PROC		0x0010
#define NFSDBG_XDR		0x0020
#define NFSDBG_FILE		0x0040
#define NFSDBG_ROOT		0x0080
#define NFSDBG_PARANOID		0x0100
#define NFSDBG_ALL		0xFFFF

#ifdef __KERNEL__
# undef ifdebug
# ifdef NFS_DEBUG
#  define ifdebug(fac)		if (nfs_debug & NFSDBG_##fac)
# else
#  define ifdebug(fac)		if (0)
# endif
# ifdef NFS_PARANOIA
#  define nfsparanoid(args...)	dfprintk(PARANOID, ##args)
# else
#  define nfsparanoid(args...)	do { } while (0)
# endif
#endif /* __KERNEL */

#endif
