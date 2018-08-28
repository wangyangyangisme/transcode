/*
 *  export_ogg.c
 *
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

#define MOD_NAME    "export_ogg.so"
#define MOD_VERSION "v0.0.5 (2003-08-31)"
#define MOD_CODEC   "(video) null | (audio) ogg"

#include "transcode.h"

#include <stdio.h>
#include <unistd.h>

static int   verbose_flag=TC_QUIET;
static int   capability_flag=TC_CAP_PCM;

#define MOD_PRE ogg
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
    int ifreq,ofreq;

    /* check for oggenc */
    if (tc_test_program("oggenc") != 0) return (TC_EXPORT_ERROR);

    ofreq = vob->mp3frequency;
    ifreq = vob->a_rate;

    /* default out freq */
    if(ofreq==0)
        ofreq = ifreq;

    if (param->flag == TC_AUDIO) {
	char buf [PATH_MAX];
	char resample [PATH_MAX];

	if (ofreq != ifreq) {
	    result = tc_snprintf(resample, PATH_MAX, "--resample %d -R %d", vob->mp3frequency, vob->a_rate);
	} else {
	    result = tc_snprintf(resample, PATH_MAX, "-R %d", vob->a_rate);
        }
	if (result < 0) {
	    tc_log_perror(MOD_NAME, "command buffer overflow");
	    return(TC_EXPORT_ERROR);
	}

	if (!strcmp(vob->video_out_file, vob->audio_out_file)) {
	    tc_log_info(MOD_NAME, "Writing audio to \"/dev/null\" (no -m option)");
	}
	if (vob->mp3bitrate == 0)
	    result = tc_snprintf (buf, PATH_MAX, "oggenc -r -B %d -C %d -q %.2f %s -Q -o \"%s\" %s -",
		vob->dm_bits,
		vob->dm_chan,
		vob->mp3quality,
		resample,
		vob->audio_out_file?vob->audio_out_file:"/dev/null",
		(vob->ex_a_string?vob->ex_a_string:""));
	else
	    result = tc_snprintf (buf, PATH_MAX, "oggenc -r -B %d -C %d -b %d %s -Q -o \"%s\" %s -",
		vob->dm_bits,
		vob->dm_chan,
		vob->mp3bitrate,
		resample,
		vob->audio_out_file?vob->audio_out_file:"/dev/null",
		(vob->ex_a_string?vob->ex_a_string:""));
	if (result < 0) {
	    tc_log_perror(MOD_NAME, "command buffer overflow");
	    return(TC_EXPORT_ERROR);
	}
	if ((pFile = popen (buf, "w")) == NULL)
	    return(TC_EXPORT_ERROR);

	if (verbose > 0)
	    tc_log_info (MOD_NAME, "%s", buf);

	return(0);

    }
    if (param->flag == TC_VIDEO)
	return(0);

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
    vob_t *vob = tc_get_vob();

    if (param->flag == TC_VIDEO)
	return (0);

    if (param->flag == TC_AUDIO)
    {
        if (pFile)
	  pclose (pFile);

	pFile = NULL;

	if (verbose > 0 && strcmp (vob->audio_out_file, "/dev/null") &&
		strcmp (vob->video_out_file, "/dev/null")!=0) {
	    tc_log_info (MOD_NAME, "Hint: Now merge the files with");
	    tc_log_info (MOD_NAME, "Hint: ogmmerge -o complete.ogg %s %s",
			 vob->video_out_file, vob->audio_out_file );
	}

        return(0);
    }

    return (TC_EXPORT_ERROR);
}

/* vim: sw=4
 */
