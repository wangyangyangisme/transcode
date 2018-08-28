/*
 * export_profile.c -- transcode export profile support code - implementation
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


#include <unistd.h>

#include "export_profile.h"
#include "libtc/libtc.h"
#include "libtc/cfgfile.h"
#include "libtc/tccodecs.h"

/* OK, that's quite ugly but I found nothing better, yet.*/
#ifdef TCEXPORT_PROFILE
/* same value for both macros */
# define TC_EXPORT_PROFILE_OPT     "-P"
#else
# define TC_EXPORT_PROFILE_OPT     "--export_prof"
#endif

#define USER_PROF_PATH   ".transcode/profiles"

/* all needed support variables/data packed in a nice structure */
typedef struct tcexportprofile_ TCExportProfile;

struct tcexportprofile_ {
    size_t profile_count;
    char  **profiles;

    TCExportInfo info;

    /* auxiliary variables */
    const char *video_codec;
    const char *audio_codec;

    const char *pre_clip_area;
    const char *post_clip_area;
};

#define CLIP_AREA_INIT  { 0, 0, 0, 0 }

/* used in tc_log_*() calls */
const char *package = __FILE__;

static TCExportProfile prof_data = {
    .profile_count = 0,
    .profiles = NULL,

    .video_codec = NULL,
    .audio_codec = NULL,
    .pre_clip_area = NULL,
    .post_clip_area = NULL,

    /*
     * we need to take care of strings deallocating
     * them between module_read_config() calls, to
     * avoid memleaks, so we use NULL marking here.
     */
    .info.video.string = NULL,
    .info.video.module = NULL,
    .info.video.module_opts = NULL,
    .info.video.log_file = NULL,

    .info.audio.string = NULL,
    .info.audio.module = NULL,
    .info.audio.module_opts = NULL,

    .info.mplex.string = NULL,
    .info.mplex.module = NULL,
    .info.mplex.module_opts = NULL,
    .info.mplex.out_file = NULL,
    .info.mplex.out_file_aux = NULL,

    /* standard initialization */
    .info.video.width = PAL_W,
    .info.video.height = PAL_H,
    .info.video.keep_asr_flag = TC_FALSE,
    .info.video.fast_resize_flag = TC_FALSE,
    .info.video.zoom_interlaced_flag = TC_FALSE,
    .info.video.pre_clip = CLIP_AREA_INIT,
    .info.video.post_clip = CLIP_AREA_INIT,
    .info.video.frc = 3, // XXX (magic number)
    .info.video.asr = -1, // XXX
    .info.video.par = 0,
    .info.video.encode_fields = TC_ENCODE_FIELDS_PROGRESSIVE,
    .info.video.gop_size = VKEYFRAMES,
    .info.video.quantizer_min = VMINQUANTIZER,
    .info.video.quantizer_max = VMAXQUANTIZER,
    .info.video.format = CODEC_NULL,
    .info.video.quality = -1,
    .info.video.bitrate = VBITRATE,
    .info.video.bitrate_max = VBITRATE,
    .info.video.pass_number = VMULTIPASS,

    .info.audio.format = CODEC_NULL,
    .info.audio.quality = -1,
    .info.audio.bitrate = ABITRATE,
    .info.audio.sample_rate = RATE,
    .info.audio.sample_bits = BITS,
    .info.audio.channels = CHANNELS,
    .info.audio.mode = AMODE,
    .info.audio.vbr_flag = TC_FALSE,
    .info.audio.flush_flag = TC_FALSE,
    .info.audio.bit_reservoir = TC_TRUE,
};

/* private helpers: declaration *******************************************/

/*
 * tc_load_single_export_profile:
 *      tc_load_export_profile backend.
 *      Find and load, by looking into user profile path then into system
 *      profiles path, the i-th selected profile.
 *      If profile can't be loaded, it will just skipped.
 *      If verbose >= TC_DEBUG and the profile wasn't loaded, notify
 *      the user using tc_log*.
 *      If verbose >= TC_INFO and profile was loaded, notify the user
 *      using tc_log*.
 *
 * Parameters:
 *          i: load the i-th already parsed (see below) export profile.
 *     config: use this TCConfigEntry array, provided by frontend, to
 *             parse profile data file
 *   sys_path: system path to look for profile data
 *  user_path: user path to look for profile data
 * Return value:
 *      1: profile data succesfully loaded
 *      0: profile data not loaded for some reasons (profile file
 *         not found or not readable).
 * Side effects:
 *      Isn't a proper side effect, anyway it's worth to note that
 *      this function _will_ alter some data not explicitely provided,
 *      via config parameter. Note that this function WILL NOT alter
 *      config data, so caller providing config data will have full
 *      control of those unproper side effects by careful craft of
 *      TCConfigEntry data.
 *      Also note that this function mangle global private prof_data
 *      variable, most notably by invoking cleanup_strings on it
 *      to avoid memleaks in subsequent calls of module_read_config.
 * Preconditions:
 *      this function should be always used _after_ a succesfull
 *      call to tc_setup_export_profiles, which parse the profiles
 *      selected by user. Otherwise it's still safe to call this function,
 *      but it always fail.
 */
static int tc_load_single_export_profile(int i, TCConfigEntry *config,
                                         const char *sys_path,
                                         const char *user_path);

/* utilities used internally (yet) */

/*
 * cleanup_strings:
 *      free()s and reset to NULL every not-NULL string in a given
 *      TCExportInfo structure
 *
 * Parameters:
 *      info: pointero to a TCExportInfo structure to cleanup.
 * Return value:
 *      None.
 */
static void cleanup_strings(TCExportInfo *info);

/*
 * setup_clip_area:
 *      helper to parse a clipping area string into a TCArea structure.
 *      Automagically expand the clipping information using the same
 *      logic of transcode core (actually this code is a very
 *      little more than a rip-off form src/transcode.c).
 *
 * Parameters:
 *      str: clipping area string to parse.
 *     area: pointero to a TCArea where clipping parameters will be stored.
 * Return value:
 *      1: succesfull
 *     -1: error: malformed clipping string or bad parameters.
 */
static int setup_clip_area(const char *str, TCArea *area);


/*************************************************************************/

int tc_setup_export_profile(int *argc, char ***argv)
{
    const char *optval = NULL;
    int ret;

    if (argc == NULL || argv == NULL) {
        tc_log_warn(package, "tc_setup_export_profile: bad data reference");
        return -2;
    }

    /* guess package name from command line */
    package = (*argv)[0];
    ret = tc_mangle_cmdline(argc, argv,
                            TC_EXPORT_PROFILE_OPT, &optval);
    if (ret == 0) { /* success */
        prof_data.profiles = tc_strsplit(optval, ',',
                                         &prof_data.profile_count);
        ret = (int)prof_data.profile_count;
        if (verbose >= TC_INFO) {
            tc_log_info(package, "E: %-16s | %i", "profiles parsed", ret);
        }
    }
    return ret;
}

void tc_cleanup_export_profile(void)
{
    tc_strfreev(prof_data.profiles);
    prof_data.profile_count = 0;

    cleanup_strings(&prof_data.info);
}

const TCExportInfo *tc_load_export_profile(void)
{
    /* not all settings will be accessible from here */
    /* note static here */
    static TCConfigEntry profile_conf[] = { 
        /* video stuff */
        { "video_codec", &(prof_data.video_codec),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "video_module", &(prof_data.info.video.module),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "video_module_options", &(prof_data.info.video.module_opts),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "video_fourcc", &(prof_data.info.video.string),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "video_bitrate", &(prof_data.info.video.bitrate),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 12000000 },
        { "video_bitrate_max", &(prof_data.info.video.bitrate_max),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 12000000 },
        { "video_gop_size", &(prof_data.info.video.gop_size),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 2000 },
        { "video_encode_fields", &(prof_data.info.video.encode_fields),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 3 },
        // FIXME: switch to char/string?
        { "video_frc", &(prof_data.info.video.frc),
                            TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 5 },
        { "video_asr", &(prof_data.info.video.asr),
                            TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 9 },
        { "video_par", &(prof_data.info.video.par),
                            TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 9 },
        // FIXME: expand achronym?
        { "video_pre_clip", &(prof_data.pre_clip_area),
                            TCCONF_TYPE_STRING, 0, 0, 0 },
        { "video_post_clip", &(prof_data.post_clip_area),
                            TCCONF_TYPE_STRING, 0, 0, 0 },
        { "video_width", &(prof_data.info.video.width),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE,
                        1, TC_MAX_V_FRAME_WIDTH },
        { "video_height", &(prof_data.info.video.height),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE,
                        1, TC_MAX_V_FRAME_HEIGHT },
        { "video_keep_asr", &(prof_data.info.video.keep_asr_flag),
                        TCCONF_TYPE_FLAG, 0, 0, 1 },
        { "video_fast_resize", &(prof_data.info.video.fast_resize_flag),
                        TCCONF_TYPE_FLAG, 0, 0, 1 },
        { "video_zoom_interlaced", &(prof_data.info.video.zoom_interlaced_flag),
                        TCCONF_TYPE_FLAG, 0, 0, 1 },
        /* audio stuff */
        { "audio_codec", &(prof_data.audio_codec),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "audio_module", &(prof_data.info.audio.module),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "audio_module_options", &(prof_data.info.audio.module_opts),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "audio_bitrate", &(prof_data.info.audio.bitrate),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 1000000 },
        { "audio_frequency", &(prof_data.info.audio.sample_rate),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 48000 },
                        // XXX: review min
        { "audio_bits", &(prof_data.info.audio.sample_bits),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 8, 16 }, // XXX
        { "audio_channels", &(prof_data.info.audio.channels),
                        TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 2 },
        /* multiplexing */
        { "mplex_module", &(prof_data.info.mplex.module),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { "mplex_module_options", &(prof_data.info.mplex.module_opts),
                        TCCONF_TYPE_STRING, 0, 0, 0 },
        { NULL, NULL, 0, 0, 0, 0 }
    };
    char home_path[PATH_MAX + 1];
    const char *home = getenv("HOME");
    int i = 0;

    if (home != NULL) {
        tc_snprintf(home_path, sizeof(home_path), "%s/%s",
                    home, USER_PROF_PATH);
    } else {
        tc_log_warn(package, "can't determinate home directory!");
        return NULL;
    }

    for (i = 0; i < prof_data.profile_count; i++) {
        tc_load_single_export_profile(i, profile_conf,
                                      PROF_PATH, home_path);
    }
    return &prof_data.info;
}

/* it's pretty naif yet. FR */
void tc_export_profile_to_vob(const TCExportInfo *info, vob_t *vob)
{
    if (info == NULL || vob == NULL) {
        return;
    }
    vob->ex_v_string = info->video.module_opts;
    vob->ex_a_string = info->audio.module_opts;
    vob->ex_m_string = info->mplex.module_opts;
    vob->ex_v_codec = info->video.format;
    vob->ex_a_codec = info->audio.format;
    vob->ex_v_fcc = info->video.string;
    vob->ex_frc = info->video.frc;
    vob->ex_asr = info->video.asr;
    vob->ex_par = info->video.par;
    vob->encode_fields = info->video.encode_fields;
    vob->divxbitrate = info->video.bitrate;
    vob->mp3bitrate = info->audio.bitrate;
    vob->video_max_bitrate = info->video.bitrate_max;
    vob->divxkeyframes = info->video.gop_size;
    vob->mp3frequency = info->audio.sample_rate;
    vob->dm_bits = info->audio.sample_bits;
    vob->dm_chan = info->audio.channels;
    vob->mp3mode = info->audio.mode;
    vob->bitreservoir = info->audio.bit_reservoir;
    vob->zoom_interlaced = info->video.zoom_interlaced_flag;
    if (info->video.fast_resize_flag) {
        tc_compute_fast_resize_values(vob, TC_FALSE);
    } else {
        vob->zoom_width = info->video.width;
        vob->zoom_height = info->video.height;
    }
}

/*************************************************************************
 * private helpers: implementation
 **************************************************************************/

#define SETUP_CODEC(TYPE) do { \
    int codec = 0; /* shortcut  */\
    if (prof_data.TYPE ## _codec != NULL) { \
        codec  = tc_codec_from_string(prof_data.TYPE ## _codec); \
        prof_data.info.TYPE.format = codec; \
        tc_free((char*)prof_data.TYPE ## _codec); /* avoid const warning */ \
        prof_data.TYPE ## _codec = NULL; \
    } \
} while (0)

#define SETUP_CLIPPING(TYPE) do { \
    if (prof_data.TYPE ## _clip_area != NULL) { \
        memset(&(prof_data.info.video.TYPE ## _clip), 0, sizeof(TCArea)); \
        setup_clip_area(prof_data.TYPE ## _clip_area, \
                        &(prof_data.info.video.TYPE ## _clip)); \
        tc_free((char*)prof_data.TYPE ## _clip_area); /* avoid const warning */ \
        prof_data.TYPE ## _clip_area = NULL; \
    } \
} while (0)

static int tc_load_single_export_profile(int i, TCConfigEntry *config,
                                         const char *sys_path,
                                         const char *user_path)
{
    int found = 0, ret = 0;
    char path_buf[PATH_MAX+1];
    const char *basedir = NULL;

    if (sys_path == NULL || user_path == NULL || config == NULL
     || ((i < 0) || i >= prof_data.profile_count)) {
        /* paranoia */
        tc_log_warn(package, "tc_load_single_export_profile:"
                             " bad data reference");
        return -1;
    }

    tc_snprintf(path_buf, sizeof(path_buf), "%s/%s.cfg",
                user_path, prof_data.profiles[i]);
    ret = access(path_buf, R_OK);
    if (ret == 0) {
        found = 1;
        basedir = user_path;
    } else {
        tc_snprintf(path_buf, sizeof(path_buf), "%s/%s.cfg",
                    sys_path, prof_data.profiles[i]);
        ret = access(path_buf, R_OK);
        if (ret == 0) {
            found = 1;
            basedir = PROF_PATH;
        }
    }

    if (found) {
        char prof_name[TC_BUF_MIN];
        cleanup_strings(&prof_data.info);
        tc_snprintf(prof_name, sizeof(prof_name), "%s.cfg",
                    prof_data.profiles[i]);

        tc_set_config_dir(basedir);
        ret = module_read_config(prof_name, NULL, config, package);
        if (ret == 0) {
            found = 0; /* module_read_config() failed */
        } else {
            if (verbose >= TC_INFO) {
                tc_log_info(package, "E: %-16s | %s", "loaded profile",
                            path_buf);
            }
            SETUP_CODEC(video);
            SETUP_CODEC(audio);
            SETUP_CLIPPING(pre);
            SETUP_CLIPPING(post);
        }
    } else {
        if (verbose >= TC_DEBUG) {
            tc_log_warn(package, "E: %-16s | %s (skipped)", "unable to load",
                        path_buf);
        }
    }
    return found;
}

#undef SETUP_CODEC
#undef SETUP_CLIPPING

/*
 * module_read_config (used internally, see later)
 * allocates new strings for option values, so
 * we need to take care of them using this couple
 * of functions
 */

#define CLEANUP_STRING(FIELD) do { \
    if (info->FIELD != NULL) {\
        tc_free(info->FIELD); \
        info->FIELD = NULL; \
    } \
} while (0)

static void cleanup_strings(TCExportInfo *info)
{
    if (info != NULL) {
        /* paranoia */

        CLEANUP_STRING(video.string);
        CLEANUP_STRING(video.module);
        CLEANUP_STRING(video.module_opts);
        CLEANUP_STRING(video.log_file);

        CLEANUP_STRING(audio.string);
        CLEANUP_STRING(audio.module);
        CLEANUP_STRING(audio.module_opts);

        CLEANUP_STRING(mplex.string);
        CLEANUP_STRING(mplex.module);
        CLEANUP_STRING(mplex.module_opts);
        CLEANUP_STRING(mplex.out_file);
        CLEANUP_STRING(mplex.out_file_aux);
    }
}

#undef CLEANUP_STRING

/*************************************************************************/

static int setup_clip_area(const char *str, TCArea *area)
{
    int n = sscanf(str, "%i,%i,%i,%i",
                   &area->top, &area->left, &area->bottom, &area->right);
    if (n < 0) {
        return -1;
    }

    /* symmetrical clipping for only 1-3 arguments */
    if (n == 1 || n == 2) {
        area->bottom = area->top;
    }
    if (n == 2 || n == 3) {
        area->right = area->left;
    }

    return 1;
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
