/*
 *  vob_pack_unpack.h
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



#ifndef _VOB_PACK_UNPACK_H
#define _VOB_PACK_UNPACK_H

#include "transcode.h"


typedef struct _vob_pack_unpack_t {
					int	s_size;
					char	*p_area;
				} vob_pack_unpack_t;

char *f_vob_pack(char *p_option,vob_t *p_vob,int *p_size);
vob_t *f_vob_unpack(char *p_option,char *p_area,int s_size);

#endif
