/*
 *  export_pvm_slave.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
# ifdef OS_DARWIN
#  include "libdldarwin/dlfcn.h"
# endif
#endif

#include "pvm_interface.h"
#include "export_pvm_slave.h"
#include "external_codec.h"
#include "vob_pack_unpack.h"
#include "transcode.h"


#define MODULE 		"pvm_functions.so"
#define MAX_BUF 	1024
#define COPY_BUFFER	10485760		/*10 MB*/

extern char *p_param1,*p_param2,*p_param3; /*codec input parameter*/
#define tc_export p_tc_export
extern int (*tc_export)(int,void *,void *);
extern int tc_accel;
extern int s_elab_type;
extern char *p_out_file_name;
extern int s_list_only,verbose;
extern char *p_request_func;
extern char *p_hostname;
extern char *p_merge_cmd;
extern int s_divxmultipass,s_internal_multipass;
extern int f_init_func(char *,char *);

static vob_t *vob;

/*start function required by some export modules*/

vob_t *tc_get_vob() {return(vob);}

void tc_outstream_rotate()
{
/*just to remove the file switch*/
}

/*end function required by some export modules*/

struct pvm_files_t	{
				int			s_seq;
				char 			s_file[2*MAX_BUF];
				char 			s_file_log[2*MAX_BUF];
				struct pvm_files_t 	*p_next;
			};

char *f_filenamelist(char *p_option,pvm_config_env *p_pvm_conf,int s_type,int s_seq)
{

	pvm_config_filelist	*p_add_list;
	int s_cont;

	s_cont=0;
	if(!strcasecmp(p_option,"filelist"))
	{
		p_add_list=p_pvm_conf->p_add_list;
	}
	else if (!strcasecmp(p_option,"loglist"))
	{
		p_add_list=p_pvm_conf->p_add_loglist;
	}
	else
		return(NULL);
	for(;p_add_list!=NULL;p_add_list=p_add_list->p_next)
	{
		if ((s_cont==s_seq)&&(p_add_list->s_type==s_type))
			return(p_add_list->p_filename);
		else if (p_add_list->s_type==s_type)
			s_cont++;
	}
	return(NULL);

}

int f_system_merge(pvm_config_env *p_pvm_conf)
{
	pvm_config_filelist *p_video_list=NULL,*p_audio_list=NULL;
	char s_buffer[MAX_BUF],*p_par=NULL;
	int s_file_dest=-1,s_count=0;

	if (p_pvm_conf->s_build_intermed_file==0)
	{
		if ((s_file_dest=creat(p_pvm_conf->s_sys_list.p_destination,S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH))==-1)
		{
			fprintf(stderr,"(%s) can't create %s output file.\n",__FILE__,p_pvm_conf->s_sys_list.p_destination);
			pvm_parser_close();
			p_pvm_conf=NULL;
			return(1);
		}
	}
	for (p_video_list=p_pvm_conf->p_add_list;((p_video_list->s_type==TC_AUDIO)&&(p_video_list!=NULL));p_video_list=p_video_list->p_next);
	for (p_audio_list=p_pvm_conf->p_add_list;((p_audio_list->s_type==TC_VIDEO)&&(p_audio_list!=NULL));p_audio_list=p_audio_list->p_next);
	if (p_video_list==NULL)
	{
		fprintf(stderr,"(%s) request a system merge without video list inside %s .\n",__FILE__,p_out_file_name);
		return(1);
	}
	else if (p_audio_list==NULL)
	{
		fprintf(stderr,"(%s) request a system merge without audio list inside %s .\n",__FILE__,p_out_file_name);
		return(1);
	}
	p_par=strtok(p_pvm_conf->p_multiplex_cmd,"\"");
	while ((p_video_list!=NULL)&&(p_audio_list!=NULL))
	{
		memset(s_buffer,'\0',sizeof(s_buffer));
		tc_snprintf(s_buffer,sizeof(s_buffer),"%s-%06d",p_pvm_conf->s_sys_list.p_destination,s_count++);
		if (f_multiplexer(p_pvm_conf->s_sys_list.p_codec,p_par,p_video_list->p_filename,p_audio_list->p_filename,s_buffer,verbose))
		{
			fprintf(stderr,"(%s) unsupported codec %s.\n",__FILE__,p_merge_cmd);
			return(1);
		}
		remove(p_video_list->p_filename);
		remove(p_audio_list->p_filename);
		if (p_pvm_conf->s_build_intermed_file==0)
		{
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) multiplex audio %s and video %s into %s\n",__FILE__,p_audio_list->p_filename,p_video_list->p_filename,s_buffer);
			if(f_copy_remove_func("open",s_buffer,s_file_dest))
				return(1);
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) merge into %s and remove file %s\n",__FILE__,p_pvm_conf->s_sys_list.p_destination,s_buffer);
		}
		p_video_list=p_video_list->p_next;
		p_audio_list=p_audio_list->p_next;
	}
	if (p_pvm_conf->s_build_intermed_file==0)
		close(s_file_dest);
	return(0);
}


int f_copy_remove_func(char *p_option,char *p_file,int s_file_dest)
{
	FILE *p_file_src;
	static char *p_buffer=NULL;
	int s_tmp;


	if(!strcasecmp(p_option,"open"))
	{
		if (p_buffer == NULL)
			p_buffer=(char *)malloc(COPY_BUFFER);
		if ((p_file_src=fopen(p_file,"r"))==NULL) /*file exist!*/
		{
			fprintf(stderr,"(%s) file %s not found\n",__FILE__,p_file);
			return(1);
		}
		while(!feof(p_file_src))	/*copy process*/
		{
			s_tmp=fread(p_buffer,1,COPY_BUFFER,p_file_src);
			write(s_file_dest,p_buffer,s_tmp);
		}
		fclose(p_file_src);
		remove(p_file);
	}
	else if (!strcasecmp(p_option,"close"))
	{
		free(p_buffer);
		p_buffer=NULL;
	}
	else
	{
		fprintf(stderr,"(%s) invalid option\n",__FILE__);
		return(1);
	}
	return(0);
}

static int f_internal_multipass(vob_t *p_vob,int s_size,int s_elab_type,transfer_t *p_export_param,char *p_input_stream)
{

	int s_file_dest,s_tmp;

	s_file_dest=-1;
	vob->divxmultipass=2;	/*setup the pass*/
	if (s_elab_type==TC_VIDEO)
		p_export_param->flag=TC_VIDEO;
	else
		p_export_param->flag=TC_AUDIO;

	if(f_init_func("open-external",p_request_func))	/*need because most of external module don't release the resources*/
	{
		fprintf(stderr,"(%s) restarting the dlopen \n",__FILE__);
		return(1);
	}
	p_export_param->flag=vob->verbose;
	tc_export(TC_EXPORT_NAME,(void *)p_export_param,NULL); /*check the capability*/
	if (s_elab_type==TC_VIDEO)
	{
		switch (vob->im_v_codec)
		{
			case CODEC_RGB:
				s_tmp=(p_export_param->flag & TC_CAP_RGB);
			break;
			case CODEC_YUV:
				s_tmp=(p_export_param->flag & TC_CAP_YUV);
			break;
			case CODEC_RAW:
			case CODEC_RAW_YUV:
				s_tmp=(p_export_param->flag & TC_CAP_VID);
			break;
			default:
				s_tmp=0;
		}
		p_export_param->flag=TC_VIDEO;
	}
	else	/*audio codec*/
	{
		switch (vob->im_a_codec)
		{
			case CODEC_PCM:
				s_tmp=(p_export_param->flag & TC_CAP_PCM);
			break;
			case CODEC_AC3:
				s_tmp=(p_export_param->flag & TC_CAP_AC3);
			break;
			case CODEC_RAW:
				s_tmp=(p_export_param->flag & TC_CAP_AUD);
			break;
			default:
				s_tmp=0;
		}
		p_export_param->flag=TC_AUDIO;
	}
	if (!s_tmp)
		return(1);
	if (tc_export(TC_EXPORT_INIT,(void *)p_export_param,vob)==TC_EXPORT_ERROR) /*check the capability*/
	{
		if (s_elab_type==TC_VIDEO)
			fprintf(stderr,"(%s) video export module error: init failed\n",__FILE__);
		else
			fprintf(stderr,"(%s) audio export module error: init failed\n",__FILE__);
		return(1);
	}
	if (tc_export(TC_EXPORT_OPEN,(void *)p_export_param,vob)==TC_EXPORT_ERROR)
	{
		if (s_elab_type==TC_VIDEO)
			fprintf(stderr,"(%s) video export module error: open failed\n",__FILE__);
		else
			fprintf(stderr,"(%s) audio export module error: open failed\n",__FILE__);
		return(1);
	}
	if ((s_file_dest=open(p_input_stream,O_RDONLY))==-1)
	{
		fprintf(stderr,"(%s) can't open %s raw file.\n",__FILE__,p_input_stream);
		return(1);
	}
	while (read(s_file_dest,p_export_param->buffer,s_size)==s_size)
	{
		if (tc_export(TC_EXPORT_ENCODE,(void *)p_export_param,vob)==TC_EXPORT_ERROR)
		{
			if (s_elab_type==TC_VIDEO)
				fprintf(stderr,"(%s) video export module error: encode failed\n",__FILE__);
			else
				fprintf(stderr,"(%s) audio export module error: encode failed\n",__FILE__);
			return(1);
		}
	}
	if (tc_export(TC_EXPORT_CLOSE,(void *)p_export_param,NULL)==TC_EXPORT_ERROR)
	{
		if (s_elab_type==TC_VIDEO)
			fprintf(stderr,"(%s) video export module error: close failed\n",__FILE__);
		else
			fprintf(stderr,"(%s) audio export module error: close failed\n",__FILE__);
		return(1);
	}
	if (tc_export(TC_EXPORT_STOP,(void *)p_export_param,NULL)==TC_EXPORT_ERROR)
	{
		if (s_elab_type==TC_VIDEO)
			fprintf(stderr,"(%s) video export module error: stop failed\n",__FILE__);
		else
			fprintf(stderr,"(%s) audio export module error: stop failed\n",__FILE__);
		return(1);
	}
	if(f_init_func("close-external",p_request_func))	/*need because most of external module don't release the resources*/
	{
		fprintf(stderr,"(%s) restarting the dlopen \n",__FILE__);
		return(1);
	}
	close(s_file_dest);
	vob->divxmultipass=1;	/*reset the pass*/
	return(0);
}



pvm_res_func_t *f_export_func(int s_option,char *p_buffer,int s_size,int s_seq)
{
        static int s_cicle=0,s_serial=0;
	static pvm_res_func_t s_result={0,0,PVM_MSG_WRKN,0,NULL};
	static transfer_t s_export_param;
	int s_tmp,s_rc;
	static char s_filename[2*MAX_BUF];
	static char s_file_int_multipass[2*MAX_BUF];
	static char s_filename_log[2*MAX_BUF];
	static char *p_suffix=NULL;
	static int s_file_dest=-1,s_first_encode=0,s_encode_size=0;
	static struct pvm_files_t *p_file_elab=NULL,*p_file_erase=NULL,*p_file_erase_tmp=NULL;
	struct pvm_files_t *p_file_elab_tmp=NULL,*p_search=NULL;
	struct stat s_f_stat;
	FILE *p_file_src=NULL;
	static pvm_config_env *p_pvm_conf=NULL;
	pvm_config_filelist *p_my_filelist=NULL;
	char *p_filetmp=NULL;

	if (p_suffix==NULL)
		p_suffix=f_external_suffix(p_request_func,p_param1);	/*function codec based*/
	s_result.s_msg_type=PVM_MSG_WRKN;	/*always initialized to PVM_MSG_WRKN*/
	if (s_result.p_result==NULL)
	{
		s_result.s_dim_buffer=(s_size> 4*MAX_BUF)?s_size:4*MAX_BUF;
        	s_result.p_result=(char *)calloc(s_result.s_dim_buffer,1);
	}
	else if (s_size>s_result.s_dim_buffer)
	{
        	s_result.p_result=(char *)realloc(s_result.p_result,s_size);
		s_result.s_dim_buffer=s_size;
	}
	memset(s_result.p_result,'\0',s_result.s_dim_buffer);
	switch(s_option)
	{
		case PVM_JOIN_OPT_INIT:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_JOIN_OPT_INIT  %d\n",__FILE__,s_elab_type);
			memset(s_filename,'\0',sizeof(s_filename));
			if (!s_list_only)
				tc_snprintf(s_filename,sizeof(s_filename),"%s%s",p_out_file_name,p_suffix);
			else
			{
				if (s_elab_type==TC_VIDEO)
					tc_snprintf(s_filename,sizeof(s_filename),"%s-video.lst",p_out_file_name);
				else if (s_elab_type==TC_AUDIO)
					tc_snprintf(s_filename,sizeof(s_filename),"%s-audio.lst",p_out_file_name);
				else
					tc_snprintf(s_filename,sizeof(s_filename),"%s-system.lst",p_out_file_name);
			}
			s_result.s_msg_type=PVM_MSG_JOIN;	/*don't need a receive msg*/
			s_result.s_ret_size=0;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_JOIN_OPT_INIT  %d\n",__FILE__,s_elab_type);
		break;
		case PVM_JOIN_OPT_RUN:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_JOIN_OPT_RUN  %d\n",__FILE__,s_elab_type);
			if ((s_elab_type==TC_VIDEO)||(s_elab_type==TC_AUDIO))	/*video and audio file*/
			{
				if ((s_file_dest=creat(s_filename,S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH))==-1)
				{
					fprintf(stderr,"can't open %s output file.\n",p_out_file_name);
					s_result.s_rc=1;
					break;
				}
				if ((s_divxmultipass==1)||(s_list_only))
				{
					memset(s_filename,'\0',sizeof(s_filename));
					if (s_elab_type==TC_VIDEO)
						tc_snprintf(s_filename,sizeof(s_filename),"[AddVideoList]\nDestination = %s%s\nCodec = %s\n",p_out_file_name,p_suffix,p_request_func);
					else
						tc_snprintf(s_filename,sizeof(s_filename),"[AddAudioList]\nDestination = %s%s\nCodec = %s\n",p_out_file_name,p_suffix,p_request_func);
					write(s_file_dest,s_filename,strlen(s_filename));
				}
				s_rc=0;
				for(p_file_elab_tmp=p_file_elab;p_file_elab_tmp!=NULL;p_file_elab_tmp=p_file_elab_tmp->p_next)
				{
					if (!s_list_only)
					{
						if (f_copy_remove_func("open",p_file_elab_tmp->s_file,s_file_dest))
						{
							s_rc=1;
						}
					}
					else
					{
						memset(s_filename,'\0',sizeof(s_filename));
						tc_snprintf(s_filename,sizeof(s_filename),"%s\n",p_file_elab_tmp->s_file);
						write(s_file_dest,s_filename,strlen(s_filename));
					}
				}
				if (s_divxmultipass==1)
				{
					memset(s_filename,'\0',sizeof(s_filename));
					if (s_elab_type==TC_VIDEO)
						tc_snprintf(s_filename,sizeof(s_filename),"[LogVideoList]\n");
					else
						tc_snprintf(s_filename,sizeof(s_filename),"[LogAudioList]\n");
					write(s_file_dest,s_filename,strlen(s_filename));
					for(p_file_elab_tmp=p_file_elab;p_file_elab_tmp!=NULL;p_file_elab_tmp=p_file_elab_tmp->p_next)
					{
						memset(s_filename_log,'\0',sizeof(s_filename_log));
						tc_snprintf(s_filename_log,sizeof(s_filename_log),"%s\n",p_file_elab_tmp->s_file_log);
						write(s_file_dest,s_filename_log,strlen(s_filename_log));
					}
				}
				if ((s_divxmultipass==1)||(s_list_only))
				{
					memset(s_filename,'\0',sizeof(s_filename));
					if (s_elab_type==TC_VIDEO)
						memcpy(s_filename,"[RemoveVideoList]\n",19);
					else
						memcpy(s_filename,"[RemoveAudioList]\n",19);
					write(s_file_dest,s_filename,strlen(s_filename));
				}
				else
					(int)f_copy_remove_func("close",NULL,0);
				for(p_file_erase_tmp=p_file_erase;p_file_erase_tmp!=NULL;p_file_erase_tmp=p_file_erase_tmp->p_next)
				{
					if (!s_list_only)
					{
						remove(p_file_erase_tmp->s_file);	/*some files are already removed*/
						if (p_file_erase_tmp->s_file_log[0]!=0)
							remove(p_file_erase_tmp->s_file_log);	/*some files are already removed*/
					}
					else
					{
						memset(s_filename,'\0',sizeof(s_filename));
						tc_snprintf(s_filename,sizeof(s_filename),"%s\n",p_file_erase_tmp->s_file);
						write(s_file_dest,s_filename,strlen(s_filename));
						if ((s_divxmultipass==1)||(s_divxmultipass==2))
						{
							if (p_file_erase_tmp->s_file_log!=NULL)
							{
								memset(s_filename_log,'\0',sizeof(s_filename_log));
								tc_snprintf(s_filename_log,sizeof(s_filename_log),"%s\n",p_file_erase_tmp->s_file_log);
								write(s_file_dest,s_filename_log,strlen(s_filename_log));
							}
						}
					}
				}
				close(s_file_dest);
				s_result.s_rc=s_rc;
			}
			else	/*system*/
			{
				memset(s_filename,'\0',sizeof(s_filename));
				tc_snprintf(s_filename,sizeof(s_filename),"%s-system.lst",p_out_file_name);
				if ((p_pvm_conf=pvm_parser_open(s_filename,verbose,1))==NULL)
				{
					fprintf(stderr,"(%s) error checking %s\n",__FILE__,s_filename);
					s_result.s_rc=1;
				}
				else
				{
					s_result.s_rc=f_system_merge(p_pvm_conf);
					for (p_my_filelist=p_pvm_conf->p_rem_list;p_my_filelist!=NULL;p_my_filelist=p_my_filelist->p_next)
					{
						if (verbose & TC_DEBUG)
							fprintf(stderr,"(%s) remove file %s\n",__FILE__,p_my_filelist->p_filename);
						remove(p_my_filelist->p_filename);
					}
					pvm_parser_close();
					p_pvm_conf=NULL;
				}
			}
			s_result.s_msg_type=PVM_MSG_WORK;	/*need a receive*/
			s_result.s_ret_size=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_JOIN_OPT_RUN  %d\n",__FILE__,s_elab_type);
		break;
		case PVM_MSG_MERG_PASTE:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_MSG_MERG_PASTE  %d\n",__FILE__,s_elab_type);
			if (s_cicle==0)
			{
				if ((s_file_dest=creat(s_filename,S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH))==-1)
				{
					fprintf(stderr,"can't open %s output file.\n",p_out_file_name);
					s_result.s_rc=1;
					break;
				}
				memset(s_filename,'\0',sizeof(s_filename));
				tc_snprintf(s_filename,sizeof(s_filename),"[SystemList]\nDestination = %s%s\nCodec = %s\nMultiplexParams = %s\nBuildOnlyIntermediateFile = %d\n",p_out_file_name,p_suffix,p_request_func,p_merge_cmd,(s_elab_type==TC_VIDEO_AUDIO)?0:1);
				write(s_file_dest,s_filename,strlen(s_filename));
			}
			write(s_file_dest,p_buffer,s_size);
			s_cicle++;
			if (s_cicle==2)		/*need to paste two files*/
			{
				close(s_file_dest);
				s_result.s_msg_type=PVM_MSG_ENDTASK_SYSTEM;	/*need a receive*/
			}
			else
				s_result.s_msg_type=PVM_MSG_WRKN;	/*need a receive*/
			s_result.s_ret_size=0;		/*nothing to send*/
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_MSG_MERG_PASTE  %d\n",__FILE__,s_elab_type);
		break;
		case PVM_JOIN_OPT_SENDFILE:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_JOIN_OPT_SENDFILE  %d\n",__FILE__,s_elab_type);
			memset(s_filename,'\0',sizeof(s_filename));
			if (s_elab_type==TC_VIDEO)
				tc_snprintf(s_filename,sizeof(s_filename),"%s-video.lst",p_out_file_name);
			else
				tc_snprintf(s_filename,sizeof(s_filename),"%s-audio.lst",p_out_file_name);
			p_file_src=fopen(s_filename,"r"); /*file exist!*/
			stat(s_filename,&s_f_stat);
			if (s_f_stat.st_size>s_result.s_dim_buffer)
			{
				s_result.p_result=(char *)realloc(s_result.p_result,s_f_stat.st_size);
				s_result.s_dim_buffer=s_f_stat.st_size;
			}
			memset(s_result.p_result,'\0',s_result.s_dim_buffer);
			fread(s_result.p_result,1,s_f_stat.st_size,p_file_src);
			fclose(p_file_src);
			s_result.s_msg_type=PVM_MSG_MERG_SEND;
			s_result.s_ret_size=s_f_stat.st_size;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_JOIN_OPT_SENDFILE  %d\n",__FILE__,s_elab_type);
		break;
		case PVM_JOIN_OPT_ADD_REMOVE:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_JOIN_OPT_ADD_REMOVE  %d\n",__FILE__,s_elab_type);
			if (p_file_erase==NULL)
			{
				p_file_erase=(struct pvm_files_t *)calloc(sizeof(struct pvm_files_t),1);
				p_file_erase_tmp=p_file_erase;
			}
			else
			{
				p_file_erase_tmp->p_next=(struct pvm_files_t *)calloc(sizeof(struct pvm_files_t),1);
				p_file_erase_tmp=p_file_erase_tmp->p_next;
			}
			p_file_erase_tmp->s_seq=s_seq;
			memcpy(p_file_erase_tmp->s_file,p_buffer,strlen(p_buffer));
			if (*p_suffix!='\0')
				memcpy(p_file_erase_tmp->s_file+strlen(p_buffer),p_suffix,strlen(p_suffix));
			if ((s_divxmultipass==1)||(s_divxmultipass==2))
				memcpy(p_file_erase_tmp->s_file_log,p_buffer+strlen(p_buffer)+1,s_size-(strlen(p_buffer)+1));
			s_result.s_msg_type=PVM_MSG_JOIN;
			s_result.s_ret_size=0;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_JOIN_OPT_ADD_REMOVE  %d\n",__FILE__,s_elab_type);
		break;
		case PVM_JOIN_OPT_ADD_ELAB:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_JOIN_OPT_ADD_ELAB  %d\n",__FILE__,s_elab_type);
			/*need to order for seq number*/
			if (p_file_elab==NULL)
			{
				p_file_elab_tmp=(struct pvm_files_t *)calloc(sizeof(struct pvm_files_t),1);
				p_file_elab=p_file_elab_tmp;
			}
			else
			{
				p_file_elab_tmp=p_file_elab;
				for (p_search=p_file_elab;p_search!=NULL;p_search=p_search->p_next)
				{
					if (p_search->s_seq >s_seq)
						break;
					else
						p_file_elab_tmp=p_search;
				}
				if (p_search==p_file_elab)
				{
					p_file_elab_tmp=(struct pvm_files_t *)calloc(sizeof(struct pvm_files_t),1);
					p_file_elab_tmp->p_next=p_file_elab;
					p_file_elab=p_file_elab_tmp;
				}
				else
				{
					p_file_elab_tmp->p_next=(struct pvm_files_t *)calloc(sizeof(struct pvm_files_t),1);
					p_file_elab_tmp=p_file_elab_tmp->p_next;
					p_file_elab_tmp->p_next=p_search;
				}
			}
			p_file_elab_tmp->s_seq=s_seq;
			memcpy(p_file_elab_tmp->s_file,p_buffer,strlen(p_buffer));
			if (*p_suffix!='\0')
				memcpy(p_file_elab_tmp->s_file+strlen(p_buffer),p_suffix,strlen(p_suffix));
			if (s_divxmultipass==1)
				memcpy(p_file_elab_tmp->s_file_log,p_buffer+strlen(p_buffer)+1,s_size-(strlen(p_buffer)+1));
			s_result.s_msg_type=PVM_MSG_JOIN;	/*don't need a receive msg*/
			s_result.s_ret_size=0;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_JOIN_OPT_ADD_ELAB  %d\n",__FILE__,s_elab_type);
		break;
		case PVM_EXP_OPT_INIT:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_EXP_OPT_INIT  %d\n",__FILE__,s_elab_type);
			if (s_size<sizeof(vob_t))
			{
				fprintf(stderr,"invalid vob_t size\n");
				s_result.s_msg_type=PVM_MSG_CONF;
				s_result.s_ret_size=0;
				s_result.s_rc=1;
			}
			else
			{
				vob=f_vob_unpack("open",p_buffer,s_size);
				vob->ex_v_fcc=p_param1;		/*override the default*/
				vob->ex_a_fcc=p_param2;		/*override the default*/
				vob->ex_profile_name=p_param3;	/*override the default*/
				vob->audio_file_flag=1;	/*force audio track on different file*/
				vob->verbose=verbose;
				memset(s_filename_log,'\0',sizeof(s_filename_log));
				tc_snprintf(s_filename_log,sizeof(s_filename_log),"%s-log-%s-%d-%d%s",p_out_file_name,p_hostname,getpid(),s_serial,p_suffix);
				if (s_internal_multipass)
				{
					vob->divxmultipass=1;		/*force multipass*/
				}
				if (vob->divxmultipass==2)		/*check the config file for multipass*/
				{
					if (p_pvm_conf==NULL)		/*check the config file for multipass*/
					{
						if (s_elab_type==TC_VIDEO)
							tc_snprintf(s_filename,sizeof(s_filename),"%s-video.lst",p_out_file_name);
						else
							tc_snprintf(s_filename,sizeof(s_filename),"%s-audio.lst",p_out_file_name);
						if ((p_pvm_conf=pvm_parser_open(s_filename,verbose,1))==NULL)	/*retreive the right config file*/
						{
							fprintf(stderr,"(%s) error checking %s\n",__FILE__,s_filename);
							s_result.s_rc=1;
							s_result.s_msg_type=PVM_MSG_CONF_JOIN;
							break;
						}
					}
					/*retreive the correct sequence of filename and log*/
					if((p_filetmp=f_filenamelist("loglist",p_pvm_conf,s_elab_type,s_seq))==NULL)	/*return the right file name*/
					{
						fprintf(stderr,"(%s) invalid sequence number %d\n",__FILE__,s_seq);
						s_result.s_rc=1;
						s_result.s_msg_type=PVM_MSG_CONF_JOIN;
						break;
					}
					memset(s_filename_log,'\0',sizeof(s_filename_log));
					memcpy(s_filename_log,p_filetmp,strlen(p_filetmp));
				}
				s_export_param.flag=verbose;	/*verbose option*/
				if(!s_cicle)
				{
					s_cicle++;
					fprintf(stderr,"(%s) on host %s pid %d recall ",__FILE__,p_hostname,getpid());
					memset(s_result.p_result,'\0',s_result.s_dim_buffer);
				}
				tc_export(TC_EXPORT_NAME,&s_export_param,NULL); /*check the capability*/
				if (s_elab_type==TC_VIDEO)
				{
					switch (vob->im_v_codec)
					{
						case CODEC_RGB:
							s_tmp=(s_export_param.flag & TC_CAP_RGB);
						break;
						case CODEC_YUV:
							s_tmp=(s_export_param.flag & TC_CAP_YUV);
						break;
						case CODEC_RAW:
						case CODEC_RAW_YUV:
							s_tmp=(s_export_param.flag & TC_CAP_VID);
						break;
						default:
							s_tmp=0;
					}
					s_export_param.flag=TC_VIDEO;
					if ((vob->divxmultipass==1)||(vob->divxmultipass==2))
						vob->divxlogfile=s_filename_log;
				}
				else	/*audio codec*/
				{
					switch (vob->im_a_codec)
					{
						case CODEC_PCM:
							s_tmp=(s_export_param.flag & TC_CAP_PCM);
						break;
						case CODEC_AC3:
							s_tmp=(s_export_param.flag & TC_CAP_AC3);
						break;
						case CODEC_RAW:
							s_tmp=(s_export_param.flag & TC_CAP_AUD);
						break;
						default:
							s_tmp=0;
					}
					s_export_param.flag=TC_AUDIO;
					if ((vob->divxmultipass==1)||(vob->divxmultipass==2))
						vob->audiologfile=s_filename_log;
				}
				if (!s_tmp)
				{
					s_result.s_msg_type=PVM_MSG_CONF;
					s_result.s_ret_size=0;
					s_result.s_rc=1;	/*capability unsupported*/
					fprintf(stderr,"(%s) unsupported codec %d",__FILE__,vob->im_v_codec);
					break;
				}
				if (tc_export(TC_EXPORT_INIT,&s_export_param,vob)==TC_EXPORT_ERROR) /*check the capability*/
				{
					if (s_elab_type==TC_VIDEO)
						fprintf(stderr,"(%s) video export module error: init failed\n",__FILE__);
					else
						fprintf(stderr,"(%s) audio export module error: init failed\n",__FILE__);
					s_result.s_msg_type=PVM_MSG_CONF;
					s_result.s_ret_size=0;
					s_result.s_rc=-1;
					break;
				}
				s_result.s_msg_type=PVM_MSG_CONF;
				s_result.s_ret_size=0;
				s_result.s_rc=0;
			}
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_EXP_OPT_INIT  %d\n",__FILE__,s_elab_type);
		break;
		case PVM_EXP_OPT_OPEN:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_EXP_OPT_OPEN %d\n",__FILE__,s_elab_type);
			memset(s_filename,'\0',sizeof(s_filename));
			if (vob->divxmultipass==2)		/*check the config file for multipass*/
			{
				if (p_pvm_conf==NULL)		/*check the config file for multipass*/
				{
					if (s_elab_type==TC_VIDEO)
						tc_snprintf(s_filename,sizeof(s_filename),"%s-video.lst",p_out_file_name);
					else
						tc_snprintf(s_filename,sizeof(s_filename),"%s-audio.lst",p_out_file_name);
					if ((p_pvm_conf=pvm_parser_open(s_filename,verbose,1))==NULL)	/*retreive the right config file*/
					{
						fprintf(stderr,"(%s) error checking %s\n",__FILE__,s_filename);
						s_result.s_rc=1;
						s_result.s_msg_type=PVM_MSG_CONF_JOIN;
						break;
					}
				}
				memset(s_filename,'\0',sizeof(s_filename));
				/*retreive the correct sequence of filename and log*/
				if((p_filetmp=f_filenamelist("filelist",p_pvm_conf,s_elab_type,s_seq))==NULL)	/*return the right file name*/
				{
					fprintf(stderr,"(%s) invalid sequence number %d\n",__FILE__,s_seq);
					s_result.s_rc=1;
					s_result.s_msg_type=PVM_MSG_CONF_JOIN;
					break;
				}
				if (*p_suffix!='\0')
					memcpy(s_filename,p_filetmp,(strstr(p_filetmp,p_suffix)-p_filetmp));
				else
					memcpy(s_filename,p_filetmp,strlen(p_filetmp));
			}
			else
			{
				tc_snprintf(s_filename,sizeof(s_filename),"%s-%s-%d-%d",p_out_file_name,p_hostname,getpid(),s_serial);
				s_serial++;
			}
			if (s_elab_type==TC_VIDEO)
			{
				vob->video_out_file=s_filename;
				s_export_param.flag=TC_VIDEO;
			}
			else	/*audio codec*/
			{
				vob->video_out_file=s_filename;	/*some export module require it e.g. export_mpeg*/
				vob->audio_out_file=s_filename;
				s_export_param.flag=TC_AUDIO;
			}
			if (tc_export(TC_EXPORT_OPEN,&s_export_param,vob)==TC_EXPORT_ERROR)
			{
				if (s_elab_type==TC_VIDEO)
					fprintf(stderr,"(%s) video export module error: open failed\n",__FILE__);
				else
					fprintf(stderr,"(%s) audio export module error: open failed\n",__FILE__);
				s_result.s_msg_type=PVM_MSG_CONF;
				s_result.s_ret_size=0;
				s_result.s_rc=-1;
				break;
			}
			memset(s_result.p_result,'\0',s_result.s_dim_buffer);
			if ((vob->divxmultipass==3)||(vob->divxmultipass==0)||(s_internal_multipass))
			{
				memcpy(s_result.p_result,s_filename,strlen(s_filename));
				s_result.s_ret_size=strlen(s_filename);	/*need a number !0*/
			}
			else
			{
				memcpy(s_result.p_result,s_filename,strlen(s_filename));
				memcpy(s_result.p_result+1+strlen(s_filename),s_filename_log,strlen(s_filename_log));
				s_result.s_ret_size=1+strlen(s_filename)+strlen(s_filename_log);	/*need a number !0*/
			}
			s_result.s_msg_type=PVM_MSG_CONF_JOIN;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_EXP_OPT_OPEN %d\n",__FILE__,s_elab_type);
		break;
		case PVM_EXP_OPT_ENCODE:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_EXP_OPT_ENCODE %d\n",__FILE__,s_elab_type);
			memcpy(&s_export_param,p_buffer,sizeof(transfer_t));
			s_export_param.buffer=p_buffer+sizeof(transfer_t);	/*pointer to the data*/
			if (s_elab_type==TC_VIDEO)
				s_export_param.flag=TC_VIDEO;
			else
				s_export_param.flag=TC_AUDIO;
			if (s_internal_multipass)
			{
				if (s_file_dest==-1)
				{
					memset(s_file_int_multipass,'\0',sizeof(s_file_int_multipass));
					if (s_elab_type==TC_VIDEO)
						tc_snprintf(s_file_int_multipass,sizeof(s_file_int_multipass),"%s-video.raw",p_out_file_name);
					else
						tc_snprintf(s_file_int_multipass,sizeof(s_file_int_multipass),"%s-audio.raw",p_out_file_name);
					if ((s_file_dest=creat(s_file_int_multipass,O_TRUNC|S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH))==-1)
					{
						fprintf(stderr,"can't open %s output file.\n",s_file_int_multipass);
						s_result.s_rc=1;
						break;
					}
					s_encode_size=s_export_param.size;
				}
				write(s_file_dest,(char *)s_export_param.buffer,s_encode_size);
			}
			if (tc_export(TC_EXPORT_ENCODE,&s_export_param,vob)==TC_EXPORT_ERROR)
			{
				if (s_elab_type==TC_VIDEO)
					fprintf(stderr,"(%s) video export module error: encode failed\n",__FILE__);
				else
					fprintf(stderr,"(%s) audio export module error: encode failed\n",__FILE__);
				s_result.s_msg_type=PVM_MSG_WORK;
				s_result.s_ret_size=0;
				s_result.s_rc=-1;
				break;
			}
			if (s_first_encode)
			{
				s_result.s_msg_type=PVM_MSG_WRKN;
				s_result.s_ret_size=0;
			}
			else
			{
				s_first_encode=1;
				memset(s_result.p_result,'\0',s_result.s_dim_buffer);
				if (vob->divxmultipass!=1)
				{
					s_result.s_ret_size=strlen(s_filename);        /*need a number !0*/
					memcpy(s_result.p_result,s_filename,s_result.s_ret_size);
				}
				else
				{
					memcpy(s_result.p_result,s_filename,strlen(s_filename));
					memcpy(s_result.p_result+1+strlen(s_filename),s_filename_log,strlen(s_filename_log));
					s_result.s_ret_size=1+strlen(s_filename)+strlen(s_filename_log);	/*need a number !0*/
				}
				s_result.s_msg_type=PVM_JOIN_OPT_ADD_ELAB;
			}
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_EXP_OPT_ENCODE %d\n",__FILE__,s_elab_type);
		break;
		case PVM_EXP_OPT_CLOSE:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_EXP_OPT_CLOSE %d\n",__FILE__,s_elab_type);
			if (s_elab_type==TC_VIDEO)
				s_export_param.flag=TC_VIDEO;
			else
				s_export_param.flag=TC_AUDIO;
			if(f_init_func("status-external",p_request_func))	/*need because most of external module don't release the resources*/
			{
				if (tc_export(TC_EXPORT_CLOSE,&s_export_param,NULL)==TC_EXPORT_ERROR)
				{
					if (s_elab_type==TC_VIDEO)
						fprintf(stderr,"(%s) video export module error: close failed\n",__FILE__);
					else
						fprintf(stderr,"(%s) audio export module error: close failed\n",__FILE__);
					s_result.s_msg_type=PVM_MSG_CONF;
					s_result.s_ret_size=0;
					s_result.s_rc=-1;
					break;
				}
			}
			s_result.s_msg_type=PVM_MSG_CONF;
			s_result.s_ret_size=0;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_EXP_OPT_CLOSE %d\n",__FILE__,s_elab_type);
		break;
		case PVM_EXP_OPT_STOP:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_EXP_OPT_STOP %d\n",__FILE__,s_elab_type);
			if (s_elab_type==TC_VIDEO)
				s_export_param.flag=TC_VIDEO;
			else
				s_export_param.flag=TC_AUDIO;
			if(f_init_func("status-external",p_request_func))	/*need because most of external module don't release the resources*/
			{
				if (tc_export(TC_EXPORT_STOP,&s_export_param,NULL)==TC_EXPORT_ERROR)
				{
					if (s_elab_type==TC_VIDEO)
						fprintf(stderr,"(%s) video export module error: stop failed\n",__FILE__);
					else
						fprintf(stderr,"(%s) audio export module error: stop failed\n",__FILE__);
					s_result.s_msg_type=PVM_MSG_CONF;
					s_result.s_ret_size=0;
					s_result.s_rc=-1;
					break;
				}
			}
			s_result.s_msg_type=PVM_MSG_CONF;
			s_result.s_ret_size=0;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_EXP_OPT_STOP %d\n",__FILE__,s_elab_type);
		break;
		case PVM_EXP_OPT_RESTART_ENCODE1:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_EXP_OPT_RESTART_ENCODE1 %d\n",__FILE__,s_elab_type);
			if (s_elab_type==TC_VIDEO)
				s_export_param.flag=TC_VIDEO;
			else
				s_export_param.flag=TC_AUDIO;
			/*force the encoder to close the file and reopen a new one*/
			if (tc_export(TC_EXPORT_CLOSE,&s_export_param,NULL)==TC_EXPORT_ERROR)
			{
				if (s_elab_type==TC_VIDEO)
					fprintf(stderr,"(%s) video export module error: close failed\n",__FILE__);
				else
					fprintf(stderr,"(%s) audio export module error: close failed\n",__FILE__);
				s_result.s_msg_type=PVM_MSG_WORK;
				s_result.s_ret_size=0;
				s_result.s_rc=-1;
				break;
			}
			if (tc_export(TC_EXPORT_STOP,&s_export_param,NULL)==TC_EXPORT_ERROR)
			{
				if (s_elab_type==TC_VIDEO)
					fprintf(stderr,"(%s) video export module error: stop failed\n",__FILE__);
				else
					fprintf(stderr,"(%s) audio export module error: stop failed\n",__FILE__);
				s_result.s_msg_type=PVM_MSG_WORK;
				s_result.s_ret_size=0;
				s_result.s_rc=-1;
				break;
			}
			if(f_init_func("close-external",p_request_func))	/*need because most of external module don't release the resources*/
			{
				fprintf(stderr,"(%s) restarting the dlopen \n",__FILE__);
				s_result.s_rc=-1;
				s_result.s_msg_type=PVM_MSG_WORK;
				break;
			}
			if (s_internal_multipass)
			{
				if (verbose & TC_DEBUG)
					fprintf(stderr,"(%s) enter in f_internal_multipass\n",__FILE__);
				close(s_file_dest);
				s_file_dest=-1;	/*reset the file desc*/
				if(f_internal_multipass(vob,s_encode_size,s_elab_type,&s_export_param,s_file_int_multipass))
				{
					s_result.s_msg_type=PVM_MSG_WORK;
					s_result.s_ret_size=0;
					s_result.s_rc=-1;
					break;
				}
				remove(s_file_int_multipass);
				remove(s_filename_log);	/*remove the log file*/
				if (verbose & TC_DEBUG)
					fprintf(stderr,"(%s) exit from f_internal_multipass\n",__FILE__);
			}
			s_result.s_rc=0;
			s_result.s_ret_size=0;
			if (s_elab_type==TC_VIDEO)
				s_result.s_msg_type=PVM_MSG_ENDTASK_VIDEO;
			else
				s_result.s_msg_type=PVM_MSG_ENDTASK_AUDIO;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_EXP_OPT_RESTART_ENCODE1 %d\n",__FILE__,s_elab_type);
		break;
		case PVM_EXP_OPT_RESTART_ENCODE2:
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) enter in PVM_EXP_OPT_RESTART_ENCODE2 %d\n",__FILE__,s_elab_type);
			if (s_elab_type==TC_VIDEO)
				s_export_param.flag=TC_VIDEO;
			else
				s_export_param.flag=TC_AUDIO;
			if (vob->divxmultipass==2)		/*check the config file for multipass*/
			{
				if (p_pvm_conf==NULL)		/*check the config file for multipass*/
				{
					if (s_elab_type==TC_VIDEO)
						tc_snprintf(s_filename,sizeof(s_filename),"%s-video.lst",p_out_file_name);
					else
					tc_snprintf(s_filename,sizeof(s_filename),"%s-audio.lst",p_out_file_name);
					if ((p_pvm_conf=pvm_parser_open(s_filename,verbose,1))==NULL)	/*retreive the right config file*/
					{
						fprintf(stderr,"(%s) error checking %s\n",__FILE__,s_filename);
						s_result.s_rc=-1;
						s_result.s_msg_type=PVM_MSG_WORK;
						break;
					}
				}
				memset(s_filename,'\0',sizeof(s_filename));
				/*retreive the correct sequence of filename and log*/
				if((p_filetmp=f_filenamelist("filelist",p_pvm_conf,s_elab_type,s_seq))==NULL)	/*return the right file name*/
				{
					fprintf(stderr,"(%s) invalid sequence number %d\n",__FILE__,s_seq);
					s_result.s_rc=-1;
					s_result.s_msg_type=PVM_MSG_WORK;
					break;
				}
				if (*p_suffix!='\0')
					memcpy(s_filename,p_filetmp,(strstr(p_filetmp,p_suffix)-p_filetmp));
				else
					memcpy(s_filename,p_filetmp,strlen(p_filetmp));
				memset(s_filename_log,'\0',sizeof(s_filename_log));
				/*retreive the correct sequence of filename and log*/
				if((p_filetmp=f_filenamelist("loglist",p_pvm_conf,s_elab_type,s_seq))==NULL)	/*return the right file name*/
				{
					fprintf(stderr,"(%s) invalid sequence number %d\n",__FILE__,s_seq);
					s_result.s_rc=-1;
					s_result.s_msg_type=PVM_MSG_WORK;
					break;
				}
				memcpy(s_filename_log,p_filetmp,strlen(p_filetmp));
			}
			else
			{
				memset(s_filename,'\0',sizeof(s_filename));
				tc_snprintf(s_filename,sizeof(s_filename),"%s-%s-%d-%d",p_out_file_name,p_hostname,getpid(),s_serial);
				if (vob->divxmultipass==1)
				{
					memset(s_filename_log,'\0',sizeof(s_filename_log));
					/*next logfile*/
					tc_snprintf(s_filename_log,sizeof(s_filename_log),"%s-log-%s-%d-%d%s",p_out_file_name,p_hostname,getpid(),s_serial,p_suffix);
				}
				s_serial++;
			}
			if (s_elab_type==TC_VIDEO)
			{
				vob->video_out_file=s_filename;
				if ((vob->divxmultipass!=3)||(vob->divxmultipass!=0))		/*check the config file for multipass*/
					vob->divxlogfile=s_filename_log;
				s_export_param.flag=TC_VIDEO;
			}
			else
			{
				vob->video_out_file=s_filename;	/*some export module require it e.g. export_mpeg*/
				vob->audio_out_file=s_filename;
				if ((vob->divxmultipass!=3)||(vob->divxmultipass!=0))		/*check the config file for multipass*/
					vob->audiologfile=s_filename_log;
				s_export_param.flag=TC_AUDIO;
			}
			if(f_init_func("open-external",p_request_func))	/*need because most of external module don't release the resources*/
			{
				fprintf(stderr,"(%s) restarting the dlopen \n",__FILE__);
				s_result.s_rc=-1;
				s_result.s_msg_type=PVM_MSG_WORK;
				break;
			}
			s_export_param.flag=verbose;	/*verbose option*/
			tc_export(TC_EXPORT_NAME,&s_export_param,NULL); /*check the capability*/
			if (s_elab_type==TC_VIDEO)
			{
				switch (vob->im_v_codec)
				{
					case CODEC_RGB:
						s_tmp=(s_export_param.flag & TC_CAP_RGB);
					break;
					case CODEC_YUV:
						s_tmp=(s_export_param.flag & TC_CAP_YUV);
					break;
					case CODEC_RAW:
					case CODEC_RAW_YUV:
						s_tmp=(s_export_param.flag & TC_CAP_VID);
					break;
					default:
						s_tmp=0;
				}
				s_export_param.flag=TC_VIDEO;
			}
			else	/*audio codec*/
			{
				switch (vob->im_a_codec)
				{
					case CODEC_PCM:
						s_tmp=(s_export_param.flag & TC_CAP_PCM);
					break;
					case CODEC_AC3:
						s_tmp=(s_export_param.flag & TC_CAP_AC3);
					break;
					case CODEC_RAW:
						s_tmp=(s_export_param.flag & TC_CAP_AUD);
					break;
					default:
						s_tmp=0;
				}
				s_export_param.flag=TC_AUDIO;
			}
			if (!s_tmp)
			{
				s_result.s_msg_type=PVM_MSG_CONF;
				s_result.s_ret_size=0;
				s_result.s_rc=1;	/*capability unsupported*/
				fprintf(stderr,"(%s) unsupported codec %d",__FILE__,vob->im_v_codec);
				break;
			}
			if (tc_export(TC_EXPORT_INIT,&s_export_param,vob)==TC_EXPORT_ERROR) /*check the capability*/
			{
				if (s_elab_type==TC_VIDEO)
					fprintf(stderr,"(%s) video export module error: init failed\n",__FILE__);
				else
					fprintf(stderr,"(%s) audio export module error: init failed\n",__FILE__);
				s_result.s_msg_type=PVM_MSG_WORK;
				s_result.s_ret_size=0;
				s_result.s_rc=-1;
				break;
			}
			if (tc_export(TC_EXPORT_OPEN,&s_export_param,vob)==TC_EXPORT_ERROR)
			{
				if (s_elab_type==TC_VIDEO)
					fprintf(stderr,"(%s) video export module error: open failed\n",__FILE__);
				else
					fprintf(stderr,"(%s) audio export module error: open failed\n",__FILE__);
				s_result.s_msg_type=PVM_MSG_WORK;
				s_result.s_ret_size=0;
				s_result.s_rc=-1;
				break;
			}
			/*finished to encode the range so the task is free: send a msg to the father and another to the merger*/
			memset(s_result.p_result,'\0',s_result.s_dim_buffer);
			if ((vob->divxmultipass==3)||(vob->divxmultipass==0)||(s_internal_multipass))		/*check the config file for multipass*/
			{
				memcpy(s_result.p_result,s_filename,strlen(s_filename));
				s_result.s_ret_size=strlen(s_filename);	/*need a number !0*/
			}
			else
			{
				memcpy(s_result.p_result,s_filename,strlen(s_filename));
				memcpy(s_result.p_result+1+strlen(s_filename),s_filename_log,strlen(s_filename_log));
				s_result.s_ret_size=1+strlen(s_filename)+strlen(s_filename_log);	/*need a number !0*/
			}
			s_result.s_msg_type=PVM_MSG_ADD_REM;
			s_first_encode=0;
			s_result.s_rc=0;
			if (verbose & TC_DEBUG)
				fprintf(stderr,"(%s) exit from PVM_EXP_OPT_RESTART_ENCODE2 %d\n",__FILE__,s_elab_type);
		break;
		default:
		break;
	}
        return((pvm_res_func_t *)&s_result);
}

