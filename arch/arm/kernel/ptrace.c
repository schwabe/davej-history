/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */
/* edited for ARM by Russell King */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>


#define REG_PC	15
#define REG_PSR	16
/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Breakpoint SWI instruction: SWI &9F0001
 */
#define BREAKINST	0xef9f0001

/*
 * Get the address of the live pt_regs for the specified task.
 * These are saved onto the top kernel stack when the process
 * is not running.
 */
static inline struct pt_regs *
get_user_regs(struct task_struct *task)
{
	return (struct pt_regs *)
		((unsigned long)task + 8192 - sizeof(struct pt_regs));
}

/*
 * this routine will get a word off of the processes privileged stack.
 * the offset is how far from the base addr as stored in the TSS.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline long get_stack_long(struct task_struct *task, int offset)
{
	return get_user_regs(task)->uregs[offset];
}

/*
 * this routine will put a word on the processes privileged stack.
 * the offset is how far from the base addr as stored in the TSS.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int
put_stack_long(struct task_struct *task, int offset, long data)
{
	struct pt_regs newregs, *regs = get_user_regs(task);
	int ret = -EINVAL;

	newregs = *regs;
	newregs.uregs[offset] = data;

	if (valid_user_regs(&newregs)) {
		regs->uregs[offset] = data;
		ret = 0;
	}

	return ret;
}

/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 */
static unsigned long get_long(struct task_struct * tsk,
	struct vm_area_struct * vma, unsigned long addr)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;
	int fault;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (pgd_none(*pgdir))
		goto none;
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle))
		goto none;
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable))
		goto none;
	page = pte_page(*pgtable);
 
	if(MAP_NR(page) >= max_mapnr)
		return 0;
	page += addr & ~PAGE_MASK;
	return *(unsigned long *)page;

none:
	fault = handle_mm_fault(tsk, vma, addr, 0);
	if (fault > 0)
		goto repeat;
	if (fault < 0)
		force_sig(SIGKILL, tsk);
	return 0;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of the page so that a text page stays readonly
 * even if a debugger scribbles breakpoints into it.  -M.U-
 */
static void put_long(struct task_struct * tsk, struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;
	int fault;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (!pgd_present(*pgdir))
		goto none;
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle))
		goto none;
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable))
		goto none;
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable))
		goto none;
	
	if (MAP_NR(page) < max_mapnr) {
		page += addr & ~PAGE_MASK;

		flush_cache_range(vma->vm_mm, addr, addr + sizeof(unsigned long));

		*(unsigned long *)page = data;

		clean_cache_area(page, sizeof(unsigned long));

		set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
		flush_tlb_page(vma, addr & PAGE_MASK);
	}
	return;

none:
	fault = handle_mm_fault(tsk, vma, addr, 1);
	if (fault > 0)
		goto repeat;
	if (fault < 0)
		force_sig(SIGKILL, tsk);
}

static struct vm_area_struct * find_extend_vma(struct task_struct * tsk, unsigned long addr)
{
	struct vm_area_struct * vma;

	addr &= PAGE_MASK;
	vma = find_vma(tsk->mm,addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	if (vma->vm_end - addr > tsk->rlim[RLIMIT_STACK].rlim_cur)
		return NULL;
	vma->vm_offset -= vma->vm_start - addr;
	vma->vm_start = addr;
	return vma;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls get_long() to read a long.
 */
static int read_long(struct task_struct * tsk, unsigned long addr,
	unsigned long * result)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(tsk, vma, addr & ~(sizeof(long)-1));
		high = get_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 1:
				low >>= 8;
				low |= high << 24;
				break;
			case 2:
				low >>= 16;
				low |= high << 16;
				break;
			case 3:
				low >>= 24;
				low |= high << 8;
				break;
		}
		*result = low;
	} else
		*result = get_long(tsk, vma, addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	struct vm_area_struct * vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_high = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_high = vma->vm_next;
			if (!vma_high || vma_high->vm_start != vma->vm_end)
				return -EIO;
		}
		low = get_long(tsk, vma, addr & ~(sizeof(long)-1));
		high = get_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				low = data;
				break;
			case 1:
				low &= 0x000000ff;
				low |= data << 8;
				high &= ~0xff;
				high |= data >> 24;
				break;
			case 2:
				low &= 0x0000ffff;
				low |= data << 16;
				high &= ~0xffff;
				high |= data >> 16;
				break;
			case 3:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= ~0xffffff;
				high |= data >> 8;
				break;
		}
		put_long(tsk, vma, addr & ~(sizeof(long)-1),low);
		put_long(tsk, vma_high, (addr+sizeof(long)) & ~(sizeof(long)-1),high);
	} else
		put_long(tsk, vma, addr, data);
	return 0;
}

#define write_tsk_long(chld, addr, val) write_long((chld), (addr), (val))
#define read_tsk_long(chld, addr, val)  read_long((chld), (addr), (val))

/*
 * Get value of register `rn' (in the instruction)
 */
static unsigned long
ptrace_getrn(struct task_struct *child, unsigned long insn)
{
	unsigned int reg = (insn >> 16) & 15;
	unsigned long val;

	val = get_stack_long(child, reg);
	if (reg == 15)
		val = pc_pointer(val + 8);


	return val;
}

/*
 * Get value of operand 2 (in an ALU instruction)
 */
static unsigned long
ptrace_getaluop2(struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;


	if (insn & 1 << 25) {
		val = insn & 255;
		shift = (insn >> 8) & 15;
		type = 3;

	} else {
		val = get_stack_long (child, insn & 15);

		if (insn & (1 << 4))
			shift = (int)get_stack_long (child, (insn >> 8) & 15);
		else
			shift = (insn >> 7) & 31;

		type = (insn >> 5) & 3;

	}

	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
		val = (val >> shift) | (val << (32 - shift));
		break;
	}

	return val;
}

/*
 * Get value of operand 2 (in a LDR instruction)
 */
static unsigned long
ptrace_getldrop2(struct task_struct *child, unsigned long insn)
{
	unsigned long val;
	int shift;
	int type;

	val = get_stack_long(child, insn & 15);
	shift = (insn >> 7) & 31;
	type = (insn >> 5) & 3;


	switch (type) {
	case 0:	val <<= shift;	break;
	case 1:	val >>= shift;	break;
	case 2:
		val = (((signed long)val) >> shift);
		break;
	case 3:
		val = (val >> shift) | (val << (32 - shift));
		break;
	}

	return val;
}

static unsigned long
get_branch_address(struct task_struct *child, unsigned long pc, unsigned long insn)
{
	unsigned long alt = 0;

	switch (insn & 0x0e000000) {
	case 0x00000000:
	case 0x02000000: {
		/*
		 * data processing
		 */
		long aluop1, aluop2, ccbit;

		if ((insn & 0xf000) != 0xf000)
			break;



		aluop1 = ptrace_getrn(child, insn);
		aluop2 = ptrace_getaluop2(child, insn);
		ccbit  = get_stack_long(child, REG_PSR) & CC_C_BIT ? 1 : 0;

		switch (insn & 0x01e00000) {
		case 0x00000000: alt = aluop1 & aluop2;		break;
		case 0x00200000: alt = aluop1 ^ aluop2;		break;
		case 0x00400000: alt = aluop1 - aluop2;		break;
		case 0x00600000: alt = aluop2 - aluop1;		break;
		case 0x00800000: alt = aluop1 + aluop2;		break;
		case 0x00a00000: alt = aluop1 + aluop2 + ccbit;	break;
		case 0x00c00000: alt = aluop1 - aluop2 + ccbit;	break;
		case 0x00e00000: alt = aluop2 - aluop1 + ccbit;	break;
		case 0x01800000: alt = aluop1 | aluop2;		break;
		case 0x01a00000: alt = aluop2;			break;
		case 0x01c00000: alt = aluop1 & ~aluop2;	break;
		case 0x01e00000: alt = ~aluop2;			break;
		}
		break;
	}

	case 0x04000000:
	case 0x06000000:
		/*
		 * ldr
		 */
		if ((insn & 0x0010f000) == 0x0010f000) {
			unsigned long base;

			base = ptrace_getrn(child, insn);
			if (insn & 1 << 24) {
				long aluop2;

				if (insn & 0x02000000)
					aluop2 = ptrace_getldrop2(child, insn);
				else
					aluop2 = insn & 0xfff;

				if (insn & 1 << 23)
					base += aluop2;
				else
					base -= aluop2;
			}

			if (read_tsk_long(child, base, &alt) == 0)
				alt = pc_pointer(alt);
		}
		break;

	case 0x08000000:
		/*
		 * ldm
		 */
		if ((insn & 0x00108000) == 0x00108000) {
			unsigned long base;
			unsigned int nr_regs;


			if (insn & (1 << 23)) {
				nr_regs = insn & 65535;

				nr_regs = (nr_regs & 0x5555) + ((nr_regs & 0xaaaa) >> 1);
				nr_regs = (nr_regs & 0x3333) + ((nr_regs & 0xcccc) >> 2);
				nr_regs = (nr_regs & 0x0707) + ((nr_regs & 0x7070) >> 4);
				nr_regs = (nr_regs & 0x000f) + ((nr_regs & 0x0f00) >> 8);
				nr_regs <<= 2;

				if (!(insn & (1 << 24)))
					nr_regs -= 4;
			} else {
				if (insn & (1 << 24))
					nr_regs = -4;
				else
					nr_regs = 0;
			}

			base = ptrace_getrn(child, insn);

			if (read_tsk_long(child, base + nr_regs, &alt) == 0)
				alt = pc_pointer(alt);
			break;
		}
		break;

	case 0x0a000000: {
		/*
		 * bl or b
		 */
		signed long displ;

		/* It's a branch/branch link: instead of trying to
		 * figure out whether the branch will be taken or not,
		 * we'll put a breakpoint at both locations.  This is
		 * simpler, more reliable, and probably not a whole lot
		 * slower than the alternative approach of emulating the
		 * branch.
		 */
		displ = (insn & 0x00ffffff) << 8;
		displ = (displ >> 6) + 8;
		if (displ != 0 && displ != 4)
			alt = pc + displ;
	    }
	    break;
	}
printk ("=%08lX\n", alt);
	if (alt != pc + 4)
		child->tss.debug[nsaved++] = alt;

	for (i = 0; i < nsaved; i++) {
		res = read_long (child, child->tss.debug[i], &insn);
		if (res >= 0) {
			child->tss.debug[i + 2] = insn;
			res = write_long (child, child->tss.debug[i], BREAKINST);
		}
		if (res < 0) {
			child->tss.debug[4] = 0;
			return res;
		}
	}
	child->tss.debug[4] = nsaved;
	return 0;
}

/* Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
int ptrace_cancel_bpt (struct task_struct *child)
{
	int i, nsaved = child->tss.debug[4];

	child->tss.debug[4] = 0;

	if (nsaved > 2) {
		printk ("ptrace_cancel_bpt: bogus nsaved: %d!\n", nsaved);
		nsaved = 2;
	}
	for (i = 0; i < nsaved; i++)
		write_long (child, child->tss.debug[i], child->tss.debug[i + 2]);
	return nsaved != 0;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		ret = 0;
		goto out;
	}
	if (pid == 1)		/* you may not mess with init */
		goto out;
	ret = -ESRCH;
	if (!(child = find_task_by_pid(pid)))
		goto out;
	ret = -EPERM;
	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out;
		if ((child->dumpable != 1 ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->sgid) ||
	 	    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child->flags & PF_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current)
		goto out;

	switch (request) {
		case PTRACE_PEEKTEXT:				/* read word at location addr. */
		case PTRACE_PEEKDATA: {
			unsigned long tmp;

			ret = read_long(child, addr, &tmp);
			if (ret >= 0)
				ret = put_user(tmp, (unsigned long *)data);
			goto out;
		}

		case PTRACE_PEEKUSR: {				/* read the word at location addr in the USER area. */
			unsigned long tmp;

			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				goto out;

			tmp = 0;  /* Default return condition */
			if (addr < sizeof (struct pt_regs))
				tmp = get_stack_long(child, (int)addr >> 2);
			ret = put_user(tmp, (unsigned long *)data);
			goto out;
		}

		case PTRACE_POKETEXT:				/* write the word at location addr. */
		case PTRACE_POKEDATA:
			ret = write_long(child,addr,data);
			goto out;

		case PTRACE_POKEUSR:				/* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr < 0 || addr >= sizeof(struct user))
				goto out;

			if (addr < sizeof (struct pt_regs))
				ret = put_stack_long(child, (int)addr >> 2, data);
			goto out;

		case PTRACE_SYSCALL:				/* continue and stop at next (return from) syscall */
		case PTRACE_CONT:				/* restart after signal. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			wake_up_process (child);
			/* make sure single-step breakpoint is gone. */
			ptrace_cancel_bpt (child);
			ret = 0;
			goto out;

		/* make the child exit.  Best I can do is send it a sigkill.
		 * perhaps it should be put in the status that it wants to
		 * exit.
		 */
		case PTRACE_KILL:
			if (child->state == TASK_ZOMBIE)	/* already dead */
				return 0;
			wake_up_process (child);
			child->exit_code = SIGKILL;
			/* make sure single-step breakpoint is gone. */
			ptrace_cancel_bpt (child);
			ret = 0;
			goto out;

		case PTRACE_SINGLESTEP:				/* execute single instruction. */
			ret = -EIO;
			if ((unsigned long) data > _NSIG)
				goto out;
			child->tss.debug[4] = -1;
			child->flags &= ~PF_TRACESYS;
			wake_up_process(child);
			child->exit_code = data;
			/* give it a chance to run. */
			ret = 0;
			goto out;
			
		case PTRACE_GETREGS:
		{	/* Get all gp regs from the child. */
			unsigned char *stack;

			ret = 0;
			stack = (unsigned char *)((unsigned long)child + 8192 - sizeof(struct pt_regs));
			if (copy_to_user((void *)data, stack,
					 sizeof(struct pt_regs)))
				ret = -EFAULT;

			goto out;
		};

		/* Set all gp regs in the child. */
		case PTRACE_SETREGS:
		{
			struct pt_regs newregs;

			ret = -EFAULT;
			if (copy_from_user(&newregs, (void *)data,
					   sizeof(struct pt_regs)) == 0) {
				struct pt_regs *regs = get_user_regs(child);

				ret = -EINVAL;
				if (valid_user_regs(&newregs)) {
					*regs = newregs;
					ret = 0;
				}
			}
			break;
		}

		/*
		 * Get the child FPU state.
		 */
		case PTRACE_GETFPREGS: 
			ret = -EIO;
			if (!access_ok(VERIFY_WRITE, (void *)data, sizeof(struct user_fp)))
				break;

			/* we should check child->used_math here */
			ret = __copy_to_user((void *)data, &child->tss.fpstate,
					     sizeof(struct user_fp)) ? -EFAULT : 0;
			break;

		/*
		 * Set the child FPU state.
		 */
		case PTRACE_SETFPREGS:
			ret = -EIO;
			if (!access_ok(VERIFY_READ, (void *)data, sizeof(struct user_fp)))
				break;

			child->used_math = 1;
			ret = __copy_from_user(&child->tss.fpstate, (void *)data,
					   sizeof(struct user_fp)) ? -EFAULT : 0;
			break;

		default:
			ret = -EIO;
			break;
	}

	return ret;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child = find_task_by_pid(pid)))
		goto out;

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;

	if (request == PTRACE_ATTACH) {
		if (child == current)
			goto out;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->sgid) ||
	 	    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;

		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}

		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child->flags & PF_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED && request != PTRACE_KILL)
		goto out;
	if (child->p_pptr != current)
		goto out;

	ret = do_ptrace(request, child, addr, data);

out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
