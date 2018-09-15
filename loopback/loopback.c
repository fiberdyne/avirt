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

static struct avirt_coreinfo *coreinfo;

/*******************************************************************************
 * Audio Path ALSA PCM Callbacks
 ******************************************************************************/
static int loopback_pcm_open(struct snd_pcm_substream *substream)
{
	return 0;
}

static int loopback_pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static snd_pcm_uframes_t
	loopback_pcm_pointer(struct snd_pcm_substream *substream)
{
	return 0;
}

static int loopback_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return 0;
	}
	return -EINVAL;
}

static int loopback_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
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
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.periods_min = 1,
	.periods_max = 8,
};

static struct avirt_audiopath loopbackap_module = {
	.name = "Loopback Audio Path",
	.version = { 0, 0, 1 },
	.value = 10,
	.hw = &loopbackap_hw,
	.pcm_ops = &loopbackap_pcm_ops,
	.blocksize = 512,
};

static int __init loopback_init(void)
{
	int err = 0;

	pr_info("init()\n");

	err = avirt_register_audiopath(&loopbackap_module, &coreinfo);
	if ((err < 0) || (!coreinfo)) {
		pr_err("%s: coreinfo is NULL!\n", __func__);
		return err;
	}

	return err;
}

static void __exit loopback_exit(void)
{
	pr_info("exit()\n");

	avirt_deregister_audiopath(&loopbackap_module);
}

module_init(loopback_init);
module_exit(loopback_exit);
