/*
 *  export_mp2enc.c
 *
 *  Georg Ludwig - January 2002
 *
 *  Parts of export_wav and export_mpeg2enc used for this file
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

#define MOD_NAME    "export_mp2enc.so"
#define MOD_VERSION "v1.0.11 (2006-03-16)"
#define MOD_CODEC   "(audio) MPEG 1/2"

#include "transcode.h"
#include "avilib/wavlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define CLAMP(x,l,h) x > h ? h : x < l ? l : x

static int 			verbose_flag	= TC_QUIET;
static int 			capability_flag	= TC_CAP_PCM;

#define MOD_PRE mp2enc
#include "export_def.h"

static FILE* 			pFile 		= NULL;
static WAV wav = NULL;

/* ------------------------------------------------------------
 *
 * open outputfile
 *
 * ------------------------------------------------------------*/

MOD_open
{
    int verb;

    /* check for mp2enc */
    if (tc_test_program("mp2enc") != 0) return (TC_EXPORT_ERROR);

    if (param->flag == TC_AUDIO)
    {
        char buf [PATH_MAX];
	char mono[] = "-m";
	char stereo[] = "-s";
	int srate, brate;
	char *chan;
	int def_srate, def_brate;
	char *def_chan;

        verb = (verbose & TC_DEBUG) ? 2:0;

	srate = (vob->mp3frequency != 0) ? vob->mp3frequency : vob->a_rate;
	brate = vob->mp3bitrate;
	chan = (vob->dm_chan>=2) ? stereo : mono;

	def_srate = srate;
	def_brate = brate;
	def_chan = chan;

	// default profile values, authority: videohelp and dvdfaq
	switch(vob->mpeg_profile) {
	case VCD_PAL: case VCD_NTSC: case VCD:
	  def_srate = 44100;
	  def_brate = 224;
	  def_chan = stereo;
	  break;
	case SVCD_PAL: case SVCD_NTSC: case SVCD:
	  def_srate = 44100;
	  def_brate = CLAMP (brate, 64, 384);
	  def_chan = stereo;
	  break;
	case XVCD_PAL: case XVCD_NTSC: case XVCD:
	  // check for invalid sample rate
	  if ((srate != 32000) && (srate != 44100) && (srate != 48000))
	  	def_srate = 44100;
	  // don't change if valid rate
	  def_brate = CLAMP (brate, 64, 384);
	  def_chan = stereo;
	  break;
	case DVD_PAL: case DVD_NTSC: case DVD:
	  def_srate = 48000;
	  def_brate = CLAMP (brate, 64, 384);
	  def_chan = stereo;
	case PROF_NONE:
	  break;
	}

	// encoding values, let commandline override profile
	if(!(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ARATE))
	  if (srate != def_srate) {
            tc_log_info(MOD_NAME, "export profile changed samplerate:"
			          " %d -> %d Hz.", srate, def_srate);
	    srate = def_srate;
	  }
	if(!(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ABITRATE))
	  if (brate != def_brate) {
            tc_log_info(MOD_NAME, "export profile changed bitrate: "
			          "%d -> %d kbps.", brate, def_brate);
	    brate = def_brate;
	  }
	if(!(vob->export_attributes & TC_EXPORT_ATTRIBUTE_ACHANS))
	  if (chan != def_chan) {
            tc_log_info(MOD_NAME, "export profile changed channels: "
			          "mono -> stereo.");
	    chan = def_chan;
	  }

	if(tc_snprintf(buf, PATH_MAX, "mp2enc -v %d -r %d -b %d %s -o \"%s\" %s", verb, srate, brate, chan, vob->audio_out_file, (vob->ex_a_string?vob->ex_a_string:"")) < 0) {
	  tc_log_perror(MOD_NAME, "cmd buffer overflow");
	  return(TC_EXPORT_ERROR);
	}

        if(verbose & TC_INFO) tc_log_info(MOD_NAME, "(%d/%d) cmd=%s",
			                  (int)strlen(buf), PATH_MAX, buf);

        if((pFile = popen (buf, "w")) == NULL)
	  return(TC_EXPORT_ERROR);

        wav = wav_fdopen(fileno(pFile), WAV_WRITE|WAV_PIPE, NULL);
        if (wav == NULL) {
      	    tc_log_perror(MOD_NAME, "open wave stream");
      	    return TC_EXPORT_ERROR;
        }

        wav_set_rate(wav, vob->a_rate);
        wav_set_bitrate(wav, vob->dm_chan*vob->a_rate*vob->dm_bits/8);
        wav_set_channels(wav, vob->dm_chan);
        wav_set_bits(wav, vob->dm_bits);

        return(0);
    }

    if (param->flag == TC_VIDEO)
	return(0);

    // invalid flag
    return(TC_EXPORT_ERROR);
}


/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
    if(param->flag == TC_AUDIO)
    {
        return(0);
    }

    if (param->flag == TC_VIDEO)
	return(0);

    // invalid flag
    return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * encode and export frame
 *
 * ------------------------------------------------------------*/


MOD_encode
{
    if(param->flag == TC_AUDIO)
    {
	if (wav_write_data(wav, param->buffer, param->size) != param->size)
        {
            tc_log_perror(MOD_NAME, "write audio frame");
            return(TC_EXPORT_ERROR);
        }
        return (0);
    }

    if (param->flag == TC_VIDEO)
        return(0);

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
    if (param->flag == TC_VIDEO)
        return (0);

    if (param->flag == TC_AUDIO)
	return (0);

    return(TC_EXPORT_ERROR);
}

/* ------------------------------------------------------------
 *
 * close codec
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->flag == TC_VIDEO)
	return (0);

    if (param->flag == TC_AUDIO)
    {
        if (wav != NULL) {
            wav_close(wav);
            wav = NULL;
        }
        if (pFile != NULL) {
    	    pclose (pFile);
            pFile = NULL;
        }

        return(0);
    }

    return (TC_EXPORT_ERROR);
}

