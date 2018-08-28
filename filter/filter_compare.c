/*
 *  filter_compare
 *
 *  Copyright (C) Antonio Beamud Montero <antonio.beamud@linkend.com>
 *  Copyright (C) Microgenesis S.A.
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

/* Note: because of ImageMagick bogosity, this must be included first, so
 * we can undefine the PACKAGE_* symbols it splats into our namespace */
#include <magick/api.h>
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "transcode.h"
#include "filter.h"
#include "libtc/libtc.h"
#include "libtc/optstr.h"

#define MOD_NAME    "filter_compare.so"
#define MOD_VERSION "v0.1.2 (2003-08-29)"
#define MOD_CAP     "compare with other image to find a pattern"
#define MOD_AUTHOR  "Antonio Beamud"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#define DELTA_COLOR 45.0

// FIXME: Try to implement the YUV colorspace

typedef struct pixelsMask {
	unsigned int row;
	unsigned int col;
	unsigned char r,g,b;
	unsigned char delta_r,delta_g,delta_b;

	struct pixelsMask *next;

}pixelsMask;

typedef struct compareData {

	FILE *results;

	float delta;
	int step;

	pixelsMask *pixel_mask;

	vob_t *vob;

	unsigned int frames;

	int width, height;
	int size;

} compareData;

/* FIXME: this uses the filter ID as an index--the ID can grow
 * arbitrarily large, so this needs to be fixed */
static compareData *compare[100];
extern int rgbswap;

/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help"
"* Overview\n"
"    Generate a file in with information about the times, \n"
"    frame, etc the pattern defined in the image \n"
"    parameter is observed.\n"
"* Options\n"
"    'pattern' path to the file used like pattern\n"
"    'results' path to the file used to write the results\n"
"    'delta' delta error allowed\n"
		, MOD_CAP);
}


int tc_filter(frame_list_t *ptr_, char *options)
{
	vframe_list_t *ptr = (vframe_list_t *)ptr_;
	int instance = ptr->filter_id;
	Image *pattern, *resized, *orig = 0;
	ImageInfo *image_info;

	PixelPacket *pixel_packet;
	pixelsMask *pixel_last;
	ExceptionInfo exception_info;

	if(ptr->tag & TC_FILTER_GET_CONFIG) {
		char buf[128];
		optstr_filter_desc(options, MOD_NAME, MOD_CAP, MOD_VERSION,
				   MOD_AUTHOR, "VRYMO", "1");

		tc_snprintf(buf, 128, "/dev/null");
		optstr_param(options, "pattern", "Pattern image file path", "%s", buf);
		tc_snprintf(buf, 128, "results.dat");
		optstr_param(options, "results", "Results file path" , "%s", buf);
		tc_snprintf(buf, 128, "%f", compare[instance]->delta);
		optstr_param(options, "delta", "Delta error", "%f",buf,"0.0", "100.0");
		return 0;
	}

	//----------------------------------
	//
	// filter init
	//
	//----------------------------------


	if(ptr->tag & TC_FILTER_INIT)
	{

		unsigned int t,r,index;
		pixelsMask *temp;

		compare[instance] = tc_malloc(sizeof(compareData));
		if(compare[instance] == NULL)
			return (-1);

		compare[instance]->vob = tc_get_vob();
		if(compare[instance]->vob ==NULL)
            return(-1);

		compare[instance]->delta=DELTA_COLOR;
		compare[instance]->step=1;
		compare[instance]->width=0;
		compare[instance]->height=0;
		compare[instance]->frames = 0;
		compare[instance]->pixel_mask = NULL;
		pixel_last = NULL;

		compare[instance]->width = compare[instance]->vob->ex_v_width;
		compare[instance]->height = compare[instance]->vob->ex_v_height;

		if (options != NULL) {
			char pattern_name[PATH_MAX];
			char results_name[PATH_MAX];
			memset(pattern_name,0,PATH_MAX);
			memset(results_name,0,PATH_MAX);

			if(verbose) tc_log_info(MOD_NAME, "options=%s", options);

			optstr_get(options, "pattern", "%[^:]", pattern_name);
			optstr_get(options, "results", "%[^:]", results_name);
			optstr_get(options, "delta", "%f", &compare[instance]->delta);

			if (verbose > 1) {
				tc_log_info(MOD_NAME, "Compare Image Settings:");
				tc_log_info(MOD_NAME, "      pattern = %s\n", pattern_name);
				tc_log_info(MOD_NAME, "      results = %s\n", results_name);
				tc_log_info(MOD_NAME, "        delta = %f\n", compare[instance]->delta);
			}

			if (strlen(results_name) == 0) {
				// Ponemos el nombre del fichero al original con extension dat
				strlcpy(results_name, "/tmp/compare.dat", sizeof(results_name));

			}
			if (!(compare[instance]->results = fopen(results_name, "w")))
			{
				tc_log_perror(MOD_NAME, "could not open file for writing");
			}

			InitializeMagick("");
			if (verbose > 1)
                tc_log_info(MOD_NAME, "Magick Initialized successfully");

			GetExceptionInfo(&exception_info);
			image_info = CloneImageInfo ((ImageInfo *) NULL);
			strlcpy(image_info->filename, pattern_name, MaxTextExtent);
			if (verbose > 1)
			     tc_log_info(MOD_NAME, "Trying to open image");
			orig = ReadImage(image_info,
					 &exception_info);

			if (orig == (Image *) NULL) {
				MagickWarning(exception_info.severity,
					      exception_info.reason,
					      exception_info.description);
				strlcpy(pattern_name, "/dev/null", sizeof(pattern_name));
			}else{
			       if (verbose > 1)
			       		tc_log_info(MOD_NAME, "Image loaded successfully");
			     }
		}

		else{
			tc_log_perror(MOD_NAME, "Not image provided");
		}

		if (options != NULL)
			if (optstr_lookup (options, "help")) {
				help_optstr();
			}


		fprintf(compare[instance]->results,"#fps:%f\n",compare[instance]->vob->fps);

		if (orig != NULL){
                        // Flip and resize
			if (compare[instance]->vob->im_v_codec == CODEC_YUV)
				TransformRGBImage(orig,YCbCrColorspace);
			if (verbose > 1) tc_log_info(MOD_NAME, "Resizing the Image");
			resized = ResizeImage(orig,
					      compare[instance]->width,
					      compare[instance]->height,
					      GaussianFilter,
					      1,
					      &exception_info);
			if (verbose > 1)
				tc_log_info(MOD_NAME, "Flipping the Image");
			pattern = FlipImage(resized, &exception_info);
			if (pattern == (Image *) NULL) {
				MagickError (exception_info.severity,
					     exception_info.reason,
					     exception_info.description);
			}

			// Filling the matrix with the pixels values not
			// alpha

			if (verbose > 1) tc_log_info(MOD_NAME, "GetImagePixels");
			pixel_packet = GetImagePixels(pattern,0,0,
						      pattern->columns,
						      pattern->rows);

			if (verbose > 1) tc_log_info(MOD_NAME, "Filling the Image matrix");
			for (t = 0; t < pattern->rows; t++)
				for (r = 0; r < pattern->columns; r++){
					index = t*pattern->columns + r;
					if (pixel_packet[index].opacity == 0){
						temp=tc_malloc(sizeof(struct pixelsMask));
						temp->row=t;
						temp->col=r;
						temp->r = (uint8_t)ScaleQuantumToChar(pixel_packet[index].red);
						temp->g = (uint8_t)ScaleQuantumToChar(pixel_packet[index].green);
						temp->b = (uint8_t)ScaleQuantumToChar(pixel_packet[index].blue);
						temp->next=NULL;

						if (pixel_last == NULL){
							pixel_last = temp;
							compare[instance]->pixel_mask = temp;
						}else{
							pixel_last->next = temp;
							pixel_last = temp;
						}
					}
				}

			if (verbose)
                tc_log_info(MOD_NAME, "%s %s",
					    MOD_VERSION, MOD_CAP);
		}
		return(0);
	}


	//----------------------------------
	//
	// filter close
	//
	//----------------------------------


	if(ptr->tag & TC_FILTER_CLOSE) {

		if (compare[instance] != NULL) {
			fclose(compare[instance]->results);
			free(compare[instance]);
		}
		DestroyMagick();
		compare[instance]=NULL;

		return(0);

	} /* filter close */

	//----------------------------------
	//
	// filter frame routine
	//
	//----------------------------------


	// tag variable indicates, if we are called before
	// transcodes internal video/audio frame processing routines
	// or after and determines video/audio context

	if((ptr->tag & TC_POST_M_PROCESS) && (ptr->tag & TC_VIDEO))  {
		// For now I only support RGB color space
		pixelsMask *item = NULL;
		double sr,sg,sb;
		double avg_dr,avg_dg,avg_db;

		if (compare[instance]->vob->im_v_codec == CODEC_RGB){

			int r,g,b,c;
			double width_long;

			if (compare[instance]->pixel_mask != NULL)
			{
				item = compare[instance]->pixel_mask;
				c = 0;

				sr = 0.0;
				sg = 0.0;
				sb = 0.0;

				width_long = compare[instance]->width*3;
				while(item){
					r = item->row*width_long + item->col*3;
					g = item->row*width_long
						+ item->col*3 + 1;
					b = item->row*width_long
						+ item->col*3 + 2;

				// diff between points
				// Interchange RGB values if necesary
					sr = sr + (double)abs((unsigned char)ptr->video_buf[r] - item->r);
					sg = sg + (double)abs((unsigned char)ptr->video_buf[g] - item->g);
					sb = sb + (double)abs((unsigned char)ptr->video_buf[b] - item->b);
					item = item->next;
					c++;
				}

				avg_dr = sr/(double)c;
				avg_dg = sg/(double)c;
				avg_db = sb/(double)c;

				if ((avg_dr < compare[instance]->delta) && (avg_dg < compare[instance]->delta) && (avg_db < compare[instance]->delta))
					fprintf(compare[instance]->results,"1");
				else
					fprintf(compare[instance]->results,"n");
				fflush(compare[instance]->results);
			}
			compare[instance]->frames++;
			return(0);
		}else{

                        // The colospace is YUV

                        // FIXME: Doesn't works, I need to code all this part
			// again

			int Y,Cr,Cb,c;

			if (compare[instance]->pixel_mask != NULL)
			{
				item = compare[instance]->pixel_mask;
				c = 0;

				sr = 0.0;
				sg = 0.0;
				sb = 0.0;

				while(item){
					Y  = item->row*compare[instance]->width + item->col;
					Cb = compare[instance]->height*compare[instance]->width
						+ (int)((item->row*compare[instance]->width + item->col)/4);
					Cr = compare[instance]->height*compare[instance]->width
						+ (int)((compare[instance]->height*compare[instance]->width)/4)
						+ (int)((item->row*compare[instance]->width + item->col)/4);

				        // diff between points
				        // Interchange RGB values if necesary

					sr = sr + (double)abs((unsigned char)ptr->video_buf[Y] - item->r);
					sg = sg + (double)abs((unsigned char)ptr->video_buf[Cb] - item->g);
					sb = sb + (double)abs((unsigned char)ptr->video_buf[Cr] - item->b);
					item = item->next;
					c++;
				}

				avg_dr = sr/(double)c;
				avg_dg = sg/(double)c;
				avg_db = sb/(double)c;

				if ((avg_dr < compare[instance]->delta) && (avg_dg < compare[instance]->delta) && (avg_db < compare[instance]->delta))
					fprintf(compare[instance]->results,"1");
				else
					fprintf(compare[instance]->results,"n");
			}
			compare[instance]->frames++;
			return(0);

		}
	}

	return(0);
}
// Proposal:
// Tilmann Bitterberg Sat, 14 Jun 2003 00:29:06 +0200 (CEST)
//
//    char *Y, *Cb, *Cr;
//    Y  = p->video_buf;
//    Cb = p->video_buf + mydata->width*mydata->height;
//    Cr = p->video_buf + 5*mydata->width*mydata->height/4;

//    for (i=0; i<mydata->width*mydata->height; i++) {
//      pixel_packet->red == *Y++;
//      get_next_pixel();
//    }

//    for (i=0; i<mydata->width*mydata->height>>2; i++) {
//      pixel_packet->green == *Cr++;
//      pixel_packet->blue == *Cb++;
//      get_next_pixel();
//    }






