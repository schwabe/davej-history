#ifndef _LINUX_TASKS_H
#define _LINUX_TASKS_H

/*
 * This is the maximum nr of tasks - change it if you need to
 */
 
#ifdef __SMP__
#define NR_CPUS	32		/* Max processors that can be running in SMP */
#else
#define NR_CPUS 1
#endif

#define NR_TASKS	512	/* Max 4092, or 4090 w/APM configured on x86 */

#define MAX_TASKS_PER_USER (NR_TASKS/2)
#define MIN_TASKS_LEFT_FOR_ROOT 4

#endif
