/*
 *  decoder.c -- transcode import layer module, implementation.
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updated and partially rewritten by
 *  Francesco Romani - July 2007
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

#include "transcode.h"
#include "dl_loader.h"
#include "filter.h"
#include "framebuffer.h"
#include "video_trans.h"
#include "audio_trans.h"
#include "decoder.h"
#include "encoder.h"
#include "frame_threads.h"
#include "cmdline.h"
#include "probe.h"


/*************************************************************************/

/* anonymous since used just internally */
enum {
    TC_IM_THREAD_UNKNOWN = -1, /* halting cause not specified      */
    TC_IM_THREAD_DONE = 0,     /* import ends as expected          */
    TC_IM_THREAD_INTERRUPT,    /* external event interrupts import */
    TC_IM_THREAD_EXT_ERROR,    /* external (I/O) error             */
    TC_IM_THREAD_INT_ERROR,    /* internal (core) error            */
    TC_IM_THREAD_PROBE_ERROR,  /* source is incompatible           */
};


typedef struct tcdecoderdata_ TCDecoderData;
struct tcdecoderdata_ {
    const char *tag;            /* audio or video? used for logging */
    FILE *fd;                   /* for stream import                */
    void *im_handle;            /* import module handle             */
    volatile int active_flag;   /* active or not?                   */
    pthread_t thread_id;
    pthread_mutex_t lock;
};


/*************************************************************************/

static TCDecoderData video_decdata = {
    .tag         = "video",
    .fd          = NULL,
    .im_handle   = NULL,
    .active_flag = 0,
    .thread_id   = (pthread_t)0,
    .lock        = PTHREAD_MUTEX_INITIALIZER,
};

static TCDecoderData audio_decdata = {
    .tag         = "audio",
    .fd          = NULL,
    .im_handle   = NULL,
    .active_flag = 0,
    .thread_id   = (pthread_t)0,
    .lock        = PTHREAD_MUTEX_INITIALIZER,
};

static pthread_t tc_pthread_main = (pthread_t)0;
static long int vframecount = 0;
static long int aframecount = 0;


/*************************************************************************/
/*  Old-style compatibility support functions                            */
/*************************************************************************/

struct modpair {
    int codec; /* internal codec/colorspace/format */
    int caps;  /* module capabilities              */
};

static const struct modpair audpairs[] = {
    { CODEC_PCM,     TC_CAP_PCM    },
    { CODEC_AC3,     TC_CAP_AC3    },
    { CODEC_RAW,     TC_CAP_AUD    },
    { CODEC_NULL,    TC_CAP_NONE   } /* end marker, must be the last */
};

static const struct modpair vidpairs[] = {
    { CODEC_RGB,     TC_CAP_RGB    },
    { CODEC_YUV,     TC_CAP_YUV    },
    { CODEC_YUV422,  TC_CAP_YUV422 },
    { CODEC_RAW_YUV, TC_CAP_VID    },
    { CODEC_RAW,     TC_CAP_VID    },
    { CODEC_NULL,    TC_CAP_NONE   } /* end marker, must be the last */
};


/*
 * check_module_caps: verifies if a module is compatible with transcode
 * core colorspace/format settings.
 *
 * Parameters:
 *       param: data describing (old-style) module capabilities.
 *       codec: codec/format/colorspace requested by core.
 *      mpairs: table of formats/capabilities to be used for check.
 * Return Value:
 *       0: module INcompatible with core format request.
 *      !0: module can accomplish to the core format request.
 */
static int check_module_caps(const transfer_t *param, int codec,
                             const struct modpair *mpairs)
{
    int caps = 0;

    if (param->flag == verbose) {
        caps = (codec == mpairs[0].codec);
        /* legacy: grab the first and stay */
    } else {
        int i = 0;

        /* module returned capability flag */
        if (verbose >= TC_DEBUG) {
            tc_log_msg(__FILE__, "Capability flag 0x%x | 0x%x",
                       param->flag, codec);
        }

        for (i = 0; mpairs[i].codec != CODEC_NULL; i++) {
            if (codec == mpairs[i].codec) {
                caps = (param->flag & mpairs[i].caps);
                break;
            }
        }
    }
    return caps;
}

/*************************************************************************/
/*                  optimized block-wise fread                           */
/*************************************************************************/

#ifdef PIPE_BUF
#define BLOCKSIZE PIPE_BUF /* 4096 on linux-x86 */
#else
#define BLOCKSIZE 4096
#endif

static int mfread(uint8_t *buf, int size, int nelem, FILE *f)
{
    int fd = fileno(f);
    int n = 0, r1 = 0, r2 = 0;
    while (n < size*nelem-BLOCKSIZE) {
        if ( !(r1 = read (fd, &buf[n], BLOCKSIZE))) return 0;
        n += r1;
    }
    while (size*nelem-n) {
        if ( !(r2 = read (fd, &buf[n], size*nelem-n)))return 0;
        n += r2;
    }
    return nelem;
}

/*************************************************************************/
/*               some macro goodies                                      */
/*************************************************************************/

#define RETURN_IF_NULL(HANDLE, MEDIA) do { \
    if ((HANDLE) == NULL) { \
        tc_log_error(PACKAGE, "Loading %s import module failed", (MEDIA)); \
        tc_log_error(PACKAGE, \
                     "Did you enable this module when you ran configure?"); \
        return TC_ERROR; \
    } \
} while (0)

#define RETURN_IF_NOT_SUPPORTED(CAPS, MEDIA) do { \
    if (!(CAPS)) { \
        tc_log_error(PACKAGE, "%s format not supported by import module", \
                     (MEDIA)); \
        return TC_ERROR; \
    } \
} while (0)

#define RETURN_IF_FUNCTION_FAILED(func, ...) do { \
    int ret = func(__VA_ARGS__); \
    if (ret != TC_OK) { \
        return TC_ERROR; \
    } \
} while (0)

#define RETURN_IF_REGISTRATION_FAILED(PTR, MEDIA) do { \
    /* ok, that's pure paranoia */ \
    if ((PTR) == NULL) { \
        tc_log_error(__FILE__, "frame registration failed (%s)", (MEDIA)); \
        return TC_IM_THREAD_INT_ERROR; \
    } \
} while (0)

/*************************************************************************/
/*               stream-specific functions                               */
/*************************************************************************/
/*               status handling functions                               */
/*************************************************************************/


/*
 * tc_import_thread_stop (Thread safe): mark the import status flag
 * as `stopped'; the import thread will stop as soon as is possible.
 *
 * Parameters:
 *      decdata: pointer to a TCDecoderData structure representing the
 *               import thread to stop.
 * Return Value:
 *      None
 */
static void tc_import_thread_stop(TCDecoderData *decdata)
{
    pthread_mutex_lock(&decdata->lock);
    decdata->active_flag = TC_FALSE;
    pthread_mutex_unlock(&decdata->lock);
}

/*
 * tc_import_thread_start (Thread safe): mark the import status flag
 * as `started'; import thread become running and it starts producing data.
 *
 * Parameters:
 *      decdata: pointer to a TCDecoderData structure representing the
 *               import thread to start.
 * Return Value:
 *      None
 */
static void tc_import_thread_start(TCDecoderData *decdata)
{
    pthread_mutex_lock(&decdata->lock);
    decdata->active_flag = TC_TRUE;
    pthread_mutex_unlock(&decdata->lock);
}

/*
 * tc_import_thread_is_active (Thread safe): poll for the current
 * status flag of an import thread.
 *
 * Parameters:
 *      decdata: pointer to a TCDecoderData structure representing the
 *               import thread to query.
 * Return Value:
 *      TC_FALSE: import thread is stopped or stopping.
 *      TC_TRUE:  import thread is running.
 */

static int tc_import_thread_is_active(TCDecoderData *decdata)
{
    int flag;
    pthread_mutex_lock(&decdata->lock);
    flag = decdata->active_flag;
    pthread_mutex_unlock(&decdata->lock);
    return flag;
}

/*************************************************************************/
/*               stream open/close functions                             */
/*************************************************************************/

/*
 * tc_import_{video,audio}_open: open audio stream for importing.
 * 
 * Parameters:
 *      vob: vob structure
 * Return Value:
 *         TC_OK: succesfull.
 *      TC_ERROR: failure; reason was tc_log*()ged out.
 */
static int tc_import_video_open(vob_t *vob)
{
    int ret;
    transfer_t import_para;

    memset(&import_para, 0, sizeof(transfer_t));

    import_para.flag = TC_VIDEO;

    ret = tcv_import(TC_IMPORT_OPEN, &import_para, vob);
    if (ret < 0) {
        tc_log_error(PACKAGE, "video import module error: OPEN failed");
        return TC_ERROR;
    }

    video_decdata.fd = import_para.fd;

    return TC_OK;
}


static int tc_import_audio_open(vob_t *vob)
{
    int ret;
    transfer_t import_para;

    memset(&import_para, 0, sizeof(transfer_t));

    import_para.flag = TC_AUDIO;

    ret = tca_import(TC_IMPORT_OPEN, &import_para, vob);
    if (ret < 0) {
        tc_log_error(PACKAGE, "audio import module error: OPEN failed");
        return TC_ERROR;
    }

    audio_decdata.fd = import_para.fd;

    return TC_OK;
}

/*
 * tc_import_{video,audio}_close: close audio stream used for importing.
 * 
 * Parameters:
 *      None.
 * Return Value:
 *         TC_OK: succesfull.
 *      TC_ERROR: failure; reason was tc_log*()ged out.
 */

static int tc_import_audio_close(void)
{
    int ret;
    transfer_t import_para;

    memset(&import_para, 0, sizeof(transfer_t));

    import_para.flag = TC_AUDIO;
    import_para.fd   = audio_decdata.fd;

    ret = tca_import(TC_IMPORT_CLOSE, &import_para, NULL);
    if (ret == TC_IMPORT_ERROR) {
        tc_log_warn(PACKAGE, "audio import module error: CLOSE failed");
        return TC_ERROR;
    }
    audio_decdata.fd = NULL;

    return TC_OK;
}

static int tc_import_video_close(void)
{
    int ret;
    transfer_t import_para;

    memset(&import_para, 0, sizeof(transfer_t));

    import_para.flag = TC_VIDEO;
    import_para.fd   = video_decdata.fd;

    ret = tcv_import(TC_IMPORT_CLOSE, &import_para, NULL);
    if (ret == TC_IMPORT_ERROR) {
        tc_log_warn(PACKAGE, "video import module error: CLOSE failed");
        return TC_ERROR;
    }
    video_decdata.fd = NULL;

    return TC_OK;
}


/*************************************************************************/
/*                       the import loops                                */
/*************************************************************************/


#define MARK_TIME_RANGE(PTR, VOB) do { \
    /* Set skip attribute based on -c */ \
    if (fc_time_contains((VOB)->ttime, (PTR)->id)) \
        (PTR)->attributes &= ~TC_FRAME_IS_OUT_OF_RANGE; \
    else \
        (PTR)->attributes |= TC_FRAME_IS_OUT_OF_RANGE; \
} while (0)


/*
 * stop_cause: specify the cause of an import loop termination.
 *
 * Parameters:
 *      ret: termination cause identifier to be specified
 * Return Value:
 *      the most specific recognizable termination cause.
 */
static int stop_cause(int ret)
{
    if (ret == TC_IM_THREAD_UNKNOWN) {
        if (tc_interrupted()) {
            ret = TC_IM_THREAD_INTERRUPT;
        } else if (tc_stopped()) {
            ret = TC_IM_THREAD_DONE;
        }
    }
    return ret;
}

/*
 * {video,audio}_import_loop: data import loops. Feed frame FIFOs with
 * new data forever until are interrupted or stopped.
 *
 * Parameters:
 *      vob: vob structure
 * Return Value:
 *      TC_IM_THREAD_* value reporting operation status.
 */
static int video_import_loop(vob_t *vob)
{
    int ret = 0, vbytes = 0;
    vframe_list_t *ptr = NULL;
    transfer_t import_para;
    TCFrameStatus next = (tc_frame_threads_have_video_workers())
                            ?TC_FRAME_WAIT :TC_FRAME_READY;
    int im_ret = TC_IM_THREAD_UNKNOWN;

    if (verbose >= TC_DEBUG)
        tc_log_msg(__FILE__, "video thread id=%ld", (unsigned long)pthread_self());

    vbytes = vob->im_v_size;

    while (tc_running() && tc_import_thread_is_active(&video_decdata)) {

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(V) %10s [%ld] %i bytes", "requesting",
                       vframecount, vbytes);

        /* stage 1: register new blank frame */
        ptr = vframe_register(vframecount);
        if (ptr == NULL) {
            if (verbose >= TC_THREADS)
                tc_log_msg(__FILE__, "(V) frame registration interrupted!");
            break;
        }

        /* stage 2: fill the frame with data */
        ptr->attributes = 0;
        MARK_TIME_RANGE(ptr, vob);

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(V) new frame registered and marked, now filling...");

        if (video_decdata.fd != NULL) {
            if (vbytes && (ret = mfread(ptr->video_buf, vbytes, 1, video_decdata.fd)) != 1)
                ret = -1;
            ptr->video_len  = vbytes;
            ptr->video_size = vbytes;
        } else {
            import_para.fd         = NULL;
            import_para.buffer     = ptr->video_buf;
            import_para.buffer2    = ptr->video_buf2;
            import_para.size       = vbytes;
            import_para.flag       = TC_VIDEO;
            import_para.attributes = ptr->attributes;

            ret = tcv_import(TC_IMPORT_DECODE, &import_para, vob);

            ptr->video_len   = import_para.size;
            ptr->video_size  = import_para.size;
            ptr->attributes |= import_para.attributes;
        }

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(V) new frame filled (%s)", (ret == -1) ?"FAILED" :"OK");

        if (ret < 0) {
            if (verbose >= TC_DEBUG)
                tc_log_msg(__FILE__, "(V) data read failed - end of stream");

            ptr->video_len  = 0;
            ptr->video_size = 0;
            if (!tc_has_more_video_in_file(vob)) {
                ptr->attributes = TC_FRAME_IS_END_OF_STREAM;
            } else {
                ptr->attributes = TC_FRAME_IS_SKIPPED;
            }
        }

        ptr->v_height   = vob->im_v_height;
        ptr->v_width    = vob->im_v_width;
        ptr->v_bpp      = BPP;

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(V) new frame is being processed");

        /* stage 3: account filled frame and process it if needed */
        if (TC_FRAME_NEED_PROCESSING(ptr)) {
            //first stage pre-processing - (synchronous)
            preprocess_vid_frame(vob, ptr);

            //filter pre-processing - (synchronous)
            ptr->tag = TC_VIDEO|TC_PRE_S_PROCESS;
            tc_filter_process((frame_list_t *)ptr);
        }

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(V) new frame ready to be pushed");

        /* stage 4: push frame to next transcoding layer */
        vframe_push_next(ptr, next);

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(V) %10s [%ld] %i bytes", "received",
                       vframecount, ptr->video_size);

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(V) new frame pushed");

        if (ret < 0) {
            /* 
             * we must delay this stuff in order to properly END_OF_STREAM
             * frames _and_ to push them to subsequent stages
             */
            tc_import_thread_stop(&audio_decdata);
            im_ret = TC_IM_THREAD_DONE;
            break;
        }
        vframecount++;
    }
    return stop_cause(im_ret);
}


#define GET_AUDIO_FRAME do { \
    if (audio_decdata.fd != NULL) { \
        if (abytes && (ret = mfread(ptr->audio_buf, abytes, 1, audio_decdata.fd)) != 1) { \
            ret = -1; \
        } \
        ptr->audio_len  = abytes; \
        ptr->audio_size = abytes; \
    } else { \
        import_para.fd         = NULL; \
        import_para.buffer     = ptr->audio_buf; \
        import_para.size       = abytes; \
        import_para.flag       = TC_AUDIO; \
        import_para.attributes = ptr->attributes; \
        \
        ret = tca_import(TC_IMPORT_DECODE, &import_para, vob); \
        \
        ptr->audio_len  = import_para.size; \
        ptr->audio_size = import_para.size; \
    } \
} while (0)

static int audio_import_loop(vob_t *vob)
{
    int ret = 0, abytes;
    aframe_list_t *ptr = NULL;
    transfer_t import_para;
    TCFrameStatus next = (tc_frame_threads_have_audio_workers())
                            ?TC_FRAME_WAIT :TC_FRAME_READY;
    int im_ret = TC_IM_THREAD_UNKNOWN;

    if (verbose >= TC_DEBUG)
        tc_log_msg(__FILE__, "audio thread id=%ld",
                   (unsigned long)pthread_self());

    abytes = vob->im_a_size;

    while (tc_running() && tc_import_thread_is_active(&audio_decdata)) {
        /* stage 1: audio adjustment for non-PAL frame rates */
        if (aframecount != 0 && aframecount % TC_LEAP_FRAME == 0) {
            abytes = vob->im_a_size + vob->a_leap_bytes;
        } else {
            abytes = vob->im_a_size;
        }

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(A) %10s [%ld] %i bytes",
                       "requesting", aframecount, abytes);

        /* stage 2: register new blank frame */
        ptr = aframe_register(aframecount);
        if (ptr == NULL) {
            if (verbose >= TC_THREADS)
                tc_log_msg(__FILE__, "(A) frame registration interrupted!");
            break;
        }

        ptr->attributes = 0;
        MARK_TIME_RANGE(ptr, vob);

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(A) new frame registered and marked, now syncing...");

        /* stage 3: fill the frame with data */
        /* stage 3.1: resync audio by discarding frames, if needed */
        if (vob->sync > 0) {
            // discard vob->sync frames
            while (vob->sync--) {
                GET_AUDIO_FRAME;

                if (ret == -1)
                    break;
            }
            vob->sync++;
        }

        /* stage 3.2: grab effective audio data */
        if (vob->sync == 0) {
            GET_AUDIO_FRAME;
        }

        /* stage 3.3: silence at last */
        if (vob->sync < 0) {
            if (verbose >= TC_DEBUG)
                tc_log_msg(__FILE__, " zero padding %d", vob->sync);
            memset(ptr->audio_buf, 0, abytes);
            ptr->audio_len  = abytes;
            ptr->audio_size = abytes;
            vob->sync++;
        }
        /* stage 3.x final note: all this stuff can be done in a cleaner way... */

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(A) syncing done, new frame ready to be filled...");

        if (ret < 0) {
            if (verbose >= TC_DEBUG)
                tc_log_msg(__FILE__, "(A) data read failed - end of stream");

            ptr->audio_len  = 0;
            ptr->audio_size = 0;
            if (!tc_has_more_audio_in_file(vob)) {
                ptr->attributes = TC_FRAME_IS_END_OF_STREAM;
            } else {
                ptr->attributes = TC_FRAME_IS_SKIPPED;
            }
        }

        // init frame buffer structure with import frame data
        ptr->a_rate = vob->a_rate;
        ptr->a_bits = vob->a_bits;
        ptr->a_chan = vob->a_chan;

        /* stage 4: account filled frame and process it if needed */
        if (TC_FRAME_NEED_PROCESSING(ptr)) {
            ptr->tag = TC_AUDIO|TC_PRE_S_PROCESS;
            tc_filter_process((frame_list_t *)ptr);
        }

        /* stage 5: push frame to next transcoding layer */
        aframe_push_next(ptr, next);

        if (verbose >= TC_THREADS)
            tc_log_msg(__FILE__, "(A) %10s [%ld] %i bytes", "received",
                                 aframecount, ptr->audio_size);

        if (ret < 0) {
            tc_import_thread_stop(&audio_decdata);
            im_ret = TC_IM_THREAD_DONE;
            break;
        }
        aframecount++;
    }
    return stop_cause(im_ret);
}

#undef GET_AUDIO_FRAME
#undef MARK_TIME_RANGE

/*************************************************************************/
/*        ladies and gentlemens, the thread routines                     */
/*************************************************************************/

/* audio decode thread wrapper */
static void *audio_import_thread(void *_vob)
{
    static int ret = 0;
    ret = audio_import_loop(_vob);
    if (verbose >= TC_CLEANUP)
        tc_log_msg(__FILE__, "audio decode loop ends with code 0x%i", ret);
    pthread_exit(&ret);
}

/* video decode thread wrapper */
static void *video_import_thread(void *_vob)
{
    static int ret = 0;
    ret = video_import_loop(_vob);
    if (verbose >= TC_CLEANUP)
        tc_log_msg(__FILE__, "video decode loop ends with code 0x%i", ret);
    pthread_exit(&ret);
}


/*************************************************************************/
/*               main API functions                                      */
/*************************************************************************/

int tc_import_status()
{
    return tc_import_video_status() && tc_import_audio_status();
}

int tc_import_video_running(void)
{
    return tc_import_thread_is_active(&video_decdata);
}

int tc_import_audio_running(void)
{
    return tc_import_thread_is_active(&audio_decdata);
}

int tc_import_video_status(void)
{
    return vframe_have_more() || tc_import_thread_is_active(&video_decdata);
}

int tc_import_audio_status(void)
{
    return aframe_have_more() || tc_import_thread_is_active(&audio_decdata);
}


void tc_import_threads_cancel(void)
{
    void *status = NULL;
    int vret, aret;

    if (tc_decoder_delay)
        tc_log_info(__FILE__, "sleeping for %i seconds to cool down", tc_decoder_delay);
    sleep(tc_decoder_delay);
    tc_log_info(__FILE__, "cancelling the import threads");

    tc_import_thread_stop(&video_decdata);
    tc_import_thread_stop(&audio_decdata);

    tc_framebuffer_interrupt_import();

    vret = pthread_join(video_decdata.thread_id, &status);
    if (verbose >= TC_DEBUG) {
        int *pst = status; /* avoid explicit cast in log below */
        tc_log_msg(__FILE__, "video thread exit (ret_code=%i)"
                             " (status_code=%i)", vret, *pst);
    }

    aret = pthread_join(audio_decdata.thread_id, &status);
    if (verbose >= TC_DEBUG) {
        int *pst = status; /* avoid explicit cast in log below */
        tc_log_msg(__FILE__, "audio thread exit (ret_code=%i)"
                             " (status_code=%i)", aret, *pst);
    }
    return;
}


void tc_import_threads_create(vob_t *vob)
{
    int ret;

    tc_import_thread_start(&audio_decdata);
    ret = pthread_create(&audio_decdata.thread_id, NULL,
                         audio_import_thread, vob);
    if (ret != 0)
        tc_error("failed to start audio stream import thread");

    tc_import_thread_start(&video_decdata);
    ret = pthread_create(&video_decdata.thread_id, NULL,
                         video_import_thread, vob);
    if (ret != 0)
        tc_error("failed to start video stream import thread");
}


int tc_import_init(vob_t *vob, const char *a_mod, const char *v_mod)
{
    transfer_t import_para;
    int caps;

    a_mod = (a_mod == NULL) ?TC_DEFAULT_IMPORT_AUDIO :a_mod;
    audio_decdata.im_handle = load_module(a_mod, TC_IMPORT+TC_AUDIO);
    RETURN_IF_NULL(audio_decdata.im_handle, "audio");

    v_mod = (v_mod == NULL) ?TC_DEFAULT_IMPORT_VIDEO :v_mod;
    video_decdata.im_handle = load_module(v_mod, TC_IMPORT+TC_VIDEO);
    RETURN_IF_NULL(video_decdata.im_handle, "video");

    memset(&import_para, 0, sizeof(transfer_t));
    import_para.flag = verbose;
    tca_import(TC_IMPORT_NAME, &import_para, NULL);

    caps = check_module_caps(&import_para, vob->im_a_codec, audpairs);
    RETURN_IF_NOT_SUPPORTED(caps, "audio");
    
    memset(&import_para, 0, sizeof(transfer_t));
    import_para.flag = verbose;
    tcv_import(TC_IMPORT_NAME, &import_para, NULL);

    caps = check_module_caps(&import_para, vob->im_v_codec, vidpairs);
    RETURN_IF_NOT_SUPPORTED(caps, "video");

    tc_pthread_main = pthread_self();

    return TC_OK;
}


int tc_import_open(vob_t *vob)
{
    RETURN_IF_FUNCTION_FAILED(tc_import_audio_open, vob);
    RETURN_IF_FUNCTION_FAILED(tc_import_video_open, vob);

    return TC_OK;
}

int tc_import_close(void)
{
    RETURN_IF_FUNCTION_FAILED(tc_import_audio_close);
    RETURN_IF_FUNCTION_FAILED(tc_import_video_close);

    return TC_OK;
}

void tc_import_shutdown(void)
{
    if (verbose >= TC_DEBUG) {
        tc_log_msg(__FILE__, "unloading audio import module");
    }

    unload_module(audio_decdata.im_handle);
    audio_decdata.im_handle = NULL;

    if (verbose >= TC_DEBUG) {
        tc_log_msg(__FILE__, "unloading video import module");
    }

    unload_module(video_decdata.im_handle);
    video_decdata.im_handle = NULL;
}


/*************************************************************************/
/*             the new multi-input sequential API                        */
/*************************************************************************/

static void dump_probeinfo(const ProbeInfo *pi, int i, const char *tag)
{
#ifdef DEBUG
    tc_log_warn(__FILE__, "(%s): %ix%i asr=%i frc=%i codec=0x%lX",
                tag, pi->width, pi->height, pi->asr, pi->frc, pi->codec);

    if (i >= 0) {
        tc_log_warn(__FILE__, "(%s): #%i %iHz %ich %ibits format=0x%X",
                    tag, i,
                    pi->track[i].samplerate, pi->track[i].chan,
                    pi->track[i].bits, pi->track[i].format);
    }
#endif
}

static int probe_im_stream(const char *src, ProbeInfo *info)
{
    static pthread_mutex_t probe_mutex = PTHREAD_MUTEX_INITIALIZER; /* XXX */
    /* UGLY! */
    int ret = 1; /* be optimistic! */

    pthread_mutex_lock(&probe_mutex);
    ret = probe_stream_data(src, seek_range, info);
    pthread_mutex_unlock(&probe_mutex);

    dump_probeinfo(info, 0, "probed");

    return ret;
}

static int probe_matches(const ProbeInfo *ref, const ProbeInfo *cand, int i)
{
    if (ref->width  != cand->width || ref->height != cand->height
     || ref->frc    != cand->frc   || ref->asr    != cand->asr
     || ref->codec  != cand->codec) {
        tc_log_error(__FILE__, "video parameters mismatch");
        dump_probeinfo(ref,  -1, "old");
        dump_probeinfo(cand, -1, "new");
        return 0;
    }

    if (i > ref->num_tracks || i > cand->num_tracks) {
        tc_log_error(__FILE__, "track parameters mismatch (i=%i|ref=%i|cand=%i)",
                     i, ref->num_tracks, cand->num_tracks);
        return 0;
    }
    if (ref->track[i].samplerate != cand->track[i].samplerate
     || ref->track[i].chan       != cand->track[i].chan      ) {
//     || ref->track[i].bits       != cand->track[i].bits      ) { 
//     || ref->track[i].format     != cand->track[i].format    ) { XXX XXX XXX
        tc_log_error(__FILE__, "audio parameters mismatch");
        dump_probeinfo(ref,  i, "old");
        dump_probeinfo(cand, i, "new");
        return 0;
    }       

    return 1;
}

static void probe_from_vob(ProbeInfo *info, const vob_t *vob)
{
    /* copy only interesting fields */
    if (info != NULL && vob != NULL) {
        int i = 0;

        info->width    = vob->im_v_width;
        info->height   = vob->im_v_height;
        info->codec    = vob->v_codec_flag;
        info->asr      = vob->im_asr;
        info->frc      = vob->im_frc;

        for (i = 0; i < TC_MAX_AUD_TRACKS; i++) {
            memset(&(info->track[i]), 0, sizeof(ProbeTrackInfo));
        }
        i = vob->a_track;

        info->track[i].samplerate = vob->a_rate;
        info->track[i].chan       = vob->a_chan;
        info->track[i].bits       = vob->a_bits;
        info->track[i].format     = vob->a_codec_flag;
    }
}

/* ok, that sucks. I know. I can't do any better now. */
static const char *current_in_file(vob_t *vob, int kind)
{
    if (kind == TC_VIDEO)
    	return vob->video_in_file;
    if (kind == TC_AUDIO)
    	return vob->audio_in_file;
    return NULL; /* cannot happen */
}

#define RETURN_IF_PROBE_FAILED(ret, src) do {                        \
    if (ret == 0) {                                                  \
        tc_log_error(PACKAGE, "probing of source '%s' failed", src); \
        status = TC_IM_THREAD_PROBE_ERROR;                           \
    }                                                                \
} while (0)

/* black magic in here? Am I looking for troubles? */
#define SWAP(type, a, b) do { \
    type tmp = a;             \
    a = b;                    \
    b = tmp;                  \
} while (0)


/*************************************************************************/

typedef struct tcmultiimportdata_ TCMultiImportData;
struct tcmultiimportdata_ {
    int             kind;

    TCDecoderData   *decdata;
    vob_t           *vob;

    ProbeInfo       infos;

    int             (*open)(vob_t *vob);
    int             (*import_loop)(vob_t *vob);
    int             (*close)(void);

    int             (*next)(vob_t *vob);
};


#define MULTIDATA_INIT(MEDIA, VOB, KIND) do {                           \
    MEDIA ## _multidata.kind         = KIND;                          \
                                                                      \
    MEDIA ## _multidata.vob          = VOB;                           \
    MEDIA ## _multidata.decdata      = &(MEDIA ## _decdata);          \
                                                                      \
    MEDIA ## _multidata.open         = tc_import_ ## MEDIA ## _open;  \
    MEDIA ## _multidata.import_loop  = MEDIA ## _import_loop;         \
    MEDIA ## _multidata.close        = tc_import_ ## MEDIA ## _close; \
    MEDIA ## _multidata.next         = tc_next_ ## MEDIA ## _in_file; \
} while (0)

#define MULTIDATA_FINI(MEDIA) do { \
    ; /* nothing */ \
} while (0)



static TCMultiImportData audio_multidata;
static TCMultiImportData video_multidata;

/*************************************************************************/

static void *multi_import_thread(void *_sid)
{
    static int status = TC_IM_THREAD_UNKNOWN; /* can't we be more specific? */

    TCMultiImportData *sid = _sid;
    int ret = TC_OK, track_id = sid->vob->a_track;
    ProbeInfo infos;
    ProbeInfo *old = &(sid->infos), *new = &infos;
    const char *fname = NULL;
    long int i = 1;

    while (tc_running() && tc_import_thread_is_active(sid->decdata)) {
        ret = sid->open(sid->vob);
        if (ret == TC_ERROR) {
            status = TC_IM_THREAD_EXT_ERROR;
            break;
        }

        status = sid->import_loop(sid->vob);
        /* source should always be closed */

        ret = sid->close();
        if (ret == TC_ERROR) {
            status = TC_IM_THREAD_EXT_ERROR;
            break;
        }

        ret = sid->next(sid->vob);
        if (ret == TC_ERROR) {
            status = TC_IM_THREAD_DONE;
            break;
        }

        fname = current_in_file(sid->vob, sid->kind);
        /* probing coherency check */
        ret = probe_im_stream(fname, new);
        RETURN_IF_PROBE_FAILED(ret, fname);

        if (probe_matches(old, new, track_id)) {
            if (verbose) {
                tc_log_info(__FILE__, "switching to %s source #%li: %s",
                            sid->decdata->tag, i, fname);
            }
        } else {
            tc_log_error(PACKAGE, "source '%s' in directory"
                                  " not compatible with former", fname);
            status = TC_IM_THREAD_PROBE_ERROR;
            break;
        }
        /* now prepare for next probing round by swapping pointers */
        SWAP(ProbeInfo*, old, new);

        i++;
    }
    status = stop_cause(status);

    tc_framebuffer_interrupt();

    pthread_exit(&status);
}

/*************************************************************************/

void tc_multi_import_threads_create(vob_t *vob)
{
    int ret;

    probe_from_vob(&(audio_multidata.infos), vob);
    MULTIDATA_INIT(audio, vob, TC_AUDIO);
    tc_import_thread_start(&audio_decdata);
    ret = pthread_create(&audio_decdata.thread_id, NULL,
                         multi_import_thread, &audio_multidata);
    if (ret != 0) {
        tc_error("failed to start sequential audio stream import thread");
    }

    probe_from_vob(&(video_multidata.infos), vob);
    MULTIDATA_INIT(video, vob, TC_VIDEO);
    tc_import_thread_start(&video_decdata);
    ret = pthread_create(&video_decdata.thread_id, NULL,
                         multi_import_thread, &video_multidata);
    if (ret != 0) {
        tc_error("failed to start sequential video stream import thread");
    }

    tc_info("sequential streams import threads started");
}


void tc_multi_import_threads_cancel(void)
{
    MULTIDATA_FINI(audio);
    MULTIDATA_FINI(video);

    tc_import_threads_cancel();
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

