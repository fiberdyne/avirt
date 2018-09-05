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
	const char *name;
	unsigned version[3];
	struct snd_pcm_hardware *hw;
	struct snd_pcm_ops *pcm_ops;
	unsigned blocksize;

	void *context;
};

/*
 * ALSA Substream device configuration
 */
struct avirt_alsa_devconfig {
	const char devicename[MAX_NAME_LEN];
	int channels;
};

/**
 * Collection of ALSA devices
 */
struct avirt_alsa_group {
	struct avirt_alsa_devconfig *config;
	int devices;
};

/**
 * AVIRT core info
 */
struct avirt_coreinfo {
	unsigned version[3];

	struct avirt_alsa_group playback;
	struct avirt_alsa_group capture;

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
 * avirt_get_current_audiopath - retrieves the current Audio Path
 * @return: Current Audio Path
 */
struct avirt_audiopath *avirt_get_current_audiopath(void);

#endif // __AVIRT_CORE_H__
