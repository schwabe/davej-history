
/*
 * atari_MFPser.h: Definitions for MFP serial ports
 *
 * Copyright 1994 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


#ifndef _ATARI_MFPSER_H
#define _ATARI_MFPSER_H

#define INIT_currMFP(info)	\
	volatile struct MFP *currMFP = (volatile struct MFP *)(info->port)

/* MFP input frequency, divided by 16 (USART prediv for async modes)
 * and 2 because of each timer run _toggles_ the output, resulting in
 * half the frequency, and 2 as greatest common divisor of all timer
 * modes.
 */
#define	MFP_BAUD_BASE	(2457600/16/2/2)

/* USART Control Register */

#define	UCR_PARITY_MASK		0x06
#define	UCR_PARITY_OFF		0x00
#define	UCR_PARITY_ODD		0x04
#define	UCR_PARITY_EVEN		0x06

#define	UCR_MODE_MASK		0x18
#define	UCR_SYNC_MODE		0x00
#define	UCR_ASYNC_1			0x08
#define	UCR_ASNYC_15		0x10
#define	UCR_ASYNC_2			0x18

#define	UCR_CHSIZE_MASK		0x60
#define	UCR_CHSIZE_8		0x00
#define	UCR_CHSIZE_7		0x20
#define	UCR_CHSIZE_6		0x40
#define	UCR_CHSIZE_5		0x60

#define	UCR_PREDIV			0x80

/* Receiver Status Register */

#define	RSR_RX_ENAB			0x01
#define	RSR_STRIP_SYNC		0x02
#define	RSR_SYNC_IN_PRGR	0x04	/* sync mode only */
#define	RSR_CHAR_IN_PRGR	0x04	/* async mode only */
#define	RSR_SYNC_SEARCH		0x08	/* sync mode only */
#define	RSR_BREAK_DETECT	0x08	/* async mode only */
#define	RSR_FRAME_ERR		0x10
#define	RSR_PARITY_ERR		0x20
#define	RSR_OVERRUN_ERR		0x40
#define	RSR_CHAR_AVAILABLE	0x80

/* Transmitter Status Register */

#define	TSR_TX_ENAB			0x01

#define	TSR_SOMODE_MASK		0x06
#define	TSR_SOMODE_OPEN		0x00
#define	TSR_SOMODE_LOW		0x02
#define	TSR_SOMODE_HIGH		0x04
#define	TSR_SOMODE_LOOP		0x06

#define	TSR_SEND_BREAK		0x08
#define	TSR_LAST_BYTE_SENT	0x10
#define	TSR_HALF_DUPLEX		0x20
#define	TSR_UNDERRUN		0x40
#define	TSR_BUF_EMPTY		0x80

/* Control signals in the GPIP */

#define	GPIP_DCD			0x02
#define	GPIP_CTS			0x04
#define	GPIP_RI				0x40

/* MFP speeders */
#define MFP_WITH_WEIRED_CLOCK	0x00
#define MFP_STANDARD		0x01
#define MFP_WITH_PLL		0x02
#define MFP_WITH_RSVE		0x03
#define MFP_WITH_RSFI		0x04


/* Convenience routine to access RTS and DTR in the Soundchip: It sets
 * the register to (oldvalue & mask) if mask is negative or (oldvalue
 * | mask) if positive. It returns the old value. I guess the
 * parameters will be constants most of the time, so gcc can throw
 * away the if statement if it isn't needed.
 */
 
static __inline__ unsigned char GIACCESS( int mask )
{
	unsigned long   cpu_status;
	unsigned char   old;

	save_flags(cpu_status);
	cli();

	sound_ym.rd_data_reg_sel = 14;
	old = sound_ym.rd_data_reg_sel;

	if (mask) {
		if (mask < 0)
			sound_ym.wd_data = old & mask;
		else
			sound_ym.wd_data = old | mask;
	}
	
	restore_flags(cpu_status);
	return( old );
}

#define	GI_RTS				0x08
#define	GI_DTR				0x10

#define	MFPser_RTSon()		GIACCESS( ~GI_RTS )
#define	MFPser_RTSoff()		GIACCESS( GI_RTS )
#define	MFPser_DTRon()		GIACCESS( ~GI_DTR )
#define	MFPser_DTRoff()		GIACCESS( GI_DTR )


int atari_MFPser_init( void );

#endif /* _ATARI_MFPSER_H */
