/*
 *  multiplex_raw.c -- write a separate plain file for each stream.
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


#include "transcode.h"
#include "libtc/optstr.h"

#include "libtc/tcmodule-plugin.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define MOD_NAME    "multiplex_raw.so"
#define MOD_VERSION "v0.0.3 (2006-03-06)"
#define MOD_CAP     "write each stream in a separate file"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO|TC_MODULE_FEATURE_AUDIO

#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE


#define RAW_VID_EXT "vid"
#define RAW_AUD_EXT "aud"

static const char raw_help[] = ""
    "Overview:\n"
    "    this module simply write audio and video streams in\n"
    "    a separate plain file for each stream.\n"
    "Options:\n"
    "    help    produce module overview and options explanations\n";

typedef struct {
    int fd_aud;
    int fd_vid;
} RawPrivateData;

static int raw_inspect(TCModuleInstance *self,
                       const char *options, const char **value)
{
    TC_MODULE_SELF_CHECK(self, "inspect");
    
    if (optstr_lookup(options, "help")) {
        *value = raw_help;
    }

    return TC_OK;
}

static int raw_configure(TCModuleInstance *self,
                         const char *options, vob_t *vob)
{
    char vid_name[PATH_MAX];
    char aud_name[PATH_MAX];
    RawPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "configure");

    pd = self->userdata;

    // XXX
    if (vob->audio_out_file == NULL
      || !strcmp(vob->audio_out_file, "/dev/null")) {
        /* use affine names */
        tc_snprintf(vid_name, PATH_MAX, "%s.%s",
                    vob->video_out_file, RAW_VID_EXT);
        tc_snprintf(aud_name, PATH_MAX, "%s.%s",
                    vob->video_out_file, RAW_AUD_EXT);
    } else {
        /* copy names verbatim */
        strlcpy(vid_name, vob->video_out_file, PATH_MAX);
        strlcpy(aud_name, vob->audio_out_file, PATH_MAX);
    }
    
    /* avoid fd loss in case of failed configuration */
    if (pd->fd_vid == -1) {
        pd->fd_vid = open(vid_name,
                          O_RDWR|O_CREAT|O_TRUNC,
                          S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if (pd->fd_vid == -1) {
            tc_log_error(MOD_NAME, "failed to open video stream file");
            return TC_ERROR;
        }
    }

    /* avoid fd loss in case of failed configuration */
    if (pd->fd_aud == -1) {
        pd->fd_aud = open(aud_name,
                          O_RDWR|O_CREAT|O_TRUNC,
                          S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if (pd->fd_aud == -1) {
            tc_log_error(MOD_NAME, "failed to open audio stream file");
            return TC_ERROR;
        }
    }
    if (vob->verbose >= TC_DEBUG) {
        tc_log_info(MOD_NAME, "video output: %s (%s)",
                    vid_name, (pd->fd_vid == -1) ?"FAILED" :"OK");
        tc_log_info(MOD_NAME, "audio output: %s (%s)",
                    aud_name, (pd->fd_aud == -1) ?"FAILED" :"OK");
    }
    return TC_OK;
}

static int raw_stop(TCModuleInstance *self)
{
    RawPrivateData *pd = NULL;
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
        pd->fd_vid = -1;
    }

    if (pd->fd_aud != -1) {
        aerr = close(pd->fd_aud);
        if (aerr) {
            tc_log_error(MOD_NAME, "closing audio file: %s",
                                   strerror(errno));
            return TC_ERROR;
        }
        pd->fd_aud = -1;
    }

    return TC_OK;
}

static int raw_multiplex(TCModuleInstance *self,
                         vframe_list_t *vframe, aframe_list_t *aframe)
{
    ssize_t w_aud = 0, w_vid = 0;

    RawPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "multiplex");

    pd = self->userdata;

    if (vframe != NULL && vframe->video_len > 0) {
        w_vid = tc_pwrite(pd->fd_vid, vframe->video_buf, vframe->video_len);
        if(w_vid < 0) {
            return TC_ERROR;
        }
    }

    if (aframe != NULL && aframe->audio_len > 0) {
        w_aud = tc_pwrite(pd->fd_aud, aframe->audio_buf, aframe->audio_len);
 		if (w_aud < 0) {
			return TC_ERROR;
		}
    }

    return (int)(w_vid + w_aud);
}

static int raw_init(TCModuleInstance *self, uint32_t features)
{
    RawPrivateData *pd = NULL;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    pd = tc_malloc(sizeof(RawPrivateData));
    if (pd == NULL) {
        return TC_ERROR;
    }

    pd->fd_aud = -1;
    pd->fd_vid = -1;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }

    self->userdata = pd;
    return TC_OK;
}

static int raw_fini(TCModuleInstance *self)
{
    TC_MODULE_SELF_CHECK(self, "fini");

    raw_stop(self);

    tc_free(self->userdata);
    self->userdata = NULL;

    return TC_OK;
}


/*************************************************************************/

static const TCCodecID raw_codecs_in[] = { TC_CODEC_ANY, TC_CODEC_ERROR };
static const TCFormatID raw_formats_out[] = { TC_FORMAT_RAW, TC_FORMAT_ERROR };
/* a multiplexor is at the end of pipeline */
TC_MODULE_MPLEX_FORMATS_CODECS(raw);

TC_MODULE_INFO(raw);

static const TCModuleClass raw_class = {
    TC_MODULE_CLASS_HEAD(raw),

    .init         = raw_init,
    .fini         = raw_fini,
    .configure    = raw_configure,
    .stop         = raw_stop,
    .inspect      = raw_inspect,

    .multiplex    = raw_multiplex,
};

TC_MODULE_ENTRY_POINT(raw)

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

