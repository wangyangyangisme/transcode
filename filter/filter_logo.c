/*
 *  filter_logo.c
 *
 *  Copyright (C) Tilmann Bitterberg - April 2002
 *  filter updates, enhancements and cleanup:
 *  Copyright (C) Sebastian Kun <seb at sarolta dot com> - March 2006
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

    /* TODO:
        - animated gif/png support -> done
        - sequences of jpgs maybe
          would be nice.
     */

#define MOD_NAME    "filter_logo.so"
#define MOD_VERSION "v0.10 (2003-10-16)"
#define MOD_CAP     "render image in videostream"
#define MOD_AUTHOR  "Tilmann Bitterberg"

/* Note: because of ImageMagick bogosity, this must be included first, so
 * we can undefine the PACKAGE_* symbols it splats into our namespace */
#include <magick/api.h>
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

/* Add workaround for deprecated ScaleCharToQuantum() function */
#undef ScaleCharToQuantum
#if MAGICKCORE_QUANTUM_DEPTH == 8
# define ScaleCharToQuantum(x) ((x))
#elif MAGICKCORE_QUANTUM_DEPTH == 16
# define ScaleCharToQuantum(x) ((x)*257)
#elif MAGICKCORE_QUANTUM_DEPTH == 32
# define ScaleCharToQuantum(x) ((x)*16843009)
#elif MAGICKCORE_QUANTUM_DEPTH == 64
# define ScaleCharToQuantum(x) ((x)*71777214294589695ULL)
#endif

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"
#include "libtcvideo/tcvideo.h"

#include <stdlib.h>
#include <stdio.h>

#define MAX_UINT8_VAL   ((uint8_t)(-1))

// basic parameter

enum POS { NONE, TOP_LEFT, TOP_RIGHT, BOT_LEFT, BOT_RIGHT, CENTER };

typedef struct MyFilterData {
    /* public */
    char         file[PATH_MAX]; /* input filename                  */
    int          posx;           /* X offset in video               */
    int          posy;           /* Y offset in video               */
    enum POS     pos;            /* predifined position             */
    int          flip;           /* bool if to flip image           */
    int          ignoredelay;    /* allow the user to ignore delays */
    int          rgbswap;        /* bool if swap colors             */
    int          grayout;        /* only render lume values         */
    int          hqconv;         /* do high quality rgb->yuv conv.  */
    unsigned int start, end;     /* ranges                          */
    unsigned int fadein;         /* No. of frames to fade in        */
    unsigned int fadeout;        /* No. of frames to fade out       */

    /* private */
    unsigned int nr_of_images;   /* animated: number of images      */
    unsigned int cur_seq;        /* animated: current image         */
    int          cur_delay;      /* animated: current delay         */
    uint8_t    **yuv;            /* buffer for RGB->YUV conversion  */

    TCVHandle    tcvhandle;      /* handle for RGB->YUV conversion  */

    /* These used to be static (per-module), but are now per-instance. */
    vob_t       *vob;            /* video info from transcode       */
    Image       *image;          /* Magick image handle             */
    Image       *images;         /* tmp Magick handle (todo:remove) */
} MyFilterData;

/* FIXME: this uses the filter ID as an index--the ID can grow
 * arbitrarily large, so this needs to be fixed */
static MyFilterData *mfd_all[100] = {NULL};

/* Only one instance of the module needs to initialize ImageMagick */
static int magick_usecount = 0;

/* Coefficients used for transparency calculations. Pre-generating these
 * in a lookup table provides a small speed boost.
 */
static float img_coeff_lookup[MAX_UINT8_VAL + 1] = {-1.0};
static float vid_coeff_lookup[MAX_UINT8_VAL + 1] = {-1.0};

/* from /src/transcode.c */
extern int rgbswap;
extern int flip;
/* should probably honor the other flags too */

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static void flogo_help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"    This filter renders an user specified image into the video.\n"
"    Any image format ImageMagick can read is accepted.\n"
"    Transparent images are also supported.\n"
"    Image origin is at the very top left.\n"
"\n"
"* Options\n"
"        'file' Image filename (required) [logo.png]\n"
"         'pos' Position (0-width x 0-height) [0x0]\n"
"      'posdef' Position (0=None, 1=TopL, 2=TopR, 3=BotL, 4=BotR, 5=Center) [0]\n"
"       'range' Restrict rendering to framerange (0-oo) [0-end]\n"
"        'fade' Fade image in/out (# of frames) (0-oo) [0-0]\n"
"        'flip' Mirror image (0=off, 1=on) [0]\n"
"     'rgbswap' Swap colors [0]\n"
"     'grayout' YUV only: don't write Cb and Cr, makes a nice effect [0]\n"
"      'hqconv' YUV only: do high quality rgb->yuv img conversion [0]\n"
" 'ignoredelay' Ignore delay specified in animations [0]\n"
		, MOD_CAP);
}


/**
 * flogo_yuvbuf_free: Frees a set of YUV frame buffers allocated with
 *                    flogo_yuvbuf_alloc().
 * Parameters:     yuv: a pointer to a set of YUV frames
 *                 num: the number of frames to free
 * Return value:   N/A
 * Preconditions:  yuv was allocated with flogo_yuvbuf_alloc
 *                 num > 0
 * Postconditions: N/A
 */
static void flogo_yuvbuf_free(uint8_t **yuv, int num) {
    int i;

    if (yuv) {
        for (i = 0; i < num; i++) {
            if (yuv[i] != NULL)
                tc_free(yuv[i]);
        }
        tc_free(yuv);
    }

    return;
}


/**
 * flogo_yuvbuf_alloc: Allocates a set of zeroed YUV frame buffers.
 *
 * Parameters:     size: the size of each frame
 *                 num:  the number of frames to allocate.
 * Return value:   An array of pointers to zeroed YUV buffers.
 * Preconditions:  size > 0
 *                 num > 0
 * Postconditions: The returned pointer should be freed with
 *                 flogo_yuvbuf_free.
 */
static uint8_t **flogo_yuvbuf_alloc(size_t size, int num) {
    uint8_t **yuv;
    int i;

    yuv = tc_zalloc(sizeof(uint8_t *) * num);
    if (yuv == NULL)
        return NULL;

    for (i = 0; i < num; i++) {
        yuv[i] = tc_malloc(sizeof(uint8_t) * size);
        if (yuv[i] == NULL) {
            // free what's already been allocated
            flogo_yuvbuf_free(yuv, i-1);
            return NULL;
        }
    }

    return yuv;
}


/**
 * flogo_convert_image: Converts a single ImageMagick RGB image into a format
 *                      usable by transcode.
 *
 * Parameters:     tcvhandle:  Opaque libtcvideo handle
 *                 src:        An ImageMagick handle (the source image)
 *                 dst:        A pointer to the output buffer
 *                 ifmt:       The output format (see aclib/imgconvert.h)
 *                 do_rgbswap: zero for no swap, nonzero to swap red and blue
 *                             pixel positions
 * Return value:   1 on success, 0 on failure
 * Preconditions:  tcvhandle != null, was returned by a call to tcv_init()
 *                 src is a valid ImageMagick RGB image handle
 *                 dst buffer is large enough to hold the result of the
 *                   requested conversion
 * Postconditions: dst get overwritten with the result of the conversion
 */
static int flogo_convert_image(TCVHandle    tcvhandle,
                               Image       *src,
                               uint8_t     *dst,
                               ImageFormat  ifmt,
                               int          do_rgbswap)
{
    PixelPacket *pixel_packet;
    uint8_t *dst_ptr = dst;

    int row, col;
    int height = src->rows;
    int width  = src->columns;
    int ret;

    unsigned long r_off, g_off, b_off;

    if (!do_rgbswap) {
        r_off = 0;
        b_off = 2;
    } else {
        r_off = 2;
        b_off = 0;
    }
    g_off = 1;

    pixel_packet = GetImagePixels(src, 0, 0, width, height);

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            *(dst_ptr + r_off) = (uint8_t)ScaleQuantumToChar(pixel_packet->red);
            *(dst_ptr + g_off) = (uint8_t)ScaleQuantumToChar(pixel_packet->green);
            *(dst_ptr + b_off) = (uint8_t)ScaleQuantumToChar(pixel_packet->blue);

            dst_ptr += 3;
            pixel_packet++;
        }
    }

    ret = tcv_convert(tcvhandle, dst, dst, width, height, IMG_RGB24, ifmt);
    if (ret == 0) {
        tc_log_error(MOD_NAME, "RGB->YUV conversion failed");
        return 0;
    }

    return 1;
}


int tc_filter(frame_list_t *ptr_, char *options)
{
    vframe_list_t *ptr = (vframe_list_t *)ptr_;
    vob_t         *vob = NULL;

    int instance = ptr->filter_id;
    MyFilterData  *mfd = mfd_all[instance];

    if (mfd != NULL) {
        vob = mfd->vob;
    }

    //----------------------------------
    //
    // filter init
    //
    //----------------------------------


    if (ptr->tag & TC_FILTER_GET_CONFIG) {
        optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYO", "1");
        // buf, name, comment, format, val, from, to
        optstr_param(options, "file",   "Image filename",    "%s",    "logo.png");
        optstr_param(options, "posdef", "Position (0=None, 1=TopL, 2=TopR, 3=BotL, 4=BotR, 5=Center)",  "%d", "0", "0", "5");
        optstr_param(options, "pos",    "Position (0-width x 0-height)",  "%dx%d", "0x0", "0", "width", "0", "height");
        optstr_param(options, "range",  "Restrict rendering to framerange",  "%u-%u", "0-0", "0", "oo", "0", "oo");
        optstr_param(options, "fade",   "Fade image in/out (# of frames)",  "%u-%u", "0-0", "0", "oo", "0", "oo");
        // bools
        optstr_param(options, "ignoredelay", "Ignore delay specified in animations", "", "0");
        optstr_param(options, "rgbswap", "Swap red/blue colors", "", "0");
        optstr_param(options, "grayout", "YUV only: don't write Cb and Cr, makes a nice effect", "",  "0");
        optstr_param(options, "hqconv",  "YUV only: do high quality rgb->yuv img conversion", "",  "0");
        optstr_param(options, "flip",    "Mirror image",  "", "0");

        return 0;
    }

    if (ptr->tag & TC_FILTER_INIT) {
        Image         *timg;
        Image         *nimg;
        ImageInfo     *image_info;
        ExceptionInfo  exception_info;

        int rgb_off = 0;

        vob_t *tmpvob;

        tmpvob = tc_get_vob();
        if (tmpvob == NULL)
            return -1;
        mfd_all[instance] = tc_zalloc(sizeof(MyFilterData));
        if (mfd_all[instance] == NULL)
            return -1;

        mfd = mfd_all[instance];

        strlcpy(mfd->file, "logo.png", PATH_MAX);
        mfd->end = (unsigned int)-1;
        mfd->vob = tmpvob;
        vob      = mfd->vob;

        if (options != NULL) {
            if (verbose)
                tc_log_info(MOD_NAME, "options=%s", options);

            optstr_get(options, "file",     "%[^:]", mfd->file);
            optstr_get(options, "posdef",   "%d",    (int *)&mfd->pos);
            optstr_get(options, "pos",      "%dx%d", &mfd->posx,  &mfd->posy);
            optstr_get(options, "range",    "%u-%u", &mfd->start, &mfd->end);
            optstr_get(options, "fade",     "%u-%u", &mfd->fadein, &mfd->fadeout);

            if (optstr_lookup(options, "ignoredelay") != NULL)
                mfd->ignoredelay = !mfd->ignoredelay;
            if (optstr_lookup(options, "flip") != NULL)
                mfd->flip    = !mfd->flip;
            if (optstr_lookup(options, "rgbswap") != NULL)
                mfd->rgbswap = !mfd->rgbswap;
            if (optstr_lookup(options, "grayout") != NULL)
                mfd->grayout = !mfd->grayout;
            if (optstr_lookup(options, "hqconv") != NULL)
                mfd->hqconv  = !mfd->hqconv;

            if (optstr_lookup (options, "help") != NULL)
                flogo_help_optstr();
        }

        if (verbose > 1) {
            tc_log_info(MOD_NAME, " Logo renderer Settings:");
            tc_log_info(MOD_NAME, "         file = %s",    mfd->file);
            tc_log_info(MOD_NAME, "       posdef = %d",    mfd->pos);
            tc_log_info(MOD_NAME, "          pos = %dx%d", mfd->posx,
                                                           mfd->posy);
            tc_log_info(MOD_NAME, "        range = %u-%u", mfd->start,
                                                           mfd->end);
            tc_log_info(MOD_NAME, "         fade = %u-%u", mfd->fadein,
                                                           mfd->fadeout);
            tc_log_info(MOD_NAME, "         flip = %d",    mfd->flip);
            tc_log_info(MOD_NAME, "  ignoredelay = %d",    mfd->ignoredelay);
            tc_log_info(MOD_NAME, "      rgbswap = %d",    mfd->rgbswap);
            tc_log_info(MOD_NAME, "      grayout = %d",    mfd->grayout);
            tc_log_info(MOD_NAME, "       hqconv = %d",    mfd->hqconv);
        }

        /* Transcode serializes module execution, so this does not need a
         * semaphore.
         */
        magick_usecount++;
        if (!IsMagickInstantiated()) {
            InitializeMagick("");
        }

        GetExceptionInfo(&exception_info);
        image_info = CloneImageInfo((ImageInfo *) NULL);
        strlcpy(image_info->filename, mfd->file, MaxTextExtent);

        mfd->image = ReadImage(image_info, &exception_info);
        if (mfd->image == (Image *) NULL) {
            MagickWarning(exception_info.severity,
                          exception_info.reason,
                          exception_info.description);
            strlcpy(mfd->file, "/dev/null", PATH_MAX);
            return 0;
        }
        DestroyImageInfo(image_info);

        if (mfd->image->columns > vob->ex_v_width
         || mfd->image->rows    > vob->ex_v_height
        ) {
            tc_log_error(MOD_NAME, "\"%s\" is too large", mfd->file);
            return -1;
        }

        if (vob->im_v_codec == CODEC_YUV) {
            if ((mfd->image->columns & 1) || (mfd->image->rows & 1)) {
                tc_log_error(MOD_NAME, "\"%s\" has odd sizes", mfd->file);
                return -1;
            }
        }

        mfd->images = (Image *)GetFirstImageInList(mfd->image);
        nimg = NewImageList();

        while (mfd->images != (Image *)NULL) {
            if (mfd->flip || flip) {
                timg = FlipImage(mfd->images, &exception_info);
                if (timg == (Image *) NULL) {
                    MagickError(exception_info.severity,
                                exception_info.reason,
                                exception_info.description);
                    return -1;
                }
                AppendImageToList(&nimg, timg);
            }

            mfd->images = GetNextImageInList(mfd->images);
            mfd->nr_of_images++;
        }

        // check for memleaks;
        //DestroyImageList(image);
        if (mfd->flip || flip) {
            mfd->image = nimg;
        }

        /* initial delay. real delay = 1/100 sec * delay */
        mfd->cur_delay = mfd->image->delay*vob->fps/100;

        if (verbose & TC_DEBUG)
            tc_log_info(MOD_NAME, "Nr: %d Delay: %d mfd->image->del %lu|",
                        mfd->nr_of_images, mfd->cur_delay, mfd->image->delay);

        if (vob->im_v_codec == CODEC_YUV) {
            /* convert Magick RGB image format to YUV */
            /* todo: convert the magick image if it's not rgb! (e.g. cmyk) */
            Image   *image;
            uint8_t *yuv_hqbuf = NULL;

            /* Round up for odd-size images */
            unsigned long width  = mfd->image->columns;
            unsigned long height = mfd->image->rows;
            int do_rgbswap  = (rgbswap || mfd->rgbswap);
            int i;

            /* Allocate buffers for the YUV420P frames. mfd->nr_of_images
             * will be 1 unless this is an animated GIF or MNG.
             * This buffer needs to be large enough to store a temporary
             * 24-bit RGB image (extracted from the ImageMagick handle).
             */
            mfd->yuv = flogo_yuvbuf_alloc(width*height * 3, mfd->nr_of_images);
            if (mfd->yuv == NULL) {
                tc_log_error(MOD_NAME, "(%d) out of memory\n", __LINE__);
                return -1;
            }

            if (mfd->hqconv) {
                /* One temporary buffer, to hold full Y, U, and V planes. */
                yuv_hqbuf = tc_malloc(width*height * 3);
                if (yuv_hqbuf == NULL) {
                    tc_log_error(MOD_NAME, "(%d) out of memory\n", __LINE__);
                    return -1;
                }
            }

            mfd->tcvhandle = tcv_init();
            if (mfd->tcvhandle == NULL) {
                tc_log_error(MOD_NAME, "image conversion init failed");
                return -1;
            }

            image = GetFirstImageInList(mfd->image);

            for (i = 0; i < mfd->nr_of_images; i++) {
                if (!mfd->hqconv) {
                    flogo_convert_image(mfd->tcvhandle, image, mfd->yuv[i],
                                        IMG_YUV420P, do_rgbswap);
                } else {
                    flogo_convert_image(mfd->tcvhandle, image, yuv_hqbuf,
                                        IMG_YUV444P, do_rgbswap);

                    // Copy over Y data from the 444 image
                    ac_memcpy(mfd->yuv[i], yuv_hqbuf, width * height);

                    // Resize U plane by 1/2 in each dimension, into the
                    // mfd YUV buffer
                    tcv_zoom(mfd->tcvhandle,
                             yuv_hqbuf + (width * height),
                             mfd->yuv[i] + (width * height),
                             width,
                             height,
                             1,
                             width / 2,
                             height / 2,
                             TCV_ZOOM_LANCZOS3
                    );

                    // Do the same with the V plane
                    tcv_zoom(mfd->tcvhandle,
                             yuv_hqbuf + 2*width*height,
                             mfd->yuv[i] + width*height + (width/2)*(height/2),
                             width,
                             height,
                             1,
                             width / 2,
                             height / 2,
                             TCV_ZOOM_LANCZOS3
                    );
                }
                image = GetNextImageInList(image);
            }

            if (mfd->hqconv)
                tc_free(yuv_hqbuf);

            tcv_free(mfd->tcvhandle);
        } else {
            /* for RGB format is origin bottom left */
            /* for RGB, rgbswap is done in the frame routine */
            rgb_off = vob->ex_v_height - mfd->image->rows;
            mfd->posy = rgb_off - mfd->posy;
        }

        switch (mfd->pos) {
          case NONE: /* 0 */
            break;
          case TOP_LEFT:
            mfd->posx = 0;
            mfd->posy = rgb_off;
            break;
          case TOP_RIGHT:
            mfd->posx = vob->ex_v_width  - mfd->image->columns;
            break;
          case BOT_LEFT:
            mfd->posy = vob->ex_v_height - mfd->image->rows - rgb_off;
            break;
          case BOT_RIGHT:
            mfd->posx = vob->ex_v_width  - mfd->image->columns;
            mfd->posy = vob->ex_v_height - mfd->image->rows - rgb_off;
            break;
          case CENTER:
            mfd->posx = (vob->ex_v_width - mfd->image->columns)/2;
            mfd->posy = (vob->ex_v_height- mfd->image->rows)/2;
            /* align to not cause color disruption */
            if (mfd->posx & 1)
                mfd->posx++;
            if (mfd->posy & 1)
                mfd->posy++;
            break;
        }


        if (mfd->posy < 0 || mfd->posx < 0
         || (mfd->posx + mfd->image->columns) > vob->ex_v_width
         || (mfd->posy + mfd->image->rows)    > vob->ex_v_height) {
            tc_log_error(MOD_NAME, "invalid position");
            return -1;
        }

        /* for running through image sequence */
        mfd->images = mfd->image;


        /* Set up image/video coefficient lookup tables */
        if (img_coeff_lookup[0] < 0) {
            int i;
            float maxrgbval = (float)MaxRGB; // from ImageMagick

            for (i = 0; i <= MAX_UINT8_VAL; i++) {
                float x = (float)ScaleCharToQuantum(i);

                /* Alternatively:
                 *  img_coeff = (maxrgbval - x) / maxrgbval;
                 *  vid_coeff = x / maxrgbval;
                 */
                img_coeff_lookup[i] = 1.0 - (x / maxrgbval);
                vid_coeff_lookup[i] = 1.0 - img_coeff_lookup[i];
            }
        }

        // filter init ok.
        if (verbose)
            tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

        return 0;
    }


    //----------------------------------
    //
    // filter close
    //
    //----------------------------------
    if (ptr->tag & TC_FILTER_CLOSE) {
        if (mfd) {
            flogo_yuvbuf_free(mfd->yuv, mfd->nr_of_images);
            mfd->yuv = NULL;

            if (mfd->image) {
                DestroyImage(mfd->image);
            }

            tc_free(mfd);
            mfd = NULL;
            mfd_all[instance] = NULL;
        }

        magick_usecount--;
        if (magick_usecount == 0 && IsMagickInstantiated()) {
            DestroyMagick();
        }

        return 0;
    } /* filter close */


    //----------------------------------
    //
    // filter frame routine
    //
    //----------------------------------


    // tag variable indicates, if we are called before
    // transcodes internal video/audo frame processing routines
    // or after and determines video/audio context

    if ((ptr->tag & TC_POST_M_PROCESS)
        && (ptr->tag & TC_VIDEO)
        && !(ptr->attributes & TC_FRAME_IS_SKIPPED)
    ) {
        PixelPacket *pixel_packet;
        uint8_t     *video_buf;

        int   do_fade    = 0;
        float fade_coeff = 0.0;
        float img_coeff, vid_coeff;

        /* Note: ImageMagick defines opacity = 0 as fully visible, and
         * opacity = MaxRGB as fully transparent.
         */
        Quantum opacity;

        int row, col;

        if (ptr->id < mfd->start || ptr->id > mfd->end)
            return 0;

        if (strcmp(mfd->file, "/dev/null") == 0)
            return 0;

        if (ptr->id - mfd->start < mfd->fadein) {
            // fading-in
            fade_coeff = (float)(mfd->start - ptr->id + mfd->fadein) / (float)(mfd->fadein);
            do_fade = 1;
        } else if (mfd->end - ptr->id < mfd->fadeout) {
            // fading-out
            fade_coeff = (float)(ptr->id - mfd->end + mfd->fadeout) / (float)(mfd->fadeout);
            do_fade = 1;
        }

        mfd->cur_delay--;

        if (mfd->cur_delay < 0 || mfd->ignoredelay) {
            int seq;

            mfd->cur_seq = (mfd->cur_seq + 1) % mfd->nr_of_images;

            mfd->images = mfd->image;
            for (seq=0; seq<mfd->cur_seq; seq++)
                mfd->images = mfd->images->next;

            mfd->cur_delay = mfd->images->delay * vob->fps/100;
        }

        pixel_packet = GetImagePixels(mfd->images, 0, 0,
                                      mfd->images->columns,
                                      mfd->images->rows);

        if (vob->im_v_codec == CODEC_RGB) {
            unsigned long r_off, g_off, b_off;

            if (!(rgbswap || mfd->rgbswap)) {
                r_off = 0;
                b_off = 2;
            } else {
                r_off = 2;
                b_off = 0;
            }
            g_off = 1;

            for (row = 0; row < mfd->image->rows; row++) {
                video_buf = ptr->video_buf + 3 * ((row + mfd->posy) * vob->ex_v_width + mfd->posx);

                for (col = 0; col < mfd->image->columns; col++) {
                    opacity = pixel_packet->opacity;

                    if (do_fade)
                        opacity += (Quantum)((MaxRGB - opacity) * fade_coeff);

                    if (opacity == 0) {
                        *(video_buf + r_off) = ScaleQuantumToChar(pixel_packet->red);
                        *(video_buf + g_off) = ScaleQuantumToChar(pixel_packet->green);
                        *(video_buf + b_off) = ScaleQuantumToChar(pixel_packet->blue);
                    } else if (opacity < MaxRGB) {
                        unsigned char opacity_uchar = ScaleQuantumToChar(opacity);
                        img_coeff = img_coeff_lookup[opacity_uchar];
                        vid_coeff = vid_coeff_lookup[opacity_uchar];

                        *(video_buf + r_off) = (uint8_t)((*(video_buf + r_off)) * vid_coeff)
                                                + (uint8_t)(ScaleQuantumToChar(pixel_packet->red)   * img_coeff);
                        *(video_buf + g_off) = (uint8_t)((*(video_buf + g_off)) * vid_coeff)
                                                + (uint8_t)(ScaleQuantumToChar(pixel_packet->green) * img_coeff);
                        *(video_buf + b_off) = (uint8_t)((*(video_buf + b_off)) * vid_coeff)
                                                + (uint8_t)(ScaleQuantumToChar(pixel_packet->blue)  * img_coeff);
                    }

                    video_buf += 3;
                    pixel_packet++;
                }
            }
        } else { /* !RGB */
            unsigned long vid_size = vob->ex_v_width * vob->ex_v_height;
            unsigned long img_size = mfd->images->columns * mfd->images->rows;

            uint8_t *img_pixel_Y, *img_pixel_U, *img_pixel_V;
            uint8_t *vid_pixel_Y, *vid_pixel_U, *vid_pixel_V;

            img_pixel_Y = mfd->yuv[mfd->cur_seq];
            img_pixel_U = img_pixel_Y + img_size;
            img_pixel_V = img_pixel_U + img_size/4;

            for (row = 0; row < mfd->images->rows; row++) {
                vid_pixel_Y = ptr->video_buf + (row + mfd->posy)*mfd->vob->ex_v_width + mfd->posx;
                vid_pixel_U = ptr->video_buf + vid_size + (row/2 + mfd->posy/2)*(mfd->vob->ex_v_width/2) + mfd->posx/2;
                vid_pixel_V = vid_pixel_U + vid_size/4;
                for (col = 0; col < mfd->images->columns; col++) {
                    int do_UV_pixels = (mfd->grayout == 0 && !(row % 2) && !(col % 2)) ? 1 : 0;
                    opacity = pixel_packet->opacity;

                    if (do_fade)
                        opacity += (Quantum)((MaxRGB - opacity) * fade_coeff);

                    if (opacity == 0) {
                        *vid_pixel_Y = *img_pixel_Y;
                        if (do_UV_pixels) {
                                *vid_pixel_U = *img_pixel_U;
                                *vid_pixel_V = *img_pixel_V;
                        }
                    } else if (opacity < MaxRGB) {
                        unsigned char opacity_uchar = ScaleQuantumToChar(opacity);
                        img_coeff = img_coeff_lookup[opacity_uchar];
                        vid_coeff = vid_coeff_lookup[opacity_uchar];

                        *vid_pixel_Y = (uint8_t)(*vid_pixel_Y * vid_coeff) + (uint8_t)(*img_pixel_Y * img_coeff);

                        if (do_UV_pixels) {
                            *vid_pixel_U = (uint8_t)(*vid_pixel_U * vid_coeff) + (uint8_t)(*img_pixel_U * img_coeff);
                            *vid_pixel_V = (uint8_t)(*vid_pixel_V * vid_coeff) + (uint8_t)(*img_pixel_V * img_coeff);
                        }
                    }

                    vid_pixel_Y++;
                    img_pixel_Y++;
                    if (do_UV_pixels) {
                        vid_pixel_U++;
                        img_pixel_U++;
                        vid_pixel_V++;
                        img_pixel_V++;
                    }
                    pixel_packet++;
                }
            }
        }
    }

    return 0;
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
