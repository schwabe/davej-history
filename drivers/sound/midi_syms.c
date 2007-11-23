/*
 * Exported symbols for midi driver.
 * __NO_VERSION__ because this is still part of sound.o.
 */

#define __NO_VERSION__
#include <linux/module.h>

char midi_syms_symbol;

#include "sound_config.h"
#define _MIDI_SYNTH_C_
#include "midi_synth.h"

struct symbol_table midi_syms=
{
#include <linux/symtab_begin.h>
	X(do_midi_msg),
	X(midi_synth_open),
	X(midi_synth_close),
	X(midi_synth_ioctl),
	X(midi_synth_kill_note),
	X(midi_synth_start_note),
	X(midi_synth_set_instr),
	X(midi_synth_reset),
	X(midi_synth_hw_control),
	X(midi_synth_aftertouch),
	X(midi_synth_controller),
	X(midi_synth_panning),
	X(midi_synth_setup_voice),
	X(midi_synth_send_sysex),
	X(midi_synth_bender),
	X(midi_synth_load_patch),
	X(MIDIbuf_avail),
#include <linux/symtab_end.h>
};