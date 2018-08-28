/*
 *  export_im.c
 *
 *  Copyright (C) Thomas Oestreich - March 2002
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

/* Note: because of ImageMagick bogosity, this must be included first, so
 * we can undefine the PACKAGE_* symbols it splats into our namespace */
#include <magick/api.h>
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "transcode.h"
#include "libtcvideo/tcvideo.h"

#include <stdio.h>
#include <stdlib.h>

#define MOD_NAME    "export_im.so"
#define MOD_VERSION "v0.0.4 (2003-11-13)"
#define MOD_CODEC   "(video) *"

static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_YUV|TC_CAP_RGB|TC_CAP_PCM|TC_CAP_AUD;

#define MOD_PRE im
#include "export_def.h"

static char buf2[PATH_MAX];

static uint8_t *tmp_buffer; //[SIZE_RGB_FRAME];
static TCVHandle tcvhandle;

static int codec, width, height;

static int counter=0;
static const char *prefix="frame.";
static const char *type;

static int interval=1;
static unsigned int int_counter=0;

ImageInfo *image_info;

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{

    /* set the 'spit-out-frame' interval */
    interval = vob->frame_interval;

    if(param->flag == TC_VIDEO) {
      int quality = 75;

      width = vob->ex_v_width;
      height = vob->ex_v_height;

      codec = (vob->im_v_codec == CODEC_YUV) ? CODEC_YUV : CODEC_RGB;

      InitializeMagick("");

      image_info=CloneImageInfo((ImageInfo *) NULL);

      if (vob->divxbitrate == VBITRATE)
	quality = 75;
      else quality = vob->divxbitrate;

      if (quality >= 100) quality = 100;
      if (quality <= 0) quality = 0;

      image_info->quality = quality;

      if (!tmp_buffer)
	tmp_buffer = malloc (vob->ex_v_width*vob->ex_v_height*3);
      if (!tmp_buffer)
	return TC_EXPORT_ERROR;

      tcvhandle = tcv_init();
      if (!tcvhandle) {
	tc_log_error(MOD_NAME, "tcv_init() failed");
	return TC_EXPORT_ERROR;
      }

      return TC_EXPORT_OK;
    }

    if(param->flag == TC_AUDIO)
      return TC_EXPORT_OK;

    // invalid flag
    return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * open outputfile
 *
 * ------------------------------------------------------------*/

MOD_open
{

    if(param->flag == TC_VIDEO) {

      // video

	switch(vob->im_v_codec) {

	case CODEC_YUV:
	case CODEC_RGB:

	  if(vob->video_out_file!=NULL && strcmp(vob->video_out_file,"/dev/null")!=0) prefix=vob->video_out_file;

	  break;

	default:

	  tc_log_warn(MOD_NAME, "codec not supported");
	  return(TC_EXPORT_ERROR);

	  break;
	}

	if(vob->ex_v_fcc != NULL && strlen(vob->ex_v_fcc) != 0) {
	  type = vob->ex_v_fcc;
	} else type="jpg";

	return(0);
    }


    if(param->flag == TC_AUDIO) return(0);

    // invalid flag
    return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{

  ExceptionInfo exception_info;
  char *out_buffer = param->buffer;
  Image *image=NULL;
  int res;

  if ((++int_counter-1) % interval != 0)
      return (0);

  if(param->flag == TC_VIDEO) {

    GetExceptionInfo(&exception_info);

    res = tc_snprintf(buf2, PATH_MAX, "%s%06d.%s", prefix, counter++, type);
    if (res < 0) {
      tc_log_perror(MOD_NAME, "cmd buffer overflow");
      return(TC_EXPORT_ERROR);
    }

    if(codec==CODEC_YUV) {
      tcv_convert(tcvhandle, param->buffer, tmp_buffer, width, height,
		  IMG_YUV_DEFAULT, IMG_RGB24);
      out_buffer = tmp_buffer;
    }

    image=ConstituteImage (width, height, "RGB", CharPixel, out_buffer, &exception_info);

    strlcpy(image->filename, buf2, MaxTextExtent);

    WriteImage(image_info, image);
    DestroyImage(image);

    return(0);
  }

  if(param->flag == TC_AUDIO) return(0);

  // invalid flag
  return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * stop encoder
 *
 * ------------------------------------------------------------*/

MOD_stop
{

  if(param->flag == TC_VIDEO) {
    DestroyImageInfo(image_info);
    ConstituteComponentTerminus();
    DestroyMagick();

    free(tmp_buffer);
    tmp_buffer = NULL;
    tcv_free(tcvhandle);
    tcvhandle = 0;

    return(0);
  }

  if(param->flag == TC_AUDIO) return(0);

  return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close outputfiles
 *
 * ------------------------------------------------------------*/

MOD_close
{

    if(param->flag == TC_AUDIO) return(0);
    if(param->flag == TC_VIDEO) return(0);

    return(TC_EXPORT_ERROR);

}

