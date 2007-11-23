/*
 * linux/fs/lockd/lockd_syms.c
 *
 * Symbols exported by the lockd module.
 *
 * Authors:	Olaf Kirch (okir@monad.swb.de)
 *
 * Copyright (C) 1997 Olaf Kirch <okir@monad.swb.de>
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

#ifdef CONFIG_MODULES

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/unistd.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/xdr4.h>
#include <linux/lockd/syscall.h>

/* Start/stop the daemon */
EXPORT_SYMBOL(lockd_up);
EXPORT_SYMBOL(lockd_down);

/* NFS client entry */
EXPORT_SYMBOL(nlmclnt_proc);

/* NFS server entry points/hooks */
EXPORT_SYMBOL(nlmsvc_invalidate_client);
EXPORT_SYMBOL(nlmsvc_ops);
EXPORT_SYMBOL(lockdctl);

/* Configuration at insmod time */
EXPORT_SYMBOL(nlmsvc_grace_period);
EXPORT_SYMBOL(nlmsvc_timeout);

/* NLM4 exported symbols */ 
EXPORT_SYMBOL(nlm4_lck_denied_grace_period);
EXPORT_SYMBOL(nlm4_lck_denied);
EXPORT_SYMBOL(nlm4_lck_blocked);
EXPORT_SYMBOL(nlm4_rofs);
EXPORT_SYMBOL(nlm4_stale_fh);
EXPORT_SYMBOL(nlm4_granted);
EXPORT_SYMBOL(nlm4_deadlock);
EXPORT_SYMBOL(nlm4_failed);
EXPORT_SYMBOL(nlm4_fbig);
EXPORT_SYMBOL(nlm4_lck_denied_nolocks);


#endif /* CONFIG_MODULES */
