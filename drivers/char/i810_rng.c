/*

	Hardware driver for Intel i810 Random Number Generator (RNG)
	Copyright 2000 Jeff Garzik <jgarzik@mandrakesoft.com>

	Driver Web site:  http://gtf.org/garzik/drivers/i810_rng/



	Based on:
	Intel 82802AB/82802AC Firmware Hub (FWH) Datasheet
		May 1999 Order Number: 290658-002 R

	Intel 82802 Firmware Hub: Random Number Generator
	Programmer's Reference Manual
		December 1999 Order Number: 298029-001 R

	Intel 82802 Firmware HUB Random Number Generator Driver
	Copyright (c) 2000 Matt Sottek <msottek@quiknet.com>

	Special thanks to Matt Sottek.  I did the "guts", he
	did the "brains" and all the testing.  (Anybody wanna send
	me an i810 or i820?)

	----------------------------------------------------------

	This software may be used and distributed according to the terms
        of the GNU Public License, incorporated herein by reference.

	----------------------------------------------------------

	From the firmware hub datasheet:

	The Firmware Hub integrates a Random Number Generator (RNG)
	using thermal noise generated from inherently random quantum
	mechanical properties of silicon. When not generating new random
	bits the RNG circuitry will enter a low power state. Intel will
	provide a binary software driver to give third party software
	access to our RNG for use as a security feature. At this time,
	the RNG is only to be used with a system in an OS-present state.

	----------------------------------------------------------

	Theory of operation:

	Character driver.  Using the standard open()
	and read() system calls, you can read random data from
	the i810 RNG device.  This data is NOT CHECKED by any
	fitness tests, and could potentially be bogus (if the
	hardware is faulty or has been tampered with).

	/dev/intel_rng is char device major 10, minor 183.


	----------------------------------------------------------

	Driver notes:

	* In order to unload the i810_rng module, you must first
	make sure all users of the character device have closed

	* FIXME:  Currently only one open() of the character device is allowed.
	If another user tries to open() the device, they will get an
	-EBUSY error.  Instead, this really should either support
	multiple simultaneous users of the character device (not hard),
	or simply block open() until the current user of the chrdev
	calls close().

	* FIXME: support poll()

	* FIXME: should we be crazy and support mmap()?

	----------------------------------------------------------

 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/spinlock.h>
#include <linux/random.h>
#include <linux/sysctl.h>
#include <linux/miscdevice.h>

#include <asm/io.h>
#include <asm/uaccess.h>


/*
 * core module and version information
 */
#define RNG_VERSION "0.6.2-2.2.x"
#define RNG_MODULE_NAME "i810_rng"
#define RNG_DRIVER_NAME   RNG_MODULE_NAME " hardware driver " RNG_VERSION
#define PFX RNG_MODULE_NAME ": "


/*
 * debugging macros
 */
#undef RNG_DEBUG /* define to 1 to enable copious debugging info */

#ifdef RNG_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define RNG_NDEBUG 0        /* define to 1 to disable lightweight runtime checks */
#if RNG_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {                                   \
        printk( "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif


/*
 * misc helper macros
 */
#define arraysize(x)            (sizeof(x)/sizeof(*(x)))


/*
 * RNG registers (offsets from rng_mem)
 */
#define RNG_HW_STATUS			0
#define		RNG_PRESENT		0x40
#define		RNG_ENABLED		0x01
#define RNG_STATUS			1
#define		RNG_DATA_PRESENT	0x01
#define RNG_DATA			2

#define RNG_ADDR			0xFFBC015F
#define RNG_ADDR_LEN			3

#define RNG_MISCDEV_MINOR		183 /* official */


/*
 * various RNG status variables.  they are globals
 * as we only support a single RNG device
 */
static int rng_allocated;		/* is someone using the RNG region? */
static int rng_hw_enabled;		/* is the RNG h/w enabled? */
static int rng_use_count;		/* number of times RNG has been enabled */
static void *rng_mem;			/* token to our ioremap'd RNG register area */
static spinlock_t rng_lock = SPIN_LOCK_UNLOCKED; /* hardware lock */
static int rng_open;			/* boolean, 0 (false) if chrdev is closed, 1 (true) if open */

/*
 * inlined helper functions for accessing RNG registers
 */
static inline u8 rng_hwstatus (void)
{
	assert (rng_mem != NULL);
	return readb (rng_mem + RNG_HW_STATUS);
}


static inline void rng_hwstatus_set (u8 hw_status)
{
	assert (rng_mem != NULL);
	writeb (hw_status, rng_mem + RNG_HW_STATUS);
}


static inline int rng_data_present (void)
{
	assert (rng_mem != NULL);
	assert (rng_hw_enabled == 1);

	return (readb (rng_mem + RNG_STATUS) & RNG_DATA_PRESENT) ? 1 : 0;
}


static inline int rng_data_read (void)
{
	assert (rng_mem != NULL);
	assert (rng_hw_enabled == 1);

	return readb (rng_mem + RNG_DATA);
}


/*
 * rng_enable - enable or disable the RNG hardware
 */
static int rng_enable (int enable)
{
	int rc = 0;
	u8 hw_status;

	DPRINTK ("ENTER\n");

	spin_lock (&rng_lock);

	hw_status = rng_hwstatus ();

	if (enable) {
		rng_hw_enabled = 1;
		rng_use_count++;
		MOD_INC_USE_COUNT;
	} else {
		rng_use_count--;
		if (rng_use_count == 0)
			rng_hw_enabled = 0;
		MOD_DEC_USE_COUNT;
	}

	if (rng_hw_enabled && ((hw_status & RNG_ENABLED) == 0)) {
		rng_hwstatus_set (hw_status | RNG_ENABLED);
		printk (KERN_INFO PFX "RNG h/w enabled\n");
	}

	else if (!rng_hw_enabled && (hw_status & RNG_ENABLED)) {
		rng_hwstatus_set (hw_status & ~RNG_ENABLED);
		printk (KERN_INFO PFX "RNG h/w disabled\n");
	}

	spin_unlock (&rng_lock);

	if ((!!enable) != (!!(rng_hwstatus () & RNG_ENABLED))) {
		printk (KERN_ERR PFX "Unable to %sable the RNG\n",
			enable ? "en" : "dis");
		rc = -EIO;
	}

	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}


static int rng_dev_open (struct inode *inode, struct file *filp)
{
	int rc = -EINVAL;

	MOD_INC_USE_COUNT;

	if ((filp->f_mode & FMODE_READ) == 0)
		goto err_out;
	if (filp->f_mode & FMODE_WRITE)
		goto err_out;

	spin_lock (&rng_lock);

	/* only allow one open of this device, exit with -EBUSY if already open */
	/* FIXME: we should sleep on a semaphore here, unless O_NONBLOCK */
	if (rng_open) {
		spin_unlock (&rng_lock);
		rc = -EBUSY;
		goto err_out;
	}

	rng_open = 1;

	spin_unlock (&rng_lock);

	if (rng_enable(1) != 0) {
		spin_lock (&rng_lock);
		rng_open = 0;
		spin_unlock (&rng_lock);
		rc = -EIO;
		goto err_out;
	}

	return 0;

err_out:
	MOD_DEC_USE_COUNT;
	return rc;
}


static int rng_dev_release (struct inode *inode, struct file *filp)
{

	if (rng_enable(0) != 0)
		return -EIO;

	spin_lock (&rng_lock);
	rng_open = 0;
	spin_unlock (&rng_lock);

	MOD_DEC_USE_COUNT;
	return 0;
}


static ssize_t rng_dev_read (struct file *filp, char * buf, size_t size,
			     loff_t *offp)
{
	int have_data, copied = 0;
	u8 data=0;
	u8 *page;

	if (size < 1)
		return 0;

	page = (unsigned char *) get_free_page (GFP_KERNEL);
	if (!page)
		return -ENOMEM;

read_loop:
	/* using the fact that read() can return >0 but
	 * less than the requested amount, we simply
	 * read up to PAGE_SIZE or buffer size, whichever
	 * is smaller, and return that data.
	 */
	if ((copied == size) || (copied == PAGE_SIZE)) {
		size_t tmpsize = (copied == size) ? size : PAGE_SIZE;
		int rc = copy_to_user (buf, page, tmpsize);
		free_page ((long)page);
		if (rc) return rc;
		return tmpsize;
	}

	spin_lock (&rng_lock);

	have_data = 0;
	if (rng_data_present ()) {
		data = rng_data_read ();
		have_data = 1;
	}

	spin_unlock (&rng_lock);

	if (have_data) {
		page[copied] = data;
		copied++;
	} else {
		if (filp->f_flags & O_NONBLOCK) {
			free_page ((long)page);
			return -EAGAIN;
		}
	}

	if (current->need_resched)
		schedule ();

	if (signal_pending (current)) {
		free_page ((long)page);
		return -ERESTARTSYS;
	}

	goto read_loop;
}


/*
 * rng_init_one - look for and attempt to init a single RNG
 */
static int __init rng_init_one (struct pci_dev *dev)
{
	int rc;
	u8 hw_status;

	DPRINTK ("ENTER\n");

	if (rng_allocated) {
		printk (KERN_ERR PFX "this driver only supports one RNG\n");
		DPRINTK ("EXIT, returning -EBUSY\n");
		return -EBUSY;
	}

	rng_mem = ioremap (RNG_ADDR, RNG_ADDR_LEN);
	if (rng_mem == NULL) {
		printk (KERN_ERR PFX "cannot ioremap RNG Memory\n");
		DPRINTK ("EXIT, returning -EBUSY\n");
		rc = -EBUSY;
		goto err_out;
	}

	/* Check for Intel 82802 */
	hw_status = rng_hwstatus ();
	if ((hw_status & RNG_PRESENT) == 0) {
		printk (KERN_ERR PFX "RNG not detected\n");
		DPRINTK ("EXIT, returning -ENODEV\n");
		rc = -ENODEV;
		goto err_out;
	}

	rng_allocated = 1;

	rc = rng_enable (0);
	if (rc) {
		printk (KERN_ERR PFX "cannot disable RNG, aborting\n");
		goto err_out;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out:
	if (rng_mem)
		iounmap (rng_mem);
	return rc;
}


/*
 * Data for PCI driver interface
 */

MODULE_AUTHOR("Jeff Garzik, Matt Sottek");
MODULE_DESCRIPTION("Intel i8xx chipset Random Number Generator (RNG) driver");


static struct file_operations rng_chrdev_ops = {
	open:		rng_dev_open,
	release:	rng_dev_release,
	read:		rng_dev_read,
};


static struct miscdevice rng_miscdev = {
	RNG_MISCDEV_MINOR,
	RNG_MODULE_NAME,
	&rng_chrdev_ops,
};


/*
 * rng_init - initialize RNG module
 */
int __init rng_init (void)
{
	int rc;
	struct pci_dev *pdev;
	
	pdev = pci_find_device (0x8086, 0x2418, NULL);
	if (!pdev)
		pdev = pci_find_device (0x8086, 0x2428, NULL);
	if (!pdev)
		return -ENODEV;

	DPRINTK ("ENTER\n");

	rc = rng_init_one(pdev);
	if (rc) {
		DPRINTK ("EXIT, returning -ENODEV\n");
		return rc;
	}

	rc = misc_register (&rng_miscdev);
	if (rc) {
		if (rng_mem)
			iounmap (rng_mem);
		DPRINTK ("EXIT, returning %d\n", rc);
		return rc;
	}

	printk (KERN_INFO RNG_DRIVER_NAME " loaded\n");

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}

#ifdef MODULE

int init_module (void) { return rng_init (); }

/*
 * rng_init - shutdown RNG module
 */
void cleanup_module (void)
{
	DPRINTK ("ENTER\n");
	
	iounmap (rng_mem);

	rng_hwstatus_set (rng_hwstatus() & ~RNG_ENABLED);

	misc_deregister (&rng_miscdev);

	DPRINTK ("EXIT\n");
}

#endif /* MODULE */
