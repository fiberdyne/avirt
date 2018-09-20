// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA Virtual Soundcard
 *
 * utils.h - Some useful utilities for AVIRT
 *
 * Copyright (C) 2010-2018 Fiberdyne Systems Pty Ltd
 */

#ifndef __AVIRT_UTILS_H__
#define __AVIRT_UTILS_H__

#include <linux/slab.h>

#define PRINT_ERR(errno, errmsg) \
	pr_err("[%s]:[ERRNO:%d]: %s ", __func__, errno, (errmsg));

#define CHK_ERR(errno)                  \
	do {                            \
		if ((errno) < 0)        \
			return (errno); \
	} while (0)

#define CHK_ERR_V(errno, errmsg, ...)                               \
	do {                                                        \
		if ((errno) < 0) {                                  \
			PRINT_ERR((errno), (errmsg), ##__VA_ARGS__) \
			return (errno);                             \
		}                                                   \
	} while (0)

#define CHK_NULL(x)                     \
	do {                            \
		if (!(x))               \
			return -EFAULT; \
	} while (0)

#define CHK_NULL_V(x, errmsg, ...)                                            \
	do {                                                                  \
		if (!(x)) {                                                   \
			char *errmsg_done =                                   \
				kasprintf(GFP_KERNEL, errmsg, ##__VA_ARGS__); \
			PRINT_ERR(EFAULT, errmsg_done);                       \
			kfree(errmsg_done);                                   \
			return -EFAULT;                                       \
		}                                                             \
	} while (0)

#endif
