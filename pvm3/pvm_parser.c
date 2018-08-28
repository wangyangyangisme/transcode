/*
 *  pvm_parser.c
 *
 *  Copyright (C) Malanchini Marzio - August 2003
 *  Updates and port to new libtc configuration file parser:
 *  (C) 2007 Francesco Romani <fromani at gmail dot com>
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

/*
 * OK, I must say that I'm not very proud of my work done here.
 * I think I'll must improve it soon.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "libtc/libtc.h"
#include "libtc/tclist.h"
#include "libtc/cfgfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pvm_parser.h>
#include <pvm_version.h>


/*************************************************************************/
/* helpers */

#define LOCALHOST   "."
/* XXX ?! sic -- Fromani */

static char *pvm_hostname(char *candidate)
{
    if (candidate) {
        tc_strstrip(candidate);
 
        if (strlen(candidate) > 0) { // XXX enlarge check value
            return candidate;
        }
    }
    return LOCALHOST;
}

/* commodity */
typedef char PVMString[TC_BUF_MIN];

typedef struct pvmnodedata_ PVMNodeData;
struct pvmnodedata_ {
    PVMString hostname;
    int enabled; /* flag */
    int maxprocs;
};

/*************************************************************************/
/* main data structure */

static pvm_config_env s_pvm_conf;

/*************************************************************************/
/* forward declarations of dispatchers */

static pvm_config_filelist *dispatch_list(TCList *src, int type,
                                          char *codec, char *destination,
                                          pvm_config_filelist **ret);

static int dispatch_node(int id, PVMNodeData *data,
                         pvm_config_env *env);

static int dispatch_null(int id, pvm_config_env *env);
static int dispatch_merger(int id, pvm_config_env *env);
static int dispatch_modules(int id, pvm_config_env *env);
static int dispatch_syslist(int id, pvm_config_env *env);


/*************************************************************************/
/* 
 * since configuration uses a lot of strings, we need a lot of temporary
 * buffers to store them
 */
/* Nodes */
static int nodes_num = 1; /* this is pretty ugly, isn't it? */
static PVMNodeData nodes_data[PVM_MAX_NODES];
/* SystemMerger */
static PVMString systemmerger_hostname;
static PVMString systemmerger_mplexparams;
/* AudioMerger */
static PVMString audiomerger_hostname;
/* VideoMerger */
static PVMString videomerger_hostname;
/* ExportAudioModule */
static PVMString exportaudiomod_codec;
static PVMString exportaudiomod_params[PVM_MAX_CODEC_PARAMS];
/* ExportVideoModule */
static PVMString exportvideomod_codec;
static PVMString exportvideomod_params[PVM_MAX_CODEC_PARAMS];
/* SystemList */
static PVMString systemlist_codec;
static PVMString systemlist_destination;
static PVMString systemlist_mplexparams;
/* AddAudio */
static PVMString addaudio_codec;
static PVMString addaudio_destination;
/* AddVideo */
static PVMString addvideo_codec;
static PVMString addvideo_destination;

/*************************************************************************/

#define NODEINIT(ID) \
    { { "Hostname", (nodes_data[ID].hostname), TCCONF_TYPE_STRING, 0, 0, 0 }, \
      { "NumProcMax", &(nodes_data[ID].maxprocs), TCCONF_TYPE_INT, \
                      0, 1, PVM_MAX_NODE_PROCS }, \
      { "Enabled", &(nodes_data[ID].enabled), TCCONF_TYPE_FLAG, 0, 0, 1 }, \
      { NULL, NULL, 0, 0, 0, 0, } }

static TCConfigEntry node_conf[PVM_MAX_NODES][4] = {
    NODEINIT(0),
    NODEINIT(1),
    NODEINIT(2),
    NODEINIT(3),
    NODEINIT(4),
    NODEINIT(5),
    NODEINIT(6),
    NODEINIT(7),
};
#undef NODEINIT
static TCConfigEntry pvmhostcaps_conf[] = {
    { "NumProcMaxForHost", &(s_pvm_conf.s_nproc),
        TCCONF_TYPE_INT, 0, 1, PVM_NUM_NODE_PROCS },
    { "MaxProcForCluster", &(s_pvm_conf.s_max_proc),
        TCCONF_TYPE_INT, 0, 1, PVM_MAX_CLUSTER_PROCS },
    { "NumElabFrameForTask", &(s_pvm_conf.s_num_frame_task),
        TCCONF_TYPE_INT, 0, 1, PVM_NUM_TASK_FRAMES },
    { "InternalMultipass", &(s_pvm_conf.s_internal_multipass),
        TCCONF_TYPE_FLAG, 0, 0, 1 },
    { "Nodes", &nodes_num, TCCONF_TYPE_INT, 0, 1, PVM_MAX_NODES },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry videomerger_conf[] = {
    { "Hostname", videomerger_hostname, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "BuildOnlyBatchMergeList",
      &(s_pvm_conf.s_video_merger.s_build_only_list),
      TCCONF_TYPE_FLAG, 0, 0, 1 },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry audiomerger_conf[] = {
    { "Hostname", audiomerger_hostname, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "BuildOnlyBatchMergeList",
      &(s_pvm_conf.s_audio_merger.s_build_only_list),
      TCCONF_TYPE_FLAG, 0, 0, 1 },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry systemmerger_conf[] = {
    { "Hostname", systemmerger_hostname, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "BuildOnlyBatchMergeList",
      &(s_pvm_conf.s_system_merger.s_build_only_list),
      TCCONF_TYPE_FLAG, 0, 0, 1 },
    { "MultiplexParams", systemmerger_mplexparams,
      TCCONF_TYPE_STRING, 0, 0, },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry exportaudiomod_conf[] = {
    { "Codec", exportaudiomod_codec, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Param1", exportaudiomod_params[0], TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Param2", exportaudiomod_params[1], TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Param3", exportaudiomod_params[2], TCCONF_TYPE_STRING, 0, 0, 0 },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry exportvideomod_conf[] = {
    { "Codec", exportvideomod_codec, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Param1", exportvideomod_params[0], TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Param2", exportvideomod_params[1], TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Param3", exportvideomod_params[2], TCCONF_TYPE_STRING, 0, 0, 0 },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry systemlist_conf[] = {
    { "Destination", systemlist_destination, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Codec", systemlist_codec, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "BuildOnlyBatchMergeList",
      &(s_pvm_conf.s_build_intermed_file),
      TCCONF_TYPE_FLAG, 0, 0, 1 },
    { "MultiplexParams", systemlist_mplexparams,
      TCCONF_TYPE_STRING, 0, 0, },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry addaudio_conf[] = {
    { "Destination", addaudio_destination, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Codec", addaudio_codec, TCCONF_TYPE_STRING, 0, 0, 0 },
    { NULL, NULL, 0, 0, 0, 0, },
};
static TCConfigEntry addvideo_conf[] = {
    { "Destination", addvideo_destination, TCCONF_TYPE_STRING, 0, 0, 0 },
    { "Codec", addvideo_codec, TCCONF_TYPE_STRING, 0, 0, 0 },
    { NULL, NULL, 0, 0, 0, 0, },
};

/*************************************************************************/

typedef struct pvm_conf_item_ PVMConfItem;
struct pvm_conf_item_ {
    const char *name;
    TCConfigEntry *conf;
    int (*dispatch)(int id, pvm_config_env *env);
    int serverside; /* flag */
    int parsed;
};

/* BIGFAT WARNING: please take a great care to keep in sync */
enum config_idx {
    CONF_PVM_HOST_CAPS_IDX = 0,
    CONF_AUDIO_MERGER_IDX,
    CONF_VIDEO_MERGER_IDX,
    CONF_SYSTEM_MERGER_IDX,
    CONF_EXPORT_AUDIO_MOD_IDX,
    CONF_EXPORT_VIDEO_MOD_IDX,
    CONF_SYSTEM_LIST_IDX,
    CONF_ADD_AUDIO_IDX,
    CONF_ADD_VIDEO_IDX,
};
static PVMConfItem pvm_config[] = {
    { "PvmHostCapability",  pvmhostcaps_conf,      dispatch_null,     0, 0, },
    { "AudioMerger",        audiomerger_conf,      dispatch_merger,   0, 0, },
    { "VideoMerger",        videomerger_conf,      dispatch_merger,   0, 0, },
    { "SystemMerger",       systemmerger_conf,     dispatch_merger,   0, 0, },
    { "ExportAudioModule",  exportaudiomod_conf,   dispatch_modules,  0, 0, },
    { "ExportVideoModule",  exportvideomod_conf,   dispatch_modules,  0, 0, },
    { "SystemList",         systemlist_conf,       dispatch_syslist,  1, 0, },
    { "AddAudio",           addaudio_conf,         dispatch_null,     1, 0, },
    { "AddVideo",           addvideo_conf,         dispatch_null,     1, 0, },
    { NULL,                 NULL,                  NULL,              0, 0, },
};

typedef struct pvm_list_item_ PVMListItem;
struct pvm_list_item_ {
    const char *name;
    pvm_config_filelist **list;
    int type;
    int parsed;
    /* dispatcher can be generic, so no need for specific one */
};
static PVMListItem pvm_filelist[] = {
    { "AddAudioList",       &s_pvm_conf.p_add_list,     TC_AUDIO, 0, },
    { "AddVideoList",       &s_pvm_conf.p_add_list,     TC_VIDEO, 0, },
    { "LogAudioList",       &s_pvm_conf.p_add_loglist,  TC_AUDIO, 0, },
    { "LogVideoList",       &s_pvm_conf.p_add_loglist,  TC_VIDEO, 0, },
    { "RemoveAudioList",    &s_pvm_conf.p_rem_list,     TC_AUDIO, 0, },
    { "RemoveVideoList",    &s_pvm_conf.p_rem_list,     TC_VIDEO, 0, },
    { NULL,                 NULL,                       0,        0, },
};

/*************************************************************************/
/* dispatcher functions */

/*
 * yes, that's ugly (and I don't really wnat expend too much on this).
 * Improvements are warmly welcome, but a rewrite from scratch is
 * probably the way to go.
 */

struct dispatch_data {
    pvm_config_filelist *head;
    pvm_config_filelist *tail;
    char                *codec;
    char                *destination;
    int                 type;
};


static int dispatch_list_item(TCListItem *item, void *userdata)
{
    struct dispatch_data *DD = userdata;
    int ret = 0;

    pvm_config_filelist *cur = tc_zalloc(sizeof(pvm_config_filelist)); // XXX

    if (!cur) {
        ret = 1;
    } else {
        cur->s_type        = DD->type;
        cur->p_codec       = DD->codec;       // softref
        cur->p_destination = DD->destination; // softref
        cur->p_filename    = item->data;      // hardref

        if (!DD->head) {
            DD->head = cur;
            DD->tail = cur;
        } else {
            DD->tail->p_next = cur;
            cur = DD->tail->p_next;
        }
    }
    return ret;
}

static pvm_config_filelist *dispatch_list(TCList *src, int type,
                                          char *codec, char *destination,
                                          pvm_config_filelist **ret)
{
    struct dispatch_data DD = {
        .head        = NULL,
        .tail        = NULL,
        .type        = type,
        .codec       = codec,
        .destination = destination,
    };

    tc_list_foreach(src, dispatch_list_item, &DD);

    if (ret) {
        *ret = DD.tail;
    }
    return DD.head;
}


static int dispatch_node(int id, PVMNodeData *data,
                         pvm_config_env *env)
{
    /* 
     * this insert nodes in reverse orders, so node defined last
     * in configuration file is first on list, but nobody really
     * cares about that.
     */
    if (env && data && data->enabled) {
        pvm_config_hosts *host = tc_zalloc(sizeof(pvm_config_hosts));
        if (host) {
            /* fill */
            host->p_hostname = data->hostname;
            host->s_nproc    = data->maxprocs;
            /* link */
            host->p_next     = env->p_pvm_hosts;
            env->p_pvm_hosts = host;

            return 1;
        }
    }
    return 0;
}


static int dispatch_merger(int id, pvm_config_env *env)
{
    switch (id) {
      case CONF_AUDIO_MERGER_IDX:
        env->s_audio_merger.p_hostname = pvm_hostname(audiomerger_hostname);
        return 1;
      case CONF_VIDEO_MERGER_IDX:
        env->s_video_merger.p_hostname = pvm_hostname(videomerger_hostname);
        return 1;
      case CONF_SYSTEM_MERGER_IDX:
        env->s_system_merger.p_hostname = pvm_hostname(systemmerger_hostname);
        tc_strstrip(systemmerger_mplexparams);
        env->p_multiplex_cmd = systemmerger_mplexparams;
        return 1;
      default: /* cannot happen */
        return 0;
    }
    return 0; /* paranoia */
}

static int dispatch_null(int id, pvm_config_env *env)
{
    return (env != NULL) ?1 :0;
}


static int dispatch_modules(int id, pvm_config_env *env)
{
    pvm_config_codec *cfg = NULL;
    char *codec = NULL;
    PVMString *params = NULL;

    switch (id) {
      case CONF_EXPORT_AUDIO_MOD_IDX:
        cfg = &(env->s_audio_codec);
        codec = exportaudiomod_codec;
        params = exportaudiomod_params;
        break;
      case CONF_EXPORT_VIDEO_MOD_IDX:
        cfg = &(env->s_video_codec);
        codec = exportvideomod_codec;
        params = exportvideomod_params;
        break;
      default: /* cannot happen */
        return 0;
    }

    tc_strstrip(codec);
    tc_strstrip(params[0]);
    tc_strstrip(params[1]);
    tc_strstrip(params[2]);
    cfg->p_codec = codec;
    cfg->p_par1  = params[0];
    cfg->p_par2  = params[1];
    cfg->p_par3  = params[2];
    return 1;
}

static int dispatch_syslist(int id, pvm_config_env *env)
{
    if (env != NULL) {
        tc_strstrip(systemlist_codec);
        tc_strstrip(systemlist_destination);
        tc_strstrip(systemlist_mplexparams);
        env->s_sys_list.p_codec         = systemlist_codec;
        env->s_sys_list.p_destination   = systemlist_destination;
        env->p_multiplex_cmd            = systemlist_mplexparams;
        return 1;
    }
    return 0;
}

/*************************************************************************/


static int parse_nodes(char *p_hostfile, int nodes)
{
    int i = 0, ret = 0, parsed = 0;
    char buf[TC_BUF_LINE];

    if (nodes > PVM_MAX_NODES) {
        tc_log_warn(__FILE__, "excessive nodes requested, autolimit to %i",
                    PVM_MAX_NODES);
        nodes = PVM_MAX_NODES;
    }

    for (i = 0; i < nodes; i++) {
        tc_snprintf(buf, sizeof(buf), "Node%i", i+1);

        ret = module_read_config(p_hostfile, buf, node_conf[i], __FILE__);
        if (ret) {
            int done = dispatch_node(i, &nodes_data[i], &s_pvm_conf);
            if (done) {
                parsed++;
            }
        }
    }
    return parsed;
}


static void parse_config(char *p_hostfile, int full)
{
    int i = 0;
    for (i = 0; pvm_config[i].name != NULL; i++) {
        int ret = 0;

        if (!full && pvm_config[i].serverside)
            continue;
        ret = module_read_config(p_hostfile, pvm_config[i].name,
                                 pvm_config[i].conf, __FILE__);
        if (ret) {
            int done = pvm_config[i].dispatch(i, &s_pvm_conf);
            pvm_config[i].parsed = done;
        }
    }
}

static void parse_filelist(char *p_hostfile)
{
    int i = 0;
    for (i = 0; pvm_filelist[i].name != NULL; i++) {
        TCList *list = module_read_config_list(p_hostfile,
                                               pvm_filelist[i].name, 
                                               __FILE__);
        if (list) {
            int type = pvm_filelist[i].type;
            char *codec = (type == TC_VIDEO) 
                                ?addvideo_codec :addaudio_codec;
            char *dest = (type == TC_VIDEO) 
                                ?addvideo_destination :addaudio_destination;
            pvm_config_filelist *tail = NULL;
            pvm_config_filelist *head = dispatch_list(list, type,
                                                      codec, dest, &tail);
            if (head) { /* then tail is valid too */
                pvm_filelist[i].parsed = 1;
                if (*(pvm_filelist[i].list) != NULL) {
                    tail->p_next = *(pvm_filelist[i].list);
                }
                *(pvm_filelist[i].list) = head;
            }
            /* always */
            module_free_config_list(list, (head != NULL) ?1 :0);
        }
    }
}

#define WAS_PARSED(IDX) pvm_config[(IDX)].parsed

static pvm_config_env *validate(int nodes, int verbose)
{
    const char *errmsg = "???";

    if (nodes < 0) {
        errmsg = "Need one PVM node configured";
        goto failed;
    }
    if (((!s_pvm_conf.s_audio_codec.p_codec) && WAS_PARSED(CONF_EXPORT_AUDIO_MOD_IDX))
     || ((!s_pvm_conf.s_video_codec.p_codec) && WAS_PARSED(CONF_EXPORT_VIDEO_MOD_IDX))) {
        errmsg = "Need at least Codec parameter in the"
                 " [ExportVideoModule] or [ExportAudioModule] section";
        goto failed;
    }
    if ((s_pvm_conf.s_system_merger.p_hostname != NULL)
     && (s_pvm_conf.p_multiplex_cmd == NULL)) {
        errmsg = "MultiplexParams parameter required in the"
                 " [SystemMerger] section";
        goto failed;
    } else if (s_pvm_conf.s_system_merger.p_hostname != NULL) {
        s_pvm_conf.s_video_merger.s_build_only_list = 1;
        s_pvm_conf.s_audio_merger.s_build_only_list = 1;
    }
    if ((s_pvm_conf.p_add_list != NULL) 
     && (s_pvm_conf.p_add_list->p_codec == NULL)
     && (WAS_PARSED(CONF_ADD_AUDIO_IDX) || WAS_PARSED(CONF_ADD_VIDEO_IDX))) {
        errmsg = "Need at least Codec parameter in the [AddList] section";
        goto failed;
    }

    /* done */
    return &s_pvm_conf;

failed:
    if (verbose) {
        tc_log_error(__FILE__, "%s", errmsg);
    }
    pvm_parser_close();
    return NULL;
}

#undef WAS_PARSED


pvm_config_env *pvm_parser_open(char *p_hostfile, int verbose, int full)
{
    int i = 0;
    /* setup defaults */
    s_pvm_conf.p_pvm_hosts = NULL;
    /* XXX: add more defaults? */
    /* get user data */
    parse_config(p_hostfile, full);
    /* get node data */
    i = parse_nodes(p_hostfile, nodes_num);
    /* get lists */
    if (full) {
        parse_filelist(p_hostfile);
    }
    /* then validate it */
    return validate(i, verbose);
}


void pvm_parser_close(void)
{
    pvm_config_hosts *p_pvm_conf_host = NULL, *p_tmp = NULL;
    pvm_config_filelist *p_pvm_conf_fileadd = NULL;
    pvm_config_filelist *p_pvm_conf_filerem = NULL;

    for (p_pvm_conf_host = s_pvm_conf.p_pvm_hosts; p_pvm_conf_host != NULL; ) {
        p_tmp = p_pvm_conf_host->p_next;
        free(p_pvm_conf_host);
        p_pvm_conf_host = p_tmp;
    }
    for (p_pvm_conf_fileadd = s_pvm_conf.p_add_list; p_pvm_conf_fileadd != NULL; ) {
        p_pvm_conf_filerem = p_pvm_conf_fileadd->p_next;
        free(p_pvm_conf_fileadd);
        p_pvm_conf_fileadd = p_pvm_conf_filerem;
    }
    for (p_pvm_conf_fileadd = s_pvm_conf.p_rem_list; p_pvm_conf_fileadd != NULL; ) {
        p_pvm_conf_filerem = p_pvm_conf_fileadd->p_next;
        free(p_pvm_conf_fileadd);
        p_pvm_conf_fileadd = p_pvm_conf_filerem;
    }
    memset(&s_pvm_conf, 0, sizeof(s_pvm_conf));
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
