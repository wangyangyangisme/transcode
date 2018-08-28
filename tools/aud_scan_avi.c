/*
 *  aud_scan_avi.c
 *
 *  Scans the audio track - AVI specific functions
 *
 *  Copyright (C) Tilmann Bitterberg - June 2003
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "aud_scan_avi.h"
#include "aud_scan.h"

// ------------------------
// You must set the requested audio before entering this function
// the AVI file out must be filled with correct values.
// ------------------------

int sync_audio_video_avi2avi (double vid_ms, double *aud_ms, avi_t *in, avi_t *out)
{
    static char *data = NULL;
    int  vbr     = AVI_get_audio_vbr(out);
    int  mp3rate = AVI_audio_mp3rate(out);
    int  format  = AVI_audio_format(out);
    int  chan    = AVI_audio_channels(out);
    long rate    = AVI_audio_rate(out);
    int  bits    = AVI_audio_bits(out);
    long bytes   = 0;

    bits = (bits == 0)?16:bits;

    if (!data) data = malloc(48000*16*4);
    if (!data) fprintf (stderr, "Malloc failed at %s:%d\n", __FILE__, __LINE__);

    if (format == 0x1) {
	mp3rate = rate*chan*bits;
    } else {
	mp3rate *= 1000;
    }

    if (tc_format_ms_supported(format)) {

	while (*aud_ms < vid_ms) {

	    if( (bytes = AVI_read_audio_chunk(in, data)) < 0) {
		AVI_print_error("AVI audio read frame");
		//*aud_ms = vid_ms;
		return(-2);
	    }
	    //fprintf(stderr, "len (%ld)\n", bytes);

	    if(AVI_write_audio(out, data, bytes)<0) {
		AVI_print_error("AVI write audio frame");
		return(-1);
	    }

	    // pass-through null frames
	    if (bytes == 0) {
		*aud_ms = vid_ms;
		break;
	    }

	    if ( vbr && tc_get_audio_header(data, bytes, format, NULL, NULL, &mp3rate)<0) {
		// if this is the last frame of the file, slurp in audio chunks
		//if (n == frames-1) continue;
		*aud_ms = vid_ms;
	    } else  {
		if (vbr) mp3rate *= 1000;
		*aud_ms += (bytes*8.0*1000.0)/(double)mp3rate;
	    }
	    /*
	       fprintf(stderr, "%s track (%d) %8.0lf->%8.0lf len (%ld) rate (%ld)\n",
	       format==0x55?"MP3":format==0x1?"PCM":"AC3",
	       j, vid_ms, aud_ms[j], bytes, mp3rate);
	     */
	}

    } else { // fallback for not supported audio format

	do {
	    if ( (bytes = AVI_read_audio_chunk(in, data) ) < 0) {
		AVI_print_error("AVI audio read frame");
		return -2;
	    }

	    if(AVI_write_audio(out, data, bytes)<0) {
		AVI_print_error("AVI write audio frame");
		return(-1);
	    }
	} while (AVI_can_read_audio(in));
    }


    return 0;
}

int sync_audio_video_avi2avi_ro (double vid_ms, double *aud_ms, avi_t *in)
{
    static char *data = NULL;
    int  vbr     = AVI_get_audio_vbr(in);
    int  mp3rate = AVI_audio_mp3rate(in);
    int  format  = AVI_audio_format(in);
    int  chan    = AVI_audio_channels(in);
    long rate    = AVI_audio_rate(in);
    int  bits    = AVI_audio_bits(in);
    long bytes   = 0;

    bits = (bits == 0)?16:bits;

    if (!data) data = malloc(48000*16*4);
    if (!data) fprintf (stderr, "Malloc failed at %s:%d\n", __FILE__, __LINE__);

    if (format == 0x1) {
	mp3rate = rate*chan*bits;
    } else {
	mp3rate *= 1000;
    }

    if (tc_format_ms_supported(format)) {

	while (*aud_ms < vid_ms) {

	    if( (bytes = AVI_read_audio_chunk(in, data)) < 0) {
		AVI_print_error("AVI audio read frame");
		//*aud_ms = vid_ms;
		return(-2);
	    }
	    //fprintf(stderr, "len (%ld)\n", bytes);

	    // pass-through null frames
	    if (bytes == 0) {
		*aud_ms = vid_ms;
		break;
	    }

	    if ( vbr && tc_get_audio_header(data, bytes, format, NULL, NULL, &mp3rate)<0) {
		// if this is the last frame of the file, slurp in audio chunks
		//if (n == frames-1) continue;
		*aud_ms = vid_ms;
	    } else  {
		if (vbr) mp3rate *= 1000;
		*aud_ms += (bytes*8.0*1000.0)/(double)mp3rate;
	    }
	    /*
	       fprintf(stderr, "%s track (%d) %8.0lf->%8.0lf len (%ld) rate (%ld)\n",
	       format==0x55?"MP3":format==0x1?"PCM":"AC3",
	       j, vid_ms, aud_ms[j], bytes, mp3rate);
	     */
	}

    } else { // fallback for not supported audio format

	do {
	    if ( (bytes = AVI_read_audio_chunk(in, data) ) < 0) {
		AVI_print_error("AVI audio read frame");
		return -2;
	    }
	} while (AVI_can_read_audio(in));
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
