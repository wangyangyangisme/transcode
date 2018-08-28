/*
 * cmdline.h -- header for transcode command line parser
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef _CMDLINE_H
#define _CMDLINE_H

/*************************************************************************/

/* The parsing routine: */
extern int parse_cmdline(int argc, char **argv, vob_t *vob);

/* Global variables from transcode.c that should eventually go away. */
extern int core_mode;
extern char *im_aud_mod, *im_vid_mod;
extern char *ex_aud_mod, *ex_vid_mod, *ex_mplex_mod;
extern char *plugins_string;
extern char *nav_seek_file, *socket_file, *chbase, //*dirbase,
            base[TC_BUF_MIN];
extern int psu_frame_threshold;
extern int no_vin_codec, no_ain_codec, no_v_out_codec, no_a_out_codec;
extern int frame_a, frame_b, splitavi_frames, psu_mode;
extern int preset_flag, auto_probe, seek_range;
extern int no_audio_adjust, no_split;
extern char *fc_ttime_string;
extern int sync_seconds;
extern pid_t writepid;

/*************************************************************************/

#endif  /* _CMDLINE_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
