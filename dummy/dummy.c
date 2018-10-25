// SPDX-License-Identifier: GPL-2.0
/* 
 * Dummy Audio Path for AVIRT
 * 
 * Original systimer code:
 * Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 * Adapt to use AVIRT
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 *  
 * dummy.c - Dummy Audio Path driver implementation for AVIRT
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <avirt/core.h>

MODULE_AUTHOR("James O'Shannessy <james.oshannessy@fiberdyne.com.au>");
MODULE_AUTHOR("Mark Farrugia <mark.farrugia@fiberdyne.com.au>");
MODULE_DESCRIPTION("Dummy Audio Path for AVIRT");
MODULE_LICENSE("GPL v2");

#define AP_UID "ap_dummy"

#define AP_INFOK(fmt, args...) DINFO(AP_UID, fmt, ##args)
#define AP_PRINTK(fmt, args...) DDEBUG(AP_UID, fmt, ##args)
#define AP_ERRORK(fmt, args...) DERROR(AP_UID, fmt, ##args)

#define DUMMY_SAMPLE_RATE 48000
#define DUMMY_BLOCKSIZE 512
#define DUMMY_PERIODS_MIN 1
#define DUMMY_PERIODS_MAX 8

#define get_dummy_ops(substream) \
	(*(const struct dummy_timer_ops **)(substream)->runtime->private_data)

static struct avirt_coreinfo *coreinfo;

/*******************************************************************************
 * System Timer Interface
 *
 * This is used to call the ALSA PCM callbacks required to notify AVIRT of the 
 * 'periods elapsed', so that buffers can keep getting transmitted here
 * 
 * (Borrowed from the default ALSA 'dummy' driver (sound/driver/dummy.c))
 ******************************************************************************/
struct dummy_timer_ops {
	int (*create)(struct snd_pcm_substream *);
	void (*free)(struct snd_pcm_substream *);
	int (*prepare)(struct snd_pcm_substream *);
	int (*start)(struct snd_pcm_substream *);
	int (*stop)(struct snd_pcm_substream *);
	snd_pcm_uframes_t (*pointer)(struct snd_pcm_substream *);
};

struct dummy_systimer_pcm {
	/* ops must be the first item */
	const struct dummy_timer_ops *timer_ops;
	spinlock_t lock;
	struct timer_list timer;
	unsigned long base_time;
	unsigned int frac_pos; /* fractional sample position (based HZ) */
	unsigned int frac_period_rest;
	unsigned int frac_buffer_size; /* buffer_size * HZ */
	unsigned int frac_period_size; /* period_size * HZ */
	unsigned int rate;
	int elapsed;
	struct snd_pcm_substream *substream;
};

static void dummy_systimer_rearm(struct dummy_systimer_pcm *dpcm)
{
	mod_timer(&dpcm->timer, jiffies + (dpcm->frac_period_rest + dpcm->rate -
					   1) / dpcm->rate);
}

static void dummy_systimer_update(struct dummy_systimer_pcm *dpcm)
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

static int dummy_systimer_start(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm = substream->runtime->private_data;
	spin_lock(&dpcm->lock);
	dpcm->base_time = jiffies;
	dummy_systimer_rearm(dpcm);
	spin_unlock(&dpcm->lock);
	return 0;
}

static int dummy_systimer_stop(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm = substream->runtime->private_data;
	spin_lock(&dpcm->lock);
	del_timer(&dpcm->timer);
	spin_unlock(&dpcm->lock);
	return 0;
}

static int dummy_systimer_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dummy_systimer_pcm *dpcm = runtime->private_data;

	dpcm->frac_pos = 0;
	dpcm->rate = runtime->rate;
	dpcm->frac_buffer_size = runtime->buffer_size * HZ;
	dpcm->frac_period_size = runtime->period_size * HZ;
	dpcm->frac_period_rest = dpcm->frac_period_size;
	dpcm->elapsed = 0;

	return 0;
}

static void dummy_systimer_callback(struct timer_list *t)
{
	struct dummy_systimer_pcm *dpcm = from_timer(dpcm, t, timer);
	unsigned long flags;
	int elapsed = 0;

	spin_lock_irqsave(&dpcm->lock, flags);
	dummy_systimer_update(dpcm);
	dummy_systimer_rearm(dpcm);
	elapsed = dpcm->elapsed;
	dpcm->elapsed = 0;
	spin_unlock_irqrestore(&dpcm->lock, flags);
	if (elapsed)
		avirt_pcm_period_elapsed(dpcm->substream);
}

static snd_pcm_uframes_t
	dummy_systimer_pointer(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm = substream->runtime->private_data;
	snd_pcm_uframes_t pos;

	spin_lock(&dpcm->lock);
	dummy_systimer_update(dpcm);
	pos = dpcm->frac_pos / HZ;
	spin_unlock(&dpcm->lock);
	return pos;
}

static int dummy_systimer_create(struct snd_pcm_substream *substream)
{
	struct dummy_systimer_pcm *dpcm;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (!dpcm)
		return -ENOMEM;
	substream->runtime->private_data = dpcm;
	timer_setup(&dpcm->timer, dummy_systimer_callback, 0);
	spin_lock_init(&dpcm->lock);
	dpcm->substream = substream;
	return 0;
}

static void dummy_systimer_free(struct snd_pcm_substream *substream)
{
	kfree(substream->runtime->private_data);
}

static const struct dummy_timer_ops dummy_systimer_ops = {
	.create = dummy_systimer_create,
	.free = dummy_systimer_free,
	.prepare = dummy_systimer_prepare,
	.start = dummy_systimer_start,
	.stop = dummy_systimer_stop,
	.pointer = dummy_systimer_pointer,
};

/*******************************************************************************
 * Audio Path ALSA PCM Callbacks
 ******************************************************************************/
static int dummy_pcm_open(struct snd_pcm_substream *substream)
{
	const struct dummy_timer_ops *ops;
	int err;

	ops = &dummy_systimer_ops;
	err = ops->create(substream);
	if (err < 0)
		return err;
	get_dummy_ops(substream) = ops;

	return 0;
}

static int dummy_pcm_close(struct snd_pcm_substream *substream)
{
	get_dummy_ops(substream)->free(substream);
	return 0;
}

static snd_pcm_uframes_t dummy_pcm_pointer(struct snd_pcm_substream *substream)
{
	return get_dummy_ops(substream)->pointer(substream);
}

static int dummy_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return get_dummy_ops(substream)->start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return get_dummy_ops(substream)->stop(substream);
	}
	return -EINVAL;
}

static int dummy_pcm_prepare(struct snd_pcm_substream *substream)
{
	return get_dummy_ops(substream)->prepare(substream);
}

static struct snd_pcm_ops dummyap_pcm_ops = {
	.open = dummy_pcm_open,
	.close = dummy_pcm_close,
	.prepare = dummy_pcm_prepare,
	.pointer = dummy_pcm_pointer,
	.trigger = dummy_pcm_trigger,
};

/*******************************************************************************
 * Dummy Audio Path AVIRT registration
 ******************************************************************************/
int dummy_configure(struct snd_card *card,
		    struct config_group *avirt_stream_group,
		    unsigned int stream_count)
{
	// Do something with streams

	struct list_head *entry;
	list_for_each (entry, &avirt_stream_group->cg_children) {
		struct config_item *item =
			container_of(entry, struct config_item, ci_entry);
		struct avirt_stream *stream =
			avirt_stream_from_config_item(item);
		AP_INFOK("stream name:%s device:%d channels:%d", stream->name,
			 stream->device, stream->channels);
	}

	return 0;
}

static struct snd_pcm_hardware dummyap_hw = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.info = (SNDRV_PCM_INFO_INTERLEAVED // Channel interleaved audio
		 | SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = DUMMY_SAMPLE_RATE,
	.rate_max = DUMMY_SAMPLE_RATE,
	.periods_min = DUMMY_PERIODS_MIN,
	.periods_max = DUMMY_PERIODS_MAX,
};

static struct avirt_audiopath dummyap_module = {
	.uid = "ap_dummy",
	.name = "Dummy Audio Path",
	.version = { 0, 0, 1 },
	.hw = &dummyap_hw,
	.pcm_ops = &dummyap_pcm_ops,
	.configure = dummy_configure,
};

static int __init dummy_init(void)
{
	int err = 0;

	pr_info("init()\n");

	err = avirt_audiopath_register(&dummyap_module, &coreinfo);
	if ((err < 0) || (!coreinfo)) {
		pr_err("%s: coreinfo is NULL!\n", __func__);
		return err;
	}

	return err;
}

static void __exit dummy_exit(void)
{
	pr_info("exit()\n");

	avirt_audiopath_deregister(&dummyap_module);
}

module_init(dummy_init);
module_exit(dummy_exit);
