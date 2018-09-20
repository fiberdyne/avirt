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

#include "core.h"

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>

#define PRINT_ERR(errno, errmsg) \
	pr_err("[%s]:[ERRNO:%d]: %s ", __func__, errno, (errmsg));

#define CHK_ERR(errno)                  \
	do {                            \
		if ((errno) < 0)        \
			return (errno); \
	} while (0)

#define CHK_ERR_V(errno, errmsg, ...)                               \
	do {                                                        \
		if ((errno) < 0) {                                  \
			PRINT_ERR((errno), (errmsg), ##__VA_ARGS__) \
			return (errno);                             \
		}                                                   \
	} while (0)

#define CHK_NULL(x)                     \
	do {                            \
		if (!(x))               \
			return -EFAULT; \
	} while (0)

#define CHK_NULL_V(x, errmsg, ...)                                            \
	do {                                                                  \
		if (!(x)) {                                                   \
			char *errmsg_done =                                   \
				kasprintf(GFP_KERNEL, errmsg, ##__VA_ARGS__); \
			PRINT_ERR(EFAULT, errmsg_done);                       \
			kfree(errmsg_done);                                   \
			return -EFAULT;                                       \
		}                                                             \
	} while (0)

extern struct avirt_coreinfo coreinfo;
extern struct snd_pcm_ops pcm_ops;

/**
 * avirt_alsa_configure_pcm- Configure the PCM device
 * @config: Device configuration structure array
 * @direction: Direction of PCM (SNDRV_PCM_STREAM_XXX)
 * @numdevices: Number of devices (array length)
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_configure_pcm(struct avirt_alsa_devconfig *config, int direction,
			     unsigned int numdevices);

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
 * avirt_alsa_get_group - Gets the device group for the specified direction
 * @direction: SNDRV_PCM_STREAM_PLAYBACK or SNDRV_PCM_STREAM_CAPTURE
 * @return: Either the playback or capture device group on success,
 * or NULL otherwise
 */
struct avirt_alsa_group *avirt_alsa_get_group(int direction);

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
