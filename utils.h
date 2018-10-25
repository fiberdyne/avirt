// SPDX-License-Identifier: GPL-2.0
/*
 * AVIRT - ALSA Virtual Soundcard
 *
 * Copyright (c) 2010-2018 Fiberdyne Systems Pty Ltd
 * 
 * utils.h - Some useful macros/utils for AVIRT
 */

#ifndef __SOUND_AVIRT_UTILS_H
#define __SOUND_AVIRT_UTILS_H

#include <linux/slab.h>

#define PRINT_ERR(errno, errmsg)                                               \
	pr_err("[%s]:[ERRNO:%d]: %s \n", __func__, errno, (errmsg));

#define CHK_ERR(errno)                                                         \
	do {                                                                   \
		if ((errno) < 0)                                               \
			return (errno);                                        \
	} while (0)

#define CHK_ERR_V(errno, errmsg, ...)                                          \
	do {                                                                   \
		if ((errno) < 0) {                                             \
			PRINT_ERR((errno), (errmsg), ##__VA_ARGS__)            \
			return (errno);                                        \
		}                                                              \
	} while (0)

#define CHK_NULL(x, errno)                                                     \
	do {                                                                   \
		if (!(x))                                                      \
			return errno;                                          \
	} while (0)

#define CHK_NULL_V(x, errno, errmsg, ...)                                      \
	do {                                                                   \
		if (!(x)) {                                                    \
			char *errmsg_done =                                    \
				kasprintf(GFP_KERNEL, errmsg, ##__VA_ARGS__);  \
			PRINT_ERR(EFAULT, errmsg_done);                        \
			kfree(errmsg_done);                                    \
			return errno;                                          \
		}                                                              \
	} while (0)

#endif /* __SOUND_AVIRT_UTILS_H */
