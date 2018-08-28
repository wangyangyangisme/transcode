/*
 *  filter_lowpass.c
 *
 *  Based on `filt'-code by Markus Wandel
 *    http://wandel.ca/homepage/audiohacks.html
 *  Copyright (C) Tilmann Bitterberg
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

#define MOD_NAME    "filter_lowpass.so"
#define MOD_VERSION "v0.1.0 (2002-02-26)"
#define MOD_CAP     "High and low pass filter"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"

#include <stdint.h>


static short *array_l = NULL, *array_r = NULL;
static int taps     = 30;
static int highpass = 0;
static int p        = 0;
static int mono     = 0;

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/


int tc_filter(frame_list_t *ptr_, char *options)
{
  aframe_list_t *ptr = (aframe_list_t *)ptr_;
  vob_t *vob=NULL;

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    // filter init ok.

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    if(options != NULL) {
	optstr_get(options, "taps", "%d", &taps);
    }

    if(taps < 0) {
	taps = -taps;
	highpass = 1;
    }

    array_r = tc_malloc (taps * sizeof(short));
    array_l = tc_malloc (taps * sizeof(short));

    if (!array_r || !array_l) {
	tc_log_error(MOD_NAME, "Malloc failed in %d", __LINE__);
	return TC_IMPORT_ERROR;
    }

    memset (array_r, 0, taps * sizeof(short));
    memset (array_l, 0, taps * sizeof(short));

    if (vob->a_chan == 1) {
	mono = 1;
    }

    if (vob->a_bits != 16) {
	tc_log_error(MOD_NAME, "This filter only supports 16 bit samples");
	return (TC_IMPORT_ERROR);
    }

    return(0);
  }

  //----------------------------------
  //
  // filter get config
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_GET_CONFIG && options) {

      char buf[255];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "AE", "1");

      tc_snprintf (buf, sizeof(buf), "%d", taps);
      optstr_param (options, "taps", "strength (may be negative)", "%d", buf, "-50", "50");
  }
  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

      if (array_r) { free (array_r); array_r = NULL; }
      if (array_l) { free (array_l); array_l = NULL; }

      return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------

  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if(ptr->tag & TC_PRE_S_PROCESS && ptr->tag & TC_AUDIO) {
      int i, j;
      int al = 0, ar = 0;
      short *s = (short *)ptr->audio_buf;

      if (taps == 0) return 0;

      if (mono) {

	  // mono, 16bit only
	  for (i = 0; i<ptr->audio_size>>1; i++) {
	      array_r[p] = s[i];
	      for (j=0; j<taps; j++) {
		  ar += array_r[j];
	      }
	      p = (p+1) % taps;
	      ar = ar / taps;
	      if(highpass) {
		  s[i] -= ar;
	      } else {
		  s[i]  = ar;
	      }
	  }

      } else {

	  // stereo, 16bit only
	  for (i = 0; i<ptr->audio_size>>1; i++) {
	      array_l[p] = s[i+0];
	      array_r[p] = s[i+1];
	      for (j=0; j<taps; j++) {
		  al += array_l[j];
		  ar += array_r[j];
	      }
	      p = (p+1) % taps;
	      al = al / taps;
	      ar = ar / taps;
	      if(highpass) {
		  s[i+0] -= al;
		  s[i+1] -= ar;
	      } else {
		  s[i+0]  = al;
		  s[i+1]  = ar;
	      }
	  }
      }
  }

  return(0);
}
