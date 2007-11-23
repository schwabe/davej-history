/*
 *  include/asm-s390/sigcontext.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 */

#ifndef _ASM_S390_SIGCONTEXT_H
#define _ASM_S390_SIGCONTEXT_H

#include <asm/ptrace.h>


/*
  Has to be at least _NSIG_WORDS from asm/signal.h
 */
#define _SIGCONTEXT_NSIG      64
#define _SIGCONTEXT_NSIG_BPW  32
/* Size of stack frame allocated when calling signal handler. */
#define __SIGNAL_FRAMESIZE      STACK_FRAME_OVERHEAD
#define _SIGCONTEXT_NSIG_WORDS  (_SIGCONTEXT_NSIG / _SIGCONTEXT_NSIG_BPW)
#define SIGMASK_COPY_SIZE       (sizeof(unsigned long)*_SIGCONTEXT_NSIG_WORDS)
struct sigcontext_struct {
	int		signal;
	unsigned long	handler;
	user_regs_struct *regs;
	unsigned long	oldmask[_SIGCONTEXT_NSIG_WORDS];
};

#endif
