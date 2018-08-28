/*
 *  export_tcaud.c - generic audio export frontend module
 *  (C) 2007-2010 Francesco Romani <fromani at gmail dot com>
 *  incorporates code from former aud_aux.c, which was
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Copyright (C) Nicolas LAURENT - August 2003
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#define MOD_NAME    "export_tcaud.so"
#define MOD_VERSION "v0.9.0 (2007-07-03)"
#define MOD_CODEC   "(audio) MPEG/AC3/PCM"

#include "transcode.h"
#include "aud_aux.h"

static int verbose_flag    = TC_DEBUG;
static int capability_flag = TC_CAP_PCM|TC_CAP_AC3|TC_CAP_AUD;

#define MOD_PRE tcaud
#include "export_def.h"

/*************************************************************************/

MOD_init
{
    if (param->flag == TC_AUDIO) {
        return tc_audio_init(vob, verbose);
    }
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

MOD_open
{
    if (param->flag == TC_AUDIO) {
        return tc_audio_open(vob, vob->avifile_out);
    }
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

MOD_encode
{
    if (param->flag == TC_AUDIO) {
	vob_t *vob = tc_get_vob();
	return tc_audio_encode(param->buffer, param->size, vob->avifile_out);
    }
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

MOD_close
{
    if (param->flag == TC_AUDIO) {
	return tc_audio_close();
    }
    if (param->flag == TC_VIDEO) {
        return TC_EXPORT_OK;
    }
    return TC_EXPORT_ERROR;
}

MOD_stop
{
    if (param->flag == TC_AUDIO) {
        return tc_audio_stop();
    }
    if (param->flag == TC_VIDEO) {
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
