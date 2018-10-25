// SPDX-License-Identifier: GPL-2.0
/*
 * AVIRT - ALSA Virtual Soundcard
 *
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 * 
 * core.h - AVIRT internal header
 */

#ifndef __SOUND_AVIRT_CORE_H
#define __SOUND_AVIRT_CORE_H

#include <sound/avirt.h>

#include "utils.h"

extern struct snd_pcm_ops pcm_ops;

struct snd_avirt_core {
	struct snd_card *card;
	struct device *dev;
	struct class *avirt_class;
	struct config_group *stream_group;
	unsigned int stream_count;
	bool streams_sealed;
};

/**
 * snd_avirt_configfs_init - Initialise the configfs system
 * @core: The snd_avirt_core pointer
 * @return: 0 on success, negative ERRNO on failure
 */
int __init snd_avirt_configfs_init(struct snd_avirt_core *core);

/**
 * snd_avirt_configfs_exit - Clean up and exit the configfs system
 * @core: The snd_avirt_core pointer
 */
void __exit snd_avirt_configfs_exit(struct snd_avirt_core *core);

/**
 * snd_avirt_streams_seal - Register the sound card to user space
 * @return: 0 on success, negative ERRNO on failure
 */
int snd_avirt_streams_seal(void);

/**
 * snd_avirt_streams_sealed - Check if the streams have been sealed or not
 * @return: true if sealed, false otherwise
 */
bool snd_avirt_streams_sealed(void);

/**
 * snd_avirt_stream_find_by_device - Get audio stream from device number
 * @device: The PCM device number corresponding to the desired stream
 * @return: The audio stream if found, or an error pointer otherwise
 */
struct snd_avirt_stream *snd_avirt_stream_find_by_device(unsigned int device);

/**
 * snd_avirt_stream_create - Create audio stream, including it's ALSA PCM device
 * @name: The name designated to the audio stream
 * @direction: The PCM direction (SNDRV_PCM_STREAM_PLAYBACK or
 *             SNDRV_PCM_STREAM_CAPTURE)
 * @return: The newly created audio stream if successful, or an error pointer
 */
struct snd_avirt_stream *snd_avirt_stream_create(const char *name,
						 int direction);

#endif /* __SOUND_AVIRT_CORE_H */
