/*
 *  filter_fields.c
 *
 *  Copyright (C) Alex Stewart - July 2002
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
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*****************************************************************************
 *              Standard Transcode Filter Defines and Includes               *
 *****************************************************************************/

#define MOD_NAME    "filter_fields.so"
#define MOD_VERSION "v0.1.1 (2003-01-21)"
#define MOD_CAP     "Field adjustment plugin"
#define MOD_AUTHOR  "Alex Stewart"

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"

/*****************************************************************************
 *                    Global Filter Variables and Defines                    *
 *****************************************************************************/

static const char *help_text[] = {
  "",
  "Transcode field-adjustment filter (filter_fields) help",
  "------------------------------------------------------",
  "",
  "The 'fields' filter is designed to shift, reorder, and",
  "generally rearrange independent fields of an interlaced",
  "video input.  Input retrieved from broadcast (PAL, NTSC,",
  "etc) video sources generally comes in an interlaced form",
  "where each pass from top to bottom of the screen displays",
  "every other scanline, and then the next pass displays the",
  "lines between the lines from the first pass.  Each pass is",
  "known as a \"field\" (there are generally two fields per",
  "frame).  When this form of video is captured and manipulated",
  "digitally, the two fields of each frame are usually merged",
  "together into one flat (planar) image per frame.  This",
  "usually produces reasonable results, however there are",
  "conditions which can cause this merging to be performed",
  "incorrectly or less-than-optimally, which is where this",
  "filter can help.",
  "",
  "The following options are supported for this filter",
  "(they can be separated by colons):",
  "",
  "  shift - Shift the video by one field (half a frame),",
  "          changing frame boundaries appropriately.  This is",
  "          useful if a video capture started grabbing video",
  "          half a frame (one field) off from where frame",
  "          boundaries were actually intended to be.",
  "",
  "  flip  - Exchange the top field and bottom field of each",
  "          frame.  This can be useful if the video signal was",
  "          sent \"bottom field first\" (which can happen",
  "          sometimes with PAL video sources) or other",
  "          oddities occurred which caused the frame",
  "          boundaries to be at the right place, but the",
  "          scanlines to be swapped.",
  "",
  "  flip_first",
  "        - Normally shifting is performed before flipping if",
  "          both are specified.  This option reverses that",
  "          behavior.  You should not normally need to use",
  "          this unless you have some extremely odd input",
  "          material, it is here mainly for completeness.",
  "",
  "  help  - Print this text.",
  "",
  "Note: the 'shift' function may produce slight color",
  "discrepancies if YUV is used as the internal transcode",
  "video format.  This is because YUV does not contain enough",
  "information to do field shifting cleanly. For best (but",
  "slower) results, use RGB mode (-V rgb24) for field",
  "shifting.",
  "",
  0 // End of Text
};

#define FIELD_OP_FLIP    0x01
#define FIELD_OP_SHIFT   0x02
#define FIELD_OP_REVERSE 0x04

#define FIELD_OP_SHIFTFLIP (FIELD_OP_SHIFT | FIELD_OP_FLIP)
#define FIELD_OP_FLIPSHIFT (FIELD_OP_SHIFTFLIP | FIELD_OP_REVERSE)

static vob_t *vob = NULL;
static char *buffer = NULL;
static int buf_field = 0;

static int field_ops = 0;
static int rgb_mode;

/*****************************************************************************
 *                         Filter Utility Functions                          *
 *****************************************************************************/

/* show_help - Print the contents of help_text for the user.
 */
static void show_help(void) {
  const char **line;

  for (line=help_text; *line; line++) {
    tc_log_info(MOD_NAME, "%s", *line);
  }
}

/* copy_field - Copy one field of a frame (every other line) from one buffer
 *              to another.
 */
static inline void copy_field(char *to, char *from, int width, int height) {
  int increment = width << 1;

  height >>= 1;
  while (height--) {
    ac_memcpy(to, from, width);
    to += increment;
    from += increment;
  }
}

/* swap_fields - Exchange one field of a frame (every other line) with another
 *               NOTE:  This function uses 'buffer' as a temporary space.
 */
static inline void swap_fields(char *f1, char *f2, int width, int height) {
  int increment = width * 2;

  height /= 2;
  while (height--) {
    ac_memcpy(buffer, f1, width);
    ac_memcpy(f1, f2, width);
    ac_memcpy(f2, buffer, width);
    f1 += increment;
    f2 += increment;
  }
}

/*****************************************************************************
 *                           Main Filter Functions                           *
 *****************************************************************************/

static int filter_fields_get_config(char *options) {
    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYE", "1");
    optstr_param (options, "flip",
	    "Exchange the top field and bottom field of each frame", "", "0");
    optstr_param (options, "shift",
	    "Shift the video by one field", "", "0");
    optstr_param (options, "flip_first",
	    "Normally shifting is performed before flipping, this option reverses that",
	    "", "0");
    return 0;
}

static int filter_fields_init(char *options) {
  int help_shown = 0;

  vob = tc_get_vob();
  if (!vob) return -1;

  if (verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

  buffer = tc_malloc(SIZE_RGB_FRAME);
  if (!buffer) {
    tc_log_error(MOD_NAME, "Unable to allocate memory.  Aborting.");
    return -1;
  }

  // Some of the data in buffer may get used for half of the first frame (when
  // shifting) so make sure it's blank to start with.
  memset(buffer, 0, SIZE_RGB_FRAME);

  if(options != NULL) {
    if (optstr_lookup (options, "flip") != NULL)
      field_ops |= FIELD_OP_FLIP;
    if (optstr_lookup (options, "shift") != NULL)
      field_ops |= FIELD_OP_SHIFT;
    if (optstr_lookup (options, "flip_first") != NULL)
      field_ops |= FIELD_OP_REVERSE;

    if (optstr_lookup (options, "help") != NULL) {
      show_help();
      help_shown = 1;
    }
  }

  // FIELD_OP_REVERSE (aka flip_first) only makes sense if we're doing
  // both operations.  If we're not, unset it.
  if (field_ops != FIELD_OP_FLIPSHIFT)
    field_ops &= ~FIELD_OP_REVERSE;

  if(verbose) {
    if (field_ops & FIELD_OP_SHIFT)
      tc_log_info(MOD_NAME, "Adjusting frame positions (shift)");
    if (field_ops & FIELD_OP_FLIP)
      tc_log_info(MOD_NAME, "Transposing input fields  (flip)");
    if (field_ops & FIELD_OP_REVERSE)
      tc_log_info(MOD_NAME, "Flipping will occur before shifting (flip_first)");
  }

  if (!field_ops) {
    tc_log_warn(MOD_NAME, "No operations specified to perform.");
    if (!help_shown) {
      tc_log_warn(MOD_NAME, "Use the 'help' option for more information.\n");
    }
    return -1;
  }

  rgb_mode = (vob->im_v_codec == CODEC_RGB);

  return 0;
}

static int filter_fields_video_frame(vframe_list_t *ptr) {
  int width = ptr->v_width * (rgb_mode ? 3 : 1);
  int height = ptr->v_height;
  char *f1 = ptr->video_buf;
  char *f2 = ptr->video_buf + width;
  char *b1 = buffer;
  char *b2 = buffer + width;

  switch (field_ops) {
    case FIELD_OP_FLIP:
      swap_fields(f1, f2, width, height);
      break;
    case FIELD_OP_SHIFT:
      copy_field(buf_field ? b2 : b1, f2, width, height);
      copy_field(f2, f1, width, height);
      copy_field(f1, buf_field ? b1 : b2, width, height);
      break;
    case FIELD_OP_SHIFTFLIP:
      // Shift + Flip is the same result as just delaying the second field by
      // one frame, so do that because it's faster.
      copy_field(buf_field ? b1 : b2, f2, width, height);
      copy_field(f2, buf_field ? b2 : b1, width, height);
      break;
    case FIELD_OP_FLIPSHIFT:
      // Flip + Shift is the same result as just delaying the first field by
      // one frame, so do that because it's faster.
      copy_field(buf_field ? b1 : b2, f1, width, height);
      copy_field(f1, buf_field ? b2 : b1, width, height);

      // Chroma information is usually taken from the top field, which we're
      // shifting here.  We probably should move the chroma info with it, but
      // this will be used so rarely (and this is only an issue in YUV mode
      // anyway, which is not reccomended to start with) that it's probably not
      // worth bothering.
      break;
  }
  buf_field ^= 1;

  return 0;
}

static int filter_fields_close(void) {
  free(buffer);
  buffer=NULL;
  return 0;
}

/*****************************************************************************
 *                            Filter Entry Point                             *
 *****************************************************************************/

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;

  if(ptr->tag & TC_FILTER_INIT) {
    return filter_fields_init(options);
  }

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
    return filter_fields_get_config(options);
  }

  if(ptr->tag & TC_FILTER_CLOSE) {
    return filter_fields_close();
  }

  // This filter is a video-only filter, which hooks into the single-threaded
  // preprocessing stage. (we need to be single-threaded because field-shifting
  // relies on getting the frames in the correct order)

  if(ptr->tag & TC_PRE_S_PROCESS && ptr->tag & TC_VIDEO) {
    return filter_fields_video_frame(ptr);
  }

  return 0;
}
