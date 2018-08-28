/*
   filter_msharpen.c

   Copyright (C) 1999-2000 Donal A. Graft
     modified 2003 by William Hawkins for use with transcode

    MSharpen Filter for VirtualDub -- performs sharpening
	limited to edge areas of the frame.
	Copyright (C) 1999-2000 Donald A. Graft

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	The author can be contacted at:
	Donald Graft
	neuron2@home.com.
*/

#define MOD_NAME    "filter_msharpen.so"
#define MOD_VERSION "(1.0) (2003-07-17)"
#define MOD_CAP     "VirtualDub's MSharpen Filter"
#define MOD_AUTHOR  "Donald Graft, William Hawkins"

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"

#include "libtcvideo/tcvideo.h"

static vob_t *vob=NULL;

///////////////////////////////////////////////////////////////////////////

typedef struct MyFilterData {
        uint8_t                 *convertFrameIn;
        uint8_t                 *convertFrameOut;
	unsigned char		*blur;
	unsigned char		*work;
	int 		       	strength;
	int	      		threshold;
	int   			mask;
        int                     highq;
	TCVHandle		tcvhandle;
} MyFilterData;

static MyFilterData *mfd;

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"    This plugin implements an unusual concept in spatial sharpening.\n"
"    Although designed specifically for anime, it also works well with\n"
"    normal video. The filter is very effective at sharpening important\n"
"    edges without amplifying noise.\n"
"\n"
"* Options\n"
"  * Strength 'strength' (0-255) [100]\n"
"    This is the strength of the sharpening to be applied to the edge\n"
"    detail areas. It is applied only to the edge detail areas as\n"
"    determined by the 'threshold' parameter. Strength 255 is the\n"
"    strongest sharpening.\n"
"\n"
"  * Threshold 'threshold' (0-255) [10]\n"
"    This parameter determines what is detected as edge detail and\n"
"    thus sharpened. To see what edge detail areas will be sharpened,\n"
"    use the 'mask' parameter.\n"
"\n"
"  * Mask 'mask' (0-1) [0]\n"
"    When set to true, the areas to be sharpened are shown in white\n"
"    against a black background. Use this to set the level of detail to\n"
"    be sharpened. This function also makes a basic edge detection filter.\n"
"\n"
"  * HighQ 'highq' (0-1) [1]\n"
"    This parameter lets you tradeoff speed for quality of detail\n"
"    detection. Set it to true for the best detail detection. Set it to\n"
"    false for maximum speed.\n"
		, MOD_CAP);
}

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

	int width, height;

	if((vob = tc_get_vob())==NULL) return(-1);

	mfd = tc_malloc(sizeof(MyFilterData));

	if (!mfd) {
		tc_log_error(MOD_NAME, "No memory at %d!\n", __LINE__);
		return (-1);
	}

	height = vob->ex_v_height;
	width  = vob->ex_v_width;

	/* default values */
	mfd->strength       = 100; /* A little bird told me this was a good value */
	mfd->threshold      = 10;
	mfd->mask           = TC_FALSE; /* not sure what this does at the moment */
	mfd->highq          = TC_TRUE; /* high Q or not? */

	if (options != NULL) {

	  if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

	  optstr_get (options, "strength",  "%d", &mfd->strength);
	  optstr_get (options, "threshold", "%d", &mfd->threshold);
	  optstr_get (options, "highq", "%d", &mfd->highq);
	  optstr_get (options, "mask", "%d", &mfd->mask);

	}

	if (verbose > 1) {

	  tc_log_info (MOD_NAME, " MSharpen Filter Settings (%dx%d):", width,height);
	  tc_log_info (MOD_NAME, "          strength = %d", mfd->strength);
	  tc_log_info (MOD_NAME, "         threshold = %d", mfd->threshold);
	  tc_log_info (MOD_NAME, "             highq = %d", mfd->highq);
	  tc_log_info (MOD_NAME, "              mask = %d", mfd->mask);
	}

	if (options)
		if ( optstr_lookup(options, "help") != NULL) {
			help_optstr();
		}

	/* fetch memory */

	mfd->blur = tc_malloc(4 * width * height);
	if (!mfd->blur){
                tc_log_error(MOD_NAME, "No memory at %d!\n", __LINE__);
		return (-1);
	}
	mfd->work = tc_malloc(4 * width * height);
	if (!mfd->work){
                tc_log_error(MOD_NAME, "No memory at %d!\n", __LINE__);
		return (-1);
	}
	mfd->convertFrameIn = tc_zalloc (width*height*4);
	if (!mfd->convertFrameIn) {
		tc_log_error(MOD_NAME, "No memory at %d!\n", __LINE__);
		return (-1);
	}

	mfd->convertFrameOut = tc_zalloc (width*height*4);
	if (!mfd->convertFrameOut) {
		tc_log_error(MOD_NAME, "No memory at %d!\n", __LINE__);
		return (-1);
	}

	mfd->tcvhandle = tcv_init();

	// filter init ok.
	if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

	return 0;

  } /* TC_FILTER_INIT */

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
    if (options) {
	    char buf[256];
	    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYO", "1");
	    tc_snprintf (buf, sizeof(buf), "%d", mfd->strength);
	    optstr_param (options, "strength", "How much  of the effect", "%d", buf, "0", "255");

	    tc_snprintf (buf, sizeof(buf), "%d", mfd->threshold);
	    optstr_param (options, "threshold",
			  "How close a pixel must be to the brightest or dimmest pixel to be mapped",
			  "%d", buf, "0", "255");
	    tc_snprintf (buf, sizeof(buf), "%d", mfd->highq);
	    optstr_param (options, "highq",  "Tradeoff speed for quality of detail detection",
		          "%d", buf, "0", "1");
	    tc_snprintf (buf, sizeof(buf), "%d", mfd->mask);
	    optstr_param (options, "mask",  "Areas to be sharpened are shown in white",
		          "%d", buf, "0", "1");

    }
  }


  if(ptr->tag & TC_FILTER_CLOSE) {

	if (mfd->convertFrameIn)
		free (mfd->convertFrameIn);
	mfd->convertFrameIn = NULL;

	if (mfd->convertFrameOut)
		free (mfd->convertFrameOut);
	mfd->convertFrameOut = NULL;

	if (mfd->blur)
                free ( mfd->blur);
	mfd->blur = NULL;
	if (mfd->work)
                free(mfd->work);
	mfd->work = NULL;

	tcv_free(mfd->tcvhandle);
	mfd->tcvhandle = 0;

	if (mfd)
		free(mfd);
	mfd = NULL;

	return 0;

  } /* TC_FILTER_CLOSE */

///////////////////////////////////////////////////////////////////////////
  if(ptr->tag & TC_POST_M_PROCESS && ptr->tag & TC_VIDEO) {


	const int	width  = ptr->v_width;
	const int	height = ptr->v_height;
	const long	pitch = ptr->v_width*4;
	int		bwidth = 4 * width;
	uint8_t         *src;
	uint8_t         *dst;
	uint8_t         *srcpp, *srcp, *srcpn, *workp, *blurp, *blurpn, *dstp;
	int r1, r2, r3, r4, g1, g2, g3, g4, b1, b2, b3, b4;
	int x, y, max;
	int strength = mfd->strength, invstrength = 255 - strength;
	int threshold = mfd->threshold;
	// const int	srcpitch = ptr->v_width*4;
	const int	dstpitch = ptr->v_width*4;

	tcv_convert(mfd->tcvhandle, ptr->video_buf, mfd->convertFrameIn,
		    ptr->v_width, ptr->v_height,
		    vob->im_v_codec==CODEC_YUV ? IMG_YUV_DEFAULT : IMG_RGB24,
		    IMG_BGRA32);

	src = mfd->convertFrameIn;
	dst = mfd->convertFrameOut;

	/* Blur the source image prior to detail detection. Separate
	   dimensions for speed. */
	/* Vertical. */
	srcpp = src;
	srcp = srcpp + pitch;
	srcpn = srcp + pitch;
	workp = mfd->work + bwidth;
	for (y = 1; y < height - 1; y++)
	{
		for (x = 0; x < bwidth; x++)
		{
			workp[x] = (srcpp[x] + srcp[x] + srcpn[x]) / 3;
		}
		srcpp += pitch;
		srcp += pitch;
		srcpn += pitch;
		workp += bwidth;
	}

	/* Horizontal. */
	workp  = mfd->work;
	blurp  = mfd->blur;
	for (y = 0; y < height; y++)
	{
		for (x = 4; x < bwidth - 4; x++)
		{
			blurp[x] = (workp[x-4] + workp[x] + workp[x+4]) / 3;
		}
		workp += bwidth;
		blurp += bwidth;
	}

	/* Fix up blur frame borders. */
	srcp = src;
	blurp = mfd->blur;
	ac_memcpy(blurp, srcp, bwidth);
	ac_memcpy(blurp + (height-1)*bwidth, srcp + (height-1)*pitch, bwidth);
	for (y = 0; y < height; y++)
	{
		*((unsigned int *)(&blurp[0])) = *((unsigned int *)(&srcp[0]));
		*((unsigned int *)(&blurp[bwidth-4])) = *((unsigned int *)(&srcp[bwidth-4]));
		srcp += pitch;
		blurp += bwidth;
	}

	/* Diagonal detail detection. */
	blurp = mfd->blur;
	blurpn = blurp + bwidth;
	workp = mfd->work;
	for (y = 0; y < height - 1; y++)
	{
		b1 = blurp[0];
		g1 = blurp[1];
		r1 = blurp[2];
		b3 = blurpn[0];
		g3 = blurpn[1];
		r3 = blurpn[2];
		for (x = 0; x < bwidth - 4; x+=4)
		{
			b2 = blurp[x+4];
			g2 = blurp[x+5];
			r2 = blurp[x+6];
			b4 = blurpn[x+4];
			g4 = blurpn[x+5];
			r4 = blurpn[x+6];
			if ((abs(b1 - b4) >= threshold) || (abs(g1 - g4) >= threshold) || (abs(r1 - r4) >= threshold) ||
				(abs(b2 - b3) >= threshold) || (abs(g2 - g3) >= threshold) || (abs(g2 - g3) >= threshold))
			{
				*((unsigned int *)(&workp[x])) = 0xffffffff;
			}
			else
			{
				*((unsigned int *)(&workp[x])) = 0x0;
			}
			b1 = b2; b3 = b4;
			g1 = g2; g3 = g4;
			r1 = r2; r3 = r4;
		}
		workp += bwidth;
		blurp += bwidth;
		blurpn += bwidth;
	}

	if (mfd->highq == TC_TRUE)
//	if (1)
	{
		/* Vertical detail detection. */
		for (x = 0; x < bwidth; x+=4)
		{
 			blurp = mfd->blur;
			blurpn = blurp + bwidth;
			workp = mfd->work;
			b1 = blurp[x];
			g1 = blurp[x+1];
			r1 = blurp[x+2];
			for (y = 0; y < height - 1; y++)
			{
				b2 = blurpn[x];
				g2 = blurpn[x+1];
				r2 = blurpn[x+2];
				if (abs(b1 - b2) >= threshold || abs(g1 - g2) >= threshold || abs(r1 - r2) >= threshold)
				{
					*((unsigned int *)(&workp[x])) = 0xffffffff;
				}
				b1 = b2;
				g1 = g2;
				r1 = r2;
				workp += bwidth;
				blurp += bwidth;
				blurpn += bwidth;
			}
		}

		/* Horizontal detail detection. */
		blurp = mfd->blur;
		workp = mfd->work;
		for (y = 0; y < height; y++)
		{
			b1 = blurp[0];
			g1 = blurp[1];
			r1 = blurp[2];
			for (x = 0; x < bwidth - 4; x+=4)
			{
				b2 = blurp[x+4];
				g2 = blurp[x+5];
				r2 = blurp[x+6];
				if (abs(b1 - b2) >= threshold || abs(g1 - g2) >= threshold || abs(r1 - r2) >= threshold)
				{
					*((unsigned int *)(&workp[x])) = 0xffffffff;
				}
				b1 = b2;
				g1 = g2;
				r1 = r2;
			}
			workp += bwidth;
			blurp += bwidth;
		}
	}

	/* Fix up detail map borders. */
	memset(mfd->work + (height-1)*bwidth, 0, bwidth);
	workp = mfd->work;
	for (y = 0; y < height; y++)
	{
		*((unsigned int *)(&workp[bwidth-4])) = 0;
		workp += bwidth;
	}

	if (mfd->mask == TC_TRUE)
	{
		workp	= mfd->work;
		dstp	= dst;
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < bwidth; x++)
			{
				dstp[x] = workp[x];
			}
			workp += bwidth;
			dstp = dstp + dstpitch;
		}
		return 0;
	}

	/* Fix up output frame borders. */
	srcp = src;
	dstp = dst;
	ac_memcpy(dstp, srcp, bwidth);
	ac_memcpy(dstp + (height-1)*pitch, srcp + (height-1)*pitch, bwidth);
	for (y = 0; y < height; y++)
	{
		*((unsigned int *)(&dstp[0])) = *((unsigned int *)(&srcp[0]));
		*((unsigned int *)(&dstp[bwidth-4])) = *((unsigned int *)(&srcp[bwidth-4]));
		srcp += pitch;
		dstp += pitch;
	}

	/* Now sharpen the edge areas and we're done! */
 	srcp = src + pitch;
 	dstp = dst + pitch;
	workp = mfd->work + bwidth;
	blurp = mfd->blur + bwidth;
	for (y = 1; y < height - 1; y++)
	{
		for (x = 4; x < bwidth - 4; x+=4)
		{
			int xplus1 = x + 1, xplus2 = x + 2;

			if (workp[x])
			{
				b4 = (4*(int)srcp[x] - 3*blurp[x]);
				g4 = (4*(int)srcp[x+1] - 3*blurp[x+1]);
				r4 = (4*(int)srcp[x+2] - 3*blurp[x+2]);

				if (b4 < 0) b4 = 0;
				if (g4 < 0) g4 = 0;
				if (r4 < 0) r4 = 0;
				max = b4;
				if (g4 > max) max = g4;
				if (r4 > max) max = r4;
				if (max > 255)
				{
					b4 = (b4 * 255) / max;
					g4 = (g4 * 255) / max;
					r4 = (r4 * 255) / max;
				}
				dstp[x]      = (strength * b4 + invstrength * srcp[x])      >> 8;
				dstp[xplus1] = (strength * g4 + invstrength * srcp[xplus1]) >> 8;
				dstp[xplus2] = (strength * r4 + invstrength * srcp[xplus2]) >> 8;
			}
			else
			{
				dstp[x]   = srcp[x];
				dstp[xplus1] = srcp[xplus1];
				dstp[xplus2] = srcp[xplus2];
			}
		}
		srcp += pitch;
		dstp += pitch;
		workp += bwidth;
		blurp += bwidth;
	}

	tcv_convert(mfd->tcvhandle, mfd->convertFrameOut, ptr->video_buf,
		    ptr->v_width, ptr->v_height, IMG_BGRA32,
		    vob->im_v_codec==CODEC_YUV ? IMG_YUV_DEFAULT : IMG_RGB24);

	return 0;
  }
  return 0;
}

