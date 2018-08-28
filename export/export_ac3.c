/*
 *  export_ac3.c
 *
 *  Copyright (C) Daniel Pittman, 2003, based on export_ogg.c which was:
 *  Copyright (C) Tilmann Bitterberg, July 2002
 *
 *  This file is part of transcode, a video stream processing tool
 *
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define MOD_NAME    "export_ac3.so"
#define MOD_VERSION "v0.1 (2003-02-26)"
#define MOD_CODEC   "(video) null | (audio) ac3"

#include "transcode.h"

#include <stdio.h>
#include <unistd.h>

static int   verbose_flag=TC_QUIET;
static int   capability_flag=TC_CAP_PCM;

#define MOD_PRE ac3
#include "export_def.h"
static FILE *pFile = NULL;

static inline int p_write (char *buf, size_t len)
{
    size_t n  = 0;
    size_t r  = 0;
    int    fd = fileno (pFile);

    while (r < len)
    {
        if ((n = write (fd, buf + r, len - r)) < 0)
	    return n;

        r += n;
    }

    return r;
}

/* ------------------------------------------------------------
 *
 * open codec
 *
 * ------------------------------------------------------------*/

MOD_open
{
    int result;

    /* check for ffmpeg */
    if (tc_test_program("ffmpeg") != 0) return (TC_EXPORT_ERROR);

    if (param->flag == TC_AUDIO) {
	char buf [PATH_MAX];

	if (vob->mp3bitrate == 0) {
            tc_log_warn (MOD_NAME, "Please set the export audio bitrate");
            return(TC_EXPORT_ERROR);
        }

	if (vob->mp3frequency == 0) {
            tc_log_warn (MOD_NAME, "Please set the export audio sample rate");
            return(TC_EXPORT_ERROR);
        }

	tc_log_warn(MOD_NAME, "*** This module is non-optimal ***");
	tc_log_warn(MOD_NAME, "*** Use -N 0x2000 instead of -y ...,ac3 (faster) ***");

        result = tc_snprintf (buf, PATH_MAX,
                              "ffmpeg -y -f s%dle -ac %d -ar %d -i - -ab %dk -acodec ac3 %s%s",
                              vob->dm_bits,
                              vob->dm_chan,
                              vob->mp3frequency,
                              vob->mp3bitrate,
                              vob->audio_out_file,
                              vob->verbose > 1 ? "" : " >/dev/null 2>&1");

	if (result < 0)
            return(TC_EXPORT_ERROR);

        if (verbose > 0)
	    tc_log_info(MOD_NAME, "%s", buf);

	if ((pFile = popen (buf, "w")) == NULL)
	    return(TC_EXPORT_ERROR);

	return(TC_EXPORT_OK);

    }
    if (param->flag == TC_VIDEO)
	return(TC_EXPORT_OK);

    // invalid flag
    return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
    if(param->flag == TC_AUDIO)
    {
        return(0);
    }

    if (param->flag == TC_VIDEO)
	return(0);

    // invalid flag
    return(TC_EXPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{
    if(param->flag == TC_AUDIO)
    {
        if (p_write (param->buffer, param->size) != param->size)
        {
            tc_log_perror(MOD_NAME, "write audio frame");
            return(TC_EXPORT_ERROR);
        }
        return (0);
    }

    if (param->flag == TC_VIDEO)
        return(0);

    // invalid flag
    return(TC_EXPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * stop codec
 *
 * ------------------------------------------------------------*/

MOD_stop
{
    if (param->flag == TC_VIDEO)
        return (0);

    if (param->flag == TC_AUDIO)
	return (0);

    return(TC_EXPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * close codec
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->flag == TC_VIDEO)
	return (0);

    if (param->flag == TC_AUDIO)
    {
        if (pFile)
	  pclose (pFile);

	pFile = NULL;

        return(0);
    }

    return (TC_EXPORT_ERROR);
}

/* vim: sw=4
 */
