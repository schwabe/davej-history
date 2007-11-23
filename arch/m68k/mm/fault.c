/*
 *  linux/arch/m68k/mm/fault.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

extern void die_if_kernel(char *, struct pt_regs *, long);
extern const int frame_extra_sizes[]; /* in m68k/kernel/signal.c */

int send_fault_sig(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		force_sig_info(current->buserr_info.si_signo,
			       &current->buserr_info, current);
	} else {
		unsigned long fixup;

		/* Are we prepared to handle this kernel fault? */
		if ((fixup = search_exception_table(regs->pc))) {
			struct pt_regs *tregs;
			/* Create a new four word stack frame, discarding the old
			   one.  */
			regs->stkadj = frame_extra_sizes[regs->format];
			tregs =	(struct pt_regs *)((ulong)regs + regs->stkadj);
			tregs->vector = regs->vector;
			tregs->format = 0;
			tregs->pc = fixup;
			tregs->sr = regs->sr;
			return -1;
		}

		if (current->buserr_info.si_signo == SIGBUS)
			force_sig_info(current->buserr_info.si_signo,
				       &current->buserr_info, current);

		/*
		 * Oops. The kernel tried to access some bad page. We'll have to
		 * terminate things with extreme prejudice.
		 */
		if ((unsigned long)current->buserr_info.si_addr < PAGE_SIZE)
			printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
		else
			printk(KERN_ALERT "Unable to handle kernel access");
		printk(" at virtual address %p\n", current->buserr_info.si_addr);
		die_if_kernel("Oops", regs, 0 /*error_code*/);
		do_exit(SIGKILL);
	}

	return 1;
}

/*
 * This routine handles page faults.  It determines the problem, and
 * then passes it off to one of the appropriate routines.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *
 * If this routine detects a bad access, it returns 1, otherwise it
 * returns 0.
 */
int do_page_fault(struct pt_regs *regs, unsigned long address,
		  unsigned long error_code)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct * vma;
	int write;

#ifdef DEBUG
	printk ("regs->sr=%#x, regs->pc=%#lx, address=%#lx, %ld, %p\n",
		regs->sr, regs->pc, address, error_code,
		current->mm->pgd);
#endif


	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_interrupt() || mm == &init_mm)
		goto no_context;

	down(&mm->mmap_sem);

	vma = find_vma(mm, address);
	if (!vma)
		goto map_err;
	if (vma->vm_flags & VM_IO)
		goto acc_err;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto map_err;
	if (user_mode(regs)) {
		/* Accessing the stack below usp is always a bug.  The
		   "+ 256" is there due to some instructions doing
		   pre-decrement on the stack and that doesn't show up
		   until later.  */
		if (address + 256 < rdusp())
			goto map_err;
	}
	if (expand_stack(vma, address))
		goto map_err;

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	write = 0;
	switch (error_code & 3) {
		default:	/* 3: write, present */
			/* fall through */
		case 2:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto acc_err;
			write++;
			break;
		case 1:		/* read, present */
			goto acc_err;
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
				goto acc_err;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	if (!handle_mm_fault(current, vma, address, write))
		goto bus_err;

	/* There seems to be a missing invalidate somewhere in do_no_page.
	 * Until I found it, this one cures the problem and makes
	 * 1.2 run on the 68040 (Martin Apel).
	 */

	if (CPU_IS_040_OR_060)
		flush_tlb_page(vma, address);

	up(&mm->mmap_sem);
	return 0;

no_context:
	current->buserr_info.si_signo = SIGBUS;
	current->buserr_info.si_addr = (void *)address;
	return send_fault_sig(regs);

bus_err:
	current->buserr_info.si_signo = SIGBUS;
	current->buserr_info.si_code = BUS_ADRERR;
	current->buserr_info.si_addr = (void *)address;
	goto send_sig;

map_err:
	current->buserr_info.si_signo = SIGSEGV;
	current->buserr_info.si_code = SEGV_MAPERR;
	current->buserr_info.si_addr = (void *)address;
	goto send_sig;

acc_err:
	current->buserr_info.si_signo = SIGSEGV;
	current->buserr_info.si_code = SEGV_ACCERR;
	current->buserr_info.si_addr = (void *)address;

send_sig:
	up(&mm->mmap_sem);
	return send_fault_sig(regs);
}
