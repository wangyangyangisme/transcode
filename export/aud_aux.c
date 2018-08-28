/*
 *  aud_aux.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) Nicolas LAURENT - August 2003
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
#include "aud_aux.h"

#include "libtc/libtc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>

#ifdef HAVE_LAME
#ifdef HAVE_LAME_INC
#include <lame/lame.h>
#else
#include <lame.h>
#endif
#endif

#ifdef HAVE_FFMPEG
#include "libtc/tcavcodec.h"


static AVCodec        *mpa_codec = NULL;
static AVCodecContext mpa_ctx;
static char           *mpa_buf     = NULL;
static int            mpa_buf_ptr  = 0;
static int            mpa_bytes_ps, mpa_bytes_pf;

#endif

/*
 * Capabilities:
 *
 *             +-------------------------------+
 *             |             Output            |
 *             +-------------------------------+
 *             |  PCM  |  MP2  |  MP3  |  AC3  |
 * +---+-------+-------+-------+-------+-------+
 * | I |  PCM  |   X   |   X   |   X   |   X   |
 * | n +-------+-------+-------+-------+-------+
 * | p |  MP2  |       |   X   |       |       |
 * | u +-------+-------+-------+-------+-------+
 * | t |  MP3  |       |       |   X   |       |
 * |   +-------+-------+-------+-------+-------+
 * |   |  AC3  |       |       |       |   X   |
 * +---+---------------------------------------+
 *
 */


/*-----------------------------------------------------------------------------
                       G L O B A L   V A R I A B L E S
-----------------------------------------------------------------------------*/

#define MP3_CHUNK_SZ (2*1152)

static int verbose_flag=TC_DEBUG;
#define IS_AUDIO_MONO   (avi_aud_chan == 1)
#ifdef HAVE_LAME
#define IS_VBR      (lame_get_VBR(lgf) != vbr_off)
#endif

/* Output buffer */
#define OUTPUT_SIZE SIZE_PCM_FRAME
static char *output   = NULL;
#ifdef HAVE_LAME
static int output_len = 0;
#endif

/* Input buffer */
#define INPUT_SIZE  SIZE_PCM_FRAME
static char *input   = NULL;
#ifdef HAVE_LAME
static int input_len = 0;
#endif

/* encoder */
static int (*tc_audio_encode_function)(char *, int, avi_t *)=NULL;
#ifdef HAVE_LAME
static lame_global_flags *lgf;
#endif
static int lame_flush=0;
static int bitrate=0;

/* output stream */
static avi_t *avifile2=NULL;
static FILE *fd=NULL;
static int is_pipe = 0;

// AVI file information for subsequent calls of open routine:
static int avi_aud_codec, avi_aud_bitrate;
static long avi_aud_rate;
static int avi_aud_chan, avi_aud_bits;

/*-----------------------------------------------------------------------------
                             P R O T O T Y P E S
-----------------------------------------------------------------------------*/

static int tc_audio_init_ffmpeg(vob_t *vob, int o_codec);
static int tc_audio_init_lame(vob_t *vob, int o_codec);
static int tc_audio_init_raw(vob_t *vob);
static int tc_audio_write(char *buffer, size_t size, avi_t *avifile);
static int tc_audio_encode_ffmpeg(char *aud_buffer, int aud_size, avi_t *avifile);
static int tc_audio_encode_mp3(char *aud_buffer, int aud_size, avi_t *avifile);
static int tc_audio_pass_through(char *aud_buffer, int aud_size, avi_t *avifile);
static int tc_audio_pass_through_ac3(char *aud_buffer, int aud_size, avi_t *avifile);
static int tc_audio_pass_through_pcm(char *aud_buffer, int aud_size, avi_t *avifile);
static int tc_audio_mute(char *aud_buffer, int aud_size, avi_t *avifile);
#ifdef HAVE_LAME
static char * lame_error2str(int error);
static void no_debug(const char *format, va_list ap) {return;}
static int tc_get_mp3_header(unsigned char* hbuf, int* chans, int* srate);
#endif

/**
 * Init Lame Encoder
 *
 * @param vob
 *
 * @return TC_EXPORT_OK or TC_EXPORT_ERROR
 */
static int tc_audio_init_lame(vob_t *vob, int o_codec)
{
    static int initialized=0;
    int res = TC_EXPORT_ERROR;

    if (!initialized)
        if (verbose_flag & TC_DEBUG)
            tc_info("Audio: using new version");

    if(initialized==0)
    {
#ifdef HAVE_LAME
        int preset = 0;
        lgf=lame_init();
        if(lgf<0)
        {
            tc_warn("Lame encoder init failed.");
            return(TC_EXPORT_ERROR);
        }

        if(!(verbose_flag & TC_DEBUG)) lame_set_msgf  (lgf, no_debug);
        if(!(verbose_flag & TC_DEBUG)) lame_set_debugf(lgf, no_debug);
        if(!(verbose_flag & TC_DEBUG)) lame_set_errorf(lgf, no_debug);

        lame_set_bWriteVbrTag(lgf, 0);
        lame_set_quality(lgf, vob->mp3quality);

        /* Setting bitrate */
        if(vob->a_vbr)
        { /* VBR */
            lame_set_VBR(lgf, vob->a_vbr);
            /*  1 = best vbr q  6=~128k */
            lame_set_VBR_q(lgf, vob->mp3quality);
            /*
             * if(vob->mp3bitrate>0)
             *  lame_set_VBR_mean_bitrate_kbps(
             *      lgf,
             *      vob->mp3bitrate);
             */
        } else {
            lame_set_VBR(lgf, 0);
            lame_set_brate(lgf, vob->mp3bitrate);
        }

        /* Playing with bitreservoir */
        if(vob->bitreservoir == TC_FALSE)
            lame_set_disable_reservoir(lgf, 1);

        /* Mono / Sterero ? */
        if (IS_AUDIO_MONO)
        {
            lame_set_num_channels(lgf, avi_aud_chan);
            lame_set_mode(lgf, MONO);
        } else {
            lame_set_num_channels(lgf, 2);
            lame_set_mode(lgf, JOINT_STEREO);
        }
        /* Overide defaults */
        if (vob->mp3mode==1)
            lame_set_mode(lgf, STEREO);
        if (vob->mp3mode==2)
            lame_set_mode(lgf, MONO);

        /* sample rate */
        lame_set_in_samplerate(lgf, vob->a_rate);
        lame_set_out_samplerate(lgf, avi_aud_rate);

        /* Optimisations */
        if(tc_accel & AC_MMX)
            lame_set_asm_optimizations(lgf, MMX, 1);

        if(tc_accel & AC_3DNOW)
            lame_set_asm_optimizations(lgf, AMD_3DNOW, 1);

        if(tc_accel & AC_SSE)
            lame_set_asm_optimizations(lgf, SSE, 1);


        /* Preset stuff */
        if (vob->lame_preset && strlen (vob->lame_preset))
        {
            char *c = strchr (vob->lame_preset, ',');
            int fast = 0;

            if (c && *c && *(c+1)) {
                if (strcmp(c+1, "fast"))
                {
                    *c = '\0';
                    fast = 1;
                }
            }

            if (strcmp (vob->lame_preset, "standard") == 0)
            {
                preset = fast?STANDARD_FAST:STANDARD;
                vob->a_vbr = 1;
            }
            else if (strcmp (vob->lame_preset, "medium") == 0)
            {
                preset = fast?MEDIUM_FAST:MEDIUM;
                vob->a_vbr = 1;
            }
            else if (strcmp (vob->lame_preset, "extreme") == 0)
            {
                preset = fast?EXTREME_FAST:EXTREME;
                vob->a_vbr = 1;
            }
            else if (strcmp (vob->lame_preset, "insane") == 0) {
                preset = INSANE;
                vob->a_vbr = 1;
            }
            else if ( atoi(vob->lame_preset) != 0)
            {
                vob->a_vbr = 1;
                preset = atoi(vob->lame_preset);
                avi_aud_bitrate = preset;
            }
            else
                tc_warn("Lame preset `%s' not supported. "
                        "Falling back defaults.",
                        vob->lame_preset);

            if (fast == 1)
                *c = ',';

            if (preset)
            {
                if (verbose_flag & TC_DEBUG)
                    tc_info("Using Lame preset `%s'.",
                            vob->lame_preset);
                lame_set_preset(lgf, preset);
            }
        }

        /* Init Lame ! */
        lame_init_params(lgf);
        if(verbose_flag)
            tc_info("Audio: using lame-%s",
                get_lame_version());
        if (verbose_flag & TC_DEBUG) {
            tc_info("Lame config: PCM -> %s",
                    (o_codec==CODEC_MP3)?"MP3":"MP2");
            tc_info("             bitrate         : %d kbit/s",
                    vob->mp3bitrate);
            tc_info("             ouput samplerate: %d Hz",
                    (vob->mp3frequency>0)?vob->mp3frequency:vob->a_rate);
        }
        /* init lame encoder only on first call */
        initialized = 1;
        res = TC_EXPORT_OK;

#else  /* HAVE_LAME */
        tc_warn("No Lame support available!");
#endif /* HAVE_LAME */
    }

    return res;
}



/**
 * Init FFMPEG AUDIO Encoder
 *
 * @param vob
 *
 * @return TC_EXPORT_OK or TC_EXPORT_ERROR
 */
static int tc_audio_init_ffmpeg(vob_t *vob, int o_codec)
{
    int init_ret = TC_EXPORT_ERROR;
#ifdef HAVE_FFMPEG
    unsigned long codeid = 0;
    int ret = 0;

    TC_INIT_LIBAVCODEC;

    switch (o_codec) {
      case   0x50:
        codeid = CODEC_ID_MP2;
        break;
      case 0x2000:
        codeid = CODEC_ID_AC3;
        break;
      default:
        tc_warn("cannot init ffmpeg with %x", o_codec);
    }

    //-- get it --
    mpa_codec = avcodec_find_encoder(codeid);
    if (!mpa_codec) {
      tc_log_warn("encode_ffmpeg", "mpa codec not found !");
      return(TC_EXPORT_ERROR);
    }

    // OPEN

    //-- set parameters (bitrate, channels and sample-rate) --
    //--------------------------------------------------------
    avcodec_get_context_defaults(&mpa_ctx);
#if LIBAVCODEC_VERSION_MAJOR < 53
    mpa_ctx.codec_type  = CODEC_TYPE_AUDIO;
#else
    mpa_ctx.codec_type  = AVMEDIA_TYPE_AUDIO;
#endif
    mpa_ctx.bit_rate    = vob->mp3bitrate * 1000;  // bitrate dest.
    mpa_ctx.channels    = vob->dm_chan;            // channels
    mpa_ctx.sample_rate = vob->a_rate;

    //-- open codec --
    //----------------
    TC_LOCK_LIBAVCODEC;
    ret = avcodec_open(&mpa_ctx, mpa_codec);
    TC_UNLOCK_LIBAVCODEC;
    if (ret < 0) {
        tc_warn("tc_audio_init_ffmpeg: could not open %s codec !",
                (codeid == CODEC_ID_MP2) ?"mpa" :"ac3");
        return(TC_EXPORT_ERROR);
    }

    //-- bytes per sample and bytes per frame --
    mpa_bytes_ps = mpa_ctx.channels * vob->dm_bits/8;
    mpa_bytes_pf = mpa_ctx.frame_size * mpa_bytes_ps;

    //-- create buffer to hold 1 frame --
    mpa_buf     = malloc(mpa_bytes_pf);
    mpa_buf_ptr = 0;
    init_ret    = TC_EXPORT_OK;

#else  /* HAVE_FFMPEG */
    tc_warn("No FFmpeg support available!");
#endif /* HAVE_FFMPEG */

    return init_ret;

}


/**
 * Init audio encoder RAW -> *
 *
 * @param vob
 *
 * @return TC_EXPORT_OK or TC_EXPORT_ERROR
 */
static int tc_audio_init_raw(vob_t *vob)
{
    if(vob->pass_flag & TC_AUDIO)
    {
        avi_t *avifile;

        avifile = AVI_open_input_file(vob->audio_in_file, 1);
        if(avifile != NULL)
        {
            /* set correct pass-through track: */
            AVI_set_audio_track(avifile, vob->a_track);

            /*
             * small hack to fix incorrect samplerates caused by
             * transcode < 0.5.0-20011109
             */
            if (vob->mp3frequency==0)
                vob->mp3frequency=AVI_audio_rate(avifile);

            avi_aud_rate    =  vob->mp3frequency;
            avi_aud_chan    =  AVI_audio_channels(avifile);
            avi_aud_bits    =  AVI_audio_bits(avifile);
            avi_aud_codec   =  AVI_audio_format(avifile);
            avi_aud_bitrate =  AVI_audio_mp3rate(avifile);

            AVI_close(avifile);
        } else {
            AVI_print_error("avi open error");
            return(TC_EXPORT_ERROR);
        }
    } else
        tc_audio_encode_function=tc_audio_mute;

    return(TC_EXPORT_OK);
}


/**
 * init audio encoder
 *
 * @param vob
 * @param v is TC_DEBUG for verbose output or 0
 *
 * @return  TC_EXPORT_OK or TC_EXPORT_ERROR
 */

int tc_audio_init(vob_t *vob, int v)
{
    int ret=TC_EXPORT_OK;
    int sample_size;
    verbose_flag=v;

    /* Default */
    avi_aud_bitrate = vob->mp3bitrate;
    avi_aud_codec = vob->ex_a_codec;

    avi_aud_bits=vob->dm_bits;
    avi_aud_chan=vob->dm_chan;
    avi_aud_rate=(vob->mp3frequency != 0)?vob->mp3frequency:vob->a_rate;

    lame_flush=vob->encoder_flush;

    /* For encoding */
    sample_size = avi_aud_bits * 8 * avi_aud_chan;

    /*
     * Sanity checks
     */
    if((sample_size == 0) &&
       (vob->im_a_codec != CODEC_NULL))
    {
        tc_warn("Zero sample size detected for audio format `0x%x'. "
              "Muting.", vob->im_a_codec);
        tc_audio_encode_function=tc_audio_mute;
        return(TC_EXPORT_OK);
    }

    output = malloc (OUTPUT_SIZE);
    input  = malloc (INPUT_SIZE);
    if (!output || !input) {
        tc_log_error(__FILE__, "(%s:%d) Out of memory", __FILE__, __LINE__);
        return (TC_EXPORT_ERROR);
    }


    /* paranoia... */
    memset (output, 0, OUTPUT_SIZE);
    memset (input, 0, INPUT_SIZE);

    if (verbose_flag & TC_DEBUG)
        tc_info("Audio submodule in=0x%x out=0x%x", vob->im_a_codec, vob->ex_a_codec);

    switch(vob->im_a_codec)
    {
    case CODEC_PCM:
        switch(vob->ex_a_codec)
        {
        case CODEC_NULL:
            tc_audio_encode_function = tc_audio_mute;
            break;

        case CODEC_MP3:
            ret=tc_audio_init_lame(vob, vob->ex_a_codec);
            tc_audio_encode_function = tc_audio_encode_mp3;
            break;

        case CODEC_PCM:
            tc_info("PCM -> PCM");
            /* adjust bitrate with magic ! */
            avi_aud_bitrate=(vob->a_rate*4)/1000*8;
            tc_audio_encode_function = tc_audio_pass_through_pcm;
            break;

        case CODEC_MP2:
            tc_info("PCM -> MP2");
            ret=tc_audio_init_ffmpeg(vob, vob->ex_a_codec);
            tc_audio_encode_function = tc_audio_encode_ffmpeg;
            break;

        case CODEC_AC3:
            tc_info("PCM -> AC3");
            ret=tc_audio_init_ffmpeg(vob, vob->ex_a_codec);
            tc_audio_encode_function = tc_audio_encode_ffmpeg;
            break;

        default:
            tc_warn("Conversion not supported (in=0x%x out=0x%x)",
                    vob->im_a_codec, vob->ex_a_codec);
            ret=TC_EXPORT_ERROR;
            break;
        }
        break;

    case CODEC_MP2:
    case CODEC_MP3: /* only pass through supported */
        switch(vob->ex_a_codec)
        {
        case CODEC_NULL:
            tc_audio_encode_function = tc_audio_mute;
            break;

        case CODEC_MP2:
        case CODEC_MP3:
            tc_audio_encode_function = tc_audio_pass_through;
            break;

        default:
            tc_warn("Conversion not supported (in=x0%x out=x0%x)",
                    vob->im_a_codec, vob->ex_a_codec);
            ret=TC_EXPORT_ERROR;
            break;
        }
        break;

    case CODEC_AC3: /* only pass through supported */
        switch(vob->ex_a_codec)
        {
        case CODEC_NULL:
            tc_audio_encode_function = tc_audio_mute;
            break;

        case CODEC_AC3:
            tc_info("AC3->AC3");
            if (vob->audio_file_flag) {
                tc_audio_encode_function = tc_audio_pass_through;
            } else {
                tc_audio_encode_function = tc_audio_pass_through_ac3;
            }
            /*
             *the bitrate can only be determined in the encoder
             * section. `bitrate_flags' will be set to 1 after
             * bitrate is determined.
             */
            break;
        default:
            tc_warn("Conversion not supported (in=0x%x out=0x%x)",
                    vob->im_a_codec, vob->ex_a_codec);
            ret=TC_EXPORT_ERROR;
            break;
        }
        break;

    case CODEC_NULL: /* no audio requested */
        tc_audio_encode_function = tc_audio_mute;
        break;

    case CODEC_RAW: /* pass-through mode */
        tc_audio_encode_function = tc_audio_pass_through;
        ret=tc_audio_init_raw(vob);
        break;

    default:
        tc_warn("Conversion not supported (in=x0%x out=x0%x)",
              vob->im_a_codec, vob->ex_a_codec);
        ret=TC_EXPORT_ERROR;
        break;
    }

    return(ret);
}


/**
 * open audio output file
 *
 * @param vob
 * @param avifile
 *
 * @return TC_EXPORT_OK or TC_EXPORT_ERROR
 */
int tc_audio_open(vob_t *vob, avi_t *avifile)
{
    if (tc_audio_encode_function != tc_audio_mute)
    {
        if(vob->audio_file_flag)
        {
            if(! fd)
            {
                if (vob->audio_out_file[0] == '|')
                {
                    fd = popen(vob->audio_out_file+1, "w");
                    if (! fd)
                    {
                        tc_warn("Cannot popen() audio "
                                "file `%s'",
                                vob->audio_out_file+1);
                        return(TC_EXPORT_ERROR);
                    }
                    is_pipe = 1;
                } else {
                    fd = fopen(vob->audio_out_file, "w");
                    if (! fd)
                    {
                        tc_warn("Cannot open() audio "
                              "file `%s'",
                              vob->audio_out_file);
                        return(TC_EXPORT_ERROR);
                    }
                }
            }

            if (verbose_flag & TC_DEBUG)
                tc_info("Sending audio output to %s",
                        vob->audio_out_file);
        } else {

            if(avifile==NULL)
            {
                tc_audio_encode_function = tc_audio_mute;
                tc_info("No option `-m' found. Muting sound.");
                return(TC_EXPORT_OK);
            }

            AVI_set_audio(avifile,
                      avi_aud_chan,
                      avi_aud_rate,
                      avi_aud_bits,
                      avi_aud_codec,
                      avi_aud_bitrate);
            AVI_set_audio_vbr(avifile, vob->a_vbr);

            if (vob->avi_comment_fd > 0)
                AVI_set_comment_fd(avifile,
                           vob->avi_comment_fd);

            if(avifile2 == NULL)
                avifile2 = avifile; /* save for close */

            if (verbose_flag & TC_DEBUG)
                tc_info("AVI stream: format=0x%x, rate=%ld Hz, "
                        "bits=%d, channels=%d, bitrate=%d",
                        avi_aud_codec,
                        avi_aud_rate,
                        avi_aud_bits,
                        avi_aud_chan,
                        avi_aud_bitrate);
        }
    }

    return(TC_EXPORT_OK);
}


/**
 * Write audio data to output stream
 */
static int tc_audio_write(char *buffer, size_t size, avi_t *avifile)
{
    if (fd != NULL) {
        if (fwrite(buffer, size, 1, fd) != 1) {
            tc_warn("Audio file write error (errno=%d) [%s].", errno, strerror(errno));
            return(TC_EXPORT_ERROR);
        }
    } else {
        if (AVI_write_audio(avifile, buffer, size) < 0) {
            AVI_print_error("AVI file audio write error");
            return(TC_EXPORT_ERROR);
        }
    }

    return(TC_EXPORT_OK);
}


/**
 * encode audio frame in MP3 format
 *
 * @param aud_buffer is the input buffer ?
 * @param aud_size is the input buffer length
 * @param avifile is the output stream
 *
 * @return
 *
 * How this code works:
 *
 * We always encode raw audio chunks which are MP3_CHUNK_SZ (==2304)
 * bytes large. `input' contains the raw audio buffer contains
 * the encoded audio
 *
 * It is possible (very likely) that lame cannot produce a valid mp3
 * chunk per "audio frame" so we do not write out any compressed audio.
 * We need to buffer the not consumed input audio where another 2304
 * bytes chunk won't fit in AND we need to buffer the already encoded
 * but not enough audio.
 *
 * To find out how much we actually need to encode we decode the mp3
 * header of the recently encoded audio chunk and read out the actual
 * length.
 *
 * Then we write the audio. There can either be more than one valid mp3
 * frame in buffer and/or still enough raw data left to encode one.
 *
 * Easy, eh? -- tibit.
 */
static int tc_audio_encode_mp3(char *aud_buffer, int aud_size, avi_t *avifile)
{
#ifdef HAVE_LAME
    int outsize=0;
    int count=0;

    /*
     * Apend the new incoming audio to the already available but not yet
     * consumed.
     */
        ac_memcpy (input+input_len, aud_buffer, aud_size);
    input_len += aud_size;
    if (verbose_flag & TC_DEBUG)
        tc_info("audio_encode_mp3: input buffer size=%d", input_len);

    /*
     * As long as lame doesn't return encoded data (lame needs to fill its
     * internal buffers) AND as long as there is enough input data left.
     */
    while(input_len >= MP3_CHUNK_SZ)
    {
        if(IS_AUDIO_MONO)
        {
            outsize = lame_encode_buffer(
                lgf,
                (short int *)(input+count*MP3_CHUNK_SZ),
                (short int *)(input+count*MP3_CHUNK_SZ),
                MP3_CHUNK_SZ/2,
                output+output_len,
                OUTPUT_SIZE - output_len);
        } else {
            outsize = lame_encode_buffer_interleaved(
                lgf,
                (short int *) (input+count*MP3_CHUNK_SZ),
                MP3_CHUNK_SZ/4,
                output + output_len,
                OUTPUT_SIZE - output_len);
        }

        if(outsize < 0)
        {
            tc_warn("Lame encoding error: (%s)",
                    lame_error2str(outsize));
            return(TC_EXPORT_ERROR);
        }

        output_len += outsize;
        input_len  -= MP3_CHUNK_SZ;

        ++count;

        if (verbose_flag & TC_DEBUG)
            tc_info("Encoding: count=%d outsize=%d output_len=%d "
                "consumed=%d",
                count, outsize, output_len, count*MP3_CHUNK_SZ);
    }
    /* Update input */
    memmove(input, input+count*MP3_CHUNK_SZ, input_len);


    if (verbose_flag & TC_DEBUG)
        tc_info("output_len=%d input_len=%d count=%d",
            output_len, input_len, count);

    /* If we don't have any output data, there's nothing to do */
    if (output_len == 0) {
        return(TC_EXPORT_OK);
    }

    /*
     * Now, it's time to write mp3 data to output stream...
     */
    if (IS_VBR)
    {
        int offset=0;
        int size;

        /*
         * In VBR mode, we should write _complete_ chunk. And their
         * size may change from one to other... So we should analyse
         * each one and write it if enough data is avaible.
         */

        if (verbose_flag & TC_DEBUG)
            tc_info("Writing... (output_len=%d)\n", output_len);
        while((size=tc_get_mp3_header(output+offset, NULL, NULL)) > 0)
        {
            if (size > output_len)
                break;

            if (verbose_flag & TC_DEBUG)
                tc_info("Writing chunk of size=%d", size);
            tc_audio_write(output+offset, size, avifile);
            offset += size;
            output_len -= size;
        }
        memmove(output, output+offset, output_len);
        if (verbose_flag & TC_DEBUG)
            tc_info("Writing OK (output_len=%d)", output_len);
    } else {
        /*
         * in CBR mode, write our data in simplest way.
         * Thinking too much about chunk will break audio playback
         * on archos Jukebox Multimedia...
         */
        tc_audio_write(output, output_len, avifile);
        output_len=0;
    }
    return(TC_EXPORT_OK);
#else   // HAVE_LAME
    tc_warn("No Lame support available!");
    return(TC_EXPORT_ERROR);
#endif
}

static int tc_audio_encode_ffmpeg(char *aud_buffer, int aud_size, avi_t *avifile)
{
#ifdef HAVE_FFMPEG
    int  in_size, out_size;
    char *in_buf;

    //-- input buffer and amount of bytes --
    in_size = aud_size;
    in_buf  = aud_buffer;

    //-- any byte in mpa-buffer left from past call ? --
    //--------------------------------------------------
    if (mpa_buf_ptr > 0) {

      int bytes_needed, bytes_avail;

      bytes_needed = mpa_bytes_pf - mpa_buf_ptr;
      bytes_avail  = in_size;

      //-- complete frame -> encode --
      //------------------------------
      if ( bytes_avail >= bytes_needed ) {

    ac_memcpy(&mpa_buf[mpa_buf_ptr], in_buf, bytes_needed);

    TC_LOCK_LIBAVCODEC;
    out_size = avcodec_encode_audio(&mpa_ctx, (unsigned char *)output,
                    OUTPUT_SIZE, (short *)mpa_buf);
    TC_UNLOCK_LIBAVCODEC;
    tc_audio_write(output, out_size, avifile);

        in_size -= bytes_needed;
        in_buf  += bytes_needed;

        mpa_buf_ptr = 0;
      }

      //-- incomplete frame -> append bytes to mpa-buffer and return --
      //---------------------------------------------------------------
      else {

    ac_memcpy(&mpa_buf[mpa_buf_ptr], aud_buffer, bytes_avail);
        mpa_buf_ptr += bytes_avail;
        return (0);
      }
    } //bytes availabe from last call?


    //-- encode only as much "full" frames as available --
    //----------------------------------------------------

    while (in_size >= mpa_bytes_pf) {
      TC_LOCK_LIBAVCODEC;
      out_size = avcodec_encode_audio(&mpa_ctx, (unsigned char *)output,
                      OUTPUT_SIZE, (short *)in_buf);
      TC_UNLOCK_LIBAVCODEC;

      tc_audio_write(output, out_size, avifile);

      in_size -= mpa_bytes_pf;
      in_buf  += mpa_bytes_pf;
    }

    //-- hold rest of bytes in mpa-buffer --
    //--------------------------------------
    if (in_size > 0) {
      mpa_buf_ptr = in_size;
      ac_memcpy(mpa_buf, in_buf, mpa_buf_ptr);
    }

    return(TC_EXPORT_OK);
#else   // HAVE_FFMPEG
    tc_warn("No FFMPEG support available!");
    return(TC_EXPORT_ERROR);
#endif
}

static int tc_audio_pass_through_ac3(char *aud_buffer, int aud_size, avi_t *avifile)
{
    if(bitrate == 0)
    {
        int i;
        uint16_t sync_word = 0;

        /* try to determine bitrate from audio frame: */
        for(i=0;i<aud_size-3;++i)
        {
            sync_word = (sync_word << 8) + (uint8_t) aud_buffer[i];
            if(sync_word == 0x0b77)
            {
                /* from import/ac3scan.c */
                static const int bitrates[] = {
                    32, 40, 48, 56,
                    64, 80, 96, 112,
                    128, 160, 192, 224,
                    256, 320, 384, 448,
                    512, 576, 640
                };
                int ratecode = (aud_buffer[i+3] & 0x3E) >> 1;
                if (ratecode < sizeof(bitrates)/sizeof(*bitrates))
                    bitrate = bitrates[ratecode];
                break;
            }
        }

        /* assume bitrate > 0 is OK. */
        if (bitrate > 0)
        {
            AVI_set_audio_bitrate(avifile, bitrate);
            if (verbose_flag & TC_DEBUG)
                tc_info("bitrate %d kBits/s", bitrate);
        }
    }

    return(tc_audio_write(aud_buffer, aud_size, avifile));
}


/**
 *
 */
static int tc_audio_pass_through_pcm(char *aud_buffer, int aud_size, avi_t *avifile)
{
#ifdef WORDS_BIGENDIAN
    int i;
    char tmp;

    for(i=0; i<aud_size; i+=2)
    {
        tmp = aud_buffer[i+1];
        aud_buffer[i+1] = aud_buffer[i];
        aud_buffer[i] = tmp;
    }
#endif
    return(tc_audio_write(aud_buffer, aud_size, avifile));

}

/**
 *
 */
static int tc_audio_pass_through(char *aud_buffer, int aud_size, avi_t *avifile)
{
    return(tc_audio_write(aud_buffer, aud_size, avifile));

}

/**
 *
 */
static int tc_audio_mute(char *aud_buffer, int aud_size, avi_t *avifile)
{
    /*
     * Avoid Gcc to complain
     */
    (void)aud_buffer;
    (void)aud_size;
    (void)avifile;

    return(TC_EXPORT_OK);
}


/**
 * encode audio frame
 *
 * @param aud_buffer is the input buffer ?
 * @param aud_size is the input buffer length ?
 * @param avifile is the output stream ?
 *
 * @return
 */
int tc_audio_encode(char *aud_buffer, int aud_size, avi_t *avifile)
{
    assert(tc_audio_encode_function != NULL);
    return(tc_audio_encode_function(aud_buffer, aud_size, avifile));
 }


/**
 * Close audio stream
 */
int tc_audio_close()
{
    /* reset bitrate flag for AC3 pass-through */
    bitrate = 0;

    if (tc_audio_encode_function == tc_audio_encode_mp3)
    {
#ifdef HAVE_LAME
        if(lame_flush) {

            int outsize=0;

            outsize = lame_encode_flush(lgf, output, 0);

            if (verbose_flag & TC_DEBUG)
                tc_info("flushing %d audio bytes", outsize);

            if (output && outsize > 0) {
                tc_audio_write(output, outsize, avifile2);
            }
        }
#endif
    }

    if(fd)
    {
        if (is_pipe)
            pclose(fd);
        else
            fclose(fd);
        fd=NULL;
    }

    avifile2 = NULL;
    return(TC_EXPORT_OK);
}



int tc_audio_stop()
{
        if (input) {
            free(input);
            input = NULL;
        }
        if (output) {
            free(output);
            output = NULL;
        }
#ifdef HAVE_LAME
    if (tc_audio_encode_function == tc_audio_encode_mp3)
        lame_close(lgf);
#endif

#ifdef HAVE_FFMPEG
    if (tc_audio_encode_function == tc_audio_encode_ffmpeg)
    {
        //-- release encoder --
        if (mpa_codec) avcodec_close(&mpa_ctx);

        //-- cleanup buffer resources --
        if (mpa_buf) free(mpa_buf);
        mpa_buf     = NULL;
        mpa_buf_ptr = 0;

    }
#endif    

    return(0);
}


#ifdef HAVE_LAME

/**
 *
 */
static char * lame_error2str(int error)
{
    switch (error)
    {
    case -1: return "-1:  mp3buf was too small";
    case -2: return "-2:  malloc() problem";
    case -3: return "-3:  lame_init_params() not called";
    case -4: return "-4:  psycho acoustic problems";
    case -5: return "-5:  ogg cleanup encoding error";
    case -6: return "-6:  ogg frame encoding error";
    default: return "Unknown lame error";
    }
}

// from mencoder
//----------------------- mp3 audio frame header parser -----------------------

static int tabsel_123[2][3][16] = {
   { {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0},
     {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,0},
     {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,0} },

   { {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},
     {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0} }
};
static long freqs[9] = { 44100, 48000, 32000, 22050, 24000, 16000 , 11025 , 12000 , 8000 };

/*
 * return frame size or -1 (bad frame)
 */
static int tc_get_mp3_header(unsigned char* hbuf, int* chans, int* srate){
    int stereo, ssize, crc, lsf, mpeg25, framesize;
    int padding, bitrate_index, sampling_frequency;
    unsigned long newhead =
      hbuf[0] << 24 |
      hbuf[1] << 16 |
      hbuf[2] <<  8 |
      hbuf[3];


    // head_check:
    if( (newhead & 0xffe00000) != 0xffe00000 ||
        (newhead & 0x0000fc00) == 0x0000fc00){
    //tc_log_warn(__FILE__, "head_check failed");
    return -1;
    }

    if((4-((newhead>>17)&3))!=3){
      tc_warn("not layer-3");
      return -1;
    }

    if( newhead & ((long)1<<20) ) {
      lsf = (newhead & ((long)1<<19)) ? 0x0 : 0x1;
      mpeg25 = 0;
    } else {
      lsf = 1;
      mpeg25 = 1;
    }

    if(mpeg25)
      sampling_frequency = 6 + ((newhead>>10)&0x3);
    else
      sampling_frequency = ((newhead>>10)&0x3) + (lsf*3);

    if(sampling_frequency>8){
    tc_warn("invalid sampling_frequency");
    return -1;  // valid: 0..8
    }

    crc = ((newhead>>16)&0x1)^0x1;
    bitrate_index = ((newhead>>12)&0xf);
    padding   = ((newhead>>9)&0x1);
//    fr->extension = ((newhead>>8)&0x1);
//    fr->mode      = ((newhead>>6)&0x3);
//    fr->mode_ext  = ((newhead>>4)&0x3);
//    fr->copyright = ((newhead>>3)&0x1);
//    fr->original  = ((newhead>>2)&0x1);
//    fr->emphasis  = newhead & 0x3;

    stereo    = ( (((newhead>>6)&0x3)) == 3) ? 1 : 2;

    if(!bitrate_index){
      tc_warn("Free format not supported.");
      return -1;
    }

    if(lsf)
      ssize = (stereo == 1) ? 9 : 17;
    else
      ssize = (stereo == 1) ? 17 : 32;
    if(crc) ssize += 2;

    framesize = tabsel_123[lsf][2][bitrate_index] * 144000;

    if(!framesize){
    tc_warn("invalid framesize/bitrate_index");
    return -1;  // valid: 1..14
    }

    framesize /= freqs[sampling_frequency]<<lsf;
    framesize += padding;

//    if(framesize<=0 || framesize>MAXFRAMESIZE) return FALSE;
    if(srate) *srate = freqs[sampling_frequency];
    if(chans) *chans = stereo;

    return framesize;
}

#endif  // HAVE_LAME

