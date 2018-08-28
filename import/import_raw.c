/*
 *  import_raw.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *
 *  This file is part of transcode, a video stream  processing tool
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

#define MOD_NAME    "import_raw.so"
#define MOD_VERSION "v0.3.3 (2008-11-23)"
#define MOD_CODEC   "(video) RGB/YUV | (audio) PCM"

#include "transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_YUV|TC_CAP_PCM|TC_CAP_YUV422;

#define MOD_PRE raw
#include "import_def.h"

#define MAX_BUF 1024
static char import_cmd_buf[MAX_BUF];
static int codec;

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    char cat_buf[1024];
    char *co = NULL;

    if (param->flag == TC_AUDIO) {
        //directory mode?
        if (tc_file_check(vob->audio_in_file) == 1) {
            tc_snprintf(cat_buf, sizeof(cat_buf), "tccat -a");
        } else {
            if (vob->im_a_string) {
                tc_snprintf(cat_buf, sizeof(cat_buf), "tcextract -x pcm %s", vob->im_a_string);
            } else {
                tc_snprintf(cat_buf, sizeof(cat_buf), "tcextract -x pcm");
            }
        }

        if (tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                        "%s -i \"%s\" -d %d | tcextract -a %d -x pcm -d %d -t raw",
                        cat_buf, vob->audio_in_file, vob->verbose, vob->a_track, vob->verbose) < 0) {
            tc_log_perror(MOD_NAME, "cmd buffer overflow");
            return TC_ERROR;
        }

	    if (verbose_flag)
            tc_log_info(MOD_NAME, "%s", import_cmd_buf);

        param->fd = popen(import_cmd_buf, "r");
        if (param->fd == NULL) {
            tc_log_perror(MOD_NAME, "popen audio stream");
            return TC_ERROR;
        }

        return TC_OK;
    }

    if (param->flag == TC_VIDEO) {
        int ret = 0;

        codec = vob->im_v_codec;

        //directory mode?
        if (tc_file_check(vob->video_in_file) == 1) {
            tc_snprintf(cat_buf, sizeof(cat_buf), "tccat");
            co = "";
        } else {
            if (vob->im_v_string) {
                tc_snprintf(cat_buf, sizeof(cat_buf), "tcextract %s", vob->im_v_string);
            } else {
                tc_snprintf(cat_buf, sizeof(cat_buf), "tcextract");
            }

            switch (codec) {
              case CODEC_RGB:
                co = "-x rgb";
                break;
              case CODEC_YUV422:
                co = "-x yuv422p";
                break;
              case CODEC_YUV: /* fallthrough */
              default:
                co = "-x yuv420p";
                break;
	        }
        }


        switch (codec) {
          case CODEC_RGB:
            ret = tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                              "%s -i \"%s\" -d %d %s | tcextract -a %d -x rgb -d %d",
                              cat_buf, vob->video_in_file, vob->verbose, co, vob->v_track, vob->verbose);
            break;
          
          case CODEC_YUV422:
            ret = tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                              "%s -i \"%s\" -d %d %s | tcextract -a %d -x yuv422p -d %d",
                              cat_buf, vob->video_in_file, vob->verbose, co, vob->v_track, vob->verbose);
	        break;

          case CODEC_YUV: /* fallthrough */
          default:
            ret = tc_snprintf(import_cmd_buf, sizeof(import_cmd_buf),
                              "%s -i \"%s\" -d %d %s | tcextract -a %d -x yuv420p -d %d",
                              cat_buf, vob->video_in_file, vob->verbose, co, vob->v_track, vob->verbose);
            break;
        }

        if (ret  < 0) {
            tc_log_perror(MOD_NAME, "cmd buffer overflow");
            return TC_ERROR;
	    }

        if (verbose_flag)
            tc_log_info(MOD_NAME, "%s", import_cmd_buf);

        param->fd = popen(import_cmd_buf, "r");
        if (param->fd == NULL) {
            tc_log_perror(MOD_NAME, "popen video stream");
            return TC_ERROR;
        }

        return TC_OK;
    }

    return TC_ERROR;
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{   
    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->fd != NULL)
        pclose(param->fd);

    return TC_OK;
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

