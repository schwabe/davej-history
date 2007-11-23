/*
 * linux/include/asm-arm/arch-ebsa285/system.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 */
#include <asm/dec21285.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/leds.h>

#define arch_do_idle()		processor.u.armv3v4._do_idle()

extern __inline__ void arch_reset(char mode)
{
	if (mode == 's') {
		/*
		 * Jump into ROM
		 */
		processor.u.armv3v4.reset(0x41000000);
	} else {
		if (machine_is_netwinder()) {
			/* open up the SuperIO chip
			 */
			outb(0x87, 0x370);
			outb(0x87, 0x370);

			/* aux function group 1 (logical device 7)
			 */
			outb(0x07, 0x370);
			outb(0x07, 0x371);

			/* set GP16 for WD-TIMER output
			 */
			outb(0xe6, 0x370);
			outb(0x00, 0x371);

			/* set a RED LED and toggle WD_TIMER for rebooting
			 */
			outb(0xc4, 0x338);
		} else {
			/* To reboot, we set up the 21285 watchdog and
			 * enable it.  We then wait for it to timeout.
			 */
			*CSR_TIMER4_LOAD = 0x8000;
			*CSR_TIMER4_CNTL = TIMER_CNTL_ENABLE |
					   TIMER_CNTL_AUTORELOAD |
					   TIMER_CNTL_DIV16;
			*CSR_SA110_CNTL |= 1 << 13;
		}
	}
}

#define arch_start_idle()	leds_event(led_idle_start)
#define arch_end_idle()		leds_event(led_idle_end)
