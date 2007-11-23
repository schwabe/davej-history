/*
 * Linux/PowerPC Real Time Clock Driver
 *
 * heavily based on:
 * Linux/SPARC Real Time Clock Driver
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * This is a little driver that lets a user-level program access
 * the PPC clocks chip. It is no use unless you
 * use the modified clock utility.
 *
 * Get the modified clock utility from:
 *   ftp://vger.kernel.org/pub/linux/Sparc/userland/clock.c
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>

#include <asm/time.h>

static int rtc_busy = 0;

/* Retrieve the current date and time from the real time clock. */
void get_rtc_time(struct rtc_time *t)
{
	unsigned long nowtime;
    
	nowtime = (ppc_md.get_rtc_time)();

	to_tm(nowtime, t);

	t->tm_year -= 1900;
	t->tm_mon -= 1;
	t->tm_wday -= 1;
}

/* Set the current date and time inthe real time clock. */
void set_rtc_time(struct rtc_time *t)
{
	unsigned long nowtime;

	printk(KERN_INFO "rtc.c:set_rtc_time: %04d-%02d-%02d %02d:%02d:%02d.\n", t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

	nowtime = mktime(t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

	printk(KERN_INFO "rtc.c:set_rtc_time: set rtc time to %d seconds.\n", nowtime);

	(ppc_md.set_rtc_time)(nowtime);
}

static long long rtc_lseek(struct file *file, long long offset, int origin)
{
	return -ESPIPE;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct rtc_time rtc_tm;

	switch (cmd)
	{
	case RTC_RD_TIME:
		if (ppc_md.get_rtc_time)
		{
			memset(&rtc_tm, 0, sizeof(rtc_tm));
			get_rtc_time(&rtc_tm);

			copy_to_user_ret((struct rtc_time*)arg, &rtc_tm, sizeof(struct rtc_time), -EFAULT);

			return 0;
		}
		else
			return -EINVAL;

	case RTC_SET_TIME:
		if (!capable(CAP_SYS_TIME))
			return -EPERM;

		if (ppc_md.set_rtc_time)
		{
			copy_from_user_ret(&rtc_tm, (struct rtc_time*)arg, sizeof(struct rtc_time), -EFAULT);

			set_rtc_time(&rtc_tm);

			return 0;
		}
		else
			return -EINVAL;

	default:
		return -EINVAL;
	}
}

static int rtc_open(struct inode *inode, struct file *file)
{
	if (rtc_busy)
		return -EBUSY;

	rtc_busy = 1;

	MOD_INC_USE_COUNT;

	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	rtc_busy = 0;
	return 0;
}

static struct file_operations rtc_fops = {
	rtc_lseek,
	NULL,		/* rtc_read */
	NULL,		/* rtc_write */
	NULL,		/* rtc_readdir */
	NULL,		/* rtc_poll */
	rtc_ioctl,
	NULL,		/* rtc_mmap */
	rtc_open,
	NULL,		/* flush */
	rtc_release
};

static struct miscdevice rtc_dev = { RTC_MINOR, "rtc", &rtc_fops };

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
__initfunc(int rtc_init(void))
#endif
{
	int error;

	error = misc_register(&rtc_dev);
	if (error) {
		printk(KERN_ERR "rtc: unable to get misc minor\n");
		return error;
	}

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	misc_deregister(&rtc_dev);
}
#endif
