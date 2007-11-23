/*
 * Exported symbols for audio driver.
 * __NO_VERSION__ because this is still part of sound.o.
 */

#define __NO_VERSION__
#include <linux/module.h>

char audio_syms_symbol;

#include "sound_config.h"
#include "sound_calls.h"

struct symbol_table audio_symbol_table = {
#include <linux/symtab_begin.h>
	X(DMAbuf_start_dma),
	X(DMAbuf_open_dma),
	X(DMAbuf_close_dma),
	X(DMAbuf_inputintr),
	X(DMAbuf_outputintr),
	X(dma_ioctl),
	X(audio_open),
	X(audio_release),
#include <linux/symtab_end.h>
};