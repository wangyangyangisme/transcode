/*
 *  divx4_vbr.h
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

#ifndef _DIVX4_VBR_H
#define _DIVX4_VBR_H

// methods from class VbrControl

void VbrControl_init_1pass_vbr(int quality, int crispness);
int VbrControl_init_2pass_vbr_encoding(const char* filename, int bitrate, double framerate, int crispness, int quality);
int VbrControl_init_2pass_vbr_analysis(const char* filename, int quality);

void VbrControl_update_1pass_vbr(void);
void VbrControl_update_2pass_vbr_encoding(int motion_bits, int texture_bits, int total_bits);
void VbrControl_update_2pass_vbr_analysis(int is_key_frame, int motion_bits, int texture_bits, int total_bits, int quant);

int VbrControl_get_quant(void);
void VbrControl_set_quant(float q);
int VbrControl_get_intra(void);
short VbrControl_get_drop(void);
void VbrControl_close(void);

#endif
