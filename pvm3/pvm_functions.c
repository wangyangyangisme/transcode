/*
 *  pvm_functions.c
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

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pvm_functions.h>

#define MAX_BUF	1024

int f_pvm_start_single_process(char *p_spawn_process,char **p_argv,char *p_where)
{
	int s_slave_tid;

	if (pvm_spawn(p_spawn_process,p_argv,PvmTaskHost,p_where,1,&s_slave_tid)<0) /* start up merger */
	{
		pvm_perror("");
		return(-1);
	}
	return(s_slave_tid);
}

void f_pvm_stop_single_process(int s_slave_tid)
{
	if (s_slave_tid >=0)
		pvm_kill(s_slave_tid);
}

static int f_pvm_send_all(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func,int s_type,int s_pos_tids)
{
	static int s_seq=-1;

	if (s_type!=-1)
	{
		s_seq=s_type-1;
		return(0);
	}
	if (s_pos_tids>p_func->s_nproc)
	{
		return(-1);
	}
	if (p_func->p_slave_tids ==NULL)
		return(-1);
	if (s_seq+1==PVM_MSG_LAST_SEQ)
		s_seq=0;
	else
		s_seq++;

	pvm_initsend(PvmDataDefault);
	pvm_pkint(&s_seq,1,1);
	pvm_pkint(&s_option,1,1);
	pvm_pkint(&s_buff_size,1,1);
	pvm_pkbyte(p_buffer,s_buff_size,1);
	pvm_send(p_func->p_slave_tids[s_pos_tids],PVM_MSG_WORK);
	return(s_seq);
}

int f_pvm_set_send(int s_set_seq)
{
	return(f_pvm_send_all(0,NULL,0,NULL,s_set_seq,0));
}

int f_pvm_send(int s_buff_size,char *p_buffer,int s_option,int s_pos_tids,pvm_func_t *p_func)
{
	return(f_pvm_send_all(s_buff_size,p_buffer,s_option,p_func,-1,s_pos_tids));
}

static int f_pvm_multi_send_all(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func,int s_wait)
{
	static int s_seq=-1;
	int s_rec_seq,s_rc;

	if (p_func->p_slave_tids ==NULL)
		return(-1);
	pvm_initsend(PvmDataDefault);
	if (!s_wait)
	{
		s_rec_seq=-1;
		pvm_pkint(&s_rec_seq,1,1);	/*no seq number */
	}
	else
	{
		s_seq++;
		pvm_pkint(&s_seq,1,1);
	}
	pvm_pkint(&s_option,1,1);
	pvm_pkint(&s_buff_size,1,1);
	pvm_pkbyte(p_buffer,s_buff_size,1);
	pvm_mcast(p_func->p_slave_tids,p_func->s_nproc,PVM_MSG_CONF);
	if (s_wait)
	{
		for(;;)
		{
			pvm_recv(-1,PVM_MSG_RING);	/*waiting the close of the token*/
			pvm_upkint(&s_rec_seq,1,1);	/*send the response to number*/
			pvm_upkint(&s_rc,1,1);		/*retrive the rc*/
			if (s_rec_seq==s_seq)		/*if the seq number match return ok*/
				return(s_rc);
		}
	}
	else
	{
		return(0);
	}
}

int f_pvm_multi_send_nw(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func)
{
	return(f_pvm_multi_send_all(s_buff_size,p_buffer,s_option,p_func,0));
}

int f_pvm_multi_send(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func)
{
	return(f_pvm_multi_send_all(s_buff_size,p_buffer,s_option,p_func,1));
}

static int f_pvm_nrecv_check(int *p_buff_size,char *p_buffer,int s_init_seq,int *s_rc)
{
	static int s_seq_rec=0;
	int s_tmp,s_bs,s_rc_int;

	if (s_init_seq != -1)
	{
		s_seq_rec=s_init_seq;		/*set the inital seq number*/
		*s_rc=0;
		return(0);
	}
	if(pvm_nrecv(-1,s_seq_rec)!=0)
	{
		pvm_upkint(&s_tmp,1,1);		/*decode the seq number of the message*/
		pvm_upkint(&s_rc_int,1,1);		/*decode the function rc */
		*s_rc=s_rc_int;			/*rc*/
		pvm_upkint(&s_bs,1,1);	/*decode the size of the next message*/
		if (s_bs!=0)
			pvm_upkbyte(p_buffer,s_bs,1);	/*decode the received buffer*/
		*p_buff_size=s_bs;
		s_seq_rec++;				/*this is the response of my request*/
		return(s_tmp);			/*return the seq number of the recv message*/
	}
	*s_rc=0;
	return(-1);
}

int f_pvm_nrecv(int *p_buff_size,char *p_buffer,int *s_rc)
{
	return(f_pvm_nrecv_check(p_buff_size,p_buffer,-1,s_rc));
}

int f_pvm_set_nrecv(int s_seq)
{
	int s_rc;
	return(f_pvm_nrecv_check(NULL,NULL,s_seq,&s_rc));
}

static int f_pvm_recv_check(int *p_buff_size,char *p_buffer,int s_init_seq,int *s_rc)
{
	static int s_seq_rec=0;
	int s_tmp,s_bs,s_rc_int;

	if (s_init_seq != -1)
	{
		s_seq_rec=s_init_seq;		/*set the inital seq number*/
		*s_rc=0;
		return(0);
	}
	pvm_recv(-1,s_seq_rec);
	pvm_upkint(&s_tmp,1,1);		/*decode the seq number of the message*/
	pvm_upkint(&s_rc_int,1,1);		/*decode the function rc */
	*s_rc=s_rc_int;			/*rc*/
	pvm_upkint(&s_bs,1,1);	/*decode the size of the next message*/
	if (s_bs!=0)
		pvm_upkbyte(p_buffer,s_bs,1);	/*decode the received buffer*/
	if (s_seq_rec==INT_MAX)
		s_seq_rec=0;
	else
		s_seq_rec++;		/*this is the response of my request*/
	*p_buff_size=s_bs;
	return(s_tmp);			/*return the seq number of the recv message*/
}

int f_pvm_recv(int *p_buff_size,char *p_buffer,int *s_rc)
{
	return(f_pvm_recv_check(p_buff_size,p_buffer,-1,s_rc));
}

int f_pvm_set_recv(int s_seq)
{
	int s_rc;
	return(f_pvm_recv_check(NULL,NULL,s_seq,&s_rc));
}


/* This function must be included in the spawn process: it initialize the skeduler */
void f_pvm_skeduler(pvm_res_func_t *(*f_my_elab_func)(int,char *,int,int))
{
	int s_father_tid,s_rec_seq,s_src_tid,s_msg_type,s_msg_size,s_bufid;
	int s_size=0,s_option,s_rc,s_my_tid,*p_other_tids,s_num_tids,s_cont;
	static int s_alloc_size=0,s_nfrxtask=1,s_join=-1;
	static char *p_buffer=NULL;
	pvm_res_func_t *p_result=NULL;
	char s_hostname[MAX_BUF];
	static int s_seq_preinit=-1;

	s_father_tid=pvm_parent();
	s_my_tid=pvm_mytid();
	s_num_tids = pvm_siblings(&p_other_tids); /* determine the size of my sibling list */
	for(;;)
	{
		s_bufid=pvm_recv(-1,-1);			/*waiting for a message*/
		pvm_bufinfo(s_bufid,&s_msg_size,&s_msg_type,&s_src_tid);	/*retrive info about the received message*/
		/*accept only from father and from all the slave process to merger not from itself or from ring*/
		if ((s_src_tid==s_father_tid)||((s_src_tid!=s_my_tid)&&(s_msg_type==PVM_MSG_JOIN)))
		{
			pvm_upkint(&s_rec_seq,1,1);			/*retrive the sequence number*/
			pvm_upkint(&s_option,1,1);			/*retrive the option number*/
			pvm_upkint(&s_size,1,1);			/*retrive the size*/
			if (s_alloc_size < s_size)			/*check the size of the buffer*/
			{
				p_buffer=(char *)realloc(p_buffer,s_size);	/*allocate/reallocate a buffer */
				s_alloc_size=s_size;
			}
			memset(p_buffer,'\0',s_alloc_size);
			if (s_size>0)
				pvm_upkbyte(p_buffer,s_size,1);		/*data packet*/
			if ((s_msg_type==PVM_MSG_CONF)&&(s_option==PVM_CHECK_VERSION))		/*check which the type of msg*/
			{
				if (memcmp((char *)EXPORT_PVM_VERSION,p_buffer,strlen(EXPORT_PVM_VERSION)) !=0)
				{
					s_rc=1;
					if((gethostname(s_hostname,sizeof(s_hostname)))!=0)
					{
						memset(s_hostname,'\0',sizeof(s_hostname));
						snprintf(s_hostname,sizeof(s_hostname),"localhost-%d\n",getpid());
					}
					fprintf(stderr,"(%s) Invalid version: (%s) request (%s) on host %s\n",__FILE__,EXPORT_PVM_VERSION,p_buffer,s_hostname);
				}
				else
					s_rc=0;
			}
			else if ((s_msg_type==PVM_MSG_CONF)&&(s_option==PVM_INIT_SKED))		/*check which the type of msg*/
			{
				memcpy(&s_nfrxtask,p_buffer,s_size);
				s_rc=0;
			}
			else if ((s_msg_type==PVM_MSG_CONF)&&(s_option==PVM_INIT_JOIN))		/*check which the type of msg*/
			{
				memcpy(&s_join,p_buffer,s_size);
				s_rc=0;
			}
			else if ((s_msg_type==PVM_MSG_WORK)&&(s_option==PVM_MERGER_INIT))		/*check which the type of msg*/
			{
				memcpy(&s_join,p_buffer,s_size);
				s_msg_type=PVM_MSG_WRKN;
				s_rc=0;
			}
			else if ((s_msg_type==PVM_MSG_WORK)&&(s_option==PVM_EXP_OPT_PREINIT))		/*check the option of msg*/
			{
				memcpy(&s_seq_preinit,p_buffer,s_size);
				s_msg_type=PVM_MSG_WRKN;
				s_rc=0;
			}
			else
			{
				if ((s_seq_preinit!=-1)&&((s_option==PVM_EXP_OPT_OPEN)||(s_option==PVM_EXP_OPT_INIT)))
					p_result=(*f_my_elab_func)(s_option,p_buffer,s_size,s_seq_preinit);
				else
					p_result=(*f_my_elab_func)(s_option,p_buffer,s_size,s_rec_seq);
				s_rc=p_result->s_rc;
				if (p_result->s_msg_type != s_msg_type)
					s_msg_type=p_result->s_msg_type;	/*the type of msg can be change by the f_my_elab_func routine*/
			}
			switch (s_msg_type)		/*check which the type of msg*/
			{
				case PVM_JOIN_OPT_ADD_ELAB:
					if (s_join==-1)
					{
						fprintf(stderr,"(%s) Merger not yet started\n",__FILE__);
						for (s_cont=0;s_cont< s_num_tids;s_cont++)	/* determine the index of my task*/
						{
							if (p_other_tids[s_cont]!=s_my_tid)
							{
								pvm_kill(p_other_tids[s_cont]);
							}
						}
						pvm_kill(s_father_tid);
						break;
					}
					pvm_initsend(PvmDataDefault);		/*initialize the send*/
					pvm_pkint(&s_rec_seq,1,1);		/*send packet sequence*/
					s_cont=PVM_JOIN_OPT_ADD_ELAB;		/*add to remove list*/
					pvm_pkint(&s_cont,1,1);			/*option data packet*/
					pvm_pkint(&(p_result->s_ret_size),1,1);			/*data packet*/
					pvm_pkbyte(p_result->p_result,p_result->s_ret_size,1);		/*data packet*/
					pvm_send(s_join,PVM_MSG_JOIN);		/*send the packet to the merger process*/
				break;
				case PVM_MSG_CONF_JOIN:
					if (s_join==-1)
					{
						fprintf(stderr,"(%s) Merger not yet started\n",__FILE__);
						for (s_cont=0;s_cont< s_num_tids;s_cont++)	/* determine the index of my task*/
						{
							if (p_other_tids[s_cont]!=s_my_tid)
							{
								pvm_kill(p_other_tids[s_cont]);
							}
						}
						pvm_kill(s_father_tid);
						break;
					}
					pvm_initsend(PvmDataDefault);		/*initialize the send*/
					pvm_pkint(&s_rec_seq,1,1);		/*send packet sequence*/
					s_cont=PVM_JOIN_OPT_ADD_REMOVE;		/*add to remove list*/
					pvm_pkint(&s_cont,1,1);			/*option data packet*/
					pvm_pkint(&(p_result->s_ret_size),1,1);			/*data packet*/
					pvm_pkbyte(p_result->p_result,p_result->s_ret_size,1);		/*data packet*/
					pvm_send(s_join,PVM_MSG_JOIN);		/*send the packet to the merger process*/
				/*don't close the case need to send to the ring*/
				case PVM_MSG_CONF:
					s_msg_type=PVM_MSG_RING;
					if (s_rec_seq!=-1)
						(int)f_ring(s_father_tid,&s_rec_seq,s_msg_type,s_rc);	/*wait for all task elaboration*/
				break;
				case PVM_MSG_WORK:
					pvm_initsend(PvmDataDefault);		/*initialize the send*/
					pvm_pkint(&s_rec_seq,1,1);		/*send packet sequence*/
					pvm_pkint(&(p_result->s_rc),1,1);		/*send the function rc*/
					pvm_pkint(&(p_result->s_ret_size),1,1);			/*data packet*/
					pvm_pkbyte(p_result->p_result,p_result->s_ret_size,1);		/*data packet*/
					pvm_send(s_father_tid,s_rec_seq);	/*send the packet*/
				break;
				case PVM_MSG_ENDTASK_SYSTEM:
					pvm_initsend(PvmDataDefault);		/*initialize the send*/
					s_cont=0;
					pvm_pkint(&s_rec_seq,1,1);		/*send packet sequence*/
					pvm_pkint(&(p_result->s_rc),1,1);		/*send the function rc*/
					pvm_pkint(&s_cont,1,1);			/*data packet*/
//					pvm_pkbyte(p_result->p_result,p_result->s_ret_size,1);		/*not really used*/
					pvm_send(s_father_tid,PVM_MSG_ENDTASK_SYSTEM);	/*send the packet*/
				break;
				case PVM_MSG_ENDTASK_VIDEO:
				case PVM_MSG_ENDTASK_AUDIO:
					pvm_initsend(PvmDataDefault);		/*initialize the send*/
					s_cont=0;
					pvm_pkint(&s_rec_seq,1,1);		/*send packet sequence*/
					pvm_pkint(&(p_result->s_rc),1,1);		/*send the function rc*/
					pvm_pkint(&s_cont,1,1);			/*data packet*/
//					pvm_pkbyte(p_result->p_result,p_result->s_ret_size,1);		/*not really used*/
					if (s_msg_type==PVM_MSG_ENDTASK_VIDEO)
						pvm_send(s_father_tid,PVM_MSG_ENDTASK_VIDEO);	/*send the packet*/
					else
						pvm_send(s_father_tid,PVM_MSG_ENDTASK_AUDIO);	/*send the packet*/
				break;
				case PVM_MSG_MERG_SEND:
				case PVM_MSG_ADD_REM:
					if (s_join==-1)
					{
						fprintf(stderr,"(%s) Merger not yet started\n",__FILE__);
						for (s_cont=0;s_cont< s_num_tids;s_cont++)	/* determine the index of my task*/
						{
							if (p_other_tids[s_cont]!=s_my_tid)
							{
								pvm_kill(p_other_tids[s_cont]);
							}
						}
						pvm_kill(s_father_tid);
						break;
					}
					pvm_initsend(PvmDataDefault);		/*initialize the send*/
					pvm_pkint(&s_rec_seq,1,1);		/*send packet sequence*/
					if (s_msg_type==PVM_MSG_MERG_SEND)
						s_cont=PVM_MSG_MERG_PASTE;		/*add to remove list*/
					else
						s_cont=PVM_JOIN_OPT_ADD_REMOVE;		/*add to remove list*/
					pvm_pkint(&s_cont,1,1);			/*option data packet*/
					pvm_pkint(&(p_result->s_ret_size),1,1);			/*data packet*/
					pvm_pkbyte(p_result->p_result,p_result->s_ret_size,1);		/*data packet*/
					pvm_send(s_join,PVM_MSG_JOIN);		/*send the packet to the merger process*/
				break;
				case PVM_MSG_WRKN:
				case PVM_MSG_JOIN:
				default:
				break;
			}
		}
	}
}


pvm_func_t *f_pvm_master_start_stop(char *p_option,char *p_spawn_process,char **p_argv,int s_nproc_host,int s_nproc_max,pvm_func_t *p_func)
{
	int s_master_tid=-1;		/* my task id */
	static int s_num_call=0;		/* my task id */
	int s_started_task,s_cont,s_num_hosts,s_num_arch;
	struct pvmhostinfo *p_host;

	if (!strcasecmp(p_option, "close"))
	{
		if (p_func->p_slave_tids !=NULL)
			for(s_cont=0;s_cont<p_func->s_nproc;s_cont++)
				pvm_kill((p_func->p_slave_tids[s_cont]));			/*terminate all slave process*/
		if (s_num_call==1)
			pvm_exit();					/* remove the master task from pvm*/
		s_num_call--;
		free(p_func->p_used_tid);
		return(NULL);
	}
	else if (!strcasecmp(p_option, "open"))
	{
		memset((char *)p_func,'\0',sizeof(pvm_func_t));
		s_num_call++;	/*number of recall*/
		p_func->s_nproc=0;
		if((p_func->p_slave_tids=calloc(sizeof(int)*s_nproc_max,1)) == NULL)		/*allocate the slave buffer*/
		{
			fprintf(stderr,"(%s) error allocating memory\n",__FILE__);
			return(NULL);
		}
		if (s_num_call==1)
			s_master_tid = pvm_mytid(); 				/* register the task in pvm */

		/* Get config and set number of slaves to start */
		pvm_config( &s_num_hosts, &s_num_arch, &p_host );
		p_func->s_nhosts = s_num_hosts;
		p_func->s_nproc = s_num_hosts * s_nproc_host;
		if(p_func->s_nproc > s_nproc_max)
			p_func->s_nproc = s_nproc_max;

		p_func->p_used_tid=(int *)malloc(sizeof(int)*(p_func->s_nproc));	/*allocate the buffer for the serial number of tid*/

		pvm_setopt(PvmShowTids,0);
		pvm_catchout(stderr);

		if ((s_started_task=pvm_spawn(p_spawn_process,p_argv,PvmTaskDefault,"",p_func->s_nproc,p_func->p_slave_tids))<0) /* start up all slave tasks */
		{
			pvm_perror("");
			return(NULL);
		}
		else if (s_started_task < p_func->s_nproc)
		{
			for(s_cont=0;s_cont<p_func->s_nproc;s_cont++)
				pvm_kill((p_func->p_slave_tids[s_cont]));			/*terminate all slave process*/
			pvm_exit();	/* remove the master task from pvm*/
			return(NULL);
		}
		return(p_func);
	}
	else
	{
		fprintf(stderr,"(%s) invalid command \n",__FILE__);
		return(NULL);
	}
}

int f_ring(int s_father_tid,int *s_rec_seq,int s_msg_type,int s_rc)
{
	int s_mytid;                  /* my task id */
	int *p_other_tids;                  /* array of task ids */
	int s_my_proc_id=0;                     /* my process number */
	int s_cont;
	int s_num_tids;
	int s_src_tid,s_dst_tid,s_rc_prev;

	s_mytid = pvm_mytid();		/*retrive tid*/
	s_num_tids = pvm_siblings(&p_other_tids); /* determine the size of my sibling list */
	for (s_cont=0;s_cont< s_num_tids;s_cont++)	/* determine the index of my task*/
	{
		if (p_other_tids[s_cont]==s_mytid)
		{
			s_my_proc_id=s_cont;
			break;
		}
	}
	if (s_my_proc_id==0)
		s_src_tid=p_other_tids[s_num_tids-1]; /* if i'm the task 0 i need to wait the wake up of the task n. s_num_tids-1 */
	else
		s_src_tid=p_other_tids[s_my_proc_id-1]; /* the other cases */

	if (s_my_proc_id==s_num_tids-1)
		s_dst_tid=p_other_tids[0]; /* if i'm the last task i need to wait the wake up of task n. 0 */
	else
		s_dst_tid=p_other_tids[s_my_proc_id+1]; /* the other cases */

	if(s_my_proc_id==0)
	{
		pvm_initsend(PvmDataDefault);	/*initialize the send*/
		pvm_pkint(&s_rc,1,1);				/*send the rc*/
		pvm_send(s_dst_tid,PVM_MSG_RING);		/*send to the next process*/
		pvm_recv(s_src_tid,PVM_MSG_RING);		/*waiting the close of the token*/
		pvm_upkint(&s_rc_prev,1,1);				/*receive the prev rc*/
		pvm_initsend(PvmDataDefault);			/*initialize the send to the main process*/
		pvm_pkint(s_rec_seq,1,1);			/*send the response to number*/
		pvm_pkint(&s_rc_prev,1,1);			/*send the rc*/
		pvm_send(s_father_tid,s_msg_type);		/*send the ok to the mcast process*/
		return(0);
	}
	pvm_recv(s_src_tid,PVM_MSG_RING);			/*waiting for the token*/
	pvm_upkint(&s_rc_prev,1,1);				/*receive the prev rc*/
	if (s_rc_prev)
		s_rc=s_rc_prev;					/*send the prev error */
	pvm_initsend(PvmDataDefault);			/*initialize the send*/
	pvm_pkint(&s_rc,1,1);				/*send the rc*/
	pvm_send(s_dst_tid,PVM_MSG_RING);		/*send to the next process*/
	return(0);
}
