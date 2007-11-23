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
#include <linux/usb.h>

/*
 * USB core
 */

int usb_hub_init(void);
void usb_hub_cleanup(void);
int usb_major_init(void);
void usb_major_cleanup(void);

/*
 * USB device drivers
 */

int input_init_module(void);
int acm_init_module(void);
int usb_dc2xx_init_module(void);
int evdev_init_module(void);
int hid_init_module(void);
int joydev_init_module(void);
int keybdev_init_module(void);
int mousedev_init_module(void);
int microtek_drv_init_module(void);
int usblp_init_module(void);
int usb_ov511_init_module(void);
int usb_rio_init_module(void);
int usb_scanner_init_module(void);
int usb_serial_init_module(void);
int usb_stor_init_module(void);
int usb_kbd_init_module(void);
int usb_mouse_init_module(void);
int wacom_init_module(void);
int wmforce_init_module(void);

int usb_audio_init(void);
int usb_ibmcam_init(void);
int dabusb_init(void);
int plusb_init(void);
int dsbr100_init(void);

/*
 * HCI drivers
 */

int uhci_init(void);
int ohci_hcd_init(void);

#ifdef MODULE

/*
 * Cleanup
 */

void cleanup_module(void)
{
	usb_major_cleanup();
	usbdevfs_cleanup();
	usb_hub_cleanup();
}

/*
 * Init
 */

int init_module(void)
#else
int usb_init(void)
#endif
{
	usb_major_init();
	usbdevfs_init();
	usb_hub_init();

#ifndef CONFIG_USB_MODULE
#ifdef CONFIG_USB_AUDIO
	usb_audio_init();
#endif
#ifdef CONFIG_USB_IBMCAM
	usb_ibmcam_init();
#endif
#ifdef CONFIG_USB_DABUSB
	dabusb_init();
#endif
#ifdef CONFIG_USB_DSBR
	dsbr100_init();
#endif
#ifdef CONFIG_USB_PLUSB
	plusb_init();
#endif

#if defined(CONFIG_INPUT_KEYBDEV) || defined(CONFIG_INPUT_MOUSEDEV) || \
    defined(CONFIG_INPUT_JOYDEV)  || defined(CONFIG_INPUT_EVDEV)    || \
    defined(CONFIG_INPUT_KBD)     || defined(CONFIG_INPUT_MOUSE)    || \
    defined(CONFIG_INPUT_WACOM)   || defined(CONFIG_INPUT_WMFORCE)  || \
    defined(CONFIG_INPUT_HID)
	input_init_module();
#endif
#ifdef CONFIG_USB_ACM
       acm_init_module();
#endif
#ifdef CONFIG_USB_DC2XX
       usb_dc2xx_init_module();
#endif
#ifdef CONFIG_INPUT_EVDEV
       evdev_init_module();
#endif
#ifdef CONFIG_USB_HID
       hid_init_module();
#endif
#ifdef CONFIG_INPUT_JOYDEV
       joydev_init_module();
#endif
#ifdef CONFIG_INPUT_KEYBDEV
       keybdev_init_module();
#endif
#ifdef CONFIG_INPUT_MOUSEDEV
       mousedev_init_module();
#endif
#ifdef CONFIG_USB_MICROTEK
	microtek_drv_init_module();
#endif
#ifdef CONFIG_USB_PRINTER
       usblp_init_module();
#endif
#ifdef CONFIG_VIDEO_OV511
	usb_ov511_init_module();
#endif
#ifdef CONFIG_USB_RIO500
       usb_rio_init_module();
#endif
#ifdef CONFIG_USB_SCANNER
       usb_scanner_init_module();
#endif
#ifdef CONFIG_USB_SERIAL
       usb_serial_init_module();
#endif
#ifdef CONFIG_USB_STORAGE
       usb_stor_init_module();
#endif
#ifdef CONFIG_USB_KBD
       usb_kbd_init_module();
#endif
#ifdef CONFIG_USB_MOUSE
       usb_mouse_init_module();
#endif
#ifdef CONFIG_USB_WACOM
       wacom_init_module();
#endif
#ifdef CONFIG_USB_WMFORCE
       wmforce_init_module();
#endif
#ifdef CONFIG_USB_UHCI
	uhci_init();
#endif
#ifdef CONFIG_USB_UHCI_ALT
	uhci_init();
#endif
#ifdef CONFIG_USB_OHCI
	ohci_hcd_init(); 
#endif
#endif
	return 0;
}
