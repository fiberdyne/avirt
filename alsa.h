// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * alsa.h - Top level ALSA interface
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#ifndef __AVIRT_ALSA_H__
#define __AVIRT_ALSA_H__

#include <linux/platform_device.h>

#define MAX_NAME_LEN 32

#define PRINT_ERR(errno, errmsg, ...) \
	pr_err("[%s()] %s [ERRNO:%d]", __func__, errmsg, ##__VA_ARGS__, errno);

#define CHK_ERR(errno)                \
	do {                          \
		if (errno < 0)        \
			return errno; \
	} while (0)

#define CHK_ERR_V(errno, errmsg, ...)                           \
	do {                                                    \
		if (errno < 0) {                                \
			PRINT_ERR(errno, errmsg, ##__VA_ARGS__) \
			return errno;                           \
		}                                               \
	} while (0)

#define CHK_NULL(x)                     \
	do {                            \
		if (!x)                 \
			return -EFAULT; \
	} while (0)

#define CHK_NULL_V(x, errmsg, ...)                               \
	do {                                                     \
		if (!x) {                                        \
			PRINT_ERR(EFAULT, errmsg, ##__VA_ARGS__) \
			return -EFAULT;                          \
		}                                                \
	} while (0)

/*
 * Substream device configuration
 */
struct avirt_alsa_dev_config {
	const char devicename[MAX_NAME_LEN];
	int channels;
};

/**
 * Stream maintainance
 */
struct avirt_alsa_stream {
	int hw_frame_idx;
	struct snd_pcm_substream *substream;
};

/**
 * Collection of devices
 */
struct avirt_alsa_dev_group {
	struct avirt_alsa_dev_config *config;
	struct avirt_alsa_stream *streams;
	int devices;
	int buffersize;
};

/**
 *  ALSA driver data
 */
struct avirt_alsa_driver {
	struct snd_card *card;
	struct avirt_alsa_dev_group playback;
	struct avirt_alsa_dev_group capture;
};

/**
 * avirt_alsa_init - Initializes the ALSA driver
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_init(void);

/**
 * avirt_alsa_configure_pcm- Configure the PCM device
 * @config: Device configuration structure array
 * @direction: Direction of PCM (SNDRV_PCM_STREAM_XXX)
 * @numdevices: Number of devices (array length)
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_configure_pcm(struct avirt_alsa_dev_config *config,
			     int direction, unsigned numdevices);

/**
 * avirt_alsa_register - Registers the ALSA driver
 * @devptr: Platform driver device
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_register(struct platform_device *devptr);

/**
 * avirt_alsa_deregister - Deregisters the ALSA driver
 * @devptr: Platform driver device
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_deregister(void);

/**
 * avirt_alsa_get_dev_group - Gets the device group for the specified direction
 * @direction: SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE
 * @return: Either the playback or capture device group on success,
 * or NULL otherwise
 */
struct avirt_alsa_dev_group *avirt_alsa_get_dev_group(int direction);

/**
 * pcm_buff_complete_cb - PCM buffer complete callback
 * @substream: pointer to ALSA PCM substream
 * @return 0 on success or error code otherwise
 * 
 * This should be called from a child Audio Path once it has finished processing
 * the PCM buffer
 */
int pcm_buff_complete_cb(struct snd_pcm_substream *substream);

#endif // __AVIRT_ALSA_H__
