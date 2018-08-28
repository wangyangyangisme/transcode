/*
 *  pvm_parser.h
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

#ifndef _PVM_PARSER_H
#define _PVM_PARSER_H

#include "tc_defaults.h"    /*TC_xxxx variables*/

#define PVM_MAX_CODEC_PARAMS    3

typedef struct _pvm_config_filelist {
    char *p_codec;
    char *p_filename;
    char *p_destination;
    int s_type;
    struct _pvm_config_filelist *p_next;
} pvm_config_filelist;

typedef struct _pvm_config_merger {
    char *p_hostname;
    int s_build_only_list;
} pvm_config_merger;

typedef struct _pvm_config_hosts {
    char *p_hostname;
    int s_nproc;
    struct _pvm_config_hosts *p_next;
} pvm_config_hosts;

typedef struct _pvm_config_codec {
    char *p_codec;
    char *p_par1;
    char *p_par2;
    char *p_par3;
} pvm_config_codec;

typedef struct _pvm_config_env {
    int s_nproc;
    int s_max_proc;
    int s_num_frame_task;
    int s_build_intermed_file;
    int s_internal_multipass;
    char *p_multiplex_cmd;
    pvm_config_codec s_audio_codec;
    pvm_config_codec s_video_codec;
    pvm_config_merger s_system_merger;
    pvm_config_merger s_video_merger;
    pvm_config_merger s_audio_merger;
    pvm_config_hosts *p_pvm_hosts;
    pvm_config_filelist *p_add_list;
    pvm_config_filelist *p_add_loglist;
    pvm_config_filelist *p_rem_list;
    pvm_config_filelist s_sys_list;
} pvm_config_env;


pvm_config_env *pvm_parser_open(char *p_hostfile, int verbose, int full);
void pvm_parser_close(void);

#endif
