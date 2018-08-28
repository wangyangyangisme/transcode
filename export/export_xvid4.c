/*****************************************************************************
 *  - XviD Transcode Export Module -
 *
 *  Copyright (C) 2001-2003 - Thomas Oestreich
 *
 *  Author : Edouard Gomez <ed.gomez@free.fr>
 *
 *  This file is part of transcode, a video stream processing tool
 *
 *  transcode is free software ; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation ; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY ; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program ; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *****************************************************************************/

/*****************************************************************************
 * Includes
 ****************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

#ifndef OS_BSD
# ifdef HAVE_MALLOC_H
# include <malloc.h>
# endif
#endif

#include "xvid4.h"

#include "transcode.h"
#include "avilib/avilib.h"
#include "aud_aux.h"
#include "libtc/libtc.h"
#include "libtcvideo/tcvideo.h"

#include "libtc/cfgfile.h"

/*****************************************************************************
 * Transcode module binding functions and strings
 ****************************************************************************/

#define MOD_NAME    "export_xvid4.so"
#define MOD_VERSION "v0.0.6 (2007-08-11)"
#define MOD_CODEC  \
"(video) XviD 1.0.x series (aka API 4.0) | (audio) MPEG/AC3/PCM"
static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_PCM |
                             TC_CAP_RGB |
                             TC_CAP_YUV |
                             TC_CAP_YUV422 |
                             TC_CAP_AC3 |
                             TC_CAP_AUD;
#define MOD_PRE xvid4_
#include "export_def.h"

/* XviD shared library name */
#define XVID_SHARED_LIB_BASE "libxvidcore"
#ifdef OS_DARWIN
#define XVID_SHARED_LIB_SUFX "dylib"
#else
#define XVID_SHARED_LIB_SUFX "so"
#endif
#define XVID_CONFIG_FILE "xvid4.cfg"

/*****************************************************************************
 * XviD symbols grouped in a nice struct.
 ****************************************************************************/

typedef int (*xvid_function_t)(void *handle, int opt,
                               void *param1, void *param2);

typedef struct _xvid_module_t
{
    void *so;
    xvid_function_t global;
    xvid_function_t encore;
    xvid_function_t plugin_onepass;
    xvid_function_t plugin_twopass1;
    xvid_function_t plugin_twopass2;
    xvid_function_t plugin_lumimasking;
} xvid_module_t;

static int load_xvid(xvid_module_t *xvid, const char *path);
static int unload_xvid(xvid_module_t *xvid);

/*****************************************************************************
 * Transcode module private data
 ****************************************************************************/

typedef struct _xvid_transcode_module_t
{
    /* XviD lib functions */
    xvid_module_t xvid;

    /* Instance related global vars */
    void *instance;
    xvid_gbl_init_t   xvid_gbl_init;
    xvid_enc_create_t xvid_enc_create;
    xvid_enc_frame_t  xvid_enc_frame;

    /* This data must survive local block scope, so here it is */
    xvid_enc_plugin_t    plugins[7];
    xvid_enc_zone_t      zones[2];
    xvid_plugin_single_t onepass;
    xvid_plugin_2pass1_t pass1;
    xvid_plugin_2pass2_t pass2;

    /* Options from the config file */
    xvid_enc_create_t    cfg_create;
    xvid_enc_frame_t     cfg_frame;
    xvid_plugin_single_t cfg_onepass;
    xvid_plugin_2pass2_t cfg_pass2;
    char *cfg_intra_matrix_file;
    char *cfg_inter_matrix_file;
    char *cfg_quant_method;
    int cfg_packed;
    int cfg_closed_gop;
    int cfg_interlaced;
    int cfg_quarterpel;
    int cfg_gmc;
    int cfg_trellis;
    int cfg_cartoon;
    int cfg_hqacpred;
    int cfg_chromame;
    int cfg_vhq;
    int cfg_motion;
    int cfg_stats;
    int cfg_greyscale;
    int cfg_turbo;
    int cfg_full1pass;

    /* MPEG4 stream buffer */
    int   stream_size;
    uint8_t *stream;

    /* File output
     * Depending on the "-F raw" option presence, this module will
     * use/initialize either the raw file descriptor or the avi file
     * pointer in the vob structure passed by transcode */
    int rawfd;

    /* Stats accumulators */
    int frames;
    long long sse_y;
    long long sse_u;
    long long sse_v;

    /* Image format conversion handle */
    TCVHandle tcvhandle;
} xvid_transcode_module_t;

static xvid_transcode_module_t thismod;

static void reset_module(xvid_transcode_module_t *mod);
static void free_module(xvid_transcode_module_t *mod);
static void read_config_file(xvid_transcode_module_t *mod);
static void dispatch_settings(xvid_transcode_module_t *mod);
static void set_create_struct(xvid_transcode_module_t *mod, vob_t *vob);
static void set_frame_struct(xvid_transcode_module_t *mod, vob_t *vob, transfer_t *t);
static const char *errorstring(int err);

/*****************************************************************************
 * Init codec
 ****************************************************************************/

MOD_init
{
    int ret;
    xvid_module_t *xvid = &thismod.xvid;

    /* Invalid flag */
    if(param->flag != TC_AUDIO && param->flag != TC_VIDEO)
        return(TC_EXPORT_ERROR);

    /* Audio init */
    if(param->flag == TC_AUDIO)
        return(tc_audio_init(vob, verbose));

    /* Video init */

    /* This is the first function called according to transcode API
     * We must initialize the module structure correctly but only
     * _once_ */
    reset_module(&thismod);

    /* Check frame dimensions */
    if(vob->ex_v_width%2 || vob->ex_v_height%2) {
        tc_log_warn(MOD_NAME, "Only even dimensions allowed (%dx%d)",
            vob->ex_v_width, vob->ex_v_height);
        return(TC_EXPORT_ERROR);
    }

    /* Buffer allocation
     * We allocate width*height*bpp/8 to "receive" the compressed stream
     * I don't think the codec will ever return more than that. It's and
     * encoder, so if it fails delivering smaller frames than original
     * ones, something really odd occurs somewhere and i prefer the
     * application crash */
    thismod.stream_size = vob->ex_v_width * vob->ex_v_height;
    if(vob->im_v_codec == CODEC_RGB) {
        thismod.stream_size *= 3;
        if (!(thismod.tcvhandle = tcv_init())) {
            tc_log_warn(MOD_NAME, "tcv_init failed");
            return TC_EXPORT_ERROR;
        }
    } else if(vob->im_v_codec == CODEC_YUV422) {
        thismod.stream_size *= 2;
        if (!(thismod.tcvhandle = tcv_init())) {
            tc_log_warn(MOD_NAME, "tcv_init failed");
            return TC_EXPORT_ERROR;
        }
    } else
        thismod.stream_size = (thismod.stream_size*3)/2;
    if((thismod.stream = malloc(thismod.stream_size)) == NULL) {
        tc_log_error(MOD_NAME, "Error allocating stream buffer");
        return(TC_EXPORT_ERROR);
    } else {
        memset(thismod.stream, 0, thismod.stream_size);
    }

    /* Load the codec */
    if(load_xvid(xvid, vob->mod_path) < 0)
        return(TC_EXPORT_ERROR);

    /* Load the config file settings */
    read_config_file(&thismod);

    /* Dispatch settings to xvid structures that hold the config ready to
     * be copied to encoder structures */
    dispatch_settings(&thismod);

    /* Init the xvidcore lib */
    memset(&thismod.xvid_gbl_init, 0, sizeof(xvid_gbl_init_t));
    thismod.xvid_gbl_init.version = XVID_VERSION;
    ret = xvid->global(NULL, XVID_GBL_INIT, &thismod.xvid_gbl_init, NULL);

    if(ret < 0) {
        tc_log_warn(MOD_NAME, "Library initialization failed");
        return(TC_EXPORT_ERROR);
    }

    /* Combine both the config settings with the transcode direct options
     * into the final xvid_enc_create_t struct */
    set_create_struct(&thismod, vob);
    ret = xvid->encore(NULL, XVID_ENC_CREATE, &thismod.xvid_enc_create, NULL);

    if(ret < 0) {
        tc_log_warn(MOD_NAME, "Encoder initialization failed");
        return(TC_EXPORT_ERROR);
    }

    /* Attach returned instance */
    thismod.instance = thismod.xvid_enc_create.handle;

    return(TC_EXPORT_OK);
}


/*****************************************************************************
 * Open the output file
 ****************************************************************************/

MOD_open
{
    int avi_output = 1;

    /* Invalid flag */
    if(param->flag != TC_AUDIO && param->flag != TC_VIDEO)
        return(TC_EXPORT_ERROR);

    /* Check for raw output */
    if((vob->ex_v_fcc != NULL) && (strlen(vob->ex_v_fcc) != 0) &&
       (strcasecmp(vob->ex_v_fcc, "raw") == 0))
        avi_output = 0;

    /* Open file */
    if(avi_output && vob->avifile_out == NULL) {

        vob->avifile_out = AVI_open_output_file(vob->video_out_file);

        if((vob->avifile_out) == NULL) {
            AVI_print_error("avi open error");
            return(TC_EXPORT_ERROR);
        }
    }

    /* Open audio file */
    if(param->flag == TC_AUDIO) {
        tc_log_warn(MOD_NAME, "Usage of this module for audio encoding is deprecated.");
        tc_log_warn(MOD_NAME, "Consider switch to export_tcaud module.");
        return(tc_audio_open(vob, vob->avifile_out));
    }

    /* Open video file */
    if(verbose_flag & TC_DEBUG)
        tc_log_info(MOD_NAME, "Using %s output",
                avi_output?"AVI":"Raw");

    if(avi_output) {
        /* AVI Video output */
        AVI_set_video(vob->avifile_out, vob->ex_v_width,
                  vob->ex_v_height, vob->ex_fps, "XVID");

        if(vob->avi_comment_fd > 0)
            AVI_set_comment_fd(vob->avifile_out,
                       vob->avi_comment_fd);
    } else {
        /* Raw Video output */
        thismod.rawfd = open(vob->video_out_file,
                    O_RDWR|O_CREAT|O_TRUNC,
                    S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if(thismod.rawfd < 0) {
            tc_log_perror(MOD_NAME, "open file");
            return(TC_EXPORT_ERROR);
        }
    }

    return(TC_EXPORT_OK);
}


/*****************************************************************************
 * Encode and export a frame
 ****************************************************************************/

static int tc_xvid_write(int bytes, vob_t *vob)
{
    /* Make sure we take care of AVI splitting */
    if(thismod.rawfd < 0) {
        if((uint32_t)(AVI_bytes_written(vob->avifile_out)+bytes+16+8)>>20 >= tc_avi_limit)
            tc_outstream_rotate_request();
        if(thismod.xvid_enc_frame.out_flags & XVID_KEYFRAME)
            tc_outstream_rotate();
    }

    /* Write bitstream */
    if(thismod.rawfd < 0) {
        int ret = AVI_write_frame(vob->avifile_out, thismod.stream, bytes,
                                  thismod.xvid_enc_frame.out_flags & XVID_KEYFRAME);
        if(ret < 0) {
            tc_log_warn(MOD_NAME, "AVI video write error");
            return(TC_EXPORT_ERROR);
        }
    } else {
        int ret = tc_pwrite(thismod.rawfd, thismod.stream, bytes);
        if(ret != bytes) {
            tc_log_warn(MOD_NAME, "RAW video write error");
            return(TC_EXPORT_ERROR);
        }
    }

    return(TC_EXPORT_OK);
}

MOD_encode
{
    int bytes;

    xvid_enc_stats_t xvid_enc_stats;
    xvid_module_t *xvid = &thismod.xvid;
    vob_t *vob = tc_get_vob();

    /* Invalid flag */
    if(param->flag != TC_AUDIO && param->flag != TC_VIDEO)
        return(TC_EXPORT_ERROR);

    /* Audio encoding */
    if(param->flag == TC_AUDIO)
        return(tc_audio_encode(param->buffer, param->size, vob->avifile_out));

    /* Video encoding */

    if(vob->im_v_codec == CODEC_YUV422) {
        /* Convert to UYVY */
        tcv_convert(thismod.tcvhandle, param->buffer, param->buffer,
                    vob->ex_v_width, vob->ex_v_height,
                    IMG_YUV422P, IMG_UYVY);
    } else if (vob->im_v_codec == CODEC_RGB) {
        /* Convert to BGR (why isn't RGB supported??) */
        tcv_convert(thismod.tcvhandle, param->buffer, param->buffer,
                    vob->ex_v_width, vob->ex_v_height,
                    IMG_RGB24, IMG_BGR24);
    }

    /* Init the stat structure */
    memset(&xvid_enc_stats,0, sizeof(xvid_enc_stats_t));
    xvid_enc_stats.version = XVID_VERSION;

    /* Combine both the config settings with the transcode direct options
     * into the final xvid_enc_frame_t struct */
    set_frame_struct(&thismod, vob, param);

    bytes = xvid->encore(thismod.instance, XVID_ENC_ENCODE,
                 &thismod.xvid_enc_frame, &xvid_enc_stats);

    /* Error handling */
    if(bytes < 0) {
        tc_log_warn(MOD_NAME, "xvidcore returned a \"%s\" error",
            errorstring(bytes));
        return(TC_EXPORT_ERROR);
    }

    /* Extract stats info */
    if(xvid_enc_stats.type>0 && thismod.cfg_stats) {
        thismod.frames++;
        thismod.sse_y += xvid_enc_stats.sse_y;
        thismod.sse_u += xvid_enc_stats.sse_u;
        thismod.sse_v += xvid_enc_stats.sse_v;
    }

    /* XviD Core rame buffering handling
    * We must make sure audio A/V is still good and does not run away */
    if(bytes == 0) {
        param->attributes |= TC_FRAME_IS_DELAYED;
        return(TC_EXPORT_OK);
    }

    /* Make sure we take care of AVI splitting */
    return tc_xvid_write(bytes, vob);
}

/*****************************************************************************
 * Close codec
 ****************************************************************************/

static int tc_xvid_flush(vob_t *vob)
{
    int bytes = 0, ret = TC_EXPORT_OK;
    xvid_enc_stats_t xvid_enc_stats;
    xvid_module_t *xvid = &thismod.xvid;

    do {
        ret = TC_EXPORT_OK;
        
        /* Init the stat structure */
        memset(&xvid_enc_stats,0, sizeof(xvid_enc_stats_t));
        xvid_enc_stats.version = XVID_VERSION;

        set_frame_struct(&thismod, vob, NULL);

        bytes = xvid->encore(thismod.instance, XVID_ENC_ENCODE,
                             &thismod.xvid_enc_frame, &xvid_enc_stats);

        /* Extract stats info */
        if(xvid_enc_stats.type>0 && thismod.cfg_stats) {
            thismod.frames++;
            thismod.sse_y += xvid_enc_stats.sse_y;
            thismod.sse_u += xvid_enc_stats.sse_u;
            thismod.sse_v += xvid_enc_stats.sse_v;
        }

        if (bytes > 0) {
            ret = tc_xvid_write(bytes, vob);
        }
    } while (bytes > 0 && ret == TC_EXPORT_OK);
    return ret;
}


MOD_close
{
    vob_t *vob = tc_get_vob();

    /* Audio file closing */
    if(param->flag == TC_AUDIO)
        return(tc_audio_close());

    if(param->flag == TC_VIDEO) {
        int ret = tc_xvid_flush(vob);
        if (ret == TC_EXPORT_OK) {
            /* Video file closing */
            if(thismod.rawfd >= 0) {
                close(thismod.rawfd);
                thismod.rawfd = -1;
            }
            if(vob->avifile_out != NULL) {
                AVI_close(vob->avifile_out);
                vob->avifile_out = NULL;
            }
            return(TC_EXPORT_OK);
        }
        /* fallback to EXPORT_ERROR */
    }
    return(TC_EXPORT_ERROR);
}


/*****************************************************************************
 * Stop encoder
 ****************************************************************************/

#define SSE2PSNR(sse, width, height) \
((!(sse)) ? (99.0f) : (48.131f - 10*(float)log10((float)(sse)/((float)((width)*(height))))))

MOD_stop
{
    int ret;
    xvid_module_t *xvid = &thismod.xvid;

    /* Invalid flag */
    if(param->flag != TC_AUDIO && param->flag != TC_VIDEO)
        return(TC_EXPORT_ERROR);

    /* Audio codec stoping */
    if(param->flag == TC_AUDIO)
        return(tc_audio_stop());

    /* Video codec stoping */

    /* ToDo: Can we flush the last frames here ? */

    /* Destroy the encoder instance */
    ret = xvid->encore(thismod.instance, XVID_ENC_DESTROY, NULL, NULL);
    if(ret < 0) {
        tc_log_warn(MOD_NAME, "Encoder instance releasing failed");
        return(TC_EXPORT_ERROR);
    }

    /* Unload the shared symbols/lib */
    unload_xvid(xvid);

    /* Free all dynamic memory allocated in the module structure */
    free_module(&thismod);

    /* Print stats before resting the complete module structure */
    if(thismod.cfg_stats) {
        if(thismod.frames) {
            thismod.sse_y /= thismod.frames;
            thismod.sse_u /= thismod.frames;
            thismod.sse_v /= thismod.frames;
        } else {
            thismod.sse_y = 0;
            thismod.sse_u = 0;
            thismod.sse_v = 0;
        }

        tc_log_info(MOD_NAME,
            "psnr y = %.2f dB, "
            "psnr u = %.2f dB, "
            "psnr v = %.2f dB",
            SSE2PSNR(thismod.sse_y,
                 thismod.xvid_enc_create.width,
                 thismod.xvid_enc_create.height),
            SSE2PSNR(thismod.sse_u,
                 thismod.xvid_enc_create.width/2,
                 thismod.xvid_enc_create.height/2),
            SSE2PSNR(thismod.sse_v,
                 thismod.xvid_enc_create.width/2,
                 thismod.xvid_enc_create.height/2));
    }

    /* This is the last function according to the transcode API
     * this should be safe to reset the module structure */
    reset_module(&thismod);

    return(TC_EXPORT_OK);
}
#undef SSE2PSNR

/*****************************************************************************
 * Transcode module helper functions
 ****************************************************************************/

static void reset_module(xvid_transcode_module_t *mod)
{
    memset(mod, 0, sizeof(xvid_transcode_module_t));

    /* This file descriptor is used as a flag
     * < 0 means AVI output (invalid descriptor)
     * >= 0 means Raw output (valid descriptor) */
    mod->rawfd = -1;

    /* Default options */
    mod->cfg_packed = 0;
    mod->cfg_closed_gop = 1;
    mod->cfg_interlaced = 0;
    mod->cfg_quarterpel = 0;
    mod->cfg_gmc = 0;
    mod->cfg_trellis = 0;
    mod->cfg_cartoon = 0;
    mod->cfg_hqacpred = 1;
    mod->cfg_chromame = 1;
    mod->cfg_vhq = 1;
    mod->cfg_motion = 6;
    mod->cfg_turbo = 0;
    mod->cfg_full1pass = 0;
    mod->cfg_stats = 0;
    mod->cfg_greyscale = 0;
    mod->cfg_quant_method = tc_strdup("h263");
    mod->cfg_create.max_bframes = 1;
    mod->cfg_create.bquant_ratio = 150;
    mod->cfg_create.bquant_offset = 100;

    return;
}

static void free_module(xvid_transcode_module_t *mod)
{

    /* Free tcvideo handle */
    tcv_free(thismod.tcvhandle);
    thismod.tcvhandle = 0;

    /* Release stream buffer memory */
    if(mod->stream) {
        free(mod->stream);
        mod->stream = NULL;
    }

    /* Release the quant method string */
    if(mod->cfg_quant_method) {
        free(thismod.cfg_quant_method);
        thismod.cfg_quant_method = NULL;
    }

    /* Release the matrix file name string */
    if(mod->cfg_inter_matrix_file) {
        free(mod->cfg_inter_matrix_file);
        mod->cfg_inter_matrix_file = NULL;
    }

    /* Release the matrix definition */
    if(mod->cfg_frame.quant_inter_matrix) {
        free(mod->cfg_frame.quant_inter_matrix);
        mod->cfg_frame.quant_inter_matrix = NULL;
    }

    /* Release the matrix file name string */
    if(mod->cfg_intra_matrix_file) {
        free(mod->cfg_intra_matrix_file);
        mod->cfg_intra_matrix_file = NULL;
    }

    /* Release the matrix definition */
    if(mod->cfg_frame.quant_intra_matrix) {
        free(mod->cfg_frame.quant_intra_matrix);
        mod->cfg_frame.quant_intra_matrix = NULL;
    }

    return;
}

/*****************************************************************************
 * Configuration functions
 *
 * They fill the .cfg_xxx members of the module structure.
 *  - read_config_file reads the values from the config file and sets .cfg_xxx
 *    members of the module structure.
 *  - dispatch_settings uses the values retrieved by read_config_file and
 *    turns them into XviD settings in the cfg_xxx xvid structure available
 *    in the module structure.
 *  - set_create_struct sets a xvid_enc_create structure according to the
 *    settings generated by the two previous functions calls.
 *  - set_frame_struct same as above for a xvid_enc_frame_t struct.
 ****************************************************************************/

static void read_config_file(xvid_transcode_module_t *mod)
{
    xvid_plugin_single_t *onepass = &mod->cfg_onepass;
    xvid_plugin_2pass2_t *pass2   = &mod->cfg_pass2;
    xvid_enc_create_t    *create  = &mod->cfg_create;
    xvid_enc_frame_t     *frame   = &mod->cfg_frame;

    TCConfigEntry complete_config[] =
        {
            /* Section [features] */
//          {"features", "Feature settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"quant_type", &mod->cfg_quant_method, TCCONF_TYPE_STRING, 0, 0, 0},
            {"motion", &mod->cfg_motion, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 6},
            {"chromame", &mod->cfg_chromame, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"vhq", &mod->cfg_vhq, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 4},
            {"max_bframes", &create->max_bframes, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 20},
            {"bquant_ratio", &create->bquant_ratio, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 200},
            {"bquant_offset", &create->bquant_offset, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 200},
            {"bframe_threshold", &frame->bframe_threshold, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -255, 255},
            {"quarterpel", &mod->cfg_quarterpel, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"gmc", &mod->cfg_gmc, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"trellis", &mod->cfg_trellis, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"packed", &mod->cfg_packed, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"closed_gop", &mod->cfg_closed_gop, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"interlaced", &mod->cfg_interlaced, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"cartoon", &mod->cfg_cartoon, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"hqacpred", &mod->cfg_hqacpred, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"frame_drop_ratio", &create->frame_drop_ratio, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"stats", &mod->cfg_stats, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"greyscale", &mod->cfg_greyscale, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"turbo", &mod->cfg_turbo, TCCONF_TYPE_FLAG, 0, 0, 1},
            {"full1pass", &mod->cfg_full1pass, TCCONF_TYPE_FLAG, 0, 0, 1},

            /* section [quantizer] */
//          {"quantizer", "Quantizer settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"min_iquant", &create->min_quant[0], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"max_iquant", &create->max_quant[0], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"min_pquant", &create->min_quant[1], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"max_pquant", &create->max_quant[1], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"min_bquant", &create->min_quant[2], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"max_bquant", &create->max_quant[2], TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
            {"quant_intra_matrix", &mod->cfg_intra_matrix_file, TCCONF_TYPE_STRING, 0, 0, 100},
            {"quant_inter_matrix", &mod->cfg_inter_matrix_file, TCCONF_TYPE_STRING, 0, 0, 100},

            /* section [cbr] */
//          {"cbr", "CBR settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"reaction_delay_factor", &onepass->reaction_delay_factor, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"averaging_period", &onepass->averaging_period, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},
            {"buffer", &onepass->buffer, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},

            /* section [vbr] */
//          {"vbr", "VBR settings", TCCONF_TYPE_SECTION, 0, 0, 0},
            {"keyframe_boost", &pass2->keyframe_boost, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"curve_compression_high", &pass2->curve_compression_high, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"curve_compression_low", &pass2->curve_compression_low, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"overflow_control_strength", &pass2->overflow_control_strength, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"max_overflow_improvement", &pass2->max_overflow_improvement, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"max_overflow_degradation", &pass2->max_overflow_degradation, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"kfreduction", &pass2->kfreduction, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100},
            {"kfthreshold", &pass2->kfthreshold, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},
            {"container_frame_overhead", &pass2->container_frame_overhead, TCCONF_TYPE_INT, TCCONF_FLAG_MIN, 0, 0},

            /* End of the config file */
            {NULL, 0, 0, 0, 0, 0}
        };

    /* Read the values */
    module_read_config("xvid4.cfg", NULL, complete_config, MOD_NAME);

    /* Print the values */
    if(verbose_flag & TC_DEBUG)
        module_print_config(complete_config, MOD_NAME);

    return;
}

static void *read_matrix(const char *filename);
static void print_matrix(unsigned char *matrix);

static void dispatch_settings(xvid_transcode_module_t *mod)
{

    xvid_enc_create_t *create = &mod->cfg_create;
    xvid_enc_frame_t  *frame  = &mod->cfg_frame;

    const int motion_presets[7] =
        {
            0,
            0,
            0,
            0,
            XVID_ME_HALFPELREFINE16,
            XVID_ME_HALFPELREFINE16 | XVID_ME_ADVANCEDDIAMOND16,
            XVID_ME_HALFPELREFINE16 | XVID_ME_EXTSEARCH16 |
            XVID_ME_HALFPELREFINE8  | XVID_ME_USESQUARES16
        };


    /* Dispatch all settings having an impact on the "create" structure */
    create->global = 0;

    if(mod->cfg_packed)
        create->global |= XVID_GLOBAL_PACKED;

    if(mod->cfg_closed_gop)
        create->global |= XVID_GLOBAL_CLOSED_GOP;

    if(mod->cfg_stats)
        create->global |= XVID_GLOBAL_EXTRASTATS_ENABLE;

    /* Dispatch all settings having an impact on the "frame" structure */
    frame->vol_flags = 0;
    frame->vop_flags = 0;
    frame->motion    = 0;

    frame->vop_flags |= XVID_VOP_HALFPEL;
    frame->motion    |= motion_presets[mod->cfg_motion];

    if(mod->cfg_stats)
        frame->vol_flags |= XVID_VOL_EXTRASTATS;

    if(mod->cfg_greyscale)
        frame->vop_flags |= XVID_VOP_GREYSCALE;

    if(mod->cfg_cartoon) {
        frame->vop_flags |= XVID_VOP_CARTOON;
        frame->motion |= XVID_ME_DETECT_STATIC_MOTION;
    }

    if(mod->cfg_intra_matrix_file) {
        frame->quant_intra_matrix = (unsigned char*)read_matrix(mod->cfg_intra_matrix_file);
        if(frame->quant_intra_matrix != NULL) {
            tc_log_info(MOD_NAME, "Loaded Intra matrix (switching to mpeg quantization type)");
            print_matrix(frame->quant_intra_matrix);
            free(mod->cfg_quant_method);
            mod->cfg_quant_method = tc_strdup("mpeg");
        }
    }
    if(mod->cfg_inter_matrix_file) {
        frame->quant_inter_matrix = read_matrix(mod->cfg_inter_matrix_file);
        if(frame->quant_inter_matrix) {
            tc_log_info(MOD_NAME, "Loaded Inter matrix (switching to mpeg quantization type)");
            print_matrix(frame->quant_inter_matrix);
            free(mod->cfg_quant_method);
            mod->cfg_quant_method = tc_strdup("mpeg");
        }
    }
    if(!strcasecmp(mod->cfg_quant_method, "mpeg")) {
        frame->vol_flags |= XVID_VOL_MPEGQUANT;
    }
    if(mod->cfg_quarterpel) {
        frame->vol_flags |= XVID_VOL_QUARTERPEL;
        frame->motion    |= XVID_ME_QUARTERPELREFINE16;
        frame->motion    |= XVID_ME_QUARTERPELREFINE8;
    }
    if(mod->cfg_gmc) {
        frame->vol_flags |= XVID_VOL_GMC;
        frame->motion    |= XVID_ME_GME_REFINE;
    }
    if(mod->cfg_interlaced) {
        frame->vol_flags |= XVID_VOL_INTERLACING;
    }
    if(mod->cfg_trellis) {
        frame->vop_flags |= XVID_VOP_TRELLISQUANT;
    }
    if(mod->cfg_hqacpred) {
        frame->vop_flags |= XVID_VOP_HQACPRED;
    }
    if(mod->cfg_motion > 4) {
        frame->vop_flags |= XVID_VOP_INTER4V;
    }
    if(mod->cfg_chromame) {
        frame->motion |= XVID_ME_CHROMA_PVOP;
        frame->motion |= XVID_ME_CHROMA_BVOP;
    }
    if(mod->cfg_vhq >= 1) {
        frame->vop_flags |= XVID_VOP_MODEDECISION_RD;
    }
    if(mod->cfg_vhq >= 2) {
        frame->motion |= XVID_ME_HALFPELREFINE16_RD;
        frame->motion |= XVID_ME_QUARTERPELREFINE16_RD;
    }
    if(mod->cfg_vhq >= 3) {
        frame->motion |= XVID_ME_HALFPELREFINE8_RD;
        frame->motion |= XVID_ME_QUARTERPELREFINE8_RD;
        frame->motion |= XVID_ME_CHECKPREDICTION_RD;
    }
    if(mod->cfg_vhq >= 4) {
        frame->motion |= XVID_ME_EXTSEARCH_RD;
    }
    if (mod->cfg_turbo) {
        frame->motion |= XVID_ME_FASTREFINE16;
        frame->motion |= XVID_ME_FASTREFINE8;
        frame->motion |= XVID_ME_SKIP_DELTASEARCH;
        frame->motion |= XVID_ME_FAST_MODEINTERPOLATE;
        frame->motion |= XVID_ME_BFRAME_EARLYSTOP;
    }

    /* motion level == 0 means no motion search which is equivalent to
     * intra coding only */
    if(mod->cfg_motion == 0) {
        frame->type = XVID_TYPE_IVOP;
    } else {
        frame->type = XVID_TYPE_AUTO;
    }

    return;
}

static void set_create_struct(xvid_transcode_module_t *mod, vob_t *vob)
{
    xvid_enc_create_t *x    = &mod->xvid_enc_create;
    xvid_enc_create_t *xcfg = &mod->cfg_create;
    xvid_module_t *xvid     = &mod->xvid;

    memset(x, 0, sizeof(xvid_enc_create_t));
    x->version = XVID_VERSION;

    /* Global encoder options */
    x->global = xcfg->global;

    /* Width and Height */
    x->width  = vob->ex_v_width;
    x->height = vob->ex_v_height;

    /* Max keyframe interval */
    x->max_key_interval = vob->divxkeyframes;

    /* FPS : we take care of non integer values */
    if((vob->ex_fps - (int)vob->ex_fps) == 0) {
        x->fincr = 1;
        x->fbase = (int)vob->ex_fps;
    } else {
        x->fincr = 1001;
        x->fbase = (int)(1001 * vob->ex_fps);
    }

    /* BFrames settings */
    x->max_bframes   = xcfg->max_bframes;
    x->bquant_ratio  = xcfg->bquant_ratio;
    x->bquant_offset = xcfg->bquant_offset;

    /* Frame dropping factor */
    x->frame_drop_ratio = xcfg->frame_drop_ratio;

    /* Quantizers */
    x->min_quant[0] = xcfg->min_quant[0];
    x->min_quant[1] = xcfg->min_quant[1];
    x->min_quant[2] = xcfg->min_quant[2];
    x->max_quant[0] = xcfg->max_quant[0];
    x->max_quant[1] = xcfg->max_quant[1];
    x->max_quant[2] = xcfg->max_quant[2];

    /* Encodings zones
     * ToDo?: Allow zones definitions */
    memset(mod->zones, 0, sizeof(mod->zones));
    x->zones     = mod->zones;

    if (1 == vob->divxmultipass && mod->cfg_full1pass)
    {
        x->zones[0].frame = 0;
        x->zones[0].mode = XVID_ZONE_QUANT;
        x->zones[0].increment = 200;
        x->zones[0].base = 100;
        x->num_zones = 1;
    } else {
        x->num_zones = 0;
    }

    /* Plugins */
    memset(mod->plugins, 0, sizeof(mod->plugins));
    x->plugins     = mod->plugins;
    x->num_plugins = 0;

    /* Initialize rate controller plugin */

    /* This is the first pass of a Two pass process */
    if(vob->divxmultipass == 1) {
        xvid_plugin_2pass1_t *pass1 = &mod->pass1;

        if(xvid->plugin_twopass1 == NULL) {
            tc_log_warn(MOD_NAME, "Two Pass #1 bitrate controller plugin not available");
            return;
        }

        memset(pass1, 0, sizeof(xvid_plugin_2pass1_t));
        pass1->version  = XVID_VERSION;
        pass1->filename = vob->divxlogfile;

        x->plugins[x->num_plugins].func  = xvid->plugin_twopass1;
        x->plugins[x->num_plugins].param = pass1;
        x->num_plugins++;
    }

    /* This is the second pass of a Two pass process */
    if(vob->divxmultipass == 2) {
        xvid_plugin_2pass2_t *pass2 = &mod->pass2;
        xvid_plugin_2pass2_t *pass2cfg = &mod->cfg_pass2;

        if(xvid->plugin_twopass2 == NULL) {
            tc_log_warn(MOD_NAME, "Two Pass #2 bitrate controller plugin not available");
            return;
        }

        memset(pass2, 0, sizeof(xvid_plugin_2pass2_t));
        pass2->version  = XVID_VERSION;
        pass2->filename = vob->divxlogfile;

        /* Apply config file settings if any, or all 0s which lets XviD
         * apply its defaults */
        pass2->keyframe_boost = pass2cfg->keyframe_boost;
        pass2->curve_compression_high = pass2cfg->curve_compression_high;
        pass2->curve_compression_low = pass2cfg->curve_compression_low;
        pass2->overflow_control_strength = pass2cfg->overflow_control_strength;
        pass2->max_overflow_improvement = pass2cfg->max_overflow_improvement;
        pass2->max_overflow_degradation = pass2cfg->max_overflow_degradation;
        pass2->kfreduction = pass2cfg->kfreduction;
        pass2->kfthreshold = pass2cfg->kfthreshold;
        pass2->container_frame_overhead = pass2cfg->container_frame_overhead;

        /* Positive bitrate values are bitrates as usual but if the
         * value is negative it is considered as being a total size
         * to reach (in kilobytes) */
        if(vob->divxbitrate > 0)
            pass2->bitrate  = vob->divxbitrate*1000;
        else
            pass2->bitrate  = vob->divxbitrate;

        x->plugins[x->num_plugins].func  = xvid->plugin_twopass2;
        x->plugins[x->num_plugins].param = pass2;
        x->num_plugins++;
    }

    /* This is a single pass encoding: either a CBR pass or a constant
     * quantizer pass */
    if(vob->divxmultipass == 0  || vob->divxmultipass == 3) {
        xvid_plugin_single_t *onepass = &mod->onepass;
        xvid_plugin_single_t *cfgonepass = &mod->cfg_onepass;

        if(xvid->plugin_onepass == NULL) {
            tc_log_warn(MOD_NAME, "One Pass bitrate controller plugin not available");
            return;
        }

        memset(onepass, 0, sizeof(xvid_plugin_single_t));
        onepass->version = XVID_VERSION;
        onepass->bitrate = vob->divxbitrate*1000;

        /* Apply config file settings if any, or all 0s which lets XviD
         * apply its defaults */
        onepass->reaction_delay_factor = cfgonepass->reaction_delay_factor;
        onepass->averaging_period = cfgonepass->averaging_period;
        onepass->buffer = cfgonepass->buffer;

        /* Quantizer mode uses the same plugin, we have only to define
         * a constant quantizer zone beginning at frame 0 */
        if(vob->divxmultipass == 3) {
            x->zones[x->num_zones].mode      = XVID_ZONE_QUANT;
            x->zones[x->num_zones].frame     = 1;
            x->zones[x->num_zones].increment = vob->divxbitrate;
            x->zones[x->num_zones].base      = 1;
            x->num_zones++;
        }


        x->plugins[x->num_plugins].func  = xvid->plugin_onepass;
        x->plugins[x->num_plugins].param = onepass;
        x->num_plugins++;
    }

    return;
}

static void set_frame_struct(xvid_transcode_module_t *mod, vob_t *vob, transfer_t *t)
{
    xvid_enc_frame_t *x    = &mod->xvid_enc_frame;
    xvid_enc_frame_t *xcfg = &mod->cfg_frame;

    memset(x, 0, sizeof(xvid_enc_frame_t));
    x->version = XVID_VERSION;

    /* Bind output buffer */
    x->bitstream = mod->stream;

    if (t == NULL) {
        x->length          = -1;
        x->input.csp       = XVID_CSP_NULL;
        x->input.plane[0]  = NULL;
//        x->input.plane[1]  = NULL;
//        x->input.plane[2]  = NULL;
        x->input.stride[0] = 0;
//        x->input.stride[1] = 0;
//        x->input.stride[2] = 0;
    } else {
        x->length    = mod->stream_size;

        /* Bind source frame */
        x->input.plane[0] = t->buffer;
        if(vob->im_v_codec == CODEC_RGB) {
            x->input.csp       = XVID_CSP_BGR;
            x->input.stride[0] = vob->ex_v_width*3;
        } else if (vob->im_v_codec == CODEC_YUV422) {
            x->input.csp       = XVID_CSP_UYVY;
            x->input.stride[0] = vob->ex_v_width*2;
        } else {
            x->input.csp       = XVID_CSP_I420;
            x->input.stride[0] = vob->ex_v_width;
        }
    }

    /* Set up core's VOL level features */
    x->vol_flags = xcfg->vol_flags;

    /* Set up core's VOP level features */
    x->vop_flags = xcfg->vop_flags;

    /* Frame type -- let core decide for us */
    x->type = xcfg->type;

    /* Force the right quantizer -- It is internally managed by RC
     * plugins */
    x->quant = 0;

    /* Set up motion estimation flags */
    x->motion = xcfg->motion;

    /* We don't use special matrices */
    x->quant_intra_matrix = xcfg->quant_intra_matrix;
    x->quant_inter_matrix = xcfg->quant_inter_matrix;

    /* pixel aspect ratio
     * transcode.c uses 0 for EXT instead of 15 */
    if ((vob->ex_par==0) &&
        (vob->ex_par_width==1) && (vob->ex_par_height==1))
        vob->ex_par = 1;

    x->par = (vob->ex_par==0)? XVID_PAR_EXT: vob->ex_par;
    x->par_width = vob->ex_par_width;
    x->par_height = vob->ex_par_height;

    return;
}

/*****************************************************************************
 * Returns an error string corresponding to the XviD err code
 ****************************************************************************/

static const char *errorstring(int err)
{
    char *error;
    switch(err) {
    case XVID_ERR_FAIL:
        error = "General fault";
        break;
    case XVID_ERR_MEMORY:
        error =  "Memory allocation error";
        break;
    case XVID_ERR_FORMAT:
        error =  "File format error";
        break;
    case XVID_ERR_VERSION:
        error =  "Structure version not supported";
        break;
    case XVID_ERR_END:
        error =  "End of stream reached";
        break;
    default:
        error = "Unknown";
    }

    return((const char *)error);
}

/*****************************************************************************
 * Read and print a matrix file
 ****************************************************************************/

static void *read_matrix(const char *filename)
{
    int i;
    unsigned char *matrix;
    FILE *input;

    /* Allocate matrix space */
    if((matrix = malloc(64*sizeof(unsigned char))) == NULL)
       return(NULL);

    /* Open the matrix file */
    if((input = fopen(filename, "rb")) == NULL) {
        tc_log_warn(MOD_NAME,
            "Error opening the matrix file %s",
            filename);
        free(matrix);
        return(NULL);
    }

    /* Read the matrix */
    for(i=0; i<64; i++) {

        int value;

        /* If fscanf fails then get out of the loop */
        if(fscanf(input, "%d", &value) != 1) {
            tc_log_warn(MOD_NAME,
                "Error reading the matrix file %s",
                filename);
            free(matrix);
            fclose(input);
            return(NULL);
        }

        /* Clamp the value to safe range */
        value     = (value<  1)?1  :value;
        value     = (value>255)?255:value;
        matrix[i] = value;
    }

    /* Fills the rest with 1 */
    while(i<64) matrix[i++] = 1;

    /* We're done */
    fclose(input);

    return(matrix);

}

static void print_matrix(unsigned char *matrix)
{
    int i;
    for(i=0; i < 64; i+=8) {
        tc_log_info(MOD_NAME,
            "%3d %3d %3d %3d "
            "%3d %3d %3d %3d",
            (int)matrix[i], (int)matrix[i+1],
            (int)matrix[i+2], (int)matrix[i+3],
            (int)matrix[i+4], (int)matrix[i+5],
            (int)matrix[i+6], (int)matrix[i+7]);
    }

    return;
}

/*****************************************************************************
 * Un/Loading XviD shared lib and symbols
 ****************************************************************************/

static int load_xvid(xvid_module_t *xvid, const char *path)
{
    const char *error;
    char soname[4][4096];
    int i;

    /* Reset pointers */
    memset(xvid, 0, sizeof(xvid[0]));

    /* First we build all sonames we will try to load */
#ifdef OS_DARWIN
    tc_snprintf(soname[0], 4095, "%s/%s.%d.%s", path, XVID_SHARED_LIB_BASE,
                XVID_API_MAJOR(XVID_API), XVID_SHARED_LIB_SUFX);
#else
    tc_snprintf(soname[0], 4095, "%s/%s.%s.%d", path, XVID_SHARED_LIB_BASE,
                XVID_SHARED_LIB_SUFX, XVID_API_MAJOR(XVID_API));
#endif
#ifdef OS_DARWIN
    tc_snprintf(soname[1], 4095, "%s.%d.%s", XVID_SHARED_LIB_BASE,
                XVID_API_MAJOR(XVID_API), XVID_SHARED_LIB_SUFX);
#else
    tc_snprintf(soname[1], 4095, "%s.%s.%d", XVID_SHARED_LIB_BASE,
                XVID_SHARED_LIB_SUFX, XVID_API_MAJOR(XVID_API));
#endif
    tc_snprintf(soname[2], 4095, "%s/%s.%s", path, XVID_SHARED_LIB_BASE,
                XVID_SHARED_LIB_SUFX);
    tc_snprintf(soname[3], 4095, "%s.%s", XVID_SHARED_LIB_BASE,
                XVID_SHARED_LIB_SUFX);

    /* Let's try each shared lib until success */
    for(i=0; i<4; i++) {
        if(verbose_flag & TC_DEBUG)
            tc_log_info(MOD_NAME, "Trying to load shared lib %s",
                soname[i]);

        /* Try loading the shared lib */
        xvid->so = dlopen(soname[i], RTLD_GLOBAL| RTLD_LAZY);

        /* Test wether loading succeeded */
        if(xvid->so != NULL)
            break;
    }

    /* None of the modules were available */
    if(xvid->so == NULL) {
        tc_log_warn(MOD_NAME, "No libxvidcore API4 found");
        return(-1);
    }

    if(verbose_flag & TC_DEBUG)
        tc_log_info(MOD_NAME, "Loaded %s", soname[i]);

    /* Next step is to load xvidcore symbols
     *
     * Some of them are mandatory, others like plugins can be safely
     * ignored if they are not available, this will just restrict user
     * available functionnality -- Up to the upper layer to handle these
     * functionnality restrictions */

    /* Mandatory symbol */
    xvid->global = dlsym(xvid->so, "xvid_global");

    if(xvid->global == NULL && (error = dlerror()) != NULL) {
        tc_log_warn(MOD_NAME, "Error loading symbol (%s)", error);
        tc_log_warn(MOD_NAME, "Library \"%s\" looks like an old "
                      "version of libxvidcore", soname[i]);
        tc_log_warn(MOD_NAME, "You cannot use this module with this"
                      " lib; maybe -y xvid2 works");
        return(-1);
    }

    /* Mandatory symbol */
    xvid->encore = dlsym(xvid->so, "xvid_encore");

    if(xvid->encore == NULL && (error = dlerror()) != NULL) {
        tc_log_warn(MOD_NAME, "Error loading symbol (%s)", error);
        return(-1);
    }

    /* Optional plugin symbols */
    xvid->plugin_onepass     = dlsym(xvid->so, "xvid_plugin_single");
    xvid->plugin_twopass1    = dlsym(xvid->so, "xvid_plugin_2pass1");
    xvid->plugin_twopass2    = dlsym(xvid->so, "xvid_plugin_2pass2");
    xvid->plugin_lumimasking = dlsym(xvid->so, "xvid_plugin_lumimasking");

    return(0);
}

static int unload_xvid(xvid_module_t *xvid)
{
    if(xvid->so != NULL) {
        dlclose(xvid->so);
        memset(xvid, 0, sizeof(xvid[0]));
    }

    return(0);
}

/*
 * Please do not modify the tag line.
 *
 * arch-tag: 16c618a5-6cda-4c95-a418-602fc4837824 export_xvid module
 */
