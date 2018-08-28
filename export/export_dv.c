/*
 *  export_dv.c
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

#define MOD_NAME    "export_dv.so"
#define MOD_VERSION "v0.5 (2003-07-24)"
#define MOD_CODEC   "(video) Digital Video | (audio) MPEG/AC3/PCM"

#include "transcode.h"
#include "avilib/avilib.h"
#include "aud_aux.h"
#include "libtcvideo/tcvideo.h"

#include <stdio.h>
#include <stdlib.h>
#include <libdv/dv.h>

static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV|TC_CAP_AC3;

#define MOD_PRE dv
#include "export_def.h"

static unsigned char *target; //[TC_FRAME_DV_PAL];

static avi_t *avifile=NULL;

static int frame_size=0, format=0;

static int dv_yuy2_mode=0;

static dv_encoder_t *encoder = NULL;
static unsigned char *pixels[3], *tmp_buf;
static TCVHandle tcvhandle;


/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{

    if(param->flag == TC_VIDEO) {
      target = tc_bufalloc(TC_FRAME_DV_PAL);
      tcvhandle = tcv_init();

      if(vob->dv_yuy2_mode == 1) {
	    tmp_buf = tc_bufalloc(PAL_W*PAL_H*2); //max frame
        dv_yuy2_mode=1;
      }

      encoder = dv_encoder_new(FALSE, FALSE, FALSE);

      return(TC_EXPORT_OK);
    }

    if(param->flag == TC_AUDIO) {
	  tc_log_warn(MOD_NAME, "Usage of this module for audio encoding is deprecated.");
      tc_log_warn(MOD_NAME, "Consider switch to export_tcaud module.");
      return(tc_audio_init(vob, verbose_flag));
    }

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
  avifile = vob->avifile_out;

  if(param->flag == TC_VIDEO) {

    AVI_set_video(vob->avifile_out, vob->ex_v_width, vob->ex_v_height, vob->ex_fps, "DVSD");

    if (vob->avi_comment_fd>0)
	AVI_set_comment_fd(vob->avifile_out, vob->avi_comment_fd);

    switch(vob->im_v_codec) {

    case CODEC_RGB:
      format=0;
      break;

    case CODEC_YUV:
      format=1;
      break;

    default:

      tc_log_warn(MOD_NAME, "codec not supported");
      return(TC_EXPORT_ERROR);

      break;
    }

    // for reading
    frame_size = (vob->ex_v_height==PAL_H) ? TC_FRAME_DV_PAL:TC_FRAME_DV_NTSC;

    encoder->isPAL = (vob->ex_v_height==PAL_H);
    encoder->is16x9 = FALSE;
    encoder->vlc_encode_passes = 3;
    encoder->static_qno = 0;
    encoder->force_dct = DV_DCT_AUTO;

    return(0);
  }


  if(param->flag == TC_AUDIO)  return(tc_audio_open(vob, vob->avifile_out));

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

  int key;

  if(param->flag == TC_VIDEO) {

    time_t now = time(NULL);

    if(dv_yuy2_mode) {
      tcv_convert(tcvhandle, param->buffer, tmp_buf, PAL_W,
		  (encoder->isPAL) ? PAL_H : NTSC_H, IMG_YUV420P, IMG_YUY2);
      pixels[0] = pixels[1] = pixels[2] = tmp_buf;
    } else {
      pixels[0] = param->buffer;
      if(encoder->isPAL) {
	pixels[1] = pixels[0] + PAL_W*PAL_H;
	pixels[2] = pixels[1] + (PAL_W/2)*(PAL_H/2);
      } else {
	pixels[1] = pixels[0] + NTSC_W*NTSC_H;
	pixels[2] = pixels[1] + (NTSC_W/2)*(NTSC_H/2);
      }
    }

    dv_encode_full_frame(encoder, pixels, (format)?e_dv_color_yuv:e_dv_color_rgb, target);

    dv_encode_metadata(target, encoder->isPAL, encoder->is16x9, &now, 0);
    dv_encode_timecode(target, encoder->isPAL, 0);


    // write video
    // only keyframes
    key = 1;

    //0.6.2: switch outfile on "r/R" and -J pv
    //0.6.2: enforce auto-split at 2G (or user value) for normal AVI files
    if((uint32_t)(AVI_bytes_written(avifile)+frame_size+16+8)>>20 >= tc_avi_limit) tc_outstream_rotate_request();

    if(key) tc_outstream_rotate();


    if(AVI_write_frame(avifile, target, frame_size, key)<0) {
      AVI_print_error("avi video write error");

      return(TC_EXPORT_ERROR);
    }
    return(0);
  }

  if(param->flag == TC_AUDIO) return(tc_audio_encode(param->buffer, param->size, avifile));

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

    dv_encoder_free(encoder);
    tcv_free(tcvhandle);

    return(0);
  }

  if(param->flag == TC_AUDIO) return(tc_audio_stop());

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
  if(param->flag == TC_AUDIO) return(tc_audio_close());

  //outputfile
  if(vob->avifile_out!=NULL) {
    AVI_close(vob->avifile_out);
    vob->avifile_out=NULL;
  }

  if(param->flag == TC_VIDEO) return(0);

  return(TC_EXPORT_ERROR);

}
