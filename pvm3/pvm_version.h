/*
 *  pvm_version.h
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

#ifndef _PVM_VERSION_H
#define _PVM_VERSION_H


#define EXPORT_PVM_VERSION   "v0.0.5 (2007-08-03)"
#define M_LOAD_LIB           "pvm_functions.so"

#define PVM_MAX_NODES             8
#define PVM_NUM_NODE_PROCS        255
#define PVM_MAX_NODE_PROCS        4095
#define PVM_MAX_CLUSTER_PROCS     4095
#define PVM_NUM_TASK_FRAMES       65535

#endif
