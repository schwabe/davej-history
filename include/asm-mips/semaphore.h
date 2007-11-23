/* $Id: semaphore.h,v 1.5.2.2 1999/06/17 12:09:42 ralf Exp $
 *
 * SMP- and interrupt-safe semaphores..
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright 1996  Linus Torvalds
 * (C) Copyright 1998, 1999  Ralf Baechle
 */
#ifndef __ASM_MIPS_SEMAPHORE_H
#define __ASM_MIPS_SEMAPHORE_H

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/spinlock.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), ATOMIC_INIT(0), NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), ATOMIC_INIT(0), NULL })

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

extern spinlock_t semaphore_wake_lock;

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

extern inline void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

/*
 * down_trylock returns 0 on success, 1 if we failed to get the lock.
 *
 * We must manipulate count and waking simultaneously and atomically.
 * Do this by using ll/sc on the pair of 32-bit words.
 */
extern inline int down_trylock(struct semaphore * sem)
{
	long ret, tmp, tmp2, sub;

#ifdef __MIPSEB__
	__asm__ __volatile__("
			.set	mips3
		0:	lld	%1, %4
			dli	%3, 0x0000000100000000
			sltu	%0, %1, $0

			bltz	%1, 1f
			move	%3, $0
		1:

			sltu	%2, %1, $0
			and	%0, %0, %2
			bnez	%0, 2f

			subu	%0, %3
			scd	%1, %4

			beqz	%1, 0b
		2:

			.set	mips0"
		: "=&r"(ret), "=&r"(tmp), "=&r"(tmp2), "=&r"(sub)
		: "m"(*sem)
		: "memory");
#endif

#ifdef __MIPSEL__
#error "FIXME: down_trylock doesn't support little endian machines yet."
#endif

	return ret;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
extern inline void up(struct semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

#endif /* __ASM_MIPS_SEMAPHORE_H */
