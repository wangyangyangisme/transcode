/*
 *  export_lzo.c
 *
 *  Copyright (C) Thomas Oestreich - October 2002
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

#define MOD_NAME    "export_lzo.so"
#define MOD_VERSION "v0.1.0 (2005-10-15)"
#define MOD_CODEC   "(video) LZO real-time compression | (audio) MPEG/AC3/PCM"

#include "transcode.h"
#include "libtc/libtc.h"
#include "avilib/avilib.h"
#include "aud_aux.h"
#include "import/magic.h"
#include "libtc/tc_lzo.h"

#include <stdio.h>
#include <stdlib.h>

static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_DV|TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV|TC_CAP_AC3|TC_CAP_AUD|TC_CAP_VID;

#define MOD_PRE lzo
#include "export_def.h"


static avi_t *avifile1=NULL;
static avi_t *avifile2=NULL;

static int info_shown=0, force_kf=0;

static int r;
static lzo_byte *out;
static lzo_byte *wrkmem;
static lzo_uint out_len;
static int codec;

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{

    if(param->flag == TC_VIDEO) {
      if(verbose & TC_DEBUG)
          tc_log_info(MOD_NAME, "max AVI-file size limit = %lu bytes",
		                         (unsigned long) AVI_max_size());

      /*
       * Step 1: initialize the LZO library
       */

      if (lzo_init() != LZO_E_OK) {
          tc_log_warn(MOD_NAME, "lzo_init() failed");
          return(TC_EXPORT_ERROR);
      }

      wrkmem = (lzo_bytep) lzo_malloc(LZO1X_1_MEM_COMPRESS);
      out = (lzo_bytep) lzo_malloc(vob->ex_v_height*vob->ex_v_width*3*2);

      if (wrkmem == NULL || out == NULL) {
          tc_log_error(MOD_NAME, "out of memory");
          return(TC_EXPORT_ERROR);
      }

      codec = vob->im_v_codec;

      return(0);
    }

    if(param->flag == TC_AUDIO)
       return(tc_audio_init(vob, verbose_flag));

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

    // open out file
    if(vob->avifile_out==NULL)
      if(NULL == (vob->avifile_out = AVI_open_output_file(vob->video_out_file))) {
	AVI_print_error("avi open error");
	exit(TC_EXPORT_ERROR);
      }

    /* save locally */
    avifile2 = vob->avifile_out;

    if(param->flag == TC_VIDEO) {

      //video

      //force keyframe
      force_kf=1;

      AVI_set_video(vob->avifile_out, vob->ex_v_width, vob->ex_v_height, vob->ex_fps, "LZO2");

      if (vob->avi_comment_fd>0)
	  AVI_set_comment_fd(vob->avifile_out, vob->avi_comment_fd);

      if(!info_shown && verbose_flag)
          tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
                      "LZO2", vob->ex_fps, vob->ex_v_width, vob->ex_v_height);

      info_shown=1;

      return(0);
    }

    if(param->flag == TC_AUDIO) {
	  tc_log_warn(MOD_NAME, "Usage of this module for audio encoding is deprecated.");
      tc_log_warn(MOD_NAME, "Consider switch to export_tcaud module.");
      return(tc_audio_open(vob, vob->avifile_out));
    }
    // invalid flag
    return(TC_EXPORT_ERROR);
}

inline static void long2str(long a, unsigned char *b)
{
      b[0] = (a&0xff000000)>>24;
      b[1] = (a&0x00ff0000)>>16;
      b[2] = (a&0x0000ff00)>>8;
      b[3] = (a&0x000000ff);
}

inline static void short2str(short a, unsigned char *b)
{
      b[0] = (a&0xff00)>>8;
      b[1] = (a&0x00ff);
}

/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{

  int key;

  tc_lzo_header_t h;

  if(param->flag == TC_VIDEO) {

    //write video

    //encode

    /*
     * compress from `in' to `out' with LZO1X-1
     */

    r = lzo1x_1_compress(param->buffer, param->size, out+sizeof(h), &out_len, wrkmem);
    h.magic = TC_CODEC_LZO2;
    h.size = out_len;
    h.method = 1;
    h.level = 1;
    h.flags = 0;
    h.flags |= ((codec==CODEC_RGB)?TC_LZO_FORMAT_RGB24:TC_LZO_FORMAT_YUV420P);
    h.pad = 0;

    ac_memcpy (out, &h, sizeof(h));

    if (r == LZO_E_OK) {
      if(verbose & TC_DEBUG)
	      tc_log_info(MOD_NAME, "compressed %lu bytes into %lu bytes",
		             (long) param->size, (long) out_len);
    } else {
      /* this should NEVER happen */
      tc_log_warn(MOD_NAME, "internal error - compression failed: %d", r);
      return(TC_EXPORT_ERROR);
    }

    /* check for an incompressible block */
    if (out_len >= param->size)  {
      if(verbose & TC_DEBUG)
	      tc_log_info(MOD_NAME, "block contains incompressible data");
      h.flags |= TC_LZO_NOT_COMPRESSIBLE;
      ac_memcpy(out+sizeof(h), param->buffer, param->size);
      out_len = param->size;
    }

    //0.5.0-pre8:
    key = ((param->attributes & TC_FRAME_IS_KEYFRAME) || force_kf) ? 1:0;

    out_len += sizeof(h);

    //0.6.2: switch outfile on "C" and -J pv
    //0.6.2: enforce auto-split at 2G (or user value) for normal AVI files
    if((uint32_t)(AVI_bytes_written(avifile2)+out_len+16+8)>>20 >= tc_avi_limit) tc_outstream_rotate_request();

    if(key) tc_outstream_rotate();

    if(AVI_write_frame(avifile2, out, out_len, key)<0) {
      AVI_print_error("avi video write error");

      return(TC_EXPORT_ERROR);
    }

    return(0);

  }

  if(param->flag == TC_AUDIO)
      return(tc_audio_encode(param->buffer, param->size, avifile2));

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

    lzo_free(wrkmem);
    lzo_free(out);

    return(0);
  }

  if(param->flag == TC_AUDIO)
      return(tc_audio_stop());

  return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close outputfiles
 *
 * ------------------------------------------------------------*/

MOD_close
{

  vob_t *vob = tc_get_vob();

  //inputfile
  if(avifile1!=NULL) {
    AVI_close(avifile1);
    avifile1=NULL;
  }

  if(param->flag == TC_AUDIO)
      return(tc_audio_close());

  //outputfile
  if(vob->avifile_out!=NULL) {
    AVI_close(vob->avifile_out);
    vob->avifile_out=NULL;
  }

  if(param->flag == TC_VIDEO) return(0);

  return(TC_EXPORT_ERROR);

}

