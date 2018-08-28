/*
 * tcexport.c -- standalone encoder frontend for transcode
 * (C) 2006-2010 - Francesco Romani <fromani at gmail dot com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */



#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "transcode.h"
#include "export_profile.h"
#include "framebuffer.h"
#include "counter.h"
#include "probe.h"
#include "encoder.h"
#include "filter.h"
#include "socket.h"
#include "libtc/tcmodule-core.h"
#include "libtc/libtc.h"
#include "libtc/cfgfile.h"
#include "libtc/tccodecs.h"
#include "libtc/tcframes.h"

#include "rawsource.h"
#include "tcstub.h"

#define EXE "tcexport"

enum {
    STATUS_DONE = -1, /* used internally */
    STATUS_OK = 0,
    STATUS_BAD_PARAM,
    STATUS_IO_ERROR,
    STATUS_NO_MODULE,
    STATUS_MODULE_ERROR,
    STATUS_PROBE_FAILED,
    /* ... */
    STATUS_INTERNAL_ERROR = 64, /* must be the last one */
};

#define VIDEO_LOG_FILE       "mpeg4.log"
#define AUDIO_LOG_FILE       "pcm.log"

#define VIDEO_CODEC          "yuv420p"
#define AUDIO_CODEC          "pcm"

#define RANGE_STR_SEP        ","

typedef  struct tcencconf_ TCEncConf;

struct tcencconf_ {
    int dry_run; /* flag */
    vob_t *vob;

    char *video_codec;
    char *audio_codec;

    char vlogfile[32];
    char alogfile[32];

    char video_mod_buf[64];
    char audio_mod_buf[64];
    char mplex_mod_buf[64];
    
    char *video_mod;
    char *audio_mod;
    char *mplex_mod;

    char *range_str;
};


void version(void)
{
    printf("%s v%s (C) 2006-2010 Transcode Team\n",
           EXE, VERSION);
}

static void usage(void)
{
    version();
    printf("Usage: %s [options]\n", EXE);
    printf("    -d verbosity      Verbosity mode [1 == TC_INFO]\n");
    printf("    -D                dry run, only loads module (used"
           " for testing)\n");
    printf("    -m path           Use PATH as module path\n");
    printf("    -c f1-f2[,f3-f4]  encode only f1-f2[,f3-f4]"
           " (frames or HH:MM:SS) [all]\n");
    printf("    -b b[,v[,q[,m]]]  audio encoder bitrate kBits/s"
           "[,vbr[,quality[,mode]]] [%i,%i,%i,%i]\n",
           ABITRATE, AVBR, AQUALITY, AMODE);
    printf("    -i file           video input file name\n");
    printf("    -p file           audio input file name\n");
    printf("    -o file           output file (base)name\n");
    printf("    -P profile        select export profile."
           " if you want to use more than one profile,\n"
           "                      provide a comma separated list.\n");
    printf("    -N V,A            Video,Audio output format"
           " (encoder) [%s,%s]\n", VIDEO_CODEC, AUDIO_CODEC);
    printf("    -y V,A,M          Video,Audio,Multiplexor export"
           " modules [%s,%s,%s]\n", TC_DEFAULT_EXPORT_VIDEO,
           TC_DEFAULT_EXPORT_AUDIO, TC_DEFAULT_EXPORT_MPLEX);
    printf("    -w b[,k[,c]]      encoder"
           " bitrate[,keyframes[,crispness]] [%d,%d,%d]\n",
            VBITRATE, VKEYFRAMES, VCRISPNESS);
    printf("    -R n[,f1[,f2]]    enable multi-pass encoding"
           " (0-3) [%d,mpeg4.log,pcm.log]\n", VMULTIPASS);
}

static void config_init(TCEncConf *conf, vob_t *vob)
{
    conf->dry_run = TC_FALSE;
    conf->vob = vob;

    conf->range_str = NULL;

    strlcpy(conf->vlogfile, VIDEO_LOG_FILE, sizeof(conf->vlogfile));
    strlcpy(conf->alogfile, AUDIO_LOG_FILE, sizeof(conf->alogfile));

    conf->video_mod = TC_DEFAULT_EXPORT_VIDEO;
    conf->audio_mod = TC_DEFAULT_EXPORT_AUDIO;
    conf->mplex_mod = TC_DEFAULT_EXPORT_MPLEX;
}

/* split up module string (=options) to module name */
static char *setup_mod_string(char *mod)
{
    size_t modlen = strlen(mod);
    char *sep = strchr(mod, '=');
    char *opts = NULL;

    if (modlen > 0 && sep != NULL) {
        size_t optslen;

        opts = sep + 1;
        optslen = strlen(opts);

        if (!optslen) {
            opts = NULL; /* no options or bad options given */
        }
        *sep = '\0'; /* mark end of module name */
    }
    return opts;
}

/* basic sanity check */
#define VALIDATE_OPTION \
        if (optarg[0] == '-') { \
            usage(); \
            return STATUS_BAD_PARAM; \
        }

static int parse_options(int argc, char** argv, TCEncConf *conf)
{
    int ch, n;
    char acodec[32], vcodec[32];
    vob_t *vob = conf->vob;

    if (argc == 1) {
        usage();
        return STATUS_BAD_PARAM;
    }

    libtc_init(&argc, &argv);

    while (1) {
        ch = getopt(argc, argv, "b:c:Dd:hi:m:N:o:p:R:y:w:v?");
        if (ch == -1) {
            break;
        }

        switch (ch) {
          case 'D':
            conf->dry_run = TC_TRUE;
            break;
          case 'd':
            VALIDATE_OPTION;
            vob->verbose = atoi(optarg);
            break;
          case 'c':
            VALIDATE_OPTION;
            conf->range_str = optarg;
            break;
          case 'b':
            VALIDATE_OPTION;
            n = sscanf(optarg, "%i,%i,%f,%i",
                       &vob->mp3bitrate, &vob->a_vbr, &vob->mp3quality,
                       &vob->mp3mode);
            if (n < 0
              || vob->mp3bitrate < 0
              || vob->a_vbr < 0
              || vob->mp3quality < -1.00001
              || vob->mp3mode < 0) {
                tc_log_error(EXE, "invalid parameter for -b");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'i':
            VALIDATE_OPTION;
            vob->video_in_file = optarg;
            break;
          case 'm':
            VALIDATE_OPTION;
            vob->mod_path = optarg;
            break;
          case 'N':
            VALIDATE_OPTION;
	        n = sscanf(optarg,"%32[^,],%32s", vcodec, acodec);
            if (n != 2) {
                tc_log_error(EXE, "invalid parameter for option -N"
                                  " (you must specify ALL parameters)");
                return STATUS_BAD_PARAM;
            }

            vob->ex_v_codec = tc_codec_from_string(vcodec);
            vob->ex_a_codec = tc_codec_from_string(acodec);

            if (vob->ex_v_codec == TC_CODEC_ERROR
             || vob->ex_a_codec == TC_CODEC_ERROR) {
                tc_log_error(EXE, "unknown A/V format");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'p':
            VALIDATE_OPTION;
            vob->audio_in_file = optarg;
            break;
          case 'R':
            VALIDATE_OPTION;
            n = sscanf(optarg,"%d,%64[^,],%64s",
                       &vob->divxmultipass, conf->vlogfile, conf->alogfile);

            if (n == 3) {
                vob->audiologfile = conf->alogfile;
                vob->divxlogfile = conf->vlogfile;
            } else if (n == 2) {
                vob->divxlogfile = conf->vlogfile;
            } else if (n != 1) {
                tc_log_error(EXE, "invalid parameter for option -R");
                return STATUS_BAD_PARAM;
            }

            if (vob->divxmultipass < 0 || vob->divxmultipass > 3) {
                tc_log_error(EXE, "invalid multi-pass in option -R");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'o':
            VALIDATE_OPTION;
            vob->video_out_file = optarg;
            break;
          case 'w':
            VALIDATE_OPTION;
            sscanf(optarg,"%d,%d,%d",
                   &vob->divxbitrate, &vob->divxkeyframes,
                   &vob->divxcrispness);

            if (vob->divxcrispness < 0 || vob->divxcrispness > 100
              || vob->divxbitrate <= 0 || vob->divxkeyframes < 0) {
                tc_log_error(EXE, "invalid parameter for option -w");
                return STATUS_BAD_PARAM;
            }
            break;
          case 'y':
            VALIDATE_OPTION;
	        n = sscanf(optarg,"%64[^,],%64[^,],%64s",
                       conf->video_mod_buf, conf->audio_mod_buf,
                       conf->mplex_mod_buf);
            if (n != 3) {
                tc_log_error(EXE, "invalid parameter for option -y"
                                  " (you must specify ALL parameters)");
                return STATUS_BAD_PARAM;
            }
            conf->video_mod = conf->video_mod_buf;
            conf->audio_mod = conf->audio_mod_buf;
            conf->mplex_mod = conf->mplex_mod_buf;

            vob->ex_v_string = setup_mod_string(conf->video_mod);
            vob->ex_a_string = setup_mod_string(conf->audio_mod);
            vob->ex_m_string = setup_mod_string(conf->mplex_mod);
            break;
          case 'v':
            version();
            return STATUS_DONE;
          case '?': /* fallthrough */
          case 'h': /* fallthrough */
          default:
            usage();
            return STATUS_BAD_PARAM;
        }
    }
    return STATUS_OK;
}

static void setup_im_size(vob_t *vob)
{
    double fch;
    int leap_bytes1, leap_bytes2;

    /* update vob structure */
    /* assert(YUV420P source) */
    vob->im_v_size = (3 * vob->im_v_width * vob->im_v_height) / 2;
    /* borrowed from transcode.c */
    /* samples per audio frame */
    // fch = vob->a_rate/vob->ex_fps;
    /* 
     * XXX I still have to understand why we
     * doing like this in transcode.c, so I'll simplify things here
     */
    fch = vob->a_rate/vob->fps;
    /* bytes per audio frame */
    vob->im_a_size = (int)(fch * (vob->a_bits/8) * vob->a_chan);
    vob->im_a_size =  (vob->im_a_size>>2)<<2;

    fch *= (vob->a_bits/8) * vob->a_chan;

    leap_bytes1 = TC_LEAP_FRAME * (fch - vob->im_a_size);
    leap_bytes2 = - leap_bytes1 + TC_LEAP_FRAME * (vob->a_bits/8) * vob->a_chan;
    leap_bytes1 = (leap_bytes1 >>2)<<2;
    leap_bytes2 = (leap_bytes2 >>2)<<2;

    if (leap_bytes1 < leap_bytes2) {
    	vob->a_leap_bytes = leap_bytes1;
    } else {
	    vob->a_leap_bytes = -leap_bytes2;
    	vob->im_a_size += (vob->a_bits/8) * vob->a_chan;
    }
}

static void setup_ex_params(vob_t *vob)
{
    /* common */
    vob->ex_fps = vob->fps;
    vob->ex_frc = vob->im_frc;
    /* video */
    vob->ex_v_width = vob->im_v_width;
    vob->ex_v_height = vob->im_v_height;
    vob->ex_v_size = vob->im_v_size;
    /* audio */
    vob->ex_a_size = vob->im_a_size;
    /* a_rate already correctly setup */
    vob->mp3frequency = vob->a_rate;
    vob->dm_bits = vob->a_bits;
    vob->dm_chan = vob->a_chan;
}

static int setup_ranges(TCEncConf *conf)
{
    vob_t *vob = conf->vob;
    int ret = 0;

    if (conf->range_str != NULL) {
        ret = parse_fc_time_string(conf->range_str, vob->fps,
                                   RANGE_STR_SEP, vob->verbose,
                                   &vob->ttime);
    } else {
        vob->ttime = new_fc_time();
        if (vob->ttime == NULL) {
            ret = -1;
        } else {
            vob->ttime->stf = TC_FRAME_FIRST;
            vob->ttime->etf = TC_FRAME_LAST;
            vob->ttime->vob_offset = 0;
            vob->ttime->next = NULL;
        }
    }
    return ret;
}


#define MOD_OPTS(opts) (((opts) != NULL) ?((opts)) :"none")
static void print_summary(TCEncConf *conf, int verbose)
{
    vob_t *vob = conf->vob;

    version();
    if (verbose >= TC_INFO) {
        tc_log_info(EXE, "M: %-16s | %s", "destination",
                    vob->video_out_file);
        tc_log_info(EXE, "E: %-16s | %i,%i kbps", "bitrate(A,V)",
                    vob->divxbitrate, vob->mp3bitrate);
        tc_log_info(EXE, "E: %-16s | %s,%s", "logfile (A,V)",
                    vob->divxlogfile, vob->audiologfile);
        tc_log_info(EXE, "V: %-16s | %s (options=%s)", "encoder",
                    conf->video_mod, MOD_OPTS(vob->ex_v_string));
        tc_log_info(EXE, "A: %-16s | %s (options=%s)", "encoder",
                    conf->audio_mod, MOD_OPTS(vob->ex_a_string));
        tc_log_info(EXE, "M: %-16s | %s (options=%s)", "format",
                    conf->mplex_mod, MOD_OPTS(vob->ex_m_string));
        tc_log_info(EXE, "M: %-16s | %.3f", "fps", vob->fps);
        tc_log_info(EXE, "V: %-16s | %ix%i", "picture size",
                    vob->im_v_width, vob->im_v_height);
        tc_log_info(EXE, "V: %-16s | %i", "bytes per frame",
                    vob->im_v_size);
        tc_log_info(EXE, "V: %-16s | %i", "pass", vob->divxmultipass);
        tc_log_info(EXE, "A: %-16s | %i,%i,%i", "rate,chans,bits",
                    vob->a_rate, vob->a_chan, vob->a_bits);
        tc_log_info(EXE, "A: %-16s | %i", "bytes per frame",
                    vob->im_a_size);
        tc_log_info(EXE, "A: %-16s | %i@%i", "adjustement",
                         vob->a_leap_bytes, vob->a_leap_frame);
    }
}
#undef MOD_OPTS

/************************************************************************/

#define EXIT_IF(cond, msg, status) \
    if((cond)) { \
        tc_log_error(EXE, msg); \
        return status; \
    }

#define GET_MODULE(mod) ((mod) != NULL) ?(mod) :"null"

int main(int argc, char *argv[])
{
    int ret = 0, status = STATUS_OK;
    double samples = 0;
    /* needed by some modules */
    TCFactory factory = NULL;
    const TCExportInfo *info = NULL;
    TCVHandle tcv_handle = tcv_init();
    TCEncConf config;
    vob_t *vob = tc_get_vob();

    /* reset some fields */
    vob->audiologfile = AUDIO_LOG_FILE;
    vob->divxlogfile = VIDEO_LOG_FILE;

    ac_init(AC_ALL);
    tc_set_config_dir(NULL);
    config_init(&config, vob);
    counter_on();

    filter[0].id = 0; /* to make gcc happy */

    /* we want to modify real argc/argv pair */
    ret = tc_setup_export_profile(&argc, &argv);
    if (ret < 0) {
        /* error, so bail out */
        return STATUS_BAD_PARAM;
    }
    info = tc_load_export_profile();
    config.audio_mod = GET_MODULE(info->audio.module);
    config.video_mod = GET_MODULE(info->video.module);
    config.mplex_mod = GET_MODULE(info->mplex.module);
    tc_export_profile_to_vob(info, vob);

    ret = parse_options(argc, argv, &config);
    if (ret != STATUS_OK) {
        return (ret == STATUS_DONE) ?STATUS_OK :ret;
    }
    if (vob->ex_v_codec == TC_CODEC_ERROR
     || vob->ex_a_codec == TC_CODEC_ERROR) {
        tc_log_error(EXE, "bad export codec/format (use -N)");
        return STATUS_BAD_PARAM;
    }
    verbose = vob->verbose;
    ret = probe_source(vob->video_in_file, vob->audio_in_file,
                       1, 0, vob);
    if (!ret) {
        return STATUS_PROBE_FAILED;
    }

    samples = TC_AUDIO_SAMPLES_IN_FRAME(vob->a_rate, vob->ex_fps);
    vob->im_a_size = tc_audio_frame_size(samples, vob->a_chan, vob->a_bits,
                                         &vob->a_leap_bytes);
    vob->im_v_size = tc_video_frame_size(vob->im_v_width, vob->im_v_height,
                                         vob->im_v_codec);
    setup_im_size(vob);
    setup_ex_params(vob);
    ret = setup_ranges(&config);
    if (ret != 0) {
        tc_log_error(EXE, "error using -c option."
                          " Recheck your frame ranges!");
        return STATUS_BAD_PARAM;
    }
    print_summary(&config, verbose);

    factory = tc_new_module_factory(vob->mod_path, vob->verbose);
    EXIT_IF(!factory, "can't setup module factory", STATUS_MODULE_ERROR);

    /* open the A/V source */
    ret = tc_rawsource_open(vob);
    EXIT_IF(ret != 2, "can't open input sources", STATUS_IO_ERROR);

    EXIT_IF(tc_rawsource_buffer == NULL, "can't get rawsource handle",
            STATUS_IO_ERROR);
    ret = tc_export_init(tc_rawsource_buffer, factory);
    EXIT_IF(ret != 0, "can't setup export subsystem", STATUS_MODULE_ERROR);

    ret = tc_export_setup(vob,
                       config.audio_mod, config.video_mod, config.mplex_mod);
    EXIT_IF(ret != 0, "can't setup export modules", STATUS_MODULE_ERROR);

    if (!config.dry_run) {
        struct fc_time *tstart = NULL;
        int last_etf = 0;
        ret = tc_encoder_init(vob);
        EXIT_IF(ret != 0, "can't initialize encoder", STATUS_INTERNAL_ERROR);

        ret = tc_encoder_open(vob);
        EXIT_IF(ret != 0, "can't open encoder files", STATUS_IO_ERROR);

        /* first setup counter ranges */
        counter_reset_ranges();
    	for (tstart = vob->ttime; tstart != NULL; tstart = tstart->next) {
	        if (tstart->etf == TC_FRAME_LAST) {
                // variable length range, oh well
                counter_reset_ranges();
                break;
            }
            if (tstart->stf > last_etf) {
                counter_add_range(last_etf, tstart->stf-1, 0);
            }
            counter_add_range(tstart->stf, tstart->etf-1, 1);
            last_etf = tstart->etf;
    	}

        /* ok, now we can do the real (ranged) encoding */
        for (tstart = vob->ttime; tstart != NULL; tstart = tstart->next) {
            tc_encoder_loop(vob, tstart->stf, tstart->etf);
            printf("\n"); /* dont' mess (too much) counter output */
        }

        ret = tc_encoder_stop();
        ret = tc_encoder_close();

    }

    tc_export_shutdown();

    ret = tc_rawsource_close();
    ret = tc_del_module_factory(factory);
    tcv_free(tcv_handle);
    free_fc_time(vob->ttime);
    tc_cleanup_export_profile();

    if(verbose >= TC_INFO) {
        long encoded = tc_get_frames_encoded();
        long dropped = - tc_get_frames_dropped();
    	long cloned  = tc_get_frames_cloned();

        tc_log_info(EXE, "encoded %ld frames (%ld dropped, %ld cloned),"
                         " clip length %6.2f s",
                    encoded, dropped, cloned, encoded/vob->fps);
    }
    return status;
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
