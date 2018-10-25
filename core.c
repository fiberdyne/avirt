// SPDX-License-Identifier: GPL-2.0
/*
 * AVIRT - ALSA Virtual Soundcard
 *
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 * 
 * core.c - AVIRT core internals
 */

#include <linux/module.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <avirt/core.h>

#include "core_internal.h"

MODULE_AUTHOR("James O'Shannessy <james.oshannessy@fiberdyne.com.au>");
MODULE_AUTHOR("Mark Farrugia <mark.farrugia@fiberdyne.com.au>");
MODULE_DESCRIPTION("ALSA virtual, dynamic soundcard");
MODULE_LICENSE("GPL v2");

#define D_LOGNAME "core"

#define D_INFOK(fmt, args...) DINFO(D_LOGNAME, fmt, ##args)
#define D_PRINTK(fmt, args...) DDEBUG(D_LOGNAME, fmt, ##args)
#define D_ERRORK(fmt, args...) DERROR(D_LOGNAME, fmt, ##args)

#define SND_AVIRTUAL_DRIVER "snd_avirt"

extern struct snd_pcm_ops pcm_ops;

static struct avirt_core core = {
	.stream_count = 0,
	.streams_sealed = false,
};

struct avirt_coreinfo coreinfo = {
	.version = { 0, 0, 1 },
};

static LIST_HEAD(audiopath_list);

struct avirt_audiopath_obj {
	struct kobject kobj;
	struct list_head list;
	struct avirt_audiopath *path;
};

static struct kset *avirt_audiopath_kset;
static struct kobject *kobj;

#define to_audiopath_obj(d) container_of(d, struct avirt_audiopath_obj, kobj)
#define to_audiopath_attr(a)                                                   \
	container_of(a, struct avirt_audiopath_attribute, attr)

/**
 * struct avirt_audiopath_attribute - access the attributes of Audio Path
 * @attr: attributes of an Audio Path
 * @show: pointer to the show function
 * @store: pointer to the store function
 */
struct avirt_audiopath_attribute {
	struct attribute attr;
	ssize_t (*show)(struct avirt_audiopath_obj *d,
			struct avirt_audiopath_attribute *attr, char *buf);
	ssize_t (*store)(struct avirt_audiopath_obj *d,
			 struct avirt_audiopath_attribute *attr,
			 const char *buf, size_t count);
};

/**
 * audiopath_attr_show - show function of an Audio Path
 * @kobj: pointer to kobject
 * @attr: pointer to attribute struct
 * @buf: buffer
 */
static ssize_t audiopath_attr_show(struct kobject *kobj, struct attribute *attr,
				   char *buf)
{
	struct avirt_audiopath_attribute *audiopath_attr;
	struct avirt_audiopath_obj *audiopath_obj;

	audiopath_attr = to_audiopath_attr(attr);
	audiopath_obj = to_audiopath_obj(kobj);

	if (!audiopath_attr->show)
		return -EIO;

	return audiopath_attr->show(audiopath_obj, audiopath_attr, buf);
}

/**
 * audiopath_attr_store - attribute store function of Audio Path object
 * @kobj: pointer to kobject
 * @attr: pointer to attribute struct
 * @buf: buffer
 * @len: length of buffer
 */
static ssize_t audiopath_attr_store(struct kobject *kobj,
				    struct attribute *attr, const char *buf,
				    size_t len)
{
	struct avirt_audiopath_attribute *audiopath_attr;
	struct avirt_audiopath_obj *audiopath_obj;

	audiopath_attr = to_audiopath_attr(attr);
	audiopath_obj = to_audiopath_obj(kobj);

	if (!audiopath_attr->store)
		return -EIO;
	return audiopath_attr->store(audiopath_obj, audiopath_attr, buf, len);
}

static const struct sysfs_ops avirt_audiopath_sysfs_ops = {
	.show = audiopath_attr_show,
	.store = audiopath_attr_store,
};

/**
 * avirt_audiopath_release - Audio Path release function
 * @kobj: pointer to Audio Paths's kobject
 */
static void avirt_audiopath_release(struct kobject *kobj)
{
	struct avirt_audiopath_obj *audiopath_obj = to_audiopath_obj(kobj);

	kfree(audiopath_obj);
}

static ssize_t audiopath_name_show(struct avirt_audiopath_obj *audiopath_obj,
				   struct avirt_audiopath_attribute *attr,
				   char *buf)
{
	return sprintf(buf, "%s\n", audiopath_obj->path->name);
}

static ssize_t audiopath_version_show(struct avirt_audiopath_obj *audiopath_obj,
				      struct avirt_audiopath_attribute *attr,
				      char *buf)
{
	struct avirt_audiopath *audiopath = audiopath_obj->path;

	return sprintf(buf, "%d.%d.%d\n", audiopath->version[0],
		       audiopath->version[1], audiopath->version[2]);
}

static struct avirt_audiopath_attribute avirt_audiopath_attrs[] = {
	__ATTR_RO(audiopath_name),
	__ATTR_RO(audiopath_version),
};

static struct attribute *avirt_audiopath_def_attrs[] = {
	&avirt_audiopath_attrs[0].attr,
	&avirt_audiopath_attrs[1].attr,
	NULL,
};

static struct kobj_type avirt_audiopath_ktype = {
	.sysfs_ops = &avirt_audiopath_sysfs_ops,
	.release = avirt_audiopath_release,
	.default_attrs = avirt_audiopath_def_attrs,
};

/**
 * create_avirt_audiopath_obj - creates an Audio Path object
 * @uid: Unique ID of the Audio Path
 *
 * This creates an Audio Path object and assigns the kset and registers
 * it with sysfs.
 * @return: Pointer to the Audio Path object or NULL if it failed.
 */
static struct avirt_audiopath_obj *create_avirt_audiopath_obj(const char *uid)
{
	struct avirt_audiopath_obj *avirt_audiopath;
	int retval;

	avirt_audiopath = kzalloc(sizeof(*avirt_audiopath), GFP_KERNEL);
	if (!avirt_audiopath)
		return NULL;
	avirt_audiopath->kobj.kset = avirt_audiopath_kset;
	retval = kobject_init_and_add(&avirt_audiopath->kobj,
				      &avirt_audiopath_ktype, kobj, "%s", uid);
	if (retval) {
		kobject_put(&avirt_audiopath->kobj);
		return NULL;
	}
	kobject_uevent(&avirt_audiopath->kobj, KOBJ_ADD);
	return avirt_audiopath;
}

static struct snd_pcm *pcm_create(struct avirt_stream *stream)
{
	bool playback = false, capture = false;
	struct snd_pcm *pcm;
	int err;

	/** Special case: loopback */
	if (!strcmp(stream->map, "ap_loopback")) {
		playback = true;
		capture = true;
		(&pcm_ops)->copy_user = NULL;
	} else if (!stream->direction) {
		playback = true;
	} else {
		capture = true;
	}

	err = snd_pcm_new(core.card, stream->name, stream->device, playback,
			  capture, &pcm);

	if (err < 0)
		return ERR_PTR(err);

	/** Register driver callbacks */
	if (playback)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &pcm_ops);
	if (capture)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_ops);

	pcm->info_flags = 0;
	strcpy(pcm->name, stream->name);

	return pcm;
}

/**
 * destroy_avirt_audiopath_obj - destroys an Audio Path object
 * @name: the Audio Path object
 */
static void destroy_avirt_audiopath_obj(struct avirt_audiopath_obj *p)
{
	kobject_put(&p->kobj);
}

/**
 * avirt_audiopath_get - retrieves the Audio Path by its UID
 * @uid: Unique ID for the Audio Path
 * @return: Corresponding Audio Path
 */
struct avirt_audiopath *avirt_audiopath_get(const char *uid)
{
	struct avirt_audiopath_obj *ap_obj;

	list_for_each_entry (ap_obj, &audiopath_list, list) {
		// pr_info("get_ap %s\n", ap_obj->path->uid);
		if (!strcmp(ap_obj->path->uid, uid))
			return ap_obj->path;
	}

	return NULL;
}

/**
 * avirt_audiopath_register - register Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be registered
 * @core: ALSA virtual driver core info
 * @return: 0 on success or error code otherwise
 */
int avirt_audiopath_register(struct avirt_audiopath *audiopath,
			     struct avirt_coreinfo **info)
{
	struct avirt_audiopath_obj *audiopath_obj;

	if (!audiopath) {
		D_ERRORK("Audio Path is NULL!");
		return -EINVAL;
	}

	audiopath_obj = create_avirt_audiopath_obj(audiopath->uid);
	if (!audiopath_obj) {
		D_INFOK("Failed to alloc driver object");
		return -ENOMEM;
	}
	audiopath_obj->path = audiopath;

	audiopath->context = audiopath_obj;
	D_INFOK("Registered new Audio Path: %s", audiopath->name);

	list_add_tail(&audiopath_obj->list, &audiopath_list);

	// If we have already sealed the streams, configure this AP
	if (core.streams_sealed)
		audiopath->configure(core.card, core.stream_group,
				     core.stream_count);

	*info = &coreinfo;

	return 0;
}
EXPORT_SYMBOL_GPL(avirt_audiopath_register);

/**
 * avirt_audiopath_deregister - deregister Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be deregistered
 * @return: 0 on success or error code otherwise
 */
int avirt_audiopath_deregister(struct avirt_audiopath *audiopath)
{
	struct avirt_audiopath_obj *audiopath_obj;

	// Check if audio path is registered
	if (!audiopath) {
		D_ERRORK("Bad Audio Path Driver");
		return -EINVAL;
	}

	audiopath_obj = audiopath->context;
	if (!audiopath_obj) {
		D_INFOK("driver not registered");
		return -EINVAL;
	}

	list_del(&audiopath_obj->list);
	destroy_avirt_audiopath_obj(audiopath_obj);
	D_INFOK("Deregistered Audio Path %s", audiopath->uid);

	return 0;
}
EXPORT_SYMBOL_GPL(avirt_audiopath_deregister);

/**
 * avirt_stream_count - get the stream count for the given direction
 * @direction: The direction to get the stream count for
 * @return: The stream count
 */
int avirt_stream_count(unsigned int direction)
{
	struct list_head *entry;
	struct config_item *item;
	struct avirt_stream *stream;
	unsigned int count = 0;

	if (direction > 1)
		return -ERANGE;

	list_for_each (entry, &core.stream_group->cg_children) {
		item = container_of(entry, struct config_item, ci_entry);
		stream = avirt_stream_from_config_item(item);
		if (!stream)
			return -EFAULT;
		if (stream->direction == direction)
			count++;
	}

	return count;
}
EXPORT_SYMBOL_GPL(avirt_stream_count);

/**
 * __avirt_stream_create - Create audio stream, including it's ALSA PCM device
 * @name: The name designated to the audio stream
 * @direction: The PCM direction (SNDRV_PCM_STREAM_PLAYBACK or
 *             SNDRV_PCM_STREAM_CAPTURE)
 * @return: The newly created audio stream if successful, or an error pointer
 */
struct avirt_stream *__avirt_stream_create(const char *name, int direction)
{
	struct avirt_stream *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return ERR_PTR(-ENOMEM);

	strcpy(stream->name, name);
	strcpy(stream->map, "none");
	stream->channels = 0;
	stream->direction = direction;
	stream->device = core.stream_count++;

	D_INFOK("name: %s device:%d", name, stream->device);

	return stream;
}

int __avirt_streams_seal(void)
{
	int err = 0;
	struct avirt_audiopath_obj *ap_obj;
	struct avirt_stream *stream;
	struct config_item *item;
	struct list_head *entry;

	if (core.streams_sealed) {
		D_ERRORK("streams are already sealed!");
		return -1;
	}

	list_for_each (entry, &core.stream_group->cg_children) {
		item = container_of(entry, struct config_item, ci_entry);
		stream = avirt_stream_from_config_item(item);
		if (!stream)
			return -EFAULT;
		stream->pcm = pcm_create(stream);
		if (IS_ERR_OR_NULL(stream->pcm))
			return (PTR_ERR(stream->pcm));
	}

	list_for_each_entry (ap_obj, &audiopath_list, list) {
		D_INFOK("configure() AP uid: %s", ap_obj->path->uid);
		ap_obj->path->configure(core.card, core.stream_group,
					core.stream_count);
	}

	err = snd_card_register(core.card);
	if (err < 0) {
		D_ERRORK("Sound card registration failed!");
		snd_card_free(core.card);
	}

	core.streams_sealed = true;

	return err;
}

bool __avirt_streams_sealed(void)
{
	return core.streams_sealed;
}

struct avirt_stream *__avirt_stream_find_by_device(unsigned int device)
{
	struct avirt_stream *stream;
	struct config_item *item;
	struct list_head *entry;

	if (device >= core.stream_count) {
		D_ERRORK("Stream device number is larger than stream count");
		return ERR_PTR(-EINVAL);
	}

	list_for_each (entry, &core.stream_group->cg_children) {
		item = container_of(entry, struct config_item, ci_entry);
		stream = avirt_stream_from_config_item(item);
		if (!stream)
			return ERR_PTR(-EFAULT);
		if (stream->device == device)
			return stream;
	}

	return NULL;
}

/**
 * core_init - Initialize the kernel module
 */
static int __init core_init(void)
{
	int err;

	D_INFOK("Alsa Virtual Sound Driver avirt-%d.%d.%d", coreinfo.version[0],
		coreinfo.version[1], coreinfo.version[2]);

	core.avirt_class = class_create(THIS_MODULE, SND_AVIRTUAL_DRIVER);
	if (IS_ERR(core.avirt_class)) {
		D_ERRORK("No udev support");
		return PTR_ERR(core.avirt_class);
	}

	core.dev = device_create(core.avirt_class, NULL, 0, NULL, "avirtcore");
	if (IS_ERR(core.dev)) {
		err = PTR_ERR(core.dev);
		goto exit_class;
	}

	// Create the card instance
	err = snd_card_new(core.dev, SNDRV_DEFAULT_IDX1, "avirt", THIS_MODULE,
			   0, &core.card);
	if (err < 0) {
		D_ERRORK("Failed to create sound card");
		goto exit_class_container;
	}

	strncpy(core.card->driver, "avirt-alsa-dev", 16);
	strncpy(core.card->shortname, "avirt", 32);
	strncpy(core.card->longname, "A virtual sound card driver for ALSA",
		80);

	avirt_audiopath_kset =
		kset_create_and_add("audiopaths", NULL, &core.dev->kobj);
	if (!avirt_audiopath_kset) {
		err = -ENOMEM;
		goto exit_snd_card;
	}

	err = __avirt_configfs_init(&core);
	if (err < 0)
		goto exit_snd_card;

	return 0;

exit_snd_card:
	snd_card_free(core.card);
exit_class_container:
	device_destroy(core.avirt_class, 0);
exit_class:
	class_destroy(core.avirt_class);

	return err;
}

/**
 * core_exit - Destroy the kernel module
 */
static void __exit core_exit(void)
{
	__avirt_configfs_exit(&core);

	kset_unregister(avirt_audiopath_kset);
	snd_card_free(core.card);
	device_destroy(core.avirt_class, 0);
	class_destroy(core.avirt_class);
}

module_init(core_init);
module_exit(core_exit);
