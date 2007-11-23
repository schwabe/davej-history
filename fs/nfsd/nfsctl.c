/*
 * linux/fs/nfsd/nfsctl.c
 *
 * Syscall interface to knfsd.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */
#define NFS_GETFH_NEW

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/unistd.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>

#include <linux/nfs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/lockd/syscall.h>

#if LINUX_VERSION_CODE >= 0x020100
#include <asm/uaccess.h>
#else
# define copy_from_user		memcpy_fromfs
# define copy_to_user		memcpy_tofs
# define access_ok		!verify_area
#endif
#include <linux/smp.h>
#include <linux/smp_lock.h>

extern long sys_call_table[];

static int	nfsctl_svc(struct nfsctl_svc *data);
static int	nfsctl_addclient(struct nfsctl_client *data);
static int	nfsctl_delclient(struct nfsctl_client *data);
static int	nfsctl_export(struct nfsctl_export *data);
static int	nfsctl_unexport(struct nfsctl_export *data);
static int	nfsctl_getfh(struct nfsctl_fhparm *, struct knfs_fh *);
static int	nfsctl_getfd(struct nfsctl_fdparm *, struct knfs_fh *);
/* static int	nfsctl_ugidupdate(struct nfsctl_ugidmap *data); */

static int	initialized = 0;

int exp_procfs_exports(char *buffer, char **start, off_t offset,
                             int length, int *eof, void *data);

struct long_maxmin
{
	long *value;
	long min_value;
	long max_value;
};

/* In seconds. */
#define TIME_DIFF_MARGIN	10
#define MIN_TIME_DIFF_MARGIN	0
#define MAX_TIME_DIFF_MARGIN	600

long nfsd_time_diff_margin = TIME_DIFF_MARGIN;

static struct long_maxmin time_diff_margin =
{
	&nfsd_time_diff_margin,
	MIN_TIME_DIFF_MARGIN,
	MAX_TIME_DIFF_MARGIN
};

static int
nfsd_proc_read_long_maxmin(char *buffer, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	int len;
	struct long_maxmin *x = (struct long_maxmin *) data;
	len = sprintf(buffer, "%ld\n", *x->value) - offset;
	if (len < length) {
		*eof = 1;
		if (len <= 0)
			 return 0;
	} else
		len = length;
	*start = buffer + offset;
	return len;
}

static int
nfsd_proc_write_long_maxmin(struct file *file, const char *buffer,
			   unsigned long count, void *data)
{
	long v;
	struct long_maxmin *x = (struct long_maxmin *) data;
	v = simple_strtoul(buffer, NULL, 10);
	if (v < x->min_value || v > x->max_value)
		return -EINVAL;
	*x->value = v;
	return count;
}

static void nfsd_proc_init(void)
{
	struct proc_dir_entry *nfsd_ent = NULL;

	if (!(nfsd_ent = create_proc_entry("fs/nfs", S_IFDIR, 0)))
		return;
	if (!(nfsd_ent = create_proc_entry("fs/nfs/exports", 0, 0)))
		return;
	nfsd_ent->read_proc = exp_procfs_exports;
	if (!(nfsd_ent = create_proc_entry("fs/nfs/time-diff-margin",
					   S_IFREG|S_IRUGO|S_IWUSR, 0)))
		return;
	nfsd_ent->read_proc = nfsd_proc_read_long_maxmin;
	nfsd_ent->write_proc = nfsd_proc_write_long_maxmin;
	nfsd_ent->data = (void *) &time_diff_margin;
}


/*
 * Initialize nfsd
 */
static void
nfsd_init(void)
{
	nfsd_stat_init();	/* Statistics */
	nfsd_cache_init();	/* RPC reply cache */
	nfsd_export_init();	/* Exports table */
	nfsd_lockd_init();	/* lockd->nfsd callbacks */
	nfsd_proc_init();
	initialized = 1;
}

static inline int
nfsctl_svc(struct nfsctl_svc *data)
{
	return nfsd_svc(data->svc_port, data->svc_nthreads);
}

static inline int
nfsctl_addclient(struct nfsctl_client *data)
{
	return exp_addclient(data);
}

static inline int
nfsctl_delclient(struct nfsctl_client *data)
{
	return exp_delclient(data);
}

static inline int
nfsctl_export(struct nfsctl_export *data)
{
	return exp_export(data);
}

static inline int
nfsctl_unexport(struct nfsctl_export *data)
{
	return exp_unexport(data);
}

#ifdef notyet
static inline int
nfsctl_ugidupdate(nfs_ugidmap *data)
{
	return -EINVAL;
}
#endif

static inline int
nfsctl_getfd(struct nfsctl_fdparm *data, struct knfs_fh *res)
{
	struct sockaddr_in	*sin;
	struct svc_client	*clp;
	int			err = 0;

	if (data->gd_addr.sa_family != AF_INET)
		return -EPROTONOSUPPORT;
	if (data->gd_version < 2 || data->gd_version > NFSSVC_MAXVERS)
		return -EINVAL;
	sin = (struct sockaddr_in *)&data->gd_addr;

	exp_readlock();
	if (!(clp = exp_getclient(sin)))
		err = -EPERM;
	else
		err = exp_rootfh(clp, 0, 0, data->gd_path, res);
	exp_unlock();

	return err;
}

static inline int
nfsctl_getfh(struct nfsctl_fhparm *data, struct knfs_fh *res)
{
	struct sockaddr_in	*sin;
	struct svc_client	*clp;
	int			err = 0;

	if (data->gf_addr.sa_family != AF_INET)
		return -EPROTONOSUPPORT;
	if (data->gf_version < 2 || data->gf_version > NFSSVC_MAXVERS)
		return -EINVAL;
	sin = (struct sockaddr_in *)&data->gf_addr;

	exp_readlock();
	if (!(clp = exp_getclient(sin)))
		err = -EPERM;
	else
		err = exp_rootfh(clp, to_kdev_t(data->gf_dev), data->gf_ino, NULL, res);
	exp_unlock();

	return err;
}

#ifdef CONFIG_NFSD
#define handle_sys_nfsservctl sys_nfsservctl
#endif

int
asmlinkage handle_sys_nfsservctl(int cmd, void *opaque_argp, void *opaque_resp)
{
	struct nfsctl_arg *	argp = opaque_argp;
	union nfsctl_res *	resp = opaque_resp;
	struct nfsctl_arg *	arg = NULL;
	union nfsctl_res *	res = NULL;
	int			err;

	MOD_INC_USE_COUNT;
	lock_kernel ();
	if (!initialized)
		nfsd_init();
	err = -EPERM;
	if (!capable(CAP_SYS_ADMIN)) {
		goto done;
	}
	err = -EFAULT;
	if (!access_ok(VERIFY_READ, argp, sizeof(*argp))
	 || (resp && !access_ok(VERIFY_WRITE, resp, sizeof(*resp)))) {
		goto done;
	}

	err = -ENOMEM;	/* ??? */
	if (!(arg = kmalloc(sizeof(*arg), GFP_USER)) ||
	    (resp && !(res = kmalloc(sizeof(*res), GFP_USER)))) {
		goto done;
	}

	err = -EINVAL;
	copy_from_user(arg, argp, sizeof(*argp));
	if (arg->ca_version != NFSCTL_VERSION) {
		printk(KERN_WARNING "nfsd: incompatible version in syscall.\n");
		goto done;
	}

	if (cmd >= NFSCTL_LOCKD) {
		err = lockdctl(cmd, argp, resp);
		goto done;
	}

	switch(cmd) {
	case NFSCTL_SVC:
		err = nfsctl_svc(&arg->ca_svc);
		break;
	case NFSCTL_ADDCLIENT:
		err = nfsctl_addclient(&arg->ca_client);
		break;
	case NFSCTL_DELCLIENT:
		err = nfsctl_delclient(&arg->ca_client);
		break;
	case NFSCTL_EXPORT:
		err = nfsctl_export(&arg->ca_export);
		break;
	case NFSCTL_UNEXPORT:
		err = nfsctl_unexport(&arg->ca_export);
		break;
#ifdef notyet
	case NFSCTL_UGIDUPDATE:
		err = nfsctl_ugidupdate(&arg->ca_umap);
		break;
#endif
	case NFSCTL_GETFH:
		err = nfsctl_getfh(&arg->ca_getfh, &res->cr_getfh);
		break;
	case NFSCTL_GETFD:
		err = nfsctl_getfd(&arg->ca_getfd, &res->cr_getfh);
		break;
	default:
		err = -EINVAL;
	}

	if (!err && resp)
		copy_to_user(resp, res, sizeof(*resp));

done:
	if (arg)
		kfree(arg);
	if (res)
		kfree(res);

	unlock_kernel ();
	MOD_DEC_USE_COUNT;
	return err;
}

#ifdef MODULE
/* New-style module support since 2.1.18 */
#if LINUX_VERSION_CODE >= 131346
EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
#endif

extern int (*do_nfsservctl)(int, void *, void *);

/*
 * This is called as the fill_inode function when an inode
 * is going into (fill = 1) or out of service (fill = 0).
 *
 * We use it here to make sure the module can't be unloaded
 * while a /proc inode is in use.
 */
void nfsd_modcount(struct inode *inode, int fill)
{
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}

/*
 * Initialize the module
 */
int
init_module(void)
{
	printk(KERN_INFO "Installing knfsd (copyright (C) 1996 okir@monad.swb.de)\n");
	do_nfsservctl = handle_sys_nfsservctl;
	return 0;
}

/*
 * Clean up the mess before unloading the module
 */
void
cleanup_module(void)
{
	if (MOD_IN_USE) {
		printk("nfsd: nfsd busy, remove delayed\n");
		return;
	}
	do_nfsservctl = NULL;
	nfsd_export_shutdown();
	nfsd_cache_shutdown();
	remove_proc_entry("fs/nfs/time-diff-margin", NULL);
	remove_proc_entry("fs/nfs/exports", NULL);
	remove_proc_entry("fs/nfs", NULL);
	nfsd_stat_shutdown();
	nfsd_lockd_shutdown();
}
#endif
