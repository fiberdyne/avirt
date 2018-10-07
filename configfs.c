// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * configfs.c - configfs for virtual ALSA card
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#include <sound/core.h>
#include "core_internal.h"

static bool streams_sealed = false;

static ssize_t cfg_avirt_stream_direction_show(struct config_item *item,
					       char *page)
{
	ssize_t count;
	struct avirt_stream *stream = avirt_stream_from_config_item(item);

	count = sprintf(page, "%d\n", stream->direction);

	return count;
}
CONFIGFS_ATTR_RO(cfg_avirt_stream_, direction);

static ssize_t cfg_avirt_stream_map_show(struct config_item *item, char *page)
{
	struct avirt_stream *stream = avirt_stream_from_config_item(item);

	return sprintf(page, "%s\n", stream->map);
}

static ssize_t cfg_avirt_stream_map_store(struct config_item *item,
					  const char *page, size_t count)
{
	struct avirt_stream *stream = avirt_stream_from_config_item(item);

	memcpy(stream->map, (char *)page, count);

	return count;
}
CONFIGFS_ATTR(cfg_avirt_stream_, map);

static ssize_t cfg_avirt_stream_channels_show(struct config_item *item,
					      char *page)
{
	ssize_t count;
	struct avirt_stream *stream = avirt_stream_from_config_item(item);

	count = sprintf(page, "%d\n", stream->channels);

	return count;
}

static ssize_t cfg_avirt_stream_channels_store(struct config_item *item,
					       const char *page, size_t count)
{
	int err;
	struct avirt_stream *stream = avirt_stream_from_config_item(item);
	unsigned long tmp;
	char *p = (char *)page;

	err = kstrtoul(p, 10, &tmp);
	if (err < 0)
		return err;

	if (tmp > INT_MAX)
		return -ERANGE;

	stream->channels = tmp;

	pr_info("%s [ARGS] channels: %d\n", __func__, stream->channels);

	return count;
}
CONFIGFS_ATTR(cfg_avirt_stream_, channels);

static struct configfs_attribute *cfg_avirt_stream_attrs[] = {
	&cfg_avirt_stream_attr_channels,
	&cfg_avirt_stream_attr_map,
	&cfg_avirt_stream_attr_direction,
	NULL,
};

static void cfg_avirt_stream_release(struct config_item *item)
{
	pr_info("%s [ARGS] item->name:%s\n", __func__, item->ci_namebuf);
	kfree(avirt_stream_from_config_item(item));
}

static struct configfs_item_operations cfg_avirt_stream_ops = {
	.release = cfg_avirt_stream_release,
};

static struct config_item_type cfg_avirt_stream_type = {
	.ct_item_ops = &cfg_avirt_stream_ops,
	.ct_attrs = cfg_avirt_stream_attrs,
	.ct_owner = THIS_MODULE,
};

static struct config_item *
cfg_avirt_stream_make_item(struct config_group *group, const char *name)
{
	char *split;
	int direction;
	struct avirt_stream *stream;

	// Get prefix (playback_ or capture_)
	split = strsep((char **)&name, "_");
	if (!split) {
		pr_err("Stream name: '%s' invalid!\n", split);
		pr_err("Must begin with playback_ * or capture_ *\n");
		return ERR_PTR(-EINVAL);
	}
	if (!strcmp(split, "playback")) {
		direction = SNDRV_PCM_STREAM_PLAYBACK;
	} else if (!strcmp(split, "capture")) {
		direction = SNDRV_PCM_STREAM_CAPTURE;
	} else {
		pr_err("Stream name: '%s' invalid!\n", split);
		pr_err("Must begin with playback_ * or capture_ *\n");
		return ERR_PTR(-EINVAL);
	}

	// Get stream name, and create PCM for stream
	split = strsep((char **)&name, "\n");
	stream = __avirt_stream_create(split, direction);
	if (IS_ERR(stream))
		return ERR_PTR(PTR_ERR(stream));

	config_item_init_type_name(&stream->item, name, &cfg_avirt_stream_type);

	return &stream->item;
}

static ssize_t cfg_avirt_stream_group_sealed_show(struct config_item *item,
						  char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", streams_sealed);
}

static ssize_t cfg_avirt_stream_group_sealed_store(struct config_item *item,
						   const char *page,
						   size_t count)
{
	unsigned long tmp;
	char *p = (char *)page;

	if (streams_sealed) {
		pr_err("AVIRT streams are already sealed!\n");
		return -EPERM;
	}

	CHK_ERR(kstrtoul(p, 10, &tmp));

	if (tmp != 1) {
		pr_err("AVIRT streams can only be sealed, not unsealed!\n");
		return -ERANGE;
	}

	streams_sealed = (bool)tmp;

	CHK_ERR(__avirt_card_register());

	return count;
}
CONFIGFS_ATTR(cfg_avirt_stream_group_, sealed);

static struct configfs_attribute *cfg_avirt_stream_group_attrs[] = {
	&cfg_avirt_stream_group_attr_sealed,
	NULL,
};

static struct configfs_group_operations cfg_avirt_stream_group_ops = {
	.make_item = cfg_avirt_stream_make_item
};

static struct config_item_type cfg_stream_group_type = {
	.ct_group_ops = &cfg_avirt_stream_group_ops,
	.ct_attrs = cfg_avirt_stream_group_attrs,
	.ct_owner = THIS_MODULE
};

static struct config_item_type cfg_avirt_group_type = {
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem cfg_subsys = {
	.su_group =
		{
			.cg_item =
				{
					.ci_namebuf = "avirt",
					.ci_type = &cfg_avirt_group_type,
				},
		},
};

int __init __avirt_configfs_init(struct avirt_core *core)
{
	int err;

	config_group_init(&cfg_subsys.su_group);
	mutex_init(&cfg_subsys.su_mutex);
	err = configfs_register_subsystem(&cfg_subsys);
	if (err) {
		pr_err("Cannot register configfs subsys!\n");
		return err;
	}
	core->stream_group = configfs_register_default_group(
		&cfg_subsys.su_group, "streams", &cfg_stream_group_type);
	if (IS_ERR(core->stream_group)) {
		err = PTR_ERR(core->stream_group);
		pr_err("Cannot register configfs default group 'streams'!\n");
		goto exit_configfs;
	}

	return 0;

exit_configfs:
	configfs_unregister_subsystem(&cfg_subsys);

	return err;
}

void __exit __avirt_configfs_exit(struct avirt_core *core)
{
	configfs_unregister_default_group(core->stream_group);
	configfs_unregister_subsystem(&cfg_subsys);
}
