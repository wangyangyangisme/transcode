/*
 *  export_raw.c
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

#define MOD_NAME    "export_raw.so"
#define MOD_VERSION "v0.3.12 (2003-08-04)"
#define MOD_CODEC   "(video) * | (audio) MPEG/AC3/PCM"

#include "transcode.h"
#include "aud_aux.h"
#include "avilib/avilib.h"
#include "import/magic.h"
#include "libtc/libtc.h"
#include "libtc/xio.h"
#include "libtcvideo/tcvideo.h"

#include <stdio.h>
#include <stdlib.h>

static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_DV|TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV|TC_CAP_AC3|TC_CAP_AUD|TC_CAP_VID|TC_CAP_YUV422;

#define MOD_PRE raw
#include "export_def.h"

static avi_t *avifile1=NULL;
static avi_t *avifile2=NULL;

static int info_shown=0, force_kf=0;
static int width=0, height=0, im_v_codec=-1;
static int mpeg_passthru;
static FILE *mpeg_f = NULL;

static TCVHandle tcvhandle = 0;
static ImageFormat srcfmt = IMG_NONE, destfmt = IMG_NONE;
static int destsize = 0;

static const struct {
    const char *name;  // fourcc
    ImageFormat format;
    int bpp;
} formats[] = {
    { "I420", IMG_YUV420P, 12 },
    { "YV12", IMG_YV12,    12 },
    { "YUY2", IMG_YUY2,    16 },
    { "UYVY", IMG_UYVY,    16 },
    { "YVYU", IMG_YVYU,    16 },
    { "Y800", IMG_Y8,       8 },
    { "RGB",  IMG_RGB24,   24 },
    { NULL,   IMG_NONE,     0 }
};

/*************************************************************************/

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{

    if(param->flag == TC_VIDEO) {
      if(verbose & TC_DEBUG) tc_log_info(MOD_NAME, "max AVI-file size limit = %lu bytes",(unsigned long) AVI_max_size());
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


    double fps;

    const char *codec;
    const char *fcc = NULL;
    int force_avi = (vob->ex_v_string && strcmp(vob->ex_v_string,"avi") == 0);

    im_v_codec = vob->im_v_codec;

    // open out file
    if(param->flag==TC_AUDIO && vob->audio_file_flag) goto further;
    if(param->flag==TC_VIDEO
       && vob->v_codec_flag == TC_CODEC_MPEG2
       && (vob->pass_flag & TC_VIDEO)
       && !force_avi)
      goto further;
    if(vob->avifile_out==NULL) {
      if(NULL == (vob->avifile_out = AVI_open_output_file(
	      (param->flag==TC_VIDEO)?  vob->video_out_file: vob->audio_out_file))) {
	AVI_print_error("avi open error");
	exit(TC_EXPORT_ERROR);
      }
    }

further:

    /* save locally */
    avifile2 = vob->avifile_out;

    if(param->flag == TC_VIDEO) {

      // video

      if (vob->ex_v_fcc) {
	int want_help = (strcasecmp(vob->ex_v_fcc, "help") == 0);
	int i;
	if (want_help)
	    tc_log_info(MOD_NAME, "Available formats:");
	for (i = 0; formats[i].name != NULL; i++) {
	    if (want_help)
		tc_log_info(MOD_NAME, "%s", formats[i].name);
	    else if (strcasecmp(formats[i].name, vob->ex_v_fcc) == 0)
		break;
	}
	if (formats[i].name == NULL) {
	    if (!want_help) {
		tc_log_warn(MOD_NAME, "Unknown output format, \"-F help\" to list");
	    }
	    return TC_EXPORT_ERROR;
	}
	fcc = formats[i].name;
	destfmt = formats[i].format;
	destsize = vob->ex_v_width * vob->ex_v_height * formats[i].bpp / 8;
      }

      if (!(tcvhandle = tcv_init())) {
	tc_log_warn(MOD_NAME, "tcv_convert_init failed");
	return(TC_EXPORT_ERROR);
      }

      switch(vob->im_v_codec) {

      case CODEC_RGB:

	//force keyframe
	force_kf=1;

	width = vob->ex_v_width;
	height = vob->ex_v_height;
	if (!fcc)
	    fcc = "RGB";

	AVI_set_video(vob->avifile_out, vob->ex_v_width, vob->ex_v_height,
		      vob->ex_fps, fcc);

	if (vob->avi_comment_fd>0)
	    AVI_set_comment_fd(vob->avifile_out, vob->avi_comment_fd);

	if(!info_shown && verbose_flag)
	  tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
		  fcc, vob->ex_fps, vob->ex_v_width, vob->ex_v_height);
	srcfmt = IMG_RGB_DEFAULT;
	break;

      case CODEC_YUV:

	//force keyframe
	force_kf=1;

	width = vob->ex_v_width;
	height = vob->ex_v_height;
	if (!fcc)
	    fcc = "I420";

	AVI_set_video(vob->avifile_out, vob->ex_v_width, vob->ex_v_height,
		      vob->ex_fps, fcc);

	if(!info_shown && verbose_flag)
	  tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
		  fcc, vob->ex_fps, vob->ex_v_width, vob->ex_v_height);
	srcfmt = IMG_YUV_DEFAULT;
	break;

      case CODEC_YUV422:

	//force keyframe
	force_kf=1;

	width = vob->ex_v_width;
	height = vob->ex_v_height;
	if (!fcc)
	    fcc = "UYVY";

	AVI_set_video(vob->avifile_out, vob->ex_v_width, vob->ex_v_height,
		      vob->ex_fps, fcc);

	if(!info_shown && verbose_flag)
	  tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
		  fcc, vob->ex_fps, vob->ex_v_width, vob->ex_v_height);

	srcfmt = IMG_YUV422P;
	if (!vob->ex_v_fcc)
	    destfmt = IMG_UYVY;
	break;


      case CODEC_RAW:
      case CODEC_RAW_YUV:

	if (vob->v_codec_flag == TC_CODEC_MPEG2) {

	    if (vob->pass_flag & TC_VIDEO) {
		mpeg_passthru = 1;
		tc_log_info(MOD_NAME, "icodec (0x%08x) and codec_flag (0x%08lx) - passthru",
		    vob->im_v_codec, vob->v_codec_flag);

		if (force_avi) {
		    fcc = "mpg2";
		    AVI_set_video(vob->avifile_out, vob->ex_v_width,
				  vob->ex_v_height, vob->ex_fps, fcc);
		    if(!info_shown && verbose_flag) {
			tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d,"
				    " height=%d", fcc, vob->ex_fps,
				    vob->ex_v_width, vob->ex_v_height);
		    }
		} else {
		    mpeg_f = fopen(vob->video_out_file, "w");
		    if (!mpeg_f) {
			tc_log_warn(MOD_NAME, "Cannot open outfile \"%s\": %s",
				    vob->video_out_file, strerror(errno));
			return (TC_EXPORT_ERROR);
		    }
		}
	    }
	}
	else
	switch(vob->v_format_flag) {

	case TC_MAGIC_DV_PAL:
	case TC_MAGIC_DV_NTSC:

	  //force keyframe
	  force_kf=1;

	  width = vob->ex_v_width;
	  height = vob->ex_v_height;

	  AVI_set_video(vob->avifile_out, vob->ex_v_width, vob->ex_v_height, vob->ex_fps, "DVSD");

	  if(!info_shown && verbose_flag)
	    tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
		    "DVSD", vob->ex_fps, vob->ex_v_width, vob->ex_v_height);
	  break;

	default:
	  // pass-through mode is the default, works only with import_avi.so
	  if(vob->pass_flag & TC_VIDEO) {
        if (tc_file_check(vob->video_in_file) != 0) {
            tc_log_warn(MOD_NAME, "bad source file: \"%s\"",
                        vob->video_in_file);
        }

	    if(avifile1==NULL) {
	      avifile1 = AVI_open_input_file(vob->video_in_file, 1);
	      if(NULL == avifile1) {
    		AVI_print_error("avi open error in export_raw");
	    	return(TC_EXPORT_ERROR);
	      }
        }

	    //read all video parameter from input file
	    width  =  AVI_video_width(avifile1);
	    height =  AVI_video_height(avifile1);

	    fps    =  AVI_frame_rate(avifile1);
	    codec  =  AVI_video_compressor(avifile1);

	    //same for outputfile
	    AVI_set_video(vob->avifile_out, width, height, fps, codec);

	    if(!info_shown && (verbose_flag))
	      tc_log_info(MOD_NAME, "codec=%s, fps=%6.3f, width=%d, height=%d",
			            codec, fps, width, height);

	    //free resources
	    if(avifile1!=NULL) {
	      AVI_close(avifile1);
	      avifile1=NULL;
	    }
	  }
	}

	break;

      default:

	tc_log_info(MOD_NAME, "codec (0x%08x) and format (0x%08lx)not supported",
		vob->im_v_codec, vob->v_format_flag);
	return(TC_EXPORT_ERROR);

	break;
      }

      info_shown=1;
      return(0);
    }


    if(param->flag == TC_AUDIO) return(tc_audio_open(vob, vob->avifile_out));

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
  int i, mod=width%4;
  int size = param->size;

  if(param->flag == TC_VIDEO) {

    if (mpeg_f) {
      if (fwrite (param->buffer, 1, param->size, mpeg_f) != param->size) {
	tc_log_warn(MOD_NAME, "Cannot write data: %s", strerror(errno));
	return(TC_EXPORT_ERROR);
      }
      return (TC_EXPORT_OK);
    }


    //0.5.0-pre8:
    key = ((param->attributes & TC_FRAME_IS_KEYFRAME) || force_kf) ? 1:0;

    //0.6.2: switch outfile on "r/R" and -J pv
    //0.6.2: enforce auto-split at 2G (or user value) for normal AVI files
    if((uint32_t)(AVI_bytes_written(avifile2)+param->size+16+8)>>20 >= tc_avi_limit) tc_outstream_rotate_request();

    if(key) tc_outstream_rotate();

    if (srcfmt && destfmt) {
      vob_t *vob = tc_get_vob();
      if (!tcv_convert(tcvhandle, param->buffer, param->buffer,
		       vob->ex_v_width, vob->ex_v_height, srcfmt, destfmt)) {
	tc_log_warn(MOD_NAME, "image conversion failed");
	return(TC_EXPORT_ERROR);
      }
      if (destsize)
	size = destsize;
    }

    // Fixup: For uncompressed AVIs, it must be aligned at
    // a 4-byte boundary
    if (mod && (destfmt ? destfmt == IMG_RGB24 : im_v_codec == CODEC_RGB)) {
	for (i = height; i>0; i--) {
	    memmove (param->buffer+(i*width*3) + mod*i,
		     param->buffer+(i*width*3) ,
		     width*3);
	}
	param->size = height*width*3 + (4-mod)*height;
	//tc_log_msg(MOD_NAME, "going here mod = |%d| width (%d) size (%d)||", mod, width, param->size);
    }
    // write video
    if(AVI_write_frame(avifile2, param->buffer, size, key)<0) {
      AVI_print_error("avi video write error");

      return(TC_EXPORT_ERROR);
    }

    return(0);

  }

  if(param->flag == TC_AUDIO) return(tc_audio_encode(param->buffer, param->size, avifile2));

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

  if (mpeg_f) {
    fclose (mpeg_f);
    mpeg_f = NULL;
  }

  //inputfile
  if(avifile1!=NULL) {
    AVI_close(avifile1);
    avifile1=NULL;
  }

  if(param->flag == TC_AUDIO) return(tc_audio_close());

  //outputfile
  if(vob->avifile_out!=NULL) {
    AVI_close(vob->avifile_out);
    vob->avifile_out=NULL;
  }

  tcv_free(tcvhandle);
  tcvhandle = 0;

  if(param->flag == TC_VIDEO) return(0);

  return(TC_EXPORT_ERROR);

}

