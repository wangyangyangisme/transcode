
/*
 *  external_codec.h
 *
 *  Copyright (C) Marzio Malanchini - August 2003
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




#ifndef _EXTERNAL_CODEC_H
#define _EXTERNAL_CODEC_H

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <pvm_parser.h>

#define MAX_BUF	1024
int f_multiplexer(char *p_codec,char *p_merge_cmd,char *p_video_filename,char *p_audio_filename,char *p_dest_file,int s_verbose);
char *f_external_suffix(char *p_codec,char *p_param);
int f_supported_export_module(char *p_codec);
char *f_supported_system(pvm_config_codec *p_v_codec,pvm_config_codec *p_a_codec);
void f_help_codec (char *p_module);
#endif
