/*
 *  export_lame.c
 *
 *  Copyright (C) Thomas Oestreich - November 2002
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

#define MOD_NAME    "export_lame.so"
#define MOD_VERSION "v0.0.3 (2003-03-06)"
#define MOD_CODEC   "(audio) MPEG 1/2"

#include "transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static int 			verbose_flag	= TC_QUIET;
static int 			capability_flag	= TC_CAP_PCM;

#define MOD_PRE lame
#include "export_def.h"

static FILE* 			pFile 		= NULL;

/* ------------------------------------------------------------
 *
 * Pipe write helper function
 *
 * ------------------------------------------------------------*/

static int p_write (char *buf, size_t len)
{
    size_t n  = 0;
    size_t r  = 0;
    int    fd = fileno (pFile);

    while (r < len)
    {
        if ((n = write (fd, buf + r, len - r)) < 0)
	    return n;

        r += n;
    }

    return r;
}

/* ------------------------------------------------------------
 *
 * open outputfile
 *
 * ------------------------------------------------------------*/

MOD_open
{

  /* check for lame */
  if (tc_test_program("lame") != 0) return (TC_EXPORT_ERROR);

  if (param->flag == TC_AUDIO) {
    char buf [PATH_MAX];
    int ifreq,ofreq,orate;
    int verb;
    int ofreq_int;
    int ofreq_dec;
    int ochan;
    char chan;
    char *ptr;
    const char * swap_bytes = "";

    char br[64];

    /* verbose? */
    verb = (verbose & TC_DEBUG) ? 2:0;

    /* fetch audio parameter */
    ofreq = vob->mp3frequency;
    ifreq = vob->a_rate;
    orate = vob->mp3bitrate;
    ochan = vob->dm_chan;
    chan = (ochan==2) ? 'j':'m';

    /* default out freq */
    if(ofreq==0)
      ofreq = ifreq;

    /* need conversion? */
    if(ofreq!=ifreq) {
      /* add sox for conversion */
      if (tc_test_program("sox") != 0) {
        return(TC_EXPORT_ERROR);
      } else {
          tc_snprintf(buf, sizeof(buf), "sox %s -r %d -c %d -t raw - -r %d -t raw - polyphase "
                   "2>/dev/null | ",
	            (vob->dm_bits==16)?"-w -s":"-b -u",
	            ifreq, ochan, ofreq);
          ptr = buf + strlen(buf);
      }
    } else {
      ptr = buf;
    }

    /* convert output frequency to fixed point */
    ofreq_int = ofreq/1000.0;
    ofreq_dec = ofreq-ofreq_int*1000;

    /* lame command line */

#if !defined(WORDS_BIGENDIAN)
	swap_bytes = "-x";
#endif

    switch(vob->a_vbr) {

    case 1:
      tc_snprintf(br, sizeof(br), "--abr %d", orate);
      break;

    case 2:
      tc_snprintf(br, sizeof(br), "--vbr-new -b %d -B %d -V %d", orate-64, orate+64, (int) vob->mp3quality);
      break;

    case 3:
      tc_snprintf(br, sizeof(br), "--r3mix");
      break;

    default:
      tc_snprintf(br, sizeof(br), "--cbr -b %d", orate);
      break;
    }

    /* ptr is a pointer into buf */
    tc_snprintf(ptr, sizeof(buf) - (ptr-buf),
		"lame %s %s -s %d.%03d -m %c - \"%s.mp3\" 2>/dev/null %s",
		swap_bytes, br, ofreq_int, ofreq_dec, chan,
		vob->audio_out_file, (vob->ex_a_string?vob->ex_a_string:""));

    tc_log_info (MOD_NAME, "%s", buf);

    if ((pFile = popen (buf, "w")) == NULL)
      return(TC_EXPORT_ERROR);

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
        if (p_write (param->buffer, param->size) != param->size)
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
        if (pFile)
	  pclose (pFile);

	pFile = NULL;

        return(0);
    }

    return (TC_EXPORT_ERROR);
}

