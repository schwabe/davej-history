/*
 **********************************************************************
 *     voicemgr.c - Voice manager for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#include "voicemgr.h"
#include "8010.h"

int emu10k1_voice_alloc(struct emu10k1_card *card, struct emu_voice *voice, struct voice_allocdesc *voiceallocdesc)
{
	u8 *voicetable = card->voicetable;
	int i;
	unsigned long flags;

	DPF(2, "emu10k1_voice_alloc()\n");

	spin_lock_irqsave(&card->lock, flags);

	if (voiceallocdesc->flags & VOICE_FLAGS_STEREO) {
		for (i = 0; i < NUM_G; i += 2)
			if ((voicetable[i] == VOICE_USAGE_FREE) && (voicetable[i + 1] == VOICE_USAGE_FREE)) {
				voicetable[i] = voiceallocdesc->usage;
				voicetable[i + 1] = voiceallocdesc->usage;
				break;
			}
	} else {
		for (i = 0; i < NUM_G; i++)
			if (voicetable[i] == VOICE_USAGE_FREE) {
				voicetable[i] = voiceallocdesc->usage;
				break;
			}
	}

	spin_unlock_irqrestore(&card->lock, flags);

	if (i >= NUM_G)
		return -1;

	voice->card = card;
	voice->flags = voiceallocdesc->flags;
	voice->usage = voiceallocdesc->usage;
	voice->num = i;

#ifdef PRIVATE_PCM_VOLUME

	for (i = 0; i < MAX_PCM_CHANNELS; i++) {
		if (sblive_pcm_volume[i].files == current->files) {
			sblive_pcm_volume[i].channel_l = voice->num;
			DPD(2, "preset left: %d\n", voice->num);
			if (voice->flags & VOICE_FLAGS_STEREO) {
				sblive_pcm_volume[i].channel_r = voice->num + 1;
				DPD(2, "preset right: %d\n", voice->num + 1);
			}
			break;
		}
	}
#endif

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		DPD(2, " voice allocated -> %d\n", voice->num + i);

		sblive_writeptr_tag(card, voice->num + i, IFATN, 0xffff,
							DCYSUSV, 0,
							VTFT, 0x0000ffff,
							PTRX, 0,
							TAGLIST_END);
	}

	return 0;
}

void emu10k1_voice_free(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	int i;
	u32 cra, sample;
	unsigned long flags;

	DPF(2, "emu10k1_voice_free()\n");

	if (voice->usage == VOICE_USAGE_FREE)
		return;

#ifdef PRIVATE_PCM_VOLUME
	for (i = 0; i < MAX_PCM_CHANNELS; i++) {
		if (sblive_pcm_volume[i].files == current->files) {
			if (voice->num == sblive_pcm_volume[i].channel_l)
				sblive_pcm_volume[i].channel_l = NUM_G;
			if ((voice->flags & VOICE_FLAGS_STEREO)
			    && (voice->num + 1) == sblive_pcm_volume[i].channel_r) {
				sblive_pcm_volume[i].channel_r = NUM_G;
			}
			break;
		}
	}
#endif

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		int cra1;

		DPD(2, " voice freed -> %d\n", voice->num + i);

		sblive_writeptr_tag(card, voice->num + i, DCYSUSV, 0, 
							VTFT, 0x0000ffff,
							PTRX_PITCHTARGET, 0,
							CVCF, 0x0000ffff,
							CPF, 0,
							TAGLIST_END);

		sample = (voice->flags & VOICE_FLAGS_16BIT) ? 0 : 0x80808080;
		cra = sblive_readptr(card, CCR, voice->num) & CCR_READADDRESS_MASK;
		cra1 = (cra >> 18) & 0xf;

		sblive_writeptr_tag(card, voice->num + i, CCR, cra,
							CD0 + cra1, sample,
							CD0 + ((cra1 + 0x1) & 0xf), sample,
							TAGLIST_END);
	}

	voice->usage = VOICE_USAGE_FREE;

	spin_lock_irqsave(&card->lock, flags);

	card->voicetable[voice->num] = VOICE_USAGE_FREE;

	if (voice->flags & VOICE_FLAGS_STEREO)
		card->voicetable[voice->num + 1] = VOICE_USAGE_FREE;

	spin_unlock_irqrestore(&card->lock, flags);

	return;
}

/*       Sets up a voices for Wave Playback */

void emu10k1_voice_playback_setup(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	u32 sample, cra = 0, start = 0;
	int i;

	DPF(2, "emu10k1_voice_playback_setup()\n");

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		sblive_writeptr(card, FXRT, voice->num + i, voice->params[i].bus_routing << 16);

		/* Stop CA */
		/* Assumption that PT is alreadt 0 so no harm overwriting */
		sblive_writeptr(card, PTRX, voice->num + i, (voice->params[i].send_a << 8) | voice->params[i].send_b);

		if (i == 0) {
			if (voice->flags & VOICE_FLAGS_STEREO) {
				/* Set stereo bit */
				cra = 64;
				sblive_writeptr(card, CPF, voice->num, CPF_STEREO_MASK);
				sblive_writeptr(card, CPF, voice->num + 1, CPF_STEREO_MASK);
			} else {
				cra = 32;
				sblive_writeptr(card, CPF, voice->num, 0);
			}

			if (voice->flags & VOICE_FLAGS_16BIT)
				sample = 0;
			else {
				cra = cra * 2;
				sample = 0x80808080;
			}
			cra -= 4;

			if (voice->flags & VOICE_FLAGS_STEREO) {
				/* CCR_READADDRESS_MASK */
				sblive_writeptr(card, CCR, voice->num, 0x3c << 16);
				sblive_writeptr_tag(card, voice->num + 1, CCR, cra << 16, CDE, sample, CDF, sample, TAGLIST_END);
				start = voice->start + cra / 2;
			} else {
				sblive_writeptr_tag(card, voice->num, CCR, 0x1c << 16,	/* FIXME: Is 0x1c correct? */
						    CDE, sample, CDF, sample, TAGLIST_END);
				start = voice->start + cra;
			}

			if (start > voice->endloop) {
				start -= voice->endloop;

				if (voice->flags & VOICE_FLAGS_STEREO)
					cra = (cra << 25) | 0x1bc0000 | ((cra - start) << 9);
				else
					cra = (cra << 25) | 0x11c0000 | ((cra - start) << 9);

				start += voice->startloop;

				if (start >= voice->endloop)
					start = voice->endloop - 1;
			} else if (voice->flags & VOICE_FLAGS_STEREO)
				cra = (cra << 25) | (0x3c << 16);
			else
				cra = (cra << 25) | (0x1c << 16);

			start |= CCCA_INTERPROM_0;
		}

		/* CSL, ST, CA */
		sblive_writeptr_tag(card, voice->num + i,
				    DSL, voice->endloop | (voice->params[i].send_d << 24),
				    PSST, voice->startloop | (voice->params[i].send_c << 24),
				    CCCA, start | ((voice->flags & VOICE_FLAGS_16BIT) ? 0 : CCCA_8BITSELECT),
				    /* Clear filter delay memory */
				    Z1, 0,
				    Z2, 0,
				    /* Invalidate maps */
				    MAPA, MAP_PTI_MASK | (card->silentpage.dma_handle * 2),
				    MAPB, MAP_PTI_MASK | (card->silentpage.dma_handle * 2), TAGLIST_END);

		/* Fill cache */
		if (i == 0)
			sblive_writeptr(card, CCR, voice->num, cra);

		/* modulation envelope */
		sblive_writeptr_tag(card, voice->num + i,
				    CVCF, 0x0000ffff,
				    VTFT, 0x0000ffff,
				    ATKHLDM, 0,
				    DCYSUSM, 0x007f,
				    LFOVAL1, 0x8000,
				    LFOVAL2, 0x8000,
				    FMMOD, 0,
				    TREMFRQ, 0,
				    FM2FRQ2, 0,
				    ENVVAL, 0x8000,
				    /* volume envelope */
				    ATKHLDV, 0x7f7f,
				    ENVVOL, 0x8000,
				    /* filter envelope */
				    PEFE_FILTERAMOUNT, 0x7f,
				    /* pitch envelope */
				    PEFE_PITCHAMOUNT, 0, TAGLIST_END);

#ifdef PRIVATE_PCM_VOLUME
{
int j;
        for (j = 0; j < MAX_PCM_CHANNELS; j++) {
                if (sblive_pcm_volume[j].channel_l == voice->num + i) {
                        voice->params[i].initial_attn = (sblive_pcm_volume[j].channel_r < NUM_G) ? sblive_pcm_volume[i].attn_l :
                // test for mono channel (reverse logic is correct here!)
                                            (sblive_pcm_volume[j].attn_r >
                                             sblive_pcm_volume[j].attn_l) ? sblive_pcm_volume[j].attn_l : sblive_pcm_volume[j].attn_r;
                                        DPD(2, "set left volume %d\n", voice->params[i].initial_attn);
                                        break;
                                } else if (sblive_pcm_volume[j].channel_r == voice->num + i) {
                                        voice->params[i].initial_attn = sblive_pcm_volume[j].attn_r;
                                        DPD(2, "set right volume %d\n", voice->params[i].initial_attn);
                                        break;
                                }
                        }
                }
#endif

		voice->params[i].fc_target = 0xffff;
	}

	return;
}

void emu10k1_voice_start(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	int i;

	DPF(2, "emu10k1_voice_start()\n");

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		sblive_writeptr_tag(card, voice->num + i,
				    IFATN, (voice->params[i].initial_fc << 8) | voice->params[i].initial_attn,
				    VTFT, (voice->params[i].volume_target << 16) | voice->params[i].fc_target,
				    CVCF, (voice->params[i].volume_target << 16) | voice->params[i].fc_target,
				    DCYSUSV, (voice->params[i].byampl_env_sustain << 8) | voice->params[i].byampl_env_decay,
				    TAGLIST_END);

		/* Using StopOnLoop for MIDI stops the playback
		   too early, which may cause a DC level to be played
		   until the note is released. */

		if (voice->usage == VOICE_USAGE_MIDI)
			emu10k1_clear_stop_on_loop(card, voice->num + i);
		else {
			if (voice->startloop > voice->end)
				emu10k1_set_stop_on_loop(card, voice->num + i);
			else
				emu10k1_clear_stop_on_loop(card, voice->num + i);
		}

		sblive_writeptr(card, PTRX_PITCHTARGET, voice->num + i, voice->pitch_target);

		if (i == 0)
			sblive_writeptr(card, CPF_CURRENTPITCH, voice->num, voice->pitch_target);

		sblive_writeptr(card, IP, voice->num + i, voice->initial_pitch);
	}

	return;
}

void emu10k1_voice_stop(struct emu_voice *voice)
{
	struct emu10k1_card *card = voice->card;
	int i;

	DPF(2, "emu10k1_voice_stop()\n");

	for (i = 0; i < (voice->flags & VOICE_FLAGS_STEREO ? 2 : 1); i++) {
		sblive_writeptr_tag(card, voice->num + i, IFATN, 0xffff,
				    VTFT, 0x0000ffff,
				    PTRX_PITCHTARGET, 0,
				    IP, 0,
				    TAGLIST_END);
	}

	return;
}

void emu10k1_voice_setcontrol(struct emu_voice *voice, struct voice_cntlset *setting, u32 numparam)
{
	struct emu10k1_card *card = voice->card;
	int count;

	for (count = 0; count < numparam; count++)
		sblive_writeptr(card, setting[count].paramID, voice->num, setting[count].value);

	return;
}

void emu10k1_voice_getcontrol(struct emu_voice *voice, u32 controlid, u32 * value)
{
	struct emu10k1_card *card = voice->card;

	*value = sblive_readptr(card, controlid, voice->num);

	return;
}
