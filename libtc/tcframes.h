/*
 * tcframes.h -- common generic audio/video/whatever frame allocation/disposal
 *               routines for transcode.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#ifndef TCFRAMES_H
#define TCFRAMES_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>

#include "libtc/libtc.h"
#include "libtc/tctimer.h"
#include "libtc/tccodecs.h"


/*************************************************************************/

/* frame attributes */
typedef enum tcframeattributes_ TCFrameAttributes;
enum tcframeattributes_ {
    TC_FRAME_IS_KEYFRAME       =   1,
    TC_FRAME_IS_INTERLACED     =   2,
    TC_FRAME_IS_BROKEN         =   4,
    TC_FRAME_IS_SKIPPED        =   8,
    TC_FRAME_IS_CLONED         =  16,
    TC_FRAME_WAS_CLONED        =  32,
    TC_FRAME_IS_OUT_OF_RANGE   =  64,
    TC_FRAME_IS_DELAYED        = 128,
    TC_FRAME_IS_END_OF_STREAM  = 256,
};

#define TC_FRAME_NEED_PROCESSING(PTR) \
    (!((PTR)->attributes & TC_FRAME_IS_OUT_OF_RANGE) \
     && !((PTR)->attributes & TC_FRAME_IS_END_OF_STREAM))

typedef enum tcframestatus_ TCFrameStatus;
enum tcframestatus_ {
    TC_FRAME_NULL    = -1, /* on the frame pool, not yet claimed   */
    TC_FRAME_EMPTY   = 0,  /* claimed and being filled by decoder  */
    TC_FRAME_WAIT,         /* needs further processing (filtering) */
    TC_FRAME_LOCKED,       /* being procedded by filter layer      */
    TC_FRAME_READY,        /* ready to be processed by encoder     */
};

/*
 * frame status transitions scheme (overview)
 *
 *     
 *     .-------<----- +-------<------+------<------+-------<-------.
 *     |              ^              ^             ^               ^
 *     V              |              |             |               |
 * FRAME_NULL -> FRAME_EMPTY -> FRAME_WAIT -> FRAME_LOCKED -> FRAME_READY
 * :_buffer_:    \_decoder_/    \______filter_stage______/    \encoder_%/
 * \__pool__/         |         :                                  ^    :
 *                    |         \_______________encoder $__________|____/
 *                    V                                            ^
 *                    `-------------->------------->---------------'
 *
 * Notes:
 *  % - regular case, frame (processing) threads avalaibles
 *  $ - practical (default) case, filtering is carried by encoder thread.
 */

/*************************************************************************/

/*
 * NOTE: The following warning will become irrelevant once NMS is
 *       in place, and frame_list_t can go away completely.  --AC
 *       (here's a FIXME tag so we don't forget)
 *
 * BIG FAT WARNING:
 *
 * These structures must be kept in sync: meaning that if you add
 * another field to the vframe_list_t you must add it at the end
 * of the structure.
 *
 * aframe_list_t, vframe_list_t and the wrapper frame_list_t share
 * the same offsets to their elements up to the field "size". That
 * means that when a filter is called with at init time with the
 * anonymouse frame_list_t, it can already access the size.
 *
 *          -- tibit
 */

/* This macro factorizes common frame data fields.
 * Is not possible to completely factor out all frame_list_t fields
 * because video and audio typess uses different names for same fields,
 * and existing code relies on this assumption.
 * Fixing this is stuff for 1.2.0 and beyond, for which I would like
 * to introduce some generic frame structure or something like it. -- FR.
 */
#define TC_FRAME_COMMON \
    int id;                       /* frame id (sequential uint) */ \
    int bufid;                    /* buffer id                  */ \
    int tag;                      /* init, open, close, ...     */ \
    int filter_id;                /* filter instance to run     */ \
    TCFrameStatus status;         /* see enumeration above      */ \
    TCFrameAttributes attributes; /* see enumeration above      */ \
    TCTimestamp timestamp;                                         \
/* BEWARE: semicolon NOT NEEDED */

/* 
 * Size vs Length
 *
 * Size represent the effective size of audio/video buffer,
 * while length represent the amount of valid data into buffer.
 * Until 1.1.0, there isn't such distinction, and 'size'
 * have approximatively a mixed meaning of above.
 *
 * In the long shot[1] (post-1.1.0) transcode will start
 * intelligently allocate frame buffers based on highest
 * request of all modules (core included) through filter
 * mangling pipeline. This will lead on circumstances on
 * which valid data into a buffer is less than buffer size:
 * think to demuxer->decoder transition or RGB24->YUV420.
 * 
 * There also are more specific cases like a full-YUV420P
 * pipeline with final conversion to RGB24 and raw output,
 * so we can have something like
 *
 * framebuffer size = sizeof(RGB24_frame)
 * after demuxer:
 *     frame length << frame size (compressed data)
 * after decoder:
 *     frame length < frame size (YUV420P smaller than RGB24)
 * in filtering:
 *      frame length < frame size (as above)
 * after encoding (in fact just colorspace transition):
 *     frame length == frame size (data becomes RGB24)
 * into muxer:
 *     frame length == frame size (as above)
 *
 * In all those cases having a distinct 'lenght' fields help
 * make things nicer and easier.
 *
 * +++
 *
 * [1] in 1.1.0 that not happens due to module interface constraints
 * since we're still bound to Old Module System.
 */

#define TC_FRAME_GET_TIMESTAMP_UINT(FP)       ((FP)->timestamp.u)
#define TC_FRAME_GET_TIMESTAMP_DOUBLE(FP)     ((FP)->timestamp.d)
#define TC_FRAME_SET_TIMESTAMP_UINT(FP, TS)   ((FP)->timestamp.u = (uint64_t)(TS))
#define TC_FRAME_SET_TIMESTAMP_DOUBLE(FP, TS) ((FP)->timestamp.d = (double)(TS))

typedef struct tcframe_ TCFrame;
struct tcframe_ {
    TC_FRAME_COMMON

    int codec;   /* codec identifier */

    int size;    /* buffer size avalaible */
    int len;     /* how much data is valid? */

    int param1;  /* v_width  or a_rate */
    int param2;  /* v_height or a_bits */
    int param3;  /* v_bpp    or a_chan */

    struct tcframe_ *next;
    struct tcframe_ *prev;
};
typedef struct tcframe_ frame_list_t;


typedef struct tcframevideo_ TCFrameVideo;
struct tcframevideo_ {
    TC_FRAME_COMMON
    /* frame physical parameter */
    
    int v_codec;       /* codec identifier */

    int video_size;    /* buffer size avalaible */
    int video_len;     /* how much data is valid? */

    int v_width;
    int v_height;
    int v_bpp;

    struct tcframevideo_ *next;
    struct tcframevideo_ *prev;

    uint8_t *video_buf;  /* pointer to current buffer */
    uint8_t *video_buf2; /* pointer to backup buffer */

    int free; /* flag */

#ifdef STATBUFFER
    uint8_t *internal_video_buf_0;
    uint8_t *internal_video_buf_1;
#else
    uint8_t internal_video_buf_0[SIZE_RGB_FRAME];
    uint8_t internal_video_buf_1[SIZE_RGB_FRAME];
#endif

    int deinter_flag;
    /* set to N for internal de-interlacing with "-I N" */

    uint8_t *video_buf_RGB[2];

    uint8_t *video_buf_Y[2];
    uint8_t *video_buf_U[2];
    uint8_t *video_buf_V[2];
};
typedef struct tcframevideo_ vframe_list_t;


typedef struct tcframeaudio_ TCFrameAudio;
struct tcframeaudio_ {
    TC_FRAME_COMMON

    int a_codec;       /* codec identifier */

    int audio_size;    /* buffer size avalaible */
    int audio_len;     /* how much data is valid? */

    int a_rate;
    int a_bits;
    int a_chan;

    struct tcframeaudio_ *next;
    struct tcframeaudio_ *prev;

    uint8_t *audio_buf;
    uint8_t *audio_buf2;

    int free; /* flag */

#ifdef STATBUFFER
    uint8_t *internal_audio_buf;
    uint8_t *internal_audio_buf_1;
#else
    uint8_t internal_audio_buf[SIZE_PCM_FRAME * 2];
    uint8_t internal_audio_buf_1[SIZE_PCM_FRAME * 2];
#endif
};
typedef struct tcframeaudio_ aframe_list_t;

/* 
 * generic pointer type, needed at least by internal code.
 * In the long (long) shot I'd like to use a unique generic
 * data container, like AVPacket (libavcodec) or something like it.
 * (see note about TC_FRAME_COMMON above) -- FR
 */
typedef union tcframeptr_ TCFramePtr;
union tcframeptr_ {
    TCFrame *generic;
    TCFrameVideo *video;
    TCFrameAudio *audio;
};

/*************************************************************************/


/*
 * tc_video_planes_size:
 *     compute the size of video planes given frame size and frame format.
 *     Recognizes only video formats used in transcode.
 *
 * Parameters:
 *     psizes: array of size that will be filled with size of respective
 *             plane, in order. If given format isn't a planar one, only
 *             first element in array is significant.
 *      width: width of video frame
 *     height: height of video frame
 *     format: format of video frame
 * Return Value:
 *     >= 0 if succesfull,
 *     TC_NULL_MATCH otherwise (wrong/unknown parameters)
 */
int tc_video_planes_size(size_t psizes[3],
                         int width, int height, int format);

/*
 * tc_video_frame_size:
 *     little helper function that returns the full dimension of a
 *     video frame given dimensions and format.
 *
 * Parameters:
 *      width: width of video frame
 *     height: height of video frame
 *     format: format of video frame
 * Return Value:
 *     size in bytes of video frame
 */
#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((unused))
#endif
static size_t tc_video_frame_size(int width, int height, int format)
{
    size_t psizes[3] = { 0, 0, 0 };
    tc_video_planes_size(psizes, width, height, format);
    return (psizes[0] + psizes[1] + psizes[2]);
}

/* 
 * OK, we have sample rate. But sample rate means "[audio] samples PER SECOND"
 * and we want audio samples PER FRAME.
 */
#define TC_AUDIO_SAMPLES_IN_FRAME(rate, fps)    ((double)rate/(double)fps)

/*
 * tc_audio_frame_size:
 *     compute the size of buffer needed to store the audio data described by
 *     given specifiers. 
 *
 * Parameters:
 *      samples: audio samples PER FRAME. Can (and it's likely that it will)
 *               be a real numner (values after the point are significant!)
 *     channels: audio channels.
 *         bits: audio BITS for sample.
 *       adjust: store here adjustement value. Such value means how much extra
 *               buffer size it's needed to safely store extra samples.
 *               We have extra samples when rate/fps != 0, so we can
 *               spread all samples in frames, there is something that
 *               "exceed" :)
 *               (OK, there is *A LOT* of room for improvement here. But this
 *               API it's also driven by legacy code).
 * Return Value:
 *     amount of buffer needed.
 * Preconditions:
 *     adjust != NULL.
 */
size_t tc_audio_frame_size(double samples, int channels,
                           int bits, int *adjust);

/*
 * tc_alloc_{video,audio}_frame:
 *     allocate, but NOT initialize, a {TCFrameVideo,TCFrameAudio},
 *     large enough to hold a video frame large as given size.
 *     This function guarantee that video buffer(s) memory will
 *     be page-aligned.
 *
 * Parameters:
 *        size: size in bytes of video frame that will be contained.
 *     partial: if !0, doesn't allocate secondary video buffer,
 *              but only primary. This allow to save memory since
 *              secondary video buffer isn't ALWAYS needed.
 * Return Value:
 *     pointer to a new TCFrameVideo (free it using tc_del_video_frame,
 *     not manually! ) if succesfull, NULL otherwise.
 */
TCFrameVideo *tc_alloc_video_frame(size_t size, int partial);
TCFrameAudio *tc_alloc_audio_frame(size_t size);


/*
 * tc_init_video_frame:
 *     properly (re)initialize an already-allocated video frame, by
 *     asjusting plane pointers, (re)setting video buffer pointers,
 *     cleaning flags et. al.
 *     You usually always need to use this function unless you
 *     perfectly knows what you're doing.
 *     Do nothing if missing TCFrameVideo to (re)initialize of
 *     one or more parameter are wrong.
 *
 * Parameters:
 *       vptr: pointer to TCFrameVideo to (re)initialize.
 *      width: video frame width.
 *     height: video frame height.
 *     format: video frame format.
 * Return Value:
 *     None
 * Preconditions:
 *     given TCFrameVideo MUST be already allocated to be large
 *     enough to safely store a video frame with given
 *     parameters. This function DO NOT check if this precondition
 *     is respected.
 */
void tc_init_video_frame(TCFrameVideo *vptr,
                         int width, int height, int format);
/*
 * tc_init_audio_frame:
 *     properly (re)initialize an already-allocated audio frame,
 *     (re)setting video buffer pointers,cleaning flags et. al.
 *     You usually always need to use this function unless you
 *     perfectly knows what you're doing.
 *     Do nothing if missing TCFrameAudio to (re)initialize of
 *     one or more parameter are wrong.
 *
 * Parameters:
 *       aptr: pointer to TCFrameAudio to (re)initialize.
 *    samples: audio frame samples that this audio frame
 *             will contain (WARNING: TCFrameAudio MUST
 *             be allocated accordingly).
 *   channels: audio frame channels.
 *       bits: audio frame bit for sample.
 * Return Value:
 *     None
 * Preconditions:
 *     given TCFrameAudio MUST be already allocated to be large
 *     enough to safely store an audio frame with given
 *     parameters. This function DO NOT check if this precondition
 *     is respected.
 */
void tc_init_audio_frame(TCFrameAudio *aptr,
                         double samples, int channels, int bits);

/*
 * tc_new_video_frame:
 *     allocate and initialize a new TCFrameVideo large enough
 *     to hold a video frame represented by given parameters.
 *     This function guarantee that video buffer(s) memory will
 *     be page-aligned.
 *
 * Parameters:
 *      width: video frame width.
 *     height: video frame height.
 *     format: video frame format.
 *    partial: if !0, doesn't allocate secondary video buffer,
 *             but only primary. This allow to save memory since
 *             secondary video buffer isn't ALWAYS needed.
 * Return Value:
 *     pointer to a new TCFrameVideo (free it using tc_del_video_frame,
 *     not manually! ) if succesfull, NULL otherwise.
 */
TCFrameVideo *tc_new_video_frame(int width, int height, int format,
                                  int partial);

/*
 * tc_new_audio_frame:
 *     allocate and initialize a new TCFrameAudio large enough
 *     to hold an audio frame represented by given parameters.
 *     This function guarantee that audio buffer memory will
 *     be page-aligned.
 *
 * Parameters:
 *    samples: audio frame samples that this audio frame
 *             will contain (WARNING: TCFrameAudio MUST
 *             be allocated accordingly).
 *   channels: audio frame channels.
 *       bits: audio frame bit for sample.
 * Return Value:
 *     pointer to a new TCFrameAudio (free it using tc_del_audio_frame,
 *     not manually! ) if succesfull, NULL otherwise.
 */
TCFrameAudio *tc_new_audio_frame(double samples, int channels, int bits);


/*
 * tc_del_{video,audio}_frame:
 *     safely deallocate memory obtained with tc_new_{video,audio}_frame
 *     or tc_alloc_{video,audio}_frame.
 *
 * Parameters:
 *     {vptr,aptr}: a pointer to a TCFrame{Video,Audio} obtained by calling
 *     tc_new_{video,audio}_frame or tc_alloc_{video,audio}_frame.
 * Return Value:
 *     None
 */
void tc_del_video_frame(TCFrameVideo *vptr);
void tc_del_audio_frame(TCFrameAudio *aptr);

/*
 * tc_blank_{video,audio}_frame:
 *      fill a provided frame with per-format valid but blank (null)
 *      content.
 *
 * Parameters:
 *     ptr: pointer to frame to fill.
 * Return Value:
 *     None.
 */
void tc_blank_video_frame(TCFrameVideo *ptr);
void tc_blank_audio_frame(TCFrameAudio *ptr);


#endif  /* TCFRAMES_H */
