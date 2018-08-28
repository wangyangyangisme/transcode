/*
 * export_pvn.c -- module for exporting PVN video streams
 * (http://www.cse.yorku.ca/~jgryn/research/pvnspecs.html)
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "transcode.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"
#include "libtc/tcmodule-plugin.h"
#include "libtcvideo/tcvideo.h"

#define MOD_NAME        "export_pvn.so"
#define MOD_VERSION     "v1.0 (2006-10-06)"
#define MOD_CAP         "Writes PVN video files"
#define MOD_AUTHOR      "Andrew Church"

#define MOD_FEATURES \
    TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO
    
#define MOD_FLAGS \
    TC_MODULE_FLAG_RECONFIGURABLE
    


/*************************************************************************/

/* Local data structure: */

typedef struct {
    int width, height;     // Frame width and height (to catch changes)
    int fd;                // Output file descriptor
    int framecount;        // Number of frames written
    off_t framecount_pos;  // File position of frame count (for rewriting)
} PrivateData;

/*************************************************************************/
/*************************************************************************/

/* Module interface routines and data. */

/*************************************************************************/

/**
 * pvn_init:  Initialize this instance of the module.  See tcmodule-data.h
 * for function details.
 */

static int pvn_init(TCModuleInstance *self, uint32_t features)
{
    PrivateData *pd;

    TC_MODULE_SELF_CHECK(self, "init");
    TC_MODULE_INIT_CHECK(self, MOD_FEATURES, features);

    self->userdata = pd = tc_malloc(sizeof(PrivateData));
    if (!pd) {
        tc_log_error(MOD_NAME, "init: out of memory!");
        return -1;
    }
    pd->fd = -1;
    pd->framecount = 0;
    pd->framecount_pos = 0;

    if (verbose) {
        tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);
    }
    return 0;
}

/*************************************************************************/

/**
 * pvn_configure:  Configure this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int pvn_configure(TCModuleInstance *self,
                         const char *options, vob_t *vob)
{
    if (!self) {
       return -1;
    }
    return 0;
}

/*************************************************************************/

/**
 * pvn_inspect:  Return the value of an option in this instance of the
 * module.  See tcmodule-data.h for function details.
 */

static int pvn_inspect(TCModuleInstance *self,
                       const char *param, const char **value)
{
    static char buf[TC_BUF_MAX];

    if (!self || !param)
       return -1;

    if (optstr_lookup(param, "help")) {
        tc_snprintf(buf, sizeof(buf),
                "Overview:\n"
                "    Writes a PVN video stream (format PV6a, 8-bit data).\n"
                "    A grayscale file (PV5a) is written instead if the -K\n"
                "    switch is given to transcode.\n"
                "    The RGB colorspace must be used (-V rgb24).\n"
                "No options available.\n");
        *value = buf;
    }
    return 0;
}

/*************************************************************************/

/**
 * pvn_stop:  Reset this instance of the module.  See tcmodule-data.h for
 * function details.
 */

static int pvn_stop(TCModuleInstance *self)
{
    PrivateData *pd;

    if (!self) {
       return -1;
    }
    pd = self->userdata;

    if (pd->fd != -1) {
        if (pd->framecount > 0 && pd->framecount_pos > 0) {
            /* Write out final frame count, if we can */
            if (lseek(pd->fd, pd->framecount_pos, SEEK_SET) != (off_t)-1) {
                char buf[11];
                int len = tc_snprintf(buf, sizeof(buf), "%10d",pd->framecount);
                if (len > 0)
                    tc_pwrite(pd->fd, buf, len);
            }
        }
        close(pd->fd);
        pd->fd = -1;
    }

    return 0;
}

/*************************************************************************/

/**
 * pvn_fini:  Clean up after this instance of the module.  See
 * tcmodule-data.h for function details.
 */

static int pvn_fini(TCModuleInstance *self)
{
    if (!self) {
       return -1;
    }
    pvn_stop(self);
    tc_free(self->userdata);
    self->userdata = NULL;
    return 0;
}

/*************************************************************************/

/**
 * pvn_multiplex:  Multiplex a frame of data.  See tcmodule-data.h for
 * function details.
 */

static int pvn_multiplex(TCModuleInstance *self,
                         vframe_list_t *vframe, aframe_list_t *aframe)
{
    PrivateData *pd;

    if (!self) {
        tc_log_error(MOD_NAME, "multiplex: self == NULL!");
        return -1;
    }
    pd = self->userdata;
    if (pd->fd == -1) {
        tc_log_error(MOD_NAME, "multiplex: no file opened!");
        return -1;
    }

    if (vframe->v_width != pd->width || vframe->v_height != pd->height) {
        tc_log_error(MOD_NAME, "Video frame size changed in midstream!");
        return -1;
    }
    if (vframe->v_codec != CODEC_RGB) {
        tc_log_error(MOD_NAME, "Invalid codec for video frame!");
        return -1;
    }
    if (vframe->video_len != pd->width * pd->height * 3
     && vframe->video_len != pd->width * pd->height  // for grayscale
    ) {
        tc_log_error(MOD_NAME, "Invalid size for video frame!");
        return -1;
    }
    if (tc_pwrite(pd->fd, vframe->video_buf, vframe->video_len)
        != vframe->video_len
    ) {
        tc_log_error(MOD_NAME, "Error writing frame %d to output file: %s",
                     pd->framecount, strerror(errno));
        return -1;
    }
    pd->framecount++;
    return vframe->video_len;
}

/*************************************************************************/

static const TCCodecID pvn_codecs_in[] = { TC_CODEC_RGB, TC_CODEC_ERROR };
static const TCCodecID pvn_codecs_out[] = { TC_CODEC_ERROR };
static const TCFormatID pvn_formats_in[] = { TC_FORMAT_ERROR };
static const TCFormatID pvn_formats_out[] = { TC_FORMAT_PVN, TC_CODEC_ERROR };

static const TCModuleInfo pvn_info = {
    .features    = MOD_FEATURES,
    .flags       = MOD_FLAGS,
    .name        = MOD_NAME,
    .version     = MOD_VERSION,
    .description = MOD_CAP,
    .codecs_in   = pvn_codecs_in,
    .codecs_out  = pvn_codecs_out,
    .formats_in  = pvn_formats_in,
    .formats_out = pvn_formats_out
};

static const TCModuleClass pvn_class = {
    .info      = &pvn_info,

    .init      = pvn_init,
    .fini      = pvn_fini,
    .configure = pvn_configure,
    .stop      = pvn_stop,
    .inspect   = pvn_inspect,

    .multiplex = pvn_multiplex,
};

extern const TCModuleClass *tc_plugin_setup(void)
{
    return &pvn_class;
}

/*************************************************************************/
/*************************************************************************/

/* Old-fashioned module interface. */

static TCModuleInstance mod;

static int verbose_flag;
static int capability_flag = TC_CAP_RGB;
#define MOD_PRE pvn
#define MOD_CODEC "(video) PVN"
#include "export_def.h"
MOD_init {return 0;}
MOD_stop {return 0;}

/*************************************************************************/

MOD_open
{
    PrivateData *pd = NULL;
    char buf[1000];
    int len;

    if (param->flag != TC_VIDEO)
        return -1;
    if (pvn_init(&mod, TC_MODULE_FEATURE_MULTIPLEX|TC_MODULE_FEATURE_VIDEO) < 0)
        return -1;
    pd = mod.userdata;

    pd->width = vob->ex_v_width;
    pd->height = vob->ex_v_height;
    /* FIXME: stdout should be handled in a more standard fashion */
    if (strcmp(vob->video_out_file, "-") == 0) {  // allow /dev/stdout too?
        pd->fd = 1;
    } else {
        pd->fd = open(vob->video_out_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (pd->fd < 0) {
            tc_log_error(MOD_NAME, "Unable to open %s: %s",
                         vob->video_out_file, strerror(errno));
            goto fail;
        }
    }
    len = tc_snprintf(buf, sizeof(buf), "PV%da\r\n%d %d\r\n",
                      tc_get_vob()->decolor ? 5 : 6,
                      pd->width, pd->height);
    if (len < 0)
        goto fail;
    if (tc_pwrite(pd->fd, buf, len) != len) {
        tc_log_error(MOD_NAME, "Unable to write header to %s: %s",
                     vob->video_out_file, strerror(errno));
        goto fail;
    }
    pd->framecount_pos = lseek(pd->fd, 0, SEEK_CUR);  // failure okay
    len = tc_snprintf(buf, sizeof(buf), "%10d\r\n8\r\n%lf\r\n",
                      0, (double)vob->ex_fps);
    if (len < 0)
        goto fail;
    if (tc_pwrite(pd->fd, buf, len) != len) {
        tc_log_error(MOD_NAME, "Unable to write header to %s: %s",
                     vob->video_out_file, strerror(errno));
        goto fail;
    }

    return 0;

  fail:
    pvn_fini(&mod);
    return -1;
}

/*************************************************************************/

MOD_close
{
    if (param->flag != TC_VIDEO)
        return -1;
    pvn_fini(&mod);
    return 0;
}

/*************************************************************************/

MOD_encode
{
    vframe_list_t vframe;

    if (param->flag != TC_VIDEO)
        return -1;

    vframe.v_width   = tc_get_vob()->ex_v_width;
    vframe.v_height  = tc_get_vob()->ex_v_height;
    vframe.v_codec   = tc_get_vob()->ex_v_codec;
    vframe.video_buf = param->buffer;
    vframe.video_len = param->size;
    if (!vframe.v_codec)
        vframe.v_codec = CODEC_RGB;  // assume it's correct
    if (tc_get_vob()->decolor) {
        // Assume the data is already decolored and just take every third byte
        int i;
        vframe.video_len /= 3;
        for (i = 0; i < vframe.video_len; i++)
            vframe.video_buf[i] = vframe.video_buf[i*3];
    }
    if (pvn_multiplex(&mod, &vframe, NULL) < 0)
        return -1;

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
