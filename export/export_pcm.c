/*
 *  export_pcm.c
 *
 *  Copyright (C) Thomas Oestreich - May 2001
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "transcode.h"
#include "avilib/avilib.h"
#include "libtc/libtc.h"

#define MOD_NAME    "export_pcm.so"
#define MOD_VERSION "v0.1.0 (2007-08-25)"
#define MOD_CODEC   "(audio) PCM (non-interleaved)"


static int verbose_flag=TC_QUIET;
static int capability_flag=TC_CAP_PCM|TC_CAP_RGB|TC_CAP_YUV|TC_CAP_VID;

#define MOD_PRE tc_pcm
#include "export_def.h"

static struct wave_header rtf;
static int fd_r = -1, fd_l = -1, fd_c = -1;
static int fd_ls = -1, fd_rs = -1, fd_lfe = -1;

typedef enum {
    CHANNEL_CENTER = 1,
    CHANNEL_STEREO = 2,
    CHANNEL_FRONT  = 4,
    CHANNEL_LFE    = 8
} PCMChannels;

static PCMChannels chan_settings[8] = {
    0,                                                       /* 0: nothing */
    CHANNEL_CENTER,                                          /* 1: mono */
    CHANNEL_STEREO,                                          /* 2: stereo */
    CHANNEL_STEREO|CHANNEL_CENTER,                           /* 3: 2.1 */   
    CHANNEL_STEREO|CHANNEL_FRONT,                            /* 4: 2+2 */
    CHANNEL_STEREO|CHANNEL_FRONT|CHANNEL_CENTER,             /* 5: 2+2+1 */
    CHANNEL_STEREO|CHANNEL_FRONT|CHANNEL_CENTER|CHANNEL_LFE, /* 6: 5.1 */
    0,                                                       /* nothing */
};

/* ------------------------------------------------------------ 
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
    int rate;

    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    if (param->flag == TC_AUDIO) {
        memset(&rtf, 0, sizeof(rtf));
    
        strlcpy(rtf.riff.id, "RIFF", 4);
        strlcpy(rtf.riff.wave_id, "WAVE", 4);
        strlcpy(rtf.format.id, "fmt ", 4);
    
        rtf.format.len = sizeof(struct common_struct);
        rtf.common.wFormatTag = CODEC_PCM;
    
        rate = (vob->mp3frequency != 0) ?vob->mp3frequency :vob->a_rate;

        rtf.common.dwSamplesPerSec = rate;
        rtf.common.dwAvgBytesPerSec = vob->dm_chan * rate * vob->dm_bits/8;
        rtf.common.wBitsPerSample = vob->dm_bits;
        rtf.common.wBlockAlign = vob->dm_chan*vob->dm_bits/8;
        
        if(vob->dm_chan >= 1 && vob->dm_chan <= 6) { /* sanity check */
            rtf.common.wChannels=vob->dm_chan;
        } else {
            tc_log_error(MOD_NAME, "bad PCM channel number: %i",
                                   vob->dm_chan);
            return TC_EXPORT_ERROR;
        }
        if (!vob->a_codec_flag
          || !rtf.common.dwSamplesPerSec
          || !rtf.common.wBitsPerSample
          || !rtf.common.wBlockAlign) {
            tc_log_error(MOD_NAME, "cannot export PCM, invalid format "
                                   "(no audio track at all?)");
            return TC_EXPORT_ERROR;
        }

        rtf.riff.len = 0x7FFFFFFF;
        rtf.data.len = 0x7FFFFFFF;

        strlcpy(rtf.data.id, "data",4);

        return TC_EXPORT_OK;
    }
    // invalid flag
    return TC_EXPORT_ERROR; 
}

/* ------------------------------------------------------------ 
 *
 * open outputfile
 *
 * ------------------------------------------------------------*/

/* XXX */
#define FD_OPEN(fd) do { \
    fd = open(fname, O_RDWR|O_CREAT|O_TRUNC, \
	                 S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH); \
    if (fd < 0) { \
        tc_log_error(MOD_NAME, "opening file: %s", strerror(errno)); \
        return TC_EXPORT_ERROR; \
    } \
} while (0)

MOD_open
{
    char fname[TC_BUF_LINE];
    int chan_flags = chan_settings[rtf.common.wChannels];  

    if (param->flag == TC_AUDIO) {
        if (chan_flags & CHANNEL_LFE) {
            tc_snprintf(fname, sizeof(fname), "%s_lfe.pcm",
                        vob->audio_out_file);
            FD_OPEN(fd_lfe);
        }
	
        if (chan_flags & CHANNEL_FRONT) {
	        tc_snprintf(fname, sizeof(fname), "%s_ls.pcm",
                        vob->audio_out_file);
            FD_OPEN(fd_ls);
	  
            tc_snprintf(fname, sizeof(fname), "%s_rs.pcm",
                        vob->audio_out_file);
            FD_OPEN(fd_rs);
        }
 
        if (chan_flags & CHANNEL_STEREO) {
	        tc_snprintf(fname, sizeof(fname), "%s_l.pcm",
                        vob->audio_out_file);
            FD_OPEN(fd_l);
	  
            tc_snprintf(fname, sizeof(fname), "%s_r.pcm",
                        vob->audio_out_file);
	        FD_OPEN(fd_r); 
        }

        if (chan_flags & CHANNEL_CENTER) {
	        tc_snprintf(fname, sizeof(fname), "%s_c.pcm",
                        vob->audio_out_file);
            FD_OPEN(fd_c);
        }

        return TC_EXPORT_OK;
    }

    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
  
    // invalid flag
    return TC_EXPORT_ERROR; 
}   

/* ------------------------------------------------------------ 
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

#define FD_WRITE(fd, buf, size) do { \
      if(fd != -1 && tc_pwrite(fd, buf, size) != size) { \
          tc_log_error(MOD_NAME,  "writing audio frame: %s", \
                       strerror(errno)); \
          return TC_EXPORT_ERROR; \
      } \
} while (0)

MOD_encode
{
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    if (param->flag == TC_AUDIO) { 
        int size = (int)(param->size/rtf.common.wChannels);
    
        switch (rtf.common.wChannels) {
          case 6:
            FD_WRITE(fd_rs,  param->buffer + 5 * size, size);
            FD_WRITE(fd_ls,  param->buffer + 4 * size, size);
            FD_WRITE(fd_r,   param->buffer + 3 * size, size);
            FD_WRITE(fd_c,   param->buffer + 2 * size, size);
            FD_WRITE(fd_l,   param->buffer +     size, size);
            FD_WRITE(fd_lfe, param->buffer,            size);
            break;

          case 2:
            FD_WRITE(fd_r, param->buffer + size, size);
            FD_WRITE(fd_l, param->buffer,        size);
            break;
      
          case 1:
            FD_WRITE(fd_c, param->buffer, size);
            break;
        }

        return TC_EXPORT_OK;
    }
  
    // invalid flag
    return TC_EXPORT_ERROR;
}

/* ------------------------------------------------------------ 
 *
 * close outputfiles
 *
 * ------------------------------------------------------------*/

#define FD_CLOSE(fd) do { \
        if (fd != -1) { \
            close(fd);\
        } \
} while (0)

MOD_close
{  
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    if (param->flag == TC_AUDIO) {
        FD_CLOSE(fd_l);
        FD_CLOSE(fd_c);
        FD_CLOSE(fd_r);
        FD_CLOSE(fd_ls);
        FD_CLOSE(fd_rs);
        FD_CLOSE(fd_lfe);
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;  
}

/* ------------------------------------------------------------ 
 *
 * stop encoder
 *
 * ------------------------------------------------------------*/

MOD_stop 
{
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    if (param->flag == TC_AUDIO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
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
