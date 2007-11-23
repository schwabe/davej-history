/*
 * linux/fs/nfsd/nfsfh.c
 *
 * NFS server file handle treatment.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 * Portions Copyright (C) 1999 G. Allen Morris III <gam3@acm.org>
 */

#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/dcache.h>
#include <asm/pgtable.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>

#define NFSDDBG_FACILITY		NFSDDBG_FH
#define NFSD_PARANOIA 1
/* #define NFSD_DEBUG_VERBOSE 1 */
/* #define NFSD_DEBUG_VERY_VERBOSE 1 */

extern unsigned long max_mapnr;

#define NFSD_FILE_CACHE 0
#define NFSD_DIR_CACHE  1
struct fh_entry {
	struct dentry * dentry;
	unsigned long reftime;
	ino_t	ino;
	kdev_t	dev;
};

#define NFSD_MAXFH \
  (((nfsd_nservers + 1) >> 1) * PAGE_SIZE/sizeof(struct fh_entry))
static struct fh_entry *filetable = NULL;
static struct fh_entry *dirstable = NULL;

static int nfsd_nr_verified = 0;
static int nfsd_nr_put = 0;
static unsigned long nfsd_next_expire = 0;

static int add_to_fhcache(struct dentry *, int);
struct dentry * lookup_inode(kdev_t, ino_t, ino_t);

static LIST_HEAD(fixup_head);
static LIST_HEAD(path_inuse);
static int nfsd_nr_fixups = 0;
static int nfsd_nr_paths = 0;
#define NFSD_MAX_PATHS 500
#define NFSD_MAX_FIXUPS 500
#define NFSD_MAX_FIXUP_AGE 30*HZ

struct nfsd_fixup {
	struct list_head lru;
	unsigned long reftime;
	ino_t	dirino;
	ino_t	ino;
	kdev_t	dev;
	ino_t	new_dirino;
};

struct nfsd_path {
	struct list_head lru;
	unsigned long reftime;
	int	users;
	ino_t	ino;
	kdev_t	dev;
	char	name[1];
};

static struct nfsd_fixup *
find_cached_lookup(kdev_t dev, ino_t dirino, ino_t ino)
{
	struct list_head *tmp = fixup_head.next;

	for (; tmp != &fixup_head; tmp = tmp->next) {
		struct nfsd_fixup *fp;

		fp = list_entry(tmp, struct nfsd_fixup, lru);
#ifdef NFSD_DEBUG_VERY_VERBOSE
printk("fixup %lu %lu, %lu %lu %s %s\n",
        fp->ino, ino,
	fp->dirino, dirino,
	kdevname(fp->dev), kdevname(dev));
#endif
		if (fp->ino != ino)
			continue;
		if (fp->dirino != dirino)
			continue;
		if (fp->dev != dev)
			continue;
		fp->reftime = jiffies;	
		list_del(tmp);
		list_add(tmp, &fixup_head);
		return fp;
	}
	return NULL;
}

/*
 * Save the dirino from a rename.
 */
void
add_to_rename_cache(ino_t new_dirino,
                    kdev_t dev, ino_t dirino, ino_t ino)
{
	struct nfsd_fixup *fp;

	if (dirino == new_dirino)
		return;

	fp = find_cached_lookup(dev, 
				dirino,
				ino);
	if (fp) {
		fp->new_dirino = new_dirino;
		return;
	}

	/*
	 * Add a new entry. The small race here is unimportant:
	 * if another task adds the same lookup, both entries
	 * will be consistent.
	 */
	fp = kmalloc(sizeof(struct nfsd_fixup), GFP_KERNEL);
	if (fp) {
		fp->dirino = dirino;
		fp->ino = ino;
		fp->dev = dev;
		fp->new_dirino = new_dirino;
		list_add(&fp->lru, &fixup_head);
		nfsd_nr_fixups++;
	}
}

/*
 * Save the dentry pointer from a successful lookup.
 */

static void free_fixup_entry(struct nfsd_fixup *fp)
{
	list_del(&fp->lru);
#ifdef NFSD_DEBUG_VERY_VERBOSE
printk("free_rename_entry: %lu->%lu %lu/%s\n",
		fp->dirino,
		fp->new_dirino,
		fp->ino,
		kdevname(fp->dev),
		(jiffies - fp->reftime));
#endif
	kfree(fp);
	nfsd_nr_fixups--;
}

/*
 * Copy a dentry's path into the specified buffer.
 */
static int copy_path(char *buffer, struct dentry *dentry, int namelen)
{
	char *p, *b = buffer;
	int result = 0, totlen = 0, len; 

	while (1) {
		struct dentry *parent;
		dentry = dentry->d_covers;
		parent = dentry->d_parent;
		len = dentry->d_name.len;
		p = (char *) dentry->d_name.name + len;
		totlen += len;
		if (totlen > namelen)
			goto out;
		while (len--)
			*b++ = *(--p);
		if (dentry == parent)
			break;
		dentry = parent;
		totlen++;
		if (totlen > namelen)
			goto out;
		*b++ = '/';
	}
	*b = 0;

	/*
	 * Now reverse in place ...
	 */
	p = buffer;
	while (p < b) {
		char c = *(--b);
		*b = *p;
		*p++ = c;
	} 
	result = 1;
out:
	return result;
}

/*
 * Add a dentry's path to the path cache.
 */
static int add_to_path_cache(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct dentry *this;
	struct nfsd_path *new;
	int len, result = 0;

#ifdef NFSD_DEBUG_VERBOSE
printk("add_to_path_cache: caching %s/%s\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	/*
	 * Get the length of the full pathname.
	 */
restart:
	len = 0;
	this = dentry;
	while (1) {
		struct dentry *parent;
		this = this->d_covers;
		parent = this->d_parent;
		len += this->d_name.len;
		if (this == parent)
			break;
		this = parent;
		len++;
	}
	/*
	 * Allocate a structure to hold the path.
	 */
	new = kmalloc(sizeof(struct nfsd_path) + len, GFP_KERNEL);
	if (new) {
		new->users = 0;	
		new->reftime = jiffies;	
		new->ino = inode->i_ino;
		new->dev = inode->i_dev;
		result = copy_path(new->name, dentry, len);
		if (!result)
			goto retry;
		list_add(&new->lru, &path_inuse);
		nfsd_nr_paths++;
#ifdef NFSD_DEBUG_VERBOSE
printk("add_to_path_cache: added %s, paths=%d\n", new->name, nfsd_nr_paths);
#endif
	}
	return result;

	/*
	 * If the dentry's path length changed, just try again.
	 */
retry:
	kfree(new);
	printk(KERN_DEBUG "add_to_path_cache: path length changed, retrying\n");
	goto restart;
}

/*
 * Search for a path entry for the specified (dev, inode).
 */
static struct nfsd_path *get_path_entry(kdev_t dev, ino_t ino)
{
	struct nfsd_path *pe;
	struct list_head *tmp;

	for (tmp = path_inuse.next; tmp != &path_inuse; tmp = tmp->next) {
		pe = list_entry(tmp, struct nfsd_path, lru);
		if (pe->ino != ino)
			continue;
		if (pe->dev != dev)
			continue;
		list_del(tmp);
		list_add(tmp, &path_inuse);
		pe->users++;
		pe->reftime = jiffies;
#ifdef NFSD_PARANOIA
printk("get_path_entry: found %s for %s/%ld\n", pe->name, kdevname(dev), ino);
#endif
		return pe;
	}
	return NULL;
}

static void put_path(struct nfsd_path *pe)
{
	pe->users--;
}

static void free_path_entry(struct nfsd_path *pe)
{
	if (pe->users)
		printk(KERN_DEBUG "free_path_entry: %s in use, users=%d\n",
			pe->name, pe->users);
	list_del(&pe->lru);
	kfree(pe);
	nfsd_nr_paths--;
}

struct nfsd_getdents_callback {
	struct nfsd_dirent *dirent;
	ino_t dirino;		/* parent inode number */
	int found;		/* dirent inode matched? */
	int sequence;		/* sequence counter */
};

struct nfsd_dirent {
	ino_t ino;		/* preset to desired entry */
	int len;
	char name[256];
};

/*
 * A rather strange filldir function to capture the inode number
 * for the second entry (the parent inode) and the name matching
 * the specified inode number.
 */
static int filldir_one(void * __buf, const char * name, int len, 
			off_t pos, ino_t ino)
{
	struct nfsd_getdents_callback *buf = __buf;
	struct nfsd_dirent *dirent = buf->dirent;
	int result = 0;

	buf->sequence++;
#ifdef NFSD_DEBUG_VERY_VERBOSE
printk("filldir_one: seq=%d, ino=%lu, name=%s\n", buf->sequence, ino, name);
#endif
	if (buf->sequence == 2) {
		buf->dirino = ino;
		goto out;
	}
	if (dirent->ino == ino) {
		dirent->len = len;
		memcpy(dirent->name, name, len);
		dirent->name[len] = '\0';
		buf->found = 1;
		result = -1;
	}
out:
	return result;
}

/*
 * Read a directory and return the parent inode number and the name
 * of the specified entry. The dirent must be initialized with the
 * inode number of the desired entry.
 */
static int get_parent_ino(struct dentry *dentry, struct nfsd_dirent *dirent)
{
	struct inode *dir = dentry->d_inode;
	int error;
	struct file file;
	struct nfsd_getdents_callback buffer;

	error = -ENOTDIR;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;
	error = -EINVAL;
	if (!dir->i_op || !dir->i_op->default_file_ops)
		goto out;
	/*
	 * Open the directory ...
	 */
	error = init_private_file(&file, dentry, FMODE_READ);
	if (error)
		goto out;
	error = -EINVAL;
	if (!file.f_op->readdir)
		goto out_close;

	buffer.dirent = dirent;
	buffer.dirino = 0;
	buffer.found = 0;
	buffer.sequence = 0;
	while (1) {
		int old_seq = buffer.sequence;
		down(&dir->i_sem);
		error = file.f_op->readdir(&file, &buffer, filldir_one);
		up(&dir->i_sem);
		if (error < 0)
			break;

		error = 0;
		if (buffer.found)
			break;
		error = -ENOENT;
		if (old_seq == buffer.sequence)
			break;
	}
	dirent->ino = buffer.dirino;

out_close:
	if (file.f_op->release)
		file.f_op->release(dir, &file);
out:
	return error;
}

/*
 * Look up a dentry given inode and parent inode numbers.
 *
 * This relies on the ability of a Unix-like filesystem to return
 * the parent inode of a directory as the ".." (second) entry.
 *
 * This could be further optimized if we had an efficient way of
 * searching for a dentry given the inode: as we walk up the tree,
 * it's likely that a dentry exists before we reach the root.
 */
struct dentry * lookup_inode(kdev_t dev, ino_t dirino, ino_t ino)
{
	struct super_block *sb;
	struct dentry *root, *dentry, *result;
	struct inode *dir;
	char *name;
	unsigned long page;
	ino_t root_ino;
	int error;
	struct nfsd_dirent dirent;

	result = ERR_PTR(-ENOMEM);
	page = __get_free_page(GFP_KERNEL);
	if (!page)
		goto out;

	/*
	 * Get the root dentry for the device.
	 */
	result = ERR_PTR(-ENOENT);
	sb = get_super(dev);
	if (!sb)
		goto out_page;
	root = dget(sb->s_root);
	root_ino = root->d_inode->i_ino; /* usually 2 */

	name = (char *) page + PAGE_SIZE;
	*(--name) = 0;

	/*
	 * Walk up the tree to construct the name string.
	 * When we reach the root inode, look up the name
	 * relative to the root dentry.
	 */
	while (1) {
		if (ino == root_ino) {
			if (*name == '/')
				name++;
			/*
			 * Note: this dput()s the root dentry.
			 */
			result = lookup_dentry(name, root, 0);
			break;
		}

		result = ERR_PTR(-ENOENT);
		dir = iget_in_use(sb, dirino);
		if (!dir)
			goto out_root;
		dentry = d_alloc_root(dir, NULL);
		if (!dentry)
			goto out_iput;

		/*
		 * Get the name for this inode and the next parent inode.
		 */
		dirent.ino = ino;
		error = get_parent_ino(dentry, &dirent);
		result = ERR_PTR(error);
		dput(dentry);
		if (error)
			goto out_root;
		/*
		 * Prepend the name to the buffer.
		 */
		result = ERR_PTR(-ENAMETOOLONG);
		name -= (dirent.len + 1);
		if ((unsigned long) name <= page)
			goto out_root;
		memcpy(name + 1, dirent.name, dirent.len);
		*name = '/';

		/*
		 * Make sure we can't get caught in a loop ...
		 */
		if (dirino == dirent.ino && dirino != root_ino) {
			printk(KERN_DEBUG 
			       "lookup_inode: looping?? (ino=%ld, path=%s)\n",
				dirino, name);	
			goto out_root;
		}
		ino = dirino;
		dirino = dirent.ino;
	}

out_page:
	free_page(page);
out:
	return result;

	/*
	 * Error exits ...
	 */
out_iput:
	result = ERR_PTR(-ENOMEM);
	iput(dir);
out_root:
	dput(root);
	goto out_page;
}

/*
 * Find an entry in the cache matching the given dentry pointer.
 */
static struct fh_entry *find_fhe(struct dentry *dentry, int cache,
				struct fh_entry **empty)
{
	struct fh_entry *fhe;
	int i, found = (empty == NULL) ? 1 : 0;

	if (!dentry)
		goto out;

	fhe = (cache == NFSD_FILE_CACHE) ? &filetable[0] : &dirstable[0];
	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (fhe->dentry == dentry) {
			fhe->reftime = jiffies;
			return fhe;
		}
		if (!found && !fhe->dentry) {
			found = 1;
			*empty = fhe;
		}
	}
out:
	return NULL;
}

/*
 * Expire a cache entry.
 */
static void expire_fhe(struct fh_entry *empty, int cache)
{
	struct dentry *dentry = empty->dentry;

#ifdef NFSD_DEBUG_VERBOSE
printk("expire_fhe: expiring %s %s/%s, d_count=%d, ino=%lu\n",
(cache == NFSD_FILE_CACHE) ? "file" : "dir",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count,empty->ino);
#endif
	empty->dentry = NULL;	/* no dentry */
	/*
	 * Add the parent to the dir cache before releasing the dentry,
	 * and check whether to save a copy of the dentry's path.
	 */
	if (dentry != dentry->d_parent) {
		struct dentry *parent = dget(dentry->d_parent);
		if (add_to_fhcache(parent, NFSD_DIR_CACHE))
			nfsd_nr_verified++;
		else
			dput(parent);
		/*
		 * If we're expiring a directory, copy its path.
		 */
		if (cache == NFSD_DIR_CACHE) {
			add_to_path_cache(dentry);
		}
	}
	dput(dentry);
	nfsd_nr_put++;
}

/*
 * Look for an empty slot, or select one to expire.
 */
static void expire_slot(int cache)
{
	struct fh_entry *fhe, *empty = NULL;
	unsigned long oldest = -1;
	int i;

	fhe = (cache == NFSD_FILE_CACHE) ? &filetable[0] : &dirstable[0];
	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (!fhe->dentry)
			goto out;
		if (fhe->reftime < oldest) {
			oldest = fhe->reftime;
			empty = fhe;
		}
	}
	if (empty)
		expire_fhe(empty, cache);

out:
	return;
}

/*
 * Expire any cache entries older than a certain age.
 */
static void expire_old(int cache, int age)
{
	struct fh_entry *fhe;
	int i;

#ifdef NFSD_DEBUG_VERY_VERBOSE
printk("expire_old: expiring %s older than %d\n",
(cache == NFSD_FILE_CACHE) ? "file" : "dir", age);
#endif
	fhe = (cache == NFSD_FILE_CACHE) ? &filetable[0] : &dirstable[0];
	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (!fhe->dentry)
			continue;
		if ((jiffies - fhe->reftime) > age)
			expire_fhe(fhe, cache);
	}

	/*
	 * Trim the fixup cache ...
	 */
	while (nfsd_nr_fixups > NFSD_MAX_FIXUPS) {
		struct nfsd_fixup *fp;
		fp = list_entry(fixup_head.prev, struct nfsd_fixup, lru);
		if ((jiffies - fp->reftime) < NFSD_MAX_FIXUP_AGE)
			break;
		free_fixup_entry(fp);
	}

	/*
	 * Trim the path cache ...
	 */
	while (nfsd_nr_paths > NFSD_MAX_PATHS) {
		struct nfsd_path *pe;
		pe = list_entry(path_inuse.prev, struct nfsd_path, lru);
		if (pe->users)
			break;
		free_path_entry(pe);
	}
}

/*
 * Add a dentry to the file or dir cache.
 *
 * Note: As NFS file handles must have an inode, we don't accept
 * negative dentries.
 */
static int add_to_fhcache(struct dentry *dentry, int cache)
{
	struct fh_entry *fhe, *empty = NULL;
	struct inode *inode = dentry->d_inode;

	if (!inode) {
#ifdef NFSD_PARANOIA
printk("add_to_fhcache: %s/%s rejected, no inode!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
		return 0;
	}

repeat:
	fhe = find_fhe(dentry, cache, &empty);
	if (fhe) {
		return 0;
	}

	/*
	 * Not found ... make a new entry.
	 */
	if (empty) {
		empty->dentry = dentry;
		empty->reftime = jiffies;
		empty->ino = inode->i_ino;
		empty->dev = inode->i_dev;
		return 1;
	}

	expire_slot(cache);
	goto repeat;
}

/*
 * Find an entry in the dir cache for the specified inode number.
 */
static struct fh_entry *find_fhe_by_ino(kdev_t dev, ino_t ino)
{
	struct fh_entry * fhe = &dirstable[0];
	int i;

	for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
		if (fhe->ino == ino && fhe->dev == dev) {
			fhe->reftime = jiffies;
			return fhe;
		}
	}
	return NULL;
}

/*
 * Find the (directory) dentry with the specified (dev, inode) number.
 * Note: this leaves the dentry in the cache.
 */
static struct dentry *find_dentry_by_ino(kdev_t dev, ino_t ino)
{
	struct fh_entry *fhe;
	struct nfsd_path *pe;
	struct dentry * dentry;

#ifdef NFSD_DEBUG_VERBOSE
printk("find_dentry_by_ino: looking for inode %ld\n", ino);
#endif
	/*
	 * Special case: inode number 2 is the root inode,
	 * so we can use the root dentry for the device.
	 */
	if (ino == 2) {
		struct super_block *sb = get_super(dev);
		if (sb) {
#ifdef NFSD_PARANOIA
printk("find_dentry_by_ino: getting root dentry for %s\n", kdevname(dev));
#endif
			if (sb->s_root) {
				dentry = dget(sb->s_root);
				goto out;
			} else {
#ifdef NFSD_PARANOIA
				printk("find_dentry_by_ino: %s has no root??\n",
					kdevname(dev));
#endif
			}
		}
	}

	/*
	 * Search the dentry cache ...
	 */
	fhe = find_fhe_by_ino(dev, ino);
	if (fhe) {
		dentry = dget(fhe->dentry);
		goto out;
	}
	/*
	 * Search the path cache ...
	 */
	dentry = NULL;
	pe = get_path_entry(dev, ino);
	if (pe) {
		struct dentry *res;
		res = lookup_dentry(pe->name, NULL, 0);
		if (!IS_ERR(res)) {
			struct inode *inode = res->d_inode;
			if (inode && inode->i_ino == ino &&
				     inode->i_dev == dev) {
				dentry = res;
#ifdef NFSD_PARANOIA
printk("find_dentry_by_ino: found %s/%s, ino=%ld\n",
dentry->d_parent->d_name.name, dentry->d_name.name, ino);
#endif
				if (add_to_fhcache(dentry, NFSD_DIR_CACHE)) {
					dget(dentry);
					nfsd_nr_verified++;
				}
				put_path(pe);
			} else {
				dput(res);
				put_path(pe);
				/* We should delete it from the cache. */
				free_path_entry(pe);
			}
		} else {
#ifdef NFSD_PARANOIA
printk("find_dentry_by_ino: %s lookup failed\n", pe->name);
#endif
			put_path(pe);
			/* We should delete it from the cache. */
			free_path_entry(pe);
		}
	}
out:
	return dentry;
}

/*
 * Look for an entry in the file cache matching the dentry pointer,
 * and verify that the (dev, inode) numbers are correct. If found,
 * the entry is removed from the cache.
 */
static struct dentry *find_dentry_in_fhcache(struct knfs_fh *fh)
{
/* FIXME: this must use the dev/ino/dir_ino triple. */ 
#if 0
	struct fh_entry * fhe;

	fhe = find_fhe(fh->fh_dcookie, NFSD_FILE_CACHE, NULL);
	if (fhe) {
		struct dentry *parent, *dentry;
		struct inode *inode;

		dentry = fhe->dentry;
		inode = dentry->d_inode;

		if (!inode) {
#ifdef NFSD_PARANOIA
printk("find_dentry_in_fhcache: %s/%s has no inode!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
			goto out;
		}
		if (inode->i_ino != u32_to_ino_t(fh->fh_ino))
			goto out;
 		if (inode->i_dev != u32_to_kdev_t(fh->fh_dev))
			goto out;

		fhe->dentry = NULL;
		fhe->ino = 0;
		fhe->dev = 0;
		nfsd_nr_put++;
		/*
		 * Make sure the parent is in the dir cache ...
		 */
		parent = dget(dentry->d_parent);
		if (add_to_fhcache(parent, NFSD_DIR_CACHE))
			nfsd_nr_verified++;
		else
			dput(parent);
		return dentry;
	}
out:
#endif
	return NULL;
}

/*
 * Look for an entry in the parent directory with the specified
 * inode number.
 */
static struct dentry *lookup_by_inode(struct dentry *parent, ino_t ino)
{
	struct dentry *dentry;
	int error;
	struct nfsd_dirent dirent;

	/*
	 * Search the directory for the inode number.
	 */
	dirent.ino = ino;
	error = get_parent_ino(parent, &dirent);
	if (error) {
#ifdef NFSD_PARANOIA
printk("lookup_by_inode: ino %ld not found in %s\n", ino, parent->d_name.name);
#endif
		goto no_entry;
	}
#ifdef NFSD_PARANOIA
printk("lookup_by_inode: found %s\n", dirent.name);
#endif

	dentry = lookup_dentry(dirent.name, parent, 0);
	if (!IS_ERR(dentry)) {
		if (dentry->d_inode && dentry->d_inode->i_ino == ino)
			goto out;
#ifdef NFSD_PARANOIA
printk("lookup_by_inode: %s/%s inode mismatch??\n",
parent->d_name.name, dentry->d_name.name);
#endif
		dput(dentry);
	} else {
#ifdef NFSD_PARANOIA
printk("lookup_by_inode: %s lookup failed, error=%ld\n",
dirent.name, PTR_ERR(dentry));
#endif
	}

no_entry:
	dentry = NULL;
out:
	return dentry;
}

/*
 * Search the fix-up list for a dentry from a prior lookup.
 */
static ino_t nfsd_cached_lookup(struct knfs_fh *fh)
{
	struct nfsd_fixup *fp;

	fp = find_cached_lookup(u32_to_kdev_t(fh->fh_dev),
				u32_to_ino_t(fh->fh_dirino),
				u32_to_ino_t(fh->fh_ino));
	if (fp)
		return fp->new_dirino;
	return 0;
}

void
expire_all(void)
{
 	if (time_after_eq(jiffies, nfsd_next_expire)) {
 		expire_old(NFSD_FILE_CACHE,  5*HZ);
 		expire_old(NFSD_DIR_CACHE , 60*HZ);
 		nfsd_next_expire = jiffies + 5*HZ;
 	}
}

/* 
 * Free cache after unlink/rmdir.
 */
void
expire_by_dentry(struct dentry *dentry)
{
	struct fh_entry *fhe;

	fhe = find_fhe(dentry, NFSD_FILE_CACHE, NULL);
	if (fhe) {
		expire_fhe(fhe, NFSD_FILE_CACHE);
	}
	fhe = find_fhe(dentry, NFSD_DIR_CACHE, NULL);
	if (fhe) {
		expire_fhe(fhe, NFSD_DIR_CACHE);
	}
}

/*
 * The is the basic lookup mechanism for turning an NFS file handle 
 * into a dentry. There are several levels to the search:
 * (1) Look for the dentry pointer the short-term fhcache,
 *     and verify that it has the correct inode number.
 *
 * (2) Try to validate the dentry pointer in the file handle,
 *     and verify that it has the correct inode number. If this
 *     fails, check for a cached lookup in the fix-up list and
 *     repeat step (2) using the new dentry pointer.
 *
 * (3) Look up the dentry by using the inode and parent inode numbers
 *     to build the name string. This should succeed for any Unix-like
 *     filesystem.
 *
 * (4) Search for the parent dentry in the dir cache, and then
 *     look for the name matching the inode number.
 *
 * (5) The most general case ... search the whole volume for the inode.
 *
 * If successful, we return a dentry with the use count incremented.
 *
 * Note: steps (4) and (5) above are probably unnecessary now that (3)
 * is working. Remove the code once this is verified ...
 */
static struct dentry *
find_fh_dentry(struct knfs_fh *fh)
{
	struct super_block *sb;
	struct dentry *dentry, *parent;
	struct inode * inode;
	struct list_head *lst;
	int looked_up = 0, retry = 0;
	ino_t dirino;

	/*
	 * Stage 1: Look for the dentry in the short-term fhcache.
	 */
	dentry = find_dentry_in_fhcache(fh);
	if (dentry) {
		nfsdstats.fh_cached++;
		goto out;
	}
	/*
	 * Stage 2: Attempt to find the inode.
	 */
	sb = get_super(fh->fh_dev);
	if (NULL == sb) {
		printk("find_fh_dentry: No SuperBlock for device %s.",
		       kdevname(fh->fh_dev));
		dentry = NULL;
		goto out;
	}

	dirino = u32_to_ino_t(fh->fh_dirino);
	inode = iget_in_use(sb, fh->fh_ino);
	if (!inode) {
		dprintk("find_fh_dentry: No inode found.\n");
		goto out_five;
	}
	goto check;
recheck:
	if (!inode) {
		dprintk("find_fh_dentry: No inode found.\n");
		goto out_three;
	}
check:
	for (lst = inode->i_dentry.next;
	     lst != &inode->i_dentry;
	     lst = lst->next) {
		dentry = list_entry(lst, struct dentry, d_alias);

/* if we are looking up a directory then we don't need the parent! */
		if (!dentry ||
		    !dentry->d_parent ||
		    !dentry->d_parent->d_inode) {
printk("find_fh_dentry: Found a useless inode %lu\n", inode->i_ino);
			continue;
		}
		if (dentry->d_parent->d_inode->i_ino != dirino)
			continue;

		dget(dentry);
		iput(inode);
#ifdef NFSD_DEBUG_VERBOSE
		printk("find_fh_dentry: Found%s %s/%s filehandle dirino = %lu, %lu\n",
		       retry ? " Renamed" : "",
		       dentry->d_parent->d_name.name,
		       dentry->d_name.name,
		       dentry->d_parent->d_inode->i_ino,
		       dirino);
#endif
		goto out;
	} /* for inode->i_dentry */

	/*
	 * Before proceeding to a lookup, check for a rename
	 */
	if (!retry && (dirino = nfsd_cached_lookup(fh))) {
		dprintk("find_fh_dentry: retry with %lu\n", dirino);
		retry = 1;
		goto recheck;
	}

	iput(inode);

	dprintk("find_fh_dentry: dirino not found %lu\n", dirino);

out_three:

	/*
	 * Stage 3: Look up the dentry based on the inode and parent inode
	 * numbers. This should work for all Unix-like filesystems.
	 */
	looked_up = 1;
	dentry = lookup_inode(u32_to_kdev_t(fh->fh_dev),
			      u32_to_ino_t(fh->fh_dirino),
			      u32_to_ino_t(fh->fh_ino));
	if (!IS_ERR(dentry)) {
		struct inode * inode = dentry->d_inode;
#ifdef NFSD_DEBUG_VERBOSE
printk("find_fh_dentry: looked up %s/%s\n",
       dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
		if (inode && inode->i_ino == u32_to_ino_t(fh->fh_ino)) {
			nfsdstats.fh_lookup++;
			goto out;
		}
#ifdef NFSD_PARANOIA
printk("find_fh_dentry: %s/%s lookup mismatch!\n",
       dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
		dput(dentry);
	}

	/*
	 * Stage 4: Look for the parent dentry in the fhcache ...
	 */
	parent = find_dentry_by_ino(u32_to_kdev_t(fh->fh_dev),
				    u32_to_ino_t(fh->fh_dirino));
	if (parent) {
		/*
		 * ... then search for the inode in the parent directory.
		 */
		dget(parent);
		dentry = lookup_by_inode(parent, u32_to_ino_t(fh->fh_ino));
		dput(parent);
		if (dentry)
			goto out;
	}

out_five:

	/*
	 * Stage 5: Search the whole volume, Yea Right.
	 */
#ifdef NFSD_PARANOIA
printk("find_fh_dentry: %s/%u dir/%u not found!\n",
       kdevname(u32_to_kdev_t(fh->fh_dev)), fh->fh_ino, fh->fh_dirino);
#endif
	dentry = NULL;
	nfsdstats.fh_stale++;
	
out:
	expire_all();
	return dentry;
}

/*
 * Perform sanity checks on the dentry in a client's file handle.
 *
 * Note that the file handle dentry may need to be freed even after
 * an error return.
 */
u32
fh_verify(struct svc_rqst *rqstp, struct svc_fh *fhp, int type, int access)
{
	struct knfs_fh	*fh = &fhp->fh_handle;
	struct svc_export *exp;
	struct dentry	*dentry;
	struct inode	*inode;
	u32		error = 0;

	dprintk("nfsd: fh_verify(exp %s/%u file (%s/%u dir %u)\n",
		kdevname(fh->fh_xdev),
		fh->fh_xino,
		kdevname(fh->fh_dev),
		fh->fh_ino,
		fh->fh_dirino);

	if (fhp->fh_dverified)
		goto check_type;
	/*
	 * Look up the export entry.
	 */
	error = nfserr_stale;
	exp = exp_get(rqstp->rq_client,
		      u32_to_kdev_t(fh->fh_xdev),
		      u32_to_ino_t(fh->fh_xino));
	if (!exp) {
		/* export entry revoked */
		nfsdstats.fh_stale++;
		goto out;
	}

	/* Check if the request originated from a secure port. */
	error = nfserr_perm;
	if (!rqstp->rq_secure && EX_SECURE(exp)) {
		printk(KERN_WARNING
			"nfsd: request from insecure port (%08x:%d)!\n",
				ntohl(rqstp->rq_addr.sin_addr.s_addr),
				ntohs(rqstp->rq_addr.sin_port));
		goto out;
	}

	/* Set user creds if we haven't done so already. */
	nfsd_setuser(rqstp, exp);

	/*
	 * Look up the dentry using the NFS file handle.
	 */
	error = nfserr_noent;
	dentry = find_fh_dentry(fh);
	if (!dentry) {
		goto out;
	}
	if (IS_ERR(dentry)) {
		error = nfserrno(-PTR_ERR(dentry));
		goto out;
	}

	/*
	 * Note:  it's possible the returned dentry won't be the one in the
	 * file handle.  We can correct the file handle for our use, but
	 * unfortunately the client will keep sending the broken one.  Let's
	 * hope the lookup will keep patching things up.
	 */
	fhp->fh_dentry = dentry;
	fhp->fh_export = exp;
	fhp->fh_dverified = 1;
	nfsd_nr_verified++;

	/* Type check. The correct error return for type mismatches
	 * does not seem to be generally agreed upon. SunOS seems to
	 * use EISDIR if file isn't S_IFREG; a comment in the NFSv3
	 * spec says this is incorrect (implementation notes for the
	 * write call).
	 */
check_type:
	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;
	error = nfserr_stale;
	/* On a heavily loaded SMP machine, more than one identical
	   requests may run at the same time on different processors.
	   One thread may get here with unfinished fh after another
	   thread just fetched the inode. It doesn't make any senses
	   to check fh->fh_generation here since it has not been set
	   yet. In that case, we shouldn't send back the stale
	   filehandle to the client. We use fh->fh_dcookie to indicate
	   if fh->fh_generation is set or not. If fh->fh_dcookie is
	   not set, don't return stale filehandle. */
	if (inode->i_generation != fh->fh_generation) {
		if (fh->fh_dcookie) {
			dprintk("fh_verify: Bad version %lu %u %u: 0x%x, 0x%x\n",
				inode->i_ino,
				inode->i_generation,
				fh->fh_generation,
				type, access);
			nfsdstats.fh_stale++;
			goto out;
		}
		else {
			/* We get here when inode is fetched by other
			   threads. We just use what is in there. */
			fh->fh_ino = ino_t_to_u32(inode->i_ino);
			fh->fh_generation = inode->i_generation;
			fh->fh_dcookie = (struct dentry *)0xfeebbaca;
			nfsdstats.fh_concurrent++;
		}
	}
	exp = fhp->fh_export;
	if (type > 0 && (inode->i_mode & S_IFMT) != type) {
		error = (type == S_IFDIR)? nfserr_notdir : nfserr_isdir;
		goto out;
	}
	if (type < 0 && (inode->i_mode & S_IFMT) == -type) {
		error = (type == -S_IFDIR)? nfserr_notdir : nfserr_isdir;
		goto out;
	}

	/*
	 * Security: Check that the export is valid for dentry <gam3@acm.org>
	 */
	error = 0;
	if (fh->fh_dev != fh->fh_xdev) {
		printk("fh_verify: Security: export on other device (%s, %s).\n",
		       kdevname(fh->fh_dev), kdevname(fh->fh_xdev));
		error = nfserr_stale;
		nfsdstats.fh_stale++;
	} else if (exp->ex_dentry != dentry) {
		struct dentry *tdentry = dentry;

		do {
			tdentry = tdentry->d_parent;
			if (exp->ex_dentry == tdentry)
				break;
			/* executable only by root and we can't be root */
			if (current->fsuid
			    && !(tdentry->d_inode->i_uid
			         && (tdentry->d_inode->i_mode & S_IXUSR))
			    && !(tdentry->d_inode->i_gid
				 && (tdentry->d_inode->i_mode & S_IXGRP))
			    && !(tdentry->d_inode->i_mode & S_IXOTH)
			    && (exp->ex_flags & NFSEXP_ROOTSQUASH)) {
				error = nfserr_stale;
				nfsdstats.fh_stale++;
dprintk("fh_verify: no root_squashed access.\n");
			}
		} while ((tdentry != tdentry->d_parent));
		if (exp->ex_dentry != tdentry) {
			error = nfserr_stale;
			nfsdstats.fh_stale++;
			printk("nfsd Security: %s/%s bad export.\n",
			       dentry->d_parent->d_name.name,
			       dentry->d_name.name);
			goto out;
		}
	}

	/* Finally, check access permissions. */
	if (!error) {
		error = nfsd_permission(exp, dentry, access);
	}
#ifdef NFSD_PARANOIA
if (error)
printk("fh_verify: %s/%s permission failure, acc=%x, error=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, access, (error >> 24));
#endif
out:
	return error;
}

/*
 * Compose a file handle for an NFS reply.
 *
 * Note that when first composed, the dentry may not yet have
 * an inode.  In this case a call to fh_update should be made
 * before the fh goes out on the wire ...
 */
void
fh_compose(struct svc_fh *fhp, struct svc_export *exp, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	struct dentry *parent = dentry->d_parent;

	dprintk("nfsd: fh_compose(exp %x/%ld %s/%s, ino=%ld)\n",
		exp->ex_dev, exp->ex_ino,
		parent->d_name.name, dentry->d_name.name,
		(inode ? inode->i_ino : 0));

	/*
	 * N.B. We shouldn't need to init the fh -- the call to fh_compose
	 * may not be done on error paths, but the cleanup must call fh_put.
	 * Fix this soon!
	 */
	if (fhp->fh_dverified || fhp->fh_locked || fhp->fh_dentry) {
		printk(KERN_ERR "fh_compose: fh %s/%s not initialized!\n",
			parent->d_name.name, dentry->d_name.name);
	}
	fh_init(fhp);

	fhp->fh_handle.fh_dcookie = dentry;
	if (inode) {
		fhp->fh_handle.fh_ino = ino_t_to_u32(inode->i_ino);
		fhp->fh_handle.fh_generation = inode->i_generation;
		fhp->fh_handle.fh_dcookie = (struct dentry *)0xfeebbaca;
	}
	fhp->fh_handle.fh_dirino = ino_t_to_u32(parent->d_inode->i_ino);
	fhp->fh_handle.fh_dev	 = kdev_t_to_u32(parent->d_inode->i_dev);
	fhp->fh_handle.fh_xdev	 = kdev_t_to_u32(exp->ex_dev);
	fhp->fh_handle.fh_xino	 = ino_t_to_u32(exp->ex_ino);

	fhp->fh_dentry = dentry; /* our internal copy */
	fhp->fh_export = exp;

	/* We stuck it there, we know it's good. */
	fhp->fh_dverified = 1;
	nfsd_nr_verified++;
}

/*
 * Update file handle information after changing a dentry.
 */
void
fh_update(struct svc_fh *fhp)
{
	struct dentry *dentry;
	struct inode *inode;

	if (!fhp->fh_dverified)
		goto out_bad;

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;
	if (!inode)
		goto out_negative;
	fhp->fh_handle.fh_ino = ino_t_to_u32(inode->i_ino);
	fhp->fh_handle.fh_generation = inode->i_generation;
	fhp->fh_handle.fh_dcookie = (struct dentry *)0xfeebbaca;
 out:
	return;

out_bad:
	printk(KERN_ERR "fh_update: fh not verified!\n");
	goto out;
out_negative:
	printk(KERN_ERR "fh_update: %s/%s still negative!\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	goto out;
}

/*
 * Release a file handle.  If the file handle carries a dentry count,
 * we add the dentry to the short-term cache rather than release it.
 */
void
fh_put(struct svc_fh *fhp)
{
	struct dentry * dentry = fhp->fh_dentry;
	if (fhp->fh_dverified) {
		fh_unlock(fhp);
		fhp->fh_dverified = 0;
		if (!dentry->d_count)
			goto out_bad;
		if (!dentry->d_inode || !add_to_fhcache(dentry, 0)) {
			dput(dentry);
			nfsd_nr_put++;
		}
	}
	return;

out_bad:
	printk(KERN_ERR "fh_put: %s/%s has d_count 0!\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	return;
}

/*
 * Flush any cached dentries for the specified device
 * or for all devices.
 *
 * This is called when revoking the last export for a
 * device, so that it can be unmounted cleanly.
 */
void nfsd_fh_flush(kdev_t dev)
{
	struct fh_entry *fhe;
	int i, pass = 2;

	fhe = &filetable[0];
	while (pass--) {
		for (i = 0; i < NFSD_MAXFH; i++, fhe++) {
			struct dentry *dentry = fhe->dentry;
			if (!dentry)
				continue;
			if (dev && dentry->d_inode->i_dev != dev)
				continue;
			fhe->dentry = NULL;
			dput(dentry);
			nfsd_nr_put++;
		}
		fhe = &dirstable[0];
	}
}

/*
 * Free the rename and path caches.
 */
void nfsd_fh_free(void)
{
	struct list_head *tmp;
	int i;

	/* Flush dentries for all devices */
	nfsd_fh_flush(0);

	/*
	 * N.B. write a destructor for these lists ...
	 */
	i = 0;
	while ((tmp = fixup_head.next) != &fixup_head) {
		struct nfsd_fixup *fp;
		fp = list_entry(tmp, struct nfsd_fixup, lru);
		free_fixup_entry(fp);
		i++;
	}
	printk(KERN_DEBUG "nfsd_fh_free: %d fixups freed\n", i);

	i = 0;
	while ((tmp = path_inuse.next) != &path_inuse) {
		struct nfsd_path *pe;
		pe = list_entry(tmp, struct nfsd_path, lru);
		free_path_entry(pe);
		i++;
	}
	printk(KERN_DEBUG "nfsd_fh_free: %d paths freed\n", i);

	printk(KERN_DEBUG "nfsd_fh_free: verified %d, put %d\n",
		nfsd_nr_verified, nfsd_nr_put);
}

void nfsd_fh_init(void)
{
	extern void __my_nfsfh_is_too_big(void); 

	if (filetable)
		return;

	/* Sanity check */ 
	if (sizeof(struct nfs_fhbase) > 32) 
		__my_nfsfh_is_too_big(); 

	filetable = kmalloc(sizeof(struct fh_entry) * NFSD_MAXFH,
			    GFP_KERNEL);
	dirstable = kmalloc(sizeof(struct fh_entry) * NFSD_MAXFH,
			    GFP_KERNEL);

	if (filetable == NULL || dirstable == NULL) {
		printk(KERN_WARNING "nfsd_fh_init : Could not allocate fhcache\n");
		nfsd_nservers = 0;
		return;
	}

	memset(filetable, 0, NFSD_MAXFH*sizeof(struct fh_entry));
	memset(dirstable, 0, NFSD_MAXFH*sizeof(struct fh_entry));
	INIT_LIST_HEAD(&path_inuse);
	INIT_LIST_HEAD(&fixup_head);

	printk(KERN_DEBUG 
		"nfsd_fh_init : initialized fhcache, entries=%lu\n", NFSD_MAXFH);
	/*
	 * Display a warning if the ino_t is larger than 32 bits.
	 */
	if (sizeof(ino_t) > sizeof(__u32))
		printk(KERN_INFO 
			"NFSD: ino_t is %d bytes, using lower 4 bytes\n",
			sizeof(ino_t));
}

void
nfsd_fh_shutdown(void)
{
	if (!filetable)
		return;
	printk(KERN_DEBUG 
		"nfsd_fh_shutdown : freeing %ld fhcache entries.\n", NFSD_MAXFH);
	kfree(filetable);
	kfree(dirstable);
	filetable = dirstable = NULL;
}
