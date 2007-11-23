/*
 * Exported symbols for sequencer driver.
 * __NO_VERSION__ because this is still part of sound.o.
 */

#define __NO_VERSION__
#include <linux/module.h>

char sequencer_syms_symbol;

#include "sound_config.h"
#include "softoss.h"
#include "sound_calls.h"
/* Tuning */
#define _SEQUENCER_C_
#include "tuning.h"


struct symbol_table sequencer_symbols=
{
#include <linux/symtab_begin.h>
	X(note_to_freq),
	X(compute_finetune),
	X(seq_copy_to_input),
	X(seq_input_event),
	X(sequencer_init),
	X(sequencer_timer),

	X(sound_timer_init),
	X(sound_timer_interrupt),
	X(sound_timer_syncinterval),
	X(reprogram_timer),

	X(softsynthp),
	X(cent_tuning),
	X(semitone_tuning),
#include <linux/symtab_end.h>
};
