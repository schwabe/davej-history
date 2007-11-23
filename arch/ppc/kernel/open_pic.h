
#ifndef _PPC_KERNEL_OPEN_PIC_H
#define _PPC_KERNEL_OPEN_PIC_H

#include "local_irq.h"

extern struct hw_interrupt_type open_pic;

void open_pic_do_IRQ(struct pt_regs *regs, int cpu, int isfake);
void openpic_ipi_action(int cpl, void *dev_id, struct pt_regs *regs);
void openpic_enable_IPI(u_int ipi);
void do_openpic_setup_cpu(void);

#endif /* _PPC_KERNEL_OPEN_PIC_H */
