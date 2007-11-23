/*
 *  arch/s390/kernel/ptrace.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Based on PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@cs.nmt.edu) 
 *
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>


void FixPerRegisters(struct task_struct *task)
{
	struct pt_regs *regs = task->tss.regs;
	per_struct *per_info=
			(per_struct *)&task->tss.per_info;

	per_info->control_regs.bits.em_instruction_fetch=
	per_info->single_step|per_info->instruction_fetch;
	
	if(per_info->single_step)
	{
		per_info->control_regs.bits.starting_addr=0;
		per_info->control_regs.bits.ending_addr=0x7fffffffUL;
	}
	else
	{
		per_info->control_regs.bits.starting_addr=
		per_info->starting_addr;
		per_info->control_regs.bits.ending_addr=
		per_info->ending_addr;
	}
	/* if any of the control reg tracing bits are on 
	   we switch on per in the psw */
	if(per_info->control_regs.words.cr[0]&PER_EM_MASK)
		regs->psw.mask |=PSW_PER_MASK;
	else
		regs->psw.mask &= ~PSW_PER_MASK;
	if(per_info->control_regs.bits.storage_alt_space_ctl)
		task->tss.user_seg|=USER_STD_MASK;
	else
		task->tss.user_seg&=~USER_STD_MASK;
}

void set_single_step(struct task_struct *task)
{
	per_struct *per_info=
			(per_struct *)&task->tss.per_info;	
	
	per_info->single_step=1;  /* Single step */
	FixPerRegisters(task);
}

void clear_single_step(struct task_struct *task)
{
	per_struct *per_info=
			(per_struct *)&task->tss.per_info;

	per_info->single_step=0;
	FixPerRegisters(task);
}



/*
 * This routine gets a long from any process space by following the page
 * tables. NOTE! You should check that the long isn't on a page boundary,
 * and that it is in the task area before calling this: this routine does
 * no checking.
 *
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
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace[1]: bad page directory %lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return 0;
	}
	pgmiddle = pmd_offset(pgdir,addr);
	if (pmd_none(*pgmiddle)) {
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace[3]: bad pmd %lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return 0;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, 0);
		goto repeat;
	}
	page = pte_page(*pgtable);
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (MAP_NR(page) >= max_mapnr)
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
static void put_long(struct task_struct * tsk, struct vm_area_struct * vma,
		     unsigned long addr, unsigned long data)
{
	pgd_t *pgdir;
	pmd_t *pgmiddle;
	pte_t *pgtable;
	unsigned long page;
		
repeat:
	pgdir = pgd_offset(vma->vm_mm, addr);
	if (!pgd_present(*pgdir)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pgd_bad(*pgdir)) {
		printk("ptrace[2]: bad page directory %lx\n", pgd_val(*pgdir));
		pgd_clear(pgdir);
		return;
	}
	pgmiddle = pmd_offset(pgdir,addr);
	if (pmd_none(*pgmiddle)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
	if (pmd_bad(*pgmiddle)) {
		printk("ptrace[4]: bad pmd %lx\n", pmd_val(*pgmiddle));
		pmd_clear(pgmiddle);
		return;
	}
	pgtable = pte_offset(pgmiddle, addr);
	if (!pte_present(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
	page = pte_page(*pgtable);
	if (!pte_write(*pgtable)) {
		handle_mm_fault(tsk, vma, addr, 1);
		goto repeat;
	}
/* this is a hack for non-kernel-mapped video buffers and similar */
	if (MAP_NR(page) < max_mapnr) {
		unsigned long phys_addr = page + (addr & ~PAGE_MASK);
		*(unsigned long *) phys_addr = data;
		flush_icache_range(phys_addr, phys_addr+4);
	}
/* we're bypassing pagetables, so we have to set the dirty bit ourselves */
/* this should also re-instate whatever read-only mode there was before */
	set_pte(pgtable, pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	flush_tlb_all();
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
	struct vm_area_struct * vma;
	addr=ADDR_BITS_REMOVE(addr);
	vma= find_extend_vma(tsk, addr);
     
	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_low = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_low = vma->vm_next;
			if (!vma_low || vma_low->vm_start != vma->vm_end)
				return -EIO;
		}
		high = get_long(tsk, vma,addr & ~(sizeof(long)-1));
		low = get_long(tsk, vma_low,(addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 3:
				low >>= 8;
				low |= high << 24;
				break;
			case 2:
				low >>= 16;
				low |= high << 16;
				break;
			case 1:
				low >>= 24;
				low |= high << 8;
				break;
		}
		*result = low;
	} else
		*result = get_long(tsk, vma,addr);
	return 0;
}

/*
 * This routine checks the page boundaries, and that the offset is
 * within the task area. It then calls put_long() to write a long.
 */
static int write_long(struct task_struct * tsk, unsigned long addr,
	unsigned long data)
{
	struct vm_area_struct * vma;

	addr=ADDR_BITS_REMOVE(addr);
	vma = find_extend_vma(tsk, addr);

	if (!vma)
		return -EIO;
	if ((addr & ~PAGE_MASK) > PAGE_SIZE-sizeof(long)) {
		unsigned long low,high;
		struct vm_area_struct * vma_low = vma;

		if (addr + sizeof(long) >= vma->vm_end) {
			vma_low = vma->vm_next;
			if (!vma_low || vma_low->vm_start != vma->vm_end)
				return -EIO;
		}
		high = get_long(tsk, vma,addr & ~(sizeof(long)-1));
		low = get_long(tsk, vma_low,(addr+sizeof(long)) & ~(sizeof(long)-1));
		switch (addr & (sizeof(long)-1)) {
			case 0: /* shouldn't happen, but safety first */
				high = data;
				break;
			case 3:
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
			case 1:
				low &= 0x00ffffff;
				low |= data << 24;
				high &= ~0xffffff;
				high |= data >> 8;
				break;
		}
		put_long(tsk, vma,addr & ~(sizeof(long)-1),high);
		put_long(tsk, vma_low,(addr+sizeof(long)) & ~(sizeof(long)-1),low);
	} else
		put_long(tsk, vma,addr,data);
	return 0;
}


int ptrace_usercopy(addr_t realuseraddr,addr_t copyaddr,int len,int tofromuser,int writeuser,u32 mask)
{
	u32  tempuser;
	int  retval=0;
	
	if(writeuser&&realuseraddr==(addr_t)NULL)
		return(0);
	if(mask!=0xffffffff)
	{
		tempuser=*((u32 *)realuseraddr);
		if(!writeuser)
		{
			tempuser&=mask;
			realuseraddr=(addr_t)&tempuser;
		}
	}
	if(tofromuser)
	{
		if(writeuser)
		{
			retval=copy_from_user((void *)realuseraddr,(void *)copyaddr,len);
		}
		else
		{
			if(realuseraddr==(addr_t)NULL)
				retval=(clear_user((void *)copyaddr,len)==-EFAULT ? -EIO:0);
			else
				retval=(copy_to_user((void *)copyaddr,(void *)realuseraddr,len)==-EFAULT ? -EIO:0);
		}      
	}
	else
	{
		if(writeuser)
			memcpy((void *)realuseraddr,(void *)copyaddr,len);
		else
			memcpy((void *)copyaddr,(void *)realuseraddr,len);
	}
	if(mask!=0xffffffff&&writeuser)
			(*((u32 *)realuseraddr))=(((*((u32 *)realuseraddr))&mask)|(tempuser&~mask));
	return(retval);
}

int copy_user(struct task_struct *task,saddr_t useraddr,addr_t copyaddr,int len,int tofromuser,int writingtouser)
{
	int copylen=0,copymax;
	addr_t  realuseraddr;
	saddr_t enduseraddr=useraddr+len;
	
	u32 mask;

	if (useraddr < 0 || enduseraddr > sizeof(struct user)||
	   (useraddr < PT_ENDREGS && (useraddr&3))||
	   (enduseraddr < PT_ENDREGS && (enduseraddr&3)))
		return (-EIO);
	while(len>0)
	{
		mask=0xffffffff;
		if(useraddr<PT_FPC)
		{
			realuseraddr=(addr_t)&(((u8 *)task->tss.regs)[useraddr]);
			if(useraddr<PT_PSWMASK)
			{
				copymax=PT_PSWMASK;
			}
			else if(useraddr<(PT_PSWMASK+4))
			{
				copymax=(PT_PSWMASK+4);
				if(writingtouser)
					mask=PSW_MASK_DEBUGCHANGE;
			}
			else if(useraddr<(PT_PSWADDR+4))
			{
				copymax=PT_PSWADDR+4;
				mask=PSW_ADDR_DEBUGCHANGE;
			}
			else
				copymax=PT_FPC;
			
		}
		else if(useraddr<(PT_FPR15_LO+4))
		{
			copymax=(PT_FPR15_LO+4);
			realuseraddr=(addr_t)&(((u8 *)&task->tss.fp_regs)[useraddr-PT_FPC]);
		}
		else if(useraddr<sizeof(user_regs_struct))
		{
			copymax=sizeof(user_regs_struct);
			realuseraddr=(addr_t)&(((u8 *)&task->tss.per_info)[useraddr-PT_CR_9]);
		}
		else 
		{
			copymax=sizeof(struct user);
			realuseraddr=(addr_t)NULL;
		}
		copylen=copymax-useraddr;
		copylen=(copylen>len ? len:copylen);
		if(ptrace_usercopy(realuseraddr,copyaddr,copylen,tofromuser,writingtouser,mask))
			return (-EIO);
		copyaddr+=copylen;
		len-=copylen;
		useraddr+=copylen;
	}
	FixPerRegisters(task);
	return(0);
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret = -EPERM;
	unsigned long tmp;
	ptrace_area   parea; 
	lock_kernel();
	if (request == PTRACE_TRACEME) 
	{
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child = find_task_by_pid(pid)))
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;
	if (request == PTRACE_ATTACH) 
	{
		if (child == current)
			goto out;
		if ((!child->dumpable ||
		     (current->uid != child->euid) ||
		     (current->uid != child->uid) ||
		     (current->gid != child->egid) ||
		     (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->ptrace & PT_PTRACED)
			goto out;
		child->ptrace |= PT_PTRACED;
		if (child->p_pptr != current) 
		{
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	// printk("child=%lX child->flags=%lX",child,child->flags);
	/* I added child!=current line so we can get the */
	/* ieee_instruction_pointer from the user structure DJB */
	if(child!=current)
	{
		if (!(child->ptrace & PT_PTRACED))
			goto out;
		if (child->state != TASK_STOPPED) 
		{
			if (request != PTRACE_KILL)
				goto out;
		}
		if (child->p_pptr != current)
			goto out;
	}
	switch (request) 
	{
		/* If I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: 
		down(&child->mm->mmap_sem);
		ret = read_long(child, addr, &tmp);
		up(&child->mm->mmap_sem);
		if (ret < 0)
			break;
		ret=put_user(tmp, (unsigned long *) data);
		break;

		/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR:
		ret=copy_user(child,addr,data,sizeof(unsigned long),TRUE,FALSE);
		break;

		/* If I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		down(&child->mm->mmap_sem);
		ret = write_long(child,addr,data);
		up(&child->mm->mmap_sem);
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret=copy_user(child,addr,(addr_t)&data,sizeof(unsigned long),FALSE,TRUE);
		break;

	case PTRACE_SYSCALL: 	/* continue and stop at next (return from) syscall */
	case PTRACE_CONT: 	 /* restart after signal. */
		ret = -EIO;
		if ((unsigned long) data >= _NSIG)
			break;
		if (request == PTRACE_SYSCALL)
			child->ptrace |= PT_TRACESYS;
		else
			child->ptrace &= ~PT_TRACESYS;
		child->exit_code = data;
		/* make sure the single step bit is not set. */
		clear_single_step(child);
		wake_up_process(child);
		ret = 0;
		break;

/*
 * make the child exit.  Best I can do is send it a sigkill. 
 * perhaps it should be put in the status that it wants to 
 * exit.
 */
	case PTRACE_KILL:
		ret = 0;
		if (child->state == TASK_ZOMBIE) /* already dead */
			break;
		child->exit_code = SIGKILL;
		clear_single_step(child);
		wake_up_process(child);
		/* make sure the single step bit is not set. */
		break;

	case PTRACE_SINGLESTEP:  /* set the trap flag. */
		ret = -EIO;
		if ((unsigned long) data >= _NSIG)
			break;
		child->ptrace &= ~PT_TRACESYS;
		child->exit_code = data;
		set_single_step(child);
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;

	case PTRACE_DETACH:  /* detach a process that was attached. */
		ret = -EIO;
		if ((unsigned long) data >= _NSIG)
			break;
		child->ptrace &= ~(PT_PTRACED|PT_TRACESYS);
		wake_up_process(child);
		child->exit_code = data;
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		/* make sure the single step bit is not set. */
		clear_single_step(child);
		ret = 0;
		break;
	case PTRACE_PEEKUSR_AREA:
	case PTRACE_POKEUSR_AREA:
		if((ret=copy_from_user(&parea,(void *)addr,sizeof(parea)))==0)  
		   ret=copy_user(child,parea.kernel_addr,parea.process_addr,
				 parea.len,TRUE,(request==PTRACE_POKEUSR_AREA));
		break;
	default:
		ret = -EIO;
		break;
	}
 out:
	unlock_kernel();
	return ret;
}

asmlinkage void syscall_trace(void)
{
	lock_kernel();
	if ((current->ptrace & (PT_PTRACED|PT_TRACESYS))
	    != (PT_PTRACED|PT_TRACESYS))
		goto out;
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
 out:
	unlock_kernel();
}
