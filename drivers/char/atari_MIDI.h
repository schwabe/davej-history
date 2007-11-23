/*
 * atari_MIDI.h: Definitions for the MIDI serial port
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#ifndef _ATARI_MIDI_H
#define _ATARI_MIDI_H

int atari_MIDI_init( void );


#ifdef _MIDI_HSK_LINES_

#define	MIDI_RTSon()
#define	MIDI_RTSoff()
#define	MIDI_DTRon()
#define	MIDI_DTRoff()

else

#define	MIDI_RTSon
#define	MIDI_RTSoff
#define	MIDI_DTRon
#define	MIDI_DTRoff

#endif

#endif /* _ATARI_MIDI_H */
