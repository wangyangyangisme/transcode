/*
 *  export_jpg.c
 *
 *  Copyright (C) Tilmann Bitterberg - September 2002
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

#define MOD_NAME    "export_jpg.so"
#define MOD_VERSION "v0.2.1 (2003-08-06)"
#define MOD_CODEC   "(video) *"

#include "transcode.h"

#include <stdio.h>
#include <stdlib.h>

#include "jpeglib.h"

static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_YUV|TC_CAP_RGB|TC_CAP_PCM|TC_CAP_AUD;

#define MOD_PRE jpg
#include "export_def.h"

static char buf2[PATH_MAX];

static int codec, width, height;

static int counter=0;
static const char *prefix="frame.";
static int jpeg_quality =0;
#define JPEG_DEFAULT_QUALITY 85

static int interval=1;
static unsigned int int_counter=0;

JSAMPLE * image_buffer;	/* Points to large array of R,G,B-order data */
static unsigned char **line[3];

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

// native YUV jpeg encoder code based on encode_JPEG of the quicktime4linux lib
//
static void write_yuv_JPEG_file(char *filename, int quality,
		       unsigned char **input,
		       int _width, int _height)
{
  int i, j, k;
  int width 	= _width;
  int height 	= _height;
  unsigned char *base[3];
  struct jpeg_compress_struct encinfo;
  struct jpeg_error_mgr jerr;
  FILE * outfile;		/* target file */

  jpeg_create_compress(&encinfo);

  encinfo.err = jpeg_std_error(&jerr);

  if ((outfile = fopen(filename, "wb")) == NULL) {
    tc_log_error(MOD_NAME, "can't open %s", filename);
  }
  jpeg_stdio_dest(&encinfo, outfile);

  encinfo.image_width = width;
  encinfo.image_height = height;
  encinfo.input_components = 3;

  jpeg_set_defaults(&encinfo);
  encinfo.dct_method = JDCT_FASTEST;

  jpeg_set_quality(&encinfo, quality, TRUE);
  encinfo.raw_data_in = TRUE;
#if JPEG_LIB_VERSION >= 70
  encinfo.do_fancy_downsampling = FALSE;
#endif
  encinfo.in_color_space = JCS_YCbCr;

  encinfo.comp_info[0].h_samp_factor = 2;
  encinfo.comp_info[0].v_samp_factor = 2;
  encinfo.comp_info[1].h_samp_factor = 1;
  encinfo.comp_info[1].v_samp_factor = 1;
  encinfo.comp_info[2].h_samp_factor = 1;
  encinfo.comp_info[2].v_samp_factor = 1;

  jpeg_start_compress(&encinfo, TRUE);

  base[0] = input[0];
  base[1] = input[1];
  base[2] = input[2];

  for (i = 0; i < height; i += 2*DCTSIZE) {
    for (j=0, k=0; j<2*DCTSIZE;j+=2, k++) {

      line[0][j]   = base[0]; base[0] += width;
      line[0][j+1] = base[0]; base[0] += width;
      line[1][k]   = base[1]; base[1] += width/2;
      line[2][k]   = base[2]; base[2] += width/2;
    }
    jpeg_write_raw_data(&encinfo, line, 2*DCTSIZE);
  }
  jpeg_finish_compress(&encinfo);

  fclose(outfile);

  jpeg_destroy_compress(&encinfo);

}

static void write_rgb_JPEG_file (char * filename, int quality, int width, int height)
{
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  /* More stuff */
  FILE * outfile;		/* target file */
  JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
  int row_stride;		/* physical row width in image buffer */

  /* Step 1: allocate and initialize JPEG compression object */

  cinfo.err = jpeg_std_error(&jerr);
  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress(&cinfo);

  /* Step 2: specify data destination (eg, a file) */
  /* Note: steps 2 and 3 can be done in either order. */

  if ((outfile = fopen(filename, "wb")) == NULL) {
    tc_log_error(MOD_NAME, "can't open %s", filename);
  }
  jpeg_stdio_dest(&cinfo, outfile);

  /* Step 3: set parameters for compression */

  /* First we supply a description of the input image.
   * Four fields of the cinfo struct must be filled in:
   */
  cinfo.image_width = width; 	/* image width and height, in pixels */
  cinfo.image_height = height;
  cinfo.input_components = 3;		/* # of color components per pixel */
  cinfo.in_color_space = JCS_RGB; 	/* colorspace of input image */

  jpeg_set_defaults(&cinfo);
  /* Now you can set any non-default parameters you wish to.
   * Here we just illustrate the use of quality (quantization table) scaling:
   */
  jpeg_set_quality(&cinfo, quality, TRUE); /* limit to baseline-JPEG values */

  /* Step 4: Start compressor */

  /* TRUE ensures that we will write a complete interchange-JPEG file.
   * Pass TRUE unless you are very sure of what you're doing.
   */
  jpeg_start_compress(&cinfo, TRUE);

  /* Step 5: while (scan lines remain to be written) */
  /*           jpeg_write_scanlines(...); */

  row_stride = cinfo.image_width * 3;	/* JSAMPLEs per row in image_buffer */

  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = & image_buffer[cinfo.next_scanline * row_stride];
    (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  /* Step 6: Finish compression */

  jpeg_finish_compress(&cinfo);
  /* After finish_compress, we can close the output file. */
  fclose(outfile);

  /* Step 7: release JPEG compression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_compress(&cinfo);

}

MOD_init
{

    /* set the 'spit-out-frame' interval */
    interval = vob->frame_interval;

    if(param->flag == TC_VIDEO) {

      width = vob->ex_v_width;
      height = vob->ex_v_height;

      codec = (vob->im_v_codec == CODEC_YUV) ? CODEC_YUV : CODEC_RGB;

      if(vob->im_v_codec == CODEC_YUV) {
	line[0] = malloc(height*sizeof(char*));
	line[1] = malloc(height*sizeof(char*)/2);
	line[2] = malloc(height*sizeof(char*)/2);
      }

      return(0);
    }

    if(param->flag == TC_AUDIO) return(0);

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
	case CODEC_RGB:

	  if(vob->video_out_file!=NULL && strcmp(vob->video_out_file,"/dev/null")!=0) prefix=vob->video_out_file;

	  break;

	default:

	  tc_log_warn(MOD_NAME, "codec not supported (0x%x)", vob->im_v_codec);
	  return(TC_EXPORT_ERROR);

	  break;
	}

	if(vob->ex_v_fcc != NULL && strlen(vob->ex_v_fcc) != 0) {
	  jpeg_quality=atoi(vob->ex_v_fcc);
	  if (jpeg_quality<=0) jpeg_quality = JPEG_DEFAULT_QUALITY;
	  if (jpeg_quality>100) jpeg_quality = 100;
	} else {
	  jpeg_quality=75;
	}

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

  char *out_buffer = param->buffer;

  if ((++int_counter-1) % interval != 0)
      return (0);

  if(param->flag == TC_VIDEO) {


    if(tc_snprintf(buf2, PATH_MAX, "%s%06d.%s", prefix, counter++, "jpg") < 0) {
      tc_log_perror(MOD_NAME, "cmd buffer overflow");
      return(TC_EXPORT_ERROR);
    }

    if(codec==CODEC_YUV) {
      unsigned char *base[3];
      YUV_INIT_PLANES(base, param->buffer, IMG_YUV420P, width, height);
      write_yuv_JPEG_file(buf2, jpeg_quality, base, width, height);

      //out_buffer = tmp_buffer;
    } else {
      image_buffer = out_buffer;
      write_rgb_JPEG_file(buf2, jpeg_quality, width, height);
    }

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
