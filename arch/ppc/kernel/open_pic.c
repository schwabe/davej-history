/*
 * open_pic.c
 *
 * Common support routines for platforms with an OpenPIC interrupt controller
 *
 */

#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/openpic.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include "open_pic.h"
#include "i8259.h"

extern volatile unsigned char *chrp_int_ack_special;

void open_pic_do_IRQ(struct pt_regs *regs, int cpu, int isfake)
{
	int irq;
        int openpic_eoi_done = 0;

#ifdef __SMP__
        {
                unsigned int loops = 1000000;
                while (test_bit(0, &global_irq_lock)) {
                        if (smp_processor_id() == global_irq_holder) {
                                printk("uh oh, interrupt while we hold global irq lock!\n");
#ifdef CONFIG_XMON
                                xmon(0);
#endif
                                break;
                        }
                        if (loops-- == 0) {
                                printk("do_IRQ waiting for irq lock (holder=%d)\n", global_irq_holder);
#ifdef CONFIG_XMON
                                xmon(0);
#endif
                        }
                }
        }
#endif /* __SMP__ */
	
        irq = openpic_irq(smp_processor_id());
	/* make sure open_pic.irq_offset is set to something!
	 * do we really need the _MACH_Pmac test??
	 */
        if (!(_machine == _MACH_Pmac) && (irq == open_pic.irq_offset))
        {
                /*
                 * This magic address generates a PCI IACK cycle.
                 *
                 * This should go in the above mask/ack code soon. -- Cort
                 */
		if ( chrp_int_ack_special )
			irq = *chrp_int_ack_special;
#ifndef CONFIG_PMAC
		else
			irq = i8259_irq(0);
#endif
                /*
                 * Acknowledge as soon as possible to allow i8259
                 * interrupt nesting                         */
                openpic_eoi(smp_processor_id());
                openpic_eoi_done = 1;
        }
        if (irq == OPENPIC_VEC_SPURIOUS)
        {
                /*
                 * Spurious interrupts should never be
                 * acknowledged
                 */
                ppc_spurious_interrupts++;
                openpic_eoi_done = 1;
		goto out;
        }

        if (irq < 0)
        {
                printk(KERN_DEBUG "Bogus interrupt %d from PC = %lx\n",
                       irq, regs->nip);
                ppc_spurious_interrupts++;
        }
	else
        {
		ppc_irq_dispatch_handler( regs, irq );
	}
out:
        if (!openpic_eoi_done)
                openpic_eoi(smp_processor_id());
}

#ifdef __SMP__
void openpic_ipi_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	smp_message_recv(cpl-OPENPIC_VEC_IPI);
}
#endif /* __SMP__ */


struct hw_interrupt_type open_pic = {
	" OpenPIC  ",
	NULL,
	NULL,
	NULL,
	openpic_enable_irq,
	openpic_disable_irq,
	/* Theorically, the mask&ack should be NULL for OpenPIC. However, doing
	 * so shows tons of bogus interrupts coming in.
	 * This problem is apparently due to the common code always calling
	 * unmask(). I apparently (need more test) fixed it in the 2.4 new IRQ
	 * management by cleanly implementing the handler's end() function, so
	 * neither mask nor unmask are needed. In the meantime, the fix below will
	 * work for 2.2 -Benh
	 *
	 * Hopefully this will fix my bogus interrups on MTX
	 * I merged everthing together so we don't have the same code in three
	 * places. This might cause stability problems, but I'd rather
	 * get it right once than three different times because someone forgot
	 * to make the same change to PReP or something --Troy
	 */
	openpic_disable_irq,
	0
};
