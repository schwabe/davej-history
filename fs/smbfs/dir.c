/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 * Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ctype.h>

#include <linux/smb_fs.h>
#include <linux/smbno.h>

#include "smb_debug.h"

#define SMBFS_MAX_AGE 5*HZ

static ssize_t smb_dir_read(struct file *, char *, size_t, loff_t *);
static int smb_readdir(struct file *, void *, filldir_t);
static int smb_dir_open(struct inode *, struct file *);

static struct dentry *smb_lookup(struct inode *, struct dentry *);
static int smb_create(struct inode *, struct dentry *, int);
static int smb_mkdir(struct inode *, struct dentry *, int);
static int smb_rmdir(struct inode *, struct dentry *);
static int smb_unlink(struct inode *, struct dentry *);
static int smb_rename(struct inode *, struct dentry *,
		      struct inode *, struct dentry *);

static struct file_operations smb_dir_operations =
{
	NULL,			/* lseek - default */
	smb_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	smb_readdir,		/* readdir */
	NULL,			/* poll - default */
	smb_ioctl,		/* ioctl */
	NULL,			/* mmap */
	smb_dir_open,		/* open(struct inode *, struct file *) */
	NULL,			/* flush */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

struct inode_operations smb_dir_inode_operations =
{
	&smb_dir_operations,	/* default directory file ops */
	smb_create,		/* create */
	smb_lookup,		/* lookup */
	NULL,			/* link */
	smb_unlink,		/* unlink */
	NULL,			/* symlink */
	smb_mkdir,		/* mkdir */
	smb_rmdir,		/* rmdir */
	NULL,			/* mknod */
	smb_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	smb_revalidate_inode,	/* revalidate */
};

static ssize_t
smb_dir_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	return -EISDIR;
}

static int 
smb_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *dir = dentry->d_inode;
	struct cache_head *cachep = NULL;
	int result;

	VERBOSE("reading %s/%s, f_pos=%d\n", DENTRY_PATH(dentry),
		(int) filp->f_pos);

	result = 0;
	switch ((unsigned int) filp->f_pos)
	{
	case 0:
		if (filldir(dirent, ".", 1, 0, dir->i_ino) < 0)
			goto out;
		filp->f_pos = 1;
	case 1:
		if (filldir(dirent, "..", 2, 1,
				dentry->d_parent->d_inode->i_ino) < 0)
			goto out;
		filp->f_pos = 2;
	}

	/*
	 * Make sure our inode is up-to-date.
	 */
	result = smb_revalidate_inode(dentry);
	if (result)
		goto out;
	/*
	 * Get the cache pointer ...
	 */
	result = -EIO;
	cachep = smb_get_dircache(dentry);
	if (!cachep)
		goto out;
	/*
	 * Make sure the cache is up-to-date.
	 *
	 * To detect changes on the server we refill on each "new" access.
	 *
	 * Directory mtime would be nice to use for finding changes,
	 * unfortunately some servers (NT4) doesn't update on local changes.
	 */
	if (!cachep->valid || filp->f_pos == 2)
	{
		result = smb_refill_dircache(cachep, dentry);
		if (result)
			goto out;
	}

	result = 0;

	while (1)
	{
		struct cache_dirent this_dirent, *entry = &this_dirent;

		if (!smb_find_in_cache(cachep, filp->f_pos, entry))
			break;
		/*
		 * Check whether to look up the inode number.
		 */
		if (!entry->ino) {
			struct qstr qname;
			/* N.B. Make cache_dirent name a qstr! */
			qname.name = entry->name;
			qname.len  = entry->len;
			entry->ino = find_inode_number(dentry, &qname);
			if (!entry->ino)
				entry->ino = smb_invent_inos(1);
		}

		if (filldir(dirent, entry->name, entry->len, 
				    filp->f_pos, entry->ino) < 0)
			break;
		filp->f_pos += 1;
	}

	/*
	 * Release the dircache.
	 */
out:
	if (cachep) {
		smb_free_dircache(cachep);
	}
	return result;
}

/*
 * Note: in order to allow the smbmount process to open the
 * mount point, we don't revalidate if conn_pid is NULL.
 */
static int
smb_dir_open(struct inode *dir, struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	struct smb_sb_info *server = server_from_dentry(dentry);
	int error = 0;

	VERBOSE("(%s/%s)\n", dentry->d_parent->d_name.name, 
		file->f_dentry->d_name.name);
	if (server->conn_pid)
		error = smb_revalidate_inode(dentry);
	return error;
}

/*
 * Dentry operations routines
 */
static int smb_lookup_validate(struct dentry *, int);
static int smb_hash_dentry(struct dentry *, struct qstr *);
static int smb_compare_dentry(struct dentry *, struct qstr *, struct qstr *);
static void smb_delete_dentry(struct dentry *);

static struct dentry_operations smbfs_dentry_operations =
{
	d_revalidate:	smb_lookup_validate,
	d_hash:		smb_hash_dentry,
	d_compare:	smb_compare_dentry,
	d_delete:	smb_delete_dentry
};

/*
 * This is the callback when the dcache has a lookup hit.
 */
static int
smb_lookup_validate(struct dentry * dentry, int flags)
{
	struct inode * inode = dentry->d_inode;
	unsigned long age = jiffies - dentry->d_time;
	int valid;

	/*
	 * The default validation is based on dentry age:
	 * we believe in dentries for 5 seconds.  (But each
	 * successful server lookup renews the timestamp.)
	 */
	valid = (age <= SMBFS_MAX_AGE);
#ifdef SMBFS_DEBUG_VERBOSE
	if (!valid)
		VERBOSE("%s/%s not valid, age=%lu\n", DENTRY_PATH(dentry), age);
#endif

	if (inode) {
		if (is_bad_inode(inode)) {
			PARANOIA("%s/%s has dud inode\n", DENTRY_PATH(dentry));
			valid = 0;
		} else if (!valid)
			valid = (smb_revalidate_inode(dentry) == 0);
	} else {
		/*
		 * What should we do for negative dentries?
		 */
	}
	return valid;
}

static int 
smb_hash_dentry(struct dentry *dir, struct qstr *this)
{
	unsigned long hash;
	int i;

	hash = init_name_hash();
	for (i=0; i < this->len ; i++)
		hash = partial_name_hash(tolower(this->name[i]), hash);
	this->hash = end_name_hash(hash);
  
	return 0;
}

static int
smb_compare_dentry(struct dentry *dir, struct qstr *a, struct qstr *b)
{
	int i, result = 1;

	if (a->len != b->len)
		goto out;
	for (i=0; i < a->len; i++) {
		if (tolower(a->name[i]) != tolower(b->name[i]))
			goto out;
	}
	result = 0;
out:
	return result;
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad inodes and close files.
 */
static void
smb_delete_dentry(struct dentry * dentry)
{
	if (dentry->d_inode) {
		if (is_bad_inode(dentry->d_inode)) {
			PARANOIA("bad inode, unhashing %s/%s\n", 
				 DENTRY_PATH(dentry));
			d_drop(dentry);
		}
		smb_close_dentry(dentry);
	} else {
		/* N.B. Unhash negative dentries? */
	}
}

/*
 * Whenever a lookup succeeds, we know the parent directories
 * are all valid, so we want to update the dentry timestamps.
 * N.B. Move this to dcache?
 */
void
smb_renew_times(struct dentry * dentry)
{
	for (;;)
	{
		dentry->d_time = jiffies;
		if (dentry == dentry->d_parent)
			break;
		dentry = dentry->d_parent;
	}
}

static struct dentry *
smb_lookup(struct inode *dir, struct dentry *dentry)
{
	struct smb_fattr finfo;
	struct inode *inode;
	int error;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	error = smb_proc_getattr(dentry, &finfo);
#ifdef SMBFS_PARANOIA
	if (error && error != -ENOENT)
		PARANOIA("find %s/%s failed, error=%d\n",
			 DENTRY_PATH(dentry), error);
#endif

	inode = NULL;
	if (error == -ENOENT)
		goto add_entry;
	if (!error)
	{
		error = -EACCES;
		finfo.f_ino = smb_invent_inos(1);
		inode = smb_iget(dir->i_sb, &finfo);
		if (inode)
		{
	add_entry:
			dentry->d_op = &smbfs_dentry_operations;
			d_add(dentry, inode);
			smb_renew_times(dentry);
			error = 0;
		}
	}
out:
	return ERR_PTR(error);
}

/*
 * This code is common to all routines creating a new inode.
 */
static int
smb_instantiate(struct dentry *dentry, __u16 fileid, int have_id)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	struct inode *inode;
	int error;
	struct smb_fattr fattr;

	VERBOSE("file %s/%s, fileid=%u\n", DENTRY_PATH(dentry), fileid);
	error = smb_proc_getattr(dentry, &fattr);
	if (error)
		goto out_close;

	smb_renew_times(dentry);
	fattr.f_ino = smb_invent_inos(1);
	inode = smb_iget(dentry->d_sb, &fattr);
	if (!inode)
		goto out_no_inode;

	if (have_id)
	{
		inode->u.smbfs_i.fileid = fileid;
		inode->u.smbfs_i.access = SMB_O_RDWR;
		inode->u.smbfs_i.open = server->generation;
	}
	d_instantiate(dentry, inode);
out:
	return error;

out_no_inode:
	error = -EACCES;
out_close:
	if (have_id)
	{
		PARANOIA("%s/%s failed, error=%d, closing %u\n",
			 DENTRY_PATH(dentry), error, fileid);
		smb_close_fileid(dentry, fileid);
	}
	goto out;
}

/* N.B. How should the mode argument be used? */
static int
smb_create(struct inode *dir, struct dentry *dentry, int mode)
{
	__u16 fileid;
	int error;

	VERBOSE("creating %s/%s, mode=%d\n", DENTRY_PATH(dentry), mode);

	error = smb_proc_create(dentry, 0, CURRENT_TIME, &fileid);
	if (!error) {
		error = smb_instantiate(dentry, fileid, 1);
	} else {
		PARANOIA("%s/%s failed, error=%d\n",
			 DENTRY_PATH(dentry), error);
	}
	return error;
}

/* N.B. How should the mode argument be used? */
static int
smb_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;

	error = smb_proc_mkdir(dentry);
	if (!error)
	{
		error = smb_instantiate(dentry, 0, 0);
	}
	return error;
}

static int
smb_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int error;

	/*
	 * Close the directory if it's open.
	 */
	smb_close(inode);

	/*
	 * Check that nobody else is using the directory..
	 */
	error = -EBUSY;
	if (!list_empty(&dentry->d_hash))
		goto out;

	error = smb_proc_rmdir(dentry);

out:
	return error;
}

static int
smb_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	/*
	 * Close the file if it's open.
	 */
	smb_close(dentry->d_inode);

	error = smb_proc_unlink(dentry);
	if (!error)
	{
		smb_renew_times(dentry);
		d_delete(dentry);
	}
	return error;
}

static int
smb_rename(struct inode *old_dir, struct dentry *old_dentry,
	   struct inode *new_dir, struct dentry *new_dentry)
{
	int error;

	/*
	 * Close any open files, and check whether to delete the
	 * target before attempting the rename.
	 */
	if (old_dentry->d_inode)
		smb_close(old_dentry->d_inode);
	if (new_dentry->d_inode)
	{
		smb_close(new_dentry->d_inode);
		error = smb_proc_unlink(new_dentry);
		if (error)
		{
			VERBOSE("unlink %s/%s, error=%d\n",
				DENTRY_PATH(new_dentry), error);
			goto out;
		}
		/* FIXME */
		d_delete(new_dentry);
	}

	error = smb_proc_mv(old_dentry, new_dentry);
	if (!error)
	{
		smb_renew_times(old_dentry);
		smb_renew_times(new_dentry);
	}
out:
	return error;
}
