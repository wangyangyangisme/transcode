/*
 *  filter_invert
 *
 *  Copyright (C) Tilmann Bitterberg - June 2002
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

#define MOD_NAME    "filter_invert.so"
#define MOD_VERSION "v0.1.4 (2003-10-12)"
#define MOD_CAP     "invert the image"
#define MOD_AUTHOR  "Tilmann Bitterberg"

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"


// basic parameter

typedef struct MyFilterData {
	unsigned int start;
	unsigned int end;
	unsigned int step;
	int boolstep;
} MyFilterData;

static MyFilterData *mfd = NULL;

/* should probably honor the other flags too */

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"    Invert an image\n"
"* Options\n"
"    'range' apply filter to [start-end]/step frames [0-oo/1]\n"
		, MOD_CAP);
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;

  int w;

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
      char buf[128];
      optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRY4O", "1");

      tc_snprintf(buf, 128, "%u-%u/%d", mfd->start, mfd->end, mfd->step);
      optstr_param (options, "range", "apply filter to [start-end]/step frames",
	      "%u-%u/%d", buf, "0", "oo", "0", "oo", "1", "oo");

      return 0;
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return(-1);

    mfd = tc_malloc (sizeof(MyFilterData));
    if(mfd == NULL)
        return (-1);


    mfd->start=0;
    mfd->end=(unsigned int)-1;
    mfd->step=1;

    if (options != NULL) {

	if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

	optstr_get (options, "range",  "%u-%u/%d",    &mfd->start, &mfd->end, &mfd->step);
    }


    if (verbose > 1) {
	tc_log_info (MOD_NAME, " Invert Image Settings:");
	tc_log_info (MOD_NAME, "             range = %u-%u", mfd->start, mfd->end);
	tc_log_info (MOD_NAME, "              step = %u", mfd->step);
    }

    if (options)
	if (optstr_lookup (options, "help")) {
	    help_optstr();
	}

    if (mfd->start % mfd->step == 0)
      mfd->boolstep = 0;
    else
      mfd->boolstep = 1;

    // filter init ok.
    if (verbose) tc_log_info (MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);


    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

    if (mfd) {
	free(mfd);
    }
    mfd=NULL;

    return(0);

  } /* filter close */

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------


  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if((ptr->tag & TC_POST_M_PROCESS) && (ptr->tag & TC_VIDEO) && !(ptr->attributes & TC_FRAME_IS_SKIPPED))  {
    char *p = ptr->video_buf;

    if (mfd->start <= ptr->id && ptr->id <= mfd->end && ptr->id%mfd->step == mfd->boolstep) {

      for (w = 0; w < ptr->video_size; w++, p++)
	     *p = 255 - *p;
    }
  }

  return(0);
}

