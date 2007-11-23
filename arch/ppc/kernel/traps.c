/*
 *  linux/arch/ppc/kernel/traps.c
 *
 *  Copyright (C) 1995-1996  Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  and Paul Mackerras (paulus@cs.anu.edu.au)
 */

/*
 * This file handles the architecture-dependent parts of hardware exceptions
 */

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
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>

extern int fix_alignment(struct pt_regs *);
extern void bad_page_fault(struct pt_regs *, unsigned long);

#ifdef CONFIG_XMON
extern void xmon(struct pt_regs *regs);
extern int xmon_bpt(struct pt_regs *regs);
extern int xmon_sstep(struct pt_regs *regs);
extern int xmon_iabr_match(struct pt_regs *regs);
extern int xmon_dabr_match(struct pt_regs *regs);
extern void (*xmon_fault_handler)(struct pt_regs *regs);
#endif

#ifdef CONFIG_XMON
void (*debugger)(struct pt_regs *regs) = xmon;
int (*debugger_bpt)(struct pt_regs *regs) = xmon_bpt;
int (*debugger_sstep)(struct pt_regs *regs) = xmon_sstep;
int (*debugger_iabr_match)(struct pt_regs *regs) = xmon_iabr_match;
int (*debugger_dabr_match)(struct pt_regs *regs) = xmon_dabr_match;
void (*debugger_fault_handler)(struct pt_regs *regs);
#else
#ifdef CONFIG_KGDB
void (*debugger)(struct pt_regs *regs);
int (*debugger_bpt)(struct pt_regs *regs);
int (*debugger_sstep)(struct pt_regs *regs);
int (*debugger_iabr_match)(struct pt_regs *regs);
int (*debugger_dabr_match)(struct pt_regs *regs);
void (*debugger_fault_handler)(struct pt_regs *regs);
#endif
#endif
/*
 * Trap & Exception support
 */

void
_exception(int signr, struct pt_regs *regs)
{
	if (!user_mode(regs))
	{
		show_regs(regs);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		debugger(regs);
#endif
		print_backtrace((unsigned long *)regs->gpr[1]);
		panic("Exception in kernel pc %lx signal %d",regs->nip,signr);
	}
	force_sig(signr, current);
}

void
MachineCheckException(struct pt_regs *regs)
{
	if ( !user_mode(regs) )
	{
#ifdef CONFIG_MBX
		/* the mbx pci read routines can cause machine checks -- Cort */
		bad_page_fault(regs,regs->dar);
		return;
#endif /* CONFIG_MBX */
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		if (debugger_fault_handler) {
			debugger_fault_handler(regs);
			return;
		}
#endif
		printk("Machine check in kernel mode.  (regs at %p)\n", regs);
		printk("Caused by (from srr1): ");
		switch( regs->msr & 0x001F0000) {
		case 0x100000:
			printk("L2 data cache parity error\n");
			break;
		case 0x80000:
			printk("Machine check signal\n");
			printk("(probably due to access of bad physical address\n");
			break;
		case 0x40000:
			printk("Transfer error ack signal\n");
			break;
		case 0x20000:
			printk("Data parity signal\n");
			break;
		case 0x10000:
			printk("Address parity signal\n");
			break;
		default:
			printk("Unknown values in srr1\n");
		}
		show_regs(regs);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		debugger(regs);
#endif
		print_backtrace((unsigned long *)regs->gpr[1]);
		panic("machine check");
	}
	_exception(SIGSEGV, regs);	
}

void
UnknownException(struct pt_regs *regs)
{
	printk("Bad trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);
	_exception(SIGTRAP, regs);	
}

void
InstructionBreakpoint(struct pt_regs *regs)
{
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_iabr_match(regs))
		return;
#endif
	_exception(SIGTRAP, regs);
}

void
RunModeException(struct pt_regs *regs)
{
	_exception(SIGTRAP, regs);	
}

/* Illegal instruction emulation support.  Originally written to
 * provide the PVR to user applications using the mfspr rd, PVR.
 * Return non-zero if we can't emulate, or EFAULT if the associated
 * memory access caused an access fault.  Return zero on success.
 *
 * There are a couple of ways to do this, either "decode" the instruction
 * or directly match lots of bits.  In this case, matching lots of
 * bits is faster and easier.
 *
 */
#define INST_MFSPR_PVR		0x7c1f42a6
#define INST_MFSPR_PVR_MASK	0xfc1fffff

static int
emulate_instruction(struct pt_regs *regs)
{
	uint    instword;
	uint    rd;
	int    retval;

	retval = EINVAL;
	if (!user_mode(regs))
		return retval;

	retval = EFAULT;
	if (get_user(instword, (uint *)(regs->nip)))
		return retval;

	/* Emulate the mfspr rD, PVR.
	 */
	retval = EINVAL;
	if ((instword & INST_MFSPR_PVR_MASK) == INST_MFSPR_PVR) {
		rd = (instword >> 21) & 0x1f;
		regs->gpr[rd] = _get_PVR();
		retval = 0;
	}
	if (retval == 0)
		regs->nip += 4;
	return(retval);
}

void
ProgramCheckException(struct pt_regs *regs)
{
	int errcode;

	if (regs->msr & 0x100000) {
		/* IEEE FP exception */
		_exception(SIGFPE, regs);
	} else if (regs->msr & 0x20000) {
		/* trap exception */
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
		if (debugger_bpt(regs))
			return;
#endif
		_exception(SIGTRAP, regs);
	} else {
		/* Try to emulate it if we should. */
		if ((errcode = emulate_instruction(regs))) {
			if (errcode == EFAULT)
				_exception(SIGBUS, regs);
			else
				_exception(SIGILL, regs);
		}
	}
}

void
SingleStepException(struct pt_regs *regs)
{
	regs->msr &= ~MSR_SE;  /* Turn off 'trace' bit */
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_sstep(regs))
		return;
#endif
	_exception(SIGTRAP, regs);	
}

void
AlignmentException(struct pt_regs *regs)
{
	int fixed;

	if (regs->msr & MSR_FP)
		giveup_fpu(current);
	fixed = fix_alignment(regs);
	if (fixed == 1) {
		regs->nip += 4;	/* skip over emulated instruction */
		return;
	}
	if (fixed == -EFAULT) {
		/* fixed == -EFAULT means the operand address was bad */
		bad_page_fault(regs, regs->dar);
		return;
	}
	_exception(SIGBUS, regs);	
}

void
StackOverflow(struct pt_regs *regs)
{
	printk(KERN_CRIT "Kernel stack overflow in process %p, r1=%lx\n",
	       current, regs->gpr[1]);
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	debugger(regs);
#endif
	show_regs(regs);
	print_backtrace((unsigned long *)regs->gpr[1]);
	panic("kernel stack overflow");
}

void
trace_syscall(struct pt_regs *regs)
{
	printk("Task: %p(%d), PC: %08lX/%08lX, Syscall: %3ld, Result: %s%ld\n",
	       current, current->pid, regs->nip, regs->link, regs->gpr[0],
	       regs->ccr&0x10000000?"Error=":"", regs->gpr[3]);
}

#ifdef CONFIG_8xx
void
SoftwareEmulation(struct pt_regs *regs)
{
	int	errcode;
	extern int	Soft_emulate_8xx (struct pt_regs *regs);
	extern void print_8xx_pte(struct mm_struct *, unsigned long);	

	if (user_mode(regs))
	{
		if ((errcode = Soft_emulate_8xx(regs))) {
printk("Software Emulation %s/%d NIP: %lx *NIP: 0x%x code: %x",
       current->comm,current->pid,
       regs->nip, *((uint *)regs->nip), errcode);
/*print_8xx_pte(current->mm, regs->nip);*/
			if (errcode == EFAULT)
				_exception(SIGBUS, regs);
			else
				_exception(SIGILL, regs);
		}
	}
	else {
		panic("Kernel Mode Software FPU Emulation");
	}
}
#endif

void
TAUException(struct pt_regs *regs)
{
	printk("TAU trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);
}

__initfunc(void trap_init(void))
{
}
