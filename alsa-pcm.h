// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * alsa-pcm.h - AVIRT ALSA PCM interface
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#ifndef __AVIRT_ALSA_PCM_H__
#define __AVIRT_ALSA_PCM_H__

#include "core.h"

extern struct avirt_coreinfo coreinfo;
extern struct snd_pcm_ops pcm_ops;

/**
 * pcm_buff_complete_cb - PCM buffer complete callback
 * @substream: pointer to ALSA PCM substream
 * @return 0 on success or error code otherwise
 *
 * This should be called from a child Audio Path once it has finished processing
 * the PCM buffer
 */
int pcm_buff_complete_cb(struct snd_pcm_substream *substream);

#endif // __AVIRT_ALSA_PCM_H__
