/*
 *  linux/fs/proc/mem.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>

#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/pgtable.h>

/*
 * mem_write isn't really a good idea right now. It needs
 * to check a lot more: if the process we try to write to 
 * dies in the middle right now, mem_write will overwrite
 * kernel memory.. This disables it altogether.
 */
#define mem_write NULL

static int check_range(struct mm_struct * mm, unsigned long addr, int count)
{
	struct vm_area_struct *vma;
	int retval;

	vma = find_vma(mm, addr);
	if (!vma)
		return -EACCES;
	if (vma->vm_start > addr)
		return -EACCES;
	if (!(vma->vm_flags & VM_READ))
		return -EACCES;
	while ((retval = vma->vm_end - addr) < count) {
		struct vm_area_struct *next = vma->vm_next;
		if (!next)
			break;
		if (vma->vm_end != next->vm_start)
			break;
		if (!(next->vm_flags & VM_READ))
			break;
		vma = next;
	}
	if (retval > count)
		retval = count;
	return retval;
}

static struct task_struct * get_task(int pid)
{
	struct task_struct * tsk = current;

	if (pid != tsk->pid) {
		tsk = find_task_by_pid(pid);

		/* Allow accesses only under the same circumstances
		 * that we would allow ptrace to work.
		 */
		if (tsk) {
			if (!(tsk->ptrace & PT_PTRACED)
			    || tsk->state != TASK_STOPPED
			    || tsk->p_pptr != current)
				tsk = NULL;
		}
	}
	return tsk;
}

static ssize_t mem_read(struct file * file, char * buf,
			size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t pte;
	char * page;
	struct task_struct * tsk;
	unsigned long addr;
	char *tmp;
	ssize_t scount, i;

	read_lock(&tasklist_lock);
	tsk = get_task(inode->i_ino >> 16);
	read_unlock(&tasklist_lock);	/* FIXME: This should really be done only afetr not using tsk any more!!! */
	if (!tsk)
		return -ESRCH;
	addr = *ppos;
	scount = check_range(tsk->mm, addr, count);
	if (scount < 0)
		return scount;
	tmp = buf;
	while (scount > 0) {
		if (signal_pending(current))
			break;
		page_dir = pgd_offset(tsk->mm,addr);
		if (pgd_none(*page_dir))
			break;
		if (pgd_bad(*page_dir)) {
			printk("Bad page dir entry %08lx\n", pgd_val(*page_dir));
			pgd_clear(page_dir);
			break;
		}
		page_middle = pmd_offset(page_dir,addr);
		if (pmd_none(*page_middle))
			break;
		if (pmd_bad(*page_middle)) {
			printk("Bad page middle entry %08lx\n", pmd_val(*page_middle));
			pmd_clear(page_middle);
			break;
		}
		pte = *pte_offset(page_middle,addr);
		if (!pte_present(pte))
			break;
		page = (char *) pte_page(pte) + (addr & ~PAGE_MASK);
		i = PAGE_SIZE-(addr & ~PAGE_MASK);
		if (i > scount)
			i = scount;
		copy_to_user(tmp, page, i);
		addr += i;
		tmp += i;
		scount -= i;
	}
	*ppos = addr;
	return tmp-buf;
}

#ifndef mem_write

static ssize_t mem_write(struct file * file, char * buf,
			 size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t pte;
	char * page;
	struct task_struct * tsk;
	unsigned long addr;
	char *tmp;
	long i;

	addr = *ppos;
	tsk = get_task(inode->i_ino >> 16);
	if (!tsk)
		return -ESRCH;
	tmp = buf;
	while (count > 0) {
		if (signal_pending(current))
			break;
		page_dir = pgd_offset(tsk,addr);
		if (pgd_none(*page_dir))
			break;
		if (pgd_bad(*page_dir)) {
			printk("Bad page dir entry %08lx\n", pgd_val(*page_dir));
			pgd_clear(page_dir);
			break;
		}
		page_middle = pmd_offset(page_dir,addr);
		if (pmd_none(*page_middle))
			break;
		if (pmd_bad(*page_middle)) {
			printk("Bad page middle entry %08lx\n", pmd_val(*page_middle));
			pmd_clear(page_middle);
			break;
		}
		pte = *pte_offset(page_middle,addr);
		if (!pte_present(pte))
			break;
		if (!pte_write(pte))
			break;
		page = (char *) pte_page(pte) + (addr & ~PAGE_MASK);
		i = PAGE_SIZE-(addr & ~PAGE_MASK);
		if (i > count)
			i = count;
		copy_from_user(page, tmp, i);
		addr += i;
		tmp += i;
		count -= i;
	}
	*ppos = addr;
	if (tmp != buf)
		return tmp-buf;
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

#endif

static long long mem_lseek(struct file * file, long long offset, int orig)
{
	switch (orig) {
		case 0:
			break;
		case 1:
			offset += file->f_pos;
			break;
		default:
			return -EINVAL;
	}
	if (offset < 0)
			return -EINVAL;
	file->f_pos = offset;
	return offset;
}

static struct file_operations proc_mem_operations = {
	mem_lseek,
	mem_read,
	mem_write,
	NULL,		/* mem_readdir */
	NULL,		/* mem_poll */
	NULL,		/* mem_ioctl */
	NULL,		/* mmap */
	NULL,		/* no special open code */
	NULL,		/* flush */
	NULL,		/* no special release code */
	NULL		/* can't fsync */
};

struct inode_operations proc_mem_inode_operations = {
	&proc_mem_operations,	/* default base directory file-ops */
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
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	proc_permission		/* permission */
};
