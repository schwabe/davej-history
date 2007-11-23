/*
 *	linux/mm/remap.c
 *
 *	(C) Copyright 1996 Linus Torvalds
 */

#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/file.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

extern int vm_enough_memory(long pages);

#define PTE_TABLE_MASK ((PTRS_PER_PTE - 1) * sizeof(pte_t))
#define PMD_TABLE_MASK ((PTRS_PER_PMD - 1) * sizeof(pmd_t))

/* Idea borrowed from copy_page_range(), see mm/memory.c */

static int copy_page_tables(struct mm_struct * mm,
	unsigned long new_addr, unsigned long old_addr, unsigned long len)
{
	pgd_t * src_pgd, * dst_pgd;
	pmd_t * dst_pmd;
	register pte_t * dst_pte;
	unsigned long src_addr, dst_addr, end;

	dst_addr = (new_addr &= PAGE_MASK);
	end = (src_addr = (old_addr &= PAGE_MASK)) + len;
	src_pgd = pgd_offset(mm, src_addr);
	dst_pgd = pgd_offset(mm, dst_addr);
	if (!(dst_pmd = pmd_alloc(dst_pgd, dst_addr)) ||
	    !(dst_pte = pte_alloc(dst_pmd, dst_addr)))
		goto oops_we_failed;

	for (;; src_pgd++) {
		pmd_t * src_pmd;

		if (pgd_none(*src_pgd))
			goto skip_copy_pmd_range;
		if (pgd_bad(*src_pgd)) {
			printk(KERN_ERR "copy_page_tables: bad source pgd (%08lx)\n", pgd_val(*src_pgd));
			pgd_clear(src_pgd);
skip_copy_pmd_range:
			src_addr = (src_addr + PGDIR_SIZE) & PGDIR_MASK;
			if (src_addr >= end)
				return 0;
			dst_addr = new_addr + (src_addr - old_addr);
			dst_pgd = pgd_offset(mm, dst_addr);
			if (!(dst_pmd = pmd_alloc(dst_pgd, dst_addr)) ||
			    !(dst_pte = pte_alloc(dst_pmd, dst_addr)))
				goto oops_we_failed;
			continue;
		}
		src_pmd = pmd_offset(src_pgd, src_addr);
		do {
			register pte_t * src_pte;

			if (pmd_none(*src_pmd))
				goto skip_copy_pte_range;
			if (pmd_bad(*src_pmd)) {
				printk(KERN_ERR "copy_page_tables: bad source pmd (%08lx)\n", pmd_val(*src_pmd));
				pmd_clear(src_pmd);
skip_copy_pte_range:
				src_addr = (src_addr + PMD_SIZE) & PMD_MASK;
				if (src_addr >= end)
					return 0;
				dst_addr = new_addr + (src_addr - old_addr);
				dst_pgd = pgd_offset(mm, dst_addr);
				if (!(dst_pmd = pmd_alloc(dst_pgd, dst_addr)) ||
				    !(dst_pte = pte_alloc(dst_pmd, dst_addr)))
					goto oops_we_failed;
				continue;
			}
			src_pte = pte_offset(src_pmd, src_addr);
			do {
				register pte_t pte = *src_pte;

				if (!pte_none(pte)) {
					if (!pte_present(pte))
						swap_duplicate(pte_val(pte));
					else {
						register unsigned long page_nr =
						    (pte_val(pte) >> PAGE_SHIFT);

						if (!(page_nr >= max_mapnr || 
						    PageReserved(mem_map+page_nr)))
							atomic_inc(&mem_map[page_nr].count);
					}
					set_pte(dst_pte, pte);
				}
				if ((src_addr += PAGE_SIZE) >= end)
					return 0;
				dst_addr += PAGE_SIZE;
				if (!((unsigned long) ++dst_pte & PTE_TABLE_MASK)) {
					if (!((unsigned long) ++dst_pmd
					      & PMD_TABLE_MASK) &&
					    !(dst_pmd = pmd_alloc(++dst_pgd,
								  dst_addr)))
						goto oops_we_failed;
					if (!(dst_pte = pte_alloc(dst_pmd,
								  dst_addr)))
						goto oops_we_failed;
				}
			} while ((unsigned long) ++src_pte & PTE_TABLE_MASK);
		} while ((unsigned long) ++src_pmd & PMD_TABLE_MASK);
	}
	return 0;

	/*
	 * Ok, the copy failed because we didn't have enough pages for
	 * the new page table tree. This is unlikely, but we have to
	 * take the possibility into account. In that case we just remove
	 * all the fresh copies of pages.
	 */
oops_we_failed:
	flush_cache_range(mm, new_addr, new_addr + len);
	zap_page_range(mm, new_addr, len);
	flush_tlb_range(mm, new_addr, new_addr + len);
	return -ENOMEM;
}

static inline unsigned long move_vma(struct vm_area_struct * vma,
	unsigned long addr, unsigned long old_len, unsigned long new_len)
{
	struct vm_area_struct * new_vma;

	new_vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (new_vma) {
		unsigned long new_addr = get_unmapped_area(0, new_len);

		if (new_addr && !copy_page_tables(current->mm, new_addr, addr, old_len)) {
			unsigned long ret;

			*new_vma = *vma;
			new_vma->vm_start = new_addr;
			new_vma->vm_end = new_addr+new_len;
			new_vma->vm_offset = vma->vm_offset + (addr - vma->vm_start);
			if (new_vma->vm_file)
				new_vma->vm_file->f_count++;
			if (new_vma->vm_ops && new_vma->vm_ops->open)
				new_vma->vm_ops->open(new_vma);
			if ((ret = do_munmap(addr, old_len))) {
				if (new_vma->vm_ops && new_vma->vm_ops->close)
					new_vma->vm_ops->close(new_vma);
				if (new_vma->vm_file)
					fput(new_vma->vm_file);
				flush_cache_range(current->mm, new_addr, new_addr + old_len);
				zap_page_range(current->mm, new_addr, old_len);
				flush_tlb_range(current->mm, new_addr, new_addr + old_len);
				kmem_cache_free(vm_area_cachep, new_vma);
				return ret;
			}
			insert_vm_struct(current->mm, new_vma);
			merge_segments(current->mm, new_vma->vm_start, new_vma->vm_end);
			current->mm->total_vm += new_len >> PAGE_SHIFT;
			if (new_vma->vm_flags & VM_LOCKED) {
				current->mm->locked_vm += new_len >> PAGE_SHIFT;
				make_pages_present(new_vma->vm_start,
						   new_vma->vm_end);
			}
			return new_addr;
		}
		kmem_cache_free(vm_area_cachep, new_vma);
	}
	return -ENOMEM;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 */
asmlinkage unsigned long sys_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if ((addr & ~PAGE_MASK) || !(old_len = PAGE_ALIGN(old_len)) ||
	    old_len > TASK_SIZE || addr > TASK_SIZE - old_len ||
	    (new_len = PAGE_ALIGN(new_len)) > TASK_SIZE)
		goto out;

	/*
	 * Always allow a shrinking remap: that just unmaps
	 * the unnecessary pages..
	 */
	if (old_len > new_len) {
		if (!(ret = do_munmap(addr + new_len, old_len - new_len)))
			ret = addr;
		goto out;
	}

	if (old_len == new_len) {
		ret = addr;
		goto out;
	}

	/*
	 * Ok, we need to grow..
	 */
	ret = -EFAULT;
	vma = find_vma(current->mm, addr);
	if (!vma || vma->vm_start > addr)
		goto out;
	/* We can't remap across vm area boundaries */
	if (old_len > vma->vm_end - addr)
		goto out;
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked = current->mm->locked_vm << PAGE_SHIFT;
		locked += new_len - old_len;
		ret = -EAGAIN;
		if ((current->rlim[RLIMIT_MEMLOCK].rlim_cur < RLIM_INFINITY) &&
		   (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur))
			goto out;
	}
	ret = -ENOMEM;
	if ((current->rlim[RLIMIT_AS].rlim_cur < RLIM_INFINITY) &&
	    ((current->mm->total_vm << PAGE_SHIFT) + (new_len - old_len)
	    > current->rlim[RLIMIT_AS].rlim_cur))
		goto out;
	/* Private writable mapping? Check memory availability.. */
	if ((vma->vm_flags & (VM_SHARED | VM_WRITE)) == VM_WRITE &&
	    !(flags & MAP_NORESERVE)				 &&
	    !vm_enough_memory((new_len - old_len) >> PAGE_SHIFT))
		goto out;

	/* old_len exactly to the end of the area.. */
	if (old_len == vma->vm_end - addr &&
	    (old_len != new_len || !(flags & MREMAP_MAYMOVE))) {
		unsigned long max_addr = TASK_SIZE;
		if (vma->vm_next)
			max_addr = vma->vm_next->vm_start;
		/* can we just expand the current mapping? */
		if (max_addr - addr >= new_len) {
			int pages = (new_len - old_len) >> PAGE_SHIFT;
			vma->vm_end = addr + new_len;
			current->mm->total_vm += pages;
			if (vma->vm_flags & VM_LOCKED) {
				current->mm->locked_vm += pages;
				make_pages_present(addr + old_len,
						   addr + new_len);
			}
			ret = addr;
			goto out;
		}
	}

	/*
	 * We weren't able to just expand or shrink the area,
	 * we need to create a new one and move it..
	 */
	if (flags & MREMAP_MAYMOVE)
		ret = move_vma(vma, addr, old_len, new_len);
	else
		ret = -ENOMEM;
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return ret;
}
