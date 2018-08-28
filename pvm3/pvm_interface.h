/*
 *  pvm_interface.h
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

#ifndef _PVM_INTERFACE_H
#define _PVM_INTERFACE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

#include <pvm_version.h>

#define PVM_DL_FUNC 1
#include <pvm_functions.h>

/* WHY we need to dlopen() this?! Why standard linkage isn't enough? -- FR */
void *f_init_pvm_func(char *p_option,void *p_ret_handle);

#endif
