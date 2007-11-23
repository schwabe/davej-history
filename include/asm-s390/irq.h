/*
 *  include/asm-s390/irq.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 *  Derived from "include/asm-i386/irq.h"
 *    Copyright (C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 */

#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#define TIMER_IRQ 0x1005

/*
 * How many IRQ's for S390 ?!?
 */
#define NR_IRQS 1024

extern int disable_irq(unsigned int);
extern int enable_irq(unsigned int);

#endif /* _ASM_IRQ_H */
