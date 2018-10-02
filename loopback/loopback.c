// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * loopback_audiopath.c - AVIRT sample Audio Path definition
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <avirt/core.h>

MODULE_AUTHOR("JOSHANNE <james.oshannessy@fiberdyne.com.au>");
MODULE_AUTHOR("MFARRUGI <mark.farrugia@fiberdyne.com.au>");
MODULE_DESCRIPTION("Sample Audio Path Module Interface");
MODULE_LICENSE("GPL v2");

#define AP_LOGNAME "LOOPAP"

#define AP_INFOK(fmt, args...) DINFO(AP_LOGNAME, fmt, ##args)
#define AP_PRINTK(fmt, args...) DPRINT(AP_LOGNAME, fmt, ##args)
#define AP_ERRORK(fmt, args...) DERROR(AP_LOGNAME, fmt, ##args)

static struct avirt_coreinfo *coreinfo;

struct loopback_pcm {
	struct snd_pcm_substream *substream;
	spinlock_t lock;
	struct timer_list timer;
	unsigned long base_time;
	unsigned int frac_pos; /* fractional sample position (based HZ) */
	unsigned int frac_period_rest;
	unsigned int frac_buffer_size; /* buffer_size * HZ */
	unsigned int frac_period_size; /* period_size * HZ */
	unsigned int rate;
	int elapsed;
};

int systimer_create(struct snd_pcm_substream *substream);
void systimer_free(struct snd_pcm_substream *substream);
int systimer_start(struct snd_pcm_substream *substream);
int systimer_stop(struct snd_pcm_substream *substream);
snd_pcm_uframes_t systimer_pointer(struct snd_pcm_substream *substream);
int systimer_prepare(struct snd_pcm_substream *substream);
void systimer_rearm(struct loopback_pcm *dpcm);
void systimer_update(struct loopback_pcm *dpcm);

void loopback_callback(struct timer_list *tlist);

/********************************
 * Loopback Timer Functions
 ********************************/

int systimer_create(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	struct loopback_pcm *dpcm;
	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (!dpcm)
		return -ENOMEM;

	timer_setup(&dpcm->timer, loopback_callback, 0);
	spin_lock_init(&dpcm->lock);
	dpcm->substream = substream;

	runtime->private_data = dpcm;
	return 0;
}

void systimer_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	kfree(runtime->private_data);
}

int systimer_start(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	spin_lock(&dpcm->lock);
	dpcm->base_time = jiffies;
	systimer_rearm(dpcm);
	spin_unlock(&dpcm->lock);
	return 0;
}

int systimer_stop(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	spin_lock(&dpcm->lock);
	del_timer(&dpcm->timer);
	spin_unlock(&dpcm->lock);
	return 0;
}

snd_pcm_uframes_t systimer_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;

	snd_pcm_uframes_t pos;

	spin_lock(&dpcm->lock);
	systimer_update(dpcm);
	pos = dpcm->frac_pos / HZ;
	spin_unlock(&dpcm->lock);
	return pos;
}

int systimer_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;

	dpcm->frac_pos = 0;
	dpcm->rate = runtime->rate;
	dpcm->frac_buffer_size = runtime->buffer_size * HZ;
	dpcm->frac_period_size = runtime->period_size * HZ;
	dpcm->frac_period_rest = dpcm->frac_period_size;
	dpcm->elapsed = 0;

	return 0;
}

void systimer_rearm(struct loopback_pcm *dpcm)
{
	mod_timer(&dpcm->timer, jiffies + (dpcm->frac_period_rest + dpcm->rate -
					   1) / dpcm->rate);
}

void systimer_update(struct loopback_pcm *dpcm)
{
	unsigned long delta;

	delta = jiffies - dpcm->base_time;
	if (!delta)
		return;
	dpcm->base_time += delta;
	delta *= dpcm->rate;
	dpcm->frac_pos += delta;
	while (dpcm->frac_pos >= dpcm->frac_buffer_size)
		dpcm->frac_pos -= dpcm->frac_buffer_size;
	while (dpcm->frac_period_rest <= delta) {
		dpcm->elapsed++;
		dpcm->frac_period_rest += dpcm->frac_period_size;
	}
	dpcm->frac_period_rest -= delta;
}

/*******
 * Loopback Timer Callback
 *******/

//
void loopback_callback(struct timer_list *tlist)
{
	struct loopback_pcm *dpcm = from_timer(dpcm, tlist, timer);

	unsigned long flags;
	int elapsed = 0;

	AP_INFOK("");
	spin_lock_irqsave(&dpcm->lock, flags);

	// Perform copy from playback to capture
	systimer_update(dpcm);
	systimer_rearm(dpcm);
	elapsed = dpcm->elapsed;
	dpcm->elapsed = 0;

	spin_unlock_irqrestore(&dpcm->lock, flags);
	if (elapsed)
		avirt_pcm_period_elapsed(dpcm->substream);
}

int loopback_configure(struct config_group *avirt_stream_group,
		       unsigned int stream_count)
{
	// Do something with streams

	struct list_head *entry;
	list_for_each (entry, &avirt_stream_group->cg_children) {
		struct config_item *item =
			container_of(entry, struct config_item, ci_entry);
		struct avirt_stream *stream =
			avirt_stream_from_config_item(item);
		pr_info("%s: stream name:%s device:%d channels:%d\n", __func__,
			stream->name, stream->device, stream->channels);
	}

	return 0;
}

/*******************************************************************************
 * Audio Path ALSA PCM Callbacks
 ******************************************************************************/
static int loopback_pcm_open(struct snd_pcm_substream *substream)
{
	int err;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;

	err = systimer_create(substream);
	if (err < 0)
		return err;

	return 0;
}

static int loopback_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;

	AP_INFOK("Close");

	systimer_free(substream);
	return 0;
}

static snd_pcm_uframes_t
	loopback_pcm_pointer(struct snd_pcm_substream *substream)
{
	AP_INFOK("Pointer");

	return systimer_pointer(substream);
}

static int loopback_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	AP_INFOK("Trigger");

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return systimer_start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return systimer_stop(substream);
	}
	return -EINVAL;
}

static int loopback_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;

	AP_INFOK("%s\n%s\n%d\n%d", substream->pcm->name,
		 substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "PLAYBACK" :
								  "CAPTURE",
		 runtime->rate, runtime->channels);

	return systimer_prepare(substream);
}

static struct snd_pcm_ops loopbackap_pcm_ops = {
	.open = loopback_pcm_open,
	.close = loopback_pcm_close,
	.prepare = loopback_pcm_prepare,
	.pointer = loopback_pcm_pointer,
	.trigger = loopback_pcm_trigger,
};

/*******************************************************************************
 * Loopback Audio Path AVIRT registration
 ******************************************************************************/
static struct snd_pcm_hardware loopbackap_hw = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.info = (SNDRV_PCM_INFO_INTERLEAVED // Channel interleaved audio
		 | SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID),
	/*
	.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
		 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000,
	.rate_min = 8000,
	.rate_max = 48000,
	*/
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 8,
	//.buffer_bytes_max = 32768,
	.periods_min = 1,
	.periods_max = 8,
};

static struct avirt_audiopath loopbackap_module = {
	.uid = "ap_loopback",
	.name = "Loopback Audio Path",
	.version = { 0, 0, 1 },
	.hw = &loopbackap_hw,
	.pcm_ops = &loopbackap_pcm_ops,
	.blocksize = 512,
	.configure = loopback_configure,
};

static int __init loopback_init(void)
{
	int err = 0;

	AP_INFOK("init()");

	err = avirt_audiopath_register(&loopbackap_module, &coreinfo);
	if ((err < 0) || (!coreinfo)) {
		AP_ERRORK("%s: coreinfo is NULL!\n", __func__);
		return err;
	}

	return err;
}

static void __exit loopback_exit(void)
{
	AP_INFOK("loopback: exit()");

	avirt_audiopath_deregister(&loopbackap_module);
}

module_init(loopback_init);
module_exit(loopback_exit);
