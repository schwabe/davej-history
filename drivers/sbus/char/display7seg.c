/* $Id: display7seg.c,v 1.1.2.1 2000/07/27 01:50:59 davem Exp $
 *
 * display7seg - Driver implementation for the 7-segment display
 * present on Sun Microsystems CP1400 and CP1500
 *
 * Copyright (c) 2000 Eric Brower (ebrower@usa.net)
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>		/* request_region, check_region */
#include <asm/ebus.h>			/* EBus device			*/
#include <asm/oplib.h>			/* OpenProm Library 		*/
#include <asm/uaccess.h>		/* put_/get_user_ret		*/

#include <asm/display7seg.h>

#define D7S_MINOR	193
#define D7S_OBPNAME	"display7seg"
#define D7S_DEVNAME "d7s"

static int sol_compat = 0;		/* Solaris compatibility mode	*/

#ifdef MODULE
EXPORT_NO_SYMBOLS;

/* Solaris compatibility flag -
 * The Solaris implementation omits support for several
 * documented driver features (ref Sun doc 806-0180-03).  
 * By default, this module supports the documented driver 
 * abilities, rather than the Solaris implementation:
 *
 * 	1) Device ALWAYS reverts to OBP-specified FLIPPED mode
 * 	   upon closure of device or module unload.
 * 	2) Device ioctls D7SIOCRD/D7SIOCWR honor toggling of
 * 	   FLIP bit
 *
 * If you wish the device to operate as under Solaris,
 * omitting above features, set this parameter to non-zero.
 */
MODULE_PARM
	(sol_compat, "1i");
MODULE_PARM_DESC
	(sol_compat, 
	 "Disables documented functionality omitted from Solaris driver");

MODULE_AUTHOR
	("Eric Brower <ebrower@usa.net>");
MODULE_DESCRIPTION
	("7-Segment Display driver for Sun Microsystems CP1400/1500");
MODULE_SUPPORTED_DEVICE
	("d7s");
#endif /* ifdef MODULE */

/*
 * Register block- see header for details
 * -----------------------------------------
 * | DP | ALARM | FLIP | 4 | 3 | 2 | 1 | 0 |
 * -----------------------------------------
 *
 * DP 		- Toggles decimal point on/off 
 * ALARM	- Toggles "Alarm" LED green/red
 * FLIP		- Inverts display for upside-down mounted board
 * bits 0-4	- 7-segment display contents
 */

struct d7s_regs {
	volatile __u8 regblk;
};

volatile struct d7s_regs *regs;

static inline void d7s_free(void)
{
	release_region((unsigned long)regs, sizeof(*regs));
}

static inline int d7s_obpflipped(void)
{
	int opt_node, ret;

	opt_node = prom_getchild(prom_root_node);
	opt_node = prom_searchsiblings(opt_node, "options");

	ret = prom_getintdefault(opt_node, "d7s-flipped?", -1);
	if (ret != -1)
		ret = 0;
	else
		ret = 1;

	return ret;
}

static int d7s_open(struct inode *inode, struct file *f)
{
	if (D7S_MINOR != MINOR(inode->i_rdev))
		return -ENODEV;

	MOD_INC_USE_COUNT;
	return 0;
}

static int d7s_release(struct inode *inode, struct file *f)
{
	if (D7S_MINOR != MINOR(inode->i_rdev))
		return -ENODEV;
	
	MOD_DEC_USE_COUNT;

	/* Reset flipped state to OBP default only if
	 * no other users have the device open and we
	 * are not operating in solaris-compat mode.
	 */
	if (MOD_IN_USE == 0 && sol_compat == 0) {
		if (d7s_obpflipped() == 0)
			regs->regblk |= D7S_FLIP;
		else
			regs->regblk &= ~D7S_FLIP;
	}

	return 0;
}

static int d7s_ioctl(struct inode *inode, struct file *f, 
		     unsigned int cmd, unsigned long arg)
{
	__u8 ireg = 0;

	if (D7S_MINOR != MINOR(inode->i_rdev))
		return -ENODEV;

	switch (cmd) {
	case D7SIOCWR:
		/* Assign device register values,
		 * we mask-out D7S_FLIP if in sol_compat mode.
		 */
		get_user_ret(ireg, (int *) arg, -EFAULT);
		if (sol_compat != 0) {
			if (regs->regblk & D7S_FLIP)
				ireg |= D7S_FLIP;
			else
				ireg &= ~D7S_FLIP;
		}
		regs->regblk = ireg;
		break;
	case D7SIOCRD:
		/* Retrieve device register values.
		 *
		 * NOTE: Solaris implementation returns D7S_FLIP bit
		 * as toggled by user, even though it does not honor it.
		 * This driver will not misinform you about the state
		 * of your hardware while in sol_compat mode.
		 */
		put_user_ret(regs->regblk, (int *) arg, -EFAULT);
		break;
	case D7SIOCTM:
		/* Toggle device mode-- flip display orientation. */
		if (regs->regblk & D7S_FLIP)
			regs->regblk &= ~D7S_FLIP;
		else
			regs->regblk |= D7S_FLIP;
		break;
	};

	return 0;
}

static struct file_operations d7s_fops = {
	NULL,			/* lseek 		*/
	NULL,			/* read			*/
	NULL,			/* write		*/
	NULL,			/* readdir		*/
	NULL,			/* select 		*/
	d7s_ioctl,
	NULL,			/* mmap 		*/
	d7s_open,
	NULL,			/* flush 		*/
	d7s_release,
 	NULL,			/* fsync 		*/
	NULL,			/* fasync 		*/
	NULL,			/* chk media chg*/
	NULL,			/* revalidate 	*/
 	NULL,			/* lock 		*/
};

static struct miscdevice d7s_miscdev = { D7S_MINOR, D7S_DEVNAME, &d7s_fops };

#ifdef MODULE
int init_module(void)
#else
__initfunc(int d7s_init(void))
#endif
{
	struct linux_ebus *ebus = NULL;
	struct linux_ebus_device *edev = NULL;
	int iTmp = 0;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, D7S_OBPNAME))
				goto ebus_done;
		}
	}

ebus_done:
	if (!edev) {
		printk("%s: unable to locate device\n", D7S_DEVNAME);
		return -ENODEV;
	}

	if (check_region(edev->base_address[0], sizeof(*regs))) {
		printk("%s: Can't get region %lx, %d\n",
		       __FUNCTION__, edev->base_address[0], (int)sizeof(*regs));
		return -ENODEV;
	}

	regs = (struct d7s_regs *) edev->base_address[0];
	request_region((unsigned long) regs, sizeof(*regs), D7S_OBPNAME);
	iTmp = misc_register(&d7s_miscdev);
	if (iTmp != 0) {
		printk("%s: unable to acquire miscdevice minor %i\n",
		       D7S_DEVNAME, D7S_MINOR);
		return iTmp;
	}

	/* OBP option "d7s-flipped?" is honored as default
	 * for the device, and reset default when detached
	 */
	iTmp = d7s_obpflipped();
	if (iTmp == 0)
		regs->regblk |= D7S_FLIP;
	else
		regs->regblk &= ~D7S_FLIP;

	printk("%s: 7-Segment Display%s at 0x%lx %s\n", 
		D7S_DEVNAME,
		(iTmp == 0) ? (" (FLIPPED)") : (""),
		edev->base_address[0],
		(sol_compat != 0) ? ("in sol_compat mode") : (""));
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	/* Honor OBP d7s-flipped? unless operating in solaris-compat mode. */
	if (sol_compat == 0) {
		if (d7s_obpflipped() == 0)
			regs->regblk |= D7S_FLIP;
		else
			regs->regblk &= ~D7S_FLIP;
	}

	misc_deregister(&d7s_miscdev);
	d7s_free();
}
#endif
