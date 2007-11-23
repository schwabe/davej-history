/*
 * include/linux/lockd/syscall.h
 *
 * This file holds all declarations for the knfsd/lockd syscall
 * interface.
 *
 */

#ifndef LOCKD_SYSCALL_H
#define LOCKD_SYSCALL_H

#include <linux/nfsd/syscall.h>

#define LOCKDCTL_SVC NFSCTL_LOCKD

#ifdef __KERNEL__
/*
 * Kernel syscall implementation.
 */
#if defined(CONFIG_LOCKD) || defined(CONFIG_LOCKD_MODULE)
extern int	lockdctl(int, void *, void *);
#endif

#endif /* __KERNEL__ */

#endif /* LOCKD_SYSCALL_H */
