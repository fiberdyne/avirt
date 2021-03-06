// SPDX-License-Identifier: GPL-2.0
/*
 * AVIRT - ALSA Virtual Soundcard
 *
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 *
 * pcm.c - AVIRT PCM interface
 */

#include "core.h"

#define D_LOGNAME "pcm"

#define D_INFOK(fmt, args...) DINFO(D_LOGNAME, fmt, ##args)
#define D_PRINTK(fmt, args...) DDEBUG(D_LOGNAME, fmt, ##args)
#define D_ERRORK(fmt, args...) DERROR(D_LOGNAME, fmt, ##args)

#define DO_AUDIOPATH_CB(ap, callback, substream, ...)                  \
	(((ap)->pcm_ops->callback) ?                                   \
		 (ap)->pcm_ops->callback((substream), ##__VA_ARGS__) : \
		 0)

/**
 * snd_avirt_pcm_period_elapsed - PCM buffer complete callback
 * @substreamid: pointer to ALSA PCM substream
 *
 * This should be called from a child Audio Path once it has finished
 * processing the pcm buffer
 */
void snd_avirt_pcm_period_elapsed(struct snd_pcm_substream *substream)
{
	// Notify ALSA middle layer of the elapsed period boundary
	snd_pcm_period_elapsed(substream);
}
EXPORT_SYMBOL_GPL(snd_avirt_pcm_period_elapsed);

/*******************************************************************************
 * ALSA PCM Callbacks
 ******************************************************************************/
/**
 * pcm_open - Implements 'open' callback for PCM middle layer
 * @substream: pointer to ALSA PCM substream
 *
 * This is called when an ALSA PCM substream is opened. The substream device
 * is configured here.
 *
 * Returns 0 on success or error code otherwise.
 */
static int pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_avirt_audiopath *audiopath;
	struct snd_avirt_stream *stream;
	struct snd_pcm_hardware *hw;
	unsigned int chans = 0;

	stream = snd_avirt_stream_find_by_device(substream->pcm->device);
	audiopath = snd_avirt_audiopath_get(stream->map);
	CHK_NULL_V(audiopath, "Cannot find Audio Path uid: '%s'!", stream->map);
	substream->private_data = audiopath;

	// Copy the hw params from the audiopath to the pcm
	hw = &substream->runtime->hw;
	memcpy(hw, audiopath->hw, sizeof(struct snd_pcm_hardware));

	stream = snd_avirt_stream_find_by_device(substream->pcm->device);
	if (IS_ERR_VALUE(stream) || !stream)
		return PTR_ERR(stream);

	// Setup remaining hw properties
	chans = stream->channels;
	hw->channels_min = chans;
	hw->channels_max = chans;

	// Do additional Audio Path 'open' callback
	return DO_AUDIOPATH_CB(audiopath, open, substream);
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
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data), close,
		substream);
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
	int retval;
	size_t bufsz;
	struct snd_avirt_audiopath *audiopath;
	struct snd_avirt_stream *stream;

	stream = snd_avirt_stream_find_by_device(substream->pcm->device);
	if (IS_ERR_VALUE(stream) || !stream)
		return PTR_ERR(stream);

	if ((params_channels(hw_params) > stream->channels) ||
	    (params_channels(hw_params) < stream->channels)) {
		D_ERRORK("Requested number of channels: %d not supported",
			 params_channels(hw_params));
		return -EINVAL;
	}

	audiopath = ((struct snd_avirt_audiopath *)substream->private_data);
	bufsz = params_buffer_bytes(hw_params) * audiopath->hw->periods_max;

	retval = snd_pcm_lib_alloc_vmalloc_buffer(substream, bufsz);
	if (retval < 0)
		D_ERRORK("pcm: buffer allocation failed: %d", retval);

	return retval;
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
	int err;

	// Do additional Audio Path 'hw_free' callback
	err = DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		hw_free, substream);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
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
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		prepare, substream);
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
		D_ERRORK("Invalid trigger cmd: %d", cmd);
		return -EINVAL;
	}

	// Do additional Audio Path 'trigger' callback
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		trigger, substream, cmd);
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
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		pointer, substream);
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
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		get_time_info, substream, system_ts, audio_ts,
		audio_tstamp_config, audio_tstamp_report);
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
	// struct snd_pcm_runtime *runtime;
	// int offset;

	// runtime = substream->runtime;
	// offset = frames_to_bytes(runtime, pos);

	// Do additional Audio Path 'copy_user' callback
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		copy_user, substream, channel, pos, src, count);
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
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		copy_kernel, substream, channel, pos, buf, count);
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
static int pcm_ack(struct snd_pcm_substream *substream)
{
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data), ack,
		substream);
}

static int pcm_silence(struct snd_pcm_substream *substream, int channel,
		       snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
	return DO_AUDIOPATH_CB(
		((struct snd_avirt_audiopath *)substream->private_data),
		fill_silence, substream, channel, pos, count);
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
