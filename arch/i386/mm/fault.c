/*
 *  linux/arch/i386/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/pgtable.h>

extern void die_if_kernel(const char *,struct pt_regs *,long);

asmlinkage void do_divide_error (struct pt_regs *, unsigned long);
asmlinkage void do_debug (struct pt_regs *, unsigned long);
asmlinkage void do_nmi (struct pt_regs *, unsigned long);
asmlinkage void do_int3 (struct pt_regs *, unsigned long);
asmlinkage void do_overflow (struct pt_regs *, unsigned long);
asmlinkage void do_bounds (struct pt_regs *, unsigned long);
asmlinkage void do_invalid_op (struct pt_regs *, unsigned long);
asmlinkage void do_general_protection (struct pt_regs *, unsigned long);

extern int pentium_f00f_bug;

static int handle_intx_eip_adjust(struct pt_regs *regs)
{
	unsigned char *addr, *csp = 0;
	int wrap = 0;
	int count = 8; /* only check for reasonable number of bytes
			 * else we do it the save 'simple way' */
	unsigned long _eip;
#define XX_WRAP(x) (wrap ? *((unsigned short *)&x) : x)

	/* We rely on being able to access the memory pointed to by cs:eip
	 * and the bytes behind it up to the faulting instruction,
	 * because we just got an exception for this instruction and
	 * hence the memory should just be successfully accessed.
	 * In case of crossing a page boundary or when accessing kernel space
	 * we just do the simple fix (increase eip by one).
	 * This assumption also obsoletes checking of segment limit.
	 * ( should be veryfied, however, if this assumption is true )
	 */

	if (regs->cs == KERNEL_CS) {
		/* not what we expect */
		regs->eip++;
		return 0;
	}

	if (regs->eflags & VM_MASK) {
		/* we have real mode type selector */
		wrap = 1;
		csp = (unsigned char *)((unsigned long)regs->cs << 4);
	}
	else if (regs->cs & 4) {
		/* we have a LDT selector */
		struct desc_struct *p, *ldt = current->ldt;
		if (!ldt)
			ldt = (struct desc_struct*) &default_ldt;
		p = ldt + (regs->cs >> 3);
		csp = (unsigned char *)((p->a >> 16) | ((p->b & 0xff) << 16) | (p->b & 0xFF000000));
		if (!(p->b & 0x400000))
			wrap = 1;	/* 16-bit segment */
	}

	_eip = regs->eip;
	addr = csp+XX_WRAP(_eip);
	while (count-- > 0) {
		if ((unsigned long)addr >= TASK_SIZE) {
			/* accessing kernel space, do the simple case */
			regs->eip++;
			return 0;
		}
		switch (get_user(addr)) {

			case 0xCC:	/* single byte INT3 */
				XX_WRAP(_eip)++;
				regs->eip = _eip;
				return 0;

			case 0xCD:	/* two byte INT 3 */
				XX_WRAP(_eip)++;
				/* fall through */
			case 0xCE:	/* INTO, single byte */
				XX_WRAP(_eip)++;
				if ( (regs->eflags & VM_MASK)
					&& ((regs->eflags & IOPL_MASK) != IOPL_MASK)) {
					/* not allowed, do GP0 fault */
					do_general_protection(regs, 0);
					return -1;
				}
				regs->eip = _eip;
				return 0;

					/* the prefixes from the Intel patch */
			case 0xF2 ... 0xF3:
			case 0x2E:
			case 0x36:
			case 0x3E:
			case 0x26:
			case 0x64 ... 0x67:
				break;	/* just skipping them */

			default:
				/* not what we handle here,
				 * just doing the simple fix
				 */
				regs->eip++;
				return 0;
		}

		if ( !(++XX_WRAP(_eip)) ) {
			/* we wrapped around */
			regs->eip++;
			return 0;
		}

		addr = csp+XX_WRAP(_eip);
		if ( !((unsigned long)addr & ~(PAGE_SIZE -1)) ) {
			/* we would cross page boundary, not good,
			 * doing the simple fix
			 */
			regs->eip++;
			return 0;
		}
	}

	/* if we come here something weird happened,
	 * just doing the simple fix
	 */
	regs->eip++;
	return 0;
}


/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *	bit 2 == 0 means kernel, 1 means user-mode
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	void (*handler)(struct task_struct *,
			struct vm_area_struct *,
			unsigned long,
			int);
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	struct vm_area_struct * vma;
	unsigned long address;
	unsigned long page;
	int write;

	/* get the address */
	__asm__("movl %%cr2,%0":"=r" (address));
	down(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (error_code & 4) {
		/*
		 * accessing the stack below %esp is always a bug.
		 * The "+ 32" is there due to some instructions (like
		 * pusha) doing pre-decrement on the stack and that
		 * doesn't show up until later..
		 */
		if (address + 32 < regs->esp)
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	write = 0;
	handler = do_no_page;
	switch (error_code & 3) {
		default:	/* 3: write, present */
			handler = do_wp_page;
#ifdef TEST_VERIFY_AREA
			if (regs->cs == KERNEL_CS)
				printk("WP fault at %08lx\n", regs->eip);
#endif
			/* fall through */
		case 2:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto bad_area;
			write++;
			break;
		case 1:		/* read, present */
			goto bad_area;
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
				goto bad_area;
	}
	handler(tsk, vma, address, write);
	up(&mm->mmap_sem);
	/*
	 * Did it hit the DOS screen memory VA from vm86 mode?
	 */
	if (regs->eflags & VM_MASK) {
		unsigned long bit = (address - 0xA0000) >> PAGE_SHIFT;
		if (bit < 32)
			tsk->tss.screen_bitmap |= 1 << bit;
	}
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up(&mm->mmap_sem);
	if (error_code & 4) {
		tsk->tss.cr2 = address;
		tsk->tss.error_code = error_code;
		tsk->tss.trap_no = 14;
		force_sig(SIGSEGV, tsk);
		return;
	}

	/*
	 * Pentium F0 0F C7 C8 bug workaround:
	 */
	if ( pentium_f00f_bug ) {
		unsigned long nr;

		nr = (address - TASK_SIZE - (unsigned long) idt) >> 3;

		if (nr < 7) {
			static void (*handler[])(struct pt_regs *, unsigned long) = {
				do_divide_error,	/* 0 - divide overflow */
				do_debug,		/* 1 - debug trap */
				do_nmi,			/* 2 - NMI */
				do_int3,		/* 3 - int 3 */
				do_overflow,		/* 4 - overflow */
				do_bounds,		/* 5 - bound range */
				do_invalid_op };	/* 6 - invalid opcode */
			if ((nr == 3) || (nr == 4))
				if (handle_intx_eip_adjust(regs))
					return;
			handler[nr](regs, error_code);
			return;
		}
	}


/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 *
 * First we check if it was the bootup rw-test, though..
 */
	if (wp_works_ok < 0 && address == TASK_SIZE && (error_code & 1)) {
		wp_works_ok = 1;
		pg0[0] = pte_val(mk_pte(0, PAGE_SHARED));
		flush_tlb();
		printk("This processor honours the WP bit even when in supervisor mode. Good.\n");
		return;
	}
	if ((unsigned long) (address-TASK_SIZE) < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	__asm__("movl %%cr3,%0" : "=r" (page));
	printk(KERN_ALERT "current->tss.cr3 = %08lx, %%cr3 = %08lx\n",
		tsk->tss.cr3, page);
	page = ((unsigned long *) page)[address >> 22];
	printk(KERN_ALERT "*pde = %08lx\n", page);
	if (page & 1) {
		page &= PAGE_MASK;
		address &= 0x003ff000;
		page = ((unsigned long *) page)[address >> PAGE_SHIFT];
		printk(KERN_ALERT "*pte = %08lx\n", page);
	}
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);
}
