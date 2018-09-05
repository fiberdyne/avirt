// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * core.c - Implementation of core module for virtual ALSA card
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include "avirt/core.h"
#include "alsa.h"

MODULE_AUTHOR("JOSHANNE <james.oshannessy@fiberdyne.com.au>");
MODULE_AUTHOR("MFARRUGI <mark.farrugia@fiberdyne.com.au>");
MODULE_DESCRIPTION("A configurable virtual soundcard");
MODULE_LICENSE("GPL v2");

#define SND_AVIRTUAL_DRIVER "snd_avirt"
#define MAX_PCM_DEVS 8
#define MAX_AUDIOPATHS 4
#define DEFAULT_FILE_PERMS 0644

/* Number of playback devices to create (max = MAX_PCM_DEVS) */
static unsigned playback_num = 0;
/* Number of capture devices to create (max = MAX_PCM_DEVS) */
static unsigned capture_num = 0;
/* Names per playback device */
static char *playback_names[MAX_PCM_DEVS];
/* Names per capture device */
static char *capture_names[MAX_PCM_DEVS];
/* Channels per playback device */
static unsigned playback_chans[MAX_PCM_DEVS];
/* Channels per capture device */
static unsigned capture_chans[MAX_PCM_DEVS];

module_param(playback_num, int, DEFAULT_FILE_PERMS);
MODULE_PARM_DESC(playback_num,
		 "Number of playback devices to create (max = MAX_PCM_DEVS)");
module_param(capture_num, int, DEFAULT_FILE_PERMS);
MODULE_PARM_DESC(capture_num,
		 "Number of capture devices to create (max = MAX_PCM_DEVS)");
module_param_array(playback_names, charp, NULL, DEFAULT_FILE_PERMS);
MODULE_PARM_DESC(playback_names, "Names per playback device");
module_param_array(capture_names, charp, NULL, DEFAULT_FILE_PERMS);
MODULE_PARM_DESC(capture_names, "Names per capture device");
module_param_array(playback_chans, int, NULL, DEFAULT_FILE_PERMS);
MODULE_PARM_DESC(playback_chans, "Channels per playback device");
module_param_array(capture_chans, int, NULL, DEFAULT_FILE_PERMS);
MODULE_PARM_DESC(capture_chans, "Channels per capture device");

static struct avirt_core {
	struct device *dev;

	struct class *avirt_class;
	struct platform_device *platform_dev;
} core;

struct avirt_coreinfo coreinfo = {
	.version = { 0, 0, 1 },
	.pcm_buff_complete = pcm_buff_complete_cb,
};

static LIST_HEAD(audiopath_list);

/**
 * avirt_probe - Register ALSA soundcard
 * @devptr: Platform driver device
 * @return: 0 on success or error code otherwise
 */
static int avirt_probe(struct platform_device *devptr)
{
	// struct avirt_alsa_devconfig capture_config[MAX_PCM_DEVS];
	struct avirt_alsa_devconfig playback_config[MAX_PCM_DEVS];
	int err = 0, i = 0;

	if (playback_num == 0 && capture_num == 0) {
		pr_err("playback_num or capture_num must be greater than 0!\n");
		return -EINVAL;
	}

	// Set up playback
	for (i = 0; i < playback_num; i++) {
		if (!playback_names[i]) {
			pr_err("Playback config devicename is NULL for idx=%d\n",
			       i);
			return -EINVAL;
		}
		memcpy((char *)playback_config[i].devicename, playback_names[i],
		       MAX_NAME_LEN);
		if (playback_chans[i] == 0) {
			pr_err("Playback config channels is 0 for idx=%d\n", i);
			return -EINVAL;
		}
		playback_config[i].channels = playback_chans[i];
	}
	err = avirt_alsa_configure_pcm(playback_config,
				       SNDRV_PCM_STREAM_PLAYBACK, playback_num);
	if (err < 0)
		return err;

// Does not work yet!
#if 0
	// Set up capture
	for (i = 0; i < capture_num; i++) {
		if (!capture_names[i]) {
			pr_err("Capture config devicename is NULL for idx=%d",
			       i);
			return -EINVAL;
		}
		memcpy((char *)capture_config[i].devicename, capture_names[i],
		       255);

		if (capture_chans[i] == 0) {
			pr_err("Capture config channels is 0 for idx=%d");
			return -EINVAL;
		}
		capture_config[i].channels = capture_chans[i];
	}
	err = avirt_alsa_configure_pcm(capture_config, SNDRV_PCM_STREAM_CAPTURE, capture_num);
	if (err < 0)
		return err;
#endif

	// Register for ALSA
	CHK_ERR(avirt_alsa_register(devptr));

	return err;
}

/**
 * avirt_remove - Deregister ALSA soundcard
 * @devptr: Platform driver device
 * @return: 0 on success or error code otherwise
 */
static int avirt_remove(struct platform_device *devptr)
{
	DPRINTK();
	return avirt_alsa_deregister();
}

static struct platform_driver avirt_driver = {
	.probe = avirt_probe,
	.remove = avirt_remove,
	.driver =
		{
			.name = SND_AVIRTUAL_DRIVER,
		},
};

struct avirt_audiopath_obj {
	struct kobject kobj;
	struct list_head list;
	struct avirt_audiopath *path;
};

static struct kset *avirt_audiopath_kset;
static struct kobject *kobj;

#define to_audiopath_obj(d) container_of(d, struct avirt_audiopath_obj, kobj)
#define to_audiopath_attr(a) \
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
 * @name: name of the Audio Path
 *
 * This creates an Audio Path object and assigns the kset and registers
 * it with sysfs.
 * @return: Pointer to the Audio Path object or NULL if it failed.
 */
static struct avirt_audiopath_obj *create_avirt_audiopath_obj(const char *name)
{
	struct avirt_audiopath_obj *avirt_audiopath;
	int retval;

	avirt_audiopath = kzalloc(sizeof(*avirt_audiopath), GFP_KERNEL);
	if (!avirt_audiopath)
		return NULL;
	avirt_audiopath->kobj.kset = avirt_audiopath_kset;
	retval = kobject_init_and_add(&avirt_audiopath->kobj,
				      &avirt_audiopath_ktype, kobj, "%s", name);
	if (retval) {
		kobject_put(&avirt_audiopath->kobj);
		return NULL;
	}
	kobject_uevent(&avirt_audiopath->kobj, KOBJ_ADD);
	return avirt_audiopath;
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
 * avirt_get_current_audiopath - retrieves the current Audio Path
 * @return: Current Audio Path
 */
struct avirt_audiopath *avirt_get_current_audiopath()
{
	struct avirt_audiopath_obj *ap_obj = list_entry(
		audiopath_list.next, struct avirt_audiopath_obj, list);
	return ap_obj->path;
}

/**
 * avirt_register_audiopath - register Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be registered
 * @core: ALSA virtual driver core info
 * @return: 0 on success or error code otherwise
 */
int avirt_register_audiopath(struct avirt_audiopath *audiopath,
			     struct avirt_coreinfo **info)
{
	struct avirt_audiopath_obj *audiopath_obj;

	if (!audiopath) {
		pr_err("Bad audio path\n");
		return -EINVAL;
	}

	audiopath_obj = create_avirt_audiopath_obj(audiopath->name);
	if (!audiopath_obj) {
		pr_info("failed to alloc driver object\n");
		return -ENOMEM;
	}
	audiopath_obj->path = audiopath;

	audiopath->context = audiopath_obj;
	pr_info("Registered new Audio Path: %s\n", audiopath->name);
	pr_info("\tBlocksize: %d, Periods: %d\n", audiopath->blocksize,
		audiopath->hw->periods_max);
	list_add_tail(&audiopath_obj->list, &audiopath_list);

	*info = &coreinfo;

	return 0;
}
EXPORT_SYMBOL_GPL(avirt_register_audiopath);

/**
 * avirt_deregister_audiopath - deregister Audio Path with ALSA virtual driver
 * @audiopath: Audio Path to be deregistered
 * @return: 0 on success or error code otherwise
 */
int avirt_deregister_audiopath(struct avirt_audiopath *audiopath)
{
	struct avirt_audiopath_obj *audiopath_obj;

	// Check if audio path is registered
	if (!audiopath) {
		pr_err("Bad Audio Path Driver\n");
		return -EINVAL;
	}

	audiopath_obj = audiopath->context;
	if (!audiopath_obj) {
		pr_info("driver not registered.\n");
		return -EINVAL;
	}

	list_del(&audiopath_obj->list);
	destroy_avirt_audiopath_obj(audiopath_obj);
	pr_info("Deregistered Audio Path %s\n", audiopath->name);

	return 0;
}
EXPORT_SYMBOL_GPL(avirt_deregister_audiopath);

/**
 * avirt_unregister_all - Unregister the platform device driver
 */
static void avirt_unregister_all(void)
{
	platform_device_unregister(core.platform_dev);
	platform_driver_unregister(&avirt_driver);
}

/**
 * core_init - Initialize the kernel module
 */
static int __init core_init(void)
{
	int err;

	pr_info("Alsa Virtual Sound Driver avirt-%d.%d.%d\n",
		coreinfo.version[0], coreinfo.version[1], coreinfo.version[2]);

	err = platform_driver_register(&avirt_driver);
	if (err < 0)
		return err;

	core.platform_dev = platform_device_register_simple(SND_AVIRTUAL_DRIVER,
							    0, NULL, 0);
	if (IS_ERR(core.platform_dev)) {
		err = PTR_ERR(core.platform_dev);
		pr_err("platform_dev error [%d]!\n", err);
		goto exit_platform_device;
	}

	core.avirt_class = class_create(THIS_MODULE, SND_AVIRTUAL_DRIVER);
	if (IS_ERR(core.avirt_class)) {
		pr_err("No udev support\n");
		err = PTR_ERR(core.avirt_class);
		goto exit_bus;
	}

	core.dev = device_create(core.avirt_class, NULL, 0, NULL, "avirtcore");
	if (IS_ERR(core.dev)) {
		err = PTR_ERR(core.dev);
		goto exit_class;
	}

	avirt_audiopath_kset =
		kset_create_and_add("audiopaths", NULL, &core.dev->kobj);
	if (!avirt_audiopath_kset) {
		err = -ENOMEM;
		goto exit_class_container;
	}

	return 0;

exit_class_container:
	device_destroy(core.avirt_class, 0);
exit_class:
	class_destroy(core.avirt_class);
exit_bus:
exit_platform_device:
	avirt_unregister_all();
	return err;
}

/**
 * core_exit - Destroy the kernel module
 */
static void __exit core_exit(void)
{
	kset_unregister(avirt_audiopath_kset);
	device_destroy(core.avirt_class, 0);
	class_destroy(core.avirt_class);

	avirt_unregister_all();

	pr_info("playback_num: %d, capture_num: %d\n", playback_num,
		capture_num);

	pr_info("playback_chans: %d %d %d %d %d %d %d %d\n", playback_chans[0],
		playback_chans[1], playback_chans[2], playback_chans[3],
		playback_chans[4], playback_chans[5], playback_chans[6],
		playback_chans[7]);

	pr_info("capture_chans: %d %d %d %d %d %d %d %d\n", capture_chans[0],
		capture_chans[1], capture_chans[2], capture_chans[3],
		capture_chans[4], capture_chans[5], capture_chans[6],
		capture_chans[7]);

	pr_info("playback_names: %s %s %s %s %s %s %s %s\n", playback_names[0],
		playback_names[1], playback_names[2], playback_names[3],
		playback_names[4], playback_names[5], playback_names[6],
		playback_names[7]);

	pr_info("capture_names: %s %s %s %s %s %s %s %s\n", capture_names[0],
		capture_names[1], capture_names[2], capture_names[3],
		capture_names[4], capture_names[5], capture_names[6],
		capture_names[7]);
}

module_init(core_init);
module_exit(core_exit);
