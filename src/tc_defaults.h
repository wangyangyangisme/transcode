/*
 *  tc_defaults.h
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>

#ifndef TC_DEFAULTS_H
#define TC_DEFAULTS_H

#define TC_DEFAULT_MOD_PATH "/usr/local/lib/transcode"

#define TC_DEFAULT_IN_FILE  "/dev/zero"
#define TC_DEFAULT_OUT_FILE "/dev/null"

// default PAL video size
#define PAL_W                  720
#define PAL_H                  576
#define BPP                     24
#define PAL_FPS                 25.0
#define MIN_FPS                  1.0
#define NTSC_FILM    ((double)(24000)/1001.0)
#define NTSC_VIDEO   ((double)(30000)/1001.0)

//NTSC
#define NTSC_W                  720
#define NTSC_H                  480

//new max frame size:
#define TC_MAX_V_FRAME_WIDTH     2500
#define TC_MAX_V_FRAME_HEIGHT    2000

// max bytes per pixel
#define TC_MAX_V_BYTESPP        4

// audio defaults
#define RATE         48000
#define BITS            16
#define CHANNELS         2

#define SIZE_RGB_FRAME ((int) TC_MAX_V_FRAME_WIDTH*TC_MAX_V_FRAME_HEIGHT*(BPP/8))
#define SIZE_PCM_FRAME ((int) (RATE/MIN_FPS) * BITS/8 * CHANNELS * 3)

#define TC_PAD_AUD_FRAMES 10
#define TC_MAX_SEEK_BYTES (1<<20)

// DivX/MPEG-4 encoder defaults
#define VBITRATE            1800
#define VKEYFRAMES           250
#define VCRISPNESS           100

#define VMULTIPASS             0
#define VQUALITY               5

#define VMINQUANTIZER          2
#define VMAXQUANTIZER         31

#define VQUANTIZER            10

#define RC_PERIOD           2000
#define RC_REACTION_PERIOD    10
#define RC_REACTION_RATIO     20

// Divx5 VBV (Video Bitrate Verifier)
// Home theatre profile and settings
#define DIVX5_VBV_PROFILE                 3
#define DIVX5_VBV_BITRATE     (4000000/400)
#define DIVX5_VBV_SIZE      (3145728/16384)
#define DIVX5_VBV_OCCUPANCY    (2359296/64)

//----------------------------------

#define ABITRATE       128
#define AQUALITY         5
#define AVBR             0
#define AMODE            0

typedef enum tcdebugmode_ TCDebugMode;
enum tcdebugmode_ {
    TC_QUIET   = 0,
    TC_INFO    = 1,
    TC_DEBUG   = 2,
    TC_STATS   = 4,
    TC_CLEANUP = 8,
    TC_FLIST   = 16,
    TC_SYNC    = 32,
    TC_COUNTER = 64,
    TC_PRIVATE = 128,
    TC_THREADS = 256,
    TC_WATCH   = 512,
};

//import/export/filter frame buffer status flag
#define TC_NONE                  0
#define TC_VIDEO                 1
#define TC_AUDIO                 2
#define TC_SUBEX                 4
#define TC_RESERVED              8
#define TC_EXTRA                16

#define TC_FILTER_INIT          16
#define TC_PRE_S_PROCESS        32
#define TC_PRE_M_PROCESS        64
#define TC_INT_M_PROCESS       128
#define TC_POST_M_PROCESS      256
#define TC_POST_S_PROCESS      512
#define TC_PREVIEW            1024
#define TC_FILTER_CLOSE       2048
#define TC_FILTER_GET_CONFIG  4096

#define TC_IMPORT             8192
#define TC_EXPORT            16384

#define TC_DELAY_MAX         40000
#define TC_DELAY_MIN         10000

#define TC_DEFAULT_IMPORT_AUDIO "null"
#define TC_DEFAULT_IMPORT_VIDEO "null"
#define TC_DEFAULT_EXPORT_AUDIO "null"
#define TC_DEFAULT_EXPORT_VIDEO "null"
#define TC_DEFAULT_EXPORT_MPLEX "null"

#define TC_FRAME_BUFFER        10
#define TC_FRAME_THREADS        1
#define TC_FRAME_THREADS_MAX   32

#define TC_FRAME_FIRST          0
#define TC_FRAME_LAST     INT_MAX

#define TC_LEAP_FRAME        1000
#define TC_MAX_AUD_TRACKS      32

//--------------------------------------------------

#define CODEC_NULL       0x0

#define CODEC_RGB          1
#define CODEC_YUV          2
#define CODEC_MP4          4
#define CODEC_YUY2         8
#define CODEC_DV          16
#define CODEC_RAW         32
#define CODEC_RAW_RGB     64
#define CODEC_RAW_YUV    128
#define CODEC_YUV422     256

#define CODEC_PCM     0x1
#define CODEC_AC3     0x2000
#define CODEC_MP2     0x50
#define CODEC_MP3     0x55
#define CODEC_DIVX    0x161
#define CODEC_IMA4    0x11
#define CODEC_LPCM    0x10001
#define CODEC_DTS     0x1000F     //??
#define CODEC_VORBIS  0xfffe
#define CODEC_VAG     0xfeed //me pizza

#define TC_INFO_NO_DEMUX        1
#define TC_INFO_MPEG_PS         2
#define TC_INFO_MPEG_ES         4
#define TC_INFO_MPEG_PES        8

#define TC_FRAME_DV_PAL     144000
#define TC_FRAME_DV_NTSC    120000

#define TC_SUBTITLE_HDRMAGIC 0x00030001

#define TC_DEFAULT_AAWEIGHT (1.0f/3.0f)
#define TC_DEFAULT_AABIAS   (0.5f)

#define TC_A52_DRC_OFF    1
#define TC_A52_DEMUX      2
#define TC_A52_DOLBY_OFF  4

#define AVI_FILE_LIMIT 2048

#define M2V_REQUANT_FACTOR  1.00f

/* 
 * flags used in modules for supporting export profiles (--export_prof)
 * if one of those flag is set, then use the value provided by the user.
 * otherwise use the ones the export modules suggests.
 */
typedef enum tcexportattribute_ TCExportAttribute;
enum tcexportattribute_ {
    TC_EXPORT_ATTRIBUTE_NONE     = (    0),
    TC_EXPORT_ATTRIBUTE_VBITRATE = (1<< 1), /* -w */
    TC_EXPORT_ATTRIBUTE_ABITRATE = (1<< 2), /* -b */
    TC_EXPORT_ATTRIBUTE_FIELDS   = (1<< 3), /* --encode_fields */
    TC_EXPORT_ATTRIBUTE_VMODULE  = (1<< 4), /* -y X,* */
    TC_EXPORT_ATTRIBUTE_AMODULE  = (1<< 5), /* -y *,X */
    TC_EXPORT_ATTRIBUTE_FRC      = (1<< 6), /* --export_fps *,X */
    TC_EXPORT_ATTRIBUTE_FPS      = (1<< 7), /* --export_fps X,* */
    TC_EXPORT_ATTRIBUTE_VCODEC   = (1<< 8), /* -F */
    TC_EXPORT_ATTRIBUTE_ACODEC   = (1<< 9), /* -N */
    TC_EXPORT_ATTRIBUTE_ARATE    = (1<<10), /* -E X,*,* */
    TC_EXPORT_ATTRIBUTE_ABITS    = (1<<11), /* -E *,X,* */
    TC_EXPORT_ATTRIBUTE_ACHANS   = (1<<12), /* -E *,*,X */
    TC_EXPORT_ATTRIBUTE_ASR      = (1<<13), /* --export_asr */
    TC_EXPORT_ATTRIBUTE_PAR      = (1<<14), /* --export_par */
    TC_EXPORT_ATTRIBUTE_GOP      = (1<<15), /* key frames */
};

#endif /* TC_DEFAULTS_H */
