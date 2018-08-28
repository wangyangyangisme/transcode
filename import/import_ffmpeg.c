/*
 *  import_ffmpeg.c
 *
 *  Copyright (C) Moritz Bunkus - October 2002
 *  libavformat support and misc updates:
 *  Copyright (C) Francesco Romani - September 2006
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

#define MOD_NAME    "import_ffmpeg.so"
#define MOD_VERSION "v0.1.15 (2008-01-28)"
#define MOD_CODEC   "(video) ffmpeg: MS MPEG4v1-3/MPEG4/MJPEG"

#include "transcode.h"
#include "libtc/libtc.h"
#include "filter.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_YUV | TC_CAP_RGB | TC_CAP_VID;

#define MOD_PRE ffmpeg
#include "import_def.h"

#include "libtc/tcavcodec.h"
#include "aclib/imgconvert.h"
#include "avilib/avilib.h"
#include "magic.h"

char import_cmd_buf[TC_BUF_MAX];

// libavcodec is not thread-safe. We must protect concurrent access to it.
// this is visible (without the mutex of course) with
// transcode .. -x ffmpeg -y ffmpeg -F mpeg4

static int done_seek=0;

struct ffmpeg_codec {
  int   id;
  unsigned int tc_id;
  char *name;
  char  fourCCs[10][5];
};

// fourCC to ID mapping taken from MPlayer's codecs.conf
static struct ffmpeg_codec ffmpeg_codecs[] = {
  {CODEC_ID_MSMPEG4V1, TC_CODEC_ERROR, "mp41",
    {"MP41", "DIV1", ""}},
  {CODEC_ID_MSMPEG4V2, TC_CODEC_MP42, "mp42",
    {"MP42", "DIV2", ""}},
  {CODEC_ID_MSMPEG4V3, TC_CODEC_DIVX3, "msmpeg4",
    {"DIV3", "DIV5", "AP41", "MPG3", "MP43", ""}},
  {CODEC_ID_MPEG4, TC_CODEC_DIVX4, "mpeg4",
    {"DIVX", "XVID", "MP4S", "M4S2", "MP4V", "UMP4", "DX50", ""}},
  {CODEC_ID_MJPEG, TC_CODEC_MJPEG, "mjpeg",
    {"MJPG", "AVRN", "AVDJ", "JPEG", "MJPA", "JFIF", ""}},
  {CODEC_ID_MPEG1VIDEO, TC_CODEC_MPG1, "mpeg1video",
    {"MPG1", ""}},
  {CODEC_ID_DVVIDEO, TC_CODEC_DV, "dvvideo",
    {"DVSD", ""}},
  {CODEC_ID_WMV1, TC_CODEC_WMV1, "wmv1",
    {"WMV1", ""}},
  {CODEC_ID_WMV2, TC_CODEC_WMV2, "wmv2",
    {"WMV2", ""}},
  {CODEC_ID_HUFFYUV, TC_CODEC_HUFFYUV, "hfyu",
    {"HFYU", ""}},
  {CODEC_ID_H263I, TC_CODEC_H263I, "h263i",
    {"I263", ""}},
  {CODEC_ID_H263P, TC_CODEC_H263P, "h263p",
    {"H263", "U263", "VIV1", ""}},
  {CODEC_ID_H264, TC_CODEC_H264, "h264",
    {"H264", "h264", "X264", "x264", "avc1", ""}},
  {CODEC_ID_RV10, TC_CODEC_RV10, "rv10",
    {"RV10", "RV13", ""}},
  {CODEC_ID_SVQ1, TC_CODEC_SVQ1, "svq1",
    {"SVQ1", ""}},
  {CODEC_ID_SVQ3, TC_CODEC_SVQ3, "svq3",
    {"SVQ3", ""}},
  {CODEC_ID_MPEG2VIDEO, TC_CODEC_MPEG2, "mpeg2video",
    {"MPG2", ""}},
  {CODEC_ID_MPEG2VIDEO, TC_CODEC_MPEG, "mpeg2video",
    {"MPG2", ""}},
  {CODEC_ID_ASV1, TC_CODEC_ASV1, "asv1",
    {"ASV1", ""}},
  {CODEC_ID_ASV2, TC_CODEC_ASV2, "asv2",
    {"ASV2", ""}},
  {CODEC_ID_FFV1, TC_CODEC_FFV1, "ffv1",
    {"FFV1", ""}},
  {CODEC_ID_RAWVIDEO, TC_CODEC_YUV420P, "raw",
    {"I420", "IYUV", ""}},
  {CODEC_ID_RAWVIDEO, TC_CODEC_YUV422P, "raw",
    {"Y42B", ""}},
  {0, TC_CODEC_UNKNOWN, NULL, {""}}};

#define BUFFER_SIZE SIZE_RGB_FRAME

static avi_t              *avifile = NULL;
static int                 pass_through = 0;
static uint8_t            *buffer =  NULL;
static uint8_t            *yuv2rgb_buffer = NULL;
static AVCodec            *lavc_dec_codec = NULL;
static AVCodecContext     *lavc_dec_context = NULL;
static int                 x_dim = 0, y_dim = 0;
static int                 pix_fmt, frame_size = 0;
static uint8_t            *frame = NULL;
static unsigned long       format_flag;
static struct ffmpeg_codec *codec;

static struct ffmpeg_codec *find_ffmpeg_codec(char *fourCC) {
  int i;
  struct ffmpeg_codec *cdc;

  cdc = &ffmpeg_codecs[0];
  while (cdc->name != NULL) {
    i = 0;
    while (cdc->fourCCs[i][0] != 0) {
      if (!strcasecmp(cdc->fourCCs[i], fourCC))
        return cdc;
      i++;
    }
    cdc++;
  }

  return NULL;
}

static struct ffmpeg_codec *find_ffmpeg_codec_id(unsigned int transcode_id) {
  struct ffmpeg_codec *cdc;

  cdc = &ffmpeg_codecs[0];
  while (cdc->name != NULL) {
      if (cdc->tc_id == transcode_id)
	  return cdc;
    cdc++;
  }

  return NULL;
}

inline static int stream_read_char(const uint8_t *d)
{
    return (*d & 0xff);
}

inline static unsigned int stream_read_dword(const uint8_t *s)
{
    unsigned int y;
    y=stream_read_char(s);
    y=(y<<8)|stream_read_char(s+1);
    y=(y<<8)|stream_read_char(s+2);
    y=(y<<8)|stream_read_char(s+3);
    return y;
}

// Determine of the compressed frame is a keyframe for direct copy
static int mpeg4_is_key(const uint8_t *data, long size)
{
        int result = 0;
        int i;

        for(i = 0; i < size - 5; i++)
        {
                if( data[i]     == 0x00 &&
                        data[i + 1] == 0x00 &&
                        data[i + 2] == 0x01 &&
                        data[i + 3] == 0xb6)
                {
                        if((data[i + 4] & 0xc0) == 0x0)
                                return 1;
                        else
                                return 0;
                }
        }

        return result;
}

static int divx3_is_key(const uint8_t *d)
{
    int32_t c=0;

    c=stream_read_dword(d);
    if(c&0x40000000) return(0);

    return(1);
}


static void enable_levels_filter(void)
{
    int handle = 0, id = tc_filter_find("levels");
    if (id == 0) {
        tc_log_info(MOD_NAME, "input is mjpeg, reducing range from YUVJ420P to YUV420P");
        handle = tc_filter_add("levels", "output=16-240:pre=1");
        if (!handle) {
            tc_log_warn(MOD_NAME, "cannot load levels filter");
        }
    }
}

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open {
  char   *fourCC = NULL;
  double  fps = 0;
  int extra_data_size = 0;
  long sret;
  int ret;

  if (param->flag == TC_VIDEO) {

    format_flag = vob->v_format_flag;

    sret = tc_file_check(vob->video_in_file); /* is a directory? */
    if (sret == 1)
      goto do_dv; /* yes, it is */
    else
      if (sret == -1)
        return TC_IMPORT_ERROR;

    if (format_flag == TC_MAGIC_AVI) {
      goto do_avi;
    } else if (format_flag==TC_MAGIC_DV_PAL || format_flag==TC_MAGIC_DV_NTSC) {
      tc_log_warn(MOD_NAME, "Format 0x%lX DV!!", format_flag);
      goto do_dv;
    } else {
      tc_log_warn(MOD_NAME, "Format 0x%lX not supported",
                      format_flag);
      return(TC_IMPORT_ERROR);
    }
    tc_log_info(MOD_NAME, "Format 0x%lX", format_flag);

do_avi:
    if(avifile==NULL) {
      if(vob->nav_seek_file) {
	if(NULL == (avifile = AVI_open_input_indexfile(vob->video_in_file,
                                                    0, vob->nav_seek_file))){
	  AVI_print_error("avi open error");
	  return(TC_IMPORT_ERROR);
	}
      } else {
	if(NULL == (avifile = AVI_open_input_file(vob->video_in_file,1))){
	  AVI_print_error("avi open error");
	  return(TC_IMPORT_ERROR);
	}
      }
    }

    // vob->offset contains the last keyframe
    if (!done_seek && vob->vob_offset>0) {
	AVI_set_video_position(avifile, vob->vob_offset);
	done_seek=1;
    }

    //important parameter
    x_dim = vob->im_v_width;
    y_dim = vob->im_v_height;
    fps   = vob->fps;

    fourCC = AVI_video_compressor(avifile);

    if (strlen(fourCC) == 0) {
      tc_log_warn(MOD_NAME, "FOURCC has zero length!? Broken source?");

      return TC_IMPORT_ERROR;
    }

    TC_INIT_LIBAVCODEC;

    codec = find_ffmpeg_codec(fourCC);
    if (codec == NULL) {
      tc_log_warn(MOD_NAME, "No codec is known for the FOURCC '%s'.",
              fourCC);
      return TC_IMPORT_ERROR;
    }

    lavc_dec_codec = avcodec_find_decoder(codec->id);
    if (!lavc_dec_codec) {
      tc_log_warn(MOD_NAME, "No codec found for the FOURCC '%s'.",
              fourCC);
      return TC_IMPORT_ERROR;
    }

    // Set these to the expected values so that ffmpeg's decoder can
    // properly detect interlaced input.
    lavc_dec_context = avcodec_alloc_context();
    if (lavc_dec_context == NULL) {
      tc_log_error(MOD_NAME, "Could not allocate enough memory.");
      return TC_IMPORT_ERROR;
    }
    lavc_dec_context->width  = x_dim;
    lavc_dec_context->height = y_dim;

    if (vob->decolor) lavc_dec_context->flags |= CODEC_FLAG_GRAY;
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
    lavc_dec_context->error_resilience  = 2;
#else
    lavc_dec_context->error_recognition = 2;
#endif
    lavc_dec_context->error_concealment = 3;
    lavc_dec_context->workaround_bugs = FF_BUG_AUTODETECT;
    lavc_dec_context->codec_tag= (fourCC[0]<<24) | (fourCC[1]<<16) |
                                 (fourCC[2]<<8) | (fourCC[3]);

    // XXX: some codecs need extra data
    switch (codec->id)
    {
      case CODEC_ID_MJPEG: extra_data_size  = 28; break;
      case CODEC_ID_LJPEG: extra_data_size  = 28; break;
      case CODEC_ID_HUFFYUV: extra_data_size = 1000; break;
      case CODEC_ID_ASV1: extra_data_size = 8; break;
      case CODEC_ID_ASV2: extra_data_size = 8; break;
      case CODEC_ID_WMV1: extra_data_size = 4; break;
      case CODEC_ID_WMV2: extra_data_size = 4; break;
      default: extra_data_size = 0; break;
    }

    if (extra_data_size) {
      lavc_dec_context->extradata = tc_zalloc(extra_data_size);
      if (!lavc_dec_context->extradata) {
        tc_log_error(MOD_NAME, "can't allocate extra_data");
        return TC_IMPORT_ERROR;
      }
      lavc_dec_context->extradata_size = extra_data_size;
    }

    TC_LOCK_LIBAVCODEC;
    ret = avcodec_open(lavc_dec_context, lavc_dec_codec);
    TC_UNLOCK_LIBAVCODEC;
    if (ret < 0) {
      tc_log_warn(MOD_NAME, "Could not initialize the '%s' codec.",
              codec->name);
      return TC_IMPORT_ERROR;
    }

    pix_fmt = vob->im_v_codec;

    frame_size = x_dim * y_dim * 3;
    switch (pix_fmt) {
      case CODEC_YUV:
        frame_size = x_dim*y_dim + 2*UV_PLANE_SIZE(IMG_YUV_DEFAULT,x_dim,y_dim);

	// we adapt the color space
        if(codec->id == CODEC_ID_MJPEG) {
	  enable_levels_filter();
        }
        break;
      case CODEC_RGB:
        frame_size = x_dim * y_dim * 3;

        if (yuv2rgb_buffer == NULL) yuv2rgb_buffer = tc_bufalloc(BUFFER_SIZE);

        if (yuv2rgb_buffer == NULL) {
          tc_log_perror(MOD_NAME, "out of memory");
          return TC_IMPORT_ERROR;
        } else
          memset(yuv2rgb_buffer, 0, BUFFER_SIZE);
        break;
      case CODEC_RAW:
      case CODEC_RAW_YUV:
      case CODEC_RAW_RGB:
        pass_through = 1;
        break;
    }

    if (!frame) {
        frame = tc_zalloc(frame_size);
        if (!frame) {
            tc_log_perror(MOD_NAME, "out of memory");
            return TC_IMPORT_ERROR;
        }
    }

    //----------------------------------------
    //
    // setup decoder
    //
    //----------------------------------------

    if(buffer == NULL) buffer=tc_bufalloc(frame_size);

    if(buffer == NULL) {
      tc_log_perror(MOD_NAME, "out of memory");
      return TC_IMPORT_ERROR;
    }

    memset(buffer, 0, frame_size);

    param->fd = NULL;

    return TC_IMPORT_OK;
do_dv:
    x_dim = vob->im_v_width;
    y_dim = vob->im_v_height;

    {
      char yuv_buf[255];
      //char ext_buf[255];
      struct ffmpeg_codec *codec;

      switch (vob->im_v_codec) {
	case CODEC_RGB:
	  tc_snprintf(yuv_buf, sizeof(yuv_buf), "rgb");
	  break;
	case CODEC_YUV:
	  tc_snprintf(yuv_buf, sizeof(yuv_buf), "yuv420p");
	  break;
      }

      codec = find_ffmpeg_codec_id (vob->v_codec_flag);
      if (codec == NULL) {
	tc_log_warn(MOD_NAME, "No codec is known for the TAG '%lx'.",
	    vob->v_codec_flag);
	return TC_IMPORT_ERROR;
      }

      // we adapt the color space
      if(codec->id == CODEC_ID_MJPEG) {
        enable_levels_filter();
      }

      sret = tc_snprintf(import_cmd_buf, TC_BUF_MAX,
			 "tccat -i \"%s\" -d %d |"
			 " tcextract -x dv -d %d |"
			 " tcdecode -x %s -t lavc -y %s -g %dx%d -Q %d -d %d",
			 vob->video_in_file, vob->verbose, vob->verbose,
			 codec->name, yuv_buf, x_dim, y_dim, vob->quality,
			 vob->verbose);
      if (sret < 0)
        return(TC_IMPORT_ERROR);
    }

    // print out
    if(verbose_flag) tc_log_info(MOD_NAME, "%s", import_cmd_buf);

    // set to NULL if we handle read
    param->fd = NULL;

    // popen
    if((param->fd = popen(import_cmd_buf, "r"))== NULL) {
      tc_log_perror(MOD_NAME, "popen LAVC stream");
      return(TC_IMPORT_ERROR);
    }

    return TC_IMPORT_OK;

  }

  return TC_IMPORT_ERROR;
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode {

  /*
   * When using directory mode or dvraw etc, we don't enter here
   * (transcode-core does the reading) so there is no need to protect this
   * stuff by and if() or something.
   */

  int        key, len;
  long       bytes_read = 0;
  int        got_picture;
  uint8_t   *src_planes[3];
  uint8_t   *dst_planes[3];
  int        src_fmt, dst_fmt;
  AVFrame    picture;

  if (param->flag == TC_VIDEO) {
    bytes_read = AVI_read_frame(avifile, (char*)buffer, &key);

    if (bytes_read < 0) return TC_IMPORT_ERROR;

    if (key) param->attributes |= TC_FRAME_IS_KEYFRAME;

    // PASS_THROUGH MODE

    if (pass_through) {
      int bkey = 0;

      // check for keyframes
      if (codec->id == CODEC_ID_MSMPEG4V3) {
	if (divx3_is_key(buffer)) bkey = 1;
      }
      else if (codec->id == CODEC_ID_MPEG4) {
	if (mpeg4_is_key(buffer, bytes_read)) bkey = 1;
      }
      else if (codec->id == CODEC_ID_MJPEG) {
	bkey = 1;
      }

      if (bkey) {
	param->attributes |= TC_FRAME_IS_KEYFRAME;
      }

      if (verbose & TC_DEBUG)
	if (key || bkey)
          tc_log_info(MOD_NAME, "Keyframe info (AVI | Bitstream) (%d|%d)",
                  key, bkey);

      param->size = (int) bytes_read;
      ac_memcpy(param->buffer, buffer, bytes_read);

      return TC_IMPORT_OK;
    }

    if (bytes_read == 0) {
        // repeat last frame
        ac_memcpy(param->buffer, frame, frame_size);
        param->size = frame_size;
        return TC_IMPORT_OK;
    }

    // ------------
    // decode frame
    // ------------

retry:
    do {
      TC_LOCK_LIBAVCODEC;
      AVPacket avpkt;
      av_init_packet(&avpkt);
      avpkt.data = NULL;
      avpkt.size = 0;
      len = avcodec_decode_video2(lavc_dec_context, &picture,
			         &got_picture, &avpkt);
      TC_UNLOCK_LIBAVCODEC;

      if (len < 0) {
	tc_log_warn (MOD_NAME, "frame decoding failed");
        return TC_IMPORT_ERROR;
      }
      if (!got_picture) {
	if (avifile->video_pos == 1) {

	  bytes_read = AVI_read_frame(avifile, (char*)buffer, &key);
	  if (bytes_read < 0) return TC_IMPORT_ERROR;
	  param->attributes &= ~TC_FRAME_IS_KEYFRAME;
	  if (key) param->attributes |= TC_FRAME_IS_KEYFRAME;
	  goto retry;

	} else {

	  // repeat last frame
	  ac_memcpy(param->buffer, frame, frame_size);
	  param->size = frame_size;
	  return TC_IMPORT_OK;
	}
      }
    } while (0);

    dst_fmt = (pix_fmt == CODEC_YUV) ?IMG_YUV_DEFAULT :IMG_RGB_DEFAULT;
    YUV_INIT_PLANES(dst_planes, param->buffer, dst_fmt,
                    lavc_dec_context->width, lavc_dec_context->height);

    // Convert avcodec image to our internal YUV or RGB format
    switch (lavc_dec_context->pix_fmt) {
      case PIX_FMT_YUVJ420P:
      case PIX_FMT_YUV420P:
        src_fmt = IMG_YUV420P;
        YUV_INIT_PLANES(src_planes, frame, src_fmt,
                        lavc_dec_context->width, lavc_dec_context->height);

	// Remove "dead space" at right edge of planes, if any
	if (picture.linesize[0] != lavc_dec_context->width) {
	    int y;
	    for (y = 0; y < lavc_dec_context->height; y++) {
                ac_memcpy(src_planes[0] + y*lavc_dec_context->width,
			  picture.data[0] + y*picture.linesize[0],
			  lavc_dec_context->width);
	    }
	    for (y = 0; y < lavc_dec_context->height / 2; y++) {
		ac_memcpy(src_planes[1] + y*(lavc_dec_context->width/2),
			  picture.data[1] + y*picture.linesize[1],
			  lavc_dec_context->width/2);
		ac_memcpy(src_planes[2] + y*(lavc_dec_context->width/2),
			  picture.data[2] + y*picture.linesize[2],
			  lavc_dec_context->width/2);
	    }
	} else {
	    ac_memcpy(src_planes[0], picture.data[0],
		      lavc_dec_context->width * lavc_dec_context->height);
	    ac_memcpy(src_planes[1], picture.data[1],
		      (lavc_dec_context->width/2)*(lavc_dec_context->height/2));
	    ac_memcpy(src_planes[2], picture.data[2],
		      (lavc_dec_context->width/2)*(lavc_dec_context->height/2));
	}
        break;

      case PIX_FMT_YUV411P:
        src_fmt = IMG_YUV411P;
        YUV_INIT_PLANES(src_planes, frame, src_fmt,
                        lavc_dec_context->width, lavc_dec_context->height);

	if (picture.linesize[0] != lavc_dec_context->width) {
	    int y;
            for (y = 0; y < lavc_dec_context->height; y++) {
                ac_memcpy(src_planes[0] + y*lavc_dec_context->width,
			              picture.data[0] + y*picture.linesize[0],
                          lavc_dec_context->width);
                ac_memcpy(src_planes[1] + y*(lavc_dec_context->width/4),
			              picture.data[1] + y*picture.linesize[1],
                          lavc_dec_context->width/4);
                ac_memcpy(src_planes[2] + y*(lavc_dec_context->width/4),
			              picture.data[2] + y*picture.linesize[2],
                          lavc_dec_context->width/4);
            }
	} else {
	    ac_memcpy(src_planes[0], picture.data[0],
		      lavc_dec_context->width * lavc_dec_context->height);
	    ac_memcpy(src_planes[1], picture.data[1],
		      (lavc_dec_context->width/4) * lavc_dec_context->height);
	    ac_memcpy(src_planes[2], picture.data[2],
		      (lavc_dec_context->width/4) * lavc_dec_context->height);
        }
        break;

      case PIX_FMT_YUVJ422P:
      case PIX_FMT_YUV422P:
        src_fmt = IMG_YUV422P;
        YUV_INIT_PLANES(src_planes, frame, src_fmt,
                        lavc_dec_context->width, lavc_dec_context->height);

        if (picture.linesize[0] != lavc_dec_context->width) {
	    int y;
            for (y = 0; y < lavc_dec_context->height; y++) {
                ac_memcpy(src_planes[0] + y*lavc_dec_context->width,
			              picture.data[0] + y*picture.linesize[0],
                          lavc_dec_context->width);
                ac_memcpy(src_planes[1] + y*(lavc_dec_context->width/2),
			              picture.data[1] + y*picture.linesize[1],
                          lavc_dec_context->width/2);
                ac_memcpy(src_planes[2] + y*(lavc_dec_context->width/2),
			              picture.data[2] + y*picture.linesize[2],
                          lavc_dec_context->width/2);
            }
	} else {
	    ac_memcpy(src_planes[0], picture.data[0],
		      lavc_dec_context->width * lavc_dec_context->height);
	    ac_memcpy(src_planes[1], picture.data[1],
		      (lavc_dec_context->width/2) * lavc_dec_context->height);
	    ac_memcpy(src_planes[2], picture.data[2],
		      (lavc_dec_context->width/2) * lavc_dec_context->height);
        }
	break;

      case PIX_FMT_YUVJ444P:
      case PIX_FMT_YUV444P:
        src_fmt = IMG_YUV444P;
        YUV_INIT_PLANES(src_planes, frame, src_fmt,
                        lavc_dec_context->width, lavc_dec_context->height);

	if (picture.linesize[0] != lavc_dec_context->width) {
	    int y;
            for (y = 0; y < lavc_dec_context->height; y++) {
                ac_memcpy(picture.data[0] + y*lavc_dec_context->width,
			              picture.data[0] + y*picture.linesize[0],
                          lavc_dec_context->width);
                ac_memcpy(picture.data[1] + y*lavc_dec_context->width,
			              picture.data[1] + y*picture.linesize[1],
                          lavc_dec_context->width);
                ac_memcpy(picture.data[2] + y*lavc_dec_context->width,
			              picture.data[2] + y*picture.linesize[2],
                          lavc_dec_context->width);
            }
	} else {
	    ac_memcpy(src_planes[0], picture.data[0],
		      lavc_dec_context->width * lavc_dec_context->height);
	    ac_memcpy(src_planes[1], picture.data[1],
		      lavc_dec_context->width * lavc_dec_context->height);
	    ac_memcpy(src_planes[2], picture.data[2],
		      lavc_dec_context->width * lavc_dec_context->height);
        }
        break;

      default:
	tc_log_warn(MOD_NAME, "Unsupported decoded frame format: %d",
		    lavc_dec_context->pix_fmt);
        return TC_IMPORT_ERROR;
    }

    ac_imgconvert(src_planes, src_fmt, dst_planes, dst_fmt,
                  lavc_dec_context->width, lavc_dec_context->height);
    param->size = frame_size;

    return TC_IMPORT_OK;
  }

  return TC_IMPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close {

  if (param->flag == TC_VIDEO) {

    if(lavc_dec_context) {
      if (!pass_through)
	avcodec_flush_buffers(lavc_dec_context);

      avcodec_close(lavc_dec_context);
      if (lavc_dec_context->extradata_size) free(lavc_dec_context->extradata);
      free(lavc_dec_context);

      lavc_dec_context = NULL;
      done_seek=0;

      pass_through = 0;

    }

    if (param->fd) pclose(param->fd);
    param->fd = NULL;

    if(avifile!=NULL) {
      AVI_close(avifile);
      avifile=NULL;
    }

    // do not free buffer and yuv2rgb_buffer!!
    /* 
     * because they are static variables and are conditionally allocated
     * -- fromani 20051112
     */
    return TC_IMPORT_OK;
  }

  return TC_IMPORT_ERROR;
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */

