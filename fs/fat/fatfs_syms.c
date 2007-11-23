/*
 * linux/fs/fat/fatfs_syms.c
 *
 * Exported kernel symbols for the low-level FAT-based fs support.
 *
 */
#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))
#include <linux/version.h>
#include <linux/module.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

#include "msbuffer.h"

extern struct file_operations fat_dir_operations;

#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(2,1,0)
#define X(sym) EXPORT_SYMBOL(sym);
#define X_PUNCT ;
#else
#define X_PUNCT ,
static struct symbol_table fat_syms = {
#include <linux/symtab_begin.h>
#endif
X(fat_add_cluster) X_PUNCT
X(fat_bmap) X_PUNCT
X(fat_brelse) X_PUNCT
X(fat_cache_inval_inode) X_PUNCT
X(fat_esc2uni) X_PUNCT
X(fat_date_unix2dos) X_PUNCT
X(fat_dir_operations) X_PUNCT
X(fat_file_read) X_PUNCT
X(fat_file_write) X_PUNCT
X(fat_fs_panic) X_PUNCT
X(fat_get_entry) X_PUNCT
X(fat_lock_creation) X_PUNCT
X(fat_mark_buffer_dirty) X_PUNCT
X(fat_mmap) X_PUNCT
X(fat_notify_change) X_PUNCT
X(fat_parent_ino) X_PUNCT
X(fat_put_inode) X_PUNCT
X(fat_put_super) X_PUNCT
X(fat_read_inode) X_PUNCT
X(fat_read_super) X_PUNCT
X(fat_readdirx) X_PUNCT
X(fat_readdir) X_PUNCT
X(fat_scan) X_PUNCT
X(fat_smap) X_PUNCT
X(fat_statfs) X_PUNCT
X(fat_truncate) X_PUNCT
X(fat_uni2esc) X_PUNCT
X(fat_unlock_creation) X_PUNCT
X(fat_write_inode) X_PUNCT
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(2,1,0)
#include <linux/symtab_end.h>
};                                           
#endif

int init_fat_fs(void)
{
#if LINUX_VERSION_CODE >= ASC_LINUX_VERSION(2,1,0)
	return 0;
#else
	return register_symtab(&fat_syms);
#endif
}
