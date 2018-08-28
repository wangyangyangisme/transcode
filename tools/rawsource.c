/*
 * rawsource.c -- (almost) raw source reader interface for encoder
 *                expect WAV audio and YUV4MPEG2 video
 * (C) 2006-2010 - Francesco Romani <fromani at gmail dot com>
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


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "framebuffer.h"
#include "dl_loader.h"
#include "rawsource.h"
#include "libtc/libtc.h"
#include "libtc/tcframes.h"
#include "avilib/wavlib.h"
#include "rawsource.h"

#define RAWSOURCE_IM_MOD    "yuv4mpeg"

static int rawsource_read_video(TCEncoderBuffer *buf, vob_t *vob);
static int rawsource_read_audio(TCEncoderBuffer *buf, vob_t *vob);
static void rawsource_dummy(TCEncoderBuffer *buf);


typedef struct tcrawsource_ {
    void *im_handle;
    transfer_t im_para;

    int eof_flag;
    int sources;

    vframe_list_t *vframe;
    aframe_list_t *aframe;
    int acount;
} TCFileSource;

static TCFileSource rawsource = {
    .im_handle = NULL,

    .eof_flag = TC_FALSE,
    .sources = 0,

    .vframe = NULL,
    .aframe = NULL,
    .acount = 0,
};

static TCEncoderBuffer raw_buffer = {
    .frame_id = 0,

    .vptr = NULL,
    .aptr = NULL,

    .acquire_video_frame = rawsource_read_video,
    .acquire_audio_frame = rawsource_read_audio,
    .dispose_video_frame = rawsource_dummy,
    .dispose_audio_frame = rawsource_dummy,
};
TCEncoderBuffer *tc_rawsource_buffer = NULL;

static int rawsource_read_video(TCEncoderBuffer *buf, vob_t *vob)
{
    int ret;

    if (!buf) {
        /* paranoia */
        return -1;
    }
    if (vob->im_v_size > rawsource.vframe->video_size) {
        /* paranoia */
        tc_log_error(__FILE__, "video buffer too small"
                               " (this should'nt happen)");
        return -1;
    }

    rawsource.im_para.buffer  = rawsource.vframe->video_buf;
    rawsource.im_para.buffer2 = NULL;
    rawsource.im_para.size    = vob->im_v_size;
    rawsource.im_para.flag    = TC_VIDEO;

    ret = tcv_import(TC_IMPORT_DECODE, &rawsource.im_para, vob);
    if (ret != TC_IMPORT_OK) {
        /* read failed */
        rawsource.eof_flag = TC_TRUE;
        return -1;
    }
    rawsource.vframe->video_size = rawsource.im_para.size;
    rawsource.vframe->attributes = rawsource.im_para.attributes;

    raw_buffer.vptr = rawsource.vframe;
    raw_buffer.frame_id++;
    return 0;
}

static int rawsource_read_audio(TCEncoderBuffer *buf, vob_t *vob)
{
    int ret = 0;
    int abytes = vob->im_a_size;

    // audio adjustment for non PAL frame rates:

    if (rawsource.acount != 0 && rawsource.acount % TC_LEAP_FRAME == 0) {
        abytes += vob->a_leap_bytes;
    }

    if (!buf) {
        /* paranoia */
        return -1;
    }
    if (abytes > rawsource.aframe->audio_size) {
        /* paranoia */
        tc_log_error(__FILE__, "audio buffer too small"
                               " (this should'nt happen)");
        return -1;
    }

    rawsource.im_para.buffer  = rawsource.aframe->audio_buf;
    rawsource.im_para.buffer2 = NULL;
    rawsource.im_para.size    = abytes;
    rawsource.im_para.flag    = TC_AUDIO;

    ret = tca_import(TC_IMPORT_DECODE, &rawsource.im_para, vob);
    if (ret != TC_IMPORT_OK) {
        /* read failed */
        rawsource.eof_flag = TC_TRUE;
        return -1;
    }
    rawsource.acount++;
    rawsource.aframe->audio_size = rawsource.im_para.size;
    rawsource.aframe->attributes = rawsource.im_para.attributes;

    raw_buffer.aptr = rawsource.aframe;
    return 0;
}

static void rawsource_dummy(TCEncoderBuffer *buf)
{
    return;
}

int tc_rawsource_open(vob_t *vob)
{
    int ret = 0;
    int num_sources = 0;
    double samples;

    if (!vob) {
        goto vframe_failed;
    }

    rawsource.vframe = tc_new_video_frame(vob->im_v_width, vob->im_v_height,
                                          vob->im_v_codec, TC_TRUE);
    if (!rawsource.vframe) {
        tc_log_error(__FILE__, "can't allocate video frame buffer");
        goto vframe_failed;
    }
    samples = TC_AUDIO_SAMPLES_IN_FRAME(vob->a_rate, vob->ex_fps);
    rawsource.aframe = tc_new_audio_frame(samples, vob->a_chan, vob->a_bits);
    if (!rawsource.aframe) {
        tc_log_error(__FILE__, "can't allocate audio frame buffer");
        goto aframe_failed;
    }

	rawsource.im_handle = load_module(RAWSOURCE_IM_MOD, TC_IMPORT|TC_AUDIO|TC_VIDEO);
	if (!rawsource.im_handle) {
        tc_log_error(__FILE__, "can't load import module");
        goto load_failed;
    }

    /* hello, module! */
	memset(&rawsource.im_para, 0, sizeof(transfer_t));
	rawsource.im_para.flag = vob->verbose;
	tca_import(TC_IMPORT_NAME, &rawsource.im_para, NULL);

    memset(&rawsource.im_para, 0, sizeof(transfer_t));
	rawsource.im_para.flag = vob->verbose;
	tcv_import(TC_IMPORT_NAME, &rawsource.im_para, NULL);

    /* open sources */
	memset(&rawsource.im_para, 0, sizeof(transfer_t));
    rawsource.im_para.flag = TC_AUDIO;
    ret = tca_import(TC_IMPORT_OPEN, &rawsource.im_para, vob);
    if (TC_IMPORT_OK == ret) {
        num_sources++;
        rawsource.sources |= TC_AUDIO;
    }

	memset(&rawsource.im_para, 0, sizeof(transfer_t));
    rawsource.im_para.flag = TC_VIDEO;
    ret = tcv_import(TC_IMPORT_OPEN, &rawsource.im_para, vob);
    if (TC_IMPORT_OK == ret) {
        num_sources++;
        rawsource.sources |= TC_VIDEO;
    }

    if (num_sources > 0) {
        tc_rawsource_buffer = &raw_buffer;
    }
    return num_sources;

load_failed:
    tc_del_audio_frame(rawsource.aframe);
aframe_failed:
    tc_del_video_frame(rawsource.vframe);
vframe_failed:
    return -1;
}

static void tc_rawsource_free(void)
{
    if (rawsource.vframe != NULL) {
        tc_del_video_frame(rawsource.vframe);
        rawsource.vframe = NULL;
    }
    if (rawsource.aframe != NULL) {
        tc_del_audio_frame(rawsource.aframe);
        rawsource.aframe = NULL;
    }
}

/* errors not fatal, but notified */
int tc_rawsource_close(void)
{
    tc_rawsource_free();

    if (rawsource.im_handle != NULL) {
        int ret = 0;

	    memset(&rawsource.im_para, 0, sizeof(transfer_t));
        rawsource.im_para.flag = TC_VIDEO;
        ret = tcv_import(TC_IMPORT_CLOSE, &rawsource.im_para, NULL);
        if(ret != TC_IMPORT_OK) {
            tc_log_warn(__FILE__, "video import module error: CLOSE failed");
        } else {
            rawsource.sources &= ~TC_VIDEO;
        }

        memset(&rawsource.im_para, 0, sizeof(transfer_t));
        rawsource.im_para.flag = TC_AUDIO;
        ret = tca_import(TC_IMPORT_CLOSE, &rawsource.im_para, NULL);
        if(ret != TC_IMPORT_OK) {
            tc_log_warn(__FILE__, "audio import module error: CLOSE failed");
        } else {
            rawsource.sources &= ~TC_AUDIO;
        }

        if (!rawsource.sources) {
            unload_module(rawsource.im_handle);
            rawsource.im_handle = NULL;
        }
    }
    return 0;
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
