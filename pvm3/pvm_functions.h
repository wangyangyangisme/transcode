/*
 *  pvm_functions.h
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

#ifndef _PVM_FUNCTIONS_H
#define _PVM_FUNCTIONS_H

#include <stdio.h>
#include <pvm3.h>
#include <pvm_version.h>


typedef struct _pvm_res_func_t 	{
					int 	s_rc;
					int 	s_ret_size;
					int 	s_msg_type;
					int 	s_dim_buffer;
					char 	*p_result;
				} pvm_res_func_t;


typedef struct _pvm_func_t 	{
					int 	s_nproc;
					int 	s_nhosts;
					int	s_current_tid;
					int 	*p_slave_tids;
					int 	*p_used_tid;
				} pvm_func_t;

#define PVM_EXP_OPT_INIT		0x00000000
#define PVM_EXP_OPT_OPEN		0x00000001
#define PVM_EXP_OPT_ENCODE		0x00000002
#define PVM_EXP_OPT_CLOSE 		0x00000003
#define PVM_EXP_OPT_STOP		0x00000004
#define PVM_EXP_OPT_RESTART_ENCODE1   	0x00000005
#define PVM_JOIN_OPT_RUN 	  	0x00000006
#define PVM_JOIN_OPT_ADD_ELAB 	  	0x00000007
#define PVM_JOIN_OPT_ADD_REMOVE	  	0x00000008
#define PVM_JOIN_OPT_INIT	  	0x00000009
#define PVM_INIT_SKED   		0x0000000A
#define PVM_INIT_JOIN   		0x0000000B
#define PVM_CHECK_VERSION   		0x0000000C
#define PVM_JOIN_OPT_SENDFILE  		0x0000000D
#define PVM_MERGER_INIT  		0x0000000E
#define PVM_EXP_OPT_PREINIT		0x0000000F
#define PVM_EXP_OPT_RESTART_ENCODE2   	0x00000010

#define PVM_MSG_WORK 			0x00000020
#define PVM_MSG_RING 			0x00000021
#define PVM_MSG_CONF 			0x00000022
#define PVM_MSG_WRKN 			0x00000023
#define PVM_MSG_JOIN	   		0x00000024
#define PVM_MSG_CONF_JOIN		0x00000025
#define PVM_MSG_MERG_SEND		0x00000026
#define PVM_MSG_MERG_PASTE		0x00000027
#define PVM_MSG_ADD_REM			0x00000028
#define PVM_MSG_ENDTASK_SYSTEM 		INT_MAX-2
#define PVM_MSG_ENDTASK_VIDEO 		INT_MAX-1
#define PVM_MSG_ENDTASK_AUDIO 		INT_MAX
#define PVM_MSG_LAST_SEQ		PVM_MSG_ENDTASK_SYSTEM

#ifdef PVM_DL_FUNC

/*public function*/
pvm_func_t * (*f_pvm_master_start_stop)(char *p_option,char *p_spawn_process,char **p_argv,int s_nproc_host,int s_nproc_max,pvm_func_t *p_func);
void (*f_pvm_skeduler)(pvm_res_func_t *(*f_my_elab_func)(int,char *,int,int));
int (*f_pvm_send)(int s_buff_size,char *p_buffer,int s_option,int s_pos_tids,pvm_func_t *p_func);
int (*f_pvm_set_send)(int s_set_seq);
int (*f_pvm_multi_send)(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func);
int (*f_pvm_multi_send_nw)(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func);
int (*f_pvm_nrecv)(int *s_buff_size,char *p_buffer,int *s_rc);
int (*f_pvm_recv)(int *s_buff_size,char *p_buffer,int *s_rc);
int (*f_pvm_set_nrecv)(int s_seq);
int (*f_pvm_set_recv)(int s_seq);
int (*f_pvm_start_single_process)(char *p_spawn_process,char **p_argv,char *p_where);
void (*f_pvm_stop_single_process)(int p_slave_tid);
#else

/*private function*/

/* f_pvm_master_start_stop:
	s_type:			1 start 0 end
	p_spawn_process: 	process to spawn
	s_nproc_host: 		number of process per host
	s_nproc_max: 		max number of process
*/
pvm_func_t *f_pvm_master_start_stop(char *p_option,char *p_spawn_process,char **p_argv,int s_nproc_host,int s_nproc_max,pvm_func_t *p_func);
/* the skeduler require a function to elab the input data*/
void f_pvm_skeduler(pvm_res_func_t *(*f_my_elab_func)(int,char *,int,int));
int f_pvm_send(int s_buff_size,char *p_buffer,int s_option,int s_pos_tids,pvm_func_t *p_func);
int f_pvm_set_send(int s_set_seq);
int f_pvm_multi_send(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func);/*broadcast the info and wait for the completition of all the task*/
int f_pvm_multi_send_nw(int s_buff_size,char *p_buffer,int s_option,pvm_func_t *p_func);/*broadcast the info*/
/* If you start with a non blocking receive you must continue with the save api or set the seq number with the set api*/
int f_pvm_nrecv(int *s_buff_size,char *p_buffer,int *s_rc);
int f_pvm_recv(int *s_buff_size,char *p_buffer,int *s_rc);
int f_pvm_set_nrecv(int s_seq);
int f_pvm_set_recv(int s_seq);
int f_ring(int s_father_tid,int *s_rec_seq,int s_msg_type,int s_rc);
int f_pvm_start_single_process(char *p_spawn_process,char **p_argv,char *p_where);
void f_pvm_stop_single_process(int p_slave_tid);


#endif

#endif
