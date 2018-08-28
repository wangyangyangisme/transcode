/*
 *  pvm_interface.c
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>


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

/* 
 * WHY we need to dlopen() this?! Why standard linkage isn't enough? 
 * Moreover: this shared module is used in just _one_ plaece (tcpvmexportd)
 * and it is _always_ used, so I don't really see the point to make it
 * a shared object. What am I missing? -- FR
 */


#include <pvm_interface.h>

#define MAX_BUF 	1024

void *f_init_pvm_func(char *p_option,void *p_ret_handle)
{
	const char *p_error;
	char s_module[MAX_BUF];
	void *p_handle;


	if(!strcasecmp(p_option,"open"))
	{
		snprintf(s_module, sizeof(s_module), "%s/%s", MOD_PATH, M_LOAD_LIB);
		p_handle=dlopen(s_module, RTLD_GLOBAL|RTLD_LAZY);
		if (!p_handle)
		{
			fputs (dlerror(), stderr);
			return(NULL);
		}
		f_pvm_start_single_process = dlsym(p_handle, "f_pvm_start_single_process");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_stop_single_process = dlsym(p_handle, "f_pvm_stop_single_process");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_master_start_stop = dlsym(p_handle, "f_pvm_master_start_stop");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_set_send = dlsym(p_handle, "f_pvm_set_send");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_send = dlsym(p_handle, "f_pvm_send");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_multi_send_nw = dlsym(p_handle, "f_pvm_multi_send_nw");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_multi_send = dlsym(p_handle, "f_pvm_multi_send");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_nrecv = dlsym(p_handle, "f_pvm_nrecv");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_recv = dlsym(p_handle, "f_pvm_recv");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_set_recv = dlsym(p_handle, "f_pvm_set_recv");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_set_nrecv = dlsym(p_handle, "f_pvm_set_nrecv");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		f_pvm_skeduler = dlsym(p_handle, "f_pvm_skeduler");
		if ((p_error = dlerror()) != NULL)
		{
			fputs(p_error, stderr);
			return(NULL);
		}
		return(p_handle);
	}
	else if(!strcasecmp(p_option,"close"))
	{
		if (p_ret_handle!=NULL)
			dlclose(p_ret_handle);
		return(NULL);
	}
	else
	{
		fprintf(stderr,"(%s) invalid command \"%s\"\n",__FILE__,p_option);
		return(NULL);
	}
}
