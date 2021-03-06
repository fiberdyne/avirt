// SPDX-License-Identifier: GPL-2.0
/* 
 * Loopback Audio Path for AVIRT
 * 
 * Original code:
 * Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 * 
 * More accurate positioning and full-duplex support:
 * Copyright (c) Ahmet İnan <ainan at mathematik.uni-freiburg.de>
 *
 * Major (almost complete) rewrite:
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 * A next major update in 2010 (separate timers for playback and capture):
 * Copyright (c) Jaroslav Kysela <perex@perex.cz>
 * 
 * Adapt to use AVIRT, looping is now conducted on the same PCM device
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 * 
 * loopback.c - Loopback Audio Path driver implementation for AVIRT
 */

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/avirt.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Loopback Audio Path for AVIRT");
MODULE_LICENSE("GPL");

#define AP_UID "ap_loopback"

#define AP_INFOK(fmt, args...) DINFO(AP_UID, fmt, ##args)
#define AP_PRINTK(fmt, args...) DDEBUG(AP_UID, fmt, ##args)
#define AP_ERRORK(fmt, args...) DERROR(AP_UID, fmt, ##args)

#define NO_PITCH 100000

static struct snd_avirt_coreinfo *coreinfo;
static struct loopback *loopback;

struct loopback_pcm;

struct loopback_cable {
	spinlock_t lock;
	struct loopback_pcm *streams[2];
	struct snd_pcm_hardware hw;
	/* flags */
	unsigned int valid;
	unsigned int running;
	unsigned int pause;
};

struct loopback_setup {
	unsigned int notify : 1;
	unsigned int rate_shift;
	unsigned int format;
	unsigned int rate;
	unsigned int channels;
	struct snd_ctl_elem_id active_id;
	struct snd_ctl_elem_id format_id;
	struct snd_ctl_elem_id rate_id;
	struct snd_ctl_elem_id channels_id;
};

struct loopback {
	struct snd_card *card;
	struct mutex cable_lock;
	struct loopback_cable *cables[MAX_STREAMS];
	struct snd_pcm *pcm[MAX_STREAMS];
	struct loopback_setup setup[MAX_STREAMS];
};

struct loopback_pcm {
	struct loopback *loopback;
	struct snd_pcm_substream *substream;
	struct loopback_cable *cable;
	unsigned int pcm_buffer_size;
	unsigned int buf_pos; /* position in buffer */
	unsigned int silent_size;
	/* PCM parameters */
	unsigned int pcm_period_size;
	unsigned int pcm_bps; /* bytes per second */
	unsigned int pcm_salign; /* bytes per sample * channels */
	unsigned int pcm_rate_shift; /* rate shift value */
	/* flags */
	unsigned int period_update_pending : 1;
	/* timer stuff */
	unsigned int irq_pos; /* fractional IRQ position */
	unsigned int period_size_frac;
	unsigned int last_drift;
	unsigned long last_jiffies;
	struct timer_list timer;
};

static inline unsigned int byte_pos(struct loopback_pcm *dpcm, unsigned int x)
{
	if (dpcm->pcm_rate_shift == NO_PITCH) {
		x /= HZ;
	} else {
		x = div_u64(NO_PITCH * (unsigned long long)x,
			    HZ * (unsigned long long)dpcm->pcm_rate_shift);
	}
	return x - (x % dpcm->pcm_salign);
}

static inline unsigned int frac_pos(struct loopback_pcm *dpcm, unsigned int x)
{
	if (dpcm->pcm_rate_shift == NO_PITCH) { /* no pitch */
		return x * HZ;
	} else {
		x = div_u64(dpcm->pcm_rate_shift * (unsigned long long)x * HZ,
			    NO_PITCH);
	}
	return x;
}

static inline struct loopback_setup *get_setup(struct loopback_pcm *dpcm)
{
	return &dpcm->loopback->setup[dpcm->substream->pcm->device];
}

static inline unsigned int get_notify(struct loopback_pcm *dpcm)
{
	return get_setup(dpcm)->notify;
}

static inline unsigned int get_rate_shift(struct loopback_pcm *dpcm)
{
	return get_setup(dpcm)->rate_shift;
}

/* call in cable->lock */
static void loopback_timer_start(struct loopback_pcm *dpcm)
{
	unsigned long tick;
	unsigned int rate_shift = get_rate_shift(dpcm);

	if (rate_shift != dpcm->pcm_rate_shift) {
		dpcm->pcm_rate_shift = rate_shift;
		dpcm->period_size_frac = frac_pos(dpcm, dpcm->pcm_period_size);
	}
	if (dpcm->period_size_frac <= dpcm->irq_pos) {
		dpcm->irq_pos %= dpcm->period_size_frac;
		dpcm->period_update_pending = 1;
	}
	tick = dpcm->period_size_frac - dpcm->irq_pos;
	tick = (tick + dpcm->pcm_bps - 1) / dpcm->pcm_bps;
	mod_timer(&dpcm->timer, jiffies + tick);
}

/* call in cable->lock */
static inline void loopback_timer_stop(struct loopback_pcm *dpcm)
{
	del_timer(&dpcm->timer);
	dpcm->timer.expires = 0;
}

static inline void loopback_timer_stop_sync(struct loopback_pcm *dpcm)
{
	del_timer_sync(&dpcm->timer);
}

#define CABLE_VALID_PLAYBACK (1 << SNDRV_PCM_STREAM_PLAYBACK)
#define CABLE_VALID_CAPTURE (1 << SNDRV_PCM_STREAM_CAPTURE)
#define CABLE_VALID_BOTH (CABLE_VALID_PLAYBACK | CABLE_VALID_CAPTURE)

static int loopback_check_format(struct loopback_cable *cable, int stream)
{
	struct snd_pcm_runtime *runtime, *cruntime;
	struct loopback_setup *setup;
	struct snd_card *card;
	int check;

	if (cable->valid != CABLE_VALID_BOTH) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			goto __notify;
		return 0;
	}
	runtime = cable->streams[SNDRV_PCM_STREAM_PLAYBACK]->substream->runtime;
	cruntime = cable->streams[SNDRV_PCM_STREAM_CAPTURE]->substream->runtime;
	check = runtime->format != cruntime->format ||
		runtime->rate != cruntime->rate ||
		runtime->channels != cruntime->channels;
	if (!check)
		return 0;
	if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		return -EIO;
	} else {
		snd_pcm_stop(
			cable->streams[SNDRV_PCM_STREAM_CAPTURE]->substream,
			SNDRV_PCM_STATE_DRAINING);
	__notify:
		runtime = cable->streams[SNDRV_PCM_STREAM_PLAYBACK]
				  ->substream->runtime;
		setup = get_setup(cable->streams[SNDRV_PCM_STREAM_PLAYBACK]);
		card = cable->streams[SNDRV_PCM_STREAM_PLAYBACK]->loopback->card;
		if (setup->format != runtime->format) {
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &setup->format_id);
			setup->format = runtime->format;
		}
		if (setup->rate != runtime->rate) {
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &setup->rate_id);
			setup->rate = runtime->rate;
		}
		if (setup->channels != runtime->channels) {
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &setup->channels_id);
			setup->channels = runtime->channels;
		}
	}
	return 0;
}

static void loopback_active_notify(struct loopback_pcm *dpcm)
{
	snd_ctl_notify(dpcm->loopback->card, SNDRV_CTL_EVENT_MASK_VALUE,
		       &get_setup(dpcm)->active_id);
}

static int loopback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;
	int err, stream = 1 << substream->stream;

	AP_INFOK();

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		err = loopback_check_format(cable, substream->stream);
		if (err < 0)
			return err;
		dpcm->last_jiffies = jiffies;
		dpcm->pcm_rate_shift = 0;
		dpcm->last_drift = 0;
		spin_lock(&cable->lock);
		cable->running |= stream;
		cable->pause &= ~stream;
		loopback_timer_start(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		spin_lock(&cable->lock);
		cable->running &= ~stream;
		cable->pause &= ~stream;
		loopback_timer_stop(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock(&cable->lock);
		cable->pause |= stream;
		loopback_timer_stop(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		spin_lock(&cable->lock);
		dpcm->last_jiffies = jiffies;
		cable->pause &= ~stream;
		loopback_timer_start(dpcm);
		spin_unlock(&cable->lock);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			loopback_active_notify(dpcm);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void params_change(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;

	cable->hw.formats = pcm_format_to_bits(runtime->format);
	cable->hw.rate_min = runtime->rate;
	cable->hw.rate_max = runtime->rate;
	cable->hw.channels_min = runtime->channels;
	cable->hw.channels_max = runtime->channels;
}

static int loopback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;
	int bps, salign;

	loopback_timer_stop_sync(dpcm);

	salign =
		(snd_pcm_format_width(runtime->format) * runtime->channels) / 8;
	bps = salign * runtime->rate;
	if (bps <= 0 || salign <= 0)
		return -EINVAL;

	dpcm->buf_pos = 0;
	dpcm->pcm_buffer_size = frames_to_bytes(runtime, runtime->buffer_size);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		/* clear capture buffer */
		dpcm->silent_size = dpcm->pcm_buffer_size;
		snd_pcm_format_set_silence(runtime->format, runtime->dma_area,
					   runtime->buffer_size *
						   runtime->channels);
	}

	dpcm->irq_pos = 0;
	dpcm->period_update_pending = 0;
	dpcm->pcm_bps = bps;
	dpcm->pcm_salign = salign;
	dpcm->pcm_period_size = frames_to_bytes(runtime, runtime->period_size);

	mutex_lock(&dpcm->loopback->cable_lock);
	if (!(cable->valid & ~(1 << substream->stream)) ||
	    (get_setup(dpcm)->notify &&
	     substream->stream == SNDRV_PCM_STREAM_PLAYBACK))
		params_change(substream);
	cable->valid |= 1 << substream->stream;
	mutex_unlock(&dpcm->loopback->cable_lock);

	return 0;
}

static void clear_capture_buf(struct loopback_pcm *dpcm, unsigned int bytes)
{
	struct snd_pcm_runtime *runtime = dpcm->substream->runtime;
	char *dst = runtime->dma_area;
	unsigned int dst_off = dpcm->buf_pos;

	if (dpcm->silent_size >= dpcm->pcm_buffer_size)
		return;
	if (dpcm->silent_size + bytes > dpcm->pcm_buffer_size)
		bytes = dpcm->pcm_buffer_size - dpcm->silent_size;

	for (;;) {
		unsigned int size = bytes;
		if (dst_off + size > dpcm->pcm_buffer_size)
			size = dpcm->pcm_buffer_size - dst_off;
		snd_pcm_format_set_silence(runtime->format, dst + dst_off,
					   bytes_to_frames(runtime, size) *
						   runtime->channels);
		dpcm->silent_size += size;
		bytes -= size;
		if (!bytes)
			break;
		dst_off = 0;
	}
}

static void copy_play_buf(struct loopback_pcm *play, struct loopback_pcm *capt,
			  unsigned int bytes)
{
	struct snd_pcm_runtime *runtime = play->substream->runtime;
	char *src = runtime->dma_area;
	char *dst = capt->substream->runtime->dma_area;
	unsigned int src_off = play->buf_pos;
	unsigned int dst_off = capt->buf_pos;
	unsigned int clear_bytes = 0;

	/* check if playback is draining, trim the capture copy size
	 * when our pointer is at the end of playback ring buffer */
	if (runtime->status->state == SNDRV_PCM_STATE_DRAINING &&
	    snd_pcm_playback_hw_avail(runtime) < runtime->buffer_size) {
		snd_pcm_uframes_t appl_ptr, appl_ptr1, diff;
		appl_ptr = appl_ptr1 = runtime->control->appl_ptr;
		appl_ptr1 -= appl_ptr1 % runtime->buffer_size;
		appl_ptr1 += play->buf_pos / play->pcm_salign;
		if (appl_ptr < appl_ptr1)
			appl_ptr1 -= runtime->buffer_size;
		diff = (appl_ptr - appl_ptr1) * play->pcm_salign;
		if (diff < bytes) {
			clear_bytes = bytes - diff;
			bytes = diff;
		}
	}

	for (;;) {
		unsigned int size = bytes;
		if (src_off + size > play->pcm_buffer_size)
			size = play->pcm_buffer_size - src_off;
		if (dst_off + size > capt->pcm_buffer_size)
			size = capt->pcm_buffer_size - dst_off;
		memcpy(dst + dst_off, src + src_off, size);
		capt->silent_size = 0;
		bytes -= size;
		if (!bytes)
			break;
		src_off = (src_off + size) % play->pcm_buffer_size;
		dst_off = (dst_off + size) % capt->pcm_buffer_size;
	}

	if (clear_bytes > 0) {
		clear_capture_buf(capt, clear_bytes);
		capt->silent_size = 0;
	}
}

static inline unsigned int bytepos_delta(struct loopback_pcm *dpcm,
					 unsigned int jiffies_delta)
{
	unsigned long last_pos;
	unsigned int delta;

	last_pos = byte_pos(dpcm, dpcm->irq_pos);
	dpcm->irq_pos += jiffies_delta * dpcm->pcm_bps;
	delta = byte_pos(dpcm, dpcm->irq_pos) - last_pos;
	if (delta >= dpcm->last_drift)
		delta -= dpcm->last_drift;
	dpcm->last_drift = 0;
	if (dpcm->irq_pos >= dpcm->period_size_frac) {
		dpcm->irq_pos %= dpcm->period_size_frac;
		dpcm->period_update_pending = 1;
	}
	return delta;
}

static inline void bytepos_finish(struct loopback_pcm *dpcm, unsigned int delta)
{
	dpcm->buf_pos += delta;
	dpcm->buf_pos %= dpcm->pcm_buffer_size;
}

/* call in cable->lock */
static unsigned int loopback_pos_update(struct loopback_cable *cable)
{
	struct loopback_pcm *dpcm_play =
		cable->streams[SNDRV_PCM_STREAM_PLAYBACK];
	struct loopback_pcm *dpcm_capt =
		cable->streams[SNDRV_PCM_STREAM_CAPTURE];
	unsigned long delta_play = 0, delta_capt = 0;
	unsigned int running, count1, count2;

	running = cable->running ^ cable->pause;
	if (running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) {
		delta_play = jiffies - dpcm_play->last_jiffies;
		dpcm_play->last_jiffies += delta_play;
	}

	if (running & (1 << SNDRV_PCM_STREAM_CAPTURE)) {
		delta_capt = jiffies - dpcm_capt->last_jiffies;
		dpcm_capt->last_jiffies += delta_capt;
	}

	if (delta_play == 0 && delta_capt == 0)
		goto unlock;

	if (delta_play > delta_capt) {
		count1 = bytepos_delta(dpcm_play, delta_play - delta_capt);
		bytepos_finish(dpcm_play, count1);
		delta_play = delta_capt;
	} else if (delta_play < delta_capt) {
		count1 = bytepos_delta(dpcm_capt, delta_capt - delta_play);
		clear_capture_buf(dpcm_capt, count1);
		bytepos_finish(dpcm_capt, count1);
		delta_capt = delta_play;
	}

	if (delta_play == 0 && delta_capt == 0)
		goto unlock;

	/* note delta_capt == delta_play at this moment */
	count1 = bytepos_delta(dpcm_play, delta_play);
	count2 = bytepos_delta(dpcm_capt, delta_capt);
	if (count1 < count2) {
		dpcm_capt->last_drift = count2 - count1;
		count1 = count2;
	} else if (count1 > count2) {
		dpcm_play->last_drift = count1 - count2;
	}
	copy_play_buf(dpcm_play, dpcm_capt, count1);
	bytepos_finish(dpcm_play, count1);
	bytepos_finish(dpcm_capt, count1);
unlock:
	return running;
}

static void loopback_timer_function(struct timer_list *t)
{
	struct loopback_pcm *dpcm = from_timer(dpcm, t, timer);
	unsigned long flags;

	spin_lock_irqsave(&dpcm->cable->lock, flags);
	if (loopback_pos_update(dpcm->cable) & (1 << dpcm->substream->stream)) {
		loopback_timer_start(dpcm);
		if (dpcm->period_update_pending) {
			dpcm->period_update_pending = 0;
			spin_unlock_irqrestore(&dpcm->cable->lock, flags);
			/* need to unlock before calling below */
			snd_avirt_pcm_period_elapsed(dpcm->substream);
			return;
		}
	}
	spin_unlock_irqrestore(&dpcm->cable->lock, flags);
}

static snd_pcm_uframes_t loopback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	snd_pcm_uframes_t pos;

	spin_lock(&dpcm->cable->lock);
	loopback_pos_update(dpcm->cable);
	pos = dpcm->buf_pos;
	spin_unlock(&dpcm->cable->lock);
	return bytes_to_frames(runtime, pos);
}

static const struct snd_pcm_hardware loopbackap_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE |
		 SNDRV_PCM_INFO_RESUME),
	.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
		    SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |
		    SNDRV_PCM_FMTBIT_FLOAT_LE | SNDRV_PCM_FMTBIT_FLOAT_BE),
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_192000,
	.rate_min = 8000,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = 32,
	.buffer_bytes_max = 2 * 1024 * 1024,
	.period_bytes_min = 64,
	/* note check overflow in frac_pos() using pcm_rate_shift before
	   changing period_bytes_max value */
	.period_bytes_max = 1024 * 1024,
	.periods_min = 1,
	.periods_max = 1024,
	.fifo_size = 0,
};

static void loopback_runtime_free(struct snd_pcm_runtime *runtime)
{
	struct loopback_pcm *dpcm = runtime->private_data;
	kfree(dpcm);
}

static int loopback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm = runtime->private_data;
	struct loopback_cable *cable = dpcm->cable;

	mutex_lock(&dpcm->loopback->cable_lock);
	cable->valid &= ~(1 << substream->stream);
	mutex_unlock(&dpcm->loopback->cable_lock);

	return 0;
}

static int rule_format(struct snd_pcm_hw_params *params,
		       struct snd_pcm_hw_rule *rule)
{
	struct loopback_pcm *dpcm = rule->private;
	struct loopback_cable *cable = dpcm->cable;
	struct snd_mask m;

	snd_mask_none(&m);
	mutex_lock(&dpcm->loopback->cable_lock);
	m.bits[0] = (u_int32_t)cable->hw.formats;
	m.bits[1] = (u_int32_t)(cable->hw.formats >> 32);
	mutex_unlock(&dpcm->loopback->cable_lock);
	return snd_mask_refine(hw_param_mask(params, rule->var), &m);
}

static int rule_rate(struct snd_pcm_hw_params *params,
		     struct snd_pcm_hw_rule *rule)
{
	struct loopback_pcm *dpcm = rule->private;
	struct loopback_cable *cable = dpcm->cable;
	struct snd_interval t;

	mutex_lock(&dpcm->loopback->cable_lock);
	t.min = cable->hw.rate_min;
	t.max = cable->hw.rate_max;
	mutex_unlock(&dpcm->loopback->cable_lock);
	t.openmin = t.openmax = 0;
	t.integer = 0;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int rule_channels(struct snd_pcm_hw_params *params,
			 struct snd_pcm_hw_rule *rule)
{
	struct loopback_pcm *dpcm = rule->private;
	struct loopback_cable *cable = dpcm->cable;
	struct snd_interval t;

	mutex_lock(&dpcm->loopback->cable_lock);
	t.min = cable->hw.channels_min;
	t.max = cable->hw.channels_max;
	mutex_unlock(&dpcm->loopback->cable_lock);
	t.openmin = t.openmax = 0;
	t.integer = 0;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static void free_cable(struct snd_pcm_substream *substream)
{
	struct loopback_cable *cable;

	cable = loopback->cables[substream->pcm->device];
	if (!cable)
		return;
	if (cable->streams[!substream->stream]) {
		/* other stream is still alive */
		spin_lock_irq(&cable->lock);
		cable->streams[substream->stream] = NULL;
		spin_unlock_irq(&cable->lock);
	} else {
		/* free the cable */
		loopback->cables[substream->pcm->device] = NULL;
		kfree(cable);
	}
}

static int loopback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct loopback_pcm *dpcm;
	struct loopback_cable *cable = NULL;
	int err = 0;

	mutex_lock(&loopback->cable_lock);
	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (!dpcm) {
		err = -ENOMEM;
		goto unlock;
	}
	dpcm->loopback = loopback;
	dpcm->substream = substream;
	timer_setup(&dpcm->timer, loopback_timer_function, 0);

	cable = loopback->cables[substream->pcm->device];
	if (!cable) {
		cable = kzalloc(sizeof(*cable), GFP_KERNEL);
		if (!cable) {
			err = -ENOMEM;
			goto unlock;
		}
		spin_lock_init(&cable->lock);
		cable->hw = loopbackap_pcm_hardware;
		loopback->cables[substream->pcm->device] = cable;
	}
	dpcm->cable = cable;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	/* use dynamic rules based on actual runtime->hw values */
	/* note that the default rules created in the PCM midlevel code */
	/* are cached -> they do not reflect the actual state */
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
				  rule_format, dpcm, SNDRV_PCM_HW_PARAM_FORMAT,
				  -1);
	if (err < 0)
		goto unlock;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  rule_rate, dpcm, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		goto unlock;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  rule_channels, dpcm,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		goto unlock;

	runtime->private_data = dpcm;
	runtime->private_free = loopback_runtime_free;
	if (get_notify(dpcm))
		runtime->hw = loopbackap_pcm_hardware;
	else
		runtime->hw = cable->hw;

	spin_lock_irq(&cable->lock);
	cable->streams[substream->stream] = dpcm;
	spin_unlock_irq(&cable->lock);

unlock:
	if (err < 0) {
		free_cable(substream);
		kfree(dpcm);
	}
	mutex_unlock(&loopback->cable_lock);
	return err;
}

static int loopback_close(struct snd_pcm_substream *substream)
{
	struct loopback_pcm *dpcm = substream->runtime->private_data;

	loopback_timer_stop_sync(dpcm);
	mutex_lock(&loopback->cable_lock);
	free_cable(substream);
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static const struct snd_pcm_ops loopbackap_pcm_ops = {
	.open = loopback_open,
	.close = loopback_close,
	.hw_free = loopback_hw_free,
	.prepare = loopback_prepare,
	.trigger = loopback_trigger,
	.pointer = loopback_pointer,
};

static int loopback_rate_shift_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 80000;
	uinfo->value.integer.max = 120000;
	uinfo->value.integer.step = 1;
	return 0;
}

static int loopback_rate_shift_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);

	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.device].rate_shift;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static int loopback_rate_shift_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = ucontrol->value.integer.value[0];
	if (val < 80000)
		val = 80000;
	if (val > 120000)
		val = 120000;
	mutex_lock(&loopback->cable_lock);
	if (val != loopback->setup[kcontrol->id.device].rate_shift) {
		loopback->setup[kcontrol->id.device].rate_shift = val;
		change = 1;
	}
	mutex_unlock(&loopback->cable_lock);
	return change;
}

static int loopback_notify_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);

	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.device].notify;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static int loopback_notify_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;
	val = ucontrol->value.integer.value[0] ? 1 : 0;
	mutex_lock(&loopback->cable_lock);
	if (val != loopback->setup[kcontrol->id.device].notify) {
		loopback->setup[kcontrol->id.device].notify = val;
		change = 1;
	}
	mutex_unlock(&loopback->cable_lock);
	return change;
}

static int loopback_active_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	struct loopback_cable *cable;

	unsigned int val = 0;

	mutex_lock(&loopback->cable_lock);
	cable = loopback->cables[kcontrol->id.device];
	if (cable != NULL) {
		unsigned int running = cable->running ^ cable->pause;

		val = (running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) ? 1 : 0;
	}
	mutex_unlock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int loopback_format_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_PCM_FORMAT_LAST;
	uinfo->value.integer.step = 1;
	return 0;
}

static int loopback_format_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.device].format;
	return 0;
}

static int loopback_rate_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 192000;
	uinfo->value.integer.step = 1;
	return 0;
}

static int loopback_rate_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);
	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.device].rate;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static int loopback_channels_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 1;
	uinfo->value.integer.max = 1024;
	uinfo->value.integer.step = 1;
	return 0;
}

static int loopback_channels_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct loopback *loopback = snd_kcontrol_chip(kcontrol);

	mutex_lock(&loopback->cable_lock);
	ucontrol->value.integer.value[0] =
		loopback->setup[kcontrol->id.device].channels;
	mutex_unlock(&loopback->cable_lock);
	return 0;
}

static struct snd_kcontrol_new loopback_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "PCM Rate Shift 100000",
		.info = loopback_rate_shift_info,
		.get = loopback_rate_shift_get,
		.put = loopback_rate_shift_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "PCM Notify",
		.info = snd_ctl_boolean_mono_info,
		.get = loopback_notify_get,
		.put = loopback_notify_put,
	},
#define ACTIVE_IDX 2
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "PCM Slave Active",
		.info = snd_ctl_boolean_mono_info,
		.get = loopback_active_get,
	},
#define FORMAT_IDX 3
	{ .access = SNDRV_CTL_ELEM_ACCESS_READ,
	  .iface = SNDRV_CTL_ELEM_IFACE_PCM,
	  .name = "PCM Slave Format",
	  .info = loopback_format_info,
	  .get = loopback_format_get },
#define RATE_IDX 4
	{ .access = SNDRV_CTL_ELEM_ACCESS_READ,
	  .iface = SNDRV_CTL_ELEM_IFACE_PCM,
	  .name = "PCM Slave Rate",
	  .info = loopback_rate_info,
	  .get = loopback_rate_get },
#define CHANNELS_IDX 5
	{ .access = SNDRV_CTL_ELEM_ACCESS_READ,
	  .iface = SNDRV_CTL_ELEM_IFACE_PCM,
	  .name = "PCM Slave Channels",
	  .info = loopback_channels_info,
	  .get = loopback_channels_get }
};

static int loopback_mixer_new(struct loopback *loopback, int notify)
{
	struct snd_card *card = loopback->card;
	struct snd_pcm *pcm;
	struct snd_kcontrol *kctl;
	struct loopback_setup *setup;
	int dev, dev_count, idx, err;

	strcpy(card->mixername, "Loopback Mixer");
	dev_count = snd_avirt_stream_count(SNDRV_PCM_STREAM_PLAYBACK);
	for (dev = 0; dev < dev_count; dev++) {
		pcm = loopback->pcm[dev];
		setup = &loopback->setup[dev];
		setup->notify = notify;
		setup->rate_shift = NO_PITCH;
		setup->format = SNDRV_PCM_FORMAT_S16_LE;
		setup->rate = 48000;
		setup->channels = 2;
		for (idx = 0; idx < ARRAY_SIZE(loopback_controls); idx++) {
			kctl = snd_ctl_new1(&loopback_controls[idx], loopback);
			if (!kctl)
				return -ENOMEM;
			kctl->id.device = dev;
			kctl->id.subdevice = 0;
			switch (idx) {
			case ACTIVE_IDX:
				setup->active_id = kctl->id;
				break;
			case FORMAT_IDX:
				setup->format_id = kctl->id;
				break;
			case RATE_IDX:
				setup->rate_id = kctl->id;
				break;
			case CHANNELS_IDX:
				setup->channels_id = kctl->id;
				break;
			default:
				break;
			}
			err = snd_ctl_add(card, kctl);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static void print_dpcm_info(struct snd_info_buffer *buffer,
			    struct loopback_pcm *dpcm, const char *id)
{
	snd_iprintf(buffer, "  %s\n", id);
	if (dpcm == NULL) {
		snd_iprintf(buffer, "    inactive\n");
		return;
	}
	snd_iprintf(buffer, "    buffer_size:\t%u\n", dpcm->pcm_buffer_size);
	snd_iprintf(buffer, "    buffer_pos:\t\t%u\n", dpcm->buf_pos);
	snd_iprintf(buffer, "    silent_size:\t%u\n", dpcm->silent_size);
	snd_iprintf(buffer, "    period_size:\t%u\n", dpcm->pcm_period_size);
	snd_iprintf(buffer, "    bytes_per_sec:\t%u\n", dpcm->pcm_bps);
	snd_iprintf(buffer, "    sample_align:\t%u\n", dpcm->pcm_salign);
	snd_iprintf(buffer, "    rate_shift:\t\t%u\n", dpcm->pcm_rate_shift);
	snd_iprintf(buffer, "    update_pending:\t%u\n",
		    dpcm->period_update_pending);
	snd_iprintf(buffer, "    irq_pos:\t\t%u\n", dpcm->irq_pos);
	snd_iprintf(buffer, "    period_frac:\t%u\n", dpcm->period_size_frac);
	snd_iprintf(buffer, "    last_jiffies:\t%lu (%lu)\n",
		    dpcm->last_jiffies, jiffies);
	snd_iprintf(buffer, "    timer_expires:\t%lu\n", dpcm->timer.expires);
}

static void print_substream_info(struct snd_info_buffer *buffer,
				 struct loopback *loopback, int device)
{
	struct loopback_cable *cable = loopback->cables[device];

	snd_iprintf(buffer, "Cable device %i:\n", device);
	if (cable == NULL) {
		snd_iprintf(buffer, "  inactive\n");
		return;
	}
	snd_iprintf(buffer, "  valid: %u\n", cable->valid);
	snd_iprintf(buffer, "  running: %u\n", cable->running);
	snd_iprintf(buffer, "  pause: %u\n", cable->pause);
	print_dpcm_info(buffer, cable->streams[0], "Playback");
	print_dpcm_info(buffer, cable->streams[1], "Capture");
}

static void print_cable_info(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	struct loopback *loopback = entry->private_data;
	int device;

	mutex_lock(&loopback->cable_lock);
	for (device = 0; device < MAX_STREAMS; device++)
		print_substream_info(buffer, loopback, device);
	mutex_unlock(&loopback->cable_lock);
}

static int loopback_proc_new(struct loopback *loopback, int cidx)
{
	char name[32];
	struct snd_info_entry *entry;
	int err;

	snprintf(name, sizeof(name), "cable#%d", cidx);
	err = snd_card_proc_new(loopback->card, name, &entry);
	if (err < 0)
		return err;

	snd_info_set_text_ops(entry, loopback, print_cable_info);
	return 0;
}

static int loopbackap_configure(struct snd_card *card,
				struct config_group *snd_avirt_stream_group,
				unsigned int stream_count)
{
	int err;
	struct list_head *entry;

	loopback = kzalloc(sizeof(struct loopback), GFP_KERNEL);
	if (!loopback)
		return -ENOMEM;
	loopback->card = card;
	mutex_init(&loopback->cable_lock);

	list_for_each (entry, &snd_avirt_stream_group->cg_children) {
		struct config_item *item =
			container_of(entry, struct config_item, ci_entry);
		struct snd_avirt_stream *stream =
			snd_avirt_stream_from_config_item(item);
		loopback->pcm[stream->device] = stream->pcm;

		AP_INFOK("stream name:%s device:%d channels:%d", stream->name,
			 stream->device, stream->channels);
	}

	err = loopback_mixer_new(loopback, 1);
	if (err < 0)
		return err;

	loopback_proc_new(loopback, 0);
	loopback_proc_new(loopback, 1);

	return 0;
}

/*******************************************************************************
 * Loopback Audio Path AVIRT registration
 ******************************************************************************/
static struct snd_avirt_audiopath loopbackap_module = {
	.uid = AP_UID,
	.name = "Loopback Audio Path",
	.version = { 0, 0, 1 },
	.hw = &loopbackap_pcm_hardware,
	.pcm_ops = &loopbackap_pcm_ops,
	.configure = loopbackap_configure,
};

static int __init alsa_card_loopback_init(void)
{
	int err = 0;

	err = snd_avirt_audiopath_register(&loopbackap_module, &coreinfo);
	if ((err < 0) || (!coreinfo)) {
		AP_ERRORK("coreinfo is NULL!");
		return err;
	}

	return 0;
}

static void __exit alsa_card_loopback_exit(void)
{
	snd_avirt_audiopath_deregister(&loopbackap_module);
}

module_init(alsa_card_loopback_init);
module_exit(alsa_card_loopback_exit)
