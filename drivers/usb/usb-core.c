/*
 * driver/usb/usb-core.c
 *
 * (C) Copyright David Waite 1999
 * based on code from usb.c, by Linus Torvalds
 *
 * The purpose of this file is to pull any and all generic modular code from
 * usb.c and put it in a separate file. This way usb.c is kept as a generic
 * library, while this file handles starting drivers, etc.
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/usb.h>

/*
 * USB core
 */
extern int usb_hub_init(void);
extern void usb_hub_cleanup(void);
extern int usb_major_init(void);
extern void usb_major_cleanup(void);


/*
 * Cleanup
 */
static void __exit usb_exit(void)
{
	usb_major_cleanup();
	usbdevfs_cleanup();
	usb_hub_cleanup();
}

/*
 * Init
 */
int usb_init(void)
{
	usb_major_init();
	usbdevfs_init();
	usb_hub_init();

	return 0;
}

module_init (usb_init);
module_exit (usb_exit);
