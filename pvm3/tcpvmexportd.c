/*
 *  tcpvmexportd.c
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

#include "pvm_interface.h"
#include "pvm_parser.h"

#include "transcode.h"
#include "export_pvm_slave.h"
#include "external_codec.h"

#define EXE "tcpvmexportd"

#define MAX_BUF 	1024

#define MASTER_MODE	0x00000001
#define SLAVE_MODE	0x00000010
#define MERGER_MODE	0x00000100
#define CHECK_MODE	0x00001000
#define MAST_MERGE_MODE	MASTER_MODE|MERGER_MODE
#define MAST_CHECK_MODE	MASTER_MODE|CHECK_MODE
#define SLAVE_MERGE_MODE	SLAVE_MODE|MERGER_MODE


void *f_handle=NULL;	/*handle for dlopen/dlclose*/
void *f_ext_handle=NULL;	/*handle for dlopen/dlclose of the external module*/
#define tc_export p_tc_export
int (*tc_export)(int,void *,void *);
char *p_param1=NULL,*p_param2=NULL,*p_param3=NULL; /*codec input parameter*/
int tc_accel=-1;
int s_elab_type=TC_VIDEO,s_list_only=0;
char *p_out_file_name=NULL;
char *p_request_func=NULL;
char *p_hostname=NULL;
int verbose=TC_QUIET;
char *p_merge_cmd=NULL;
unsigned int tc_avi_limit=AVI_FILE_LIMIT;	/*NEED TO CHECK*/
int s_divxmultipass=0,s_internal_multipass=0;

void version()
{
  /* id string */
  fprintf(stderr, "%s (%s v%s) (C) 2001-2003 Thomas Oestreich\n",
	  EXE, PACKAGE, VERSION);
}


static void usage(int status)
{
	version();
	fprintf(stderr,"\nUsage: %s -s|-m [options]\n", EXE);
	fprintf(stderr,"\t -s             start %s in slave mode [default]\n",EXE);
	fprintf(stderr,"\t -m             start %s in master mode [off]\n",EXE);
	fprintf(stderr,"\t -j             start %s in merge mode [off]\n",EXE);
	fprintf(stderr,"\t -L             create only the merge list [off]\n");
	fprintf(stderr,"\t -p number      Multipass. [0]\n");
	fprintf(stderr,"\t -C             Check the config or merge file [off]\n");
	fprintf(stderr,"\t -M             Enable internal multipass [off]\n");
	fprintf(stderr,"\t -c name        name of slave function req in slave mode [none]\n");
	fprintf(stderr,"\t -x parameters  multiplex parameters [none]\n");
	fprintf(stderr,"\t -t type        elab video or audio frame (video|audio|system|multisystem) [video]\n");
	fprintf(stderr,"\t -f name        out file name [/tmp/my_file_name]\n");
	fprintf(stderr,"\t -1 param       first parameter to pass to the slave function [null]\n");
	fprintf(stderr,"\t -2 param       second parameter to pass to the slave function [null]\n");
	fprintf(stderr,"\t -3 param       third parameter to pass to the slave function [null]\n");
	fprintf(stderr,"\t -d level       verbose level 0|1|2 [0]\n");
	fprintf(stderr,"\t -h             print this help\n");
	fprintf(stderr,"\t -v             print version\n");
	exit(status);
}


int f_init_func(char *p_option,char *p_mod)
{
	const char *p_error;
	char *p_modpath=MOD_PATH;
	char s_module[MAX_BUF];

	if(!strcasecmp(p_option,"open-external"))
	{
		if (p_mod!=NULL)
		{
			memset(s_module,'\0',sizeof(s_module));
			tc_snprintf(s_module, sizeof(s_module), "%s/export_%s.so", p_modpath,p_mod);
			f_ext_handle=dlopen(s_module, RTLD_GLOBAL|RTLD_LAZY);
			if (!f_ext_handle)
			{
				fputs (dlerror(), stderr);
				dlclose(f_handle);
				return(1);
			}
			tc_export = dlsym(f_ext_handle, "tc_export");
			if ((p_error = dlerror()) != NULL)
			{
				fputs(p_error, stderr);
				return(1);
			}
		}
	}
	else if(!strcasecmp(p_option,"close-external"))
	{
		if (f_ext_handle!=NULL)
			dlclose(f_ext_handle);
		f_ext_handle=NULL;
		tc_export=NULL;
	}
	else if(!strcasecmp(p_option,"status-external"))
	{
		if(f_ext_handle!=NULL)
			return(1);
	}
	else if(!strcasecmp(p_option,"open"))
	{
		if (p_mod!=NULL)
		{
			tc_accel = ac_cpuinfo();
			memset(s_module,'\0',sizeof(s_module));
			tc_snprintf(s_module, sizeof(s_module), "%s/export_%s.so", p_modpath,p_mod);
			f_ext_handle=dlopen(s_module, RTLD_GLOBAL|RTLD_LAZY);
			if (!f_ext_handle)
			{
				fputs (dlerror(), stderr);
				dlclose(f_handle);
				return(1);
			}
			tc_export = dlsym(f_ext_handle, "tc_export");
			if ((p_error = dlerror()) != NULL)
			{
				fputs(p_error, stderr);
				return(1);
			}
		}
		if ((f_handle=f_init_pvm_func("open",NULL))==NULL)
			return(1);
	}
	else if(!strcasecmp(p_option,"close"))
	{
		if (f_ext_handle!=NULL)
			dlclose(f_ext_handle);
		(void *)f_init_pvm_func("close",f_handle);
		f_ext_handle=NULL;
		f_handle=NULL;
	}
	else
	{
		fprintf(stderr,"(%s) invalid command \"%s\"\n",__FILE__,p_option);
		return(1);
	}
	return(0);
}



int main(int argc,char **argv)
{
	char s_cmd;
	char *p_argv[]={"-s",(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0};
	int s_slave=0,s_cont=1,s_file_dest;
	char *p_tmp_file="/tmp/my_file_name";
	pvm_config_env *p_pvm_conf;
	pvm_config_filelist *p_my_filelist=NULL;
	char s_hostname[MAX_BUF];

	p_out_file_name=p_tmp_file;

	if((gethostname(s_hostname,sizeof(s_hostname)))!=0)
	{
		memset(s_hostname,'\0',sizeof(s_hostname));
		tc_snprintf(s_hostname,sizeof(s_hostname),"localhost-%d\n",getpid());
	}
	p_hostname=(char *)s_hostname;

	while ((s_cmd = getopt(argc, argv, "mMCLsjx:c:1:2:3:t:f:d:p:vh")) != -1)
	{
		switch (s_cmd)
		{
			case 'c':
		  		if(optarg[0]=='-')
			  		usage(EXIT_FAILURE);
		  		p_argv[s_cont++]="-c";
		  		p_argv[s_cont++]=p_request_func=optarg;
		  	break;
			case 'M':
				s_internal_multipass=1;
		  	break;
			case 'p':
				s_divxmultipass=((atoi(optarg)<0)||(atoi(optarg)>3))?0:atoi(optarg);
		  	break;
			case 'd':
				verbose=((atoi(optarg)<0)||(atoi(optarg)>2))?0:atoi(optarg);
		  	break;
			case 'C':
		  		s_slave|=CHECK_MODE;
		  	break;
			case 'L':
		  		s_list_only=1;
		  	break;
			case 'j':
		  		s_slave|=MERGER_MODE;
		  	break;
			case 's':
		  		s_slave|=SLAVE_MODE;		/*default*/
		  	break;
			case 'm':
		  		s_slave|=MASTER_MODE;
		  	break;
			case 'f':
		  		p_argv[s_cont++]="-f";
		  		p_argv[s_cont++]=p_out_file_name=optarg;
		  	break;
			case 't':
		  		p_argv[s_cont++]="-t";
		  		p_argv[s_cont++]=optarg;
				if(!strcasecmp(optarg,"video"))
					s_elab_type=TC_VIDEO;
				else if(!strcasecmp(optarg,"audio"))
					s_elab_type=TC_AUDIO;
				else if(!strcasecmp(optarg,"system"))
					s_elab_type=TC_VIDEO_AUDIO;
				else if(!strcasecmp(optarg,"multisystem"))
					s_elab_type=TC_MULTI_VIDEO_AUDIO;
				else
					usage(EXIT_FAILURE);
		  	break;
			case 'x':
		  		p_merge_cmd=optarg;	/*multiplex command*/
		  	break;
			case '1':
		  		p_param1=strtok(optarg,"\"");	/*First parameter*/
		  	break;
			case '2':
		  		p_param2=strtok(optarg,"\"");	/*Second parameter*/
		  	break;
			case '3':
		  		p_param3=strtok(optarg,"\"");	/*Third parameter*/
		  	break;
			case 'v':
		  		version();
		  		exit(0);
		  	break;
			case 'h':
		  		usage(EXIT_SUCCESS);
			default:
				usage(EXIT_FAILURE);
		}
	}

	if(optind < argc)
	{
		if(strcmp(argv[optind],"-")!=0)
			usage(EXIT_FAILURE);
        }
	if(argc==1)
		usage(EXIT_FAILURE);
	if (((s_slave==SLAVE_MODE)||(s_slave==(SLAVE_MERGE_MODE)))&&(p_request_func==NULL))
		usage(EXIT_FAILURE);
	if ((s_slave==(MAST_MERGE_MODE)||s_slave==(MAST_CHECK_MODE))&&(p_out_file_name==NULL))
		usage(EXIT_FAILURE);
	switch(s_slave)
	{
		case MAST_CHECK_MODE:
		case MAST_MERGE_MODE:
			p_pvm_conf = pvm_parser_open(p_out_file_name,verbose, 1);
			if (p_pvm_conf == NULL)
			{
				fprintf(stderr,"[%s] error checking %s\n",EXE,p_out_file_name);
				exit(1);
			}
			if (s_slave == (MAST_MERGE_MODE))
			{
				if (p_pvm_conf->s_sys_list.p_destination!=NULL)	/*syslist?*/
				{
					if(f_system_merge(p_pvm_conf))	/*if 1 then error*/
						exit(1);
				}
				else if (p_pvm_conf->p_add_list->p_destination!=NULL)
				{
					if ((s_file_dest=creat(p_pvm_conf->p_add_list->p_destination,S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH))==-1)
					{
						fprintf(stderr,"[%s] can't create %s output file.\n",EXE,p_pvm_conf->p_add_list->p_destination);
						pvm_parser_close();
                        p_pvm_conf=NULL;
						exit(1);
					}
					for (p_my_filelist=p_pvm_conf->p_add_list;p_my_filelist!=NULL;p_my_filelist=p_my_filelist->p_next)
					{
						fprintf(stderr,"[%s] merge into %s and remove file %s\n",EXE,p_pvm_conf->p_add_list->p_destination,p_my_filelist->p_filename);
						if(f_copy_remove_func("open",p_my_filelist->p_filename,s_file_dest))	/*if 1 then error*/
							exit(1);
					}
					close(s_file_dest);
					f_copy_remove_func("close",NULL,0);
				}
				else
				{
					fprintf(stderr,"[%s] no destination file name.\n",EXE);
					exit(1);
				}
				for (p_my_filelist=p_pvm_conf->p_rem_list;p_my_filelist!=NULL;p_my_filelist=p_my_filelist->p_next)
				{
					fprintf(stderr,"[%s] remove file %s\n",EXE,p_my_filelist->p_filename);
					remove(p_my_filelist->p_filename);
				}
			}
			pvm_parser_close();
			p_pvm_conf=NULL;
		break;
		case SLAVE_MODE:
		case SLAVE_MERGE_MODE:
			if((!strcasecmp(p_request_func,"mpeg2enc-mp2enc"))||(!strcasecmp(p_request_func,"mpeg-mpeg"))||(!strcasecmp(p_request_func,"avi-avi")))
			{
				if (f_init_func("open",NULL))		/*init the pvm interface*/
					exit(1);
				f_pvm_skeduler(f_export_func);
			}
			else
			{
				if (f_init_func("open",p_request_func))
					exit(1);
				f_pvm_skeduler(f_export_func);
			}
		break;
		default:
		break;
	}
	f_init_func("close",NULL);
	return(0);
}
