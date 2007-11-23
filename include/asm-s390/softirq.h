/*
 *  include/asm-s390/softirq.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/softirq.h"
 */

#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#ifndef __LINUX_SMP_H
#include <linux/smp.h>
#endif

#include <asm/atomic.h>
#include <asm/hardirq.h>
#include <asm/lowcore.h>

#define get_active_bhs()        (bh_mask & bh_active)
#define clear_active_bhs(x)     atomic_clear_mask((x),(atomic_t*) &bh_active)

#ifdef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t global_bh_lock;
extern atomic_t global_bh_count;
extern spinlock_t s390_bh_lock;

extern void synchronize_bh(void);

static inline void start_bh_atomic(void)
{
	atomic_inc(&global_bh_lock);
	synchronize_bh();
}

static inline void end_bh_atomic(void)
{
	atomic_dec(&global_bh_lock);
}

/* These are for the IRQs testing the lock */
static inline int softirq_trylock(int cpu)
{
	if (!test_and_set_bit(0,&global_bh_count)) {
		if (atomic_read(&global_bh_lock) == 0) {
                        atomic_inc(&safe_get_cpu_lowcore(cpu).local_bh_count);
			return 1;
		}
		clear_bit(0,&global_bh_count);
	}
	return 0;
}

static inline void softirq_endlock(int cpu)
{
        atomic_dec(&safe_get_cpu_lowcore(cpu).local_bh_count);
	clear_bit(0,&global_bh_count);
}

#else

extern inline void start_bh_atomic(void)
{
        atomic_inc(&S390_lowcore.local_bh_count);
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
        atomic_dec(&S390_lowcore.local_bh_count);
}

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	(atomic_compare_and_swap(0,1,&S390_lowcore.local_bh_count) == 0)

#define softirq_endlock(cpu)   atomic_set(&S390_lowcore.local_bh_count,0);
#define synchronize_bh()       barrier()

#endif	/* SMP */

extern inline void init_bh(int nr, void (*routine)(void))
{
        unsigned long flags;

        bh_base[nr] = routine;
        atomic_set(&bh_mask_count[nr], 0);

        spin_lock_irqsave(&s390_bh_lock, flags);
        bh_mask |= 1 << nr;
        spin_unlock_irqrestore(&s390_bh_lock, flags);
}

extern inline void remove_bh(int nr)
{
        unsigned long flags;

        spin_lock_irqsave(&s390_bh_lock, flags);
        bh_mask &= ~(1 << nr);
        spin_unlock_irqrestore(&s390_bh_lock, flags);

        synchronize_bh();
        bh_base[nr] = NULL;
}

extern inline void mark_bh(int nr)
{
        set_bit(nr, &bh_active);
}

/*
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
extern inline void disable_bh(int nr)
{
        unsigned long flags;

        spin_lock_irqsave(&s390_bh_lock, flags);
        bh_mask &= ~(1 << nr);
        atomic_inc(&bh_mask_count[nr]);
        spin_unlock_irqrestore(&s390_bh_lock, flags);
        synchronize_bh();
}

extern inline void enable_bh(int nr)
{
        unsigned long flags;

        spin_lock_irqsave(&s390_bh_lock, flags);
        if (atomic_dec_and_test(&bh_mask_count[nr]))
                bh_mask |= 1 << nr;
        spin_unlock_irqrestore(&s390_bh_lock, flags);
}

#endif	/* __ASM_SOFTIRQ_H */







