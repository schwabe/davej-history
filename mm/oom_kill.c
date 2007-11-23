/*
 *  linux/mm/oom_kill.c
 * 
 *  Copyright (C)  1998,2000  Rik van Riel
 *	Thanks go out to Claus Fischer for some serious inspiration and
 *	for goading me into coding this file...
 *
 *  The routines in this file are used to kill a process when
 *  we're seriously out of memory. This gets called from kswapd()
 *  in linux/mm/vmscan.c when we really run out of memory.
 *
 *  Since we won't call these routines often (on a well-configured
 *  machine) this file will double as a 'coding guide' and a signpost
 *  for newbie kernel hackers. It features several pointers to major
 *  kernel subsystems and hints as to where to find out what things do.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/timex.h>

/* #define DEBUG */
#define min(a,b) (((a)<(b))?(a):(b))

/*
 * A rough approximation to the sqrt() function.
 */
inline int int_sqrt(unsigned int x)
{
	unsigned int out = x;
	while (x & ~(unsigned int)1) x >>=2, out >>=1;
	if (x) out -= out >> 2;
	return (out ? out : 1);
}	

/*
 * Basically, points = size / (sqrt(CPU_used) * sqrt(sqrt(time_running)))
 * with some bonusses/penalties.
 *
 * We try to chose our `guilty' task in such a way that we free
 * up the maximum amount of memory and lose the minimum amount of
 * done work.
 *
 * The definition of the task_struct, the structure describing the state
 * of each process, can be found in include/linux/sched.h. For
 * capability info, you should read include/linux/capability.h.
 */

inline int badness(struct task_struct *p)
{
	int points = p->mm->total_vm;
	points /= int_sqrt((p->times.tms_utime + p->times.tms_stime) >> (SHIFT_HZ + 3));
	points /= int_sqrt(int_sqrt((jiffies - p->start_time) >> (SHIFT_HZ + 10)));
/*
 * Niced processes are probably less important; kernel/sched.c
 * and include/linux/sched.h contain most info on scheduling.
 */
	if (p->priority < DEF_PRIORITY)
		points <<= 1;
/*
 * p->(e)uid is the process User ID, ID 0 is root, the super user.
 * The super user usually only runs (important) system services
 * and properly checked programs which we don't want to kill.
 */
	if (p->uid == 0 || p->euid == 0 || cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_ADMIN))
		points >>= 2;
/*
 * We don't want to kill a process with direct hardware access.
 * Not only could this mess up the hardware, but these processes
 * are usually fairly important too.
 */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_RAWIO))
		points >>= 1;
#ifdef DEBUG
	printk(KERN_DEBUG "OOMkill: task %d (%s) got %d points\n",
	p->pid, p->comm, points);
#endif
	return points;
}

/*
 * Simple selection loop. We chose the process with the highest
 * number of 'points'. We need the locks to make sure that the
 * list of task structs doesn't change while we look the other way.
 */
inline struct task_struct * select_bad_process(void)
{
	int points = 0, maxpoints = 0;
	struct task_struct *p = NULL;
	struct task_struct *chosen = NULL;

	read_lock(&tasklist_lock);
	for_each_task(p)
	{
		if (p->pid)
			points = badness(p);
		if (points > maxpoints) {
			chosen = p;
			maxpoints = points;
		}
	}
	read_unlock(&tasklist_lock);
	return chosen;
}

/*
 * We kill the 'best' process and print a message to userspace.
 * The only things to be careful about are:
 *  - don't SIGKILL a process with direct hardware access.
 *  - are we killing ourselves?
 *  - when we kill someone else, can we sleep and get out of the way?
 */
void oom_kill(unsigned long gfp_mask)
{

	struct task_struct *p = select_bad_process();

	if (p == NULL)
		return;

	if (p == current) {
		printk(KERN_ERR "Out of Memory: Killed process %d (%s).",
			 p->pid, p->comm);
	} else {
		printk(KERN_ERR "Out of Memory: Killed process %d (%s), "
			"saved process %d (%s).",
			p->pid, p->comm, current->pid, current->comm);
	}

	/* This process has hardware access, be more careful */
	if (cap_t(p->cap_effective) & CAP_TO_MASK(CAP_SYS_RAWIO)) {
		force_sig(SIGTERM, p);
	} else {
		force_sig(SIGKILL, p);
	}

	/* Get out of the way so that p can die */
	if (p != current && (gfp_mask & __GFP_WAIT) && current->state == TASK_RUNNING) {
		p->counter = 2 * DEF_PRIORITY;
		current->policy |= SCHED_YIELD;
		schedule();
	}
	return;
}

/*
 * We are called when __get_free_pages() thinks the system may
 * be out of memory. If we really are out of memory, we can do
 * nothing except freeing up memory by killing a process...
 */

int out_of_memory(unsigned long gfp_mask)
{
	int count = page_cluster;
	int loop = 0;
	int freed = 0;

again:
	if (gfp_mask & __GFP_WAIT) {
		/* Try to free up some memory */
		current->flags |= PF_MEMALLOC;
		do {
			freed += try_to_free_pages(gfp_mask);
			run_task_queue(&tq_disk);
			if (freed && nr_free_pages > freepages.min) {
				current->flags &= ~PF_MEMALLOC;
				return 0;
			}
		} while (--count);
		current->flags &= ~PF_MEMALLOC;
	}

	/* Darn, we failed. Now we have to kill something */
	if (!loop)
		oom_kill(gfp_mask);

	if (nr_free_pages > freepages.min)
		return 0;
	if (!loop) {
		loop = 1;
		goto again;
	}
	/* Still out of memory, let the caller deal with it */
	return 1;
}
