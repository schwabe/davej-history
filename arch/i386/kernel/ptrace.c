/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */

#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/debugreg.h>

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which flags the user has access to. */
/* 1 = access 0 = no access */
#define FLAG_MASK 0x00044dd5

/* set's the trap flag. */
#define TRAP_FLAG 0x100

/*
 * this is the number to subtract from the top of the stack. To find
 * the local frame.
 */
#define MAGICNUMBER 68

/* change a pid into a task struct. */
static inline struct task_struct * get_task(int pid)
{
	int i;

	for (i = 1; i < NR_TASKS; i++) {
		if (task[i] != NULL && (task[i]->pid == pid))
			return task[i];
	}
	return NULL;
}

/*
 * this routine will get a word off of the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */   
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task->tss.esp0;
	stack += offset;
	return (*((int *)stack));
}

/*
 * this routine will put a word on the processes privileged stack. 
 * the offset is how far from the base addr as stored in the TSS.  
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
	unsigned long data)
{
	unsigned char * stack;

	stack = (unsigned char *) task->tss.esp0;
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
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
	pgd_t * pgdir;
	pmd_t * pgmiddle;
	pte_t * pgtable;
	unsigned long page;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (pgd_none(*pgdir)) {
		do_no_page(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(tsk, vma, addr, 0);
		goto repeat;
	}
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page >= high_memory)
		return 0;
	page += addr & ~PAGE_MASK;
	return *(unsigned long *) page;
}

/*
 * This routine puts a long into any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
 * Now keeps R/W state of page so that a text page stays readonly
 * even if a debugger scribbles breakpoints into it.  -M.U-
 */
static void put_long(struct task_struct * tsk, struct vm_area_struct * vma, unsigned long addr,
	unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;

repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (!pgd_present(*pgdir)) {
		do_no_page(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace: bad page directory %08lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir, addr);
	if (pmd_none(*pgmiddle)) {
		do_no_page(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace: bad page middle %08lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		do_no_page(tsk, vma, addr, 1);
		goto repeat;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		do_wp_page(tsk, vma, addr, 1);
		goto repeat;
	}
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (page < high_memory)
		*(unsigned long *) (page + (addr & ~PAGE_MASK)) = data;
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb();
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
#ifdef CONFIG_MATH_EMULATION
static void write_emulator_word(struct task_struct *child,
				unsigned long register_offset,
				long data)
{
	int i, j;
	struct i387_soft_struct *soft_fpu;
	struct fpu_reg *this_fpreg, *next_fpreg;
	char hard_reg[2][10];
	int control_word;
	unsigned long top;
	i = register_offset / 10;
	j = register_offset % 10;
	soft_fpu = &child->tss.i387.soft;
	top = i + (unsigned long) soft_fpu->top;
	control_word = soft_fpu->cwd;
	this_fpreg = &soft_fpu->regs[(top + i) % 8];
	next_fpreg = &soft_fpu->regs[(top + i + 1) % 8];
	softreg_to_hardreg(this_fpreg, hard_reg[0], control_word);
	if (j > 6)
		softreg_to_hardreg(next_fpreg, hard_reg[1], control_word);
	*(long *) &hard_reg[0][j] = data;
	hardreg_to_softreg(hard_reg[0], this_fpreg);
	if (j > 6)
		hardreg_to_softreg(hard_reg[1], next_fpreg);
}
#endif /* defined(CONFIG_MATH_EMULATION) */

/* Put a word to the part of the user structure containing
 * floating point registers
 * Floating point support added to ptrace by Ramon Garcia,
 * ramon@juguete.quim.ucm.es
 */

static int put_fpreg_word (struct task_struct *child,
	unsigned long addr, long data)
{
	struct user *dummy = NULL;
	if (addr < (long) (&dummy->i387.st_space))
		return -EIO;
	addr -= (long) (&dummy->i387.st_space);

	if (!hard_math) {
#ifdef CONFIG_MATH_EMULATION
		write_emulator_word(child, addr, data);
#else
		return 0;
#endif
	}
	else 
#ifndef __SMP__
		if (last_task_used_math == child) {
			clts();
			__asm__("fsave %0; fwait":"=m" (child->tss.i387));
			last_task_used_math = current;
			stts();
		}
#endif
	*(long *)
		((char *) (child->tss.i387.hard.st_space) + addr) = data;
	return 0;
}

#ifdef CONFIG_MATH_EMULATION

static unsigned long get_emulator_word(struct task_struct *child,
				       unsigned long register_offset)
{
	char hard_reg[2][10];
	int i, j;
	struct fpu_reg *this_fpreg, *next_fpreg;
	struct i387_soft_struct *soft_fpu;
	long int control_word;
	unsigned long top;
	unsigned long tmp;
	i = register_offset / 10;
	j = register_offset % 10;
	soft_fpu = &child->tss.i387.soft;
	top = (unsigned long) soft_fpu->top;
	this_fpreg = &soft_fpu->regs[(top + i) % 8];
	next_fpreg = &soft_fpu->regs[(top + i + 1) % 8];
	control_word = soft_fpu->cwd;
	softreg_to_hardreg(this_fpreg, hard_reg[0], control_word);
	if (j > 6)
		softreg_to_hardreg(next_fpreg, hard_reg[1], control_word);
	tmp = *(long *)
		&hard_reg[0][j];
	return tmp;
}

#endif /* defined(CONFIG_MATH_EMULATION) */
/* Get a word from the part of the user structure containing
 * floating point registers
 */
static unsigned long get_fpreg_word(struct task_struct *child,
	unsigned long addr)
{
	struct user *dummy = NULL;
	unsigned long tmp;
	addr -= (long) (&dummy->i387.st_space);
	if (!hard_math) {
#ifdef CONFIG_MATH_EMULATION
		tmp = get_emulator_word(child, addr);
#else
		tmp = 0;
#endif /* !defined(CONFIG_MATH_EMULATION) */
	} else {
#ifndef __SMP__
		if (last_task_used_math == child) {
			clts();
			__asm__("fsave %0; fwait":"=m" (child->tss.i387));
			last_task_used_math = current;
			stts();
		}
#endif
		tmp = *(long *)
			((char *) (child->tss.i387.hard.st_space) +
			 addr);
	}
	return tmp;
}

void safe_wake_up_process(struct task_struct * p)
{
	struct desc_struct * d;
	unsigned long limit;

	void check(int index, int mask, int value) {
		unsigned long selector, limit;

		if (!p) return;

		selector = get_stack_long(p, sizeof(long)*index - MAGICNUMBER);
		if (selector & 4) {
			d = p->ldt;
			if (d) limit = get_limit(p->tss.ldt); else limit = 0;
		} else {
			d = gdt;
			limit = 8 * 8;
		}

		if ((selector & 0xFFF8) >= limit) {
			d = NULL;
			force_sig(SIGSEGV, p); p = NULL;
		} else {
			d += selector >> 3;
			if ((d->b & mask) != value ||
			    (d->b >> 13) < (selector & 3)) {
				force_sig(SIGSEGV, p); p = NULL;
			}
		}
	}

	check(DS, 0x9800, 0x9000); /* Allow present data segments only */
	check(ES, 0x9800, 0x9000);
	check(FS, 0x9800, 0x9000);
	check(GS, 0x9800, 0x9000);
	check(SS, 0x9A00, 0x9200); /* Stack segment should not be read-only */
	check(CS, 0x9800, 0x9800); /* Allow present code segments only */

	if (!p) return;

	if (d) {
		limit = (d->a & 0xFFFF) | (d->b & 0xF0000);
		if (d->b & 0x800000) {
			limit <<= 12; limit |= 0xFFF;
		}

		if (get_stack_long(p, sizeof(long)*EIP - MAGICNUMBER) > limit) {
			force_sig(SIGSEGV, p); return;
		}
	}

	wake_up_process(p);
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	struct user * dummy;
	int i;

	dummy = NULL;

	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			return -EPERM;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		return 0;
	}
	if (pid == 1)		/* you may not mess with init */
		return -EPERM;
	if (!(child = get_task(pid)))
		return -ESRCH;
	if (request == PTRACE_ATTACH) {
		if (child == current)
			return -EPERM;
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
	 	    (current->gid != child->sgid) ||
	 	    (current->gid != child->gid)) && !suser())
			return -EPERM;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			return -EPERM;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		return 0;
	}
	if (!(child->flags & PF_PTRACED))
		return -ESRCH;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			return -ESRCH;
	}
	if (child->p_pptr != current)
		return -ESRCH;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
		case PTRACE_PEEKTEXT: /* read word at location addr. */ 
		case PTRACE_PEEKDATA: {
			unsigned long tmp;
			int res;

			res = read_long(child, addr, &tmp);
			if (res < 0)
				return res;
			res = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (!res)
				put_fs_long(tmp,(unsigned long *) data);
			return res;
		}

	/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			int res;
			
  			if ((addr & 3 && 
  			     (addr < (long) (&dummy->i387) ||
  			      addr > (long) (&dummy->i387.st_space[20]) )) ||
  			    addr < 0 || addr > sizeof(struct user) - 3)
				return -EIO;
			
			res = verify_area(VERIFY_WRITE, (void *) data, sizeof(long));
			if (res)
				return res;
			tmp = 0;  /* Default return condition */
  			if (addr >= (long) (&dummy->i387) &&
  			    addr < (long) (&dummy->i387.st_space[20]) ) {
#ifndef CONFIG_MATH_EMULATION
				if (!hard_math)
					return -EIO;
#endif /* defined(CONFIG_MATH_EMULATION) */
				tmp = get_fpreg_word(child, addr);
  			} 
			if(addr < 17*sizeof(long)) {
			  addr = addr >> 2; /* temporary hack. */

			  tmp = get_stack_long(child, sizeof(long)*addr - MAGICNUMBER);
			  if (addr == DS || addr == ES ||
			      addr == FS || addr == GS ||
			      addr == CS || addr == SS)
			    tmp &= 0xffff;
			};
			if(addr >= (long) &dummy->u_debugreg[0] &&
			   addr <= (long) &dummy->u_debugreg[7]){
				addr -= (long) &dummy->u_debugreg[0];
				addr = addr >> 2;
				tmp = child->debugreg[addr];
			};
			put_fs_long(tmp,(unsigned long *) data);
			return 0;
		}

      /* when I and D space are separate, this will have to be fixed. */
		case PTRACE_POKETEXT: /* write the word at location addr. */
		case PTRACE_POKEDATA:
			return write_long(child,addr,data);

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			if ((addr & 3 &&
			     (addr < (long) (&dummy->i387.st_space[0]) ||
			      addr > (long) (&dummy->i387.st_space[20]) )) ||
			    addr < 0 || 
			    addr > sizeof(struct user) - 3)
				return -EIO;
				
			if (addr >= (long) (&dummy->i387.st_space[0]) &&
			    addr < (long) (&dummy->i387.st_space[20]) ) {
#ifndef CONFIG_MATH_EMULATION
				if (!hard_math)
					return -EIO;
#endif /* defined(CONFIG_MATH_EMULATION) */
				return put_fpreg_word(child, addr, data);
			}
			addr = addr >> 2; /* temporary hack. */
			
			if (addr == ORIG_EAX)
				return -EIO;
			if (addr == DS || addr == ES ||
			    addr == FS || addr == GS ||
			    addr == CS || addr == SS) {
			    	data &= 0xffff;
			    	if (data && (data & 3) != 3)
					return -EIO;
			}
			if (addr == EFL) {   /* flags. */
				data &= FLAG_MASK;
				data |= get_stack_long(child, EFL*sizeof(long)-MAGICNUMBER)  & ~FLAG_MASK;
			}
		  /* Do not allow the user to set the debug register for kernel
		     address space */
		  if(addr < 17){
			  if (put_stack_long(child, sizeof(long)*addr-MAGICNUMBER, data))
				return -EIO;
			return 0;
			};

		  /* We need to be very careful here.  We implicitly
		     want to modify a portion of the task_struct, and we
		     have to be selective about what portions we allow someone
		     to modify. */

		  addr = addr << 2;  /* Convert back again */
		  if(addr >= (long) &dummy->u_debugreg[0] &&
		     addr <= (long) &dummy->u_debugreg[7]){

			  if(addr == (long) &dummy->u_debugreg[4]) return -EIO;
			  if(addr == (long) &dummy->u_debugreg[5]) return -EIO;
			  if(addr < (long) &dummy->u_debugreg[4] &&
			     ((unsigned long) data) >= 0xbffffffd) return -EIO;
			  
			  if(addr == (long) &dummy->u_debugreg[7]) {
				  data &= ~DR_CONTROL_RESERVED;
				  for(i=0; i<4; i++)
					  if ((0x5f54 >> ((data >> (16 + 4*i)) & 0xf)) & 1)
						  return -EIO;
			  };

			  addr -= (long) &dummy->u_debugreg;
			  addr = addr >> 2;
			  child->debugreg[addr] = data;
			  return 0;
		  };
		  return -EIO;

		case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
		case PTRACE_CONT: { /* restart after signal. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			if (request == PTRACE_SYSCALL)
				child->flags |= PF_TRACESYS;
			else
				child->flags &= ~PF_TRACESYS;
			child->exit_code = data;
			safe_wake_up_process(child);
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
		case PTRACE_KILL: {
			long tmp;

			if (child->state == TASK_ZOMBIE)	/* already dead */
				return 0;
			wake_up_process(child);
			child->exit_code = SIGKILL;
	/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		case PTRACE_SINGLESTEP: {  /* set the trap flag. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			child->flags &= ~PF_TRACESYS;
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) | TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			child->exit_code = data;
			safe_wake_up_process(child);
	/* give it a chance to run. */
			return 0;
		}

		case PTRACE_DETACH: { /* detach a process that was attached. */
			long tmp;

			if ((unsigned long) data > NSIG)
				return -EIO;
			child->flags &= ~(PF_PTRACED|PF_TRACESYS);
			child->exit_code = data;
			safe_wake_up_process(child);
			REMOVE_LINKS(child);
			child->p_pptr = child->p_opptr;
			SET_LINKS(child);
			/* make sure the single step bit is not set. */
			tmp = get_stack_long(child, sizeof(long)*EFL-MAGICNUMBER) & ~TRAP_FLAG;
			put_stack_long(child, sizeof(long)*EFL-MAGICNUMBER,tmp);
			return 0;
		}

		default:
			return -EIO;
	}
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
	if (current->exit_code)
		current->signal |= (1 << (current->exit_code - 1));
	current->exit_code = 0;
}
