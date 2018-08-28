/*
 *  export_pvm.c
 *
 *  Copyright (C) Malanchini Marzio - June 2003
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

#include <stdio.h>
#include <stdlib.h>

#include "pvm_version.h"
#include "pvm_interface.h"
#include "pvm_parser.h"
#include "external_codec.h"
#include "vob_pack_unpack.h"

#define MOD_NAME    "export_pvm.so"
#define MOD_VERSION  EXPORT_PVM_VERSION
#define MOD_CODEC   "(video) * | (audio) *"

#define MAX_BUF 1024

#define MIN_TOT_NPROC   1
#define MIN_FRAME   10

static int verbose_flag=TC_QUIET;
static int capability_flag=-1; //all codecs are welcome

#define MOD_PRE pvm
#include "export_def.h"

static int s_merger_tid_audio[]={-1,-1},s_merger_tid_video[]={-1,-1},s_init_check=0;
static int s_merger_tid_system[]={-1,-1};
static int s_null_video_module=0,s_null_audio_module=0,s_sys_merger_started=-1;
static void *p_handle=NULL;
static pvm_func_t s_pvm_single_proc_audio,s_pvm_single_proc_video,s_pvm_fun_audio,s_pvm_fun_video,s_pvm_single_proc_system;
static pvm_config_env s_pvm_conf,*p_pvm_conf=NULL;

static char *p_par1=NULL,*p_par2=NULL;


extern pthread_mutex_t s_channel_lock;

static void adjust_ch(char *line, char ch)
{
  char *src = &line[strlen(line)];
  char *dst = line;

  //-- remove blanks from right and left side --
  do { src--; } while ( (src != line) && (*src == ch) );
  *(src+1) = '\0';
  src = line;
  while (*src == ch) src++;

  if (src == line) return;

  //-- copy rest --
  while (*src)
  {
    *dst = *src;
    src++;
    dst++;
  }
  *dst = '\0';
}

static void f_pvm_balancer(char *p_option,pvm_func_t *p_func,int s_seq,int s_type)
{
    int s_cont,s_dummy,s_dummy1,s_rc;

    if (!strcasecmp(p_option, "open"))
    {
        for(s_cont=0;s_cont<p_func->s_nproc;s_cont++)
            p_func->p_used_tid[s_cont]=INT_MAX; /*setup to INT_MAX*/
        p_func->s_current_tid=0;    /*First tid*/
    }
    else if (!strcasecmp(p_option, "close"))
    {
        for(s_cont=0;s_cont<p_func->s_nproc;s_cont++)
            p_func->p_used_tid[s_cont]=INT_MAX; /*setup to INT_MAX*/
    }
    else if (!strcasecmp(p_option, "set-seq"))
    {
        p_func->p_used_tid[p_func->s_current_tid]=s_seq;
    }
    else if (!strcasecmp(p_option, "first-free"))
    {
        for(s_cont=(((1+p_func->s_current_tid)==p_func->s_nproc)?0:(1+p_func->s_current_tid));((p_func->p_used_tid[s_cont]!=INT_MAX)&&(s_cont<p_func->s_current_tid));s_cont=((((1+s_cont)==p_func->s_nproc)?0:(1+s_cont))));   /*det the first task free*/
//      for(s_cont=0;((p_func->p_used_tid[s_cont]!=INT_MAX)&&(s_cont<p_func->s_nproc));s_cont++);   /*det the first task free*/
        if ((s_cont<p_func->s_nproc)&&(p_func->p_used_tid[s_cont]==INT_MAX))    /*it's already assigned to a running task?*/
        {
            p_func->s_current_tid=s_cont;   /*setting up the new task*/
            if (verbose_flag & TC_DEBUG)
            {
                if (s_type== TC_VIDEO)
                    tc_log_info(MOD_NAME, "The new task for video is %d",s_cont);
                else
                    tc_log_info(MOD_NAME, "The new task for audio is %d",s_cont);
            }
        }
        else    /*stop and wait for the first free task*/
        {
            if (verbose_flag & TC_DEBUG)
            {
                if (s_type== TC_VIDEO)
                    tc_log_info(MOD_NAME, "Waiting for a video free task...");
                else
                    tc_log_info(MOD_NAME, "Waiting for an audio free task...");
            }
            s_cont=s_seq;
            if (s_type== TC_VIDEO)
                (int)f_pvm_set_recv(PVM_MSG_ENDTASK_VIDEO); /*set up to receive the PVM_MSG_ENDTASK_VIDEO*/
            else
                (int)f_pvm_set_recv(PVM_MSG_ENDTASK_AUDIO); /*set up to receive the PVM_MSG_ENDTASK_AUDIO*/
            s_seq=f_pvm_recv(&s_dummy,(char *)&s_dummy1,&s_rc); /*Waiting for a free task */
            (int)f_pvm_set_recv(s_cont);    /*set up to receive the prev seq*/
            for(s_cont=0;p_func->p_used_tid[s_cont]!=s_seq;s_cont++);   /*det the task free*/
            p_func->s_current_tid=s_cont;   /*setting up the new task*/
            if (verbose_flag & TC_DEBUG)
            {
                if (s_type== TC_VIDEO)
                    tc_log_info(MOD_NAME, "The new video task free is %d",s_cont);
                else
                    tc_log_info(MOD_NAME, "The new audio task free is %d",s_cont);
            }
        }
    }
}

static void f_help(void)
{
    tc_log_info(MOD_NAME, "%s",MOD_VERSION);
    tc_log_info(MOD_NAME, "  -F configfile,[[nproc]:[maxproc]:[nfrxtask]]");
    tc_log_info(MOD_NAME, "  nproc,maxproc,nfrxtask override the parameter present in the config file");
    tc_log_info(MOD_NAME, "  List of known and supported codecs:");
    f_help_codec(MOD_NAME);
}

#define PVM_DEINIT do { \
    if (s_init_check == 1) { \
        void *ret = f_init_pvm_func("close", p_handle); \
        s_init_check = (ret != NULL) ?1 :0; /* uhm... -- Fromani */ \
    } else { \
        s_init_check -= 1; \
    } \
} while (0)

#define PVM_CLOSE do { \
    f_pvm_balancer("close", p_pvm_fun, 0, param->flag); \
    f_pvm_master_start_stop("close", "tcpvmexportd", NULL, \
                            p_pvm_conf->s_nproc, p_pvm_conf->s_max_proc, p_pvm_fun); \
    PVM_DEINIT; \
} while (0)

/* ------------------------------------------------------------
 *
 * open codec
 *
 * ------------------------------------------------------------*/

MOD_open
{
    pvm_func_t *p_pvm_fun=NULL;
    int s_null_module;

    pthread_mutex_lock(&s_channel_lock);    /*this is the only way to make my module work with nultithreads: need to change all the code*/
    if(param->flag == TC_VIDEO)
    {
        s_null_module=s_null_video_module;
        p_pvm_fun=&s_pvm_fun_video;
        if (verbose_flag & TC_DEBUG)
            tc_log_info(MOD_NAME, "enter in MOD_OPEN Video");
    }
    else
    {
        s_null_module=s_null_audio_module;
        p_pvm_fun=&s_pvm_fun_audio;
        if (verbose_flag & TC_DEBUG)
            tc_log_info(MOD_NAME, "enter in MOD_OPEN Video");
    }
    if (!s_null_module)
    {
        if((f_pvm_multi_send(0,(char *)0,PVM_EXP_OPT_OPEN,p_pvm_fun))==-1)
        {
            PVM_CLOSE;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
    }
    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
    if (verbose_flag & TC_DEBUG)
        tc_log_info(MOD_NAME, "exit MOD_OPEN Video");
    return TC_OK;
}

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
    char *p_file_to_open=NULL;
    char *p_tmp=NULL;
    char *p_vob_buffer=NULL;
    char *p_argv[]={"-s",(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0};
    char *p_argv_merger[]={"-s","-j",(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0};
    char *p_argv_merger_sys[]={"-s","-j",(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0,(char*)0};
    int s_cont = 1, s_contj = 2, s_contsys = 2, s_vob_buffer_size;
    pvm_config_codec    *p_conf_codec;
    pvm_func_t *p_pvm_fun=NULL;
    pvm_func_t *p_pvm_single_proc=NULL;
    int *p_merger_tid,*p_merger_sys_tid=0;
    int s_null_module=0;
    pvm_config_merger *p_merger_conf=NULL;
    char s_version[MAX_BUF];
    static int s_msys=0;

    pthread_mutex_lock(&s_channel_lock);    /*this is the only way to make my module work with nultithreads: need to change all the code*/
    if (verbose_flag & TC_DEBUG)
        tc_log_info(MOD_NAME, "enter in MOD_INIT");
    if (s_init_check==0)
    {
        s_init_check++;     //do it only for the first time
        memset((char *)&s_pvm_conf,'\0',sizeof(pvm_config_env));
        p_pvm_conf=&s_pvm_conf;
        if(vob->ex_v_fcc != NULL && strlen(vob->ex_v_fcc) != 0)
        {
            p_par1 = tc_strdup(vob->ex_v_fcc); /* ...and memleaks for all! */
            adjust_ch(p_par1, ' ');    /*-- module to recall --*/
            if(!strcasecmp(p_par1,"list"))
            {
                f_help();
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
            p_pvm_conf = pvm_parser_open(p_par1, verbose, 0);
            if (p_pvm_conf == NULL)
            {
                tc_log_warn(MOD_NAME, "error checking %s",p_par1);
                f_help();
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
            ac_memcpy((char *)&s_pvm_conf,(char *)p_pvm_conf,sizeof(pvm_config_env));
            p_pvm_conf=&s_pvm_conf;
        }
        else    //need at least the config file
        {
            f_help();
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if(vob->ex_a_fcc != NULL && strlen(vob->ex_a_fcc) != 0)
        {
            p_par2 = tc_strdup(vob->ex_a_fcc);
            adjust_ch(p_par2, ' '); /*-- first parameter of the module --*/
            if (p_par2[0]==':')
            {
                if (p_pvm_conf->s_nproc==0)
                    p_pvm_conf->s_nproc=1;  /*default nproc per host*/
                if (p_par2[1]==':')
                {
                    if (p_pvm_conf->s_max_proc==0)
                        p_pvm_conf->s_max_proc=10;
                }
                else
                {
                    p_tmp=strtok(p_par2,":");
                    if (p_tmp==NULL)
                    {
                        if (p_pvm_conf->s_max_proc==0)
                            p_pvm_conf->s_max_proc=10;
                    }
                    else
                    {
                        p_pvm_conf->s_max_proc=atoi(p_tmp);
                        p_pvm_conf->s_max_proc=(p_pvm_conf->s_max_proc<MIN_TOT_NPROC)?10:p_pvm_conf->s_max_proc;    /*the max is set to 10*/
                    }
                }
                p_tmp=strtok(NULL,":");
                if (p_tmp==NULL)
                {
                    if (p_pvm_conf->s_num_frame_task==0)
                        p_pvm_conf->s_num_frame_task=100;
                }
                else
                {
                    p_pvm_conf->s_num_frame_task=atoi(p_tmp);
                    p_pvm_conf->s_num_frame_task=(p_pvm_conf->s_num_frame_task<MIN_FRAME)?100:p_pvm_conf->s_num_frame_task; /*the to the default */
                }
            }
            else
            {
                p_pvm_conf->s_nproc=atoi(strtok(p_par2,":"));
                p_pvm_conf->s_nproc=((p_pvm_conf->s_nproc)<1)?1:p_pvm_conf->s_nproc;    /*the min is set to 2*/
                p_tmp=strtok(NULL,":");
                if (p_tmp==NULL)
                {
                    if (p_pvm_conf->s_max_proc==0)
                        p_pvm_conf->s_max_proc=10;
                }
                else
                {
                    p_pvm_conf->s_max_proc=atoi(p_tmp);
                    p_pvm_conf->s_max_proc=(p_pvm_conf->s_max_proc<MIN_TOT_NPROC)?10:p_pvm_conf->s_max_proc;    /*the max is set to 10*/
                }
                p_tmp=strtok(NULL,":");
                if (p_tmp==NULL)
                {
                    if (p_pvm_conf->s_num_frame_task==0)
                        p_pvm_conf->s_num_frame_task=100;
                }
                else
                {
                    p_pvm_conf->s_num_frame_task=atoi(p_tmp);
                    p_pvm_conf->s_num_frame_task=(p_pvm_conf->s_num_frame_task<MIN_FRAME)?100:p_pvm_conf->s_num_frame_task;
                }
            }
        }
    }
    else
    {
        s_init_check++;
    }
    if(param->flag == TC_VIDEO)
    {
        p_conf_codec=&(p_pvm_conf->s_video_codec);
        p_file_to_open = tc_strdup(vob->video_out_file);
        p_merger_conf=&(p_pvm_conf->s_video_merger);
    }
    else
    {
        p_conf_codec=&(p_pvm_conf->s_audio_codec);
        if (vob->audio_out_file !=NULL)
            p_file_to_open = tc_strdup(vob->audio_out_file);
        else
            p_file_to_open = tc_strdup(vob->video_out_file);
        p_merger_conf=&(p_pvm_conf->s_audio_merger);
    }
    if (p_pvm_conf->p_multiplex_cmd!=NULL)
    {
        p_argv_merger_sys[s_contsys++]="-x";
        p_argv_merger_sys[s_contsys++]=p_pvm_conf->p_multiplex_cmd;
    }
    if (f_supported_export_module(p_conf_codec->p_codec))
    {
        p_argv[s_cont++]="-c";
        p_argv[s_cont++]=p_conf_codec->p_codec; /*store the parameter*/
        p_argv_merger[s_contj++]="-c";
        p_argv_merger[s_contj++]=p_conf_codec->p_codec; /*store the parameter*/
        p_argv_merger_sys[s_contsys++]="-c";
        if ((p_argv_merger_sys[s_contsys++]=f_supported_system((pvm_config_codec *)&(p_pvm_conf->s_video_codec),(pvm_config_codec *)&(p_pvm_conf->s_audio_codec)))!=NULL)
        {
            if (p_pvm_conf->s_system_merger.p_hostname!=NULL)
            {
                if (vob->divxmultipass!=1)
                    s_sys_merger_started=0;
                else            /*if multipass == 1 i don't need to start the system merger*/
                {
                    s_contsys--;
                    p_argv_merger_sys[s_contsys]="unknown"; /*store the parameter*/
                    s_sys_merger_started=-1;    /*so the system merger never started*/
                }
            }
            else
            {
                s_contsys--;
                p_argv_merger_sys[s_contsys]="unknown"; /*store the parameter*/
                s_sys_merger_started=-1;    /*so the system merger never started*/
            }
        }
        else
        {
            p_argv_merger_sys[s_contsys-1]="unknown"; /*store the parameter*/
            s_sys_merger_started=-1;    /*so the system merger never started*/
        }
        if (p_conf_codec->p_par1!=NULL)
            if (p_conf_codec->p_par1[0]!=' ')
            {
                p_argv[s_cont++]="-1";
                p_argv[s_cont++]=p_conf_codec->p_par1; /*store the parameter*/
                p_argv_merger[s_contj++]="-1";
                p_argv_merger[s_contj++]=p_conf_codec->p_par1; /*store the parameter*/
                p_argv_merger_sys[s_contsys++]="-1";
                p_argv_merger_sys[s_contsys++]=p_conf_codec->p_par1; /*store the parameter*/
            }
        if (p_conf_codec->p_par2!=NULL)
            if (p_conf_codec->p_par2[0]!=' ')
            {
                p_argv[s_cont++]="-2";
                p_argv[s_cont++]=p_conf_codec->p_par2; /*store the parameter*/
            }
        if (p_conf_codec->p_par3!=NULL)
            if (p_conf_codec->p_par3[0]!=' ')
            {
                p_argv[s_cont++]="-3";
                p_argv[s_cont++]=p_conf_codec->p_par3; /*store the parameter*/
            }
    }
    else if (!strcasecmp(p_conf_codec->p_codec, "null"))
    {
        s_null_module=1;
        if(param->flag == TC_VIDEO)
        {
            s_null_video_module=s_null_module;
            tc_log_info(MOD_NAME, "use internal video null codec");
        }
        else
        {
            s_null_audio_module=s_null_module;
            tc_log_info(MOD_NAME, "use internal audio null codec");
        }
    }
    else
    {
        tc_log_warn(MOD_NAME, "unsupported %s codec",p_conf_codec->p_codec);
        f_help();       /*unsupported codec parameter*/
        pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
        return TC_ERROR;
    }
    if ((!strcasecmp(p_pvm_conf->s_audio_codec.p_codec, "null"))||(!strcasecmp(p_pvm_conf->s_video_codec.p_codec, "null")))
    {
        s_sys_merger_started=-1;    /*so the system merger never started*/
        p_pvm_conf->s_system_merger.p_hostname=NULL;
    }
    if (!s_null_module) /*skip if null module is requested*/
    {
        /*check the default setup*/
        p_pvm_conf->s_nproc=(p_pvm_conf->s_nproc<1)?1:p_pvm_conf->s_nproc;
        p_pvm_conf->s_max_proc=(p_pvm_conf->s_max_proc<MIN_TOT_NPROC)?10:p_pvm_conf->s_max_proc;
        p_pvm_conf->s_num_frame_task=(p_pvm_conf->s_num_frame_task<MIN_FRAME)?100:p_pvm_conf->s_num_frame_task;

        if ((vob->divxmultipass==0)&&(p_pvm_conf->s_internal_multipass))
        {
            p_argv[s_cont++]="-M";  /*use internale multipass only if -R option isn't set*/
        }
        else
        {
            p_argv_merger[s_contj++]="-p";
            switch(vob->divxmultipass)
            {
                case 3:
                    p_argv_merger[s_contj++]="3";
                break;
                case 2:
                    p_argv_merger[s_contj++]="2";
                break;
                case 1:
                    p_argv_merger[s_contj++]="1";
                break;
                case 0:
                default:
                    p_argv_merger[s_contj++]="0";
                break;
            }
        }
        p_argv[s_cont++]="-d";
        p_argv_merger[s_contj++]="-d";
        p_argv_merger_sys[s_contsys++]="-d";
        if (verbose == 0)
        {
            p_argv[s_cont++]="0";
            p_argv_merger[s_contj++]="0";
            p_argv_merger_sys[s_contsys++]="0";
        }
        else if (verbose==1)
        {
            p_argv[s_cont++]="1";
            p_argv_merger[s_contj++]="1";
            p_argv_merger_sys[s_contsys++]="1";
        }
        else
        {
            p_argv[s_cont++]="2";
            p_argv_merger[s_contj++]="2";
            p_argv_merger_sys[s_contsys++]="2";
        }
        if (p_file_to_open !=NULL)
        {
            p_argv[s_cont++]="-f";
            p_argv[s_cont++]=p_file_to_open; /*video/audio out file name*/
            p_argv_merger[s_contj++]="-f";
            p_argv_merger[s_contj++]=p_file_to_open; /*video/audio out file name*/
            p_argv_merger_sys[s_contsys++]="-f";
            p_argv_merger_sys[s_contsys++]=p_file_to_open;
        }
        p_argv_merger_sys[s_contsys++]="-L";    /*need to create system list every time*/
        if (p_merger_conf->s_build_only_list)
            p_argv_merger[s_contj++]="-L";
        if ((verbose_flag & TC_INFO)||(verbose_flag & TC_DEBUG))
            tc_log_info(MOD_NAME, "P1=%s, P2=%s (%d %d %d)",
                p_par1,p_par2,p_pvm_conf->s_nproc,
                p_pvm_conf->s_max_proc,
                p_pvm_conf->s_num_frame_task);
        if (p_handle==NULL)
        {
            if((p_handle=f_init_pvm_func("open",NULL))==NULL)
            {
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
        }
        p_argv[s_cont++]="-t";
        p_argv_merger[s_contj++]="-t";
        p_argv_merger_sys[s_contsys++]="-t";
        if (p_pvm_conf->s_system_merger.s_build_only_list==2)
            p_argv_merger_sys[s_contsys++]="multisystem";
        else
            p_argv_merger_sys[s_contsys++]="system";
        if (s_sys_merger_started==0)
        {
            memset((char *)&s_pvm_single_proc_system,'\0',sizeof(pvm_func_t));
            s_pvm_single_proc_system.p_slave_tids=&s_merger_tid_system[0];  /*data for the merger process*/
            s_pvm_single_proc_system.s_nproc=1;
            s_pvm_single_proc_system.s_current_tid=0;     /*data for the merger process*/
            p_merger_sys_tid=(int *)&s_merger_tid_system[0];
        }
        if(param->flag == TC_VIDEO)
        {
            p_argv[s_cont++]="video"; /*video parameter*/
            p_argv_merger[s_contj++]="video"; /*video parameter*/
            memset((char *)&s_pvm_fun_video,'\0',sizeof(pvm_func_t));
            p_pvm_fun=&s_pvm_fun_video;
            p_merger_tid=&s_merger_tid_video[0];
            memset((char *)&s_pvm_single_proc_video,'\0',sizeof(pvm_func_t));
            p_pvm_single_proc=&s_pvm_single_proc_video;
            p_pvm_single_proc->p_slave_tids=&s_merger_tid_video[0]; /*data for the merger process*/
        }
        else
        {
            p_argv[s_cont++]="audio"; /*audio parameter*/
            p_argv_merger[s_contj++]="audio";/*audio parameter*/
            memset((char *)&s_pvm_fun_audio,'\0',sizeof(pvm_func_t));
            p_pvm_fun=&s_pvm_fun_audio;
            p_merger_tid=&s_merger_tid_audio[0];
            memset((char *)&s_pvm_single_proc_audio,'\0',sizeof(pvm_func_t));
            p_pvm_single_proc=&s_pvm_single_proc_audio;
            p_pvm_single_proc->p_slave_tids=&s_merger_tid_audio[0]; /*data for the merger process*/
        }
        p_pvm_single_proc->s_nproc=1;               /*data for the merger process*/
        p_pvm_single_proc->s_current_tid=0; /*data for the merger process*/
        if (f_pvm_master_start_stop("open","tcpvmexportd",p_argv,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun)==NULL)
        {
            PVM_DEINIT;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        memset((char *)&s_version,'\0',MAX_BUF);
        tc_snprintf((char *)&s_version,MAX_BUF,"%s",EXPORT_PVM_VERSION);
        if((f_pvm_multi_send(strlen(&s_version[0]),(char *)&s_version[0],PVM_CHECK_VERSION,p_pvm_fun))==-1)
        {
            (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
            PVM_DEINIT;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if (p_pvm_conf->s_system_merger.p_hostname!=NULL)
        {
            if ((s_sys_merger_started==0)&&(s_msys==0))
            {
                s_sys_merger_started=1; /*execute only 1 time*/
                s_msys=1;
                if ((*p_merger_sys_tid=f_pvm_start_single_process("tcpvmexportd",p_argv_merger_sys,p_pvm_conf->s_system_merger.p_hostname)) ==-1)
                {
                    (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
                    PVM_DEINIT;
                    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                    return TC_ERROR;
                }
                if (verbose_flag & TC_DEBUG)
                    tc_log_info(MOD_NAME, "System merger tid %d",*p_merger_sys_tid);
                /*send the output file name*/
                if(f_pvm_send(strlen(p_file_to_open),(char *)p_file_to_open,PVM_JOIN_OPT_INIT,s_pvm_single_proc_system.s_current_tid,&s_pvm_single_proc_system)==-1)
                {
                    f_pvm_stop_single_process(*p_merger_sys_tid);
                    (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
                    PVM_DEINIT;
                    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                    return TC_ERROR;
                }
            }
        }
        if ((*p_merger_tid=f_pvm_start_single_process("tcpvmexportd",p_argv_merger,p_merger_conf->p_hostname)) ==-1)
        {
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
            PVM_DEINIT;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if (verbose_flag & TC_DEBUG)
            tc_log_info(MOD_NAME, "MOD_INIT stop f_pvm_start_single_process");
        if (verbose_flag & TC_DEBUG)
            tc_log_info(MOD_NAME, "MOD_INIT start PVM_INIT_SKED");
        if((f_pvm_multi_send(sizeof(int),(char *)&p_pvm_conf->s_num_frame_task,PVM_INIT_SKED,p_pvm_fun))==-1)
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
            PVM_DEINIT;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if (verbose_flag & TC_DEBUG)
            tc_log_info(MOD_NAME, "MOD_INIT stop PVM_INIT_SKED");
        if (verbose_flag & TC_DEBUG)
        {
            if(param->flag == TC_VIDEO)
                tc_log_info(MOD_NAME, "Video merger tid %d",*p_merger_tid);
            else
                tc_log_info(MOD_NAME, "Audio merger tid %d",*p_merger_tid);
        }
        if((f_pvm_multi_send(sizeof(int),(char *)p_merger_tid,PVM_INIT_JOIN,p_pvm_fun))==-1)    /*send the merger tid*/
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
            PVM_DEINIT;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        /*initial send of the vob structures to the slave processes*/
        p_vob_buffer=f_vob_pack("open",vob,&s_vob_buffer_size);
        if (vob->divxmultipass==2)
        {
            if (verbose_flag & TC_DEBUG)
                tc_log_info(MOD_NAME, "enter in preinit msg");
            for(s_cont=0;s_cont<p_pvm_fun->s_nproc;s_cont++)
            {
                if(f_pvm_send(sizeof(int),(char *)&s_cont,PVM_EXP_OPT_PREINIT,s_cont,p_pvm_fun)==-1)
                {
                    f_pvm_stop_single_process(*p_merger_tid);
                    if (s_sys_merger_started==1)
                        s_sys_merger_started--;
                    else if (s_sys_merger_started==0)
                        f_pvm_stop_single_process(*p_merger_sys_tid);
                    (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
                    PVM_DEINIT;
                    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                    return TC_ERROR;
                }
            }
            if (verbose_flag & TC_DEBUG)
                tc_log_info(MOD_NAME, "exit from preinit msg");
        }
        if((f_pvm_multi_send(s_vob_buffer_size,p_vob_buffer,PVM_EXP_OPT_INIT,p_pvm_fun))==-1)
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
            PVM_DEINIT;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        (char *)f_vob_pack("close",NULL,&s_vob_buffer_size);
        if(f_pvm_send(strlen(p_file_to_open),(char *)p_file_to_open,PVM_JOIN_OPT_INIT,p_pvm_single_proc->s_current_tid,p_pvm_single_proc)==-1)
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
            PVM_DEINIT;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if (s_msys==1)
        {
            if(f_pvm_send(sizeof(int),(char *)p_merger_sys_tid,PVM_MERGER_INIT,p_pvm_single_proc->s_current_tid,p_pvm_single_proc)==-1)
            {
                f_pvm_stop_single_process(*p_merger_tid);
                if (s_sys_merger_started==1)
                    s_sys_merger_started--;
                else if (s_sys_merger_started==0)
                    f_pvm_stop_single_process(*p_merger_sys_tid);
                (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
                PVM_DEINIT;
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
        }
    }
    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
    if (verbose_flag & TC_DEBUG)
        tc_log_info(MOD_NAME, "exit from MOD_INIT");
    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{
    static int s_video_cont=0,s_audio_cont=0,s_video_seq=0,s_audio_seq=0;
    static char *p_video_buffer=NULL,*p_audio_buffer=NULL;
    pvm_func_t *p_pvm_fun=NULL;
    char *p_buffer=NULL;
    int s_seq,s_cont=0;
    int *p_merger_tid=0,*p_merger_sys_tid=0;
    int s_null_module;

    pthread_mutex_lock(&s_channel_lock);    /*this is the only way to make my module work with nultithreads: need to change all the code*/
    p_merger_sys_tid=(int *)&s_merger_tid_system[0];
        if(param->flag == TC_VIDEO)
        {
        s_null_module=s_null_video_module;
        if (!s_null_module)
        {
            p_pvm_fun=&s_pvm_fun_video;
            p_buffer=p_video_buffer;
            s_seq=s_video_seq;
            p_merger_tid=&s_merger_tid_video[0];
            s_cont=s_video_cont;
        }
    }
    else
        {
        s_null_module=s_null_audio_module;
        if (!s_null_module)
        {
            p_pvm_fun=&s_pvm_fun_audio;
            p_buffer=p_audio_buffer;
            s_seq=s_audio_seq;
            p_merger_tid=&s_merger_tid_audio[0];
            s_cont=s_audio_cont;
        }
    }
    if (!s_null_module)
    {
        if (p_buffer==NULL)
        {
            p_buffer=(char *)malloc(sizeof(transfer_t)+param->size);
            f_pvm_balancer("open",p_pvm_fun,0,param->flag);
        }
        (int)f_pvm_set_send(s_seq); /*set the seq number*/
        ac_memcpy(p_buffer,(char *)param,sizeof(transfer_t));
        ac_memcpy(p_buffer+sizeof(transfer_t),(char *)param->buffer,param->size);
        if((s_seq=f_pvm_send((sizeof(transfer_t)+param->size),(char *)p_buffer,PVM_EXP_OPT_ENCODE,p_pvm_fun->s_current_tid,p_pvm_fun))==-1)
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            PVM_CLOSE;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if (verbose_flag & TC_DEBUG)
        {
            if(param->flag == TC_VIDEO)
                tc_log_info(MOD_NAME,"Send %d video sequence number",s_seq);
            else
                tc_log_info(MOD_NAME,"Send %d audio sequence number",s_seq);
        }
        f_pvm_balancer("set-seq",p_pvm_fun,s_seq,param->flag); /*the tid in s_tid_pos now elab the seq*/
        if (s_cont<p_pvm_conf->s_num_frame_task)
        {
            s_cont++;
        }
        else
        {
            (int)f_pvm_set_send(s_seq); /*set the prev. seq number*/
            if((s_seq=f_pvm_send(0,(char *)0,PVM_EXP_OPT_RESTART_ENCODE1,p_pvm_fun->s_current_tid,p_pvm_fun))==-1)
            {
                f_pvm_stop_single_process(*p_merger_tid);
                if (s_sys_merger_started==1)
                    s_sys_merger_started--;
                else if (s_sys_merger_started==0)
                    f_pvm_stop_single_process(*p_merger_sys_tid);
                PVM_CLOSE;
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
            f_pvm_balancer("first-free",p_pvm_fun,s_seq,param->flag);
            if((s_seq=f_pvm_send(0,(char *)0,PVM_EXP_OPT_RESTART_ENCODE2,p_pvm_fun->s_current_tid,p_pvm_fun))==-1) /*automatic increment of seq*/
            {
                f_pvm_stop_single_process(*p_merger_tid);
                if (s_sys_merger_started==1)
                    s_sys_merger_started--;
                else if (s_sys_merger_started==0)
                    f_pvm_stop_single_process(*p_merger_sys_tid);
                PVM_CLOSE;
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
            s_cont=0;
        }
        if(param->flag == TC_VIDEO)
        {
            p_video_buffer=p_buffer;
            s_video_seq=s_seq;
            s_video_cont=s_cont;
        }
        else
        {
            p_audio_buffer=p_buffer;
            s_audio_seq=s_seq;
            s_audio_cont=s_cont;
        }
    }
    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * stop codec
 *
 * ------------------------------------------------------------*/

MOD_stop
{
    pvm_func_t *p_pvm_fun=NULL;
    int *p_merger_tid=0,*p_merger_sys_tid=0;
    int s_null_module;
    static int s_msys=0;

    p_merger_sys_tid=(int *)&s_merger_tid_system[0];
    pthread_mutex_lock(&s_channel_lock);    /*this is the only way to make my module work with nultithreads: need to change all the code*/
        if(param->flag == TC_VIDEO)
        {
        s_null_module=s_null_video_module;
        if (!s_null_module)
        {
            p_pvm_fun=&s_pvm_fun_video;
            p_merger_tid=&s_merger_tid_video[0];
            }
        }
    else
        {
        s_null_module=s_null_audio_module;
        if (!s_null_module)
        {
            p_pvm_fun=&s_pvm_fun_audio;
            p_merger_tid=&s_merger_tid_audio[0];
            }
        }
    if (!s_null_module)
    {
        if (p_handle!=NULL)
        {
            if (p_pvm_fun!=NULL)
            {
                (int)f_pvm_multi_send(0,(char *)0,PVM_EXP_OPT_STOP,p_pvm_fun);
                f_pvm_stop_single_process(*p_merger_tid);
                if (s_sys_merger_started!=-1)
                {
                    if (s_msys==0)
                    {
                        s_msys=1;
                        f_pvm_stop_single_process(*p_merger_sys_tid);
                        memset((char *)&s_pvm_single_proc_system,'\0',sizeof(pvm_func_t));
                        s_merger_tid_system[0]=-1;
                    }
                }
                f_pvm_balancer("close",p_pvm_fun,0,param->flag);
                (pvm_func_t*)f_pvm_master_start_stop("close","tcpvmexportd",(char **)0,p_pvm_conf->s_nproc,p_pvm_conf->s_max_proc,p_pvm_fun);
            }
            PVM_DEINIT;
            if (s_init_check==0)
                p_handle=NULL;
        }
        if(param->flag == TC_VIDEO)
        {
            memset((char *)&s_pvm_fun_video,'\0',sizeof(pvm_func_t));
            s_merger_tid_video[0]=-1;
        }
        else
        {
            memset((char *)&s_pvm_fun_video,'\0',sizeof(pvm_func_t));
            s_merger_tid_audio[0]=-1;
        }
    }
    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
    return TC_OK;
}


/* ------------------------------------------------------------
 *
 * close codec
 *
 * ------------------------------------------------------------*/

MOD_close
{
    int s_seq=0,s_dummy,s_dummy1,s_rc;
    pvm_func_t *p_pvm_fun=NULL;
    int *p_merger_tid=0,*p_merger_sys_tid=0;
    pvm_func_t *p_pvm_single_proc=NULL;
    int s_null_module;
    static int s_msys=0;

    p_merger_sys_tid=(int *)&s_merger_tid_system[0];
    pthread_mutex_lock(&s_channel_lock);    /*this is the only way to make my module work with nultithreads: need to change all the code*/
        if(param->flag == TC_VIDEO)
        {
        s_null_module=s_null_video_module;
        if (!s_null_module)
        {
            p_pvm_fun=&s_pvm_fun_video;
            p_merger_tid=&s_merger_tid_video[0];
            p_pvm_single_proc=&s_pvm_single_proc_video;
        }
        }
    else
        {
        s_null_module=s_null_audio_module;
        if (!s_null_module)
        {
            p_pvm_fun=&s_pvm_fun_audio;
            p_merger_tid=&s_merger_tid_audio[0];
            p_pvm_single_proc=&s_pvm_single_proc_audio;
        }
        }
    if (!s_null_module)
    {
        if((s_seq=f_pvm_send(0,(char *)0,PVM_EXP_OPT_RESTART_ENCODE1,p_pvm_fun->s_current_tid,p_pvm_fun))==-1)  /*flush the buffer*/
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            PVM_CLOSE;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if((f_pvm_multi_send(0,(char *)0,PVM_EXP_OPT_CLOSE,p_pvm_fun))==-1)
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            PVM_CLOSE;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if((s_seq=f_pvm_send(0,(char *)0,PVM_JOIN_OPT_RUN,0,p_pvm_single_proc))==-1) /*s_seq not really used*/
        {
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            PVM_CLOSE;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if (verbose_flag & TC_DEBUG)
        {
            if(param->flag == TC_VIDEO)
                tc_log_info(MOD_NAME,"Waiting for video merger task (seq %d) termination...",s_seq);
            else
                tc_log_info(MOD_NAME,"Waiting for audio merger task (seq %d) termination...",s_seq);
        }
        (int)f_pvm_set_recv(s_seq); /*set the last s_seq send*/
        (int)f_pvm_recv(&s_dummy,(char *)&s_dummy1,&s_rc);
        if (s_rc)
        {
            tc_log_warn(MOD_NAME,"Can't close destination file");
            f_pvm_stop_single_process(*p_merger_tid);
            if (s_sys_merger_started==1)
                s_sys_merger_started--;
            else if (s_sys_merger_started==0)
                f_pvm_stop_single_process(*p_merger_sys_tid);
            PVM_CLOSE;
            pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
            return TC_ERROR;
        }
        if (verbose_flag & TC_DEBUG)
            tc_log_info(MOD_NAME, "done.");
        if (s_sys_merger_started!=-1)
        {
            if((s_seq=f_pvm_send(0,(char *)0,PVM_JOIN_OPT_SENDFILE,0,p_pvm_single_proc))==-1) /*s_seq not really used*/
            {
                f_pvm_stop_single_process(*p_merger_tid);
                if (s_sys_merger_started==1)
                    s_sys_merger_started--;
                else if (s_sys_merger_started==0)
                    f_pvm_stop_single_process(*p_merger_sys_tid);
                PVM_CLOSE;
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
        }
        if (s_msys)
        {
            if (verbose_flag & TC_DEBUG)
                tc_log_info(MOD_NAME, "Waiting for system merger build list...");
            (int)f_pvm_set_recv(PVM_MSG_ENDTASK_SYSTEM);    /*wait for system merger end task*/
            (int)f_pvm_recv(&s_dummy,(char *)&s_dummy1,&s_rc);
            if (verbose_flag & TC_DEBUG)
                tc_log_info(MOD_NAME, "done.");
            if (s_rc)
            {
                tc_log_warn(MOD_NAME,"Can't close destination file");
                f_pvm_stop_single_process(*p_merger_tid);
                PVM_CLOSE;
                pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                return TC_ERROR;
            }
            if ((p_pvm_conf->s_system_merger.p_hostname!=NULL)&&(p_pvm_conf->s_system_merger.s_build_only_list!=1)) /*do it for 0 and 2*/
            {
                if((s_seq=f_pvm_send(0,(char *)0,PVM_JOIN_OPT_RUN,0,&s_pvm_single_proc_system))==-1) /*s_seq not really used*/
                {
                    f_pvm_stop_single_process(*p_merger_tid);
                    f_pvm_stop_single_process(*p_merger_sys_tid);
                    PVM_CLOSE;
                    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                    return TC_ERROR;
                }
                if (verbose_flag & TC_DEBUG)
                    tc_log_info(MOD_NAME,"Waiting for system merger task (seq %d) termination...",s_seq);
                (int)f_pvm_set_recv(s_seq); /*set the last s_seq send*/
                (int)f_pvm_recv(&s_dummy,(char *)&s_dummy1,&s_rc);
                if (s_rc)
                {
                    tc_log_warn(MOD_NAME,"Can't close destination file");
                    f_pvm_stop_single_process(*p_merger_tid);
                    PVM_CLOSE;
                    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
                    return TC_ERROR;
                }
                if (verbose_flag & TC_DEBUG)
                    tc_log_info(MOD_NAME,"done.");
            }
        }
        if (s_sys_merger_started!=-1)
            s_msys=1;
    }
    pthread_mutex_unlock(&s_channel_lock);  /*this is the only way to make my module work with nultithreads: need to change all the code*/
    return TC_OK;
}


