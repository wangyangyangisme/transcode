/*
 *  export_mov.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) (2002) Christian Vogelgsang
 *  <Vogelgsang@informatik.uni-erlangen.de> (extension for all codecs supported
 *  by quicktime4linux
 *  <stefanscheffler@gmx.net> 2004 fixes & features
 *  Copyright (C) ken@hero.com 2001 (initial module author)
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
 */

#define MOD_NAME    "export_mov.so"
#define MOD_VERSION "v0.1.2 (2004-01-19)"
#define MOD_CODEC   "(video) * | (audio) *"

#include "transcode.h"
#include "import/magic.h"
#include "encoder.h"
#include "libtc/optstr.h"
#include "libtcvideo/tcvideo.h"
#include <stdio.h>
#include <stdlib.h>

/* verbose flag */
static int verbose_flag=TC_QUIET;

/* set encoder capabilities */
static int capability_flag=
 /* audio formats */
 TC_CAP_PCM| /* pcm audio */
 /* video formats */
 TC_CAP_RGB|  /* rgb frames */
 TC_CAP_YUV|  /* YUV 4:2:0 */
 TC_CAP_YUY2|
 TC_CAP_VID|
 TC_CAP_YUV422; /* YUV 4:2:2 */

#define MOD_PRE mov
#include "export_def.h"

#include <quicktime.h>
#include <colormodels.h>
#include <lqt.h>

#define QT_LIST_AUDIO "audio codec"
#define QT_LIST_VIDEO "video codec"
#define QT_LIST_PARM "parameters"

/* exported quicktime file */
static quicktime_t *qtfile = NULL;

/* row pointer for source frames */
static unsigned char** row_ptr = NULL;

/* temporary buffer */
static uint8_t *tmp_buf;

/* toggle for raw frame export */
static int rawVideo = 0;
static int rawAudio = 0;

/* colormodels */
static ImageFormat tc_cm = 0;
static int qt_cm = 0;

/* store frame dimension */
static int w = 0, h = 0;

/* audio channels */
static int channels = 0;

/* sample size */
static int bits = 0;

/* encode audio buffers */
static int16_t* audbuf0 = NULL;
static int16_t* audbuf1 = NULL;

struct qt_codec_list {
    char *name;
    char *internal_name;
    char *comments;
};

static TCVHandle tcvhandle = 0;

/* special paramters not retrievable from lqt */
struct qt_codec_list qt_param_list[] = {
  {"", "",  ""},
  {"Special Parameters:", "",  ""},
  {"copyright", "",  "Copyright string (no '=' or ',' allowed)"},
  {"name", "",  "Name string (no '=' or ',' allowed) "},
  {"info", "",  "Info string (no '=' or ',' allowed) "},
  {NULL, NULL, NULL}};

#ifdef LIBQUICKTIME_000904
/* from libquicktime */
static int tc_quicktime_get_timescale(double frame_rate)
{
	int timescale = 600;
	/* Encode the 29.97, 23.976, 59.94 framerates */
	if(frame_rate - (int)frame_rate != 0)
		timescale = (int)(frame_rate * 1001 + 0.5);
	else
		if((600 / frame_rate) - (int)(600 / frame_rate) != 0)
			timescale = (int)(frame_rate * 100 + 0.5);
	return timescale;
}
#endif

/* print list of things. Shamelessly stolen from export_ffmpeg.c */
static int list(char *list_type)
{
    int cod = 0;
    int i = 0;

    lqt_codec_info_t ** qi = NULL;


    if (strcmp(list_type, QT_LIST_VIDEO) == 0) {
        qi = lqt_query_registry(0, 1, 1, 0);
    } else if (strcmp(list_type, QT_LIST_AUDIO) == 0) {
        qi = lqt_query_registry(1, 0, 1, 0);
    } else {
        qi = lqt_query_registry(1, 1, 1, 0);
    }

    tc_log_info(MOD_NAME, "List of supported %s:", list_type);
    tc_log_info(MOD_NAME, "Name                    comments");
    tc_log_info(MOD_NAME, "---------------         "
                          "-----------------------------------");
    while (qi[cod] != NULL) {
        if (strcmp(list_type, QT_LIST_PARM) == 0) {
            tc_log_info(MOD_NAME, "%s:", qi[cod]->name);
            for(i = 0; i < qi[cod]->num_encoding_parameters; i++) {
                if (qi[cod]->encoding_parameters[i].type != LQT_PARAMETER_SECTION) {
                    tc_log_info(MOD_NAME, " %-23s %s",
                        qi[cod]->encoding_parameters[i].name,
                        qi[cod]->encoding_parameters[i].real_name);
                }
            }
        } else {
            tc_log_info(MOD_NAME, "%-23s %s",
                qi[cod]->name,
                qi[cod]->description);
        }
        cod++;
    }

    return 1;
}

/* ------------------------------------------------------------
 *
 * open outputfile
 *
 * ------------------------------------------------------------*/

MOD_open
{
  if(param->flag == TC_VIDEO)
    return(0);

  if(param->flag == TC_AUDIO)
    return(0);

  return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{

  int list_was_called = 0;

  /* for codec parameters */
  int jpeg_quality = 0;
  int div3_bitrate_tolerance = 500000;
  int vorbis_max_bitrate = 192;
  int vorbis_min_bitrate = 128;

  /* overwriting empty parameters now saves trouble later */
  if (vob->ex_v_fcc == NULL) vob->ex_v_fcc = "";
  if (vob->ex_a_fcc == NULL) vob->ex_a_fcc = "";
  if (vob->ex_profile_name == NULL) vob->ex_profile_name = "";

  if (!strcasecmp(vob->ex_v_fcc, "list")) list_was_called = list(QT_LIST_VIDEO);
  if (!strcasecmp(vob->ex_a_fcc, "list")) list_was_called = list(QT_LIST_AUDIO);
  if (!strcasecmp(vob->ex_profile_name, "list")) {
    int i;

    list_was_called = list(QT_LIST_PARM);

    /* list special paramters at the end */
    for(i = 0; qt_param_list[i].name != NULL; i++) {
        tc_log_info(MOD_NAME, "  %-23s %s",
            qt_param_list[i].name,
            qt_param_list[i].comments);
    }
  }

  if (list_was_called) {
    return(TC_EXPORT_ERROR);
  }
  /* video setup -------------------------------------------------- */
  if(param->flag == TC_VIDEO) {

    lqt_codec_info_t ** qt_codec_info = NULL;

    char *qt_codec;
    int divx_bitrate;

    /* fetch frame size */
    w = vob->ex_v_width;
    h = vob->ex_v_height;

    /* fetch qt codec from -F switch */
    qt_codec =  tc_strdup(vob->ex_v_fcc);

    /* open target file for writing */
    if(NULL == (qtfile = quicktime_open((char *)vob->video_out_file, 0, 1)) ){
        tc_log_warn(MOD_NAME, "error opening qt file '%s'",
            vob->video_out_file);
        return(TC_EXPORT_ERROR);
    }

    /* set qt codecs */
    /* not needed for passthrough*/
    if (vob->im_v_codec != CODEC_RAW && vob->im_v_codec != CODEC_RAW_YUV && vob->im_v_codec != CODEC_RAW_RGB) {
        if(qt_codec == NULL || strlen(qt_codec)==0) {
            /* default */
            qt_codec = "mjpa";
            if (verbose_flag != TC_QUIET) {
                tc_log_info(MOD_NAME, "Empty qt codec name. Switching to %s use '-F list'"
                                " to get a list of supported codec.", qt_codec);
            }
        }

        /* check if we can encode with this codec */
        qt_codec_info = lqt_find_video_codec_by_name(qt_codec);
        if (!qt_codec_info) {
        tc_log_warn(MOD_NAME, "qt video codec '%s' not supported!", qt_codec);
            return(TC_EXPORT_ERROR);
        }

#if !defined(LIBQUICKTIME_000904)
        /* set proposed video codec */
        lqt_set_video(qtfile, 1, w, h, vob->ex_fps,qt_codec_info[0]);
#else
        /* set proposed video codec */
        lqt_set_video(qtfile, 1, w, h,
		tc_quicktime_get_timescale(vob->ex_fps) / vob->ex_fps+0.5,
		tc_quicktime_get_timescale(vob->ex_fps), qt_codec_info[0]);
#endif
    }

    /* set color model */
    switch(vob->im_v_codec) {
        case CODEC_RGB:
            qt_cm = BC_RGB888;
            tc_cm = IMG_RGB_DEFAULT;
            break;

        case CODEC_YUV:
            qt_cm = BC_YUV420P;
            tc_cm = IMG_YUV_DEFAULT;
            break;

        case CODEC_YUV422:
            tc_cm = IMG_YUV422P;
            qt_cm = BC_YUV422P;
            break;

        case CODEC_YUY2:
            tc_cm = IMG_YUY2;
            qt_cm = BC_YUV422;
            break;

         /* passthrough */
        case CODEC_RAW_RGB:
        case CODEC_RAW_YUV:
        case CODEC_RAW:
            /* set out output codec to input codec */
            if(qt_codec == NULL || strlen(qt_codec)==0) {
                switch (vob->v_codec_flag) {
                    case TC_CODEC_MJPEG:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"jpeg");
                        break;

                    case TC_CODEC_MPEG:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"mpeg");
                        break;

                    case TC_CODEC_DV:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"dvc ");
                        break;

                    case TC_CODEC_SVQ1:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"SVQ1");
                        break;

                    case TC_CODEC_SVQ3:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"SVQ3");
                        break;

                    case TC_CODEC_YUV420P:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"yv12");
                        break;

                    case TC_CODEC_RGB:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"raw ");
                        break;

                    case TC_CODEC_YUV2: /* test this */
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"yuv2");
                        break;

                    case TC_CODEC_DIVX3:
                    case TC_CODEC_DIVX4:
                    case TC_CODEC_DIVX5:
                        quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,"DIVX");
                        break;

                    default:
                        tc_log_warn(MOD_NAME, "codec '%lx' not supported for pass-through",
					vob->v_codec_flag);
			tc_log_warn(MOD_NAME, "        If you really know what you are doing you can force");
                        tc_log_warn(MOD_NAME, "        a codec via -F <vc>, '-F list' returns a list");
                        return(TC_EXPORT_ERROR);
                        break;
                }
            }
            else {
                tc_log_warn(MOD_NAME,"Overriding the output codec is almost never a good idea");
                quicktime_set_video(qtfile, 1, w, h, vob->ex_fps,qt_codec);
            }

            rawVideo = 1;
            break;

        default:
            /* unsupported internal format */
	    tc_log_warn(MOD_NAME, "unsupported internal video format %x",
                vob->ex_v_codec);
            return(TC_EXPORT_ERROR);
            break;
    }
    
    /* if cs conversation not supported for codec do conversation */
    /* not required for pass through */
    if (vob->im_v_codec != CODEC_RAW && vob->im_v_codec != CODEC_RAW_YUV && vob->im_v_codec != CODEC_RAW_RGB) {
        if (quicktime_writes_cmodel(qtfile, qt_cm, 0) != 1) { 
            if (verbose_flag != TC_QUIET) {
                tc_log_info(MOD_NAME,"Colorspace not supported for this codec converting to RGB");
            }
            
            qt_cm = BC_RGB888;
            lqt_set_cmodel(qtfile, 0, qt_cm);
	    
            tcvhandle = tcv_init();
            if (!tcvhandle) {
                tc_log_warn(MOD_NAME, "image conversion init failed");
                return TC_EXPORT_ERROR;
            }
            
        } else {
            lqt_set_cmodel(qtfile, 0, qt_cm);
        }
    }

    /* set codec parameters */
    /* tc uses kb */
    divx_bitrate = vob->divxbitrate * 1000;
    /* if max bitrate > bitrate  use difference for bitrate tolerance */
    if (vob->video_max_bitrate > vob->divxbitrate) div3_bitrate_tolerance = (vob->video_max_bitrate - vob->divxbitrate) * 1000;


    /* Audio */
    /* must be changed when it's changed in lqt; "bit_rate" conflicts with ffmpeg video codec */
    if (strcmp(qt_codec,"ffmpeg_mp2") == 0||
        strcmp(qt_codec,"ffmpeg_mp3") == 0||
        strcmp(qt_codec,"ffmpeg_ac3") == 0)
        quicktime_set_parameter(qtfile, "bit_rate", &vob->mp3bitrate);

    if (strcmp(qt_codec,"lame") == 0)
        quicktime_set_parameter(qtfile, "mp3_bitrate", &vob->mp3bitrate);

    /* have to check this */
    if (strcmp(qt_codec,"vorbis") == 0) {
        quicktime_set_parameter(qtfile, "vorbis_bitrate", &vob->mp3bitrate);
        quicktime_set_parameter(qtfile, "vorbis_max_bitrate", &vorbis_max_bitrate);
        quicktime_set_parameter(qtfile, "vorbis_min_bitrate", &vorbis_min_bitrate);
        quicktime_set_parameter(qtfile, "vorbis_vbr", &vob->a_vbr);
    }

    /* Video */
    /* jpeg_quality == 0-100 ; tc quality setting (-Q) ==  1-5 */
    jpeg_quality = 20 * vob->divxquality;

    if (strcmp(qt_codec,"mjpa") == 0||
        strcmp(qt_codec,"jpeg") == 0)
        quicktime_set_parameter(qtfile, "jpeg_quality", &jpeg_quality);

    /* looks terrible :/ */
    if (strcmp(qt_codec,"ffmpeg_mjpg") == 0||
        strcmp(qt_codec,"ffmpeg_h263p") == 0||
        strcmp(qt_codec,"ffmpeg_h263") == 0||
        strcmp(qt_codec,"ffmpeg_msmpeg4v3") == 0||
        strcmp(qt_codec,"ffmpeg_msmpeg4v2") == 0||
        strcmp(qt_codec,"ffmpeg_msmpeg4v1") == 0||
        strcmp(qt_codec,"ffmpeg_msmpg1") == 0||
        strcmp(qt_codec,"ffmpeg_mpg4") == 0){

        int min_bitrate = 0;

        quicktime_set_parameter(qtfile, "flags_gray", &vob->decolor); /* set format different for this */

        switch (vob->decolor) {
        case 1:
            quicktime_set_parameter(qtfile, "aspect_ratio_info", "Square");
            break;

        case 2:
            quicktime_set_parameter(qtfile, "aspect_ratio_info", "4:3");
            break;

        case 3:
            quicktime_set_parameter(qtfile, "aspect_ratio_info", "16:9");
            break;

        default:
            tc_log_warn(MOD_NAME, "Given aspect ratio not supported, using default");
            break;
        }

        quicktime_set_parameter(qtfile, "flags_gray", &vob->decolor);
        quicktime_set_parameter(qtfile, "bit_rate", &vob->divxbitrate);
        quicktime_set_parameter(qtfile, "bit_rate_tolerance", &div3_bitrate_tolerance);

        min_bitrate = vob->divxbitrate - div3_bitrate_tolerance;
        quicktime_set_parameter(qtfile, "rc_max_rate", &vob->video_max_bitrate);
        quicktime_set_parameter(qtfile, "qmax", &vob->max_quantizer);
        quicktime_set_parameter(qtfile, "qmin", &vob->min_quantizer);


        if (!strcmp(qt_codec,"ffmpeg_mjpg"))
            quicktime_set_parameter(qtfile, "gob_size", &vob->divxkeyframes);
    }

    if (strcmp(vob->ex_v_fcc,"opendivx") == 0) {
        quicktime_set_parameter(qtfile, "divx_bitrate", &divx_bitrate);
        quicktime_set_parameter(qtfile, "divx_rc_period", &vob->rc_period);
        quicktime_set_parameter(qtfile, "divx_rc_reaction_period", &vob->rc_reaction_period);
        quicktime_set_parameter(qtfile, "divx_rc_reaction_ratio", &vob->rc_reaction_ratio);
        quicktime_set_parameter(qtfile, "divx_max_key_interval", &vob->divxkeyframes);
        quicktime_set_parameter(qtfile, "divx_min_quantizer", &vob->min_quantizer);
        quicktime_set_parameter(qtfile, "divx_max_quantizer", &vob->max_quantizer);
        quicktime_set_parameter(qtfile, "divx_quantizer", &vob->min_quantizer);
        quicktime_set_parameter(qtfile, "divx_quality", &vob->quality);
    }

    if (strcmp(qt_codec,"rtjpeg") == 0)
        quicktime_set_parameter(qtfile, "rtjpeg_quality", &jpeg_quality);


    /* set extended parameters */
    if (vob->ex_profile_name != NULL) {
        int i;
        char meta[128];

        if (optstr_get(vob->ex_profile_name, "copyright", "%128[^:]", meta) > 0) {
            quicktime_set_copyright(qtfile, meta);
        }
        if(optstr_get(vob->ex_profile_name, "name", "%128[^:]", meta) > 0) {
            quicktime_set_name(qtfile, meta);
        }
        if(optstr_get(vob->ex_profile_name, "info", "%128[^:]", meta) > 0) {
            quicktime_set_name(qtfile, meta);
        }

        for (i = 0; i < (* qt_codec_info)->num_encoding_parameters; i++) {
            lqt_parameter_info_t param = (* qt_codec_info)->encoding_parameters[i];

            if (param.type == LQT_PARAMETER_INT) {
                int val;
                if (optstr_get(vob->ex_profile_name, param.name, "%i", &val) > 0) {
                    lqt_set_video_parameter(qtfile, 0, param.name, &val);
                }
            }
            else if (param.type == LQT_PARAMETER_FLOAT) {
                float val;
                if (optstr_get(vob->ex_profile_name, param.name, "%f", &val) > 0) {
                    lqt_set_video_parameter(qtfile, 0, param.name, &val);
                }
            }
            else if (param.type == LQT_PARAMETER_STRING || param.type == LQT_PARAMETER_STRINGLIST) {
                char val[128];
                if (optstr_get(vob->ex_profile_name, param.name, "%128[^:]", val) > 0) {
                    lqt_set_video_parameter(qtfile, 0, param.name, val);
                }
            }
        }
    }

    /* alloc row pointers for frame encoding */
    row_ptr = malloc (sizeof(char *) * h);

    /* same for temp buffer ... used during yuy2/yv12 encoding */
    tmp_buf = malloc (w*h*2);

    /* verbose */
    tc_log_info(MOD_NAME, "video codec='%s' w=%d h=%d fps=%g",
	    qt_codec,w,h,vob->ex_fps);

    return(0);
  }

  /* audio setup -------------------------------------------------- */
  if(param->flag == TC_AUDIO){
    const char *qt_codec;
    lqt_codec_info_t ** qt_codec_info = 0;

    /* no audio setup if we don't have any channels (size == 0 might be better?)*/
    if(vob->dm_chan==0) return 0;

    /* check given audio format */
    if((vob->dm_chan!=1)&&(vob->dm_chan!=2)) {
        tc_log_warn(MOD_NAME, "Only mono or stereo audio supported");
        return(TC_EXPORT_ERROR);
    }
    channels = vob->dm_chan;
    bits = vob->dm_bits;

    /* fetch qt codec from -F switch */
    qt_codec = vob->ex_a_fcc;
    if(qt_codec == NULL || strlen(qt_codec)==0) {
        qt_codec = "ima4";
        if (verbose_flag != TC_QUIET) {
            tc_log_info(MOD_NAME, "empty qt codec name - switching to %s"
			          " use '-F ,list'", qt_codec);
	    tc_log_info(MOD_NAME, "to get a list of supported codec");
        }
    }


    /* check encoder mode */
    switch(vob->im_a_codec) {
        case CODEC_PCM:
            /* allocate sample buffers */
            audbuf0 = (int16_t*)malloc(vob->ex_a_size);
            audbuf1 = (int16_t*)malloc(vob->ex_a_size);

            /* need to encode audio */
            rawAudio = 0;
            break;

        default:
            /* unsupported internal format */
            tc_log_warn(MOD_NAME, "unsupported internal audio format %x",
                vob->ex_v_codec);
            return(TC_EXPORT_ERROR);
            break;
    }

    qt_codec_info = lqt_find_audio_codec_by_name(qt_codec);
    if (!qt_codec_info){
        tc_log_warn(MOD_NAME, "qt audio codec '%s' unsupported",
            qt_codec);
        return(TC_EXPORT_ERROR);
    }

    /* setup audio parameters */
    lqt_set_audio(qtfile,channels,vob->a_rate,bits,qt_codec_info[0]);

    /* verbose */
    tc_log_info(MOD_NAME, "audio codec='%s' bits=%d chan=%d rate=%d",
	    qt_codec,bits,channels,vob->a_rate);
    return(0);
  }

  return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * encode and export frame
 *
 * ------------------------------------------------------------*/


MOD_encode
{
  /* video -------------------------------------------------------- */
  if(param->flag == TC_VIDEO) {
    vob_t *vob = tc_get_vob();
    
    /* raw mode is easy */
    if(rawVideo) {
        /* add divx keyframes if needed */
        if(quicktime_divx_is_key(param->buffer, param->size) == 1) quicktime_insert_keyframe(qtfile, (int)tc_get_frames_encoded(), 0);
        if(quicktime_write_frame(qtfile,param->buffer,param->size,0)<0) {
            tc_log_warn(MOD_NAME, "error writing raw video frame");
            return(TC_EXPORT_ERROR);
        }
    }

    /* encode frame */
    else {
        uint8_t *ptr = param->buffer;
        int iy;

        switch(qt_cm) {
            case BC_RGB888:
                /* convert to rgb if unsupported */
                if (tc_cm != IMG_RGB24) {
                    if (!tcv_convert(tcvhandle, param->buffer, param->buffer,
                                     vob->ex_v_width, vob->ex_v_height,
                                     tc_cm, IMG_RGB24)) {
                        tc_log_warn(MOD_NAME, "image format conversion failed");
                        return TC_EXPORT_ERROR;
                    }
                }

                /* setup row pointers for RGB */
                for (iy = 0; iy < h; iy++) {
                    row_ptr[iy] = ptr;
                    ptr += w*3;
                }
                break;
            case BC_YUV420P:
                /* setup row pointers for YUV420P */
                row_ptr[0] = ptr;
                ptr = ptr + (h * w);
                /* note, quicktime wants YV12 so we reverse the planes */
                row_ptr[2] = ptr;
                ptr = ptr + (h * w) / 4;
                row_ptr[1] = ptr;
                break;
            case BC_YUV422P:
                /* setup row pointers for YUV422P */
                row_ptr[0] = ptr;
                ptr = ptr + (h * w);
                row_ptr[1] = ptr;
                ptr = ptr + (h * w) / 2;
                row_ptr[2] = ptr;
                break;
            case BC_YUV422:
                /* setup row pointers for YUY2 */
                for (iy = 0; iy < h; iy++) {
                    row_ptr[iy] = ptr;
                    ptr += w*2;
                }
                break;
        }

        if(quicktime_encode_video(qtfile, row_ptr, 0)<0) {
            tc_log_warn(MOD_NAME, "error encoding video frame");
            return(TC_EXPORT_ERROR);
        }
    }

    return(0);
  }
  /* audio -------------------------------------------------------- */
  if(param->flag == TC_AUDIO){
    /* raw mode is easy */
    if(rawAudio) {
        if(quicktime_write_frame(qtfile,
            param->buffer,param->size,0)<0) {
            tc_log_warn(MOD_NAME, "error writing raw audio frame");
            return(TC_EXPORT_ERROR);
        }
    }
    /* encode audio */
    else {
        int s,t;
        int16_t *aptr[2] = { audbuf0, audbuf1 };

        /* calc number of samples */
        int samples = param->size;

        /* no audio */
        if (samples == 0) return 0;

        if(bits==16)
            samples >>= 1;
        if(channels==2)
            samples >>= 1;

        /* fill audio buffer */
        if(bits==8) {
            /* UNTESTED: please verify :) */
            if(channels==1) {
                for(s=0;s<samples;s++)
                    audbuf0[s] = ((((int16_t)param->buffer[s]) << 8)-0x8000);
            }
            else /* stereo */ {
                for(s=0,t=0;s<samples;s++,t+=2) {
                    audbuf0[s] = ((((int16_t)param->buffer[t]) << 8)-0x8000);
                    audbuf1[s] = ((((int16_t)param->buffer[t+1]) << 8)-0x8000);
                }
            }
        }
        else /* 16 bit */ {
            if(channels==1) {
                aptr[0] = (int16_t *)param->buffer;
            }
            else /* stereo */ {
                /* decouple channels */
                for(s=0,t=0;s<samples;s++,t+=2) {
                    audbuf0[s] = ((int16_t *)param->buffer)[t];
                    audbuf1[s] = ((int16_t *)param->buffer)[t+1];
                }
            }
        }

        /* encode audio samples */
        if ((quicktime_encode_audio(qtfile, aptr, NULL, samples)<0)){
            tc_log_warn(MOD_NAME, "error encoding audio frame");
            return(TC_EXPORT_ERROR);
        }
    }

    return(0);
  }

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
    /* free row pointers */
    if(row_ptr!=NULL)
        free(row_ptr);
    return(0);
  }

  if(param->flag == TC_AUDIO) {
    /* free audio buffers */
    if(audbuf0!=NULL)
        free(audbuf0);
    if(audbuf1!=NULL)
        free(audbuf1);
    return(0);
  }

  return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close outputfile
 *
 * ------------------------------------------------------------*/

MOD_close
{
  if(param->flag == TC_VIDEO){
    /* close quicktime file */
    quicktime_close(qtfile);
    qtfile=NULL;
    return(0);
    }

  if(param->flag == TC_AUDIO) {
    return(0);
  }

  return(TC_EXPORT_ERROR);

}
