/*
 *	CPU Microcode Update interface for Linux
 *
 *	Copyright (C) 2000 Tigran Aivazian
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to IA32 family - PentiumPro, Pentium II, Pentium III,
 *	Pentium II Xeon, Pentium III Xeon, Pentium 4 etc.
 *
 *	Reference: Section 8.10 of Volume III, Intel Pentium 4 Manual, 
 *	Order Number 245472 or free download from:
 *		
 *	http://developer.intel.com/design/pentium4/manuals/245472.htm
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	1.0	16 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Initial release.
 *	1.01	18 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Added read() support + cleanups.
 *	1.02	21 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Added 'device trimming' support. open(O_WRONLY) zeroes
 *		and frees the saved copy of applied microcode.
 *	1.03	29 February 2000, Tigran Aivazian <tigran@sco.com>
 *		Made to use devfs (/dev/cpu/microcode) + cleanups.
 *	1.04	06 June 2000, Simon Trimmer <simon@veritas.com>
 *		Added misc device support (now uses both devfs and misc).
 *		Added MICROCODE_IOCFREE ioctl to clear memory.
 *	1.05	09 June 2000, Simon Trimmer <simon@veritas.com>
 *		Messages for error cases (non intel & no suitable microcode).
 *	1.06	07 Dec 2000, Tigran Aivazian <tigran@veritas.com>
 *		Pentium 4 support + backported fixes from 2.4
 *	1.07	13 Dec 2000, Tigran Aivazian <tigran@veritas.com>
 *		More bugfixes backported from 2.4
 *	1.08	27 Dec 2000, Tigran Aivazian <tigran@veritas.com>
 *		Fix: X86_FEATURE_30 was used incorrectly (in a 2.4 manner)
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/smp.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>

#define MICROCODE_VERSION 	"1.08"

MODULE_DESCRIPTION("Intel CPU (IA-32) microcode update driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@veritas.com>");
EXPORT_NO_SYMBOLS;

/* VFS interface */
static int microcode_open(struct inode *, struct file *);
static ssize_t microcode_read(struct file *, char *, size_t, loff_t *);
static ssize_t microcode_write(struct file *, const char *, size_t, loff_t *);
static int microcode_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

/* read()/write()/ioctl() are serialized on this */
static struct semaphore microcode_sem = MUTEX;

/* internal helpers to do the work */
static int do_microcode_update(void);
static void do_update_one(void *);

/* the actual array of microcode blocks, each 2048 bytes */
static struct microcode *microcode;
static unsigned int microcode_num;
static char *mc_applied; /* holds an array of applied microcode blocks */
static unsigned int mc_fsize;

static struct file_operations microcode_fops = {
	read:		microcode_read,
	write:		microcode_write,
	ioctl:		microcode_ioctl,
	open:		microcode_open,
};

static struct miscdevice microcode_dev = {
	minor: MICROCODE_MINOR,
	name:	"microcode",
	fops:	&microcode_fops,
};

int __init microcode_init(void)
{
	if (misc_register(&microcode_dev) < 0) {
		printk(KERN_ERR "microcode: can't misc_register on minor=%d\n",
			MICROCODE_MINOR);
		return -EINVAL;
	}
	printk(KERN_INFO "IA-32 Microcode Update Driver: v%s <tigran@veritas.com>\n", 
			MICROCODE_VERSION);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	misc_deregister(&microcode_dev);
	if (mc_applied)
		kfree(mc_applied);
	printk(KERN_INFO "IA-32 Microcode Update Driver v%s unregistered\n", 
			MICROCODE_VERSION);
}

int init_module(void)
{
	return microcode_init();
}
#endif

static int microcode_open(struct inode *inode, struct file *file)
{
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

/*
 * update_req[cpu].err is set to 1 if update failed on 'cpu', 0 otherwise
 * if err==0, microcode[update_req[cpu].slot] points to applied block of microcode
 */
struct update_req {
	int err;
	int slot;
} update_req[NR_CPUS];

static int do_microcode_update(void)
{
	int i, error = 0, err;
	struct microcode *m;

	if (smp_call_function(do_update_one, NULL, 1, 1) != 0)
		panic("do_microcode_update(): timed out waiting for other CPUs\n");

	do_update_one(NULL);

	for (i=0; i<smp_num_cpus; i++) {
		err = update_req[i].err;
		error += err;
		if (!err) {
			m = (struct microcode *)mc_applied + i;
			memcpy(m, &microcode[update_req[i].slot], sizeof(struct microcode));
		}
	}
	return error;
}

static void do_update_one(void *arg)
{
	int cpu_num = smp_processor_id();
	struct cpuinfo_x86 *c = cpu_data + cpu_num;
	struct update_req *req = update_req + cpu_num;
	unsigned int pf = 0, val[2], rev, sig;
	int i,found=0;

	req->err = 1; /* be pessimistic */

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
		(c->x86_capability & X86_FEATURE_30) ) { /* IA64 */
		printk(KERN_ERR "microcode: CPU%d not a capable Intel processor\n", cpu_num);
		return;
	}

	sig = c->x86_mask + (c->x86_model<<4) + (c->x86<<8);

	if ((c->x86_model >= 5) || (c->x86 > 6)) {
		/* get processor flags from MSR 0x17 */
		rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		pf = 1 << ((val[1] >> 18) & 7);
	}

	for (i=0; i<microcode_num; i++)
		if (microcode[i].sig == sig && microcode[i].pf == pf &&
		    microcode[i].ldrver == 1 && microcode[i].hdrver == 1) {

			found=1;

			/* trick, to work even if there was no prior update by the BIOS */
			wrmsr(MSR_IA32_UCODE_REV, 0, 0);
			__asm__ __volatile__ ("cpuid" : : : "ax", "bx", "cx", "dx");

			/* get current (on-cpu) revision into rev (ignore val[0]) */
			rdmsr(MSR_IA32_UCODE_REV, val[0], rev);
			if (microcode[i].rev < rev) {
				printk(KERN_ERR 
					"microcode: CPU%d not 'upgrading' to earlier revision"
					" %d (current=%d)\n", cpu_num, microcode[i].rev, rev);
			} else if (microcode[i].rev == rev) {
				printk(KERN_ERR
					"microcode: CPU%d already up-to-date (revision %d)\n",
						cpu_num, rev);
			} else {
				int sum = 0;
				struct microcode *m = &microcode[i];
				unsigned int *sump = (unsigned int *)(m+1);

				while (--sump >= (unsigned int *)m)
					sum += *sump;
				if (sum != 0) {
					printk(KERN_ERR "microcode: CPU%d aborting, "
							"bad checksum\n", cpu_num);
					break;
				}

				/* write microcode via MSR 0x79 */
				wrmsr(MSR_IA32_UCODE_WRITE, (unsigned int)(m->bits), 0);

				/* serialize */
				__asm__ __volatile__ ("cpuid" : : : "ax", "bx", "cx", "dx");

				/* get the current revision from MSR 0x8B */
				rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

				/* notify the caller of success on this cpu */
				req->err = 0;
				req->slot = i;
				printk(KERN_ERR "microcode: CPU%d updated from revision "
						"%d to %d, date=%08x\n", 
						cpu_num, rev, val[1], m->date);
			}
			break;
		}

	if(!found)
		printk(KERN_ERR "microcode: CPU%d no microcode found! (sig=%x, pflags=%d)\n",
				cpu_num, sig, pf);
}

static ssize_t microcode_read(struct file *file, char *buf, size_t len, loff_t *ppos)
{
	ssize_t err = 0;

	down(&microcode_sem);
	if (*ppos >= mc_fsize)
		goto out;
	if (*ppos + len > mc_fsize)
		len = mc_fsize - *ppos;
	err = -EFAULT;
	if (copy_to_user(buf, mc_applied + *ppos, len))
		goto out;
	*ppos += len;
	err = len;
out:
	up(&microcode_sem);
	return err;
}

static ssize_t microcode_write(struct file *file, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret;

	if (len % sizeof(struct microcode) != 0) {
		printk(KERN_ERR "microcode: can only write in N*%d bytes units\n", 
			sizeof(struct microcode));
		return -EINVAL;
	}
	down(&microcode_sem);
	if (!mc_applied) {
		mc_applied = kmalloc(smp_num_cpus*sizeof(struct microcode),
				GFP_KERNEL);
		if (!mc_applied) {
			up(&microcode_sem);
			printk(KERN_ERR "microcode: out of memory for saved microcode\n");
			return -ENOMEM;
		}
		memset(mc_applied, 0, mc_fsize);
	}
	
	microcode_num = len/sizeof(struct microcode);
	microcode = vmalloc(len);
	if (!microcode) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	if (copy_from_user(microcode, buf, len)) {
		ret = -EFAULT;
		goto out_fsize;
	}
	if(do_microcode_update()) {
		ret = -EIO;
		goto out_fsize;
	} else {
		mc_fsize = smp_num_cpus * sizeof(struct microcode);
		ret = (ssize_t)len;
	}
out_fsize:
	vfree(microcode);
out_unlock:
	up(&microcode_sem);
	return ret;
}

static int microcode_ioctl(struct inode *inode, struct file *file, 
		unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
		case MICROCODE_IOCFREE:
			down(&microcode_sem);
			if (mc_applied) {
				int bytes = smp_num_cpus * sizeof(struct microcode);

				memset(mc_applied, 0, mc_fsize);
				kfree(mc_applied);
				mc_applied = NULL;
				mc_fsize = 0;
				printk(KERN_WARNING "microcode: freed %d bytes\n", bytes);
				up(&microcode_sem);
				return 0;
			}
			up(&microcode_sem);
			return -ENODATA;

		default:
			printk(KERN_ERR "microcode: unknown ioctl cmd=%d\n",
					cmd);
			return -EINVAL;
	}
	/* NOT REACHED */
	return -EINVAL;
}
