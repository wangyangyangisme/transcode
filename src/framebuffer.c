/*
 * framebuffer.c -- audio/video frame ringbuffers, reloaded.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
 * Based on code
 * (C) 2001-2006 - Thomas Oestreich.
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

#include <pthread.h>

#include "transcode.h"
#include "tc_defaults.h"
#include "framebuffer.h"
#include "decoder.h"
#include "encoder-common.h"

#include "libtc/tcframes.h"
#include "libtc/ratiocodes.h"

/*
 * Summary:
 * This code acts as generic ringbuffer implementation, with
 * specializations for main (audio and video) ringbufffers
 * in order to cope legacy constraints from 1.0.x series.
 * It replaces former src/{audio,video}_buffer.c in (hopefully!)
 * a more generic, clean, maintanable, compact way.
 * 
 * Please note that there is *still* some other ringbuffer
 * scatthered through codebase (subtitle buffer,d emux buffers,
 * possibly more). They will be merged lately or will be dropped
 * or reworked.
 *
 * This code can, of course, be further improved (and GREATLY improved,
 * especially for multithreading safeness), but doing so
 * hasn't high priority on my TODO list, I've covered with this
 * piece of code most urgent todos for 1.1.0.      -- FR
 */

static pthread_mutex_t aframe_list_lock = PTHREAD_MUTEX_INITIALIZER;
static aframe_list_t *aframe_list_head = NULL;
static aframe_list_t *aframe_list_tail = NULL;
static pthread_cond_t audio_import_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t audio_filter_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t audio_export_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t vframe_list_lock = PTHREAD_MUTEX_INITIALIZER;
static vframe_list_t *vframe_list_head = NULL;
static vframe_list_t *vframe_list_tail = NULL;
static pthread_cond_t video_import_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t video_filter_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t video_export_cond = PTHREAD_COND_INITIALIZER;

void tc_framebuffer_interrupt_import(void)
{
    pthread_mutex_lock(&aframe_list_lock);
    pthread_cond_signal(&audio_import_cond);
    pthread_mutex_unlock(&aframe_list_lock);

    pthread_mutex_lock(&vframe_list_lock);
    pthread_cond_signal(&video_import_cond);
    pthread_mutex_unlock(&vframe_list_lock);
}

void tc_framebuffer_interrupt(void)
{
    pthread_mutex_lock(&aframe_list_lock);
    pthread_cond_signal(&audio_import_cond);
    pthread_cond_broadcast(&audio_filter_cond);
    /* filter layer deserves special care */
    pthread_cond_signal(&audio_export_cond);
    pthread_mutex_unlock(&aframe_list_lock);

    pthread_mutex_lock(&vframe_list_lock);
    pthread_cond_signal(&video_import_cond);
    pthread_cond_broadcast(&video_filter_cond);
    /* filter layer deserves special care */
    pthread_cond_signal(&video_export_cond);
    pthread_mutex_unlock(&vframe_list_lock);
}

/* ------------------------------------------------------------------ */

/*
 * Layered, custom allocator/disposer for ringbuffer structures.
 * The idea is to simplify (from ringbuffer viewpoint!) frame
 * allocation/disposal and to make it as much generic as is possible
 * (avoif if()s and so on).
 */

typedef TCFramePtr (*TCFrameAllocFn)(const TCFrameSpecs *);

typedef void (*TCFrameFreeFn)(TCFramePtr);

/* ------------------------------------------------------------------ */

typedef struct tcringframebuffer_ TCRingFrameBuffer;
struct tcringframebuffer_ {
    /* real ringbuffer */
    TCFramePtr *frames;

    /* indexes of ringbuffer */
    int next;
    int last;

    /* counters. How many frames in various TCFrameStatus-es? */
    int null;
    int empty;
    int wait;
    int locked;
    int ready;

    /* what we need here? */
    const TCFrameSpecs *specs;
    /* (de)allocation helpers */
    TCFrameAllocFn alloc;
    TCFrameFreeFn free;
};

static TCRingFrameBuffer tc_audio_ringbuffer;
static TCRingFrameBuffer tc_video_ringbuffer;

/* 
 * Specs used internally. I don't export this structure directly
 * because I want to be free to change it if needed
 */
static TCFrameSpecs tc_specs = {
    /* Largest supported values, to ensure the buffer is always big enough
     * (see FIXME in tc_framebuffer_set_specs()) */
    .frc      = 3,  // PAL, why not
    .width    = TC_MAX_V_FRAME_WIDTH,
    .height   = TC_MAX_V_FRAME_HEIGHT,
    .format   = TC_CODEC_RGB,
    .rate     = RATE,
    .channels = CHANNELS,
    .bits     = BITS,
    .samples  = 48000.0,
};

/*
 * Frame allocation/disposal helpers, needed by code below
 * thin wrappers around libtc facilities
 * I don't care about layering and performance loss, *here*, because
 * frame are supposed to be allocated/disposed ahead of time, and
 * always outside inner (performance-sensitive) loops.
 */

/* ------------------------------------------------------------------ */

#define TCFRAMEPTR_IS_NULL(tcf)    (tcf.generic == NULL)

static TCFramePtr tc_video_alloc(const TCFrameSpecs *specs)
{
    TCFramePtr frame;
    frame.video = tc_new_video_frame(specs->width, specs->height,
                                      specs->format, TC_FALSE);
    return frame;
}

static TCFramePtr tc_audio_alloc(const TCFrameSpecs *specs)
{
    TCFramePtr frame;
    frame.audio = tc_new_audio_frame(specs->samples, specs->channels,
                                     specs->bits);
    return frame;
}


static void tc_video_free(TCFramePtr frame)
{
    tc_del_video_frame(frame.video);
}

static void tc_audio_free(TCFramePtr frame)
{
    tc_del_audio_frame(frame.audio);
}

/* ------------------------------------------------------------------ */

/* exported commodities :) */

vframe_list_t *vframe_alloc_single(void)
{
    return tc_new_video_frame(tc_specs.width, tc_specs.height,
                              tc_specs.format, TC_TRUE);
}

aframe_list_t *aframe_alloc_single(void)
{
    return tc_new_audio_frame(tc_specs.samples, tc_specs.channels,
                              tc_specs.bits);
}

/* ------------------------------------------------------------------ */

static void tc_ring_framebuffer_dump_status(const TCRingFrameBuffer *rfb,
                                            const char *id)
{
    tc_log_msg(__FILE__, "%s: null=%i empty=%i wait=%i"
                         " locked=%i ready=%i",
                         id, rfb->null, rfb->empty, rfb->wait,
                         rfb->locked, rfb->ready);
}


const TCFrameSpecs *tc_framebuffer_get_specs(void)
{
    return &tc_specs;
}

/* 
 * using an <OOP-ism>accessor</OOP-ism> is also justified here
 * by the fact that we compute (ahead of time) samples value for
 * later usage.
 */
void tc_framebuffer_set_specs(const TCFrameSpecs *specs)
{
    /* silently ignore NULL specs */
    if (specs != NULL) {
        double fps;

        /* raw copy first */
        ac_memcpy(&tc_specs, specs, sizeof(TCFrameSpecs));

        /* restore width/height/bpp
         * (FIXME: temp until we have a way to know the max size that will
         *         be used through the decode/process/encode chain; without
         *         this, -V yuv420p -y raw -F rgb (e.g.) crashes with a
         *         buffer overrun)
         */
        tc_specs.width  = TC_MAX_V_FRAME_WIDTH;
        tc_specs.height = TC_MAX_V_FRAME_HEIGHT;
        tc_specs.format = TC_CODEC_RGB;

        /* then deduct missing parameters */
        if (tc_frc_code_to_value(tc_specs.frc, &fps) == TC_NULL_MATCH) {
            fps = 1.0; /* sane, very worst case value */
        }
/*        tc_specs.samples = (double)tc_specs.rate/fps; */
        tc_specs.samples = (double)tc_specs.rate;
        /* 
         * FIXME
         * ok, so we use a MUCH larger buffer (big enough to store 1 *second*
         * of raw audio, not 1 *frame*) than needed for reasons similar as 
         * seen for above video.
         * Most notably, this helps in keeping buffers large enough to be
         * suitable for encoder flush (see encode_lame.c first).
         */
    }
}

/* ------------------------------------------------------------------ */
/* NEW API, yet private                                               */
/* ------------------------------------------------------------------ */

/*
 * Threading notice:
 *
 * Generic code doesn't use any locking at all (yet).
 * That's was a design choice. For clarity, locking is
 * provided by back-compatibility wrapper functions,
 * or by any other higher-lever caller.
 *
 * Client code (= outside this code) MUST NEVER used not-thread
 * safe code.
 */


/*
 * tc_init_ring_framebuffer: (NOT thread safe)
 *     initialize a framebuffer ringbuffer by allocating needed
 *     amount of frames using given parameters.
 *
 * Parameters:
 *       rfb: ring framebuffer structure to initialize.
 *     specs: frame specifications to use for allocation.
 *     alloc: frame allocation function to use.
 *      free: frame disposal function to use.
 *      size: size of ringbuffer (number of frame to allocate)
 * Return Value:
 *      > 0: wrong (NULL) parameters
 *        0: succesfull
 *      < 0: allocation failed for one or more framesbuffers/internal error
 */
static int tc_init_ring_framebuffer(TCRingFrameBuffer *rfb,
                                    const TCFrameSpecs *specs,
                                    TCFrameAllocFn alloc,
                                    TCFrameFreeFn free,
                                    int size)
{
    if (rfb == NULL || specs == NULL || size < 0
     || alloc == NULL || free == NULL) {
        return 1;
    }
    size = (size > 0) ?size :1; /* allocate at least one frame */

    rfb->frames = tc_malloc(size * sizeof(TCFramePtr));
    if (rfb->frames == NULL) {
        return -1;
    }

    rfb->specs = specs;
    rfb->alloc = alloc;
    rfb->free  = free;

    for (rfb->last = 0; rfb->last < size; rfb->last++) {
        rfb->frames[rfb->last] = rfb->alloc(rfb->specs);
        if (TCFRAMEPTR_IS_NULL(rfb->frames[rfb->last])) {
            if (verbose >= TC_DEBUG) {
                tc_log_error(__FILE__, "failed frame allocation");
            }
            return -1;
        }

        rfb->frames[rfb->last].generic->status = TC_FRAME_NULL;
        rfb->frames[rfb->last].generic->bufid = rfb->last;
    }

    rfb->next   = 0;

    rfb->null   = size;
    rfb->empty  = 0;
    rfb->wait   = 0;
    rfb->locked = 0;
    rfb->ready  = 0;

    if (verbose >= TC_STATS) {
        tc_log_info(__FILE__, "allocated %i frames in ringbuffer", size);
    }
    return 0;
}

/*
 * tc_fini_ring_framebuffer: (NOT thread safe)
 *     finalize a framebuffer ringbuffer by freeing all acquired
 *     resources (framebuffer memory).
 *
 * Parameters:
 *       rfb: ring framebuffer structure to finalize.
 * Return Value:
 *       None.
 */
static void tc_fini_ring_framebuffer(TCRingFrameBuffer *rfb)
{
    if (rfb != NULL && rfb->free != NULL) {
        int i = 0, n = rfb->last;
    
        for (i = 0; i < rfb->last; i++) {
            rfb->free(rfb->frames[i]);
        }
        tc_free(rfb->frames);
        rfb->last = 0;
    
        if (verbose >= TC_STATS) {
            tc_log_info(__FILE__, "freed %i frames in ringbuffer", n);
        }
    }
}

/*
 * tc_ring_framebuffer_retrieve_frame: (NOT thread safe)
 *      retrieve next unclaimed (TC_FRAME_NULL) framebuffer from
 *      ringbuffer and return a pointer to it for later usage
 *      by client code.
 *
 * Parameters:
 *      rfb: ring framebuffer to use
 * Return Value:
 *      Always a framebuffer generic pointer. That can be pointing to
 *      NULL if there aren't no more unclaimed (TC_FRAME_NULL) framebuffer
 *      avalaible; otherwise it contains
 *      a pointer to retrieved framebuffer.
 *      DO NOT *free() such pointer directly! use
 *      tc_ring_framebuffer_release_frame() instead!
 */
static TCFramePtr tc_ring_framebuffer_retrieve_frame(TCRingFrameBuffer *rfb)
{
    TCFramePtr ptr;
    ptr.generic = NULL;

    if (rfb != NULL) {
        int i = 0;

        ptr = rfb->frames[rfb->next];
        for (i = 0; i < rfb->last; i++) {
            if (ptr.generic->status == TC_FRAME_NULL) {
                break;
            }
            rfb->next++;
            rfb->next %= rfb->last;
            ptr = rfb->frames[rfb->next];
        }

        if (ptr.generic->status != TC_FRAME_NULL) {
            if (verbose >= TC_FLIST) {
                tc_log_warn(__FILE__, "retrieved buffer=%i, but not empty",
                                      ptr.generic->status);
            }
            ptr.generic = NULL; /* enforce NULL-ness */
        } else {
            if (verbose >= TC_FLIST) {
                tc_log_msg(__FILE__, "retrieved buffer = %i [%i]",
                                     rfb->next, ptr.generic->bufid);
            }
            /* adjust internal pointer */
            rfb->null--;
            rfb->next++;
            rfb->next %= rfb->last;
        }
    }
    return ptr;
}

/*
 * tc_ring_framebuffer_release_frame: (NOT thread safe)
 *      release a previously retrieved frame back to ringbuffer,
 *      removing claim from it and making again avalaible (TC_FRAME_NULL).
 *
 * Parameters:
 *         rfb: ring framebuffer to use.
 *       frame: generic pointer to frame to release.
 * Return Value:
 *       > 0: wrong (NULL) parameters.
 *         0: succesfull
 *       < 0: internal error (frame to be released isn't empty).
 */
static int tc_ring_framebuffer_release_frame(TCRingFrameBuffer *rfb,
                                             TCFramePtr frame)
{
    if (rfb == NULL || TCFRAMEPTR_IS_NULL(frame)) {
        return 1;
    }
    if (verbose >= TC_FLIST) {
        tc_log_msg(__FILE__, "releasing frame #%i [%i]",
                   frame.generic->bufid, rfb->next);
    }
    frame.generic->status = TC_FRAME_NULL;
    rfb->null++;
    return 0;
}

/*
 * tc_ring_framebuffer_register_frame: (NOT thread safe)
 *      retrieve and register a framebuffer from a ringbuffer by
 *      attaching an ID to it, setup properly status and updating
 *      internal ringbuffer counters.
 *
 *      That's the function that client code is supposed to use
 *      (maybe wrapped by some thin macros to save status setting troubles).
 *      In general, dont' use retrieve_frame directly, use register_frame
 *      instead.
 *
 * Parameters:
 *         rfb: ring framebuffer to use
 *          id: id to attach to registered framebuffer
 *      status: status of framebuffer to register. This was needed to
 *              make registering process multi-purpose.
 * Return Value:
 *      Always a generic framebuffer pointer. That can be pointing to NULL
 *      if there isn't no more framebuffer avalaible on given ringbuffer;
 *      otherwise, it will point to a valid framebuffer.
 */
static TCFramePtr tc_ring_framebuffer_register_frame(TCRingFrameBuffer *rfb,
                                                     int id, int status)
{
    TCFramePtr ptr;

    /* retrive a valid pointer from the pool */
    if (verbose >= TC_FLIST) {
        tc_log_msg(__FILE__, "register frame id = %i", id);
    }
#ifdef STATBUFFER
    ptr = tc_ring_framebuffer_retrieve_frame(rfb);
#else
    ptr = rfb->alloc(rfb->specs);
#endif

    if (!TCFRAMEPTR_IS_NULL(ptr)) {
        if (status == TC_FRAME_EMPTY) {
            rfb->empty++;
            /* blank common attributes */
            memset(ptr.generic, 0, sizeof(frame_list_t));
            ptr.generic->id = id;
        } else if (status == TC_FRAME_WAIT) {
            rfb->wait++;
        }
        ptr.generic->status = status;

        /* enforce */
        ptr.generic->next = NULL;
        ptr.generic->prev = NULL;

        if (verbose >= TC_FLIST) {
            tc_ring_framebuffer_dump_status(rfb, "register_frame");
        }
    }
    return ptr; 
}

/*
 * tc_ring_framebuffer_remove_frame: (NOT thread safe)
 *      De-register and release a given framebuffer;
 *      also updates internal ringbuffer counters.
 *      
 *      That's the function that client code is supposed to use.
 *      In general, don't use release_frame directly, use remove_frame
 *      instead.
 *
 * Parameters:
 *        rfb: ring framebuffer to use.
 *      frame: generic pointer to frambuffer to remove.
 * Return Value:
 *      None.
 */
static void tc_ring_framebuffer_remove_frame(TCRingFrameBuffer *rfb,
                                             TCFramePtr frame)
{
    if (rfb != NULL || !TCFRAMEPTR_IS_NULL(frame)) {
        if (frame.generic->status == TC_FRAME_READY) {
            rfb->ready--;
        }
        if (frame.generic->status == TC_FRAME_LOCKED) {
            rfb->locked--;
        }
        /* release valid pointer to pool */
#ifdef STATBUFFER
        tc_ring_framebuffer_release_frame(rfb, frame);
#else
        rfb->free(frame);
#endif

        if (verbose >= TC_FLIST) {
            tc_ring_framebuffer_dump_status(rfb, "remove_frame");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Backwared-compatible API                                           */
/* ------------------------------------------------------------------ */

int aframe_alloc(int num)
{
    return tc_init_ring_framebuffer(&tc_audio_ringbuffer, &tc_specs,
                                    tc_audio_alloc, tc_audio_free, num);
}

int vframe_alloc(int num)
{
    return tc_init_ring_framebuffer(&tc_video_ringbuffer, &tc_specs,
                                    tc_video_alloc, tc_video_free, num);
}


void aframe_free(void)
{
    tc_fini_ring_framebuffer(&tc_audio_ringbuffer);
}

void vframe_free(void)
{
    tc_fini_ring_framebuffer(&tc_video_ringbuffer);
}


/* ------------------------------------------------------------------ */

/* 
 * Macro VS generic functions like above:
 *
 * I've used generic code and TCFramePtr in every place I was
 * capable to introduce them in a *clean* way without using any
 * casting. Of course there is still a lot of room for improvements,
 * but back compatibility is an issue too. I'd like to get rid
 * of all those macro and swtich to pure generic code of course,
 * so this will be improved in future revisions. In the
 * meantime, patches and suggestions welcome ;)             -- FR
 */

#define LIST_FRAME_APPEND(ptr, tail) do { \
    if ((tail) != NULL) { \
        (tail)->next = (ptr); \
        (ptr)->prev = (tail); \
    } \
    (tail) = (ptr); \
} while (0)

#define LIST_FRAME_INSERT(ptr, head) do { \
    if ((head) == NULL) { \
        (head) = ptr; \
    } \
} while (0)


aframe_list_t *aframe_register(int id)
{
    int interrupted = TC_FALSE;
    TCFramePtr frame;

    pthread_mutex_lock(&aframe_list_lock);

    if (verbose >= TC_FLIST)
        tc_log_msg(__FILE__, "(A|register) requesting a new audio frame");

    while ((!interrupted && tc_import_audio_running())
      && tc_audio_ringbuffer.null == 0) {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(A|register) audio frame not ready, waiting");
        pthread_cond_wait(&audio_import_cond, &aframe_list_lock);
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(A|register) audio frame wait ended");
        interrupted = !tc_running();
    }

    if (interrupted) {
        frame.audio = NULL;
    } else {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "new audio frame ready");

        frame = tc_ring_framebuffer_register_frame(&tc_audio_ringbuffer,
                                                   id, TC_FRAME_EMPTY);
        if (!TCFRAMEPTR_IS_NULL(frame)) {
            /* 
             * complete initialization:
             * backward-compatible stuff
             */
            LIST_FRAME_APPEND(frame.audio, aframe_list_tail);
            /* first frame registered must set aframe_list_head */
            LIST_FRAME_INSERT(frame.audio, aframe_list_head);
        }
    }
    pthread_mutex_unlock(&aframe_list_lock);
    return frame.audio;
}

vframe_list_t *vframe_register(int id)
{
    int interrupted = TC_FALSE;
    TCFramePtr frame;
    
    pthread_mutex_lock(&vframe_list_lock);

    if (verbose >= TC_FLIST)
        tc_log_msg(__FILE__, "(V|register) requesting a new video frame");

    while ((!interrupted && tc_import_video_running())
      && tc_video_ringbuffer.null == 0) {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(V|register) video frame not ready, waiting");
        pthread_cond_wait(&video_import_cond, &vframe_list_lock);
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(V|register) video frame wait ended");
        interrupted = !tc_running();
    }

    if (interrupted) {
        frame.video = NULL;
    } else {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "new video frame ready");

        frame = tc_ring_framebuffer_register_frame(&tc_video_ringbuffer,
                                                   id, TC_FRAME_EMPTY); 
        if (!TCFRAMEPTR_IS_NULL(frame)) {
            /* 
             * complete initialization:
             * backward-compatible stuff
             */
            LIST_FRAME_APPEND(frame.video, vframe_list_tail);
            /* first frame registered must set vframe_list_head */
            LIST_FRAME_INSERT(frame.video, vframe_list_head);
        }
    }
    pthread_mutex_unlock(&vframe_list_lock);
    return frame.video;
}


/* ------------------------------------------------------------------ */


#define LIST_FRAME_LINK(ptr, f, tail) do { \
     /* insert after ptr */ \
    (ptr)->next = (f)->next; \
    (f)->next = (ptr); \
    (ptr)->prev = (f); \
    \
    if ((ptr)->next == NULL) { \
        /* must be last ptr in the list */ \
        (tail) = (ptr); \
    } \
} while (0)


aframe_list_t *aframe_dup(aframe_list_t *f)
{
    int interrupted = TC_FALSE;
    TCFramePtr frame;

    if (f == NULL) {
        tc_log_warn(__FILE__, "aframe_dup: empty frame");
        return NULL;
    }

    pthread_mutex_lock(&aframe_list_lock);

    while (!interrupted && tc_audio_ringbuffer.null == 0) {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(A|dup) audio frame not ready, waiting");
        pthread_cond_wait(&audio_import_cond, &aframe_list_lock);
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(A|dup) audio frame wait ended");
        interrupted = !tc_running();
    }

    frame = tc_ring_framebuffer_register_frame(&tc_audio_ringbuffer,
                                               0, TC_FRAME_WAIT);
    if (!TCFRAMEPTR_IS_NULL(frame)) {
        aframe_copy(frame.audio, f, 1);

        LIST_FRAME_LINK(frame.audio, f, aframe_list_tail);
    }
    pthread_mutex_unlock(&aframe_list_lock);
    return frame.audio;
}

vframe_list_t *vframe_dup(vframe_list_t *f)
{
    int interrupted = TC_FALSE;
    TCFramePtr frame;

    if (f == NULL) {
        tc_log_warn(__FILE__, "vframe_dup: empty frame");
        return NULL;
    }

    pthread_mutex_lock(&vframe_list_lock);

    while (!interrupted && tc_video_ringbuffer.null == 0) {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(V|dup) video frame not ready, waiting");
        pthread_cond_wait(&video_import_cond, &vframe_list_lock);
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(V|dup) video frame wait ended");
        interrupted = !tc_running();
    }

    frame = tc_ring_framebuffer_register_frame(&tc_video_ringbuffer,
                                               0, TC_FRAME_WAIT);
    if (!TCFRAMEPTR_IS_NULL(frame)) {
        vframe_copy(frame.video, f, 1);

        LIST_FRAME_LINK(frame.video, f, vframe_list_tail);
    }
    pthread_mutex_unlock(&vframe_list_lock);
    return frame.video;
}


/* ------------------------------------------------------------------ */

#define LIST_FRAME_REMOVE(ptr, head, tail) do { \
    if ((ptr)->prev != NULL) { \
        ((ptr)->prev)->next = (ptr)->next; \
    } \
    if ((ptr)->next != NULL) { \
        ((ptr)->next)->prev = (ptr)->prev; \
    } \
    \
    if ((ptr) == (tail)) { \
        (tail) = (ptr)->prev; \
    } \
    if ((ptr) == (head)) { \
        (head) = (ptr)->next; \
    } \
} while (0)

void aframe_remove(aframe_list_t *ptr)
{
    if (ptr == NULL) {
        tc_log_warn(__FILE__, "aframe_remove: given NULL frame pointer");
    } else {
        TCFramePtr frame;
        frame.audio = ptr;

        pthread_mutex_lock(&aframe_list_lock);

        LIST_FRAME_REMOVE(ptr, aframe_list_head, aframe_list_tail);

        tc_ring_framebuffer_remove_frame(&tc_audio_ringbuffer, frame);
        pthread_cond_signal(&audio_import_cond);

        pthread_mutex_unlock(&aframe_list_lock);
    }
}

void vframe_remove(vframe_list_t *ptr)
{
    if (ptr == NULL) {
        tc_log_warn(__FILE__, "vframe_remove: given NULL frame pointer");
    } else {
        TCFramePtr frame;
        frame.video = ptr;

        pthread_mutex_lock(&vframe_list_lock);
        
        LIST_FRAME_REMOVE(ptr, vframe_list_head, vframe_list_tail);
        
        tc_ring_framebuffer_remove_frame(&tc_video_ringbuffer, frame);
        pthread_cond_signal(&video_import_cond);

        pthread_mutex_unlock(&vframe_list_lock);
    }
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

static aframe_list_t *aframe_retrieve_nowait(void)
{
    aframe_list_t *ptr = NULL;
    pthread_mutex_lock(&aframe_list_lock);

    if (verbose >= TC_CLEANUP) {
        tc_log_msg(__FILE__, "(A|retrieve_nowait) requesting a new audio frame");
    }
    if (aframe_list_head != NULL && aframe_list_head->status == TC_FRAME_READY) {
        ptr = aframe_list_head;
    }
    if (verbose >= TC_CLEANUP) {
        tc_log_msg(__FILE__, "got a new audio frame reference: %p", ptr);
    }
    pthread_mutex_unlock(&aframe_list_lock);
    return ptr;
}


static vframe_list_t *vframe_retrieve_nowait(void)
{
    vframe_list_t *ptr = NULL;
    pthread_mutex_lock(&vframe_list_lock);

    if (verbose >= TC_CLEANUP) {
        tc_log_msg(__FILE__, "(V|retrieve_nowait) requesting a new video frame");
    }
    if (vframe_list_head != NULL && vframe_list_head->status == TC_FRAME_READY) {
        ptr = vframe_list_head;
    }
    if (verbose >= TC_CLEANUP) {
        tc_log_msg(__FILE__, "got a new video frame reference: %p", ptr);
    }
    pthread_mutex_unlock(&vframe_list_lock);
    return ptr;
}


/* ------------------------------------------------------------------ */

aframe_list_t *aframe_retrieve(void)
{
    int interrupted = TC_FALSE;
    aframe_list_t *ptr = NULL;
    pthread_mutex_lock(&aframe_list_lock);

    if (verbose >= TC_FLIST)
        tc_log_msg(__FILE__, "(A|retrieve) requesting a new audio frame");
    while (!interrupted
      && (aframe_list_head == NULL
        || aframe_list_head->status != TC_FRAME_READY)) {
        if (verbose >= TC_FLIST) {
            tc_log_msg(__FILE__, "(A|retrieve) audio frame not ready, waiting");
            tc_ring_framebuffer_dump_status(&tc_audio_ringbuffer, "retrieve");
        }
        pthread_cond_wait(&audio_export_cond, &aframe_list_lock);
        if (verbose >= TC_FLIST)
           tc_log_msg(__FILE__, "(A|retrieve) audio wait just ended");
        interrupted = !tc_running();
    }

    if (!interrupted) {
        ptr = aframe_list_head;
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "got a new audio frame reference: %p", ptr);
    }
    pthread_mutex_unlock(&aframe_list_lock);
    return ptr;
}


vframe_list_t *vframe_retrieve(void)
{
    int interrupted = TC_FALSE;
    vframe_list_t *ptr = NULL;
    pthread_mutex_lock(&vframe_list_lock);

    if (verbose >= TC_FLIST)
        tc_log_msg(__FILE__, "(V|retrieve) requesting a new video frame");
    while (!interrupted
      && (vframe_list_head == NULL
        || vframe_list_head->status != TC_FRAME_READY)) {
        if (verbose >= TC_FLIST) {
            tc_log_msg(__FILE__, "(V|retrieve) video frame not ready, waiting");
            tc_ring_framebuffer_dump_status(&tc_video_ringbuffer, "retrieve");
        }
        pthread_cond_wait(&video_export_cond, &vframe_list_lock);
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(V|retrieve) video wait just ended");
        interrupted = !tc_running();
    }

    if (!interrupted) {
        ptr = vframe_list_head;
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "got a new video frame reference: %p", ptr);
    }
    pthread_mutex_unlock(&vframe_list_lock);
    return ptr;
}

#undef LIST_FRAME_RETRIEVE

/* ------------------------------------------------------------------ */

void aframe_flush(void)
{
    int i = 0, done = TC_FALSE;
  
    do {
        aframe_list_t *ptr = aframe_retrieve_nowait();
        if (!ptr) {
            done = TC_TRUE;
        } else {
            if (verbose >= TC_CLEANUP) {
                tc_log_msg(__FILE__,
                           "flushing audio buffer id=[%i] bufid=[%i]",
                           ptr->id, ptr->bufid); 
            }
            aframe_remove(ptr);
            i++;
        }            
    } while (!done);
  
    if (verbose >= TC_CLEANUP) {
        tc_log_msg(__FILE__, "flushed %i audio frames", i); 
    }
}

void vframe_flush(void)
{
    int i = 0, done = TC_FALSE;
  
    do {
        vframe_list_t *ptr = vframe_retrieve_nowait();
        if (!ptr) {
            done = TC_TRUE;
        } else {
            if (verbose >= TC_CLEANUP) {
                tc_log_msg(__FILE__,
                           "flushing video buffer id=[%i] bufid=[%i]",
                           ptr->id, ptr->bufid); 
            }
            vframe_remove(ptr);
            i++;
        }            
    } while (!done);
  
    if (verbose >= TC_CLEANUP) {
        tc_log_msg(__FILE__, "flushed %i video frames", i); 
    }
}

void tc_framebuffer_flush(void)
{
    aframe_flush();
    vframe_flush();
}


/* ------------------------------------------------------------------ */

#define DEC_COUNTERS(RFB, STATUS) do { \
    if ((STATUS) == TC_FRAME_READY) { \
        (RFB)->ready--; \
    } \
    if ((STATUS) == TC_FRAME_LOCKED) { \
        (RFB)->locked--; \
    } \
    if ((STATUS) == TC_FRAME_WAIT) { \
       (RFB)->wait--; \
    } \
} while(0)

#define INC_COUNTERS(RFB, STATUS) do { \
    if ((STATUS) == TC_FRAME_READY) { \
        (RFB)->ready++; \
    } \
    if ((STATUS) == TC_FRAME_LOCKED) { \
        (RFB)->locked++; \
    } \
    if ((STATUS) == TC_FRAME_WAIT) { \
       (RFB)->wait++; \
    } \
} while(0)

#define FRAME_SET_STATUS(RFB, PTR, NEW_STATUS) do { \
    DEC_COUNTERS((RFB), (PTR)->status); \
    (PTR)->status = (NEW_STATUS); \
    INC_COUNTERS((RFB), (PTR)->status); \
} while (0)

#define FRAME_LOOKUP(RFB, PTR, OLD_STATUS, NEW_STATUS) do { \
     /* move along the chain and check for status */ \
    for (; (PTR) != NULL; (PTR) = (PTR)->next) { \
        if ((PTR)->status == (OLD_STATUS)) { \
            /* found matching frame */ \
            FRAME_SET_STATUS(RFB, PTR, NEW_STATUS); \
            break; \
        } \
    } \
} while (0)


aframe_list_t *aframe_reserve(void)
{
    int interrupted = TC_FALSE;
    aframe_list_t *ptr = NULL;

    pthread_mutex_lock(&aframe_list_lock);

    while (!interrupted && tc_audio_ringbuffer.wait == 0) {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(A|reserve) audio frame not ready, waiting");
        pthread_cond_wait(&audio_filter_cond, &aframe_list_lock);
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(A|reserve) audio wait just ended");
        interrupted = !tc_running();
    }

    if (!interrupted) {
        ptr = aframe_list_head;
        FRAME_LOOKUP(&tc_audio_ringbuffer, ptr, TC_FRAME_WAIT, TC_FRAME_LOCKED);
    }

    pthread_mutex_unlock(&aframe_list_lock);
    return ptr;
}

vframe_list_t *vframe_reserve(void)
{
    int interrupted = TC_FALSE;
    vframe_list_t *ptr = NULL;

    pthread_mutex_lock(&vframe_list_lock);

    while (!interrupted && tc_video_ringbuffer.wait == 0) {
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(V|reserve) video frame not ready, waiting");
        pthread_cond_wait(&video_filter_cond, &vframe_list_lock);
        if (verbose >= TC_FLIST)
            tc_log_msg(__FILE__, "(V|reserve) video wait just ended");
        interrupted = !tc_running();
    }

    if (!interrupted) {
        ptr = vframe_list_head;
        FRAME_LOOKUP(&tc_video_ringbuffer, ptr, TC_FRAME_WAIT, TC_FRAME_LOCKED);
    }

    pthread_mutex_unlock(&vframe_list_lock);
    return ptr;
}

#undef FRAME_LOOKUP

/* ------------------------------------------------------------------ */


#define FRAME_SET_EXT_STATUS(RFB, PTR, NEW_STATUS) do { \
    if ((PTR)->status == TC_FRAME_EMPTY) { \
        (RFB)->empty--; \
    } \
    FRAME_SET_STATUS((RFB), (PTR), (NEW_STATUS)); \
    if ((PTR)->status == TC_FRAME_EMPTY) { \
        (RFB)->empty++; \
    } \
} while (0)


void aframe_push_next(aframe_list_t *ptr, int status)
{
    if (ptr == NULL) {
        /* a bit more of paranoia */
        tc_log_warn(__FILE__, "aframe_push_next: given NULL frame pointer");
    } else {
        pthread_mutex_lock(&aframe_list_lock);
        FRAME_SET_EXT_STATUS(&tc_audio_ringbuffer, ptr, status);

        if (status == TC_FRAME_WAIT) {
            pthread_cond_signal(&audio_filter_cond);
        } else if (status == TC_FRAME_READY && ptr == aframe_list_head) { // XXX
            pthread_cond_signal(&audio_export_cond);
        }
        if (verbose >= TC_FLIST) {
            tc_ring_framebuffer_dump_status(&tc_audio_ringbuffer, "push_next");
        }
        pthread_mutex_unlock(&aframe_list_lock);
    }
}


void vframe_push_next(vframe_list_t *ptr, int status)
{
    if (ptr == NULL) {
        /* a bit more of paranoia */
        tc_log_warn(__FILE__, "vframe_push_next: given NULL frame pointer");
    } else {
        pthread_mutex_lock(&vframe_list_lock);
        FRAME_SET_EXT_STATUS(&tc_video_ringbuffer, ptr, status);

        if (status == TC_FRAME_WAIT) {
            pthread_cond_signal(&video_filter_cond);
        } else if (status == TC_FRAME_READY && ptr == vframe_list_head) { // XXX
            pthread_cond_signal(&video_export_cond);
        }
        if (verbose >= TC_FLIST) {
            tc_ring_framebuffer_dump_status(&tc_video_ringbuffer, "push_next");
        }
        pthread_mutex_unlock(&vframe_list_lock);
    }
}

#undef FRAME_SET_STATUS
#undef FRAME_SET_EXT_STATUS

/* ------------------------------------------------------------------ */


void aframe_dump_status(void)
{
    tc_ring_framebuffer_dump_status(&tc_audio_ringbuffer,
                                    "audio buffer status");
}

void vframe_dump_status(void)
{
    tc_ring_framebuffer_dump_status(&tc_video_ringbuffer,
                                    "video buffer status");
}

int vframe_have_more(void)
{
    int ret;
    pthread_mutex_lock(&vframe_list_lock);
    ret = (vframe_list_tail == NULL) ?0 :1;
    pthread_mutex_unlock(&vframe_list_lock);
    return ret;
}

int aframe_have_more(void)
{
    int ret;
    pthread_mutex_lock(&aframe_list_lock);
    ret = (aframe_list_tail == NULL) ?0 :1;
    pthread_mutex_unlock(&aframe_list_lock);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Frame copying routines                                             */
/* ------------------------------------------------------------------ */

void aframe_copy(aframe_list_t *dst, const aframe_list_t *src,
                 int copy_data)
{
    if (!dst || !src) {
        tc_log_warn(__FILE__, "aframe_copy: given NULL frame pointer");
    	return;
    }

    /* copy all common fields with just one move */
    ac_memcpy(dst, src, sizeof(frame_list_t));
    
    if (copy_data == 1) {
        /* really copy video data */
        ac_memcpy(dst->audio_buf, src->audio_buf, dst->audio_size);
    } else {
        /* soft copy, new frame points to old audio data */
        dst->audio_buf = src->audio_buf;
    }
}

void vframe_copy(vframe_list_t *dst, const vframe_list_t *src,
                 int copy_data)
{
    if (!dst || !src) {
        tc_log_warn(__FILE__, "vframe_copy: given NULL frame pointer");
    	return;
    }

    /* copy all common fields with just one move */
    ac_memcpy(dst, src, sizeof(frame_list_t));
    
    dst->deinter_flag = src->deinter_flag;
    dst->free         = src->free;
    /* 
     * we assert that plane pointers *are already properly set*
     * we're focused on copy _content_ here.
     */

    if (copy_data == 1) {
        /* really copy video data */
        ac_memcpy(dst->video_buf, src->video_buf, dst->video_size);
    } else {
        /* soft copy, new frame points to old video data */
        dst->video_buf = src->video_buf;
    }
}

/*************************************************************************/

void vframe_get_counters(int *im, int *fl, int *ex)
{
    pthread_mutex_lock(&vframe_list_lock);
    *im = tc_video_ringbuffer.null + tc_video_ringbuffer.empty;
    *fl = tc_video_ringbuffer.wait + tc_video_ringbuffer.locked;
    *ex = tc_video_ringbuffer.ready;
    pthread_mutex_unlock(&vframe_list_lock);
}

void aframe_get_counters(int *im, int *fl, int *ex)
{
    pthread_mutex_lock(&aframe_list_lock);
    *im = tc_audio_ringbuffer.null + tc_audio_ringbuffer.empty;
    *fl = tc_audio_ringbuffer.wait + tc_audio_ringbuffer.locked;
    *ex = tc_audio_ringbuffer.ready;
    pthread_mutex_unlock(&aframe_list_lock);
}

void tc_framebuffer_get_counters(int *im, int *fl, int *ex)
{
    int v_im, v_fl, v_ex, a_im, a_fl, a_ex;

    vframe_get_counters(&v_im, &v_fl, &v_ex);
    aframe_get_counters(&a_im, &a_fl, &a_ex);

    *im = v_im + a_im;
    *fl = v_fl + a_fl;
    *ex = v_ex + a_ex;
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
