/*
 * linux/arch/m68k/mac/mackeyb.c
 *
 * Keyboard driver for Macintosh computers.
 *
 * Adapted from drivers/macintosh/key_mac.c and arch/m68k/atari/akakeyb.c 
 * (see that file for its authors and contributors).
 *
 * Copyright (C) 1997 Michael Schmitz.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * misc. keyboard stuff (everything not in adb-bus.c or keyb_m68k.c)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/init.h>
/* keyb */
#include <linux/keyboard.h>
#include <linux/random.h>
#include <linux/delay.h>
/* keyb */

#include <asm/setup.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
/* for keyboard_input stuff */
#include <asm/adb.h>
#define KEYB_KEYREG	0	/* register # for key up/down data */
#define KEYB_LEDREG	2	/* register # for leds on ADB keyboard */
#define MOUSE_DATAREG	0	/* reg# for movement/button codes from mouse */
/* end keyboard_input stuff */

#include <linux/kbd_kern.h>
#include <linux/kbd_ll.h>

static void kbd_repeat(unsigned long);
static struct timer_list repeat_timer = { NULL, NULL, 0, 0, kbd_repeat };
static int last_keycode;

static void input_keycode(int, int);

extern struct kbd_struct kbd_table[];

extern void adb_bus_init(void);
extern void handle_scancode(unsigned char, int);
extern void put_queue(int);

/* keyb */
static void mac_leds_done(struct adb_request *);
static void keyboard_input(unsigned char *, int, struct pt_regs *);
static void mouse_input(unsigned char *, int, struct pt_regs *);

#ifdef CONFIG_ADBMOUSE
/* XXX: Hook for mouse driver */
void (*adb_mouse_interrupt_hook)(unsigned char *, int);
/* Turn this on by default */
int adb_emulate_buttons = 1;
int adb_button2_keycode = 0x7c; /* right option key */
int adb_button3_keycode = 0x7d;	/* right control key */
#endif

/* The mouse driver - for debugging */
extern void adb_mouse_interrupt(char *, int);
/* end keyb */

/* this map indicates which keys shouldn't autorepeat. */
static unsigned char dont_repeat[128] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,	/* esc...option */
	0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, /* num lock */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, /* scroll lock */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/*
 * Mac private key maps
 */
u_short mac_plain_map[NR_KEYS] __initdata = {
	0xfb61,	0xfb73,	0xfb64,	0xfb66,	0xfb68,	0xfb67,	0xfb7a,	0xfb78,
	0xfb63,	0xfb76,	0xf200,	0xfb62,	0xfb71,	0xfb77,	0xfb65,	0xfb72,
	0xfb79,	0xfb74,	0xf031,	0xf032,	0xf033,	0xf034,	0xf036,	0xf035,
	0xf03d,	0xf039,	0xf037,	0xf02d,	0xf038,	0xf030,	0xf05d,	0xfb6f,
	0xfb75,	0xf05b,	0xfb69,	0xfb70,	0xf201,	0xfb6c,	0xfb6a,	0xf027,
	0xfb6b,	0xf03b,	0xf05c,	0xf02c,	0xf02f,	0xfb6e,	0xfb6d,	0xf02e,
	0xf009,	0xf020,	0xf060,	0xf07f,	0xf200,	0xf01b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xfb61,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

u_short mac_shift_map[NR_KEYS] __initdata = {
	0xfb41,	0xfb53,	0xfb44,	0xfb46,	0xfb48,	0xfb47,	0xfb5a,	0xfb58,
	0xfb43,	0xfb56,	0xf200,	0xfb42,	0xfb51,	0xfb57,	0xfb45,	0xfb52,
	0xfb59,	0xfb54,	0xf021,	0xf040,	0xf023,	0xf024,	0xf05e,	0xf025,
	0xf02b,	0xf028,	0xf026,	0xf05f,	0xf02a,	0xf029,	0xf07d,	0xfb4f,
	0xfb55,	0xf07b,	0xfb49,	0xfb50,	0xf201,	0xfb4c,	0xfb4a,	0xf022,
	0xfb4b,	0xf03a,	0xf07c,	0xf03c,	0xf03f,	0xfb4e,	0xfb4d,	0xf03e,
	0xf009,	0xf020,	0xf07e,	0xf07f,	0xf200,	0xf01b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xfb41,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf10e,	0xf10f,	0xf110,	0xf10c,	0xf111,	0xf112,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf203,	0xf200,	0xf113,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf20b,	0xf116,	0xf10d,	0xf117,
	0xf10b,	0xf20a,	0xf10a,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

u_short mac_altgr_map[NR_KEYS] __initdata = {
	0xf914,	0xfb73,	0xf917,	0xf919,	0xfb68,	0xfb67,	0xfb7a,	0xfb78,
	0xf916,	0xfb76,	0xf200,	0xf915,	0xfb71,	0xfb77,	0xf918,	0xfb72,
	0xfb79,	0xfb74,	0xf200,	0xf040,	0xf200,	0xf024,	0xf200,	0xf200,
	0xf200,	0xf05d,	0xf07b,	0xf05c,	0xf05b,	0xf07d,	0xf07e,	0xfb6f,
	0xfb75,	0xf200,	0xfb69,	0xfb70,	0xf201,	0xfb6c,	0xfb6a,	0xf200,
	0xfb6b,	0xf200,	0xf200,	0xf200,	0xf200,	0xfb6e,	0xfb6d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf90a,	0xf90b,	0xf90c,	0xf90d,	0xf90e,	0xf90f,
	0xf910,	0xf911,	0xf914,	0xf912,	0xf913,	0xf200,	0xf200,	0xf200,
	0xf510,	0xf511,	0xf512,	0xf50e,	0xf513,	0xf514,	0xf200,	0xf516,
	0xf200,	0xf10c,	0xf200,	0xf202,	0xf200,	0xf515,	0xf200,	0xf517,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf50f,	0xf117,
	0xf50d,	0xf119,	0xf50c,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

u_short mac_ctrl_map[NR_KEYS] __initdata = {
	0xf001,	0xf013,	0xf004,	0xf006,	0xf008,	0xf007,	0xf01a,	0xf018,
	0xf003,	0xf016,	0xf200,	0xf002,	0xf011,	0xf017,	0xf005,	0xf012,
	0xf019,	0xf014,	0xf200,	0xf000,	0xf01b,	0xf01c,	0xf01e,	0xf01d,
	0xf200,	0xf200,	0xf01f,	0xf01f,	0xf07f,	0xf200,	0xf01d,	0xf00f,
	0xf015,	0xf01b,	0xf009,	0xf010,	0xf201,	0xf00c,	0xf00a,	0xf007,
	0xf00b,	0xf200,	0xf01c,	0xf200,	0xf07f,	0xf00e,	0xf00d,	0xf20e,
	0xf200,	0xf000,	0xf000,	0xf008,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf001,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf204,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

u_short mac_shift_ctrl_map[NR_KEYS] __initdata = {
	0xf001,	0xf013,	0xf004,	0xf006,	0xf008,	0xf007,	0xf01a,	0xf018,
	0xf003,	0xf016,	0xf200,	0xf002,	0xf011,	0xf017,	0xf005,	0xf012,
	0xf019,	0xf014,	0xf200,	0xf000,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf01f,	0xf200,	0xf200,	0xf200,	0xf00f,
	0xf015,	0xf200,	0xf009,	0xf010,	0xf201,	0xf00c,	0xf00a,	0xf200,
	0xf00b,	0xf200,	0xf200,	0xf200,	0xf200,	0xf00e,	0xf00d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf001,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf200,	0xf117,
	0xf200,	0xf119,	0xf200,	0xf700,	0xf701,	0xf702,	0xf200,	0xf20c,
};

u_short mac_alt_map[NR_KEYS] __initdata = {
	0xf861,	0xf873,	0xf864,	0xf866,	0xf868,	0xf867,	0xf87a,	0xf878,
	0xf863,	0xf876,	0xf200,	0xf862,	0xf871,	0xf877,	0xf865,	0xf872,
	0xf879,	0xf874,	0xf831,	0xf832,	0xf833,	0xf834,	0xf836,	0xf835,
	0xf83d,	0xf839,	0xf837,	0xf82d,	0xf838,	0xf830,	0xf85d,	0xf86f,
	0xf875,	0xf85b,	0xf869,	0xf870,	0xf80d,	0xf86c,	0xf86a,	0xf827,
	0xf86b,	0xf83b,	0xf85c,	0xf82c,	0xf82f,	0xf86e,	0xf86d,	0xf82e,
	0xf809,	0xf820,	0xf860,	0xf87f,	0xf200,	0xf81b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf210,	0xf211,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf900,	0xf901,	0xf902,	0xf903,	0xf904,	0xf905,
	0xf906,	0xf907,	0xf861,	0xf908,	0xf909,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

u_short mac_ctrl_alt_map[NR_KEYS] __initdata = {
	0xf801,	0xf813,	0xf804,	0xf806,	0xf808,	0xf807,	0xf81a,	0xf818,
	0xf803,	0xf816,	0xf200,	0xf802,	0xf811,	0xf817,	0xf805,	0xf812,
	0xf819,	0xf814,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf80f,
	0xf815,	0xf200,	0xf809,	0xf810,	0xf201,	0xf80c,	0xf80a,	0xf200,
	0xf80b,	0xf200,	0xf200,	0xf200,	0xf200,	0xf80e,	0xf80d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf801,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

extern unsigned int keymap_count;

/*
 * Misc. defines for testing 
 */

extern int console_loglevel;

static struct adb_request led_request;
extern int in_keybinit;


/*
 * machdep keyboard routines, interface and key repeat method modeled after
 * drivers/macintosh/keyb_mac.c
 */

int mackbd_translate(unsigned char keycode, unsigned char *keycodep,
		      char raw_mode)
{
	if (!raw_mode) {
		/*
		 * Convert R-shift/control/option to L version.
		 * Remap keycode 0 (A) to the unused keycode 0x5a.
		 */
		switch (keycode) {
		case 0x7b: keycode = 0x38; break; /* R-shift */
		case 0x7c: keycode = 0x3a; break; /* R-option */
		case 0x7d: keycode = 0x36; break; /* R-control */
		case 0:	   keycode = 0x5a; break; /* A */
		}
	}

	*keycodep = keycode;
	return 1;
}

int mackbd_unexpected_up(unsigned char keycode)
{
	return 0x80;
}

static void
keyboard_input(unsigned char *data, int nb, struct pt_regs *regs)
{
	/* first check this is from register 0 */
	if (nb != 5 || (data[2] & 3) != KEYB_KEYREG)
		return;		/* ignore it */
	kbd_pt_regs = regs;
	input_keycode(data[3], 0);
	if (!(data[4] == 0xff || (data[4] == 0x7f && data[3] == 0x7f)))
		input_keycode(data[4], 0);
}

static void
input_keycode(int keycode, int repeat)
{
	struct kbd_struct *kbd;
	int up_flag;

 	kbd = kbd_table + fg_console;
	up_flag = (keycode & 0x80);
        keycode &= 0x7f;

	if (!repeat)
		del_timer(&repeat_timer);

#ifdef CONFIG_ADBMOUSE
	/*
	 * XXX: Add mouse button 2+3 fake codes here if mouse open.
	 *	Keep track of 'button' states here as we only send 
	 *	single up/down events!
	 *	Really messy; might need to check if keyboard is in
	 *	VC_RAW mode.
	 *	Might also want to know how many buttons need to be emulated.
	 *	-> hide this as function in arch/m68k/mac ?
	 */
	if (adb_emulate_buttons
	    && (keycode == adb_button2_keycode
		|| keycode == adb_button3_keycode)
	    && (adb_mouse_interrupt_hook || console_loglevel == 10)) {
		int button;
		/* faked ADB packet */
		static unsigned char data[4] = { 0, 0x80, 0x80, 0x80 };

		button = keycode == adb_button2_keycode? 2: 3;
		if (data[button] != up_flag) {
			/* send a fake mouse packet */
			data[button] = up_flag;
			if (console_loglevel >= 8)
				printk("fake mouse event: %x %x %x\n",
				       data[1], data[2], data[3]);
			if (adb_mouse_interrupt_hook)
				adb_mouse_interrupt_hook(data, 4);
		}
		return;
	}
#endif /* CONFIG_ADBMOUSE */

	/*
	 * This should not be done in raw mode, but basically X is
	 * all screwed up, so we try to make it less so by adjusting
	 * things.  Note that this is also redundant with
	 * mackbd_translate() above.  Either we are wrong somewhere
	 * or X is wrong, and I'm betting on the latter.
	 */
	switch (keycode) {
		case 0x7b: keycode = 0x38; break; /* R-shift */
		case 0x7c: keycode = 0x3a; break; /* R-option */
		case 0x7d: keycode = 0x36; break; /* R-control */
		case 0x0:  if (kbd->kbdmode != VC_RAW) 
			       keycode = 0x5a; /* A; keycode 0 deprecated */
			       break; 
	}

	if (kbd->kbdmode != VC_RAW) {
		if (!up_flag && !dont_repeat[keycode]) {
			last_keycode = keycode;
			repeat_timer.expires = jiffies + (repeat? HZ/15: HZ/2);
			add_timer(&repeat_timer);
		}

		/*
		 * XXX fix caps-lock behaviour by turning the key-up
		 * transition into a key-down transition.
		 * MSch: need to turn each caps-lock event into a down-up
		 * double event (keyboard code assumes caps-lock is a toggle)
		 * 981127: fix LED behavior (kudos atong!)
		 */
		switch (keycode) {
		case 0x39:
			handle_scancode(keycode, 1);	/* down */
			up_flag = 0x80;			/* see below ... */
		 	mark_bh(KEYBOARD_BH);
			break;
		 case 0x47:
		 	mark_bh(KEYBOARD_BH);
		 	break;
		}
	}

	handle_scancode(keycode, !up_flag);
}

static void
kbd_repeat(unsigned long xxx)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	input_keycode(last_keycode, 1);
	restore_flags(flags);
}

  /* [ACA:23-Mar-97] Three button mouse support.  This is designed to
     function with MkLinux DR-2.1 style X servers.  It only works with
     three-button mice that conform to Apple's multi-button mouse
     protocol. */

  /*
    The X server for MkLinux DR2.1 uses the following unused keycodes to
    read the mouse:

    0x7e  This indicates that the next two keycodes should be interpreted
          as mouse information.  The first following byte's high bit
          represents the state of the left button.  The lower seven bits
          represent the x-axis acceleration.  The lower seven bits of the
          second byte represent y-axis acceleration.

    0x3f  The x server interprets this keycode as a middle button
          release.

    0xbf  The x server interprets this keycode as a middle button
          depress.

    0x40  The x server interprets this keycode as a right button
          release.

    0xc0  The x server interprets this keycode as a right button
          depress.

    NOTES: There should be a better way of handling mice in the X server.
    The MOUSE_ESCAPE code (0x7e) should be followed by three bytes instead
    of two.  The three mouse buttons should then, in the X server, be read
    as the high-bits of all three bytes.  The x and y motions can still be
    in the first two bytes.  Maybe I'll do this...
  */

  /*
    Handler 4 -- Apple Extended mouse protocol.

    For Apple's 3-button mouse protocol the data array will contain the
    following values:

		BITS    COMMENTS
    data[0] = 0000 0000 ADB packet identifer.
    data[1] = 0100 0000 Extended protocol register.
	      Bits 6-7 are the device id, which should be 1.
	      Bits 4-5 are resolution which is in "units/inch".
	      The Logitech MouseMan returns these bits clear but it has
	      200/300cpi resolution.
	      Bits 0-3 are unique vendor id.
    data[2] = 0011 1100 Bits 0-1 should be zero for a mouse device.
	      Bits 2-3 should be 8 + 4.
		      Bits 4-7 should be 3 for a mouse device.
    data[3] = bxxx xxxx Left button and x-axis motion.
    data[4] = byyy yyyy Second button and y-axis motion.
    data[5] = byyy bxxx Third button and fourth button.  Y is additional
	      high bits of y-axis motion.  XY is additional
	      high bits of x-axis motion.

    NOTE: data[0] and data[2] are confirmed by the parent function and
    need not be checked here.
  */

  /*
    Handler 1 -- 100cpi original Apple mouse protocol.
    Handler 2 -- 200cpi original Apple mouse protocol.

    For Apple's standard one-button mouse protocol the data array will
    contain the following values:

                BITS    COMMENTS
    data[0] = 0000 0000 ADB packet identifer.
    data[1] = ???? ???? (?)
    data[2] = ???? ??00 Bits 0-1 should be zero for a mouse device.
    data[3] = bxxx xxxx First button and x-axis motion.
    data[4] = byyy yyyy Second button and y-axis motion.

    NOTE: data[0] is confirmed by the parent function and need not be
    checked here.
  */

static void
mouse_input(unsigned char *data, int nb, struct pt_regs *regs)
{
#ifdef DEBUG_ADB
	if (console_loglevel == 10 &&
	    (nb < 5 || nb > 6 || (data[2] & 3) != MOUSE_DATAREG)) {
		int i;

		printk(KERN_DEBUG "data from mouse:");
		for (i = 0; i < nb; ++i)
			printk(" %x", data[i]);
		printk("\n");
		return;
	}
#endif

	if (adb_mouse_interrupt_hook) {
		adb_mouse_interrupt_hook(data+2, nb-2);
		/*
		 * passing the mouse data to i.e. the X server as done for
		 * Xpmac will confuse applications on a sane X server :-)
		 */
		return;
	} 
}

/* Map led flags as defined in kbd_kern.h to bits for Apple keyboard. */
static unsigned char mac_ledmap[8] = {
    0,		/* none */
    4,		/* scroll lock */
    1,		/* num lock */
    5,		/* scroll + num lock */
    2,		/* caps lock */
    6,		/* caps + scroll lock */
    3,		/* caps + num lock */
    7,		/* caps + num + scroll lock */
};

static int leds_pending;

void mackbd_leds(unsigned int leds)
{
	if (led_request.complete) {
		if (console_loglevel == 10)
			printk(KERN_DEBUG "mackbd_leds: got reply, sending request!\n");
		adb_request(&led_request, mac_leds_done, 4, ADB_PACKET,
			     ADB_WRITEREG(ADB_KEYBOARD, KEYB_LEDREG),
			     0xff, ~mac_ledmap[leds]);
	} else
		leds_pending = leds | 0x100;
}

static void mac_leds_done(struct adb_request *req)
{
	int leds;

	if (leds_pending) {
		leds = leds_pending & 0xff;
		leds_pending = 0;
		mackbd_leds(leds);
	}
	mark_bh(KEYBOARD_BH);
}

__initfunc(int mackbd_init_hw(void))
{
	static struct adb_request autopoll_req, confcod_req;
	int ct;

	/* setup key map */
	memcpy(key_maps[0], mac_plain_map, sizeof(plain_map));
	memcpy(key_maps[1], mac_shift_map, sizeof(plain_map));
	memcpy(key_maps[2], mac_altgr_map, sizeof(plain_map));
	memcpy(key_maps[4], mac_ctrl_map, sizeof(plain_map));
	memcpy(key_maps[5], mac_shift_ctrl_map, sizeof(plain_map));
	memcpy(key_maps[8], mac_alt_map, sizeof(plain_map));
	memcpy(key_maps[12], mac_ctrl_alt_map, sizeof(plain_map));

	/* initialize mouse interrupt hook */
	adb_mouse_interrupt_hook = NULL;
	
	/* the input functions ... */	
	adb_register(ADB_KEYBOARD, keyboard_input);
	adb_register(ADB_MOUSE, mouse_input);

	/* turn on ADB auto-polling in the CUDA */
	
	/*
	 *	Older boxes don't support CUDA_* targets and CUDA commands
	 *	instead we emulate them in the adb_request hook to make
	 *	the code interfaces saner.
	 *
	 *	Note XXX: the Linux PMac and this code both assume the
	 *	devices are at their primary ids and do not do device
	 *	assignment. This isn't ideal. We should fix it to follow
	 *	the reassignment specs.
	 */

	if (macintosh_config->adb_type == MAC_ADB_CUDA) {
		printk(KERN_DEBUG "CUDA autopoll on ...\n");
		adb_request(&autopoll_req, NULL, 3, CUDA_PACKET, CUDA_AUTOPOLL, 1);
		ct = 0;
		while (!autopoll_req.complete && ct++ < 1000)
			adb_poll();
		if (ct == 1000) printk(KERN_ERR "ADB timeout\n");
		autopoll_req.complete = 1;
	}

	/*
	 *	XXX: all ADB requests now in CUDA format; adb_request takes 
	 *	care of that for other Macs.
	 */

	printk(KERN_DEBUG "Configuring keyboard:\n");
	/* Aarrrgggh!  Die in hell! */
	udelay(8000);

	/* 
	 * turn on all leds - the keyboard driver will turn them back off 
	 * via mackbd_leds if everything works ok!
	 */
	printk(KERN_DEBUG "leds on ...\n");
	adb_request(&led_request, NULL, 4, ADB_PACKET,
		     ADB_WRITEREG(ADB_KEYBOARD, KEYB_LEDREG), 0xff, ~7);

	/*
	 * The polling stuff should go away as soon as the ADB driver is stable
	 */
	ct = 0;
	while (!led_request.complete && ct++ < 1000)
		adb_poll();
	if (ct == 1000) printk(KERN_ERR "ADB timeout\n");
	led_request.complete = 1;

	printk(KERN_DEBUG "configuring coding mode ...\n");
	udelay(8000);

	/* 
	 * get the keyboard to send separate codes for
	 * left and right shift, control, option keys. 
	 */
	adb_request(&confcod_req, NULL, 4, ADB_PACKET, 
		     ADB_WRITEREG(ADB_KEYBOARD, 3), 0, 3);
	ct = 0;
	while (!confcod_req.complete && ct++ < 1000)
		adb_poll();
	if (ct == 1000) printk(KERN_ERR "ADB timeout\n");
	confcod_req.complete = 1;

	in_keybinit = 0;
	printk(KERN_DEBUG "keyboard init done\n");
	udelay(8000);

	return 0;
}
