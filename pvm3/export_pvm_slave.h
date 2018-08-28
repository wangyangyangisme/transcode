/*
 *  export_pvm_slave.h
 *
 *  Copyright (C) Marzio Malanchini - July 2003
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



#ifndef _EXPORT_PVM_MPEG_H
#define _EXPORT_PVM_MPEG_H

#define PVM_DL_FUNC 1
#include <pvm_functions.h>
#include <pvm_parser.h>

#define TC_VIDEO_AUDIO		0x01000000
#define TC_MULTI_VIDEO_AUDIO	0x10000000

int f_init_func(char *p_option,char *p_mod);
pvm_res_func_t *f_export_func(int s_option,char *p_buffer,int s_size,int s_seq);
int f_copy_remove_func(char *p_option,char *p_file,int s_file_dest);
int f_system_merge(pvm_config_env *p_pvm_conf);
char *f_filenamelist(char *p_option,pvm_config_env *p_pvm_conf,int s_type,int s_seq);
#endif
