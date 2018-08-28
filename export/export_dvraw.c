/*
 *  export_dvraw.c
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

#define MOD_NAME    "export_dvraw.so"
#define MOD_VERSION "v0.4.1 (2007-08-17)"
#define MOD_CODEC   "(video) Digital Video | (audio) PCM"

#include "transcode.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"
#include "libtcvideo/tcvideo.h"

#include <stdio.h>
#include <stdlib.h>
#include <libdv/dv.h>

static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV|TC_CAP_VID|TC_CAP_YUV422;

#define MOD_PRE dvraw
#include "export_def.h"

static int fd = -1;

/* only 2 channesl supported */
#define MAX_CHANNELS 2
static int16_t *audio_bufs[MAX_CHANNELS] = { NULL, NULL };

static uint8_t *target = NULL, *vbuf = NULL;

static dv_encoder_t *encoder = NULL;
static uint8_t *pixels[3] = { NULL, NULL, NULL }, *tmp_buf = NULL;
static TCVHandle tcvhandle;

static int frame_size = 0, format = 0;
static int pass_through = 0;

static int chans = 0, rate = 0;
static int dv_yuy2_mode = 0;
static int dv_uyvy_mode = 0;
static int is_PAL = 0;

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
    int i;

    if (param->flag == TC_VIDEO) {
        is_PAL = (vob->ex_v_height == PAL_H);
        target = tc_bufalloc(TC_FRAME_DV_PAL);
        vbuf   = tc_bufalloc(PAL_W * PAL_H * 3);

        tcvhandle = tcv_init();

        if (vob->dv_yuy2_mode == 1) {
            tmp_buf = tc_bufalloc(PAL_W * PAL_H * 2); //max frame
            dv_yuy2_mode = 1;
        }

        if (vob->im_v_codec == CODEC_YUV422) {
            tmp_buf = tc_bufalloc(PAL_W * PAL_H * 2); //max frame
            dv_uyvy_mode = 1;
        }

        encoder = dv_encoder_new(FALSE, FALSE, FALSE);
        return TC_OK;
    }

    if (param->flag == TC_AUDIO) {
        // tmp audio buffer
        for ( i = 0; i < MAX_CHANNELS; i++) {
            audio_bufs[i] = tc_malloc(DV_AUDIO_MAX_SAMPLES * sizeof(int16_t));
            if (!(audio_bufs[i])) {
                tc_log_error(MOD_NAME, "out of memory");
                return TC_ERROR;
            }
        }
        return TC_OK;
    }

    return(TC_ERROR);
}

/* ------------------------------------------------------------
 *
 * open outputfile
 *
 * ------------------------------------------------------------*/

MOD_open
{
    int bytealignment = 0, bytespersecond = 0, bytesperframe = 0;

    if (param->flag == TC_VIDEO) {
        fd = open(vob->video_out_file, O_RDWR|O_CREAT|O_TRUNC,
		          S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
        if (fd < 0) {
            tc_log_perror(MOD_NAME, "open file");
            return TC_ERROR;
        }

        switch (vob->im_v_codec) {
          case CODEC_RGB:
            format = 0;
            if (verbose >= TC_DEBUG)
                tc_log_info(MOD_NAME, "raw format is RGB");
            break;
          case CODEC_YUV:
            format = 1;
            if (verbose >= TC_DEBUG)
                tc_log_info(MOD_NAME, "raw format is YUV420P");
            break;
          case CODEC_YUV422:
            format = 2;
            if (verbose >= TC_DEBUG)
                tc_log_info(MOD_NAME, "raw format is YUV422");
            break;
          case CODEC_RAW:
          case CODEC_RAW_YUV:
            format = 1;
            pass_through = 1;
            break;
          default:
            tc_log_warn(MOD_NAME, "codec not supported");
            return TC_ERROR;
        }

        frame_size = (is_PAL) ?TC_FRAME_DV_PAL :TC_FRAME_DV_NTSC;

        if (verbose >= TC_DEBUG)
            tc_log_info(MOD_NAME, "encoding to %s DV",
                        (is_PAL) ?"PAL" :"NTSC");

        // Store aspect ratio - ex_asr uses the value 3 for 16x9 (XXX: tricky)
        encoder->is16x9 = (((vob->ex_asr < 0) ?vob->im_asr :vob->ex_asr) == 3);
        encoder->isPAL = is_PAL;
        encoder->vlc_encode_passes = 3;
        encoder->static_qno = 0;
        if (vob->ex_v_string != NULL)
            if (optstr_get(vob->ex_v_string, "qno", "%d", &encoder->static_qno) == 1)
                tc_log_info(MOD_NAME, "using quantisation: %d", encoder->static_qno);
        encoder->force_dct = DV_DCT_AUTO;

        return TC_OK;
    }

    if (param->flag == TC_AUDIO) {
        if (!encoder) {
            tc_log_warn(MOD_NAME, "-y XXX,dvraw is not possible without the video");
            tc_log_warn(MOD_NAME, "export module also being dvraw");
            return TC_ERROR;
        }
        chans = vob->dm_chan;
        //re-sampling only with -J resample possible
        rate = vob->a_rate;

        bytealignment = (chans == 2) ?4 :2;
        bytespersecond = rate * bytealignment;
        bytesperframe = bytespersecond/(is_PAL ?25 :30);

        if(verbose >= TC_DEBUG)
            tc_log_info(MOD_NAME, "audio: CH=%d, f=%d, balign=%d, bps=%d, bpf=%d",
		                chans, rate, bytealignment, bytespersecond, bytesperframe);

        return TC_OK;
    }
    return TC_ERROR;
}

/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{
    int i;

    if (param->flag == TC_VIDEO) {
        if (pass_through) {
            ac_memcpy(target, param->buffer, frame_size);
        } else {
            ac_memcpy(vbuf, param->buffer, param->size);
        }

        return TC_OK;
    }

    if (param->flag == TC_AUDIO) {
        int16_t *abufs[2] = { audio_bufs[0], audio_bufs[1] }; /* working copies */
        time_t now = time(NULL);
        int achans = chans;

        if (!pass_through) {
            if (dv_uyvy_mode) {
                tcv_convert(tcvhandle,
                		    vbuf, tmp_buf, PAL_W, (encoder->isPAL) ? PAL_H : NTSC_H,
		                    (format==2) ? IMG_YUV422P : IMG_YUV420P, IMG_UYVY);
                pixels[0] = pixels[1] = pixels[2] = tmp_buf;
            } else if (dv_yuy2_mode) {
            	tcv_convert(tcvhandle,
		                    vbuf, tmp_buf, PAL_W, (encoder->isPAL) ? PAL_H : NTSC_H,
                		    (format==2) ? IMG_YUV422P : IMG_YUV420P, IMG_YUY2);
            	pixels[0] = pixels[1] = pixels[2] = tmp_buf;
            } else {
                pixels[0] = vbuf;
                if (encoder->isPAL) {
                    pixels[1] = pixels[0] + PAL_W*PAL_H;
                    pixels[2] = pixels[1] + (PAL_W/2)*(format==2?PAL_H:PAL_H/2);
                } else {
                    pixels[1] = pixels[0] + NTSC_W*NTSC_H;
                    pixels[2] = pixels[1] + (NTSC_W/2)*(format==2?NTSC_H:NTSC_H/2);
                }
            }

            dv_encode_full_frame(encoder, pixels, (format)?e_dv_color_yuv:e_dv_color_rgb, target);
        } //no pass-through
        /* 
         * samples * channels * bits = size;
         * so
         * samples = size / (channels * bits)
         */
        encoder->samples_this_frame = param->size / (chans * sizeof(int16_t));
        dv_encode_metadata(target, encoder->isPAL, encoder->is16x9, &now, 0);
        dv_encode_timecode(target, encoder->isPAL, 0);

#ifdef WORDS_BIGENDIAN
        for (i=0; i<param->size; i+=2) {
	        uint8_t tmp = param->buffer[i];
            param->buffer[i] = param->buffer[i+1];
            param->buffer[i+1] = tmp;
        }
#endif

        // Although dv_encode_full_audio supports 4 channels, the internal
        // PCM data (param->buffer) is only carrying 2 channels so only deal
        // with 1 or 2 channel audio.
        // Work around apparent bug in dv_encode_full_audio when chans == 1
        // by putting silence in 2nd channel and calling with chans = 2
        if (chans == 1) {
            abufs[0] = (int16_t *)param->buffer; /* avoid ac_memcpy */
            memset(abufs[1], 0, DV_AUDIO_MAX_SAMPLES * 2);
            achans = 2;
        } else {
            // assume 2 channel, demultiplex for libdv API
            for(i = 0; i < param->size/4; i++) { // XXX magic number
                abufs[0][i] = ((int16_t *)param->buffer)[i * 2    ];
                abufs[1][i] = ((int16_t *)param->buffer)[i * 2 + 1];
            }
        }
        dv_encode_full_audio(encoder, abufs, achans, rate, target);

        if (tc_pwrite(fd, target, frame_size) != frame_size) {
            tc_log_perror(MOD_NAME, "write frame");
            return TC_ERROR;
        }

        return TC_OK;
    }

    return TC_ERROR;
}

/* ------------------------------------------------------------
 *
 * stop encoder
 *
 * ------------------------------------------------------------*/

MOD_stop
{
    int i;

    if (param->flag == TC_VIDEO) {
        dv_encoder_free(encoder);
        tcv_free(tcvhandle);
        return TC_OK;
    }

    if (param->flag == TC_AUDIO) {
        for(i = 0; i < MAX_CHANNELS; i++)
            tc_free(audio_bufs[i]);
        return TC_OK;
    }

    return TC_ERROR;
}

/* ------------------------------------------------------------
 *
 * close outputfiles
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if(param->flag == TC_VIDEO) {
        close(fd);
        return TC_OK;
    }

    if (param->flag == TC_AUDIO)
        return TC_OK;

    return TC_ERROR;
}


