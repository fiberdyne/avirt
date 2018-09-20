// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * core.h - System-level header for virtual ALSA card
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#ifndef __AVIRT_CORE_H__
#define __AVIRT_CORE_H__

#include <sound/pcm.h>
#include <linux/configfs.h>

#define MAX_NAME_LEN 32

/**
 * PCM buffer complete callback
 *
 * These are called from the audiopath when a PCM buffer has completed, and
 * new data can be submitted/retrieved
 */
typedef int (*avirt_buff_complete)(struct snd_pcm_substream *substream);

/**
 * AVIRT Audio Path info
 */
struct avirt_audiopath {
	const char *uid; /* Unique identifier */
	const char *name; /* Pretty name */
	unsigned int version[3]; /* Version - Major.Minor.Ext */
	struct snd_pcm_hardware *hw; /* ALSA PCM HW conf */
	struct snd_pcm_ops *pcm_ops; /* ALSA PCM op table */
	unsigned int blocksize; /* Audio frame size accepted */

	void *context;
};

/*
 * Audio stream configuration
 */
struct avirt_stream {
	struct snd_pcm *pcm; /* Stream PCM device */
	unsigned int channels; /* Stream channel count */
	struct config_item item; /* configfs item reference */
};

/**
 * Collection of audio streams
 */
struct avirt_stream_group {
	struct avirt_stream *streams; /* AVIRT stream array */
	unsigned int devices; /* Number of stream devices */
};

/**
 * AVIRT core info
 */
struct avirt_coreinfo {
	unsigned int version[3];

	struct avirt_stream_group playback;
	struct avirt_stream_group capture;

	avirt_buff_complete pcm_buff_complete;
};

/**
 * avirt_register_audiopath - register Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be registered
 * @core: ALSA virtual driver core info
 * @return: 0 on success or error code otherwise
 */
int avirt_register_audiopath(struct avirt_audiopath *audiopath,
			     struct avirt_coreinfo **coreinfo);

/**
 * avirt_deregister_audiopath - deregister Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be deregistered
 * @return: 0 on success or error code otherwise
 */
int avirt_deregister_audiopath(struct avirt_audiopath *audiopath);

/**
 * avirt_get_audiopath - retrieves the Audio Path by it's UID
 * @uid: Unique ID for the Audio Path
 * @return: Corresponding Audio Path
 */
struct avirt_audiopath *avirt_get_audiopath(const char *uid);

/**
 * avirt_subscribe_stream - subscribe the Audio Path to the given streams
 * @audiopath: Audio Path to subscribe for
 * @streams: The streams to subscribe the Audio Path to
 * return: 0 on success or error code otherwise
 */
int avirt_subscribe_stream(struct avirt_audiopath *audiopath,
			   const char **streams);

#endif // __AVIRT_CORE_H__
