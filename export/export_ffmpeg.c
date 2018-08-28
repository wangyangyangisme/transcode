/*
 *  export_ffmpeg.c
 *    based heavily on mplayers ve_lavc.c
 *
 *  Copyright (C) Moritz Bunkus - October 2002
 *    UpToDate by Tilmann Bitterberg - July 2003
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

#include "transcode.h"
#include "libtc/libtc.h"
#include "libtc/tcavcodec.h"
#include "filter.h"
#include "avilib/avilib.h"
#include "aud_aux.h"
#include "aclib/imgconvert.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>

#if !defined(INFINITY) && defined(HUGE_VAL)
#define INFINITY HUGE_VAL
#endif


#define MOD_NAME    "export_ffmpeg.so"
#define MOD_VERSION "v0.3.18 (2008-11-29)"
#define MOD_CODEC   "(video) " LIBAVCODEC_IDENT \
                    " | (audio) MPEG/AC3/PCM"

static int verbose_flag    = TC_QUIET;
static int capability_flag = TC_CAP_YUV|TC_CAP_RGB|TC_CAP_PCM|TC_CAP_AC3|
                             TC_CAP_AUD|TC_CAP_YUV422;
#define MOD_PRE ffmpeg
#include "export_def.h"

#include "ffmpeg_cfg.h"


/*************************************************************************
 * libavcodec is not thread-safe. We must protect concurrent access to it.
 * this is visible (without the mutex of course) with
 * transcode .. -x ffmpeg -y ffmpeg -F mpeg4
 */


struct ffmpeg_codec {
    char *name;
    char *fourCC;
    char *comments;
    int   multipass;
};

static struct ffmpeg_codec ffmpeg_codecs[] = {
    {"mpeg4",      "DIVX", "MPEG4 compliant video", 1},
    {"msmpeg4",    "div3", "old DivX3 compatible (aka MSMPEG4v3)", 1},
    {"msmpeg4v2",  "MP42", "old DivX3 compatible (older version)", 1},
    {"mjpeg",      "MJPG", "Motion JPEG", 0},
    {"ljpeg",      "LJPG", "Lossless JPEG", 0},
    {"mpeg1video", "mpg1", "MPEG1 compliant video", 1},
    {"mpeg2video", "mpg2", "MPEG2 compliant video", 1},
    {"h263",       "h263", "H263", 0},
    {"h263p",      "h263", "H263 plus", 1},
    {"h264",       "h264", "H264 (avc)", 1},
    {"wmv1",       "WMV1", "Windows Media Video v1", 1},
    {"wmv2",       "WMV2", "Windows Media Video v2", 1},
    {"rv10",       "RV10", "old RealVideo codec", 1},
    {"huffyuv",    "HFYU", "Lossless HUFFYUV codec", 1},
    {"dvvideo",    "DVSD", "Digital Video", 0},
    {"ffv1",       "FFV1", "FF Video Codec 1 (an experimental lossless codec)", 0},
    {"asv1",       "ASV1", "ASUS V1 codec", 0},
    {"asv2",       "ASV2", "ASUS V2 codec", 0},
    {NULL,         NULL,   NULL, 0}
};

typedef enum /* do not edit without changing *_name and *_rate */
{
    pc_none,
    pc_vcd,
    pc_svcd,
    pc_xvcd,
    pc_dvd
} pseudo_codec_t;

typedef enum /* do not edit without changing *_name and *_rate */
{
    vt_none = 0,
    vt_pal,
    vt_ntsc
} video_template_t;

static pseudo_codec_t       pseudo_codec = pc_none;
static video_template_t     video_template = vt_none;
static char                *real_codec = 0;
static const char          *pseudo_codec_name[] = { "none", "vcd", "svcd", "xvcd", "dvd" };
static const int            pseudo_codec_rate[] = { 0, 44100, 44100, -1, 48000 };
static const char          *vt_name[] = { "general", "pal/secam", "ntsc" };
static const char          *il_name[] = { "off", "top-first", "bottom-first", "unknown" };

static uint8_t             *enc_buffer = NULL;
static uint8_t             *img_buffer = NULL;
static AVFrame             *lavc_convert_frame = NULL;

static AVCodec             *lavc_venc_codec = NULL;
static AVFrame             *lavc_venc_frame = NULL;
static AVCodecContext      *lavc_venc_context;
static avi_t               *avifile = NULL;
static int                  pix_fmt;
static FILE                *stats_file = NULL;
static size_t               size;
static int                  encoded_frames = 0;
static int                  frames = 0;
static struct ffmpeg_codec *codec;
static int                  is_mpegvideo = 0;
static int                  is_huffyuv = 0;
static int                  is_mjpeg = 0;
static FILE                *mpeg1fd = NULL;
static int                  interlacing_active = 0;
static int                  interlacing_top_first = 0;
/* We can't declare lavc_param_psnr static so save it to this variable */
static int                  do_psnr = 0;


static struct ffmpeg_codec *find_ffmpeg_codec(char *name)
{
    int i = 0;
    for (i = 0; ffmpeg_codecs[i].name != NULL; i++) {
        if (!strcasecmp(name, ffmpeg_codecs[i].name))
            return &ffmpeg_codecs[i];
    }
    return NULL;
}

/* second step name mangling */
static const char *ffmpeg_codec_name(const char *tc_name)
{
#if LIBAVCODEC_VERSION_INT  >= ((51<<16)+(44<<8)+0)
    if (!strcmp(tc_name, "h264")) {
        return "libx264";
    }
#endif
    return tc_name;
}

static double psnr(double d) {
    if (d == 0)
        return INFINITY;
    return -10.0 * log(d) / log(10);
}


// Could be using GNU extension 'strchrnul' instead:
static char *tc_strchrnul(const char *s, int c) {
	char *tmp = strchr(s, c);
	if (tmp == NULL) {
		tmp = s + strlen(s);
	}
	return tmp;
}


/* START: COPIED FROM ffmpeg-0.5_p22846(ffmpeg.c, cmdutils.c) */
#include <libavcodec/opt.h>
#include <libavutil/avstring.h>
#include <libswscale/swscale.h>

/* GLUE: */
#define FFMPEG_DATADIR lavc_param_ffmpeg_datadir

/* GLUE: */
static AVCodecContext *avcodec_opts[AVMEDIA_TYPE_NB] = {NULL};

static // GLUE
const char **opt_names;
static int opt_name_count;

static char  *audio_codec_name = NULL;
static char *subtitle_codec_name = NULL;
static char *video_codec_name = NULL;
static int audio_stream_copy = 0;
static int video_stream_copy = 0;
static int subtitle_stream_copy = 0;

static int av_exit(int ret)
{
    av_free(opt_names);

    av_free(video_codec_name);
    av_free(audio_codec_name);
    av_free(subtitle_codec_name);

    exit(ret); /* not all OS-es handle main() return value */
    return ret;
}

static void opt_codec(int *pstream_copy, char **pcodec_name,
                      int codec_type, const char *arg)
{
    av_freep(pcodec_name);
    if (!strcmp(arg, "copy")) {
        *pstream_copy = 1;
    } else {
        *pcodec_name = av_strdup(arg);
    }
}

static void opt_audio_codec(const char *arg)
{
    opt_codec(&audio_stream_copy, &audio_codec_name, AVMEDIA_TYPE_AUDIO, arg);
}

static void opt_video_codec(const char *arg)
{
    opt_codec(&video_stream_copy, &video_codec_name, AVMEDIA_TYPE_VIDEO, arg);
}

static void opt_subtitle_codec(const char *arg)
{
    opt_codec(&subtitle_stream_copy, &subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, arg);
}

static
int opt_default(const char *opt, const char *arg){
    int type;
    int ret= 0;
    const AVOption *o= NULL;
    int opt_types[]={AV_OPT_FLAG_VIDEO_PARAM, AV_OPT_FLAG_AUDIO_PARAM, 0, AV_OPT_FLAG_SUBTITLE_PARAM, 0};

    for(type=0; type<AVMEDIA_TYPE_NB && ret>= 0; type++){
		/* GLUE: +if */
		if (type == AVMEDIA_TYPE_VIDEO) {
        const AVOption *o2 = av_find_opt(avcodec_opts[0], opt, NULL, opt_types[type], opt_types[type]);
        if(o2)
            ret = av_set_string3(avcodec_opts[type], opt, arg, 1, &o);
		/* GLUE: +if */
		}
    }
    /* GLUE: disabling
    if(!o)
        ret = av_set_string3(avformat_opts, opt, arg, 1, &o);
    if(!o && sws_opts)
        ret = av_set_string3(sws_opts, opt, arg, 1, &o);
	*/
    if(!o){
		/* GLUE: disabling
        if(opt[0] == 'a')
            ret = av_set_string3(avcodec_opts[AVMEDIA_TYPE_AUDIO], opt+1, arg, 1, &o);
        else */ if(opt[0] == 'v')
            ret = av_set_string3(avcodec_opts[AVMEDIA_TYPE_VIDEO], opt+1, arg, 1, &o);
		/* GLUE: disabling
        else if(opt[0] == 's')
            ret = av_set_string3(avcodec_opts[AVMEDIA_TYPE_SUBTITLE], opt+1, arg, 1, &o);
		*/
    }
    if (o && ret < 0) {
        fprintf(stderr, "Invalid value '%s' for option '%s'\n", arg, opt);
        exit(1);
    }
    if (!o) {
        fprintf(stderr, "Unrecognized option '%s'\n", opt);
        exit(1);
    }

//    av_log(NULL, AV_LOG_ERROR, "%s:%s: %f 0x%0X\n", opt, arg, av_get_double(avcodec_opts, opt, NULL), (int)av_get_int(avcodec_opts, opt, NULL));

    //FIXME we should always use avcodec_opts, ... for storing options so there will not be any need to keep track of what i set over this
    opt_names= av_realloc(opt_names, sizeof(void*)*(opt_name_count+1));
    opt_names[opt_name_count++]= o->name;

	/* GLUE: disabling
    if(avcodec_opts[0]->debug || avformat_opts->debug)
        av_log_set_level(AV_LOG_DEBUG);
	*/
    return 0;
}

static int opt_preset(const char *opt, const char *arg)
{
    FILE *f=NULL;
    char filename[1000], tmp[1000], tmp2[1000], line[1000];
    int i;
    const char *base[2]= { getenv("HOME"),
                           FFMPEG_DATADIR,
                         };

    if (*opt != 'f') {
        for(i=!base[0]; i<2 && !f; i++){
            snprintf(filename, sizeof(filename), "%s%s/%s.ffpreset", base[i], i ? "" : "/.ffmpeg", arg);
            f= fopen(filename, "r");
            if(!f){
                char *codec_name= *opt == 'v' ? video_codec_name :
                                  *opt == 'a' ? audio_codec_name :
                                                subtitle_codec_name;
                snprintf(filename, sizeof(filename), "%s%s/%s-%s.ffpreset", base[i],  i ? "" : "/.ffmpeg", codec_name, arg);
                f= fopen(filename, "r");
            }
        }
    } else {
        av_strlcpy(filename, arg, sizeof(filename));
        f= fopen(filename, "r");
    }

    if(!f){
        fprintf(stderr, "File for preset '%s' not found\n", arg);
        av_exit(1);
    }

    while(!feof(f)){
        int e= fscanf(f, "%999[^\n]\n", line) - 1;
        if(line[0] == '#' && !e)
            continue;
        e|= sscanf(line, "%999[^=]=%999[^\n]\n", tmp, tmp2) - 2;
        if(e){
            fprintf(stderr, "%s: Invalid syntax: '%s'\n", filename, line);
            av_exit(1);
        }
        if(!strcmp(tmp, "acodec")){
            opt_audio_codec(tmp2);
        }else if(!strcmp(tmp, "vcodec")){
            opt_video_codec(tmp2);
        }else if(!strcmp(tmp, "scodec")){
            opt_subtitle_codec(tmp2);
        }else if(opt_default(tmp, tmp2) < 0){
            fprintf(stderr, "%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n", filename, line, tmp, tmp2);
            av_exit(1);
        }
    }

    fclose(f);

    return 0;
}
/* END: COPIED FROM ffmpeg-0.5_p22846(ffmpeg.c, cmdutils.c) */


/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
    char *user_codec_string = NULL;

    if (param->flag == TC_VIDEO) {
        size_t fsize = 0;
        char *p = NULL;
        int i = 0, ret = 0;
        /* Check if the user used '-F codecname' and abort if not. */

        if (vob->ex_v_fcc) {
            user_codec_string = tc_strdup(vob->ex_v_fcc);
            tc_strstrip(user_codec_string);
        }

        if (!user_codec_string || !strlen(user_codec_string)) {
            tc_log_info(MOD_NAME, "You must chose a codec by supplying '-F "
                        "<codecname>'. A list of supported codecs can be obtained with "
                        "'transcode -y ffmpeg -F list'.");

            return TC_EXPORT_ERROR;
        }

        if (!strcasecmp(user_codec_string, "list")) {
        	tc_log_info(MOD_NAME, "List of known and supported codecs:");
	        tc_log_info(MOD_NAME, " Name       fourCC multipass comments");
        	tc_log_info(MOD_NAME, " ---------- ------ --------- "
	                		      "-----------------------------------");
            for (i = 0; ffmpeg_codecs[i].name != NULL; i++) {
                tc_log_info(MOD_NAME, " %-10s  %s     %3s    %s",
                            ffmpeg_codecs[i].name, ffmpeg_codecs[i].fourCC,
                            ffmpeg_codecs[i].multipass ? "yes" : "no",
                            ffmpeg_codecs[i].comments);
            }
            return TC_EXPORT_ERROR;
        }

        if (!strcmp(user_codec_string, "mpeg1"))
            real_codec = tc_strdup("mpeg1video");
        else if (!strcmp(user_codec_string, "mpeg2"))
            real_codec = tc_strdup("mpeg2video");
        else if (!strcmp(user_codec_string, "dv"))
            real_codec = tc_strdup("dvvideo");
        else
            real_codec = tc_strdup(user_codec_string);

        if (!strcmp(user_codec_string, "huffyuv"))
            is_huffyuv = 1;

        if (!strcmp(user_codec_string, "mjpeg")
         || !strcmp(user_codec_string, "ljpeg")) {
            int handle;

            is_mjpeg = 1;

            tc_log_info(MOD_NAME, "output is mjpeg or ljpeg, extending range from "
		                "YUV420P to YUVJ420P (full range)");

            handle = tc_filter_add("levels", "input=16-240");
            if (!handle)
                tc_log_warn(MOD_NAME, "cannot load levels filter");
        }

        tc_free(user_codec_string);
        user_codec_string = NULL;

        p = strrchr(real_codec, '-');
        if (p) { /* chop off -ntsc/-pal and set type */
            *p++ = 0;

            if (!strcmp(p, "ntsc")) {
                video_template = vt_ntsc;
            } else if (!strcmp(p, "pal")) {
                video_template = vt_pal;
            } else {
	    	    tc_log_warn(MOD_NAME, "Video template standard must be one of pal/ntsc");
    	    	return(TC_EXPORT_ERROR);
	        }
        }

    if (!strcmp(real_codec, "vcd")) {
        tc_free(real_codec);
        real_codec = tc_strdup("mpeg1video");
        pseudo_codec = pc_vcd;
    } else if (!strcmp(real_codec, "svcd")) {
        tc_free(real_codec);
        real_codec = tc_strdup("mpeg2video");
        pseudo_codec = pc_svcd;
    } else if(!strcmp(real_codec, "xvcd")) {
        tc_free(real_codec);
        real_codec = tc_strdup("mpeg2video");
        pseudo_codec = pc_xvcd;
    } else if(!strcmp(real_codec, "dvd")) {
        tc_free(real_codec);
        real_codec = tc_strdup("mpeg2video");
        pseudo_codec = pc_dvd;
    }

    if (!strcmp(real_codec, "mpeg1video"))
        is_mpegvideo = 1;

    if (!strcmp(real_codec, "mpeg2video"))
        is_mpegvideo = 2;

    codec = find_ffmpeg_codec(real_codec);
    if (codec == NULL) {
        tc_log_warn(MOD_NAME, "Unknown codec '%s'.", real_codec);
        return TC_EXPORT_ERROR;
    }

    TC_LOCK_LIBAVCODEC;
    avcodec_init();
    avcodec_register_all();
    TC_UNLOCK_LIBAVCODEC;

    /* -- get it -- */
    lavc_venc_codec = avcodec_find_encoder_by_name(ffmpeg_codec_name(codec->name));
    if (!lavc_venc_codec) {
        tc_log_warn(MOD_NAME, "Could not find a FFMPEG codec for '%s'.",
                codec->name);
        return TC_EXPORT_ERROR;
    }
    if (verbose) {
	    tc_log_info(MOD_NAME, "Using FFMPEG codec '%s' (FourCC '%s', %s).",
		            codec->name, codec->fourCC, codec->comments);
    }

    lavc_venc_context = avcodec_alloc_context();
    lavc_venc_frame   = avcodec_alloc_frame();

    lavc_convert_frame= avcodec_alloc_frame();
    size = avpicture_get_size(PIX_FMT_RGB24, vob->ex_v_width, vob->ex_v_height);
    enc_buffer = tc_malloc(size);

    if (lavc_venc_context == NULL || !enc_buffer || !lavc_convert_frame) {
        tc_log_error(MOD_NAME, "Could not allocate enough memory.");
        return TC_EXPORT_ERROR;
    }

    pix_fmt = vob->im_v_codec;

    if (! (pix_fmt == CODEC_RGB || pix_fmt == CODEC_YUV || pix_fmt == CODEC_YUV422)) {
        tc_log_warn(MOD_NAME, "Unknown color space %d.", pix_fmt);
        return TC_EXPORT_ERROR;
    }
    if (pix_fmt == CODEC_RGB || pix_fmt == CODEC_YUV422 || is_huffyuv) {
        img_buffer = tc_malloc(size);
        if (!img_buffer) {
            tc_log_error(MOD_NAME, "conversion buffer allocation failed.");
            return TC_EXPORT_ERROR;
        }
    }

    lavc_venc_context->width              = vob->ex_v_width;
    lavc_venc_context->height             = vob->ex_v_height;
    lavc_venc_context->qmin               = vob->min_quantizer;
    lavc_venc_context->qmax               = vob->max_quantizer;

    if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_GOP)
        lavc_venc_context->gop_size = vob->divxkeyframes;
    else if (is_mpegvideo)
        lavc_venc_context->gop_size = 15; /* conservative default for mpeg1/2 svcd/dvd */
    else
        lavc_venc_context->gop_size = 250; /* reasonable default for mpeg4 (and others) */

    if (pseudo_codec != pc_none) { /* using profiles */
    	if (verbose) {
	        tc_log_info(MOD_NAME,
		            	"Selected %s profile, %s video type for video",
            			pseudo_codec_name[pseudo_codec],
			            vt_name[video_template]);
        }

        if (!(vob->export_attributes & TC_EXPORT_ATTRIBUTE_FIELDS)) {
            if (video_template == vt_pal) {
                vob->encode_fields = TC_ENCODE_FIELDS_TOP_FIRST;
            } else if (video_template == vt_ntsc) {
                vob->encode_fields = TC_ENCODE_FIELDS_BOTTOM_FIRST;
            } else {
                tc_log_warn(MOD_NAME, "Interlacing parameters unknown, "
                                      "select video type with profile");
                vob->encode_fields = TC_ENCODE_FIELDS_UNKNOWN;
            }

    	    if (verbose) {
	        	tc_log_info(MOD_NAME, "Set interlacing to %s",
			                il_name[vob->encode_fields]);
    	    }
        }

        if (!(vob->export_attributes & TC_EXPORT_ATTRIBUTE_FRC)) {
            if (video_template == vt_pal)
                vob->ex_frc = 3;
            else if (video_template == vt_ntsc)
                vob->ex_frc = 4;
            else
                vob->ex_frc = 0; /* unknown */
	    }

	    if (verbose) {
    		tc_log_info(MOD_NAME, "Set frame rate to %s",
	    	    	    vob->ex_frc == 3 ? "25" :
		    	        vob->ex_frc == 4 ? "29.97" : "unknown");
	    }
    }

    switch(pseudo_codec) {
    case(pc_vcd):
        if (vob->ex_v_width != 352)
            tc_log_warn(MOD_NAME, "X resolution is not 352 as required");

        if (vob->ex_v_height != 240 && vob->ex_v_height != 288)
            tc_log_warn(MOD_NAME, "Y resolution is not 240 or 288 as required");

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_VBITRATE) {
            if (vob->divxbitrate != 1150)
                tc_log_warn(MOD_NAME, "Video bitrate not 1150 kbps as required");
        } else {
            vob->divxbitrate = 1150;
	    if (verbose) {
		tc_log_info(MOD_NAME, "Set video bitrate to 1150");
	    }
        }

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_GOP) {
            if(vob->divxkeyframes > 9)
                tc_log_warn(MOD_NAME, "GOP size not < 10 as required");
        } else {
            vob->divxkeyframes = 9;
	    if (verbose) {
		tc_log_info(MOD_NAME, "Set GOP size to 9");
	    }
        }

        lavc_venc_context->gop_size = vob->divxkeyframes;
        lavc_param_rc_min_rate = 1150;
        lavc_param_rc_max_rate = 1150;
        lavc_param_rc_buffer_size = 40 * 8;
        lavc_param_rc_buffer_aggressivity = 99;
        lavc_param_scan_offset = 0;

        break;

    case(pc_svcd):
        if (vob->ex_v_width != 480)
            tc_log_warn(MOD_NAME, "X resolution is not 480 as required");

        if (vob->ex_v_height != 480 && vob->ex_v_height != 576)
            tc_log_warn(MOD_NAME, "Y resolution is not 480 or 576 as required");

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_VBITRATE) {
            if(vob->divxbitrate != 2040)
                tc_log_warn(MOD_NAME, "Video bitrate not 2040 kbps as required");
        } else {
            vob->divxbitrate = 2040;
            tc_log_warn(MOD_NAME, "Set video bitrate to 2040");
        }

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_GOP) {
            if (vob->divxkeyframes > 18)
                tc_log_warn(MOD_NAME, "GOP size not < 19 as required");
        } else {
            if (video_template == vt_ntsc)
                  vob->divxkeyframes = 18;
            else
                  vob->divxkeyframes = 15;

            tc_log_warn(MOD_NAME, "Set GOP size to %d", vob->divxkeyframes);
        }

        lavc_venc_context->gop_size = vob->divxkeyframes;
        lavc_param_rc_min_rate= 0;
        lavc_param_rc_max_rate = 2516;
        lavc_param_rc_buffer_size = 224 * 8;
        lavc_param_rc_buffer_aggressivity = 99;
        lavc_param_scan_offset = CODEC_FLAG_SVCD_SCAN_OFFSET;

        break;

    case(pc_xvcd):
        if (vob->ex_v_width != 480)
            tc_log_warn(MOD_NAME, "X resolution is not 480 as required");

        if (vob->ex_v_height != 480 && vob->ex_v_height != 576)
            tc_log_warn(MOD_NAME, "Y resolution is not 480 or 576 as required");

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_VBITRATE) {
            if (vob->divxbitrate < 1000 || vob->divxbitrate > 9000)
                tc_log_warn(MOD_NAME, "Video bitrate not between 1000 and 9000 kbps as required");
        } else {
            vob->divxbitrate = 2040;
            tc_log_warn(MOD_NAME, "Set video bitrate to 2040");
        }

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_GOP) {
            if (vob->divxkeyframes > 18)
                tc_log_warn(MOD_NAME, "GOP size not < 19 as required");
        } else {
            if (video_template == vt_ntsc)
                vob->divxkeyframes = 18;
            else
                vob->divxkeyframes = 15;

            tc_log_warn(MOD_NAME, "Set GOP size to %d", vob->divxkeyframes);
        }

        lavc_venc_context->gop_size = vob->divxkeyframes;
        lavc_param_rc_min_rate = 0;
        if (vob->video_max_bitrate != 0)
            lavc_param_rc_max_rate = vob->video_max_bitrate;
        else
            lavc_param_rc_max_rate = 5000;

        lavc_param_rc_buffer_size = 224 * 8;
        lavc_param_rc_buffer_aggressivity = 99;
        lavc_param_scan_offset = CODEC_FLAG_SVCD_SCAN_OFFSET;

        break;

    case(pc_dvd):
        if (vob->ex_v_width != 720 && vob->ex_v_width != 704 && vob->ex_v_width != 352)
            tc_log_warn(MOD_NAME, "X resolution is not 720, 704 or 352 as required");

        if (vob->ex_v_height != 576 && vob->ex_v_height != 480 && vob->ex_v_height != 288 && vob->ex_v_height != 240)
            tc_log_warn(MOD_NAME, "Y resolution is not 576, 480, 288 or 240 as required");

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_VBITRATE) {
            if(vob->divxbitrate < 1000 || vob->divxbitrate > 9800)
                tc_log_warn(MOD_NAME, "Video bitrate not between 1000 and 9800 kbps as required");
        } else {
            vob->divxbitrate = 5000;
	    if (verbose) {
		tc_log_info(MOD_NAME, "Set video bitrate to 5000");
	    }
        }

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_GOP) {
            if (vob->divxkeyframes > 18)
                tc_log_warn(MOD_NAME, "GOP size not < 19 as required");
        } else {
            if (video_template == vt_ntsc)
                vob->divxkeyframes = 18;
            else
                vob->divxkeyframes = 15;

	    if (verbose) {
		tc_log_info(MOD_NAME, "Set GOP size to %d",
			    vob->divxkeyframes);
	    }
        }

        lavc_venc_context->gop_size = vob->divxkeyframes;
        lavc_param_rc_min_rate = 0;
        lavc_param_rc_max_rate = 6000;  /*FIXME: ffmpeg exceeds maxrate in 2-pass*/
        lavc_param_rc_buffer_size = 224 * 8;
        lavc_param_rc_buffer_aggressivity = 99;

        break;

    case(pc_none): /* leave everything alone, prevent gcc warning */
	if (verbose) {
	    tc_log_info(MOD_NAME, "No profile selected");
	}

        break;
    }

    switch (vob->ex_frc) {
    case 1: /* 23.976 */
        lavc_venc_context->time_base.den = 24000;
        lavc_venc_context->time_base.num = 1001;
        break;
    case 2: /* 24.000 */
        lavc_venc_context->time_base.den = 24000;
        lavc_venc_context->time_base.num = 1000;
        break;
    case 3: /* 25.000 */
        lavc_venc_context->time_base.den = 25000;
        lavc_venc_context->time_base.num = 1000;
        break;
    case 4: /* 29.970 */
        lavc_venc_context->time_base.den = 30000;
        lavc_venc_context->time_base.num = 1001;
        break;
    case 5: /* 30.000 */
        lavc_venc_context->time_base.den = 30000;
        lavc_venc_context->time_base.num = 1000;
        break;
    case 6: /* 50.000 */
        lavc_venc_context->time_base.den = 50000;
        lavc_venc_context->time_base.num = 1000;
        break;
    case 7: /* 59.940 */
        lavc_venc_context->time_base.den = 60000;
        lavc_venc_context->time_base.num = 1001;
        break;
    case 8: /* 60.000 */
        lavc_venc_context->time_base.den = 60000;
        lavc_venc_context->time_base.num = 1000;
        break;
    case 0: /* not set */
    default:
        if ((vob->ex_fps > 29) && (vob->ex_fps < 30)) {
            lavc_venc_context->time_base.den = 30000;
            lavc_venc_context->time_base.num = 1001;
        } else {
            lavc_venc_context->time_base.den = (int)(vob->ex_fps * 1000.0);
            lavc_venc_context->time_base.num = 1000;
        }
        break;
    }

    module_read_config("ffmpeg.cfg", codec->name, lavcopts_conf, MOD_NAME);
    if (verbose_flag & TC_DEBUG) {
        tc_log_info(MOD_NAME, "Using the following FFMPEG parameters:");
        module_print_config(lavcopts_conf, MOD_NAME);
    }

    /* this overrides transcode settings */
    if (lavc_param_fps_code > 0) {
        switch (lavc_param_fps_code) {
        case 1: /* 23.976 */
            lavc_venc_context->time_base.den = 24000;
            lavc_venc_context->time_base.num = 1001;
            break;
        case 2: /* 24.000 */
            lavc_venc_context->time_base.den = 24000;
            lavc_venc_context->time_base.num = 1000;
            break;
        case 3: /* 25.000 */
            lavc_venc_context->time_base.den = 25000;
            lavc_venc_context->time_base.num = 1000;
            break;
        case 4: /* 29.970 */
            lavc_venc_context->time_base.den = 30000;
            lavc_venc_context->time_base.num = 1001;
            break;
        case 5: /* 30.000 */
            lavc_venc_context->time_base.den = 30000;
            lavc_venc_context->time_base.num = 1000;
            break;
        case 6: /* 50.000 */
            lavc_venc_context->time_base.den = 50000;
            lavc_venc_context->time_base.num = 1000;
            break;
        case 7: /* 59.940 */
            lavc_venc_context->time_base.den = 60000;
            lavc_venc_context->time_base.num = 1001;
            break;
        case 8: /* 60.000 */
            lavc_venc_context->time_base.den = 60000;
            lavc_venc_context->time_base.num = 1000;
            break;
        case 0: /* not set */
        default:
            /*
             *  lavc_venc_context->time_base.den = (int)(vob->ex_fps * 1000.0);
             *  lavc_venc_context->time_base.num = 1000;
             */
            break;
        }
    }

    /* closedgop requires scene detection to be disabled separately */
    if (lavc_param_closedgop)
        lavc_param_sc_threshold = 1000000000;

    lavc_venc_context->bit_rate           = vob->divxbitrate * 1000;
    lavc_venc_context->bit_rate_tolerance = lavc_param_vrate_tolerance * 1000;
    lavc_venc_context->lmin= (int)(FF_QP2LAMBDA * lavc_param_lmin + 0.5);
    lavc_venc_context->lmax= (int)(FF_QP2LAMBDA * lavc_param_lmax + 0.5);
    lavc_venc_context->max_qdiff          = lavc_param_vqdiff;
    lavc_venc_context->qcompress          = lavc_param_vqcompress;
    lavc_venc_context->qblur              = lavc_param_vqblur;
    lavc_venc_context->max_b_frames       = lavc_param_vmax_b_frames;
    lavc_venc_context->b_quant_factor     = lavc_param_vb_qfactor;
    lavc_venc_context->rc_strategy        = lavc_param_vrc_strategy;
    lavc_venc_context->b_frame_strategy   = lavc_param_vb_strategy;
    lavc_venc_context->b_quant_offset     = lavc_param_vb_qoffset;
    lavc_venc_context->luma_elim_threshold= lavc_param_luma_elim_threshold;
    lavc_venc_context->chroma_elim_threshold= lavc_param_chroma_elim_threshold;
    lavc_venc_context->rtp_payload_size   = lavc_param_packet_size;
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)   
    if (lavc_param_packet_size)
        lavc_venc_context->rtp_mode       = 1;
#endif        
    lavc_venc_context->strict_std_compliance= lavc_param_strict;
    lavc_venc_context->i_quant_factor     = lavc_param_vi_qfactor;
    lavc_venc_context->i_quant_offset     = lavc_param_vi_qoffset;
    lavc_venc_context->rc_qsquish         = lavc_param_rc_qsquish;
    lavc_venc_context->rc_qmod_amp        = lavc_param_rc_qmod_amp;
    lavc_venc_context->rc_qmod_freq       = lavc_param_rc_qmod_freq;
    lavc_venc_context->rc_eq              = lavc_param_rc_eq;
    lavc_venc_context->rc_max_rate        = lavc_param_rc_max_rate * 1000;
    lavc_venc_context->rc_min_rate        = lavc_param_rc_min_rate * 1000;
    lavc_venc_context->rc_buffer_size     = lavc_param_rc_buffer_size * 1024;
    lavc_venc_context->rc_buffer_aggressivity= lavc_param_rc_buffer_aggressivity;
    lavc_venc_context->rc_initial_cplx    = lavc_param_rc_initial_cplx;
    lavc_venc_context->debug              = lavc_param_debug;
    lavc_venc_context->last_predictor_count= lavc_param_last_pred;
    lavc_venc_context->pre_me             = lavc_param_pre_me;
    lavc_venc_context->me_pre_cmp         = lavc_param_me_pre_cmp;
    lavc_venc_context->pre_dia_size       = lavc_param_pre_dia_size;
    lavc_venc_context->me_subpel_quality  = lavc_param_me_subpel_quality;
    lavc_venc_context->me_range           = lavc_param_me_range;
    lavc_venc_context->intra_quant_bias   = lavc_param_ibias;
    lavc_venc_context->inter_quant_bias   = lavc_param_pbias;
    lavc_venc_context->coder_type         = lavc_param_coder;
    lavc_venc_context->context_model      = lavc_param_context;
    lavc_venc_context->scenechange_threshold= lavc_param_sc_threshold;
    lavc_venc_context->noise_reduction    = lavc_param_noise_reduction;
    lavc_venc_context->inter_threshold    = lavc_param_inter_threshold;
    lavc_venc_context->intra_dc_precision = lavc_param_intra_dc_precision;
    lavc_venc_context->skip_top           = lavc_param_skip_top;
    lavc_venc_context->skip_bottom        = lavc_param_skip_bottom;

    if ((lavc_param_threads < 1) || (lavc_param_threads > 7)) {
        tc_log_warn(MOD_NAME, "Thread count out of range (should be [0-7])");
	return(TC_EXPORT_ERROR);
    }

    lavc_venc_context->thread_count = lavc_param_threads;

    if (verbose) {
	tc_log_info(MOD_NAME, "Starting %d thread(s)",
		    lavc_venc_context->thread_count);
    }

    avcodec_thread_init(lavc_venc_context, lavc_param_threads);

    if (lavc_param_intra_matrix) {
        char *tmp;

        lavc_venc_context->intra_matrix =
            malloc(sizeof(*lavc_venc_context->intra_matrix) * 64);

        for (i = 0;
            (tmp = strsep(&lavc_param_intra_matrix, ",")) && (i < 64);
            i++) {
            if (!tmp || (tmp && !strlen(tmp)))
                break;
            lavc_venc_context->intra_matrix[i] = atoi(tmp);
        }

        if (i != 64) {
            free(lavc_venc_context->intra_matrix);
            lavc_venc_context->intra_matrix = NULL;
        } else {
	    if (verbose) {
		tc_log_info(MOD_NAME, "Using user specified intra matrix");
	    }
	}
    }

    if (lavc_param_inter_matrix) {
        char *tmp;

        lavc_venc_context->inter_matrix =
            malloc(sizeof(*lavc_venc_context->inter_matrix) * 64);

        for (i = 0;
            (tmp = strsep(&lavc_param_intra_matrix, ",")) && (i < 64);
            i++) {
            if (!tmp || (tmp && !strlen(tmp)))
                break;
            lavc_venc_context->inter_matrix[i] = atoi(tmp);
        }

        if (i != 64) {
            free(lavc_venc_context->inter_matrix);
            lavc_venc_context->inter_matrix = NULL;
        } else {
	    if (verbose) {
		tc_log_info(MOD_NAME, "Using user specified inter matrix");
	    }
	}
    }

    p = lavc_param_rc_override_string;
    for (i = 0; p; i++) {
        int start, end, q;
        int e = sscanf(p, "%d,%d,%d", &start, &end, &q);

        if (e != 3) {
            tc_log_warn(MOD_NAME, "Error parsing vrc_override.");
            return TC_EXPORT_ERROR;
        }
        lavc_venc_context->rc_override =
            realloc(lavc_venc_context->rc_override, sizeof(RcOverride) * (i + 1));
        lavc_venc_context->rc_override[i].start_frame = start;
        lavc_venc_context->rc_override[i].end_frame   = end;
        if (q > 0) {
            lavc_venc_context->rc_override[i].qscale         = q;
            lavc_venc_context->rc_override[i].quality_factor = 1.0;
        } else {
            lavc_venc_context->rc_override[i].qscale         = 0;
            lavc_venc_context->rc_override[i].quality_factor = -q / 100.0;
        }
        p = strchr(p, '/');
        if (p)
            p++;
    }
    lavc_venc_context->rc_override_count     = i;
    lavc_venc_context->mpeg_quant            = lavc_param_mpeg_quant;
    lavc_venc_context->dct_algo              = lavc_param_fdct;
    lavc_venc_context->idct_algo             = lavc_param_idct;
    lavc_venc_context->lumi_masking          = lavc_param_lumi_masking;
    lavc_venc_context->temporal_cplx_masking = lavc_param_temporal_cplx_masking;
    lavc_venc_context->spatial_cplx_masking  = lavc_param_spatial_cplx_masking;
    lavc_venc_context->p_masking             = lavc_param_p_masking;
    lavc_venc_context->dark_masking          = lavc_param_dark_masking;

    if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_PAR) { /* export_par explicitely set by user */
        if (vob->ex_par > 0) {
            switch(vob->ex_par) {
            case 1:
                lavc_venc_context->sample_aspect_ratio.num = 1;
                lavc_venc_context->sample_aspect_ratio.den = 1;
                break;
            case 2:
                lavc_venc_context->sample_aspect_ratio.num = 1200;
                lavc_venc_context->sample_aspect_ratio.den = 1100;
                break;
            case 3:
                lavc_venc_context->sample_aspect_ratio.num = 1000;
                lavc_venc_context->sample_aspect_ratio.den = 1100;
                break;
            case 4:
                lavc_venc_context->sample_aspect_ratio.num = 1600;
                lavc_venc_context->sample_aspect_ratio.den = 1100;
                break;
            case 5:
                lavc_venc_context->sample_aspect_ratio.num = 4000;
                lavc_venc_context->sample_aspect_ratio.den = 3300;
                break;
            default:
                tc_log_warn(MOD_NAME, "Parameter value for --export_par out of range (allowed: [1-5])");
		return(TC_EXPORT_ERROR);
            }
        } else {
            if (vob->ex_par_width > 0 && vob->ex_par_height > 0) {
                lavc_venc_context->sample_aspect_ratio.num = vob->ex_par_width;
                lavc_venc_context->sample_aspect_ratio.den = vob->ex_par_height;
            } else {
                tc_log_warn(MOD_NAME, "Parameter values for --export_par parameter out of range (allowed: [>0]/[>0])");
                lavc_venc_context->sample_aspect_ratio.num = 1;
                lavc_venc_context->sample_aspect_ratio.den = 1;
            }
        }
    } else {
        double dar, sar;

        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_ASR) { /* export_asr explicitely set by user */
            if (vob->ex_asr > 0) {
                switch(vob->ex_asr) {
                case 1: dar = 1.0; break;
                case 2: dar = 4.0/3.0; break;
                case 3: dar = 16.0/9.0; break;
                case 4: dar = 221.0/100.0; break;
                default:
                    tc_log_warn(MOD_NAME, "Parameter value to --export_asr out of range (allowed: [1-4])");
		    return(TC_EXPORT_ERROR);
                }

                sar = dar * ((double)vob->ex_v_height / (double)vob->ex_v_width);
		if (verbose) {
		    tc_log_info(MOD_NAME, "Display aspect ratio calculated"
				" as %f", dar);
		    tc_log_info(MOD_NAME, "Sample aspect ratio calculated"
				" as %f", sar);
		}
                lavc_venc_context->sample_aspect_ratio.num = (int)(sar * 1000);
                lavc_venc_context->sample_aspect_ratio.den = 1000;
            } else {
                tc_log_warn(MOD_NAME, "Parameter value to --export_asr out of range (allowed: [1-4])");
		return(TC_EXPORT_ERROR);
	    }
        } else { /* user did not specify asr at all, assume no change */
	    if (verbose) {
		tc_log_info(MOD_NAME, "Set display aspect ratio to input");
	    }
            /*
             * sar = (4.0 * ((double)vob->ex_v_height) / (3.0 * (double)vob->ex_v_width));
             * lavc_venc_context->sample_aspect_ratio.num = (int)(sar * 1000);
             * lavc_venc_context->sample_aspect_ratio.den = 1000;
             */
            lavc_venc_context->sample_aspect_ratio.num = 1;
            lavc_venc_context->sample_aspect_ratio.den = 1;
        }
    }

    lavc_venc_context->flags = 0;

    if (lavc_param_mb_decision)
        lavc_venc_context->mb_decision= lavc_param_mb_decision;

    lavc_venc_context->me_cmp     = lavc_param_me_cmp;
    lavc_venc_context->me_sub_cmp = lavc_param_me_sub_cmp;
    lavc_venc_context->mb_cmp     = lavc_param_mb_cmp;
    lavc_venc_context->ildct_cmp  = lavc_param_ildct_cmp;
    lavc_venc_context->dia_size   = lavc_param_dia_size;
    lavc_venc_context->flags |= lavc_param_qpel;
    lavc_venc_context->flags |= lavc_param_gmc;
    lavc_venc_context->flags |= lavc_param_closedgop;
    lavc_venc_context->flags |= lavc_param_trunc;
    lavc_venc_context->flags |= lavc_param_aic;
    lavc_venc_context->flags |= lavc_param_umv;
    lavc_venc_context->flags |= lavc_param_v4mv;
    lavc_venc_context->flags |= lavc_param_data_partitioning;
    lavc_venc_context->flags |= lavc_param_cbp;
    lavc_venc_context->flags |= lavc_param_mv0;
    lavc_venc_context->flags |= lavc_param_qp_rd;
    lavc_venc_context->flags |= lavc_param_scan_offset;
    lavc_venc_context->flags |= lavc_param_ss;
    lavc_venc_context->flags |= lavc_param_alt;
    lavc_venc_context->flags |= lavc_param_ilme;
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)   
    lavc_venc_context->flags |= lavc_param_trell;
#else
    lavc_venc_context->trellis = lavc_param_trell;
#endif

    if (lavc_param_gray)
        lavc_venc_context->flags |= CODEC_FLAG_GRAY;
    if (lavc_param_normalize_aqp)
        lavc_venc_context->flags |= CODEC_FLAG_NORMALIZE_AQP;

    switch(vob->encode_fields) {
    case TC_ENCODE_FIELDS_TOP_FIRST:
        interlacing_active = 1;
        interlacing_top_first = 1;
        break;
    case TC_ENCODE_FIELDS_BOTTOM_FIRST:
        interlacing_active = 1;
        interlacing_top_first = 0;
        break;
    default: /* progressive / unknown */
        interlacing_active = 0;
        interlacing_top_first = 0;
        break;
    }

    lavc_venc_context->flags |= interlacing_active ?
        CODEC_FLAG_INTERLACED_DCT : 0;
    lavc_venc_context->flags |= interlacing_active ?
        CODEC_FLAG_INTERLACED_ME : 0;

    lavc_venc_context->flags |= lavc_param_psnr;
    do_psnr = lavc_param_psnr;

    lavc_venc_context->prediction_method = lavc_param_prediction_method;

    if(is_huffyuv)
        lavc_venc_context->pix_fmt = PIX_FMT_YUV422P;
    else
    {
        switch(pix_fmt)
        {
            case CODEC_YUV:
            case CODEC_RGB:
            {
                if(is_mjpeg)
                    lavc_venc_context->pix_fmt = PIX_FMT_YUVJ420P;
                else
                    lavc_venc_context->pix_fmt = PIX_FMT_YUV420P;
                break;
            }

            case CODEC_YUV422:
            {
                if(is_mjpeg)
                    lavc_venc_context->pix_fmt = PIX_FMT_YUVJ422P;
                else
                    lavc_venc_context->pix_fmt = PIX_FMT_YUV422P;
                break;
            }

            default:
            {
                tc_log_warn(MOD_NAME, "Unknown pixel format %d.", pix_fmt);
                return TC_EXPORT_ERROR;
            }
        }
    }

    switch (vob->divxmultipass) {
      case 1:
        if (!codec->multipass) {
          tc_log_warn(MOD_NAME, "This codec does not support multipass "
                  "encoding.");
          return TC_EXPORT_ERROR;
        }
        lavc_venc_context->flags |= CODEC_FLAG_PASS1;
        stats_file = fopen(vob->divxlogfile, "w");
        if (stats_file == NULL){
          tc_log_warn(MOD_NAME, "Could not create 2pass log file \"%s\".",
                  vob->divxlogfile);
          return TC_EXPORT_ERROR;
        }
        break;
      case 2:
        if (!codec->multipass) {
          tc_log_warn(MOD_NAME, "This codec does not support multipass "
                  "encoding.");
          return TC_EXPORT_ERROR;
        }
        lavc_venc_context->flags |= CODEC_FLAG_PASS2;
        stats_file= fopen(vob->divxlogfile, "r");
        if (stats_file==NULL){
          tc_log_warn(MOD_NAME, "Could not open 2pass log file \"%s\" for "
                  "reading.", vob->divxlogfile);
          return TC_EXPORT_ERROR;
        }
        fseek(stats_file, 0, SEEK_END);
        fsize = ftell(stats_file);
        fseek(stats_file, 0, SEEK_SET);

    // count the lines of the file to not encode to much
    {
        char lbuf[255];
        while (fgets (lbuf, 255, stats_file))
        encoded_frames++;
    }

        fseek(stats_file, 0, SEEK_SET);

        lavc_venc_context->stats_in= malloc(fsize + 1);
        lavc_venc_context->stats_in[fsize] = 0;

        if (fread(lavc_venc_context->stats_in, fsize, 1, stats_file) < 1){
          tc_log_warn(MOD_NAME, "Could not read the complete 2pass log file "
                  "\"%s\".", vob->divxlogfile);
          return TC_EXPORT_ERROR;
        }
        break;
      case 3:
        /* fixed qscale :p */
        lavc_venc_context->flags   |= CODEC_FLAG_QSCALE;
        lavc_venc_frame->quality  = vob->divxbitrate;
        break;
    }

    lavc_venc_context->me_method = ME_ZERO + lavc_param_vme;


	/* FIXME: transcode itself contains "broken ffmpeg default settings", thus we need to override them! */
	if (lavc_param_video_preset) {
		avcodec_opts[AVMEDIA_TYPE_VIDEO] = lavc_venc_context;
		video_codec_name = ffmpeg_codec_name(codec->name);

		const char *preset_start = lavc_param_video_preset;
		while (preset_start) {
			const char *preset_end = tc_strchrnul(preset_start, ',');
			char preset_name[255] = {'\0'};

			if (strncpy(preset_name, preset_start, preset_end-preset_start) != preset_name) {
				tc_log_warn(MOD_NAME, "Extracting preset name failed");
				return TC_EXPORT_ERROR;
			}

			if (verbose) {
				tc_log_info(MOD_NAME, "Parsing ffmpeg preset '%s'", preset_name);
			}
			if (opt_preset("vpre", preset_name) != 0) {
				tc_log_warn(MOD_NAME, "Parsing ffmpeg preset '%s' failed", preset_name);
			}
			if (verbose) {
				int i;
				tc_log_info(MOD_NAME, "After parsing preset '%s', %i options are overridden:", preset_name, opt_name_count);
				for (i=0; i < opt_name_count; i++)
					tc_log_info(MOD_NAME, "-- %s", opt_names[i]);
			}

			if (*preset_end != '\0') {
				preset_start = preset_end+1;
			}
			else {
				preset_start = NULL;
			}
		}
	}


    //-- open codec --
    //----------------
    TC_LOCK_LIBAVCODEC;
    ret = avcodec_open(lavc_venc_context, lavc_venc_codec);
    TC_UNLOCK_LIBAVCODEC;
    if (ret < 0) {
      tc_log_warn(MOD_NAME, "could not open FFMPEG codec");
      return TC_EXPORT_ERROR;
    }

    if (lavc_venc_context->codec->encode == NULL) {
      tc_log_warn(MOD_NAME, "could not open FFMPEG codec "
              "(lavc_venc_context->codec->encode == NULL)");
      return TC_EXPORT_ERROR;
    }

    /* free second pass buffer, its not needed anymore */
    if (lavc_venc_context->stats_in)
      free(lavc_venc_context->stats_in);
    lavc_venc_context->stats_in = NULL;

    if (verbose_flag & TC_DEBUG) {
     //-- GMO start --
      if (vob->divxmultipass == 3) {
        tc_log_info(MOD_NAME, "    single-pass session: 3 (VBR)");
        tc_log_info(MOD_NAME, "          VBR-quantizer: %d",
                vob->divxbitrate);
      } else {
        tc_log_info(MOD_NAME, "     multi-pass session: %d",
                vob->divxmultipass);
        tc_log_info(MOD_NAME, "      bitrate [kBits/s]: %d",
                lavc_venc_context->bit_rate/1000);
      }

      //-- GMO end --

      tc_log_info(MOD_NAME, "  max keyframe interval: %d",
              vob->divxkeyframes);
      tc_log_info(MOD_NAME, "             frame rate: %.2f",
              vob->ex_fps);
      tc_log_info(MOD_NAME, "            color space: %s",
              (pix_fmt == CODEC_RGB) ? "RGB24":
             ((pix_fmt == CODEC_YUV) ? "YUV420P" : "YUV422"));
      tc_log_info(MOD_NAME, "             quantizers: %d/%d",
              lavc_venc_context->qmin, lavc_venc_context->qmax);
    }

    return TC_EXPORT_OK;
  }

    if (param->flag == TC_AUDIO)
    {
        pseudo_codec_t target;
        char * user_codec_string;

        tc_log_warn(MOD_NAME, "Usage of this module for audio encoding is deprecated.");
        tc_log_warn(MOD_NAME, "Consider switch to export_tcaud module.");

        if(vob->ex_v_fcc)
        {
            user_codec_string = tc_strdup(vob->ex_v_fcc);
            tc_strstrip(user_codec_string);
        }
        else
            user_codec_string = 0;

        if(user_codec_string)
        {
            if(!strncmp(user_codec_string, "vcd", 3))
                target = pc_vcd;
            else if(!strncmp(user_codec_string, "svcd", 4))
                    target = pc_svcd;
            else if(!strncmp(user_codec_string, "xvcd", 4))
                target = pc_xvcd;
            else if(!strncmp(user_codec_string, "dvd", 3))
                        target = pc_dvd;
                    else
                        target = pc_none;
        }
        else
            target = pc_none;

        free(user_codec_string);
        user_codec_string = 0;

        if(target != pc_none)
        {
            int resample_active = tc_filter_find("resample") != 0;
            int rate = pseudo_codec_rate[target];

	    if (verbose) {
		tc_log_info(MOD_NAME, "Selected %s profile for audio",
			    pseudo_codec_name[target]);
		tc_log_info(MOD_NAME, "Resampling filter %sactive",
			    resample_active ? "already " : "in");
	    }

            if(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ACHANS)
            {
                if(vob->dm_chan != 2)
                    tc_log_warn(MOD_NAME, "Number of audio channels not 2 as required");
            }
            else
            {
                vob->dm_chan = 2;
		if (verbose) {
		    tc_log_info(MOD_NAME, "Set number of audio channels to 2");
		}
            }

            if(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ABITS)
            {
                if(vob->dm_bits != 16)
                    tc_log_warn(MOD_NAME, "Number of audio bits not 16 as required");
            }
            else
            {
                vob->dm_bits = 16;
		if (verbose) {
		    tc_log_info(MOD_NAME, "Set number of audio bits to 16");
		}
            }

            if(resample_active)
            {
                if(vob->mp3frequency != 0)
                    tc_log_warn(MOD_NAME, "Resampling filter active but vob->mp3frequency not 0!");

                if(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ARATE)
                {
		    if (verbose) {
			if((rate == -1) || (vob->a_rate == rate)) {
			    tc_log_info(MOD_NAME,
					"No audio resampling necessary");
			} else {
			    tc_log_info(MOD_NAME, "Resampling audio from"
					" %d Hz to %d Hz as required",
					vob->a_rate, rate);
			}
		    }
                }
                else if (rate != -1)
                {
                    vob->a_rate = rate;
		    if (verbose) {
			tc_log_info(MOD_NAME, "Set audio sample rate to %d Hz",
				    rate);
		    }
                }
            }
            else
            {
                if((vob->export_attributes & TC_EXPORT_ATTRIBUTE_ARATE) && (vob->mp3frequency != 0))
                {
                    if(vob->mp3frequency != rate)
                        tc_log_warn(MOD_NAME, "Selected audio sample rate (%d Hz) not %d Hz as required",
					      vob->mp3frequency, rate);

                    if(vob->mp3frequency != vob->a_rate)
                        tc_log_warn(MOD_NAME, "Selected audio sample rate (%d Hz) not equal to input "
					      "sample rate (%d Hz), use -J", vob->mp3frequency, vob->a_rate);
                }
                else
                {
                    if(vob->a_rate == rate && vob->mp3frequency == rate) {
			if (verbose) {
			    tc_log_info(MOD_NAME, "Set audio sample rate"
					" to %d Hz", rate);
			}
                    } else if (vob->a_rate == rate && vob->mp3frequency == 0) {
                        vob->mp3frequency = rate;
			if (verbose) {
			    tc_log_info(MOD_NAME, "No audio resampling"
					" necessary, using %d Hz", rate);
			}
                    }
                    else
                    {
                        vob->mp3frequency = rate;
                        tc_log_warn(MOD_NAME, "Set audio sample rate to %d Hz, input rate is %d Hz",
					       rate, vob->a_rate);
                        tc_log_warn(MOD_NAME, "   loading resample plugin");

                        if(tc_filter_add("resample", NULL) == -1)
                            tc_log_warn(MOD_NAME, "Load of resample filter failed, expect trouble");
                    }
                }
            }

            if(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ABITRATE)
            {
                if((target != pc_dvd) && (target != pc_xvcd))
                {
                    if(vob->mp3bitrate != 224)
                        tc_log_warn(MOD_NAME, "Audio bit rate not 224 kbps as required");
                }
                else
                {
                    if(vob->mp3bitrate < 160 || vob->mp3bitrate > 320)
                        tc_log_warn(MOD_NAME, "Audio bit rate not between 160 and 320 kbps as required");
                }
            }
            else
            {
                vob->mp3bitrate = 224;
		if (verbose) {
		    tc_log_info(MOD_NAME, "Set audio bit rate to 224 kbps");
		}
            }

            if(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ACODEC)
            {
                if(target != pc_dvd)
                {
                    if(vob->ex_a_codec != CODEC_MP2)
                        tc_log_warn(MOD_NAME, "Audio codec not mp2 as required");
                }
                else
                {
                    if(vob->ex_a_codec != CODEC_MP2 && vob->ex_a_codec != CODEC_AC3)
                        tc_log_warn(MOD_NAME, "Audio codec not mp2 or ac3 as required");
                }
            }
            else
            {
                if(target != pc_dvd)
                {
                    vob->ex_a_codec = CODEC_MP2;
		    if (verbose) {
			tc_log_info(MOD_NAME, "Set audio codec to mp2");
		    }
                }
                else
                {
                    vob->ex_a_codec = CODEC_AC3;
		    if (verbose) {
			tc_log_info(MOD_NAME, "Set audio codec to ac3");
		    }
                }
            }
        }

        return tc_audio_init(vob, verbose_flag);
    }

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

  // open output file

  /* Open file */
  if ( (param->flag == TC_VIDEO && !is_mpegvideo) || (param->flag == TC_AUDIO && !vob->audio_file_flag)) {
    if (vob->avifile_out==NULL) {

      vob->avifile_out = AVI_open_output_file(vob->video_out_file);

      if ((vob->avifile_out) == NULL) {
    AVI_print_error("avi open error");
    return TC_EXPORT_ERROR;
      }

    }
  }

  /* Save locally */
  avifile = vob->avifile_out;


  if (param->flag == TC_VIDEO) {

    // video
    if (is_mpegvideo) {

        mpeg1fd = fopen(vob->video_out_file, "wb");

        if (!mpeg1fd)
        {
            tc_log_warn(MOD_NAME, "Cannot open file \"%s\", using /dev/null",
                        vob->video_out_file);
            mpeg1fd = fopen("/dev/null", "wb");
        }

    } else {
      // pass extradata to AVI writer
      if (lavc_venc_context->extradata > 0) {
          avifile->extradata      = lavc_venc_context->extradata;
          avifile->extradata_size = lavc_venc_context->extradata_size;
      }
      else {
          avifile->extradata      = NULL;
          avifile->extradata_size = 0;
      }

      AVI_set_video(avifile, vob->ex_v_width, vob->ex_v_height, vob->ex_fps,
                    codec->fourCC);

      if (vob->avi_comment_fd>0)
      AVI_set_comment_fd(vob->avifile_out, vob->avi_comment_fd);
    }

    return TC_EXPORT_OK;
  }


  if (param->flag == TC_AUDIO)
    return tc_audio_open(vob, vob->avifile_out);

  // invalid flag
  return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{

  int out_size;
  const char pict_type_char[5]= {'?', 'I', 'P', 'B', 'S'};

  if (param->flag == TC_VIDEO) {

    ++frames;

    if (encoded_frames && frames > encoded_frames)
    return TC_EXPORT_ERROR;

    lavc_venc_frame->interlaced_frame = interlacing_active;
    lavc_venc_frame->top_field_first = interlacing_top_first;

    switch (pix_fmt)
    {
        case CODEC_YUV:
            lavc_venc_frame->linesize[0] = lavc_venc_context->width;
            lavc_venc_frame->linesize[1] = lavc_venc_context->width / 2;
            lavc_venc_frame->linesize[2] = lavc_venc_context->width / 2;

            if(is_huffyuv)
            {
                uint8_t *src[3];
	        	YUV_INIT_PLANES(src, param->buffer, IMG_YUV_DEFAULT,
			                	lavc_venc_context->width, lavc_venc_context->height);
                avpicture_fill((AVPicture *)lavc_venc_frame, img_buffer,
                               PIX_FMT_YUV422P, lavc_venc_context->width,
                               lavc_venc_context->height);
        		/* FIXME: can't use tcv_convert (see decode_lavc.c) */
                ac_imgconvert(src, IMG_YUV_DEFAULT,
                              lavc_venc_frame->data, IMG_YUV422P,
                              lavc_venc_context->width,
                              lavc_venc_context->height);
            }
            else
            {
                YUV_INIT_PLANES(lavc_venc_frame->data, param->buffer,
                                IMG_YUV420P, lavc_venc_context->width,
                                lavc_venc_context->height);
            }
            break;

        case CODEC_YUV422:
            if(is_huffyuv)
            {
                YUV_INIT_PLANES(lavc_venc_frame->data, param->buffer,
                                IMG_YUV422P, lavc_venc_context->width,
                                lavc_venc_context->height);
            }
            else
            {
                uint8_t *src[3];
        		YUV_INIT_PLANES(src, param->buffer, IMG_YUV422P,
		                		lavc_venc_context->width,
                                lavc_venc_context->height);
                avpicture_fill((AVPicture *)lavc_venc_frame, img_buffer,
                               PIX_FMT_YUV420P, lavc_venc_context->width,
                               lavc_venc_context->height);
                ac_imgconvert(src, IMG_YUV422P,
                              lavc_venc_frame->data, IMG_YUV420P,
                              lavc_venc_context->width,
                              lavc_venc_context->height);
            }
            break;

        case CODEC_RGB:
            avpicture_fill((AVPicture *)lavc_venc_frame, img_buffer,
                           PIX_FMT_YUV420P, lavc_venc_context->width,
                           lavc_venc_context->height);
    	    ac_imgconvert(&param->buffer, IMG_RGB_DEFAULT,
                              lavc_venc_frame->data, IMG_YUV420P,
                              lavc_venc_context->width,
                              lavc_venc_context->height);
            break;

        default:
              tc_log_warn(MOD_NAME, "Unknown pixel format %d.", pix_fmt);
              return TC_EXPORT_ERROR;
    }


    TC_LOCK_LIBAVCODEC;
    out_size = avcodec_encode_video(lavc_venc_context,
                                    enc_buffer, size,
                                    lavc_venc_frame);
    TC_UNLOCK_LIBAVCODEC;

    if (out_size < 0) {
      tc_log_warn(MOD_NAME, "encoder error: size (%d)", out_size);
      return TC_EXPORT_ERROR;
    }
    if (verbose & TC_STATS) {
      tc_log_warn(MOD_NAME, "encoder: size of encoded (%d)", out_size);
    }

    //0.6.2: switch outfile on "r/R" and -J pv
    //0.6.2: enforce auto-split at 2G (or user value) for normal AVI files
    if (!is_mpegvideo) {
      if((uint32_t)(AVI_bytes_written(avifile)+out_size+16+8)>>20 >= tc_avi_limit) tc_outstream_rotate_request();

      if (lavc_venc_context->coded_frame->key_frame) tc_outstream_rotate();

      if (AVI_write_frame(avifile, enc_buffer, out_size,
                       lavc_venc_context->coded_frame->key_frame? 1 : 0) < 0) {
    AVI_print_error("avi video write error");

    return TC_EXPORT_ERROR;
      }
    } else { // mpegvideo
      if ( (out_size >0) && (fwrite (enc_buffer, out_size, 1, mpeg1fd) <= 0) ) {
    tc_log_warn(MOD_NAME, "encoder error write failed size (%d)", out_size);
    //return TC_EXPORT_ERROR;
      }
    }

    /* store psnr / pict size / type / qscale */
    if(do_psnr){
        static FILE *fvstats=NULL;
        char filename[20];
        double f= lavc_venc_context->width*lavc_venc_context->height*255.0*255.0;

        if(!fvstats) {
            time_t today2;
            struct tm *today;
            today2 = time(NULL);
            today = localtime(&today2);
            tc_snprintf(filename, sizeof(filename), "psnr_%02d%02d%02d.log",
	                today->tm_hour, today->tm_min, today->tm_sec);
            fvstats = fopen(filename,"w");
            if(!fvstats) {
                tc_log_perror(MOD_NAME, "fopen");
                lavc_param_psnr=0; // disable block
        do_psnr = 0;
                /*exit(1);*/
            }
        }

        fprintf(fvstats, "%6d, %2d, %6d, %2.2f, %2.2f, %2.2f, %2.2f %c\n",
            lavc_venc_context->coded_frame->coded_picture_number,
            lavc_venc_context->coded_frame->quality,
            out_size,
            psnr(lavc_venc_context->coded_frame->error[0]/f),
            psnr(lavc_venc_context->coded_frame->error[1]*4/f),
            psnr(lavc_venc_context->coded_frame->error[2]*4/f),
            psnr((lavc_venc_context->coded_frame->error[0]+lavc_venc_context->coded_frame->error[1]+lavc_venc_context->coded_frame->error[2])/(f*1.5)),
            pict_type_char[lavc_venc_context->coded_frame->pict_type]
            );
    }

    /* store stats if there are any */
    if (lavc_venc_context->stats_out && stats_file)
      fprintf(stats_file, "%s", lavc_venc_context->stats_out);

    return TC_EXPORT_OK;
  }

  if (param->flag == TC_AUDIO)
    return tc_audio_encode(param->buffer, param->size, avifile);

  // invalid flag
  return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * stop encoder
 *
 * ------------------------------------------------------------*/

#define FREEPTR(PTR) do { \
    if ((PTR)) {          \
        free((PTR));      \
        (PTR) = NULL;     \
    }                     \
} while (0)


MOD_stop
{

  if (param->flag == TC_VIDEO) {

    if(do_psnr){
        double f= lavc_venc_context->width*lavc_venc_context->height*255.0*255.0;

        f*= lavc_venc_context->coded_frame->coded_picture_number;

        tc_log_info(MOD_NAME, "PSNR: Y:%2.2f, Cb:%2.2f, Cr:%2.2f, All:%2.2f",
            psnr(lavc_venc_context->error[0]/f),
            psnr(lavc_venc_context->error[1]*4/f),
            psnr(lavc_venc_context->error[2]*4/f),
            psnr((lavc_venc_context->error[0]+lavc_venc_context->error[1]+lavc_venc_context->error[2])/(f*1.5))
            );
    }

    FREEPTR(enc_buffer);
    FREEPTR(img_buffer);
    FREEPTR(lavc_venc_frame);

    //-- release encoder --
    if (lavc_venc_codec) {
      avcodec_close(lavc_venc_context);
      lavc_venc_codec = NULL;
    }

    if (stats_file) {
      fclose(stats_file);
      stats_file = NULL;
    }

    if (lavc_venc_context != NULL) {
      if (lavc_venc_context->rc_override) {
        FREEPTR(lavc_venc_context->rc_override);
      }
      FREEPTR(lavc_venc_context);
    }
    free(real_codec);
    return TC_EXPORT_OK;
  }

  if (param->flag == TC_AUDIO)
    return tc_audio_stop();

  return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * close outputfiles
 *
 * ------------------------------------------------------------*/

MOD_close
{

  vob_t *vob = tc_get_vob();

  if (param->flag == TC_AUDIO)
    return tc_audio_close();

  if (vob->avifile_out!=NULL) {
    AVI_close(vob->avifile_out);
    vob->avifile_out=NULL;
    return TC_EXPORT_OK;
  }

  if (is_mpegvideo) {
    if (mpeg1fd) {
      fclose (mpeg1fd);
      mpeg1fd = NULL;
      return TC_EXPORT_OK;
    }
  }

  return TC_EXPORT_ERROR;

}
