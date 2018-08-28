/*
 * tcavcodec.h -- transcode's support macros and tools for easier
 *                libavcodec/libavformat/libavutil usage
 * (C) 2007-2010 - Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef TCAVCODEC_H
#define TCAVCODEC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>

#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/*************************************************************************/

/*
 * libavcodec lock. Used for serializing initialization/open of library.
 * Other libavcodec routines (avcodec_{encode,decode}_* should be thread
 * safe (as ffmpeg crew said) if each thread uses it;s own AVCodecContext,
 * as we do.
 */
extern pthread_mutex_t tc_libavcodec_mutex;

/*
 *  libavcodec locking goodies. It's preferred and encouraged  to use
 *  macros below, but accessing libavcodec mutex will work too.
 */
#define TC_LOCK_LIBAVCODEC	(pthread_mutex_lock(&tc_libavcodec_mutex))
#define TC_UNLOCK_LIBAVCODEC	(pthread_mutex_unlock(&tc_libavcodec_mutex))


#define TC_INIT_LIBAVCODEC do { \
    TC_LOCK_LIBAVCODEC; \
    avcodec_init(); \
    avcodec_register_all(); \
    TC_UNLOCK_LIBAVCODEC; \
} while (0)


#endif  /* TCAVCODEC_H */

