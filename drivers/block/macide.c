/*
 *  linux/drivers/block/macide.c -- Macintosh IDE Driver
 *
 *     Copyright (C) 1998 by Michael Schmitz
 *
 *  This driver was written based on information obtained from the MacOS IDE
 *  driver binary by Mikael Forselius
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/delay.h>

#include "ide.h"

#include <asm/machw.h>
#include <asm/macintosh.h>
#include <asm/macints.h>

    /*
     *  Base of the IDE interface (see ATAManager ROM code)
     */

#define MAC_HD_BASE	0x50f1a000

    /*
     *  Offsets from the above base (scaling 4)
     */

#define MAC_HD_DATA	0x00
#define MAC_HD_ERROR	0x04		/* see err-bits */
#define MAC_HD_NSECTOR	0x08		/* nr of sectors to read/write */
#define MAC_HD_SECTOR	0x0c		/* starting sector */
#define MAC_HD_LCYL	0x10		/* starting cylinder */
#define MAC_HD_HCYL	0x14		/* high byte of starting cyl */
#define MAC_HD_SELECT	0x18		/* 101dhhhh , d=drive, hhhh=head */
#define MAC_HD_STATUS	0x1c		/* see status-bits */
#define MAC_HD_CONTROL	0x38		/* control/altstatus */

static const int macide_offsets[IDE_NR_PORTS] = {
    MAC_HD_DATA, MAC_HD_ERROR, MAC_HD_NSECTOR, MAC_HD_SECTOR, MAC_HD_LCYL,
    MAC_HD_HCYL, MAC_HD_SELECT, MAC_HD_STATUS, MAC_HD_CONTROL
};

	/*
	 * Other registers
	 */

	/* 
	 * IDE interrupt status register for both (?) hwifs on Quadra
	 * Initial setting: 0xc
	 * Guessing again:
	 * Bit 0+1: some interrupt flags
	 * Bit 2+3: some interrupt enable
	 * Bit 4:   ??
	 * Bit 5:   IDE interrupt flag (any hwif)
	 * Bit 6:   maybe IDE interrupt enable (any hwif) ??
	 * Bit 7:   Any interrupt condition
	 *
	 * Only relevant item: bit 5, to be checked by mac_ack_intr
	 */

#define MAC_HD_ISR	0x101

static int mac_ack_intr(ide_hwif_t* hwif)
{
	unsigned char isr;
	isr = readb(MAC_HD_BASE + MAC_HD_ISR);
	if (isr & (1<<5)) {
		writeb(isr & ~(1<<5), MAC_HD_BASE + MAC_HD_ISR);
		return 1;
	}

	return 0;
}

    /*
     *  Probe for a Macintosh IDE interface
     */

__initfunc(int macide_probe_hwif(int index, ide_hwif_t *hwif))
{
	static int mac_index = 0;

	if (!MACH_IS_MAC || macintosh_config->ide_type == 0)
		return 0;

	if (!mac_index) {
		if (macintosh_config->ide_type == MAC_IDE_QUADRA)
			printk("ide%d: Macintosh Quadra IDE interface\n",
			       index);
		else
			printk("ide%d: Macintosh Powerbook IDE interface\n",
			       index);
		mac_index = index+1;
	}
	if (mac_index == index+1) {
		ide_setup_ports(hwif,
				(ide_ioreg_t)MAC_HD_BASE,
				macide_offsets, 
				0, NULL);

		if (macintosh_config->ide_type == MAC_IDE_QUADRA) {
			hwif->irq = IRQ_NUBUS_F;
		} else {
			/* PowerBooks - Slot C is the Comm Slot on the
			   Q630 */
			hwif->irq = IRQ_NUBUS_C;
		}
		hwif->ack_intr = mac_ack_intr;

		return 1;
	}
	return 0;
}
