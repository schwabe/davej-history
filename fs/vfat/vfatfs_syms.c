/*
 * linux/fs/msdos/vfatfs_syms.c
 *
 * Exported kernel symbols for the VFAT filesystem.
 * These symbols are used by dmsdos.
 */

#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))
#include <linux/version.h>
#include <linux/module.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

struct file_system_type vfat_fs_type = {
	vfat_read_super, "vfat", 1, NULL
};

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(2,1,0)
#define X(sym) EXPORT_SYMBOL(sym);
#define X_PUNCT ;
#else
#define X_PUNCT ,
static struct symbol_table vfat_syms = {
#include <linux/symtab_begin.h>
#endif
X(vfat_create) X_PUNCT
X(vfat_unlink) X_PUNCT
X(vfat_unlink_uvfat) X_PUNCT
X(vfat_mkdir) X_PUNCT
X(vfat_rmdir) X_PUNCT
X(vfat_rename) X_PUNCT
X(vfat_put_super) X_PUNCT
X(vfat_read_super) X_PUNCT
X(vfat_read_inode) X_PUNCT
X(vfat_lookup) X_PUNCT
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(2,1,0)
#include <linux/symtab_end.h>
};                                           
#endif

int init_vfat_fs(void)
{
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(2,1,0)
	return register_filesystem(&vfat_fs_type);
#else
	int status;

	if ((status = register_filesystem(&vfat_fs_type)) == 0)
		status = register_symtab(&vfat_syms);
	return status;
#endif
}

