/*
 *  vob_pack_unpack.c
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#include <vob_pack_unpack.h>

#define MAX_BUF	102400

char *f_vob_pack(char *p_option,vob_t *p_vob,int *p_size)
{
	static char *p_buffer=NULL;
	vob_pack_unpack_t	*p_pup_area;

	if(!strcasecmp(p_option,"open"))
	{
		if (p_buffer==NULL)
		{
			p_buffer=(char *)calloc(MAX_BUF,1);
		}
		memset((char *)p_buffer,'\0',MAX_BUF);
		memcpy((char *)p_buffer,(char *)p_vob,sizeof(vob_t));
		*p_size=sizeof(vob_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+sizeof(vob_t));
		if (p_vob->vmod_probed!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->vmod_probed);
			memcpy(p_pup_area->p_area,p_vob->vmod_probed,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->amod_probed!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->amod_probed);
			memcpy(p_pup_area->p_area,p_vob->amod_probed,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->vmod_probed_xml!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->vmod_probed_xml);
			memcpy(p_pup_area->p_area,p_vob->vmod_probed_xml,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->amod_probed_xml!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->amod_probed_xml);
			memcpy(p_pup_area->p_area,p_vob->amod_probed_xml,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->video_in_file!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->video_in_file);
			memcpy(p_pup_area->p_area,p_vob->video_in_file,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->audio_in_file!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->audio_in_file);
			memcpy(p_pup_area->p_area,p_vob->audio_in_file,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->nav_seek_file!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->nav_seek_file);
			memcpy(p_pup_area->p_area,p_vob->nav_seek_file,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->vob_info_file!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->vob_info_file);
			memcpy(p_pup_area->p_area,p_vob->vob_info_file,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->video_out_file!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->video_out_file);
			memcpy(p_pup_area->p_area,p_vob->video_out_file,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->audio_out_file!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->audio_out_file);
			memcpy(p_pup_area->p_area,p_vob->audio_out_file,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->divxlogfile!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->divxlogfile);
			memcpy(p_pup_area->p_area,p_vob->divxlogfile,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->lame_preset!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->lame_preset);
			memcpy(p_pup_area->p_area,p_vob->lame_preset,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->audiologfile!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->audiologfile);
			memcpy(p_pup_area->p_area,p_vob->audiologfile,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->ex_v_fcc!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->ex_v_fcc);
			memcpy(p_pup_area->p_area,p_vob->ex_v_fcc,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->ex_a_fcc!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->ex_a_fcc);
			memcpy(p_pup_area->p_area,p_vob->ex_a_fcc,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->ex_profile_name!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->ex_profile_name);
			memcpy(p_pup_area->p_area,p_vob->ex_profile_name,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->mod_path!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->mod_path);
			memcpy(p_pup_area->p_area,p_vob->mod_path,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->im_v_string!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->im_v_string);
			memcpy(p_pup_area->p_area,p_vob->im_v_string,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->im_a_string!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->im_a_string);
			memcpy(p_pup_area->p_area,p_vob->im_a_string,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->ex_v_string!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->ex_v_string);
			memcpy(p_pup_area->p_area,p_vob->ex_v_string,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/
		if (p_vob->ex_a_string!=NULL)
		{
			p_pup_area->p_area=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
			p_pup_area->s_size=strlen(p_vob->ex_a_string);
			memcpy(p_pup_area->p_area,p_vob->ex_a_string,p_pup_area->s_size);
			p_pup_area->s_size+=1;	/*strlen + \0 so the strings will be terminated*/
		}
		else
			p_pup_area->s_size=1;	/*only \0*/
		*p_size=*p_size+p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+*p_size); /*pointer to the next section*/

	/* avifile_in and avifile_out are setting by the export_module*/
	/* ttime not used in export_module*/

		return(p_buffer);
	}
	else if(!strcasecmp(p_option,"close"))
	{
		free(p_buffer);
		p_buffer=NULL;
		return(NULL);
	}
	return(NULL);
}


vob_t *f_vob_unpack(char *p_option,char *p_area,int s_size)
{
	static char *p_buffer=NULL,*p_pun_buf;
	int s_cont;
	vob_pack_unpack_t	*p_pup_area;
	vob_t	*p_vob;

	if(!strcasecmp(p_option,"open"))
	{
		if (p_buffer==NULL)
		{
			p_buffer=(char *)calloc(MAX_BUF,1);
		}
		memset((char *)p_buffer,'\0',MAX_BUF);
		memcpy((char *)p_buffer,p_area,s_size);
		p_vob=(vob_t *)p_buffer;
		s_cont=sizeof(vob_t);

		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->vmod_probed=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->amod_probed=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->vmod_probed_xml=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);

		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->amod_probed_xml=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->video_in_file=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->audio_in_file=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->nav_seek_file=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->vob_info_file=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->video_out_file=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->audio_out_file=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->divxlogfile=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->lame_preset=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->audiologfile=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->ex_v_fcc=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->ex_a_fcc=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->ex_profile_name=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->mod_path=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->im_v_string=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->im_a_string=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->ex_v_string=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);
		p_pup_area=(vob_pack_unpack_t *)(p_buffer+s_cont); /*pointer to the next section*/
		p_pun_buf=(char *)((char *)p_pup_area+sizeof(vob_pack_unpack_t));
		if (p_pup_area->s_size==1)
			p_pun_buf=NULL;
		p_vob->ex_a_string=p_pun_buf;
		s_cont+=p_pup_area->s_size+sizeof(vob_pack_unpack_t);

	/* avifile_in and avifile_out are setting by the export_module*/
	/* ttime not used in export_module*/

		p_vob->avifile_in=(avi_t *)NULL;
		p_vob->avifile_out=(avi_t *)NULL;
		p_vob->ttime=NULL;

		return(p_vob);
	}
	else if(!strcasecmp(p_option,"close"))
	{
		free(p_buffer);
		p_buffer=NULL;
		return(NULL);
	}
	return(NULL);
}
