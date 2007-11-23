/*
 *	The sound core exports the following symbols to the rest of
 *	modulespace.
 *
 *      (C) Copyright 1997      Alan Cox, Licensed under the GNU GPL
 */

#include <linux/module.h>
#include "sound_config.h"
#include "sound_calls.h"
#include "soundmodule.h"

char sound_syms_symbol;
extern int softoss_dev;

struct symbol_table sound_syms={
#include <linux/symtab_begin.h>
	X(mixer_devs),
	X(audio_devs),
	X(num_mixers),
	X(num_audiodevs),

	X(midi_devs),
	X(num_midis),
	X(synth_devs),
	X(num_synths),

	X(sound_timer_devs),
	X(num_sound_timers),

	X(sound_install_audiodrv),
	X(sound_install_mixer),
	X(sound_alloc_dma),
	X(sound_free_dma),
	X(sound_open_dma),
	X(sound_close_dma),
	X(sound_alloc_audiodev),
	X(sound_alloc_mididev),
	X(sound_alloc_mixerdev),
	X(sound_alloc_timerdev),
	X(sound_alloc_synthdev),
	X(sound_unload_audiodev),
	X(sound_unload_mididev),
	X(sound_unload_mixerdev),
	X(sound_unload_timerdev),
	X(sound_unload_synthdev),

	X(load_mixer_volumes),

	X(conf_printf),
	X(conf_printf2),

	X(softoss_dev),

/* Locking */
	X(sound_locker),
	X(sound_notifier_chain_register),
#include <linux/symtab_end.h>
};

MODULE_DESCRIPTION("Sound subsystem");
MODULE_AUTHOR("Hannu Savolainen, et al.");
