/*
 *  aud_aux.h
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

#ifndef _AUD_AUX_H
#define _AUD_AUX_H

#include "config.h"
#include "transcode.h"
#include "avilib/avilib.h"

int tc_audio_init(vob_t *vob, int debug);
int tc_audio_open(vob_t *vob, avi_t *avifile);
int tc_audio_encode(char *aud_buffer, int aud_size, avi_t *avifile);
int tc_audio_stop(void);
int tc_audio_close(void);

#endif
