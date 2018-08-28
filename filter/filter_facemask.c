/*
 *  filter_facemask.c
 *
 *  Copyright (C) Julien Tierny <julien.tierny@wanadoo.fr> - October 2004
 *  Copyright (C) Thomas Oestreich - June 2001
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA].
 *
 */

#define MOD_NAME    "filter_facemask.so"
#define MOD_VERSION "v0.2 (2004-11-01)"
#define MOD_CAP     "Mask people faces in video interviews."
#define MOD_AUTHOR  "Julien Tierny"

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"

/* For RGB->YUV conversion */
#include "libtcvideo/tcvideo.h"


/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/


typedef struct parameter_struct {
	int 	xpos;
	int	ypos;
	int	xresolution;
	int	yresolution;
	int	xdim;
	int	ydim;
	TCVHandle tcvhandle;
} parameter_struct;

static parameter_struct *parameters = NULL;

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help"
"\n* Overview:\n"
"  This filter can mask people faces in video interviews.\n"
"  Both YUV and RGB formats are supported, in multithreaded mode.\n"
"\n"
"* Warning:\n"
"  You have to calibrate by your own the mask dimensions and positions so as it fits to your video sample.\n"
"  You also have to choose a resolution that is multiple of the mask dimensions.\n"
"\n"
"* Options:\n"
"  'xpos':        Position of the upper left corner of the mask (x)\n"
"  'ypos':        Position of the upper left corner of the mask (y)\n"
"  'xresolution': Resolution of the mask (width)\n"
"  'yresolution': Resolution of the mask (height)\n"
"  'xdim':        Width of the mask (= n*xresolution)\n"
"  'ydim':        Height of the mask (= m*yresolution)\n"
		, MOD_CAP);
}

static int check_parameters(int x, int y, int w, int h, int W, int H, vob_t *vob){

	/* First, we check if the face-zone is contained in the picture */
	if ((x+W) > vob->im_v_width){
		tc_log_error(MOD_NAME, "Face zone is larger than the picture !");
		return -1;
	}
	if ((y+H) > vob->im_v_height){
		tc_log_error(MOD_NAME, "Face zone is taller than the picture !");
		return -1;
	}

	/* Then, we check the resolution */
	if ((H%h) != 0) {
		tc_log_error(MOD_NAME, "Uncorrect Y resolution !");
		return -1;
	}
	if ((W%w) != 0) {
		tc_log_error(MOD_NAME, "Uncorrect X resolution !");
		return -1;
	}
	return 0;
}

static int average_neighbourhood(int x, int y, int w, int h, unsigned char *buffer, int width){
	unsigned int 	red=0, green=0, blue=0;
	int 			i=0,j=0;

	for (j=y; j<=y+h; j++){
		for (i=3*(x + width*(j-1)); i<3*(x + w + (j-1)*width); i+=3){
			red 	+= (int) buffer[i];
			green 	+= (int) buffer[i+1];
			blue 	+= (int) buffer[i+2];
		}
	}

	red 	/= ((w+1)*h);
	green 	/= ((w+1)*h);
	blue 	/= ((w+1)*h);

	/* Now let's print values in buffer */
	for (j=y; j<y+h; j++)
		for (i=3*(x + width*(j-1)); i<3*(x + w + (j-1)*width); i+=3){
			buffer[i] 		= (char)red;
			buffer[i+1] 	= (char)green;
 			buffer[i+2]		= (char)blue;
		}
	return 0;
}

static int print_mask(int x, int y, int w, int h, int W, int H, vframe_list_t *ptr){
	int				i=0,j=0;
	for (j=y; j<=y+H; j+=h)
		for (i=x; i<=x+W; i+=w)
			average_neighbourhood(i, j, w, h, ptr->video_buf, ptr->v_width);
	return 0;
}

int tc_filter(frame_list_t *ptr_, char *options){
	vframe_list_t *ptr = (vframe_list_t *)ptr_;
	static 			vob_t *vob=NULL;


  if(ptr->tag & TC_FILTER_GET_CONFIG) {

	optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Julien Tierny", "VRYMEO", "1");
	optstr_param(options, "xpos", "Position of the upper left corner of the mask (x)", "%d", "0", "0", "oo");
	optstr_param(options, "ypos", "Position of the upper left corner of the mask (y)", "%d", "0", "0", "oo");
	optstr_param(options, "xresolution", "Resolution of the mask (width)", "%d", "0", "1", "oo");
	optstr_param(options, "yresolution", "Resolution of the mask (height)", "%d", "0", "1", "oo");
	optstr_param(options, "xdim", "Width of the mask (= n*xresolution)", "%d", "0", "1", "oo");
	optstr_param(options, "ydim", "Height of the mask (= m*yresolution)", "%d", "0", "1", "oo");
	return 0;
  }

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL)
		return(-1);


	/* Now, let's handle the options ... */
	parameters = tc_malloc (sizeof(parameter_struct));
	if(parameters == NULL)
		return -1;

	/* Filter default options */
	if (verbose & TC_DEBUG)
		tc_log_info(MOD_NAME, "Preparing default options.");
	parameters->xpos 		= 0;
	parameters->ypos 		= 0;
	parameters->xresolution	= 1;
	parameters->yresolution	= 1;
	parameters->xdim		= 1;
	parameters->ydim		= 1;
	parameters->tcvhandle	= 0;

	if (options){
		/* Get filter options via transcode core */
		if (verbose & TC_DEBUG)
			tc_log_info(MOD_NAME, "Merging options from transcode.");
		optstr_get(options, "xpos",  		 	"%d",		&parameters->xpos);
		optstr_get(options, "ypos",   			"%d",		&parameters->ypos);
		optstr_get(options, "xresolution",   	"%d",		&parameters->xresolution);
		optstr_get(options, "yresolution",   	"%d",		&parameters->yresolution);
		optstr_get(options, "xdim",			   	"%d",		&parameters->xdim);
		optstr_get(options, "ydim",			   	"%d",		&parameters->ydim);
		if (optstr_lookup(options, "help") !=NULL) help_optstr();
	}

	if (vob->im_v_codec == CODEC_YUV){
		if (!(parameters->tcvhandle = tcv_init())) {
			tc_log_error(MOD_NAME, "Error at image conversion initialization.");
			return(-1);
		}
	}

	if (check_parameters(parameters->xpos, parameters->ypos, parameters->xresolution, parameters->yresolution, parameters->xdim, parameters->ydim, vob) < 0)
		return -1;

	if(verbose)
		tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

	tcv_free(parameters->tcvhandle);

	/* Let's free the parameter structure */
	free(parameters);
	parameters = NULL;

    return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------

	if(ptr->tag & TC_POST_M_PROCESS && ptr->tag & TC_VIDEO && !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {


		switch(vob->im_v_codec){
			case CODEC_RGB:
				return print_mask(parameters->xpos, parameters->ypos, parameters->xresolution, parameters->yresolution, parameters->xdim, parameters->ydim, ptr);
				break;

			case CODEC_YUV:

				if (!tcv_convert(parameters->tcvhandle, ptr->video_buf, ptr->video_buf, ptr->v_width, ptr->v_height, IMG_YUV_DEFAULT, IMG_RGB24)){
					tc_log_error(MOD_NAME, "cannot convert YUV stream to RGB format !");
					return -1;
				}

				if ((print_mask(parameters->xpos, parameters->ypos, parameters->xresolution, parameters->yresolution, parameters->xdim, parameters->ydim, ptr))<0) return -1;
				if (!tcv_convert(parameters->tcvhandle, ptr->video_buf, ptr->video_buf, ptr->v_width, ptr->v_height, IMG_RGB24, IMG_YUV_DEFAULT)){
					tc_log_error(MOD_NAME, "cannot convert RGB stream to YUV format !");
					return -1;
				}
				break;

			default:
				tc_log_error(MOD_NAME, "Internal video codec is not supported.");
				return -1;
		}
	}
	return(0);
}
