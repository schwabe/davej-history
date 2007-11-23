/*
 * ADB Miscellaneous stuff
 */

#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#ifdef CONFIG_ADB_NEW
#include <linux/adb.h>
#else
#include <asm/adb.h>
#endif
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/setup.h>
#include <asm/macintosh.h>

extern void cuda_poll(void);
extern int cuda_request(struct adb_request *req, 
			void (*done)(struct adb_request *),
			int nbytes, ...);

/*
 * Return the current time as the number of seconds since January 1, 1904.
 *
 * This method works for both CUDA and IIsi-style ADB. It'll probably
 * work for PowerBooks too once we have PowerBook ADB working.
 */

__u32 adb_read_time(void)
{
	volatile struct adb_request req;
	__u32 time;

#ifdef CONFIG_ADB_NEW
#ifdef CONFIG_ADB_CUDA
	cuda_request((struct adb_request *) &req, NULL,
			2, CUDA_PACKET, CUDA_GET_TIME);
	while (!req.complete) cuda_poll();
#endif
#else
	adb_request((struct adb_request *) &req, NULL,
			2, CUDA_PACKET, CUDA_GET_TIME);
	while (!req.complete);
#endif

	time = (req.reply[3] << 24) | (req.reply[4] << 16)
		| (req.reply[5] << 8) | req.reply[6];
	return time;
}

/*
 * Set the current system time
 *
 * This method works for both CUDA and IIsi-style ADB. It'll probably
 * work for PowerBooks too once we have PowerBook ADB working.
 */

void adb_write_time(__u32 data)
{
	volatile struct adb_request req;

#ifdef CONFIG_ADB_NEW
#ifdef CONFIG_ADB_CUDA
	cuda_request((struct adb_request *) &req, NULL,
			6, CUDA_PACKET, CUDA_SET_TIME,
			(data >> 24) & 0xFF, (data >> 16) & 0xFF,
			(data >> 8) & 0xFF, data & 0xFF);
	while (!req.complete) cuda_poll();
#endif
#else
	adb_request((struct adb_request *) &req, NULL,
			6, CUDA_PACKET, CUDA_SET_TIME,
			(data >> 24) & 0xFF, (data >> 16) & 0xFF,
			(data >> 8) & 0xFF, data & 0xFF);
	while (!req.complete);
#endif
}

/*
 * Initially discovered this technique in the Mach kernel of MkLinux in
 * osfmk/src/mach_kernel/ppc/POWERMAC/cuda_power.c.  Found equivalent LinuxPPC
 * code in arch/ppc/kernel/setup.c, which also has a PMU technique for
 * PowerBooks!
 *
 * --David Kilzer
 */

void adb_poweroff(void)
{
	struct adb_request req;

	/*
	 * Print our "safe" message before we send the request
	 * just in case the request never returns.
	 */

	printk ("It is now safe to switch off your machine.\n");

#ifdef CONFIG_ADB_NEW
#ifdef CONFIG_ADB_CUDA
	cuda_request (&req, NULL, 2, CUDA_PACKET, CUDA_POWERDOWN);
	for (;;) cuda_poll();
#endif
#else
	adb_request (&req, NULL, 2, CUDA_PACKET, CUDA_POWERDOWN);
	for (;;) ;
#endif
}

/*
 * Initially discovered this technique in the Mach kernel of MkLinux in
 * osfmk/src/mach_kernel/ppc/POWERMAC/cuda_power.c.  Found equivalent LinuxPPC
 * code in arch/ppc/kernel/setup.c, which also has a PMU technique!
 * --David Kilzer
 * 
 * I suspect the MAC_ADB_CUDA code might work with other ADB types of machines
 * but have no way to test this myself.  --DDK
 */

void adb_hwreset(void)
{
	struct adb_request req;

	printk ("Resetting system...\n");

#ifdef CONFIG_ADB_NEW
#ifdef CONFIG_ADB_CUDA
	cuda_request (&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
	for (;;) cuda_poll();
#endif
#else
	adb_request (&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
	for (;;) ;
#endif
}
