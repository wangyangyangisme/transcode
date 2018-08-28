/*
 *  import_imlist.c
 *
 *  Copyright (C) Thomas Oestreich - February 2002
 *  port to MagickWand API:
 *  Copyright (C) Francesco Romani - July 2007
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

#define MOD_NAME    "import_imlist.so"
#define MOD_VERSION "v0.1.1 (2007-08-04)"
#define MOD_CODEC   "(video) RGB"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* Note: because of ImageMagick bogosity, this must be included first, so
 * we can undefine the PACKAGE_* symbols it splats into our namespace */
#ifdef HAVE_BROKEN_WAND
#include <wand/magick-wand.h>
#else /* we have a SANE wand header */
#include <wand/MagickWand.h>
#endif /* HAVE_BROKEN_WAND */

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include <stdlib.h>
#include <stdio.h>

#include "src/transcode.h"

static int verbose_flag = TC_QUIET;
static int capability_flag = TC_CAP_RGB|TC_CAP_VID;

#define MOD_PRE imlist
#include "import_def.h"


static int TCHandleMagickError(MagickWand *wand)
{
    ExceptionType severity;
    const char *description = MagickGetException(wand, &severity);

    tc_log_error(MOD_NAME, "%s", description);

    MagickRelinquishMemory((void*)description);
    return TC_IMPORT_ERROR;
}

static int width = 0, height = 0;
static FILE *fd = NULL;
static MagickWand *wand = NULL;

/* ------------------------------------------------------------
 *
 * open stream
 *
 * ------------------------------------------------------------*/

MOD_open
{
    if (param->flag == TC_AUDIO) {
        return(TC_IMPORT_OK);
    }

    if (param->flag == TC_VIDEO) {
        param->fd = NULL;

        width  = vob->im_v_width;
        height = vob->im_v_height;

        tc_log_warn(MOD_NAME,
                    "This module is DEPRECATED.");
        tc_log_warn(MOD_NAME,
                    "Please consider to use the multi input mode"
                    " (--multi_input) with import_im module.");
        tc_log_warn(MOD_NAME,
                    "(e.g.) transcode --multi_input -x im ...");

        fd = fopen(vob->video_in_file, "r");
        if (fd == NULL) {
            return TC_IMPORT_ERROR;
        }

        MagickWandGenesis();
        wand = NewMagickWand();

        if (wand == NULL) {
            tc_log_error(MOD_NAME, "cannot create magick wand");
            return TC_IMPORT_ERROR;
        }

        return TC_IMPORT_OK;
    }

    return TC_IMPORT_ERROR;
}


/* ------------------------------------------------------------
 *
 * decode  stream
 *
 * ------------------------------------------------------------*/

MOD_decode
{
    char filename[PATH_MAX+1];
    MagickBooleanType status;

    if (param->flag == TC_AUDIO) {
        return TC_IMPORT_OK;
    }

    if (param->flag == TC_VIDEO) {
        // read a filename from the list
        if (fgets(filename, PATH_MAX, fd) == NULL) {
            return TC_IMPORT_ERROR;
        }
        filename[PATH_MAX] = '\0'; /* enforce */
        tc_strstrip(filename);

        ClearMagickWand(wand);
        /* 
         * This avoids IM to buffer all read images.
         * I'm quite sure that this can be done in a smarter way,
         * but I haven't yet figured out how. -- FRomani
         */

        status = MagickReadImage(wand, filename);
        if (status == MagickFalse) {
            return TCHandleMagickError(wand);
        }

        MagickSetLastIterator(wand);

        status = MagickGetImagePixels(wand,
                                      0, 0, width, height,
                                      "RGB", CharPixel,
                                      param->buffer);
        if (status == MagickFalse) {
            return TCHandleMagickError(wand);
        }

        param->attributes |= TC_FRAME_IS_KEYFRAME;

        return TC_IMPORT_OK;
    }
    return TC_IMPORT_ERROR;
}

/* ------------------------------------------------------------
 *
 * close stream
 *
 * ------------------------------------------------------------*/

MOD_close
{
    if (param->flag == TC_AUDIO) {
        return TC_IMPORT_OK;
    }

    if (param->flag == TC_VIDEO) {
        if (fd != NULL) {
            fclose(fd);
            fd = NULL;
        }

        if (wand != NULL) {
            DestroyMagickWand(wand);
            MagickWandTerminus();
            wand = NULL;
        }

        return TC_IMPORT_OK;
    }

    return TC_IMPORT_ERROR;
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
