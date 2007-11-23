
/*
   md.h : Multiple Devices driver compatibility layer for Linux 2.0/2.2
          Copyright (C) 1998 Ingo Molnar
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#include <linux/version.h>

#ifndef _MD_COMPATIBLE_H
#define _MD_COMPATIBLE_H

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#if LINUX_VERSION_CODE < LinuxVersionCode(2,1,0)

/* 000 */
#define md__get_free_pages(x,y) __get_free_pages(x,y,GFP_KERNEL)

#ifdef __i386__
/* 001 */
extern __inline__ int md_cpu_has_mmx(void)
{
	return x86_capability & 0x00800000;
}
#endif

/* 002 */
#define md_clear_page(page)        memset((void *)(page), 0, PAGE_SIZE)

/* 003 */
/*
 * someone please suggest a sane compatibility layer for modules
 */
#define MD_EXPORT_SYMBOL(x)

/* 004 */
static inline unsigned long
md_copy_from_user(void *to, const void *from, unsigned long n)
{
	int err;

	err = verify_area(VERIFY_READ,from,n);
	if (!err)
		memcpy_fromfs(to, from, n);
	return err; 
}

/* 005 */
extern inline unsigned long
md_copy_to_user(void *to, const void *from, unsigned long n)
{
	int err;

	err = verify_area(VERIFY_WRITE,to,n);
	if (!err)
		memcpy_tofs(to, from, n);
	return err; 
}

/* 006 */
#define md_put_user(x,ptr)						\
({									\
	int __err;							\
									\
	__err = verify_area(VERIFY_WRITE,ptr,sizeof(*ptr));		\
	if (!__err)							\
		put_user(x,ptr);					\
	__err;								\
})

/* 007 */
extern inline int md_capable_admin(void)
{
	return suser();
}
 
/* 008 */
#define MD_FILE_TO_INODE(file) ((file)->f_inode)

/* 009 */
extern inline void md_flush_signals (void)
{
	current->signal = 0;
}
 
/* 010 */
#define __S(nr) (1<<((nr)-1))
extern inline void md_init_signals (void)
{
        current->exit_signal = SIGCHLD;
        current->blocked = ~(__S(SIGKILL));
}
#undef __S

/* 011 */
extern inline unsigned long md_signal_pending (struct task_struct * tsk)
{
	return (tsk->signal & ~tsk->blocked);
}

/* 012 */
#define md_set_global_readahead(x) read_ahead[MD_MAJOR] = MD_READAHEAD

/* 013 */
#define md_mdelay(n) (\
	{unsigned long msec=(n); while (msec--) udelay(1000);})

/* 014 */
#define MD_SYS_DOWN 0
#define MD_SYS_HALT 0
#define MD_SYS_POWER_OFF 0

/* 015 */
#define md_register_reboot_notifier(x)

/* 016 */
extern __inline__ unsigned long
md_test_and_set_bit(int nr, void * addr)
{
	unsigned long flags;
	unsigned long oldbit;

	save_flags(flags);
	cli();
	oldbit = test_bit(nr,addr);
	set_bit(nr,addr);
	restore_flags(flags);
	return oldbit;
}

/* 017 */
extern __inline__ unsigned long
md_test_and_clear_bit(int nr, void * addr)
{
	unsigned long flags;
	unsigned long oldbit;

	save_flags(flags);
	cli();
	oldbit = test_bit(nr,addr);
	clear_bit(nr,addr);
	restore_flags(flags);
	return oldbit;
}

/* 018 */
#define md_atomic_read(x) (*(volatile int *)(x))
#define md_atomic_set(x,y) (*(volatile int *)(x) = (y))

/* 019 */
extern __inline__ void md_lock_kernel (void)
{
#if __SMP__
	lock_kernel();
	syscall_count++;
#endif
}

extern __inline__ void md_unlock_kernel (void)
{
#if __SMP__
	syscall_count--;
	unlock_kernel();
#endif
}
/* 020 */

#define md__init
#define md__initdata
#define md__initfunc(__arginit) __arginit

/* 021 */

/* 022 */

struct md_list_head {
	struct md_list_head *next, *prev;
};

#define MD_LIST_HEAD(name) \
	struct md_list_head name = { &name, &name }

#define MD_INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

static __inline__ void md__list_add(struct md_list_head * new,
	struct md_list_head * prev,
	struct md_list_head * next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static __inline__ void md_list_add(struct md_list_head *new,
						struct md_list_head *head)
{
	md__list_add(new, head, head->next);
}

static __inline__ void md__list_del(struct md_list_head * prev,
					struct md_list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

static __inline__ void md_list_del(struct md_list_head *entry)
{
	md__list_del(entry->prev, entry->next);
}

static __inline__ int md_list_empty(struct md_list_head *head)
{
	return head->next == head;
}

#define md_list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/* 023 */

static __inline__ signed long md_schedule_timeout(signed long timeout)
{
	current->timeout = jiffies + timeout;
	schedule();
	return 0;
}

/* 024 */
#define md_need_resched(tsk) (need_resched)

/* 025 */
typedef struct { int gcc_is_buggy; } md_spinlock_t;
#define MD_SPIN_LOCK_UNLOCKED (md_spinlock_t) { 0 }

#define md_spin_lock_irq cli
#define md_spin_unlock_irq sti
#define md_spin_unlock_irqrestore(x,flags) restore_flags(flags)
#define md_spin_lock_irqsave(x,flags) do { save_flags(flags); cli(); } while (0)

/* END */

#else

#include <linux/reboot.h>
#include <linux/vmalloc.h>

/* 000 */
#define md__get_free_pages(x,y) __get_free_pages(x,y)

#ifdef __i386__
/* 001 */
extern __inline__ int md_cpu_has_mmx(void)
{
	return boot_cpu_data.x86_capability & X86_FEATURE_MMX;
}
#endif

/* 002 */
#define md_clear_page(page)        clear_page(page)

/* 003 */
#define MD_EXPORT_SYMBOL(x) EXPORT_SYMBOL(x)

/* 004 */
#define md_copy_to_user(x,y,z) copy_to_user(x,y,z)

/* 005 */
#define md_copy_from_user(x,y,z) copy_from_user(x,y,z)

/* 006 */
#define md_put_user put_user

/* 007 */
extern inline int md_capable_admin(void)
{
	return capable(CAP_SYS_ADMIN);
}

/* 008 */
#define MD_FILE_TO_INODE(file) ((file)->f_dentry->d_inode)

/* 009 */
extern inline void md_flush_signals (void)
{
	spin_lock(&current->sigmask_lock);
	flush_signals(current);
	spin_unlock(&current->sigmask_lock);
}
 
/* 010 */
extern inline void md_init_signals (void)
{
        current->exit_signal = SIGCHLD;
        siginitsetinv(&current->blocked, sigmask(SIGKILL));
}

/* 011 */
#define md_signal_pending signal_pending

/* 012 */
extern inline void md_set_global_readahead(int * table)
{
	max_readahead[MD_MAJOR] = table;
}

/* 013 */
#define md_mdelay(x) mdelay(x)

/* 014 */
#define MD_SYS_DOWN SYS_DOWN
#define MD_SYS_HALT SYS_HALT
#define MD_SYS_POWER_OFF SYS_POWER_OFF

/* 015 */
#define md_register_reboot_notifier register_reboot_notifier

/* 016 */
#define md_test_and_set_bit test_and_set_bit

/* 017 */
#define md_test_and_clear_bit test_and_clear_bit

/* 018 */
#define md_atomic_read atomic_read
#define md_atomic_set atomic_set

/* 019 */
#define md_lock_kernel lock_kernel
#define md_unlock_kernel unlock_kernel

/* 020 */

#include <linux/init.h>

#define md__init __init
#define md__initdata __initdata
#define md__initfunc(__arginit) __initfunc(__arginit)

/* 021 */


/* 022 */

#define md_list_head list_head
#define MD_LIST_HEAD(name) LIST_HEAD(name)
#define MD_INIT_LIST_HEAD(ptr) INIT_LIST_HEAD(ptr)
#define md_list_add list_add
#define md_list_del list_del
#define md_list_empty list_empty

#define md_list_entry(ptr, type, member) list_entry(ptr, type, member)

/* 023 */

#define md_schedule_timeout schedule_timeout

/* 024 */
#define md_need_resched(tsk) ((tsk)->need_resched)

/* 025 */
#define md_spinlock_t spinlock_t
#define MD_SPIN_LOCK_UNLOCKED SPIN_LOCK_UNLOCKED

#define md_spin_lock_irq spin_lock_irq
#define md_spin_unlock_irq spin_unlock_irq
#define md_spin_unlock_irqrestore spin_unlock_irqrestore
#define md_spin_lock_irqsave spin_lock_irqsave

/* END */

#endif

#endif _MD_COMPATIBLE_H

