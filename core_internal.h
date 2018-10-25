// SPDX-License-Identifier: GPL-2.0
/*
 * AVIRT - ALSA Virtual Soundcard
 *
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 * 
 * core_internal.h - AVIRT internal header
 */

#ifndef __AVIRT_CORE_INTERNAL_H__
#define __AVIRT_CORE_INTERNAL_H__

#include <avirt/core.h>

#include "utils.h"

struct avirt_core {
	struct snd_card *card;
	struct device *dev;
	struct class *avirt_class;
	struct config_group *stream_group;
	unsigned int stream_count;
	bool streams_sealed;
};

/**
 * __avirt_configfs_init - Initialise the configfs system
 * @core: The avirt_core pointer
 * @return: 0 on success, negative ERRNO on failure
 */
int __init __avirt_configfs_init(struct avirt_core *core);

/**
 * __avirt_configfs_exit - Clean up and exit the configfs system
 * @core: The avirt_core pointer
 */
void __exit __avirt_configfs_exit(struct avirt_core *core);

/**
 * __avirt_streams_seal - Register the sound card to user space
 * @return: 0 on success, negative ERRNO on failure
 */
int __avirt_streams_seal(void);

/**
 * __avirt_streams_sealed - Check whether the streams have been sealed or not
 * @return: true if sealed, false otherwise
 */
bool __avirt_streams_sealed(void);

/**
 * __avirt_stream_find_by_device - Get audio stream from device number
 * @device: The PCM device number corresponding to the desired stream
 * @return: The audio stream if found, or an error pointer otherwise
 */
struct avirt_stream *__avirt_stream_find_by_device(unsigned int device);

/**
 * __avirt_stream_create - Create audio stream, including it's ALSA PCM device
 * @name: The name designated to the audio stream
 * @direction: The PCM direction (SNDRV_PCM_STREAM_PLAYBACK or
 *             SNDRV_PCM_STREAM_CAPTURE)
 * @return: The newly created audio stream if successful, or an error pointer
 */
struct avirt_stream *__avirt_stream_create(const char *name, int direction);

#endif // __AVIRT_CORE_INTERNAL_H__
