/*
 *  multiplex_y4m.c -- pack a yuv420p stream in YUV4MPEG2 format
 *                     and/or a pcm stream in WAVE format
 *  (C) 2005-2010 Francesco Romani <fromani at gmail dot com>
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


#include "config.h"

#include "transcode.h"
#include "libtc/optstr.h"
#include "libtc/ratiocodes.h"

#include "libtc/tcmodule-plugin.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#if defined(HAVE_MJPEGTOOLS_INC)
#include "yuv4mpeg.h"
#include "mpegconsts.h"
#else
#include "mjpegtools/yuv4mpeg.h"
#include "mjpegtools/mpegconsts.h"
#endif

#include "avilib/wavlib.h"

#define MOD_NAME    "multiplex_y4m.so"
#define MOD_VERSION "v0.0.1 (2006-03-22)"
#define MOD_CAP     "write YUV4MPEG2 video and WAVE audio streams"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO
    

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE
    


#define YW_VID_EXT "y4m"
#define YW_AUD_EXT "wav"

/* 
 * 'yw_' prefix is used internally to avoid any name clash
 * with mjpegtools's y4m_* routines
 */


/* XXX */
static const char yw_help[] = ""
    "Overview:\n"
    "    this module writes a yuv420p video stream using YUV4MPEG2 format\n"
    "    and/or a pcm stream using WAVE format.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

typedef struct {
    int fd_vid;
    WAV wav;

    y4m_frame_info_t frameinfo;
    y4m_stream_info_t streaminfo;

    int width;
    int height;
    
} YWPrivateData;


static int yw_inspect(TCModuleInstance *self,
                      const char *options, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");
    
    if (optstr_lookup(options, "help")) {
        *value = yw_help;
    }

    return TC_OK;
}

static int yw_open_video(YWPrivateData *pd, const char *filename,
                         vob_t *vob)
{
    int asr, ret;
    y4m_ratio_t framerate;
    y4m_ratio_t asr_rate;

    /* avoid fd loss in case of failed configuration */
    if (pd->fd_vid == -1) {
        pd->fd_vid = open(filename,
                          O_RDWR|O_CREAT|O_TRUNC,
                          S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if (pd->fd_vid == -1) {
            tc_log_error(MOD_NAME, "failed to open video stream file '%s'"
                                   " (reason: %s)", filename,
                                   strerror(errno));
            return TC_ERROR;
        }
    }
    y4m_init_stream_info(&(pd->streaminfo));

    //note: this is the real framerate of the raw stream
    framerate = (vob->ex_frc == 0) ?mpeg_conform_framerate(vob->ex_fps)
                                   :mpeg_framerate(vob->ex_frc);
    if (framerate.n == 0 && framerate.d == 0) {
    	framerate.n = vob->ex_fps * 1000;
	    framerate.d = 1000;
    }
    
    asr = (vob->ex_asr < 0) ?vob->im_asr :vob->ex_asr;
    tc_asr_code_to_ratio(asr, &asr_rate.n, &asr_rate.d); 

    y4m_init_stream_info(&(pd->streaminfo));
    y4m_si_set_framerate(&(pd->streaminfo), framerate);
    if (vob->encode_fields == TC_ENCODE_FIELDS_TOP_FIRST) {
        y4m_si_set_interlace(&(pd->streaminfo), Y4M_ILACE_TOP_FIRST);
    } else if (vob->encode_fields == TC_ENCODE_FIELDS_BOTTOM_FIRST) {
        y4m_si_set_interlace(&(pd->streaminfo), Y4M_ILACE_BOTTOM_FIRST);
    } else if (vob->encode_fields == TC_ENCODE_FIELDS_PROGRESSIVE) {
        y4m_si_set_interlace(&(pd->streaminfo), Y4M_ILACE_NONE);
    }
    /* XXX */
    y4m_si_set_sampleaspect(&(pd->streaminfo),
                            y4m_guess_sar(pd->width,
                                          pd->height,
                                          asr_rate));
    y4m_si_set_height(&(pd->streaminfo), pd->height);
    y4m_si_set_width(&(pd->streaminfo), pd->width);
    /* Y4M_CHROMA_420JPEG     4:2:0, H/V centered, for JPEG/MPEG-1 */
    /* Y4M_CHROMA_420MPEG2   4:2:0, H cosited, for MPEG-2         */
    /* Y4M_CHROMA_420PALDV   4:2:0, alternating Cb/Cr, for PAL-DV */
    y4m_si_set_chroma(&(pd->streaminfo), Y4M_CHROMA_420JPEG); // XXX
    
    ret = y4m_write_stream_header(pd->fd_vid, &(pd->streaminfo));
    if (ret != Y4M_OK) {
        tc_log_warn(MOD_NAME, "failed to write video YUV4MPEG2 header: %s",
                              y4m_strerr(ret));
        return TC_ERROR;
    }
    return TC_OK;
}

static int yw_open_audio(YWPrivateData *pd, const char *filename,
                         vob_t *vob)
{
    WAVError err;
    int rate;

    pd->wav = wav_open(filename, WAV_WRITE, &err);
    if (!pd->wav) {
        tc_log_error(MOD_NAME, "failed to open audio stream file '%s'"
                               " (reason: %s)", filename,
                               wav_strerror(err));
        return TC_ERROR;
    }

    rate = (vob->mp3frequency != 0) ?vob->mp3frequency :vob->a_rate;
    wav_set_bits(pd->wav, vob->dm_bits);
    wav_set_rate(pd->wav, rate);
    wav_set_bitrate(pd->wav, vob->dm_chan * rate * vob->dm_bits/8);
    wav_set_channels(pd->wav, vob->dm_chan);

    return TC_OK;
}

static int yw_configure(TCModuleInstance *self,
                         const char *options, vob_t *vob)
{
    char vid_name[PATH_MAX];
    char aud_name[PATH_MAX];
    YWPrivateData *pd = NULL;
    int ret;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    if (vob->audio_out_file == NULL
      || !strcmp(vob->audio_out_file, "/dev/null")) {
        /* use affine names */
        tc_snprintf(vid_name, PATH_MAX, "%s.%s",
                    vob->video_out_file, YW_VID_EXT);
        tc_snprintf(aud_name, PATH_MAX, "%s.%s",
                    vob->video_out_file, YW_AUD_EXT);
    } else {
        /* copy names verbatim */
        strlcpy(vid_name, vob->video_out_file, PATH_MAX);
        strlcpy(aud_name, vob->audio_out_file, PATH_MAX);
    }
    
    pd->width = vob->ex_v_width;
    pd->height = vob->ex_v_height;
    
    ret = yw_open_video(pd, vid_name, vob);
    if (ret != TC_OK) {
        return ret;
    }
    ret = yw_open_audio(pd, aud_name, vob);
    if (ret != TC_OK) {
        return ret;
    }
    if (vob->verbose >= TC_DEBUG) {
        tc_log_info(MOD_NAME, "video output: %s (%s)",
                    vid_name, (pd->fd_vid == -1) ?"FAILED" :"OK");
        tc_log_info(MOD_NAME, "audio output: %s (%s)",
                    aud_name, (pd->wav == NULL) ?"FAILED" :"OK");
    }
    return TC_OK;
}

static int yw_stop(TCModuleInstance *self)
{
    YWPrivateData *pd = NULL;
    int verr, aerr;

    TC_MODULE_SELF_CHECK(self, "stop");

    pd = self->userdata;

    if (pd->fd_vid != -1) {
        verr = close(pd->fd_vid);
        if (verr) {
            tc_log_error(MOD_NAME, "closing video file: %s",
                                   strerror(errno));
            return TC_ERROR;
        }
        y4m_fini_frame_info(&pd->frameinfo);
        y4m_fini_stream_info(&(pd->streaminfo));
   
        pd->fd_vid = -1;
    }

    if (pd->wav != NULL) {
        aerr = wav_close(pd->wav);
        if (aerr != 0) {
            tc_log_error(MOD_NAME, "closing audio file: %s",
                                   wav_strerror(wav_last_error(pd->wav)));
            return TC_ERROR;
        }
        pd->wav = NULL;
    }

    return TC_OK;
}

static int yw_multiplex(TCModuleInstance *self,
                         vframe_list_t *vframe, aframe_list_t *aframe)
{
    ssize_t w_aud = 0, w_vid = 0;

    YWPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "multiplex");

    pd = self->userdata;

    if (vframe != NULL && vframe->video_len > 0) {
        uint8_t *planes[3];
        int ret = 0;
        y4m_init_frame_info(&pd->frameinfo);
        YUV_INIT_PLANES(planes, vframe->video_buf, IMG_YUV420P,
                        pd->width, pd->height);
        
        ret = y4m_write_frame(pd->fd_vid, &(pd->streaminfo),
                                 &pd->frameinfo, planes);
        if (ret != Y4M_OK) {
            tc_log_warn(MOD_NAME, "error while writing video frame: %s",
                                  y4m_strerr(ret));
            return TC_ERROR;
        }
        w_vid = vframe->video_len;
    }

    if (aframe != NULL && aframe->audio_len > 0) {
        w_aud = wav_write_data(pd->wav, aframe->audio_buf, aframe->audio_len);
        if (w_aud != aframe->audio_len) {
            tc_log_warn(MOD_NAME, "error while writing audio frame: %s",
                                  wav_strerror(wav_last_error(pd->wav)));
            return TC_ERROR;
        }
    }

    return (int)(w_vid + w_aud);
}

static int yw_init(TCModuleInstance *self, uint32_t features)
{
    YWPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(YWPrivateData));
    if (pd == NULL) {
        return TC_ERROR;
    }

    pd->width = 0;
    pd->height = 0;
    pd->fd_vid = -1;
    pd->wav = NULL;
    y4m_init_stream_info(&(pd->streaminfo));
    /* frameinfo will be initialized at each multiplex call  */

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }

    self->userdata = pd;
    return TC_OK;
}

static int yw_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    yw_stop(self);

    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}


/*************************************************************************/

static const TCCodecID yw_codecs_in[] = { TC_CODEC_YUV420P, TC_CODEC_PCM,
                                     TC_CODEC_ERROR };

/* a multiplexor is at the end of pipeline */
static const TCCodecID yw_codecs_out[] = { TC_CODEC_ERROR };
static const TCFormatID yw_formats_in[] = { TC_FORMAT_ERROR };
static const TCFormatID yw_formats_out[] = { 
    TC_FORMAT_YUV4MPEG, TC_FORMAT_WAV, TC_FORMAT_ERROR
};

static const TCModuleInfo yw_info = {
    .features    = MOD_FEATURES,
    .flags       = MOD_FLAGS,
    .name        = MOD_NAME,
    .version     = MOD_VERSION,
    .description = MOD_CAP,
    .codecs_in   = yw_codecs_in,
    .codecs_out  = yw_codecs_out,
    .formats_in  = yw_formats_in,
    .formats_out = yw_formats_out
};

static const TCModuleClass yw_class = {
    TC_MODULE_CLASS_HEAD(yw),

    .init         = yw_init,
    .fini         = yw_fini,
    .configure    = yw_configure,
    .stop         = yw_stop,
    .inspect      = yw_inspect,

    .multiplex    = yw_multiplex,
};

TC_MODULE_ENTRY_POINT(yw);

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

