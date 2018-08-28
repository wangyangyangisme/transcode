/*
 *  export_ppm.c
 *
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define MOD_NAME    "export_ppm.so"
#define MOD_VERSION "v0.1.1 (2002-02-14)"
#define MOD_CODEC   "(video) PPM/PGM | (audio) MPEG/AC3/PCM"

#include "transcode.h"
#include "avilib/avilib.h"
#include "aud_aux.h"
#include "libtcvideo/tcvideo.h"

#include <stdio.h>
#include <stdlib.h>

static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_YUV|TC_CAP_RGB|TC_CAP_PCM|TC_CAP_AC3|TC_CAP_AUD|TC_CAP_YUV422;

#define MOD_PRE ppm
#include "export_def.h"

static char buf[256];
static char buf2[64];

static uint8_t *tmp_buffer; //[SIZE_RGB_FRAME];

static int codec, width, height, row_bytes;

static int counter=0;
static const char *prefix="frame";
static const char *type;
static int interval=1;
static unsigned int int_counter=0;

static TCVHandle tcvhandle;

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

      /* this supports output of 4:2:0 YUV material, ie CODEC_YUV */
      if(vob->im_v_codec == CODEC_YUV) {
	width = vob->ex_v_width;
	height = vob->ex_v_height;

	row_bytes = BPP/8 * vob->ex_v_width;

	codec =  CODEC_YUV;

	if (!tmp_buffer)
	  tmp_buffer = malloc (vob->ex_v_width*vob->ex_v_height*3);
	if (!tmp_buffer)
	  return -1;

	tcvhandle = tcv_init();
	if (!tcvhandle) {
	  tc_log_error(MOD_NAME, "tcv_init() failed");
	  return -1;
	}
      }

      /* this supports output of 4:2:2 YUV material, ie CODEC_YUV422 */
      if(vob->im_v_codec == CODEC_YUV422) {
	/* size of the exported image */
	width = vob->ex_v_width;
	height = vob->ex_v_height;

	/* bytes per scan line (aka row) */
	row_bytes = BPP/8 * vob->ex_v_width;

	codec =  CODEC_YUV422;

	/* this is for the output, one byte each for R, G and B per pixel */
	if (!tmp_buffer)
	  tmp_buffer = malloc (vob->ex_v_width*vob->ex_v_height*3);
	if (!tmp_buffer)
	  return -1;

	tcvhandle = tcv_init();
	if (!tcvhandle) {
	  tc_log_error(MOD_NAME, "tcv_init() failed");
	  return -1;
	}
      }

      /* source stream encoding format not supported */
      return(0);

    }

    /* audio is not supported in PPM image format... */
    if(param->flag == TC_AUDIO) return(tc_audio_init(vob, verbose_flag));

    // invalid flag
    return(TC_EXPORT_ERROR);
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
	case CODEC_YUV422:
	case CODEC_RGB:

	  if(vob->video_out_file!=NULL && strcmp(vob->video_out_file,"/dev/null")!=0) prefix=vob->video_out_file;

	  type = (vob->decolor) ? "P5":"P6";

	  tc_snprintf(buf, sizeof(buf), "%s\n#(%s-v%s) \n%d %d 255\n", type, PACKAGE, VERSION, vob->ex_v_width, vob->ex_v_height);

	  break;

	default:

	  tc_log_warn(MOD_NAME, "codec not supported");
	  return(TC_EXPORT_ERROR);

	  break;
	}

	return(TC_EXPORT_OK);
    }


    if(param->flag == TC_AUDIO) {
	  tc_log_warn(MOD_NAME, "Usage of this module for audio encoding is deprecated.");
      tc_log_warn(MOD_NAME, "Consider switch to export_tcaud module.");
	  return(tc_audio_open(vob, NULL));
    }

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

  FILE *fd;
  char *out_buffer = param->buffer;
  int n, out_size = param->size;

  if ((++int_counter-1) % interval != 0)
      return (0);

  if(param->flag == TC_VIDEO) {


    if(codec==CODEC_YUV) {
      tcv_convert(tcvhandle, param->buffer, tmp_buffer, width, height,
                  IMG_YUV_DEFAULT, IMG_RGB24);
      out_buffer = tmp_buffer;
      out_size = height * 3 *width;
    }

    if(codec==CODEC_YUV422) {
      tcv_convert(tcvhandle, param->buffer, tmp_buffer, width, height,
                  IMG_YUV422P, IMG_RGB24);
      out_buffer = tmp_buffer;
      out_size = height * 3 *width;
    }

    if(strncmp(type, "P5", 2)==0) {
	out_size /= 3;
	//tcv_convert(tcvhandle, tmp_buffer, tmp_buffer, width, height,
	//            IMG_RGB24, IMG_GRAY8);
	for (n=0; n<out_size; ++n) out_buffer[n] = out_buffer[3*n];
	tc_snprintf(buf2, sizeof(buf2), "%s%06d.pgm", prefix, counter++);
    } else
	tc_snprintf(buf2, sizeof(buf2), "%s%06d.ppm", prefix, counter++);

    if((fd = fopen(buf2, "w"))==NULL) {
      tc_log_perror(MOD_NAME, "fopen file");
      return(TC_EXPORT_ERROR);
    }

    if(fwrite(buf, strlen(buf), 1, fd) != 1) {
      tc_log_perror(MOD_NAME, "write header");
      return(TC_EXPORT_ERROR);
    }

    if(fwrite(out_buffer, out_size, 1, fd) != 1) {
	tc_log_perror(MOD_NAME, "write frame");
	return(TC_EXPORT_ERROR);
    }
    fclose(fd);
    return(0);
  }

  if(param->flag == TC_AUDIO) return(tc_audio_encode(param->buffer, param->size, NULL));

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

  if(param->flag == TC_VIDEO) return(0);
  if(param->flag == TC_AUDIO) return(tc_audio_stop());

  free(tmp_buffer);
  tmp_buffer = NULL;
  tcv_free(tcvhandle);
  tcvhandle = 0;

  return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close outputfiles
 *
 * ------------------------------------------------------------*/

MOD_close
{

    if(param->flag == TC_AUDIO) return(tc_audio_close());
    if(param->flag == TC_VIDEO) return(0);

    return(TC_EXPORT_ERROR);

}

