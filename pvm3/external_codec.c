/*
 *  external_codec.c
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

#include "transcode.h"
#include "external_codec.h"

#include <stdlib.h>

#define MPEG2ENC_MP2ENC_PROG	"mplex"
#define MPEG_MPEG_PROG		"tcmplex"
#define AVI_AVI_PROG		"avimerge"


static char *p_supported_modules[] = {
				"null",
				"mpeg2enc",
				"mp2enc",
				"mpeg",
				"divx5",
				"ffmpeg",
				"xvid",
				 NULL
			};

void f_help_codec (char *p_module)
{
	int s_cont;

	fprintf(stderr, "[%s]       Supported Modules\n",p_module);
	fprintf(stderr, "[%s]     --------------------\n",p_module);
	for(s_cont=0;p_supported_modules[s_cont]!=NULL;s_cont++)
	{
		if (s_cont%2)
			fprintf(stderr, "\t%s\n",p_supported_modules[s_cont]);
		else
			fprintf(stderr, "[%s]     %s",p_module,p_supported_modules[s_cont]);
	}
	if ((s_cont-1)%2)
		fprintf(stderr, "[%s]     --------------------\n",p_module);
	else
		fprintf(stderr, "\n[%s]     --------------------\n",p_module);
}

char *f_supported_system(pvm_config_codec *p_v_codec,pvm_config_codec *p_a_codec)
{
	static char *p_buffer="mpeg1audio";

	if ((!strcasecmp(p_v_codec->p_codec, "mpeg2enc"))&&(!strcasecmp(p_a_codec->p_codec, "mp2enc")))
		return((char *)"mpeg2enc-mp2enc");
	else if ((!strcasecmp(p_v_codec->p_codec, "mpeg"))&&(!strcasecmp(p_a_codec->p_codec, "mpeg")))
		return((char *)"mpeg-mpeg");
	else if ((!strcasecmp(p_v_codec->p_codec, "ffmpeg"))&&(!strcasecmp(p_a_codec->p_codec, "ffmpeg"))&&((!strcasecmp(p_v_codec->p_par1, "mpeg1video"))))
	{
		p_a_codec->p_par1=p_buffer;	/*this isn't defined but useful to known the type of the output stram*/
		return((char *)"mpeg-mpeg");
	}
	else
		return((char *)"avi-avi");
	return(NULL);
}

int f_supported_export_module(char *p_codec)
{
	int s_cont;

	for(s_cont=0;p_supported_modules[s_cont]!=NULL;s_cont++)
	{
		if (!strcasecmp(p_codec,p_supported_modules[s_cont]))
		{
			return(1);
		}
	}
	return(0);
}

int f_multiplexer(char *p_codec,char *p_merge_cmd,char *p_video_filename,char *p_audio_filename,char *p_dest_file,int s_verbose)
{
	char s_buffer[2*MAX_BUF];

	if(!strcasecmp(p_codec,"mpeg2enc-mp2enc"))
	{
		memset(s_buffer,'\0',sizeof(s_buffer));
		if (p_merge_cmd!=NULL)
			tc_snprintf(s_buffer,sizeof(s_buffer),"%s %s -o %s %s %s",MPEG2ENC_MP2ENC_PROG,p_merge_cmd,p_dest_file,p_video_filename,p_audio_filename);
		else
			tc_snprintf(s_buffer,sizeof(s_buffer),"%s -o %s %s %s",MPEG2ENC_MP2ENC_PROG,p_dest_file,p_video_filename,p_audio_filename);
		if(s_verbose & TC_DEBUG)
			fprintf(stderr,"(%s) multiplex cmd: %s\n",__FILE__,s_buffer);
		(int)system(s_buffer);
		return(0);
	}
	else if(!strcasecmp(p_codec,"mpeg-mpeg"))
	{
		memset(s_buffer,'\0',sizeof(s_buffer));
		if (p_merge_cmd!=NULL)
			tc_snprintf(s_buffer,sizeof(s_buffer),"%s %s -o %s -i %s -p %s",MPEG_MPEG_PROG,p_merge_cmd,p_dest_file,p_video_filename,p_audio_filename);
		else
			tc_snprintf(s_buffer,sizeof(s_buffer),"%s -o %s -i %s -p %s",MPEG_MPEG_PROG,p_dest_file,p_video_filename,p_audio_filename);
		if(s_verbose & TC_DEBUG)
			fprintf(stderr,"(%s) multiplex cmd: %s\n",__FILE__,s_buffer);
		(int)system(s_buffer);
		return(0);
	}
	else if(!strcasecmp(p_codec,"avi-avi"))
	{
		memset(s_buffer,'\0',sizeof(s_buffer));
		if (p_merge_cmd!=NULL)
			tc_snprintf(s_buffer,sizeof(s_buffer),"%s %s -o %s -i %s -p %s",AVI_AVI_PROG,p_merge_cmd,p_dest_file,p_video_filename,p_audio_filename);
		else
			tc_snprintf(s_buffer,sizeof(s_buffer),"%s -o %s -i %s -p %s",AVI_AVI_PROG,p_dest_file,p_video_filename,p_audio_filename);
		if(s_verbose & TC_DEBUG)
			fprintf(stderr,"(%s) multiplex cmd: %s\n",__FILE__,s_buffer);
		(int)system(s_buffer);
		return(0);
	}
	else
		return(1);
}

char *f_external_suffix(char *p_codec,char *p_param)
{

	static char *p_suffix[]={".m1v\0",".m2v\0",".mpa\0",".mpeg\0","\0",NULL};
	char s_var;
	if (p_param!=NULL)
	{
		if(!strcasecmp(p_codec,"mp2enc"))
		{
			return(p_suffix[2]);
		}
		else if((!strcasecmp(p_codec,"mpeg2enc-mp2enc"))||(!strcasecmp(p_codec,"mpeg-mpeg")))
		{
			return(p_suffix[3]);
		}
		else if((!strcasecmp(p_codec,"ffmpeg"))&&((!strcasecmp(p_param,"mpeg1video"))))
		{
			return(p_suffix[0]);
		}
		else if((!strcasecmp(p_codec,"ffmpeg"))&&((!strcasecmp(p_param,"mpeg1audio"))))
		{
			return(p_suffix[4]);
		}
		else if(!strcasecmp(p_codec,"mpeg2enc"))
		{
			s_var=p_param[0];
			if (p_param==NULL)	/*det the suffix*/
				return(p_suffix[0]);
			else if (strchr("1234568", tolower(s_var))==NULL)
				return(p_suffix[0]);
			else if (strchr("34568",tolower(s_var))!=NULL)
				return(p_suffix[1]);
			else
				return(p_suffix[0]);
		}
		else if(!strcasecmp(p_codec,"mpeg"))
		{
			s_var=p_param[0];
			if (p_param==NULL)	/*det the suffix*/
				return(p_suffix[0]);
			else if (strchr("1bvs2d", tolower(s_var))==NULL)
				return(p_suffix[0]);
			else if (strchr("1bv",tolower(s_var))!=NULL)
				return(p_suffix[0]);
			else
				return(p_suffix[1]);
		}
	}
	else
	{
		if(!strcasecmp(p_codec,"mp2enc"))
		{
			return(p_suffix[2]);
		}
		else if(!strcasecmp(p_codec,"mpeg"))	/*valid only for audio (no param)*/
		{
			return(p_suffix[2]);
		}
		else if((!strcasecmp(p_codec,"mpeg2enc-mp2enc"))||(!strcasecmp(p_codec,"mpeg-mpeg")))
		{
			return(p_suffix[3]);
		}
		else
			return(p_suffix[4]);
	}
	return(p_suffix[4]);
}

