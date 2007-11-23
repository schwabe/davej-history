#ifndef _ALPHA_SOFTIRQ_H
#define _ALPHA_SOFTIRQ_H

#include <linux/stddef.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>

#ifndef __SMP__
extern unsigned long local_bh_count;
#else
#define local_bh_count          (cpu_data[smp_processor_id()].bh_count)
extern spinlock_t alpha_bh_lock;
#endif

#define get_active_bhs()	(bh_mask & bh_active)

static inline void clear_active_bhs(unsigned long x)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	bic %0,%2,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (bh_active)
	:"Ir" (x), "m" (bh_active));
}

#ifdef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t global_bh_lock;
extern atomic_t global_bh_count;

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

/* These are for the irq's testing the lock */
static inline int softirq_trylock(int cpu)
{
	if (!test_and_set_bit(0,&global_bh_count)) {
		if (atomic_read(&global_bh_lock) == 0) {
			++local_bh_count;
			return 1;
		}
		clear_bit(0,&global_bh_count);
	}
	return 0;
}

static inline void softirq_endlock(int cpu)
{
	local_bh_count--;
	clear_bit(0,&global_bh_count);
}

#else

extern inline void start_bh_atomic(void)
{
	local_bh_count++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	local_bh_count--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu) \
  (local_bh_count ? 0 : (local_bh_count = 1))

#define softirq_endlock(cpu) \
  (local_bh_count = 0)

#define synchronize_bh()	do { } while (0)

#endif	/* SMP */

extern inline void init_bh(int nr, void (*routine)(void))
{
	unsigned long flags;

	bh_base[nr] = routine;
	atomic_set(&bh_mask_count[nr], 0);

	spin_lock_irqsave(&alpha_bh_lock, flags);
	bh_mask |= 1 << nr;
	spin_unlock_irqrestore(&alpha_bh_lock, flags);
}

extern inline void remove_bh(int nr)
{
	unsigned long flags;

	spin_lock_irqsave(&alpha_bh_lock, flags);
	bh_mask &= ~(1 << nr);
	spin_unlock_irqrestore(&alpha_bh_lock, flags);

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

	spin_lock_irqsave(&alpha_bh_lock, flags);
	bh_mask &= ~(1 << nr);
	atomic_inc(&bh_mask_count[nr]);
	spin_unlock_irqrestore(&alpha_bh_lock, flags);
	synchronize_bh();
}

extern inline void enable_bh(int nr)
{
	unsigned long flags;

	spin_lock_irqsave(&alpha_bh_lock, flags);
	if (atomic_dec_and_test(&bh_mask_count[nr]))
		bh_mask |= 1 << nr;
	spin_unlock_irqrestore(&alpha_bh_lock, flags);
}

#endif /* _ALPHA_SOFTIRQ_H */
