// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * alsa.c - ALSA PCM driver for virtual ALSA card
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#include <linux/slab.h>
#include <sound/core.h>
#include <sound/initval.h>

#include "alsa.h"

extern struct avirt_coreinfo coreinfo;

static struct snd_card *card = NULL;
extern struct snd_pcm_ops pcm_ops;

/**
 * pcm_constructor - Constructs the ALSA PCM middle devices for this driver
 * @card: The snd_card struct to construct the devices for
 * @return 0 on success or error code otherwise
 */
static int pcm_constructor(struct snd_card *card)
{
	struct snd_pcm *pcm;
	int i;

	// Allocate Playback PCM instances
	for (i = 0; i < coreinfo.playback.devices; i++) {
		CHK_ERR(snd_pcm_new(card,
				    coreinfo.playback.config[i].devicename, i,
				    1, 0, &pcm));

		/** Register driver callbacks */
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_ops);

		pcm->info_flags = 0;
		strcpy(pcm->name, coreinfo.playback.config[i].devicename);
	}

	// Allocate Capture PCM instances
	for (i = 0; i < coreinfo.capture.devices; i++) {
		CHK_ERR(snd_pcm_new(card, coreinfo.capture.config[i].devicename,
				    i, 0, 1, &pcm));

		/** Register driver callbacks */
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_ops);

		pcm->info_flags = 0;
		strcpy(pcm->name, coreinfo.capture.config[i].devicename);
	}

	return 0;
}

/**
 * alloc_dev_config - Allocates memory for ALSA device configuration
 * @return: 0 on success or error code otherwise
 */
static int alloc_dev_config(struct avirt_alsa_devconfig **devconfig,
			    struct avirt_alsa_devconfig *userconfig,
			    unsigned numdevices)
{
	if (numdevices == 0)
		return 0;

	*devconfig = kzalloc(sizeof(struct avirt_alsa_devconfig) * numdevices,
			     GFP_KERNEL);
	if (!(*devconfig))
		return -ENOMEM;

	memcpy(*devconfig, userconfig,
	       sizeof(struct avirt_alsa_devconfig) * numdevices);

	return 0;
}

struct avirt_alsa_group *avirt_alsa_get_group(int direction)
{
	switch (direction) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		return &coreinfo.playback;
	case SNDRV_PCM_STREAM_CAPTURE:
		return &coreinfo.capture;
	default:
		pr_err("[%s] Direction must be SNDRV_PCM_STREAM_XXX!",
		       __func__);
		return NULL;
	}
}

/**
 * avirt_alsa_configure_pcm- Configure the PCM device
 * @config: Device configuration structure array
 * @direction: Direction of PCM (SNDRV_PCM_STREAM_XXX)
 * @numdevices: Number of devices (array length)
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_configure_pcm(struct avirt_alsa_devconfig *config, int direction,
			     unsigned numdevices)
{
	struct avirt_alsa_group *group;

	group = avirt_alsa_get_group(direction);
	CHK_NULL(group);

	CHK_ERR(alloc_dev_config(&group->config, config, numdevices));

	group->devices = numdevices;

	return 0;
}

/**
 * avirt_alsa_register - Registers the ALSA driver
 * @devptr: Platform driver device
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_register(struct platform_device *devptr)
{
	static struct snd_device_ops device_ops;

	// Create the card instance
	CHK_ERR_V(snd_card_new(&devptr->dev, SNDRV_DEFAULT_IDX1, "avirt",
			       THIS_MODULE, 0, &card),
		  "Failed to create sound card");

	strcpy(card->driver, "avirt-alsa-device");
	strcpy(card->shortname, "avirt");
	strcpy(card->longname, "A virtual sound card driver for ALSA");

	// Create new sound device
	CHK_ERR_V((snd_device_new(card, SNDRV_DEV_LOWLEVEL, &coreinfo,
				  &device_ops)),
		  "Failed to create sound device");

	CHK_ERR((pcm_constructor(card)));

	/** Register with the ALSA framework */
	CHK_ERR_V(snd_card_register(card), "Device registration failed");

	return 0;
}

/**
 * avirt_alsa_deregister - Deregisters the ALSA driver
 * @devptr: Platform driver device
 * @return: 0 on success or error code otherwise
 */
int avirt_alsa_deregister(void)
{
	CHK_NULL(card);
	snd_card_free(card);
	CHK_NULL(coreinfo.playback.config);
	kfree(coreinfo.playback.config);
	CHK_NULL(coreinfo.capture.config);
	kfree(coreinfo.capture.config);

	return 0;
}

/**
 * pcm_buff_complete_cb - PCM buffer complete callback
 * @substreamid: pointer to ALSA PCM substream
 * @return 0 on success or error code otherwise
 * 
 * This should be called from a child Audio Path once it has finished processing
 * the pcm buffer
 */
int pcm_buff_complete_cb(struct snd_pcm_substream *substream)
{
	// Notify ALSA middle layer of the elapsed period boundary
	snd_pcm_period_elapsed(substream);

	return 0;
}
