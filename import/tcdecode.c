/*
 *  tcdecode.c
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

#include "transcode.h"
#include "tcinfo.h"

#include <limits.h>

#include "ioaux.h"
#include "tc.h"
#include "libtc/xio.h"

#define EXE "tcdecode"

extern long fileinfo(int fd, int skip);

int verbose = TC_QUIET;

void import_exit(int code)
{
  if (verbose & TC_DEBUG)
    tc_log_msg(EXE, "(pid=%d) exit (code %d)", (int) getpid(), code);
  exit(code);
}


/* ------------------------------------------------------------
 *
 * print a usage/version message
 *
 * ------------------------------------------------------------*/


void version(void)
{
    /* print id string to stderr */
    fprintf(stderr, "%s (%s v%s) (C) 2001-2003 Thomas Oestreich,"
                                    " 2003-2010 Transcode Team\n",
                    EXE, PACKAGE, VERSION);
}


static void usage(int status)
{
  version();

  fprintf(stderr,"\nUsage: %s [options]\n", EXE);

  fprintf(stderr,"    -i file           input file [stdin]\n");
  fprintf(stderr,"    -x codec          source codec (required)\n");
  fprintf(stderr,"    -t package        codec package\n");
  fprintf(stderr,"    -g wxh            stream frame size [autodetect]\n");
  fprintf(stderr,"    -y format         output raw stream format [rgb]\n");
  fprintf(stderr,"    -Q mode           decoding quality (0=fastest-5=best) [%d]\n", VQUALITY);
  fprintf(stderr,"    -d mode           verbosity mode\n");
  fprintf(stderr,"    -s c,f,r          audio gain for ac3 downmixing [1,1,1]\n");
  fprintf(stderr,"    -A n              A52 decoder flag [0]\n");
  fprintf(stderr,"    -C s,e            decode only from start to end ((V) frames/(A) bytes) [all]\n");
  fprintf(stderr,"    -Y                use libdv YUY2 decoder mode\n");
  fprintf(stderr,"    -z r              convert zero padding to silence\n");
  fprintf(stderr,"    -v                print version\n");

  exit(status);
}

/* ------------------------------------------------------------
 *
 * universal decode thread frontend
 *
 * ------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    decode_t decode;
    int ch, done=0;
    char *codec=NULL, *format="rgb", *magic="none";

    memset(&decode, 0, sizeof(decode));
    decode.magic = TC_MAGIC_UNKNOWN;
    decode.stype = TC_STYPE_UNKNOWN;
    decode.quality = VQUALITY;
    decode.ac3_gain[0] = decode.ac3_gain[1] = decode.ac3_gain[2] = 1.0;
    decode.frame_limit[0]=0;
    decode.frame_limit[1]=LONG_MAX;

    libtc_init(&argc, &argv);

    while ((ch = getopt(argc, argv, "Q:t:d:x:i:a:g:vy:s:YC:A:z:?h")) != -1) {
	switch (ch) {

	case 'i':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  decode.name = optarg;
	  break;

	case 'd':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  verbose = atoi(optarg);
	  break;

	case 'Q':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  decode.quality = atoi(optarg);
	  break;

	case 'A':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  decode.a52_mode = atoi(optarg);
	  break;

	case 'x':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  codec = optarg;
	  break;

	case 't':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  magic = optarg;
	  break;

	case 'y':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  format = optarg;
	  break;

	case 'g':
	  if (optarg[0]=='-') usage(EXIT_FAILURE);
	  if (2 != sscanf(optarg,"%dx%d", &decode.width, &decode.height)) usage(EXIT_FAILURE);
	  break;

	case 'v':
	  version();
	  exit(0);
	  break;

	case 'Y':
	  decode.dv_yuy2_mode=1;
	  break;

	case 's':
	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  if (3 != sscanf(optarg,"%lf,%lf,%lf", &decode.ac3_gain[0], &decode.ac3_gain[1], &decode.ac3_gain[2])) usage(EXIT_FAILURE);
	  break;

	case 'C':
	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  if (2 != sscanf(optarg,"%ld,%ld", &decode.frame_limit[0], &decode.frame_limit[1])) usage(EXIT_FAILURE);
 	  if (decode.frame_limit[0] >= decode.frame_limit[1])
	  {
  		tc_log_error(EXE, "Invalid -C options");
		usage(EXIT_FAILURE);
	  }
	  break;

	case 'z':
	  if(optarg[0]=='-') usage(EXIT_FAILURE);
	  decode.padrate = atoi(optarg);
	  break;

	case 'h':
	  usage(EXIT_SUCCESS);
	default:
	  usage(EXIT_FAILURE);
	}
    }

    ac_init(AC_ALL);

    /* ------------------------------------------------------------
     *
     * fill out defaults for info structure
     *
     * ------------------------------------------------------------*/

    // assume defaults
    if(decode.name==NULL) decode.stype=TC_STYPE_STDIN;

    // no autodetection yet
    if(codec==NULL) {
	tc_log_error(EXE, "codec must be specified");
	usage(EXIT_FAILURE);
    }

    // do not try to mess with the stream
    if (decode.stype != TC_STYPE_STDIN) {
	if (tc_file_check(decode.name)) exit(1);
	if ((decode.fd_in = xio_open(decode.name, O_RDONLY)) < 0) {
	    tc_log_perror(EXE, "open file");
	    exit(1);
	}

	// try to find out the filetype
	decode.magic = fileinfo(decode.fd_in, 0);
	if (verbose)
	    tc_log_msg(EXE, "(pid=%d) %s", getpid(), filetype(decode.magic));

    } else decode.fd_in = STDIN_FILENO;

    decode.fd_out = STDOUT_FILENO;
    decode.codec = TC_CODEC_UNKNOWN;
    decode.verbose = verbose;
    if (decode.width < 0) decode.width = 0;
    if (decode.height < 0) decode.height = 0;

    /* ------------------------------------------------------------
     *
     * output raw stream format
     *
     * ------------------------------------------------------------*/

    if (!strcmp(format, "rgb")) decode.format = TC_CODEC_RGB;
    else if (!strcmp(format, "yuv420p")) decode.format = TC_CODEC_YUV420P;
    else if (!strcmp(format, "yuv2")) decode.format = TC_CODEC_YUV2;
    else if (!strcmp(format, "yuy2")) decode.format = TC_CODEC_YUY2;
    else if (!strcmp(format, "pcm")) decode.format = TC_CODEC_PCM;
    else if (!strcmp(format, "raw")) decode.format = TC_CODEC_RAW;

    /* ------------------------------------------------------------
     *
     * codec specific section
     *
     * note: user provided values overwrite autodetection!
     *
     * ------------------------------------------------------------*/

    // FFMPEG can decode a lot
    if(!strcmp(magic, "ffmpeg") || !strcmp(magic, "lavc")) {
	if (!strcmp(codec, "mpeg2")) decode.codec = TC_CODEC_MPEG2;
	else if (!strcmp(codec, "mpeg2video")) decode.codec = TC_CODEC_MPEG2;
	else if (!strcmp(codec, "mpeg1video")) decode.codec = TC_CODEC_MPEG1;
	else if (!strcmp(codec, "divx3")) decode.codec = TC_CODEC_DIVX3;
	else if (!strcmp(codec, "divx")) decode.codec = TC_CODEC_DIVX4;
	else if (!strcmp(codec, "divx4")) decode.codec = TC_CODEC_DIVX4;
	else if (!strcmp(codec, "mp42")) decode.codec = TC_CODEC_MP42;
	else if (!strcmp(codec, "mjpg")) decode.codec = TC_CODEC_MJPEG;
	else if (!strcmp(codec, "mjpeg")) decode.codec = TC_CODEC_MJPEG;
	else if (!strcmp(codec, "rv10")) decode.codec = TC_CODEC_RV10;
	else if (!strcmp(codec, "svq1")) decode.codec = TC_CODEC_SVQ1;
	else if (!strcmp(codec, "svq3")) decode.codec = TC_CODEC_SVQ3;
	else if (!strcmp(codec, "vp3")) decode.codec = TC_CODEC_VP3;
	else if (!strcmp(codec, "4xm")) decode.codec = TC_CODEC_4XM;
	else if (!strcmp(codec, "wmv1")) decode.codec = TC_CODEC_WMV1;
	else if (!strcmp(codec, "wmv2")) decode.codec = TC_CODEC_WMV2;
	else if (!strcmp(codec, "hfyu")) decode.codec = TC_CODEC_HUFFYUV;
	else if (!strcmp(codec, "indeo3")) decode.codec = TC_CODEC_INDEO3;
	else if (!strcmp(codec, "h263p")) decode.codec = TC_CODEC_H263P;
	else if (!strcmp(codec, "h263i")) decode.codec = TC_CODEC_H263I;
	else if (!strcmp(codec, "dvvideo")) decode.codec = TC_CODEC_DV;
	else if (!strcmp(codec, "dv")) decode.codec = TC_CODEC_DV;
	else if (!strcmp(codec, "vag")) decode.codec = TC_CODEC_VAG;

	decode_lavc(&decode);
    }

    // MPEG2
    if (!strcmp(codec, "mpeg2")) {
	decode.codec = TC_CODEC_MPEG2;
	decode_mpeg2(&decode);
	done = 1;
    }

    // OGG
    if (!strcmp(codec, "ogg")) {
	decode.codec = TC_CODEC_VORBIS;
	decode_ogg(&decode);
	done = 1;
    }

    // AC3
    if (!strcmp(codec, "ac3")) {
	decode.codec = TC_CODEC_AC3;
	decode_a52(&decode);
	done = 1;
    }

    // MP3
    if (!strcmp(codec, "mp3")) {
	decode.codec = TC_CODEC_MP3;
	decode_mp3(&decode);
	done = 1;
    }

    // MP2
    if (!strcmp(codec, "mp2")) {
	decode.codec = TC_CODEC_MP3;
	decode_mp2(&decode);
	done = 1;
    }

    // DV
    if (!strcmp(codec, "dv")) {
	decode.codec = TC_CODEC_DV;
	decode_dv(&decode);
	done = 1;
    }

    // YUV420P
    if (!strcmp(codec, "yuv420p")) {
	decode.codec = TC_CODEC_YUV420P;
	decode_yuv(&decode);
	done = 1;
    }

#if 0
    // DivX Video
    if (!strcmp(codec, "divx")) {
      decode.select = TC_VIDEO;
      decode_divx(&decode);
      done = 1;
    }
#endif

    // MOV
    if (!strcmp(codec, "mov")) {
      decode_mov(&decode);
      done = 1;
    }

    // LZO
    if (!strcmp(codec, "lzo")) {
      decode_lzo(&decode);
      done = 1;
    }

    if(!done) {
	tc_log_error(EXE, "(pid=%d) unable to handle codec %s", getpid(), codec);
	exit(1);
    }

    if (decode.fd_in != STDIN_FILENO) xio_close(decode.fd_in);

    return 0;
}

#include "libtc/static_xio.h"
