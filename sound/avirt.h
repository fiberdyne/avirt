// SPDX-License-Identifier: GPL-2.0
/*
 * AVIRT - ALSA Virtual Soundcard
 *
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 * 
 * avirt.h - AVIRT system-level header
 */

#ifndef __SOUND_AVIRT_H
#define __SOUND_AVIRT_H

#include <sound/core.h>
#include <sound/pcm.h>
#include <linux/configfs.h>

#define MAX_STREAMS 16
#define MAX_NAME_LEN 80

#define DINFO(logname, fmt, args...)                                           \
	snd_printk(KERN_INFO "AVIRT: %s: " fmt "\n", logname, ##args)

#define DERROR(logname, fmt, args...)                                          \
	snd_printk(KERN_ERR "AVIRT: %s: " fmt "\n", logname, ##args)

#define DDEBUG(logname, fmt, args...)                                          \
	snd_printk(KERN_DEBUG "AVIRT: %s: " fmt "\n", logname, ##args)

/**
 * AVIRT Audio Path configure function type
 * Each Audio Path registers this at avirt_audiopath_register time.
 * It is then called by the core once AVIRT has been configured
 */
typedef int (*avirt_audiopath_configure)(struct snd_card *card,
					 struct config_group *stream_group,
					 unsigned int stream_count);

/**
 * AVIRT Audio Path info
 */
struct avirt_audiopath {
	const char *uid; /* Unique identifier */
	const char *name; /* Pretty name */
	unsigned int version[3]; /* Version - Major.Minor.Ext */
	const struct snd_pcm_hardware *hw; /* ALSA PCM HW conf */
	const struct snd_pcm_ops *pcm_ops; /* ALSA PCM op table */
	avirt_audiopath_configure configure; /* Configure callback function */

	void *context;
};

/*
 * Audio stream configuration
 */
struct avirt_stream {
	char name[MAX_NAME_LEN]; /* Stream name */
	char map[MAX_NAME_LEN]; /* Stream Audio Path mapping */
	unsigned int channels; /* Stream channel count */
	unsigned int device; /* Stream PCM device no. */
	unsigned int direction; /* Stream direction */
	struct snd_pcm *pcm; /* ALSA PCM  */
	struct config_item item; /* configfs item reference */
};

/**
 * AVIRT core info
 */
struct avirt_coreinfo {
	unsigned int version[3];
};

/**
 * avirt_audiopath_register - register Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be registered
 * @core: ALSA virtual driver core info
 * @return: 0 on success or error code otherwise
 */
int avirt_audiopath_register(struct avirt_audiopath *audiopath,
			     struct avirt_coreinfo **coreinfo);

/**
 * avirt_audiopath_deregister - deregister Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be deregistered
 * @return: 0 on success or error code otherwise
 */
int avirt_audiopath_deregister(struct avirt_audiopath *audiopath);

/**
 * avirt_audiopath_get - retrieves the Audio Path by it's UID
 * @uid: Unique ID for the Audio Path
 * @return: Corresponding Audio Path
 */
struct avirt_audiopath *avirt_audiopath_get(const char *uid);

/**
 * avirt_stream_count - get the stream count for the given direction
 * @direction: The direction to get the stream count for
 * @return: The stream count
 */
int avirt_stream_count(unsigned int direction);

/**
 * avirt_stream_from_config_item - Convert a config_item to an avirt_stream
 * @item: The config_item to convert from
 * @return: The item's avirt_stream if successful, NULL otherwise
 */
static inline struct avirt_stream *
avirt_stream_from_config_item(struct config_item *item)
{
	return item ? container_of(item, struct avirt_stream, item) : NULL;
}

/**
 * avirt_pcm_period_elapsed - PCM buffer complete callback
 * @substream: pointer to ALSA PCM substream
 *
 * This should be called from a child Audio Path once it has finished processing
 * the PCM buffer
 */
void avirt_pcm_period_elapsed(struct snd_pcm_substream *substream);

#endif // __SOUND_AVIRT_H
