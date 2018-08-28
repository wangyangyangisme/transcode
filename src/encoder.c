/*
 *  encoder.c -- transcode export layer module, implementation.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - January 2006
 *  New rotation code written by
 *  Francesco Romani - May 2006
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

#define SUPPORT_OLD_ENCODER  // for now

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "transcode.h"
#include "framebuffer.h"
#include "filter.h"
#include "counter.h"
#include "video_trans.h"
#include "audio_trans.h"
#include "decoder.h"
#include "encoder.h"
#include "frame_threads.h"

#include "libtc/tcframes.h"

#include <stdint.h>

/*************************************************************************/
/* Our data structure forward declaration                                */

typedef struct tcrotatecontext_ TCRotateContext;
typedef struct tcencoderdata_ TCEncoderData;

/*************************************************************************/
/* private function prototypes                                           */

/* new-style rotation support */
static void tc_rotate_init(TCRotateContext *rotor,
                           const char *video_base_name,
                           const char *audio_base_name);

static void tc_rotate_set_frames_limit(TCRotateContext *rotor,
                                       vob_t *vob, uint32_t frames);
static void tc_rotate_set_bytes_limit(TCRotateContext *rotor,
                                      vob_t *vob, uint64_t bytes);

static void tc_rotate_output_name(TCRotateContext *rotor, vob_t *vob);

static int tc_rotate_if_needed_null(TCRotateContext *rotor,
                                    vob_t *vob, uint32_t bytes);
static int tc_rotate_if_needed_by_frames(TCRotateContext *rotor,
                                         vob_t *vob, uint32_t bytes);
static int tc_rotate_if_needed_by_bytes(TCRotateContext *rotor,
                                        vob_t *vob, uint32_t bytes);

/* old-style rotation support is all public, see transcode.h */

/* new-style encoder */

static int encoder_export(TCEncoderData *data, vob_t *vob);
static void encoder_skip(TCEncoderData *data);
static int encoder_flush(TCEncoderData *data);

/* rest of API is already public */

/* old-style encoder */
#ifdef SUPPORT_OLD_ENCODER

static int OLD_tc_export_setup(vob_t *vob,
                            const char *a_mod, const char *v_mod);
static void OLD_tc_export_shutdown(void);
static int OLD_tc_encoder_init(vob_t *vob);
static int OLD_tc_encoder_open(vob_t *vob);
static int OLD_tc_encoder_close(void);
static int OLD_tc_encoder_stop(void);
static int OLD_encoder_export(TCEncoderData *data, vob_t *vob);

#endif  // SUPPORT_OLD_ENCODER

/* misc helpers */
static int need_stop(TCEncoderData *encdata);
static int is_last_frame(TCEncoderData *encdata, int cluster_mode);
static void export_update_formats(vob_t *vob, const TCModuleInfo *vinfo,
                                  const TCModuleInfo *ainfo);
static int alloc_buffers(TCEncoderData *data);
static void free_buffers(TCEncoderData *data);


/*************************************************************************/

/*
 * new encoder module design principles
 * 1) keep it simple, stupid
 * 2) to have more than one encoder doesn't make sense in transcode, so
 * 3) new encoder will be monothread, like the old one
 */

/*************************************************************************/
/*************************************************************************/

/*
 * new-style output rotation support. Always avalaible, but
 * only new code is supposed to use it.
 * This code is private since only encoder code it's supposed
 * to use it. If this change, this code will be put in a
 * separate .c/.h pair.
 *
 * The tricky part of this code it's mainly the
 * vob->{video,audio}_out_file mangling.
 * This it's still done mainly for legacy reasons.
 * After every rotation, such fields will be updated to point
 * not to real initialization data, but to private buffers of (a)
 * TCRotateContext strucutre. This can hardly seen as good, and
 * should be changed/improved in future releases.
 * Anyway, original values of mentioned field isn't lost since it
 * will be stored in TCRotateContext.{video,audio}_base_name.
 * ------------------------------------------------------------
 */

/*
 * TCExportRotate:
 *    Generic function called after *every* frame was encoded.
 *    Rotate output file(s) if condition incapsulate in specific
 *    functions is satisfied.
 *
 * Parameters:
 *    rotor: TCRotateContext to use to check condition.
 *      vob: pointer to vob_t structure to update with new
 *           export file(s) name after succesfull rotation.
 *    bytes: total size of byte encoded (Audio + Video) in last
 *           rencoding loop.
 * Return value:
 *    TC_OK: successful.
 *    TC_ERROR: error happened and notified using tc_log*().
 *
 *    Of course no error can happen if rotating condition isn't met
 *    (so no rotation it's supposed to happen).
 *    Please note that caller code CANNOT know when rotation happens:
 *    This is a feature, not a bug! Having rotation policy incapsulated
 *    into this code and rotation machinery transparent to caller
 *    it's EXACTLY the purpose oft this code! :)
 */
typedef int (*TCExportRotate)(TCRotateContext *rotor, vob_t *vob,
                              uint32_t bytes);


struct tcrotatecontext_ {
    char video_path_buf[PATH_MAX+1];
    char audio_path_buf[PATH_MAX+1];
    const char *video_base_name;
    const char *audio_base_name;
    uint32_t chunk_num;
    int null_flag;

    uint32_t chunk_frames;

    uint64_t encoded_bytes;
    uint64_t chunk_bytes;

    TCExportRotate rotate_if_needed;
};

/*************************************************************************/


/* macro goody for output rotation request */
#define TC_ROTATE_IF_NEEDED(rotor, vob, bytes) \
    ((rotor)->rotate_if_needed((rotor), (vob), bytes))

/*
 * tc_rotate_init:
 *    initialize a TCRotateContext with given basenames both for
 *    audio and video output files.
 *    Uses null rotation function as default rotation function:
 *    this means that output rotation just never happen.
 *
 * Parameters:
 *              rotor: pointer to a TCRotateContext structure to
 *                     initialize.
 *    video_base_name: basename for main export file (Audio + Video).
 *    audio_base_name: basename for auxiliary export file
 *                     (separate audio track).
 * Return value:
 *    None.
 */
static void tc_rotate_init(TCRotateContext *rotor,
                           const char *video_base_name,
                           const char *audio_base_name)
{
    if (rotor != NULL) {
        memset(rotor, 0, sizeof(TCRotateContext));
        rotor->video_base_name = video_base_name;
        rotor->audio_base_name = audio_base_name;
        if (video_base_name == NULL || strlen(video_base_name) == 0
         || strcmp(video_base_name, "/dev/null") == 0) {
            rotor->null_flag = TC_TRUE;
        } else {
            rotor->null_flag = TC_FALSE;
            strlcpy(rotor->video_path_buf, video_base_name,
                    sizeof(rotor->video_path_buf));
            /*
             * FIXME: Yep, this taste like a duplicate.
             * The whole *_out_file thing need a deep review,
             * but I want to go a little ahead with the whole
             * NMS-powered export layer and write a few more
             * NMS export modules before to go with this. -- FR
             */
            if (audio_base_name == NULL || strlen(audio_base_name) == 0
              || strcmp(audio_base_name, video_base_name) == 0
              || strcmp(audio_base_name, "/dev/null") == 0) {
               /*
                * DO NOT separate export audio track, use the same
                * export file both for audio and for video
                */
                strlcpy(rotor->audio_path_buf, video_base_name,
                        sizeof(rotor->audio_path_buf));
            } else {
                /* separate audio file */
                strlcpy(rotor->audio_path_buf, audio_base_name,
                        sizeof(rotor->audio_path_buf));
            }
        }
        rotor->rotate_if_needed = tc_rotate_if_needed_null;
    }
}

/*
 * tc_rotate_set {frames,bytes}_limit:
 *    setup respecitvely frames and bytes limit for each output chunk.
 *    When calling this function user ask for rotation, so they also
 *    directly updates vob.{video,audio}_out_file so even first
 *    tc_encoder_open() later call will uses names of the right format
 *    (i.e. with the same layout of second and further chunks).
 *    This is done in order to avoid any later rename() and disomogeneities
 *    in output file name as experienced in transcode 1.0.x and before.
 *
 *    Calling this functions multiple times will not hurt anything,
 *    but only the last limit set will be honoured. In other words,
 *    it's impossible (yet) to limit output BOTH for frames and for size.
 *    This may change in future releases.
 *
 * Parameters:
 *    rotor: TCRotateContext to set limit on.
 *      vob: pointer to vob structure to update.
 *   frames: frame limit for each output chunk.
 *    bytes: size limit for each output chunk.
 * Return value:
 *    None
 * Side effects:
 *    vob parameter will be updated. Modified fields:
 *    video_out_file, audio_out_file.
 */

#define PREPARE_OUTPUT_NAME(rotor, vob) \
    if ((rotor)->chunk_num == 0) \
        tc_rotate_output_name((rotor), (vob))

static void tc_rotate_set_frames_limit(TCRotateContext *rotor,
                                       vob_t *vob, uint32_t frames)
{
    if (rotor != NULL && !rotor->null_flag) {
        rotor->chunk_frames = frames;
        rotor->rotate_if_needed = tc_rotate_if_needed_by_frames;
        PREPARE_OUTPUT_NAME(rotor, vob);
    }
}

static void tc_rotate_set_bytes_limit(TCRotateContext *rotor,
                                      vob_t *vob, uint64_t bytes)
{
    if (rotor != NULL && !rotor->null_flag) {
        rotor->chunk_bytes = bytes;
        rotor->rotate_if_needed = tc_rotate_if_needed_by_bytes;
        PREPARE_OUTPUT_NAME(rotor, vob);
    }
}

#undef PREPARE_OUTPUT_NAME

/* helpers ***************************************************************/

/*
 * all rotation helpers uses at least if()s as possible, so we must
 * drop paranoia here
 */

/* 
 * tc_rotate_output_name:
 *    make names of new main/auxiliary output file chunk and updates
 *    vob fields accordingly.
 *
 * Parameters:
 *    rotor: TCRotateContext to use to make new output name(s).
 *      vob: pointer to vob_t structure to update.
 * Return value:
 *    none.
 */

/* pretty naif, yet. */
/* FIXME: OK, we must deeply review the whole *out-file thing ASAP */
static void tc_rotate_output_name(TCRotateContext *rotor, vob_t *vob)
{
    tc_snprintf(rotor->video_path_buf, sizeof(rotor->video_path_buf),
                "%s-%03i", rotor->video_base_name, rotor->chunk_num);
    tc_snprintf(rotor->audio_path_buf, sizeof(rotor->audio_path_buf),
                "%s-%03i", rotor->audio_base_name, rotor->chunk_num);
    vob->video_out_file = rotor->video_path_buf;
    vob->audio_out_file = rotor->audio_path_buf;
    rotor->chunk_num++;
}

/*************************************************************************/
/*
 * real rotation policy implementations. Rotate output file(s)
 * respectively:
 *  - never (_null)
 *  - when encoded frames reach limit (_by_frames)
 *  - when encoded AND written *bytes* reach limit (_by_bytes).
 *
 * For details see documentation of TCExportRotate above.
 */

#define ROTATE_UPDATE_COUNTERS(bytes) do { \
    rotor->encoded_bytes += (bytes); \
} while (0);

static int tc_rotate_if_needed_null(TCRotateContext *rotor,
                                    vob_t *vob, uint32_t bytes)
{
    ROTATE_UPDATE_COUNTERS(bytes);
    return TC_OK;
}

#define ROTATE_COMMON_CODE(rotor, vob) do { \
    ret = tc_encoder_close(); \
    if (ret != TC_OK) { \
        tc_log_error(__FILE__, "unable to close output stream"); \
        ret = TC_ERROR; \
    } else { \
        tc_rotate_output_name((rotor), (vob)); \
        tc_log_info(__FILE__, "rotating video output stream to %s", \
                               (rotor)->video_path_buf); \
        tc_log_info(__FILE__, "rotating audio output stream to %s", \
                               (rotor)->audio_path_buf); \
        ret = tc_encoder_open((vob)); \
        if (ret != TC_OK) { \
            tc_log_error(__FILE__, "unable to reopen output stream"); \
            ret = TC_ERROR; \
        } \
    } \
} while (0)


static int tc_rotate_if_needed_by_frames(TCRotateContext *rotor,
                                         vob_t *vob, uint32_t bytes)
{
    int ret = TC_OK;
    ROTATE_UPDATE_COUNTERS(bytes);

    if (tc_get_frames_encoded() >= rotor->chunk_frames) {
        ROTATE_COMMON_CODE(rotor, vob);
    }
    return ret;
}

static int tc_rotate_if_needed_by_bytes(TCRotateContext *rotor,
                                        vob_t *vob, uint32_t bytes)
{
    int ret = TC_OK;
    ROTATE_UPDATE_COUNTERS(bytes);

    if (rotor->encoded_bytes >= rotor->chunk_bytes) {
        ROTATE_COMMON_CODE(rotor, vob);
    }
    return ret;
}

#undef ROTATE_COMMON_CODE
#undef ROTATE_UPDATE_COUNTERS

/*************************************************************************/
/*************************************************************************/
/* real encoder code                                                     */


struct tcencoderdata_ {
    /* flags, used internally */
    int error_flag;
    int fill_flag;

    /* frame boundaries */
    int frame_first; // XXX
    int frame_last; // XXX
    /* needed by encoder_skip */
    int saved_frame_last; // XXX

    int this_frame_last; // XXX
    int old_frame_last; // XXX

    TCEncoderBuffer *buffer;

    vframe_list_t *venc_ptr;
    aframe_list_t *aenc_ptr;

    TCFactory factory;

    TCModule vid_mod;
    TCModule aud_mod;
    TCModule mplex_mod;

    TCRotateContext rotor_data;

#ifdef SUPPORT_OLD_ENCODER
    transfer_t export_para;

    void *ex_a_handle;
    void *ex_v_handle;
#endif
};

static TCEncoderData encdata = {
    .error_flag = 0,
    .fill_flag = 0,
    .frame_first = 0,
    .frame_last = 0,
    .saved_frame_last = 0,
    .this_frame_last = 0,
    .old_frame_last = 0,
    .buffer = NULL,
    .venc_ptr = NULL,
    .aenc_ptr = NULL,
    .factory = NULL,
    .vid_mod = NULL,
    .aud_mod = NULL,
    .mplex_mod = NULL,
    /* rotor_data explicitely initialized later */
#ifdef SUPPORT_OLD_ENCODER
    .ex_a_handle = NULL,
    .ex_v_handle = NULL,
#endif
};


#define RESET_ATTRIBUTES(ptr)   do { \
        (ptr)->attributes = 0; \
} while (0)

/*
 * is_last_frame:
 *      check if current frame it's supposed to be the last one in
 *      encoding frame range. Catch all all known special cases
 * 
 * Parameters:
 *           encdata: fetch current frame id from this structure reference.
 *      cluster_mode: boolean flag. When in cluster mode we need to take
 *                    some special care.
 * Return value:
 *     !0: current frame is supposed to be the last one
 *      0: otherwise
 */
static int is_last_frame(TCEncoderData *encdata, int cluster_mode)
{
    int fid = encdata->buffer->frame_id;
    if (cluster_mode) {
        fid -= tc_get_frames_dropped();
    }

    if ((encdata->buffer->vptr->attributes & TC_FRAME_IS_END_OF_STREAM
      || encdata->buffer->aptr->attributes & TC_FRAME_IS_END_OF_STREAM)) {
        /* `consume' the flag(s) */
        encdata->buffer->vptr->attributes &= ~TC_FRAME_IS_END_OF_STREAM;
        encdata->buffer->aptr->attributes &= ~TC_FRAME_IS_END_OF_STREAM;
        return 1;
    }
    return (fid == encdata->frame_last);
}

/*
 * export_update_formats:
 *      coerce exported formats to the default ones from the loaded
 *      encoder modules IF AND ONLY IF user doesn't have requested
 *      specific ones.
 *
 *      That's a temporary workaround until we have a full-NMS
 *      export layer.
 *
 * Parameters:
 *        vob: pointer to vob_t structure to update.
 *      vinfo: pointer to TCModuleInfo of video encoder module.
 *      ainfo: pointer to TCModuleInfo of audio encoder module.
 * Return value:
 *      None
 */
static void export_update_formats(vob_t *vob, const TCModuleInfo *vinfo,
                                  const TCModuleInfo *ainfo)
{
    if (vob == NULL || vinfo == NULL || ainfo == NULL) {
        /* should never happen */
        tc_log_error(__FILE__, "missing export formats references");
    }
    /* 
     * OK, that's pretty hackish since export_attributes should
     * go away in near future. Neverthless, ex_a_codec features
     * a pretty unuseful default (CODEC_MP3), so we don't use
     * such default value to safely distinguish between -N given
     * or not given.
     * And so we must use another flag, and export_attributes are
     * the simplest things that work, now/
     */
    if (!(vob->export_attributes & TC_EXPORT_ATTRIBUTE_VCODEC)) {
        vob->ex_v_codec = vinfo->codecs_out[0];
    }
    if (!(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ACODEC)) {
        vob->ex_a_codec = ainfo->codecs_out[0];
    }
}

/* ------------------------------------------------------------
 *
 * export init
 *
 * ------------------------------------------------------------*/

int tc_export_init(TCEncoderBuffer *buffer, TCFactory factory)
{
    if (buffer == NULL) {
        tc_log_error(__FILE__, "missing encoder buffer reference");
        return TC_ERROR;
    }
    encdata.buffer = buffer;

#ifndef SUPPORT_OLD_ENCODER  // factory==NULL to signal use of old code
    if (factory == NULL) {
        tc_log_error(__FILE__, "missing factory reference");
        return TC_ERROR;
    }
#endif
    encdata.factory = factory;
    return TC_OK;
}

int tc_export_setup(vob_t *vob,
                 const char *a_mod, const char *v_mod, const char *m_mod)
{
    int match = 0;
    const char *mod_name = NULL;

#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return OLD_tc_export_setup(vob, a_mod, v_mod);
#endif

    if (verbose >= TC_DEBUG) {
        tc_log_info(__FILE__, "loading export modules");
    }

    mod_name = (a_mod == NULL) ?TC_DEFAULT_EXPORT_AUDIO :a_mod;
    encdata.aud_mod = tc_new_module(encdata.factory, "encode", mod_name, TC_AUDIO);
    if (!encdata.aud_mod) {
        tc_log_error(__FILE__, "can't load audio encoder");
        return TC_ERROR;
    }
    mod_name = (v_mod == NULL) ?TC_DEFAULT_EXPORT_VIDEO :v_mod;
    encdata.vid_mod = tc_new_module(encdata.factory, "encode", mod_name, TC_VIDEO);
    if (!encdata.vid_mod) {
        tc_log_error(__FILE__, "can't load video encoder");
        return TC_ERROR;
    }
    mod_name = (m_mod == NULL) ?TC_DEFAULT_EXPORT_MPLEX :m_mod;
    encdata.mplex_mod = tc_new_module(encdata.factory, "multiplex", mod_name,
                                      TC_VIDEO|TC_AUDIO);
    if (!encdata.mplex_mod) {
        tc_log_error(__FILE__, "can't load multiplexor");
        return TC_ERROR;
    }
    export_update_formats(vob, tc_module_get_info(encdata.vid_mod),
                               tc_module_get_info(encdata.aud_mod));

    match = tc_module_match(vob->ex_a_codec,
                            encdata.aud_mod, encdata.mplex_mod);
    if (!match) {
        tc_log_error(__FILE__, "audio encoder incompatible "
                               "with multiplexor");
        return TC_ERROR;
    }
    match = tc_module_match(vob->ex_v_codec,
                            encdata.vid_mod, encdata.mplex_mod);
    if (!match) {
        tc_log_error(__FILE__, "video encoder incompatible "
                               "with multiplexor");
        return TC_ERROR;
    }
    tc_rotate_init(&encdata.rotor_data,
                   vob->video_out_file, vob->audio_out_file);

    return TC_OK;
}

/* ------------------------------------------------------------
 *
 * export close, unload modules
 *
 * ------------------------------------------------------------*/

void tc_export_shutdown(void)
{
#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return OLD_tc_export_shutdown();
#endif

    if (verbose >= TC_DEBUG) {
        tc_log_info(__FILE__, "unloading export modules");
    }

    tc_del_module(encdata.factory, encdata.mplex_mod);
    tc_del_module(encdata.factory, encdata.vid_mod);
    tc_del_module(encdata.factory, encdata.aud_mod);
}


/* ------------------------------------------------------------
 *
 * encoder init
 *
 * ------------------------------------------------------------*/

int tc_encoder_init(vob_t *vob)
{
    int ret;
    const char *options = NULL;

#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return OLD_tc_encoder_init(vob);
#endif

    ret = alloc_buffers(&encdata);
    if (ret != TC_OK) {
        tc_log_error(__FILE__, "can't allocate encoder buffers");
        return TC_ERROR;
    }

    options = (vob->ex_v_string) ?vob->ex_v_string :"";
    ret = tc_module_configure(encdata.vid_mod, options, vob);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "video export module error: init failed");
        return TC_ERROR;
    }

    options = (vob->ex_a_string) ?vob->ex_a_string :"";
    ret = tc_module_configure(encdata.aud_mod, options, vob);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "audio export module error: init failed");
        return TC_ERROR;
    }

    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encoder open
 *
 * ------------------------------------------------------------*/

int tc_encoder_open(vob_t *vob)
{
    int ret;
    const char *options = NULL;

#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return OLD_tc_encoder_open(vob);
#endif

    options = vob->ex_m_string ? vob->ex_m_string : "";
    ret = tc_module_configure(encdata.mplex_mod, options, vob);
    if (ret == TC_ERROR) {
        tc_log_warn(__FILE__, "multiplexor module error: init failed");
        return TC_ERROR;
    }

    // XXX
    tc_module_pass_extradata(encdata.vid_mod, encdata.mplex_mod);

    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encoder close
 *
 * ------------------------------------------------------------*/

int tc_encoder_close(void)
{
    int ret;

#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return OLD_tc_encoder_close();
#endif

    /* old style code handle flushing in modules, not here */
    ret = encoder_flush(&encdata);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "error while closing encoder: flush failed");
        return TC_ERROR;
    }

    ret = tc_module_stop(encdata.mplex_mod);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "multiplexor module error: stop failed");
        return TC_ERROR;
    }

    if(verbose >= TC_DEBUG) {
        tc_log_info(__FILE__, "encoder closed");
    }
    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encoder stop
 *
 * ------------------------------------------------------------*/

int tc_encoder_stop(void)
{
    int ret;

#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return OLD_tc_encoder_stop();
#endif

    ret = tc_module_stop(encdata.vid_mod);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "video export module error: stop failed");
        return TC_ERROR;
    }

    ret = tc_module_stop(encdata.aud_mod);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "audio export module error: stop failed");
        return TC_ERROR;
    }

    free_buffers(&encdata);

    if(verbose >= TC_DEBUG) {
        tc_log_info(__FILE__, "encoder stopped");
    }
    return TC_OK;
}

/* ------------------------------------------------------------
 *
 * encoder main loop helpers
 *
 * ------------------------------------------------------------*/

static int alloc_buffers(TCEncoderData *data)
{
    data->venc_ptr = vframe_alloc_single();
    if (data->venc_ptr == NULL) {
        goto no_vframe;
    }
    data->aenc_ptr = aframe_alloc_single();
    if (data->aenc_ptr == NULL) {
        goto no_aframe;
    }
    return TC_OK;

no_aframe:
    tc_del_video_frame(data->venc_ptr);
no_vframe:
    return TC_ERROR;
}

static void free_buffers(TCEncoderData *data)
{
    tc_del_video_frame(data->venc_ptr);
    tc_del_audio_frame(data->aenc_ptr);
}

/*
 * NOTE about counter/condition/mutex handling inside various
 * encoder helpers.
 *
 * Code are still a little bit confusing since things aren't
 * updated or used at the same function level.
 * Code works, but isn't still well readable.
 * We need stil more cleanup and refactoring for future releases.
 */


/*
 * dispatch the acquired frames to encoder modules, and adjust frame counters
 */
static int encoder_export(TCEncoderData *data, vob_t *vob)
{
    int video_delayed = 0;
    int ret;

#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return OLD_encoder_export(data, vob);
#endif
    /* remove spurious attributes */
    RESET_ATTRIBUTES(data->venc_ptr);
    RESET_ATTRIBUTES(data->aenc_ptr);

    /* step 1: encode video */
    ret = tc_module_encode_video(data->vid_mod,
                                 data->buffer->vptr, data->venc_ptr);
    if (ret != TC_OK) {
        tc_log_error(__FILE__, "error encoding video frame");
        data->error_flag = 1;
    }
    if (data->venc_ptr->attributes & TC_FRAME_IS_DELAYED) {
        data->venc_ptr->attributes &= ~TC_FRAME_IS_DELAYED;
        video_delayed = 1;
    }

    /* step 2: encode audio */
    if (video_delayed) {
        data->buffer->aptr->attributes |= TC_FRAME_IS_CLONED;
        tc_log_info(__FILE__, "Delaying audio");
    } else {
        ret = tc_module_encode_audio(data->aud_mod,
                                     data->buffer->aptr, data->aenc_ptr);
        if (ret != TC_OK) {
            tc_log_error(__FILE__, "error encoding audio frame");
            data->error_flag = 1;
        }
    }

    /* step 3: multiplex and rotate */
    // FIXME: Do we really need bytes-written returned from this, or can
    //        we just return TC_OK/TC_ERROR like other functions? --AC
    ret = tc_module_multiplex(data->mplex_mod,
                              data->venc_ptr, data->aenc_ptr);
    if (ret < 0) {
        tc_log_error(__FILE__, "error multiplexing encoded frames");
        data->error_flag = 1;
    }
    data->error_flag = TC_ROTATE_IF_NEEDED(&encdata.rotor_data, vob, ret);

    /* step 4: show and update stats */
    if (tc_progress_meter) {
        int last = (data->frame_last == TC_FRAME_LAST)
                        ?(-1) :data->frame_last;
        if (!data->fill_flag) {
            data->fill_flag = 1;
        }
        counter_print(1, data->buffer->frame_id, data->frame_first, last);
    }

    tc_update_frames_encoded(1);
    return (data->error_flag) ?TC_ERROR :TC_OK;
}


#define RETURN_IF_NOT_OK(RET, KIND) do { \
    if ((RET) != TC_OK) { \
        tc_log_error(__FILE__, "error encoding final %s frame", (KIND)); \
        return TC_ERROR; \
    } \
} while (0)

#define RETURN_IF_MUX_ERROR(BYTES) do { \
    if ((BYTES) < 0) { \
        tc_log_error(__FILE__, "error multiplexing final audio frame"); \
        return TC_ERROR; \
    } \
} while (0)
        

/* DO NOT rotate here, this data belongs to current chunk */
static int encoder_flush(TCEncoderData *data)
{
    int ret = TC_ERROR, bytes = 0;

    do {
        RESET_ATTRIBUTES(data->aenc_ptr);
        ret = tc_module_encode_audio(data->aud_mod, NULL, data->aenc_ptr);
        RETURN_IF_NOT_OK(ret, "audio");

        bytes = tc_module_multiplex(data->mplex_mod, NULL, data->aenc_ptr);
    } while (bytes != 0);
    RETURN_IF_MUX_ERROR(bytes);

    do {
        RESET_ATTRIBUTES(data->venc_ptr);
        ret = tc_module_encode_video(data->vid_mod, NULL, data->venc_ptr);
        RETURN_IF_NOT_OK(ret, "video");

        bytes = tc_module_multiplex(data->mplex_mod, data->venc_ptr, NULL);
    } while (bytes != 0);
    RETURN_IF_MUX_ERROR(bytes);

    return TC_OK;
}


void tc_export_rotation_limit_frames(vob_t *vob, uint32_t frames)
{
#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return;
#endif

    tc_rotate_set_frames_limit(&encdata.rotor_data, vob, frames);
}

void tc_export_rotation_limit_megabytes(vob_t *vob, uint32_t megabytes)
{
#ifdef SUPPORT_OLD_ENCODER
    if (!encdata.factory)
        return;
#endif

    tc_rotate_set_bytes_limit(&encdata.rotor_data,
                              vob, megabytes * 1024 * 1024);
}

/*************************************************************************/

#ifdef SUPPORT_OLD_ENCODER

#include "dl_loader.h"

/* ------------------------------------------------------------
 *
 * export init
 *
 * ------------------------------------------------------------*/

static int OLD_tc_export_setup(vob_t *vob,
                            const char *a_mod, const char *v_mod)
{
    const char *mod_name = NULL;

    // load export modules
    mod_name = (a_mod == NULL) ?TC_DEFAULT_EXPORT_AUDIO :a_mod;
    encdata.ex_a_handle = load_module((char*)mod_name, TC_EXPORT + TC_AUDIO);
    if (encdata.ex_a_handle == NULL) {
        tc_log_warn(__FILE__, "loading audio export module failed");
        return TC_ERROR;
   }

    mod_name = (v_mod==NULL) ?TC_DEFAULT_EXPORT_VIDEO :v_mod;
    encdata.ex_v_handle = load_module((char*)mod_name, TC_EXPORT + TC_VIDEO);
    if (encdata.ex_v_handle == NULL) {
        tc_log_warn(__FILE__, "loading video export module failed");
        return TC_ERROR;
    }

    encdata.export_para.flag = verbose;
    tca_export(TC_EXPORT_NAME, &encdata.export_para, NULL);

    if(encdata.export_para.flag != verbose) {
        // module returned capability flag
        int cc=0;

        if (verbose & TC_DEBUG) {
            tc_log_info(__FILE__, "audio capability flag 0x%x | 0x%x",
                                  encdata.export_para.flag, vob->im_a_codec);
        }

        switch (vob->im_a_codec) {
          case CODEC_PCM:
            cc = (encdata.export_para.flag & TC_CAP_PCM);
            break;
          case CODEC_AC3:
            cc = (encdata.export_para.flag & TC_CAP_AC3);
            break;
          case CODEC_RAW:
            cc = (encdata.export_para.flag & TC_CAP_AUD);
            break;
          default:
            cc = 0;
        }

        if (cc == 0) {
            tc_log_warn(__FILE__, "audio codec not supported by export module");
            return TC_ERROR;
        }
    } else { /* encdata.export_para.flag == verbose */
        if (vob->im_a_codec != CODEC_PCM) {
            tc_log_warn(__FILE__, "audio codec not supported by export module");
            return TC_ERROR;
        }
    }

    encdata.export_para.flag = verbose;
    tcv_export(TC_EXPORT_NAME, &encdata.export_para, NULL);

    if (encdata.export_para.flag != verbose) {
        // module returned capability flag
        int cc = 0;

        if (verbose & TC_DEBUG) {
            tc_log_info(__FILE__, "video capability flag 0x%x | 0x%x",
                                  encdata.export_para.flag, vob->im_v_codec);
        }

        switch (vob->im_v_codec) {
          case CODEC_RGB:
            cc = (encdata.export_para.flag & TC_CAP_RGB);
            break;
          case CODEC_YUV:
            cc = (encdata.export_para.flag & TC_CAP_YUV);
            break;
          case CODEC_YUV422:
            cc = (encdata.export_para.flag & TC_CAP_YUV422);
            break;
          case CODEC_RAW:
          case CODEC_RAW_YUV: /* fallthrough */
            cc = (encdata.export_para.flag & TC_CAP_VID);
            break;
          default:
            cc = 0;
        }

        if (cc == 0) {
            tc_log_warn(__FILE__, "video codec not supported by export module");
            return TC_ERROR;
        }
    } else { /* encdata.export_para.flag == verbose */
        if (vob->im_v_codec != CODEC_RGB) {
            tc_log_warn(__FILE__, "video codec not supported by export module");
            return TC_ERROR;
        }
    }

    return TC_OK;
}

/* ------------------------------------------------------------
 *
 * export close, unload modules
 *
 * ------------------------------------------------------------*/

static void OLD_tc_export_shutdown(void)
{
    if (verbose & TC_DEBUG) {
        tc_log_info(__FILE__, "unloading export modules");
    }

    unload_module(encdata.ex_a_handle);
    unload_module(encdata.ex_v_handle);
}


/* ------------------------------------------------------------
 *
 * encoder init
 *
 * ------------------------------------------------------------*/

static int OLD_tc_encoder_init(vob_t *vob)
{
    int ret;

    encdata.export_para.flag = TC_VIDEO;
    ret = tcv_export(TC_EXPORT_INIT, &encdata.export_para, vob);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "video export module error: init failed");
        return TC_ERROR;
    }

    encdata.export_para.flag = TC_AUDIO;
    ret = tca_export(TC_EXPORT_INIT, &encdata.export_para, vob);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "audio export module error: init failed");
        return TC_ERROR;
    }

    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encoder open
 *
 * ------------------------------------------------------------*/

static int OLD_tc_encoder_open(vob_t *vob)
{
    int ret;

    encdata.export_para.flag = TC_VIDEO;
    ret = tcv_export(TC_EXPORT_OPEN, &encdata.export_para, vob);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "video export module error: open failed");
        return TC_ERROR;
    }

    encdata.export_para.flag = TC_AUDIO;
    ret = tca_export(TC_EXPORT_OPEN, &encdata.export_para, vob);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "audio export module error: open failed");
        return TC_ERROR;
    }

    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encoder close
 *
 * ------------------------------------------------------------*/

static int OLD_tc_encoder_close(void)
{
    /* 
     * close, errors not fatal.
     * flushing handled internally by export modules.
     */

    encdata.export_para.flag = TC_AUDIO;
    tca_export(TC_EXPORT_CLOSE, &encdata.export_para, NULL);

    encdata.export_para.flag = TC_VIDEO;
    tcv_export(TC_EXPORT_CLOSE, &encdata.export_para, NULL);

    if(verbose & TC_DEBUG) {
        tc_log_info(__FILE__, "encoder closed");
    }
    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encoder stop
 *
 * ------------------------------------------------------------*/

static int OLD_tc_encoder_stop(void)
{
    int ret;

    encdata.export_para.flag = TC_VIDEO;
    ret = tcv_export(TC_EXPORT_STOP, &encdata.export_para, NULL);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "video export module error: stop failed");
        return TC_ERROR;
    }

    encdata.export_para.flag = TC_AUDIO;
    ret = tca_export(TC_EXPORT_STOP, &encdata.export_para, NULL);
    if (ret != TC_OK) {
        tc_log_warn(__FILE__, "audio export module error: stop failed");
        return TC_ERROR;
    }

    return TC_OK;
}

/*
 * dispatch the acquired frames to encoder modules
 */
static int OLD_encoder_export(TCEncoderData *data, vob_t *vob)
{
    int video_delayed = 0;

    /* encode and export video frame */
    data->export_para.buffer = data->buffer->vptr->video_buf;
    data->export_para.size = data->buffer->vptr->video_size;
    data->export_para.attributes = data->buffer->vptr->attributes;
    if (data->buffer->vptr->attributes & TC_FRAME_IS_KEYFRAME) {
        data->export_para.attributes |= TC_FRAME_IS_KEYFRAME;
    }
    data->export_para.flag = TC_VIDEO;

    if(tcv_export(TC_EXPORT_ENCODE, &data->export_para, vob) < 0) {
        tc_log_warn(__FILE__, "error encoding video frame");
        data->error_flag = 1;
    }
    /* maybe clone? */
    data->buffer->vptr->attributes = data->export_para.attributes;
    if (data->buffer->vptr->attributes & TC_FRAME_IS_DELAYED) {
        data->buffer->vptr->attributes &= ~TC_FRAME_IS_DELAYED;
        video_delayed = 1;
    }

    /* encode and export audio frame */
    data->export_para.buffer = data->buffer->aptr->audio_buf;
    data->export_para.size = data->buffer->aptr->audio_size;
    data->export_para.attributes = data->buffer->aptr->attributes;
    data->export_para.flag = TC_AUDIO;

    if(video_delayed > 0) {
        data->buffer->aptr->attributes |= TC_FRAME_IS_CLONED;
        tc_log_info(__FILE__, "Delaying audio");
    } else {
        if (tca_export(TC_EXPORT_ENCODE, &data->export_para, vob) < 0) {
            tc_log_warn(__FILE__, "error encoding audio frame");
            data->error_flag = 1;
        }

        /* maybe clone? */
        data->buffer->aptr->attributes = data->export_para.attributes;
    }

    if (tc_progress_meter) {
        int last = (data->frame_last == TC_FRAME_LAST) ?(-1) :data->frame_last;
        if (!data->fill_flag) {
            data->fill_flag = 1;
        }
        counter_print(1, data->buffer->frame_id, data->frame_first, last);
    }

    // XXX: _always_ update?
    tc_update_frames_encoded(1);
    return data->error_flag ? TC_ERROR : TC_OK;
}


/************************************************************************* 
 * old style rotation support code.
 * TEMPORARY merged here until it will be deleted together with
 * old encoder code when NMS-powered export layer is ready to switch
 * (read: when we have enough encode/multiplexor module to go).
 *************************************************************************/

#include "libtc/xio.h"

//-----------------------------------------------------------------
//
// r,R - switch output file
//
//-----------------------------------------------------------------

static int rotate_ctr = 0;
static int rotate_flag = 0;
static char *base = NULL;

void tc_outstream_rotate_request(void)
{
    //set flag
    rotate_flag = 1;
}

void tc_outstream_rotate(void)
{
    char buf[TC_BUF_MAX];
    vob_t *vob=tc_get_vob();

    if (!rotate_flag)
        return;

    //reset flag to avoid re-entry
    rotate_flag=0;

    // do not try to rename /dev/null
    if(strcmp(vob->video_out_file, "/dev/null") == 0)
        return;

    // get current outfile basename
    base = tc_strdup(vob->video_out_file);

    //check
    if (base == NULL)
        return;

    // close output
    if (tc_encoder_close()<0)
        tc_error("failed to close output");

    // create new filename
    tc_snprintf(buf, sizeof(buf), "%s-%03d", base, rotate_ctr++);
    //rename old outputfile
    if (xio_rename(base, buf) < 0)
        tc_error("failed to rename output file");

    // reopen output
    if (tc_encoder_open(vob) < 0)
        tc_error("failed to open output");

    tc_log_info(__FILE__, "outfile %s saved to %s", base, buf);
    free(base);
}

/*************************************************************************/

#endif  // SUPPORT_OLD_ENCODER

/*
 * fake encoding, simply adjust frame counters.
 */
static void encoder_skip(TCEncoderData *data)
{
    if (tc_progress_meter) {
        if (!data->fill_flag) {
            data->fill_flag = 1;
        }
        counter_print(0, data->buffer->frame_id, data->saved_frame_last,
                      data->frame_first-1);
    }
    data->buffer->vptr->attributes |= TC_FRAME_IS_OUT_OF_RANGE;
    data->buffer->aptr->attributes |= TC_FRAME_IS_OUT_OF_RANGE;
}

static int need_stop(TCEncoderData *encdata)
{
    return !tc_running() || !tc_import_status() || encdata->error_flag;
}

/* ------------------------------------------------------------
 *
 * encoder main loop
 *
 * ------------------------------------------------------------*/

void tc_encoder_loop(vob_t *vob, int frame_first, int frame_last)
{
    int err = 0, eos = 0; /* End Of Stream flag */

    if (encdata.this_frame_last != frame_last) {
        encdata.old_frame_last  = encdata.this_frame_last;
        encdata.this_frame_last = frame_last;
    }

    encdata.error_flag  = 0; /* reset */
    encdata.frame_first = frame_first;
    encdata.frame_last  = frame_last;
    encdata.saved_frame_last = encdata.old_frame_last;

    while (!need_stop(&encdata)) {
        /* stop here if pause requested */
        tc_pause();

        err = encdata.buffer->acquire_video_frame(encdata.buffer, vob);
        if (err) {
            if (verbose >= TC_DEBUG) {
                tc_log_warn(__FILE__, "failed to acquire next raw video frame");
            }
            break; /* can't acquire video frame */
        }

        err = encdata.buffer->acquire_audio_frame(encdata.buffer, vob);
        if (err) {
            if (verbose >= TC_DEBUG) {
                tc_log_warn(__FILE__, "failed to acquire next raw audio frame");
            }
            break;  /* can't acquire frame */
        }

        eos = is_last_frame(&encdata, tc_cluster_mode);
        if (eos) {
            break;
        }

        /* check frame id */
        if (frame_first <= encdata.buffer->frame_id
          && encdata.buffer->frame_id < frame_last) {
            encoder_export(&encdata, vob);
        } else { /* frame not in range */
            encoder_skip(&encdata);
        } /* frame processing loop */

        /* release frame buffer memory */
        encdata.buffer->dispose_video_frame(encdata.buffer);
        encdata.buffer->dispose_audio_frame(encdata.buffer);
    }
    /* main frame decoding loop */

    if (verbose >= TC_CLEANUP) {
        if (eos) {
            tc_log_info(__FILE__, "encoder last frame finished (%i/%i)",
                        encdata.buffer->frame_id, encdata.frame_last);
        }
        tc_log_info(__FILE__, "export terminated - buffer(s) empty");
    }
    return;
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
