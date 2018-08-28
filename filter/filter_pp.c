/*
 *  filter_pp.c
 *
 *  Copyright (C) Gerhard Monzel - Januar 2002
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

#define MOD_NAME    "filter_pp.so"
#define MOD_VERSION "v1.2.4 (2003-01-24)"
#define MOD_CAP     "Mplayers postprocess filters"
#define MOD_AUTHOR  "Michael Niedermayer et al, Gerhard Monzel"

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"

#include <ctype.h>
#include <stdint.h>

#include <libpostproc/postprocess.h>

/* FIXME: these use the filter ID as an index--the ID can grow
 * arbitrarily large, so this needs to be fixed */
static pp_mode_t *mode[100];
static pp_context_t *context[100];
static int width[100], height[100];
static int pre[100];

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static void optstr_help (void)
{
  tc_log_info(MOD_NAME, "(%s) help\n"
"<filterName>[:<option>[:<option>...]][[|/][-]<filterName>[:<option>...]]...\n"
"long form example:\n"
"vdeblock:autoq/hdeblock:autoq/linblenddeint    default,-vdeblock\n"
"short form example:\n"
"vb:a/hb:a/lb                                   de,-vb\n"
"more examples:\n"
"tn:64:128:256\n"
"Filters                        Options\n"
"short  long name       short   long option     Description\n"
"*      *               a       autoq           cpu power dependant enabler\n"
"                       c       chrom           chrominance filtring enabled\n"
"                       y       nochrom         chrominance filtring disabled\n"
"hb     hdeblock        (2 Threshold)           horizontal deblocking filter\n"
"       1. difference factor: default=64, higher -> more deblocking\n"
"       2. flatness threshold: default=40, lower -> more deblocking\n"
"                       the h & v deblocking filters share these\n"
"                       so u cant set different thresholds for h / v\n"
"vb     vdeblock        (2 Threshold)           vertical deblocking filter\n"
"h1     x1hdeblock                              Experimental h deblock filter 1\n"
"v1     x1vdeblock                              Experimental v deblock filter 1\n"
"dr     dering                                  Deringing filter\n"
"al     autolevels                              automatic brightness / contrast\n"
"                       f       fullyrange      stretch luminance to (0..255)\n"
"lb     linblenddeint                           linear blend deinterlacer\n"
"li     linipoldeint                            linear interpolating deinterlace\n"
"ci     cubicipoldeint                          cubic interpolating deinterlacer\n"
"md     mediandeint                             median deinterlacer\n"
"fd     ffmpegdeint                             ffmpeg deinterlacer\n"
"de     default                                 hb:a,vb:a,dr:a,al\n"
"fa     fast                                    h1:a,v1:a,dr:a,al\n"
"tn     tmpnoise        (3 Thresholds)          Temporal Noise Reducer\n"
"                       1. <= 2. <= 3.          larger -> stronger filtering\n"
"fq     forceQuant      <quantizer>             Force quantizer\n"
"pre    pre                                     run as a pre filter\n"
	      , MOD_CAP);
}


static void do_getconfig(char *opts)
{

    optstr_filter_desc (opts, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VYMOE", "1");

    optstr_param (opts, "hb", "Horizontal deblocking filter", "%d:%d", "64:40", "0", "255", "0", "255");
    optstr_param (opts, "vb", "Vertical deblocking filter", "%d:%d", "64:40", "0", "255", "0", "255");
    optstr_param (opts, "h1", "Experimental h deblock filter 1", "", "0");
    optstr_param (opts, "v1", "Experimental v deblock filter 1", "", "0");
    optstr_param (opts, "dr", "Deringing filter", "", "0");
    optstr_param (opts, "al", "Automatic brightness / contrast", "", "0");
    optstr_param (opts, "f", "Stretch luminance to (0..255)", "", "0");
    optstr_param (opts, "lb", "Linear blend deinterlacer", "", "0");
    optstr_param (opts, "li", "Linear interpolating deinterlace", "", "0");
    optstr_param (opts, "ci", "Cubic interpolating deinterlacer", "", "0");
    optstr_param (opts, "md", "Median deinterlacer", "", "0");
    optstr_param (opts, "de", "Default preset (hb:a/vb:a/dr:a/al)", "", "0");
    optstr_param (opts, "fa", "Fast preset (h1:a/v1:a/dr:a/al)", "", "0");
    optstr_param (opts, "tn", "Temporal Noise Reducer (1<=2<=3)",
	    "%d:%d:%d", "64:128:256", "0", "700", "0", "1500", "0", "3000");
    optstr_param (opts, "fq", "Force quantizer", "%d", "15", "0", "255");
    optstr_param (opts, "pre", "Run as a PRE filter", "", "0");
}

static int no_optstr (char *s)
{
  int result = 0; // decrement if transcode, increment if mplayer
  char *c = s;

  while (c && *c && (c = strchr (c, '=')))  { result--; c++; }
  c = s;
  while (c && *c && (c = strchr (c, '/')))  { result++; c++; }
  c = s;
  while (c && *c && (c = strchr (c, '|')))  { result++; c++; }
  c = s;
  while (c && *c && (c = strchr (c, ',')))  { result++; c++; }


  return (result<=0)?0:1;
}

static void do_optstr(char *opts)
{
    opts++;

    while (*opts) {

	if (*(opts-1) == ':') {
	    if (isalpha(*opts)) {
		if (
			(
			    strncmp(opts, "autoq", 5)   == 0
		        ) || (
			    strncmp(opts, "chrom", 5)   == 0
		        ) || (
			    strncmp(opts, "nochrom", 7) == 0
			) || (
			   (strncmp(opts, "a", 1)==0) && (strncmp(opts,"al",2)!=0)
			) || (
			   (strncmp(opts, "c", 1)==0) && (strncmp(opts,"ci",2)!=0)
			) || (
			    strncmp(opts, "y", 1)==0
			)
		   ) {
		    opts++;
		    continue;
		} else {
		    *(opts-1) = '/';
		}
	    }


	}

	if (*opts == '=')
	    *opts = ':';

	opts++;
    }
}

static char * pp_lookup(char *haystack, char *needle)
{
	char *ch = haystack;
	int found = 0;
	int len = strlen (needle);

	while (!found) {
		ch = strstr(ch, needle);

		if (!ch) break;

		if (ch[len] == '\0' || ch[len] == '=' || ch[len] == '/') {
			found = 1;
		} else {
			ch++;
		}
	}

	return (ch);


}

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  static vob_t *vob=NULL;
  int instance = ptr->filter_id;


  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if( (ptr->tag & TC_AUDIO))
	  return 0;

  if(ptr->tag & TC_FRAME_IS_SKIPPED)
	  return 0;

  if(ptr->tag & TC_FILTER_INIT)
  {
    char *c;
    int len=0;

    if((vob = tc_get_vob())==NULL) return(-1);

    if (vob->im_v_codec == CODEC_RGB)
    {
      tc_log_error(MOD_NAME, "filter is not capable for RGB-Mode !");
      return(-1);
    }

    if (!options || !(len=strlen(options)))
    {
      tc_log_error(MOD_NAME, "this filter needs options !");
      return(-1);
    }


    if (!no_optstr(options)) {
	do_optstr(options);
    }

    // if "pre" is found, delete it
    if ( (c=pp_lookup(options, "pre")) ) {
	memmove (c, c+3, &options[len]-c);
	pre[instance] = 1;
    }

    if ( (c=pp_lookup(options, "help")) ) {
	memmove (c, c+4, &options[len]-c);
	optstr_help();
    }

    if (pre[instance]) {
      width[instance] = vob->im_v_width;
      height[instance]= vob->im_v_height;
    } else {
      width[instance] = vob->ex_v_width;
      height[instance]= vob->ex_v_height;
    }

    //tc_log_msg(MOD_NAME, "after pre (%s)", options);

    mode[instance] = pp_get_mode_by_name_and_quality(options, PP_QUALITY_MAX);

    if(mode[instance]==NULL) {
      tc_log_error(MOD_NAME, "internal error (pp_get_mode_by_name_and_quality)");
      return(-1);
    }

    if(tc_accel & AC_MMXEXT)
      context[instance] = pp_get_context(width[instance], height[instance], PP_CPU_CAPS_MMX2);
    else if(tc_accel & AC_3DNOW)
      context[instance] = pp_get_context(width[instance], height[instance], PP_CPU_CAPS_3DNOW);
    else if(tc_accel & AC_MMX)
      context[instance] = pp_get_context(width[instance], height[instance], PP_CPU_CAPS_MMX);
    else
      context[instance] = pp_get_context(width[instance], height[instance], 0);

    if(context[instance]==NULL) {
      tc_log_error(MOD_NAME, "internal error (pp_get_context) (instance=%d)", instance);
      return(-1);
    }

    // filter init ok.
    if(verbose) tc_log_info(MOD_NAME, "%s %s #%d", MOD_VERSION, MOD_CAP, ptr->filter_id);
    return(0);
  }

  //----------------------------------
  //
  // filter configure
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_GET_CONFIG)
  {
      do_getconfig (options);
      return 0;
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE)
  {
    if (mode[instance])
      pp_free_mode(mode[instance]);
    mode[instance] = NULL;
    if (context[instance])
      pp_free_context(context[instance]);
    context[instance] = NULL;

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

  if(((ptr->tag & TC_PRE_M_PROCESS  && pre[instance]) ||
	  (ptr->tag & TC_POST_M_PROCESS && !pre[instance])) &&
	  !(ptr->attributes & TC_FRAME_IS_SKIPPED))
  {
    unsigned char *pp_page[3];
    int ppStride[3];

      pp_page[0] = ptr->video_buf;
      pp_page[1] = pp_page[0] + (width[instance] * height[instance]);
      pp_page[2] = pp_page[1] + (width[instance] * height[instance])/4;

      ppStride[0] = width[instance];
      ppStride[1] = ppStride[2] = width[instance]>>1;

      pp_postprocess((void *)pp_page, ppStride,
		     pp_page, ppStride,
		     width[instance], height[instance],
		     NULL, 0, mode[instance], context[instance], 0);
  }

  return(0);
}

