/*
 *  export_wav.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#define MOD_NAME    "export_wav.so"
#define MOD_VERSION "v0.3.0 (2006-03-16)"
#define MOD_CODEC   "(audio) WAVE PCM"

#include "transcode.h"
#include "avilib/wavlib.h"

#include <stdio.h>
#include <stdlib.h>

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV|TC_CAP_VID;

#define MOD_PRE wav
#include "export_def.h"

static WAV wav = NULL;

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    if (param->flag == TC_AUDIO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * open outputfile
 *
 * ------------------------------------------------------------*/

MOD_open
{
    if (param->flag == TC_AUDIO) {
        WAVError err;
        int rate;

        wav = wav_open(vob->audio_out_file, WAV_WRITE, &err);
        if (wav == NULL) {
            tc_log_error(MOD_NAME, "open file: %s", wav_strerror(err));
            return TC_EXPORT_ERROR;
        }

        rate = (vob->mp3frequency != 0) ?vob->mp3frequency :vob->a_rate;
        wav_set_bits(wav, vob->dm_bits);
        wav_set_rate(wav, rate);
        wav_set_bitrate(wav, vob->dm_chan * rate * vob->dm_bits/8);
        wav_set_channels(wav, vob->dm_chan);

        return TC_EXPORT_OK;
    }

    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{
    if (param->flag == TC_AUDIO) {
        if (wav_write_data(wav, param->buffer, param->size) != param->size) {
            tc_log_error(MOD_NAME, "write audio frame: %s",
                                   wav_strerror(wav_last_error(wav)));
            return TC_EXPORT_ERROR;
        }
        return TC_EXPORT_OK;
    }

    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * close outputfiles
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    if (param->flag == TC_AUDIO) {
        if (wav_close(wav) != 0) {
            tc_log_error(MOD_NAME, "closing file: %s",
                                   wav_strerror(wav_last_error(wav)));
            return TC_EXPORT_ERROR;
        }
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * stop encoder
 *
 * ------------------------------------------------------------*/

MOD_stop
{
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    if (param->flag == TC_AUDIO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

