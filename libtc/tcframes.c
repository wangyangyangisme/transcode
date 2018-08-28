/*
 * tcframes.c -- common generic audio/video/whatever frame allocation/disposal
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


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "src/tc_defaults.h"

#include "tcframes.h"

int tc_video_planes_size(size_t psizes[3],
                         int width, int height, int format)
{
    switch (format) {
      case CODEC_RAW: /* worst case paranoia, fallback */
      case CODEC_RAW_RGB: /* worst case paranoia again, fallback */
      case CODEC_RAW_YUV: /* worst case paranoia again, fallback */
      case CODEC_RGB: /* backward compatibility, fallback */
      case TC_CODEC_RGB:
        psizes[0] = width * height;
        psizes[1] = width * height;
        psizes[2] = width * height;
        break;
      case CODEC_YUV422: /* backward compatibility, fallback */
      case TC_CODEC_YUV422P:
        psizes[0] = width * height;
        psizes[1] = width * height / 2;
        psizes[2] = width * height / 2;
        break;
      case CODEC_YUV: /* backward compatibility, fallback */
      case TC_CODEC_YUV420P:
        psizes[0] = width * height;
        psizes[1] = width * height / 4;
        psizes[2] = width * height / 4;
        break;
      default: /* unknown */
        psizes[0] = 0;
        psizes[1] = 0;
        psizes[2] = 0;
        return TC_NULL_MATCH;
    }
    return 0;
}

/* blank )set to zero) rightmost X bits of I */
#define TRUNC_VALUE(i, x)	(((i) >> (x)) << (x))

size_t tc_audio_frame_size(double samples, int channels,
                           int bits, int *adjust)
{
    double rawsize = samples * (bits/8) * channels;
    size_t asize = TRUNC_VALUE(((size_t)rawsize), 2);
    int leap1 = 0, leap2 = 0, leap = 0;

    leap1 = TC_LEAP_FRAME * (rawsize - asize);
    leap2 = -leap1 + TC_LEAP_FRAME * (bits/8) * channels;
    leap1 = TRUNC_VALUE(leap1, 2);
    leap2 = TRUNC_VALUE(leap2, 2);

    if (leap1 < leap2) {
        leap = leap1;
    } else {
        leap = -leap2;
    	asize += (bits/8) * channels;
    }
    *adjust = leap;
    return asize;
}

#undef TRUNC_VALUE

void tc_init_video_frame(vframe_list_t *vptr,
                         int width, int height, int format)
{
    size_t psizes[3];

    tc_video_planes_size(psizes, width, height, format);

    vptr->video_buf_RGB[0] = vptr->internal_video_buf_0;
    vptr->video_buf_RGB[1] = vptr->internal_video_buf_1;

    vptr->video_buf_Y[0] = vptr->internal_video_buf_0;
    vptr->video_buf_U[0] = vptr->video_buf_Y[0] + psizes[0];
    vptr->video_buf_V[0] = vptr->video_buf_U[0] + psizes[1];

    vptr->video_buf_Y[1] = vptr->internal_video_buf_1;
    vptr->video_buf_U[1] = vptr->video_buf_Y[1] + psizes[0];
    vptr->video_buf_V[1] = vptr->video_buf_U[1] + psizes[1];

    vptr->video_buf = vptr->internal_video_buf_0;
    vptr->video_buf2 = vptr->internal_video_buf_1;
    vptr->free = 1;

    vptr->video_size = psizes[0] + psizes[1] + psizes[2];
    vptr->video_len = vptr->video_size; /* default */
}

void tc_init_audio_frame(aframe_list_t *aptr,
                         double samples, int channels, int bits)
{
    int unused = 0;
    
    aptr->audio_size = tc_audio_frame_size(samples, channels, bits,
                                           &unused);
    aptr->audio_buf = aptr->internal_audio_buf;
}


vframe_list_t *tc_new_video_frame(int width, int height, int format,
                                  int partial)
{
    vframe_list_t *vptr = NULL;
    size_t psizes[3] = { 0, 0, 0 };

    int ret = tc_video_planes_size(psizes, width, height, format);
    if (ret == 0) {
        vptr = tc_alloc_video_frame(psizes[0] + psizes[1] + psizes[2],
                                    partial);

        if (vptr != NULL) {
            tc_init_video_frame(vptr, width, height, format);
        }
    }
    return vptr;
}

aframe_list_t *tc_new_audio_frame(double samples, int channels, int bits)
{
    aframe_list_t *aptr = NULL;
    int unused = 0;
    size_t asize = tc_audio_frame_size(samples, channels, bits, &unused);

    aptr = tc_alloc_audio_frame(asize);

    if (aptr != NULL) {
        tc_init_audio_frame(aptr, samples, channels, bits);
    }
    return aptr;
}

#define TC_FRAME_EXTRA_SIZE     128

/* 
 * About TC_FRAME_EXTRA_SIZE:
 *
 * This is an emergency parachute for codecs that delivers
 * encoded frames *larger* than raw ones. Such beasts exists,
 * even LZO does it in some (AFAIK uncommon) circumstances.
 *
 * On those cases, the Sane Thing To Do from the encoder
 * viewpoint is to deliver an header + payload content, where
 * 'header' is a standard frame header with one flag set
 * (1 bit in the best, very unlikely, case) meaning that 
 * following payload is uncompressed.
 *
 * So, EXTRA_SIZE is supposed to catch such (corner) cases
 * by providing enough extra data for sane headers (for example,
 * LZO header is 16 byte long).
 *
 * Please note that this issue affects only
 * demultiplexor -> decoder and
 * encoder -> multiplexor communications.
 *
 * Yes, that's a bit hackish. Anyone has a better, more generic
 * and clean solution? Remember that frames must be pre-allocated,
 * allocating them on-demand isn't a viable alternative.
 */

vframe_list_t *tc_alloc_video_frame(size_t size, int partial)
{
    vframe_list_t *vptr = tc_zalloc(sizeof(vframe_list_t));

#ifdef TC_FRAME_EXTRA_SIZE
    size += TC_FRAME_EXTRA_SIZE;
#endif

    if (vptr != NULL) {
#ifdef STATBUFFER
        vptr->internal_video_buf_0 = tc_bufalloc(size);
        if (vptr->internal_video_buf_0 == NULL) {
            tc_free(vptr);
            return NULL;
        }
        if (!partial) {
            vptr->internal_video_buf_1 = tc_bufalloc(size);
            if (vptr->internal_video_buf_1 == NULL) {
                tc_buffree(vptr->internal_video_buf_0);
                tc_free(vptr);
                return NULL;
            }
        } else {
            vptr->internal_video_buf_1 = NULL;
        }
        vptr->video_size = size;
#endif /* STATBUFFER */
    }
    return vptr;

}

aframe_list_t *tc_alloc_audio_frame(size_t size)
{
    aframe_list_t *aptr = tc_zalloc(sizeof(aframe_list_t));

#ifdef TC_FRAME_EXTRA_SIZE
    size += TC_FRAME_EXTRA_SIZE;
#endif

    if (aptr != NULL) {
#ifdef STATBUFFER
        aptr->internal_audio_buf = tc_bufalloc(size);
        if (aptr->internal_audio_buf == NULL) {
            tc_free(aptr);
            return NULL;
        }
        aptr->audio_size = size;
#endif /* STATBUFFER */
    }
    return aptr;
}

void tc_del_video_frame(vframe_list_t *vptr)
{
    if (vptr != NULL) {
#ifdef STATBUFFER
        if (vptr->internal_video_buf_1 != NULL) {
            tc_buffree(vptr->internal_video_buf_1);
        }
        tc_buffree(vptr->internal_video_buf_0);
#endif
        tc_free(vptr);
    }
}

void tc_del_audio_frame(aframe_list_t *aptr)
{
    if (aptr != NULL) {
#ifdef STATBUFFER
        tc_buffree(aptr->internal_audio_buf);
#endif
        tc_free(aptr);
    }
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
