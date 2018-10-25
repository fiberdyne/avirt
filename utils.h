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

#define CHK_ERR(errno)                                                         \
	do {                                                                   \
		if ((errno) < 0)                                               \
			return (errno);                                        \
	} while (0)

#define CHK_ERR_V(errno, fmt, args...)                                         \
	do {                                                                   \
		if ((errno) < 0) {                                             \
			DERROR(D_LOGNAME, (fmt), ##args)                       \
			return (errno);                                        \
		}                                                              \
	} while (0)

#define CHK_NULL(x)                                                            \
	do {                                                                   \
		if (!(x))                                                      \
			return -EFAULT;                                        \
	} while (0)

#define CHK_NULL_V(x, fmt, args...)                                            \
	do {                                                                   \
		if (!(x)) {                                                    \
			DERROR(D_LOGNAME, "[ERRNO: %d] " fmt, -EFAULT,         \
			       ##args);                                        \
			return -EFAULT;                                        \
		}                                                              \
	} while (0)

#endif /* __SOUND_AVIRT_UTILS_H */
