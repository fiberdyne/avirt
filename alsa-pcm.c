// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * alsa-pcm.c - ALSA PCM implementation
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#include "core.h"
#include "alsa.h"

#define DO_AUDIOPATH_CB(callback, substream, ...)                    \
	do {                                                         \
		struct avirt_audiopath *ap;                          \
		ap = avirt_get_current_audiopath();                  \
		CHK_NULL_V(ap, "Cannot find Audio Path!");           \
		if (ap->pcm_ops->callback) {                         \
			return ap->pcm_ops->callback(substream,      \
						     ##__VA_ARGS__); \
		}                                                    \
	} while (0)

/**
 * configure_pcm - set up substream properties from user configuration
 * @substream: pointer to ALSA PCM substream
 * @return 0 on success or error code otherwise
 */
static int configure_pcm(struct snd_pcm_substream *substream)
{
	struct avirt_alsa_dev_config *config;
	struct avirt_audiopath *audiopath;
	struct avirt_alsa_dev_group *group;
	struct snd_pcm_hardware *hw;
	unsigned bytes_per_sample = 0, blocksize = 0;

	audiopath = avirt_get_current_audiopath();
	CHK_NULL_V(audiopath, "Cannot find Audio Path!");

	blocksize = audiopath->blocksize;

	// Copy the hw params from the audiopath to the pcm
	hw = &substream->runtime->hw;
	memcpy(hw, audiopath->hw, sizeof(struct snd_pcm_hardware));
	pr_info("%s %d %d", __func__, blocksize, hw->periods_max);

	if (hw->formats == SNDRV_PCM_FMTBIT_S16_LE)
		bytes_per_sample = 2;
	else {
		pr_err("[%s] PCM only supports SNDRV_PCM_FMTBIT_S16_LE",
		       __func__);
		return -EINVAL;
	}

	// Get device group (playback/capture)
	group = avirt_alsa_get_dev_group(substream->stream);
	CHK_NULL(group);

	// Check if substream id is valid
	if (substream->pcm->device >= group->devices)
		return -1;

	// Setup remaining hw properties
	config = &group->config[substream->pcm->device];
	hw->channels_min = config->channels;
	hw->channels_max = config->channels;
	hw->buffer_bytes_max = blocksize * hw->periods_max * bytes_per_sample *
			       config->channels;
	hw->period_bytes_min = blocksize * bytes_per_sample * config->channels;
	hw->period_bytes_max = blocksize * bytes_per_sample * config->channels;

	return 0;
}

/*******************************************************************************
 * ALSA PCM Callbacks
 ******************************************************************************/
/**
 * pcm_open - Implements 'open' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 *
 * This is called when an ALSA PCM substream is opened. The substream device is
 * configured here.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_open(struct snd_pcm_substream *substream)
{
	// Setup the pcm device based on the configuration assigned
	CHK_ERR_V(configure_pcm(substream), "Failed to setup pcm device");

	// Do additional Audio Path 'open' callback
	DO_AUDIOPATH_CB(open, substream);

	return 0;
}

/**
 * pcm_close - Implements 'close' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 *
 * This is called when a PCM substream is closed.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_close(struct snd_pcm_substream *substream)
{
	// Do additional Audio Path 'close' callback
	DO_AUDIOPATH_CB(close, substream);

	return 0;
}

/**
 * pcm_hw_params - Implements 'hw_params' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * @hw_params: contains the hardware parameters for the PCM
 *
 * This is called when the hardware parameters are set, including buffer size,
 * the period size, the format, etc. The buffer allocation should be done here.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	int channels, err;
	size_t bufsz;
	struct avirt_audiopath *audiopath;
	struct avirt_alsa_dev_group *group;

	group = avirt_alsa_get_dev_group(substream->stream);
	CHK_NULL(group);

	channels = group->config[substream->pcm->device].channels;

	if ((params_channels(hw_params) > channels) ||
	    (params_channels(hw_params) < channels)) {
		pr_err("Requested number of channels not supported.\n");
		return -EINVAL;
	}

	audiopath = avirt_get_current_audiopath();
	CHK_NULL_V(audiopath, "Cannot find Audio Path!");

	bufsz = params_buffer_bytes(hw_params) * audiopath->hw->periods_max;

	err = snd_pcm_lib_alloc_vmalloc_buffer(substream, bufsz);
	if (err <= 0) {
		pr_err("pcm: buffer allocation failed (%d)\n", err);
		return err;
	}

	// Do additional Audio Path 'hw_params' callback
	// DO_AUDIOPATH_CB(hw_params, substream, hw_params);

	return 0;
}

/**
 * pcm_hw_free - Implements 'hw_free' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 *
 * Release the resources allocated via 'hw_params'.
 * This function is always called before the 'close' callback .
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_hw_free(struct snd_pcm_substream *substream)
{
	CHK_ERR(snd_pcm_lib_free_vmalloc_buffer(substream));

	// Do additional Audio Path 'hw_free' callback
	// DO_AUDIOPATH_CB(hw_free, substream);

	return 0;
}

/**
 * pcm_prepare - Implements 'prepare' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * 
 * The format rate, sample rate, etc., can be set here. This callback can be
 * called many times at each setup. This function is also used to handle overrun
 * and underrun conditions when we try and resync the DMA (if we're using DMA).
 * 
 * Returns 0 on success or error code otherwise.
 */
static int pcm_prepare(struct snd_pcm_substream *substream)
{
	// Do additional Audio Path 'prepare' callback
	DO_AUDIOPATH_CB(prepare, substream);

	return 0;
}

/**
 * pcm_trigger - Implements 'trigger' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * @cmd: action to be performed (start or stop)
 * 
 * This is called when the PCM is started, stopped or paused. The action
 * indicated action is specified in the second argument, SNDRV_PCM_TRIGGER_XXX
 * 
 * Returns 0 on success or error code otherwise.
 */
static int pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		break;
	default:
		pr_err("trigger must be START or STOP");
		return -EINVAL;
	}

	// Do additional Audio Path 'trigger' callback
	DO_AUDIOPATH_CB(trigger, substream, cmd);

	return 0;
}

/**
 * pcm_pointer - Implements 'pointer' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * 
 * This gets called when the user space needs a DMA buffer index. IO errors will
 * be generated if the index does not increment, or drives beyond the frame
 * threshold of the buffer itself.
 * 
 * Returns the current hardware buffer frame index.
 */
static snd_pcm_uframes_t pcm_pointer(struct snd_pcm_substream *substream)
{
	// Do additional Audio Path 'pointer' callback
	DO_AUDIOPATH_CB(pointer, substream);

	return 0;
}

/**
 * pcm_pointer - Implements 'get_time_info' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * @system_ts
 * @audio_ts
 * @audio_tstamp_config
 * @audio_tstamp_report
 * 
 * Generic way to get system timestamp and audio timestamp info
 * 
 * Returns 0 on success or error code otherwise
 */
static int pcm_get_time_info(
	struct snd_pcm_substream *substream, struct timespec *system_ts,
	struct timespec *audio_ts,
	struct snd_pcm_audio_tstamp_config *audio_tstamp_config,
	struct snd_pcm_audio_tstamp_report *audio_tstamp_report)
{
	struct avirt_alsa_dev_group *group;

	group = avirt_alsa_get_dev_group(substream->stream);
	CHK_NULL(group);

	DO_AUDIOPATH_CB(get_time_info, substream, system_ts, audio_ts,
			audio_tstamp_config, audio_tstamp_report);

	return 0;
}

/**
 * pcm_copy_user - Implements 'copy_user' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * @channel:
 * @pos: The offset in the DMA
 * @src: Audio PCM data from the user space
 * @count:
 * 
 * This is where we need to copy user audio PCM data into the sound driver
 * 
 * Returns 0 on success or error code otherwise.
 *
 */
static int pcm_copy_user(struct snd_pcm_substream *substream, int channel,
			 snd_pcm_uframes_t pos, void __user *src,
			 snd_pcm_uframes_t count)
{
	//struct snd_pcm_runtime *runtime;
	//int offset;

	//runtime = substream->runtime;
	//offset = frames_to_bytes(runtime, pos);

	// Do additional Audio Path 'copy_user' callback
	DO_AUDIOPATH_CB(copy_user, substream, channel, pos, src, count);

	return 0;
}

/**
 * pcm_copy_kernel - Implements 'copy_kernel' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * @channel:
 * @pos: The offset in the DMA
 * @src: Audio PCM data from the user space
 * @count:
 * 
 * This is where we need to copy kernel audio PCM data into the sound driver
 * 
 * Returns 0 on success or error code otherwise.
 *
 */
static int pcm_copy_kernel(struct snd_pcm_substream *substream, int channel,
			   unsigned long pos, void *buf, unsigned long count)
{
	DO_AUDIOPATH_CB(copy_kernel, substream, channel, pos, buf, count);
	return 0;
}

/**
 * pcm_ack - Implements 'ack' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 * 
 * This is where we need to ack
 * 
 * Returns 0 on success or error code otherwise.
 *
 */
int pcm_ack(struct snd_pcm_substream *substream)
{
	DO_AUDIOPATH_CB(ack, substream);
	return 0;
}

static int pcm_silence(struct snd_pcm_substream *substream, int channel,
		       snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
	DO_AUDIOPATH_CB(fill_silence, substream, channel, pos, count);
	return 0;
}

struct snd_pcm_ops pcm_ops = {
	.open = pcm_open,
	.close = pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = pcm_hw_params,
	.hw_free = pcm_hw_free,
	.prepare = pcm_prepare,
	.trigger = pcm_trigger,
	.pointer = pcm_pointer,
	.get_time_info = pcm_get_time_info,
	.fill_silence = pcm_silence,
	.copy_user = pcm_copy_user,
	.copy_kernel = pcm_copy_kernel,
	.page = snd_pcm_lib_get_vmalloc_page,
	.ack = pcm_ack,

};
