/*  $Id: process.c,v 1.1 1996/12/28 18:39:39 davem Exp $
 *  arch/sparc64/kernel/process.c
 *
 *  Copyright (C) 1995, 1996 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/config.h>

#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/processor.h>
#include <asm/pstate.h>
#include <asm/elf.h>

extern void fpsave(unsigned long *, unsigned long *, void *, unsigned long *);

#ifndef __SMP__

/*
 * the idle loop on a Sparc... ;)
 */
asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;)
		schedule();
	return 0;
}

#else

/*
 * the idle loop on a SparcMultiPenguin...
 */
asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	schedule();
	return 0;
}

/* This is being executed in task 0 'user space'. */
int cpu_idle(void *unused)
{
	volatile int *spap = &smp_process_available;
	volatile int cval;

	while(1) {
		if(0==*spap)
			continue;
		cli();
		/* Acquire exclusive access. */
		while((cval = smp_swap(spap, -1)) == -1)
			while(*spap == -1)
				;
                if (0==cval) {
			/* ho hum, release it. */
			*spap = 0;
			sti();
                        continue;
                }
		/* Something interesting happened, whee... */
		*spap = (cval - 1);
		sti();
		idle();
	}
}

#endif

extern char reboot_command [];

#ifdef CONFIG_SUN_CONSOLE
extern void console_restore_palette (void);
extern int serial_console;
#endif

void halt_now(void)
{
	sti();
	udelay(8000);
	cli();
#ifdef CONFIG_SUN_CONSOLE
	if (!serial_console)
		console_restore_palette ();
#endif
	prom_halt();
	panic("Halt failed!");
}

void hard_reset_now(void)
{
	char *p;
	
	sti();
	udelay(8000);
	cli();

	p = strchr (reboot_command, '\n');
	if (p) *p = 0;
#ifdef CONFIG_SUN_CONSOLE
	if (!serial_console)
		console_restore_palette ();
#endif
	if (*reboot_command)
		prom_reboot (reboot_command);
	prom_feval ("reset");
	panic("Reboot failed!");
}

void show_regwindow(struct reg_window *rw)
{
	printk("l0: %016lx l1: %016lx l2: %016lx l3: %016lx\n"
	       "l4: %016lx l5: %016lx l6: %016lx l7: %016lx\n",
	       rw->locals[0], rw->locals[1], rw->locals[2], rw->locals[3],
	       rw->locals[4], rw->locals[5], rw->locals[6], rw->locals[7]);
	printk("i0: %016lx i1: %016lx i2: %016lx i3: %016lx\n"
	       "i4: %016lx i5: %016lx i6: %016lx i7: %016lx\n",
	       rw->ins[0], rw->ins[1], rw->ins[2], rw->ins[3],
	       rw->ins[4], rw->ins[5], rw->ins[6], rw->ins[7]);
}

void show_regwindow32(struct reg_window32 *rw)
{
	printk("l0: %08lx l1: %08lx l2: %08lx l3: %08lx\n"
	       "l4: %08lx l5: %08lx l6: %08lx l7: %08lx\n",
	       rw->locals[0], rw->locals[1], rw->locals[2], rw->locals[3],
	       rw->locals[4], rw->locals[5], rw->locals[6], rw->locals[7]);
	printk("i0: %08lx i1: %08lx i2: %08lx i3: %08lx\n"
	       "i4: %08lx i5: %08lx i6: %08lx i7: %08lx\n",
	       rw->ins[0], rw->ins[1], rw->ins[2], rw->ins[3],
	       rw->ins[4], rw->ins[5], rw->ins[6], rw->ins[7]);
}

void show_stackframe(struct sparc_stackf *sf)
{
	unsigned long size;
	unsigned long *stk;
	int i;

	printk("l0: %016lx l1: %016lx l2: %016lx l3: %016lx\n"
	       "l4: %016lx l5: %016lx l6: %016lx l7: %016lx\n",
	       sf->locals[0], sf->locals[1], sf->locals[2], sf->locals[3],
	       sf->locals[4], sf->locals[5], sf->locals[6], sf->locals[7]);
	printk("i0: %016lx i1: %016lx i2: %016lx i3: %016lx\n"
	       "i4: %016lx i5: %016lx fp: %016lx ret_pc: %016lx\n",
	       sf->ins[0], sf->ins[1], sf->ins[2], sf->ins[3],
	       sf->ins[4], sf->ins[5], (unsigned long)sf->fp, sf->callers_pc);
	printk("sp: %016lx x0: %016lx x1: %016lx x2: %016lx\n"
	       "x3: %016lx x4: %016lx x5: %016lx xx: %016lx\n",
	       (unsigned long)sf->structptr, sf->xargs[0], sf->xargs[1],
	       sf->xargs[2], sf->xargs[3], sf->xargs[4], sf->xargs[5],
	       sf->xxargs[0]);
	size = ((unsigned long)sf->fp) - ((unsigned long)sf);
	size -= STACKFRAME_SZ;
	stk = (unsigned long *)((unsigned long)sf + STACKFRAME_SZ);
	i = 0;
	do {
		printk("s%d: %016lx\n", i++, *stk++);
	} while ((size -= sizeof(unsigned long)));
}

void show_stackframe32(struct sparc_stackf32 *sf)
{
	unsigned long size;
	unsigned long *stk;
	int i;

	printk("l0: %08lx l1: %08lx l2: %08lx l3: %08lx\n"
	       "l4: %08lx l5: %08lx l6: %08lx l7: %08lx\n",
	       sf->locals[0], sf->locals[1], sf->locals[2], sf->locals[3],
	       sf->locals[4], sf->locals[5], sf->locals[6], sf->locals[7]);
	printk("i0: %08lx i1: %08lx i2: %08lx i3: %08lx\n"
	       "i4: %08lx i5: %08lx fp: %08lx ret_pc: %08lx\n",
	       sf->ins[0], sf->ins[1], sf->ins[2], sf->ins[3],
	       sf->ins[4], sf->ins[5], (unsigned long)sf->fp, sf->callers_pc);
	printk("sp: %08lx x0: %08lx x1: %08lx x2: %08lx\n"
	       "x3: %08lx x4: %08lx x5: %08lx xx: %08lx\n",
	       (unsigned long)sf->structptr, sf->xargs[0], sf->xargs[1],
	       sf->xargs[2], sf->xargs[3], sf->xargs[4], sf->xargs[5],
	       sf->xxargs[0]);
	size = ((unsigned long)sf->fp) - ((unsigned long)sf);
	size -= STACKFRAME_SZ;
	stk = (unsigned long *)((unsigned long)sf + STACKFRAME_SZ);
	i = 0;
	do {
		printk("s%d: %08lx\n", i++, *stk++);
	} while ((size -= sizeof(unsigned long)));
}

void show_regs(struct pt_regs * regs)
{
#if __MPP__
	printk("CID: %d\n",mpp_cid());
#endif
        printk("TSTATE: %016lx TPC: %016lx TNPC: %016lx Y: %016lx\n", regs->tstate,
	       regs->tpc, regs->tnpc, regs->y);
	printk("g0: %016lx g1: %016lx g2: %016lx g3: %016lx\n",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("g4: %016lx g5: %016lx g6: %016lx g7: %016lx\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("o0: %016lx o1: %016lx o2: %016lx o3: %016lx\n",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("o4: %016lx o5: %016lx sp: %016lx ret_pc: %016lx\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
	show_regwindow((struct reg_window *)regs->u_regs[14]);
}

void show_regs32(struct pt_regs32 *regs)
{
#if __MPP__
	printk("CID: %d\n",mpp_cid());
#endif
        printk("PSR: %08lx PC: %08lx NPC: %08lx Y: %08lx\n", regs->psr,
	       regs->pc, regs->npc, regs->y);
	printk("g0: %08lx g1: %08lx g2: %08lx g3: %08lx\n",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("g4: %08lx g5: %08lx g6: %08lx g7: %08lx\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("o0: %08lx o1: %08lx o2: %08lx o3: %08lx\n",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("o4: %08lx o5: %08lx sp: %08lx ret_pc: %08lx\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
	show_regwindow32((struct reg_window32 *)regs->u_regs[14]);
}

void show_thread(struct thread_struct *tss)
{
	int i;

	printk("kregs:             0x%016lx\n", (unsigned long)tss->kregs);
	show_regs(tss->kregs);
	printk("sig_address:       0x%016lx\n", tss->sig_address);
	printk("sig_desc:          0x%016lx\n", tss->sig_desc);
	printk("ksp:               0x%016lx\n", tss->ksp);
	printk("kpc:               0x%016lx\n", tss->kpc);

	for (i = 0; i < NSWINS; i++) {
		if (!tss->rwbuf_stkptrs[i])
			continue;
		printk("reg_window[%d]:\n", i);
		printk("stack ptr:         0x%016lx\n", tss->rwbuf_stkptrs[i]);
		show_regwindow(&tss->reg_window[i]);
	}
	printk("w_saved:           0x%08lx\n", tss->w_saved);

	/* XXX missing: float_regs */
	printk("fsr:               0x%016lx\n", tss->fsr);
	printk("fpqdepth:          0x%016lx\n", tss->fpqdepth);
	/* XXX missing: fpqueue */

	printk("sstk_info.stack:   0x%016lx\n",
	        (unsigned long)tss->sstk_info.the_stack);
	printk("sstk_info.status:  0x%016lx\n",
	        (unsigned long)tss->sstk_info.cur_status);
	printk("flags:             0x%016lx\n", tss->flags);
	printk("current_ds:        0x%016lx\n", tss->current_ds);

	/* XXX missing: core_exec */
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	kill_user_windows();
#ifndef __SMP__
	if(last_task_used_math == current) {
#else
	if(current->flags & PF_USEDFPU) {
#endif
		/* Keep process from leaving FPU in a bogon state. */
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->tss.float_regs[0], &current->tss.fsr,
		       &current->tss.fpqueue[0], &current->tss.fpqdepth);
#ifndef __SMP__
		last_task_used_math = NULL;
#else
		current->flags &= ~PF_USEDFPU;
#endif
	}
	mmu_exit_hook();
}

void flush_thread(void)
{
	kill_user_windows();
	current->tss.w_saved = 0;
	current->tss.uwinmask = 0;
	current->tss.sstk_info.cur_status = 0;
	current->tss.sstk_info.the_stack = 0;

	/* No new signal delivery by default */
	current->tss.new_signal = 0;
#ifndef __SMP__
	if(last_task_used_math == current) {
#else
	if(current->flags & PF_USEDFPU) {
#endif
		/* Clean the fpu. */
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->tss.float_regs[0], &current->tss.fsr,
		       &current->tss.fpqueue[0], &current->tss.fpqdepth);
#ifndef __SMP__
		last_task_used_math = NULL;
#else
		current->flags &= ~PF_USEDFPU;
#endif
	}

	mmu_flush_hook();
	/* Now, this task is no longer a kernel thread. */
	current->tss.flags &= ~SPARC_FLAG_KTHREAD;
	current->tss.current_ds = USER_DS;
}

static __inline__ void copy_regs(struct pt_regs *dst, struct pt_regs *src)
{
	__asm__ __volatile__("ldd\t[%1 + 0x00], %%g2\n\t"
			     "ldd\t[%1 + 0x08], %%g4\n\t"
			     "ldd\t[%1 + 0x10], %%o4\n\t"
			     "std\t%%g2, [%0 + 0x00]\n\t"
			     "std\t%%g4, [%0 + 0x08]\n\t"
			     "std\t%%o4, [%0 + 0x10]\n\t"
			     "ldd\t[%1 + 0x18], %%g2\n\t"
			     "ldd\t[%1 + 0x20], %%g4\n\t"
			     "ldd\t[%1 + 0x28], %%o4\n\t"
			     "std\t%%g2, [%0 + 0x18]\n\t"
			     "std\t%%g4, [%0 + 0x20]\n\t"
			     "std\t%%o4, [%0 + 0x28]\n\t"
			     "ldd\t[%1 + 0x30], %%g2\n\t"
			     "ldd\t[%1 + 0x38], %%g4\n\t"
			     "ldd\t[%1 + 0x40], %%o4\n\t"
			     "std\t%%g2, [%0 + 0x30]\n\t"
			     "std\t%%g4, [%0 + 0x38]\n\t"
			     "ldd\t[%1 + 0x48], %%g2\n\t"
			     "std\t%%o4, [%0 + 0x40]\n\t"
			     "std\t%%g2, [%0 + 0x48]\n\t" : :
			     "r" (dst), "r" (src) :
			     "g2", "g3", "g4", "g5", "o4", "o5");
}

static __inline__ void copy_regwin(struct reg_window *dst, struct reg_window *src)
{
	__asm__ __volatile__("ldd\t[%1 + 0x00], %%g2\n\t"
			     "ldd\t[%1 + 0x08], %%g4\n\t"
			     "ldd\t[%1 + 0x10], %%o4\n\t"
			     "std\t%%g2, [%0 + 0x00]\n\t"
			     "std\t%%g4, [%0 + 0x08]\n\t"
			     "std\t%%o4, [%0 + 0x10]\n\t"
			     "ldd\t[%1 + 0x18], %%g2\n\t"
			     "ldd\t[%1 + 0x20], %%g4\n\t"
			     "ldd\t[%1 + 0x28], %%o4\n\t"
			     "std\t%%g2, [%0 + 0x18]\n\t"
			     "std\t%%g4, [%0 + 0x20]\n\t"
			     "std\t%%o4, [%0 + 0x28]\n\t"
			     "ldd\t[%1 + 0x30], %%g2\n\t"
			     "ldd\t[%1 + 0x38], %%g4\n\t"
			     "std\t%%g2, [%0 + 0x30]\n\t"
			     "std\t%%g4, [%0 + 0x38]\n\t" : :
			     "r" (dst), "r" (src) :
			     "g2", "g3", "g4", "g5", "o4", "o5");
}

static __inline__ struct sparc_stackf *
clone_stackframe(struct sparc_stackf *dst, struct sparc_stackf *src)
{
	unsigned long size;
	struct sparc_stackf *sp;

	size = ((unsigned long)src->fp) - ((unsigned long)src);
	sp = (struct sparc_stackf *)(((unsigned long)dst) - size); 

	if (copy_to_user(sp, src, size))
		return 0;
	if (put_user(dst, &sp->fp))
		return 0;
	return sp;
}


/* Copy a Sparc thread.  The fork() return value conventions
 * under SunOS are nothing short of bletcherous:
 * Parent -->  %o0 == childs  pid, %o1 == 0
 * Child  -->  %o0 == parents pid, %o1 == 1
 *
 * NOTE: We have a separate fork kpsr/kwim because
 *       the parent could change these values between
 *       sys_fork invocation and when we reach here
 *       if the parent should sleep while trying to
 *       allocate the task_struct and kernel stack in
 *       do_fork().
 */
extern void ret_from_syscall(void);

int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	struct reg_window *new_stack;
	unsigned long stack_offset;

#ifndef __SMP__
	if(last_task_used_math == current) {
#else
	if(current->flags & PF_USEDFPU) {
#endif
		put_psr(get_psr() | PSR_EF);
		fpsave(&p->tss.float_regs[0], &p->tss.fsr,
		       &p->tss.fpqueue[0], &p->tss.fpqdepth);
#ifdef __SMP__
		current->flags &= ~PF_USEDFPU;
#endif
	}

	/* Calculate offset to stack_frame & pt_regs */
	stack_offset = ((PAGE_SIZE<<1) - TRACEREG_SZ);

	if(regs->psr & PSR_PS)
		stack_offset -= REGWIN_SZ;
	childregs = ((struct pt_regs *) (p->kernel_stack_page + stack_offset));
	copy_regs(childregs, regs);
	new_stack = (((struct reg_window *) childregs) - 1);
	copy_regwin(new_stack, (((struct reg_window *) regs) - 1));

	p->tss.ksp = p->saved_kernel_stack = (unsigned long) new_stack;
	p->tss.kpc = (((unsigned long) ret_from_syscall) - 0x8);
	p->tss.kpsr = current->tss.fork_kpsr;
	p->tss.kwim = current->tss.fork_kwim;
	p->tss.kregs = childregs;

	if(regs->psr & PSR_PS) {
		childregs->u_regs[UREG_FP] = p->tss.ksp;
		p->tss.flags |= SPARC_FLAG_KTHREAD;
		p->tss.current_ds = KERNEL_DS;
		childregs->u_regs[UREG_G6] = (unsigned long) p;
	} else {
		childregs->u_regs[UREG_FP] = sp;
		p->tss.flags &= ~SPARC_FLAG_KTHREAD;
		p->tss.current_ds = USER_DS;

		if (sp != current->tss.kregs->u_regs[UREG_FP]) {
			struct sparc_stackf *childstack;
			struct sparc_stackf *parentstack;

			/*
			 * This is a clone() call with supplied user stack.
			 * Set some valid stack frames to give to the child.
			 */
			childstack = (struct sparc_stackf *)sp;
			parentstack = (struct sparc_stackf *)
					current->tss.kregs->u_regs[UREG_FP];

#if 0
			printk("clone: parent stack:\n");
			show_stackframe(parentstack);
#endif

			childstack = clone_stackframe(childstack, parentstack);
			if (!childstack)
				return -EFAULT;

#if 0
			printk("clone: child stack:\n");
			show_stackframe(childstack);
#endif

			childregs->u_regs[UREG_FP] = (unsigned long)childstack;
		}
	}

	/* Set the return value for the child. */
	childregs->u_regs[UREG_I0] = current->pid;
	childregs->u_regs[UREG_I1] = 1;

	/* Set the return value for the parent. */
	regs->u_regs[UREG_I1] = 0;

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	unsigned long first_stack_page;

	dump->magic = SUNOS_CORE_MAGIC;
	dump->len = sizeof(struct user);
	dump->regs.psr = regs->psr;
	dump->regs.pc = regs->pc;
	dump->regs.npc = regs->npc;
	dump->regs.y = regs->y;
	/* fuck me plenty */
	memcpy(&dump->regs.regs[0], &regs->u_regs[1], (sizeof(unsigned long) * 15));
	dump->uexec = current->tss.core_exec;
	dump->u_tsize = (((unsigned long) current->mm->end_code) -
		((unsigned long) current->mm->start_code)) & ~(PAGE_SIZE - 1);
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1)));
	dump->u_dsize -= dump->u_tsize;
	dump->u_dsize &= ~(PAGE_SIZE - 1);
	first_stack_page = (regs->u_regs[UREG_FP] & ~(PAGE_SIZE - 1));
	dump->u_ssize = (TASK_SIZE - first_stack_page) & ~(PAGE_SIZE - 1);
	memcpy(&dump->fpu.fpstatus.fregs.regs[0], &current->tss.float_regs[0], (sizeof(unsigned long) * 32));
	dump->fpu.fpstatus.fsr = current->tss.fsr;
	dump->fpu.fpstatus.flags = dump->fpu.fpstatus.extra = 0;
	dump->fpu.fpstatus.fpq_count = current->tss.fpqdepth;
	memcpy(&dump->fpu.fpstatus.fpq[0], &current->tss.fpqueue[0],
	       ((sizeof(unsigned long) * 2) * 16));
	dump->sigcode = current->tss.sig_desc;
}

/*
 * fill in the fpu structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, elf_fpregset_t * fpregs)
{
	/* Currently we report that we couldn't dump the fpu structure */
	return 0;
}

/*
 * sparc_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage int sparc_execve(struct pt_regs *regs)
{
	int error, base = 0;
	char *filename;

	/* Check for indirect call. */
	if(regs->u_regs[UREG_G1] == 0)
		base = 1;

	error = getname((char *) regs->u_regs[base + UREG_I0], &filename);
	if(error)
		return error;
	error = do_execve(filename, (char **) regs->u_regs[base + UREG_I1],
			  (char **) regs->u_regs[base + UREG_I2], regs);
	putname(filename);
	return error;
}