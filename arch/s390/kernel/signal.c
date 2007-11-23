/*
 *  arch/s390/kernel/signal.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  based on PPC & i386 signal handling by  Linus Torvalds &  Gary Thomas 
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#if CONFIG_BINFMT_TOC
#include <linux/toc.h>
#endif
/*
 * These are the flags in the MSR that the user is allowed to change
 * by modifying the saved value of the MSR on the stack.  SE and BE
 * should not be in this list since gdb may want to change these.  I.e,
 * you should be able to step out of a signal handler to see what
 * instruction executes next after the signal handler completes.
 * Alternately, if you stepped into a signal handler, you should be
 * able to continue 'til the next breakpoint from within the signal
 * handler, even if the handler returns.
 */


#define __390_TO_DO__    0
#define __390_NEEDED__   0 
#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

int do_signal(struct pt_regs *reg,sigset_t *oldset);
extern int sys_wait4(pid_t pid, unsigned long *stat_addr,
		     int options, unsigned long *ru);





/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(struct pt_regs * regs,int history0, int history1, old_sigset_t mask)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	regs->gprs[2] = -EINTR;

	while (1) 
	  {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	  }
}


//
// N.B. This hasn't been tested it just compiles easily
//
asmlinkage int sys_rt_sigsuspend(struct pt_regs * regs,sigset_t *unewset, size_t sigsetsize)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	regs->gprs[2]	= -EINTR;
	while (1) 
	  {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	  }
}

asmlinkage int sys_rt_sigreturn(unsigned long __unused)
{
	printk("sys_rt_sigreturn(): %s/%d not yet implemented.\n",
	       current->comm,current->pid);
	do_exit(SIGSEGV);
}

asmlinkage int
sys_sigaltstack(const stack_t *uss, stack_t *uoss)
{
	struct pt_regs *regs = (struct pt_regs *) &uss;
	return do_sigaltstack(uss, uoss, regs->gprs[15]);
}


asmlinkage int sys_sigaction(int sig, const struct old_sigaction *act,struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) 
	  {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	  }

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) 
	  {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||


		    __put_user((u32)old_ka.sa.sa_handler,&oact->sa_handler) ||
		    __put_user((u32)old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	  }
	return ret;
}

/*
 * When we have signals to deliver, we set up on the
 * user stack, going down from the original stack pointer:
 *	a sigregs struct
 *	one or more sigcontext structs
 *	a gap of __SIGNAL_FRAMESIZE bytes
 *
 * Each of these things must be a multiple of 16 bytes in size.
 *
 * XXX ultimately we will have to stack up a siginfo and ucontext
 * for each rt signal.
 */

struct sigregs {
	user_regs_struct user_regs;

        unsigned short   tramp[1];
        /* Programs using the rs6000/xcoff abi can save up to 19 gp regs
           and 18 fp regs below sp before decrementing it. */
};   


int sys_sigreturn(struct pt_regs *regs)
{
	struct sigcontext_struct *sc, sigctx;
	struct sigregs *sr;
	int ret;
        user_regs_struct saved_regs;  
	sigset_t set;
	unsigned long prevsp;
	routine_descriptor tempdes;

	sc = (struct sigcontext_struct *)(regs->gprs[15] + __SIGNAL_FRAMESIZE);
	if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
		goto badframe;
	memcpy(&set.sig[0],&sigctx.oldmask[0],SIGMASK_COPY_SIZE);
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	sc++;			/* Look at next sigcontext */
	if (sc == (struct sigcontext_struct *)(sigctx.regs)) {
		/* Last stacked signal - restore registers */
		sr = (struct sigregs *) sigctx.regs;
		if (copy_from_user(&saved_regs, &sr->user_regs,
				   sizeof(sr->user_regs)))
			goto badframe;
		saved_regs.psw.mask=(regs->psw.mask&~PSW_MASK_DEBUGCHANGE)
			|(saved_regs.psw.mask&PSW_MASK_DEBUGCHANGE);
		saved_regs.psw.addr=(regs->psw.addr&~PSW_ADDR_DEBUGCHANGE)
			|(saved_regs.psw.addr&PSW_ADDR_DEBUGCHANGE);
		memcpy(regs,&saved_regs,sizeof(s390_regs));
		restore_fp_regs(&saved_regs.fp_regs);
		ret = regs->gprs[2];

	} else {
		/* More signals to go */
		regs->gprs[15] = (unsigned long)sc - __SIGNAL_FRAMESIZE;
		if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
			goto badframe;
		sr = (struct sigregs *) sigctx.regs;
		regs->gprs[2] = ret = sigctx.signal;
#if DEBUG_SIG
		regs->gprs[3] = (unsigned long) sr; // My best guess says this is used for debugging only DJB
#endif
		regs->gprs[14] = (unsigned long) sr->tramp; // return address
		
		if(uses_routine_descriptors())
		{
		   if(copy_from_user(&tempdes,(char *)sigctx.handler,sizeof(tempdes)))
		      goto badframe;
		   fix_routine_descriptor_regs(&tempdes,regs);
		}
		else
		   regs->psw.addr = FIX_PSW(sigctx.handler);

		if (get_user(prevsp, &sr->user_regs.gprs[15])
		    || put_user(prevsp, (unsigned long *) regs->gprs[15]))
			goto badframe;
	}
	return ret;

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
}	


/*
 * Set up a signal frame.
 */
void setup_frame(struct pt_regs *regs, struct sigregs *frame,unsigned long newsp)
{
	struct sigcontext_struct *sc = (struct sigcontext_struct *) newsp;
	s390_fp_regs fpregs;

	routine_descriptor tempdes,*tempdesptr;

	if (verify_area(VERIFY_WRITE, frame, sizeof(*frame)))
		goto badframe;
	save_fp_regs(&fpregs);
	if (__copy_to_user(&frame->user_regs, regs, sizeof(s390_regs))
	    || __copy_to_user(&frame->user_regs.fp_regs,&fpregs,sizeof(fpregs))
	    || __put_user(0x0A77, &frame->tramp[0]))    /* SVC 0x77 */
		goto badframe;
	newsp -= __SIGNAL_FRAMESIZE;
	if (put_user(regs->gprs[15], (unsigned long *)newsp) || get_user(regs->gprs[2], &sc->signal))
		goto badframe;

	if(uses_routine_descriptors())
	{
	    if(get_user(tempdesptr, (routine_descriptor **)& sc->handler)
	       || copy_from_user(&tempdes,tempdesptr,sizeof(tempdes)))
	       goto badframe;
	    fix_routine_descriptor_regs(&tempdes,regs);
	}
	else
	{
	   if(get_user(regs->psw.addr, (void **) & sc->handler))
		goto badframe;
	   regs->psw.addr=FIX_PSW(regs->psw.addr);
	}
	regs->gprs[15] = newsp;
#if DEBUG_SIG
	regs->gprs[3] = (unsigned long) frame;
#endif
	regs->gprs[14] = (unsigned long) frame->tramp;

	return;

badframe:
#if DEBUG_SIG
	printk("badframe in setup_frame, regs=%p frame=%p newsp=%lx\n",
	       regs, frame, newsp);
#endif
	lock_kernel();
	do_exit(SIGSEGV);
}

/*
 * OK, we're invoking a handler
 */
void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs,
	      unsigned long *newspp, unsigned long frame)
{
	struct sigcontext_struct *sc;

	if (regs->trap == 0x20) /* System Call! */
	{
		switch(regs->gprs[2])
		{
		case -ERESTARTNOHAND:
				regs->gprs[2] = -EINTR;
				break;
		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) 
			{
				regs->gprs[2] = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
			regs->gprs[2] = regs->orig_gpr2;
			regs->psw.addr -= 2;	      
                        /* Back up & retry system call */
		}
	}
	/* Put another sigcontext on the stack */
	*newspp -= sizeof(*sc);
	sc = (struct sigcontext_struct *) *newspp;
	if (verify_area(VERIFY_WRITE, sc, sizeof(*sc)))
		goto badframe;

	if (__put_user((unsigned long) ka->sa.sa_handler, &sc->handler)
	    || copy_to_user(&sc->oldmask[0],&oldset->sig[0],SIGMASK_COPY_SIZE)
	    || __put_user((u32)((struct pt_regs *)frame), &sc->regs)
	    || __put_user(sig, &sc->signal))
		goto badframe;

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
	}
	return;

badframe:
#if DEBUG_SIG
	printk("badframe in handle_signal, regs=%p frame=%lx newsp=%lx\n",
	       regs, frame, *newspp);
	printk("sc=%p sig=%d ka=%p info=%p oldset=%p\n", sc, sig, ka, info, oldset);
#endif
	lock_kernel();
	do_exit(SIGSEGV);
}


/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
int do_signal(struct pt_regs *regs,sigset_t *oldset)
{
	siginfo_t info;
	struct k_sigaction *ka;
	unsigned long frame, newsp;

	if (!oldset)
		oldset = &current->blocked;

	newsp = frame = regs->gprs[15] - sizeof(struct sigregs);

	for (;;) {
		unsigned long signr;

		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);

		if (!signr)
			break;

		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->p_pptr->pid;
				info.si_uid = current->p_pptr->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}

		ka = &current->sig->action[signr-1];
		if (ka->sa.sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* Check for SIGCHLD: it's special.  */
			while (sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}

		if (ka->sa.sa_handler == SIG_DFL) {
			int exit_code = signr;

			/* Init gets no signals it doesn't want.  */
			if (current->pid == 1)
				continue;

			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
				/* FALLTHRU */

			case SIGSTOP:
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
				lock_kernel();
				if (current->binfmt
				    && current->binfmt->core_dump
				    && current->binfmt->core_dump(signr, regs))
					exit_code |= 0x80;
				unlock_kernel();
				/* FALLTHRU */

			default:
				lock_kernel();
				sigaddset(&current->signal, signr);
				recalc_sigpending(current);
				current->flags |= PF_SIGNALED;
				do_exit(exit_code);
				/* NOTREACHED */
			}
		}

		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, ka, &info, oldset, regs, &newsp, frame);
	}

	if (
	    regs->trap == 0x20 /* System Call! */ &&
	    ((int)regs->gprs[2] == -ERESTARTNOHAND ||
	     (int)regs->gprs[2] == -ERESTARTSYS ||
	     (int)regs->gprs[2] == -ERESTARTNOINTR)) {
		regs->gprs[2] = regs->orig_gpr2;
		regs->psw.addr -= 2;	      /* Back up & retry system call */
	}

	if (newsp == frame)
		return 0;		/* no signals delivered */

	setup_frame(regs, (struct sigregs *) frame, newsp);
	return 1;
}




















