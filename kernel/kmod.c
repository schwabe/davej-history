/*
	kmod, the new module loader (replaces kerneld)
	Kirk Petersen

	Reorganized not to be a daemon by Adam Richter, with guidance
	from Greg Zornetzer.

	Modified to avoid chroot and file sharing problems.
	Mikael Pettersson

	Back port check for modprobe loops from 2.3.
	Keith Owens <kaos@ocs.com.au> May 2000
*/

#define __KERNEL_SYSCALLS__

#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

/*
	modprobe_path is set via /proc/sys.
*/
char modprobe_path[256] = "/sbin/modprobe";

static inline void
use_init_fs_context(void)
{
	struct fs_struct * fs;

	/*
	 * Don't use the user's fs context, use init's instead.
	 * Note that we can use "init_task" (which is not actually
	 * the same as the user-level "init" process) because we
	 * started "init" with a CLONE_FS
	 */

	lock_kernel();

	fs = current->fs;
	dput(fs->root);
	dput(fs->pwd);
	fs->root = dget(init_task.fs->root);
	fs->pwd = dget(init_task.fs->pwd);
	fs->umask = 0022;

	unlock_kernel();
}

int exec_usermodehelper(char *program_path, char *argv[], char *envp[])
{
	int i;

	current->session = 1;
	current->pgrp = 1;

	use_init_fs_context();

	/* Prevent parent user process from sending signals to child.
	   Otherwise, if the modprobe program does not exist, it might
	   be possible to get a user defined signal handler to execute
	   as the super user right after the execve fails if you time
	   the signal just right.
	*/
	spin_lock_irq(&current->sigmask_lock);
	flush_signals(current);
	flush_signal_handlers(current);
	spin_unlock_irq(&current->sigmask_lock);

	for (i = 0; i < current->files->max_fds; i++ ) {
		if (current->files->fd[i]) close(i);
	}

	/* Drop the "current user" thing */
	free_uid(current);

	/* Give kmod all effective privileges.. */
	current->uid = current->euid = current->fsuid = 0;
	cap_set_full(current->cap_effective);

	/* Allow execve args to be in kernel space. */
	set_fs(KERNEL_DS);

	/* Go, go, go... */
	if (execve(program_path, argv, envp) < 0)
		return -errno;
	return 0;
}

static int exec_modprobe(void * module_name)
{
	static char * envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { modprobe_path, "-s", "-k", (char*)module_name, NULL };
	int ret;

	ret = exec_usermodehelper(modprobe_path, argv, envp);
	if (ret) {
		printk(KERN_ERR
		       "kmod: failed to exec %s -s -k %s, errno = %d\n",
		       modprobe_path, (char*) module_name, errno);
	}
	return ret;
}

/**
 *	request_module - try to load a kernel module
 *	@module_name: Name of module
 *
 * 	Load a module using the user mode module loader. The function returns
 *	zero on success or a negative errno code on failure. Note that a
 * 	successful module load does not mean the module did not then unload
 *	and exit on an error of its own. Callers must check that the service
 *	they requested is now available not blindly invoke it.
 *
 *	If module auto-loading support is disabled then this function
 *	becomes a no-operation.
 */
 
int request_module(const char * module_name)
{
	int pid;
	int waitpid_result;
	sigset_t tmpsig;
	int i;
	static atomic_t kmod_concurrent = ATOMIC_INIT(0);
#define MAX_KMOD_CONCURRENT 50	/* Completely arbitrary value - KAO */
	static int kmod_loop_msg;

	/* Don't allow request_module() before the root fs is mounted!  */
	if ( ! current->fs->root ) {
		printk(KERN_ERR "request_module[%s]: Root fs not mounted\n",
			module_name);
		return -EPERM;
	}

	/* If modprobe needs a service that is in a module, we get a recursive
	 * loop.  Limit the number of running kmod threads to NR_TASKS/2 or
	 * MAX_KMOD_CONCURRENT, whichever is the smaller.  A cleaner method
	 * would be to run the parents of this process, counting how many times
	 * kmod was invoked.  That would mean accessing the internals of the
	 * process tables to get the command line, proc_pid_cmdline is static
	 * and it is not worth changing the proc code just to handle this case. 
	 * KAO.
	 */
	i = NR_TASKS/2;
	if (i > MAX_KMOD_CONCURRENT)
		i = MAX_KMOD_CONCURRENT;
	atomic_inc(&kmod_concurrent);
	if (atomic_read(&kmod_concurrent) > i) {
		if (kmod_loop_msg++ < 5)
			printk(KERN_ERR
			       "kmod: runaway modprobe loop assumed and stopped\n");
		atomic_dec(&kmod_concurrent);
		return -ENOMEM;
	}

	{
	int old=current->dumpable;
	current->dumpable=0;	/* block ptrace */
	pid = kernel_thread(exec_modprobe, (void*) module_name, 0);
	if (pid < 0) {
		printk(KERN_ERR "request_module[%s]: fork failed, errno %d\n", module_name, -pid);
		atomic_dec(&kmod_concurrent);
		current->dumpable=old;
		return pid;
	}
	current->dumpable=old;
	}

	/* Block everything but SIGKILL/SIGSTOP */
	spin_lock_irq(&current->sigmask_lock);
	tmpsig = current->blocked;
	siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGSTOP));
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	waitpid_result = waitpid(pid, NULL, __WCLONE);
	atomic_dec(&kmod_concurrent);

	/* Allow signals again.. */
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = tmpsig;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	if (waitpid_result != pid) {
		printk(KERN_ERR "request_module[%s]: waitpid(%d,...) failed, errno %d\n",
		       module_name, pid, -waitpid_result);
	}
	return 0;
}


#ifdef CONFIG_HOTPLUG
/*
	hotplug path is set via /proc/sys
	invoked by hotplug-aware bus drivers,
	with exec_usermodehelper and some thread-spawner

	argv [0] = hotplug_path;
	argv [1] = "usb", "scsi", "pci", "network", etc;
	... plus optional type-specific parameters
	argv [n] = 0;

	envp [*] = HOME, PATH; optional type-specific parameters

	a hotplug bus should invoke this for device add/remove
	events.  the command is expected to load drivers when
	necessary, and may perform additional system setup.
*/
char hotplug_path[256] = "/sbin/hotplug";

#endif

