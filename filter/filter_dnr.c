/*
 *  filter_dnr.c
 *
 *  Copyright (C) Gerhard Monzel - November 2001
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

#define MOD_NAME    "filter_dnr.so"
#define MOD_VERSION "v0.2 (2003-01-21)"
#define MOD_CAP     "dynamic noise reduction"
#define MOD_AUTHOR  "Gerhard Monzel"

#include "transcode.h"
#include "filter.h"
#include "libtc/optstr.h"

#include <math.h>


typedef unsigned char T_PIXEL;

typedef struct t_dnr_filter_ctx
{
  int     is_first_frame;
  int     pPartial;
  int     pThreshold;
  int     pThreshold2;
  int     pPixellock;
  int     pPixellock2;
  int     pScene;

  int     isYUV;
  T_PIXEL *lastframe;
  T_PIXEL *origframe;
  int     gu_ofs, bv_ofs;

  unsigned char lookup[256][256];
  unsigned char *lockhistory;

  T_PIXEL *src_data;
  T_PIXEL *undo_data;
  long  src_h, src_w;
  int   img_size;
  int   hist_size;
  int   pitch;
  int   line_size_c;
  int   line_size_l;
  int   undo;

} T_DNR_FILTER_CTX;


static int dnr_run(T_DNR_FILTER_CTX *fctx, T_PIXEL *data)
{
  T_PIXEL       *RY1, *RY2, *RY3, *GU1, *GU2, *GU3, *BV1, *BV2, *BV3;
  int           rl, rc, w, h, update_needed, totpixels;
  int           threshRY, threshGU=0, threshBV=0;
  int           ry1, ry2, gu1=0, gu2=0, bv1=0, bv2=0;
  long          totlocks = 0;
  unsigned char *lockhistory = fctx->lockhistory;

  //-- get data into account --
  fctx->src_data = data;

  //-- if we are dealing with the first --
  //-- frame, just make a copy.         --
  if (fctx->is_first_frame)
  {
    ac_memcpy(fctx->lastframe, fctx->src_data, fctx->img_size);
    fctx->undo_data      = fctx->lastframe;
    fctx->is_first_frame = 0;

    return 0;
  }

  //-- make sure to preserve the existing frame --
  //-- in case this is a scene change           --
  ac_memcpy(fctx->origframe, fctx->src_data, fctx->img_size);

  if (fctx->isYUV)
  {
    RY1 = fctx->src_data;
    GU1 = RY1 + fctx->gu_ofs;
    BV1 = RY1 + fctx->bv_ofs;

    RY2 = fctx->lastframe;
    GU2 = RY2 + fctx->gu_ofs;
    BV2 = RY2 + fctx->bv_ofs;

    RY3 = fctx->src_data;
    GU3 = RY3 + fctx->gu_ofs;
    BV3 = RY3 + fctx->bv_ofs;
  }
  else
  {
    RY1 = fctx->src_data;
    GU1 = RY1 + fctx->gu_ofs;
    BV1 = RY1 + fctx->bv_ofs;

    RY2 = fctx->lastframe;
    GU2 = RY2 + fctx->gu_ofs;
    BV2 = RY2 + fctx->bv_ofs;

    RY3 = fctx->src_data;
    GU3 = RY3 + fctx->gu_ofs;
    BV3 = RY3 + fctx->bv_ofs;
  }

  h = fctx->src_h;
  do
  {
    w  = fctx->src_w;
    rl = rc = 0;
    do
    {
      update_needed = 1;

      //-- on every row get (luma) actual/locked pixels --
      //-- -> calculate thresold (biased diff.)         --
      //--------------------------------------------------
      ry1 = RY1[rl];
      ry2 = RY2[rl];
      threshRY = fctx->lookup[ry1][ry2];

      //-- in YUV-Mode on every even row  (RGB every --
      //-- row) get (chroma) actual/locked pixels    --
      //-- -> calculate thresold (biased diff.)      --
      //-----------------------------------------------
      if (!fctx->isYUV || !(rl&0x01))
      {
        gu1 = GU1[rc];
        bv1 = BV1[rc];
        gu2 = GU2[rc];
        bv2 = BV2[rc];
        threshGU = fctx->lookup[gu1][gu2];
        threshBV = fctx->lookup[bv1][bv2];
      }

      //-- PARTIAL --
      //-------------
      if (fctx->pPartial)
      {
        // we're doing a full pixel lock since we're --
        // under all thresholds in a couple of time  --
        //---------------------------------------------
        if ( (threshRY < fctx->pPixellock) &&
             (threshGU < fctx->pPixellock2) &&
             (threshBV < fctx->pPixellock2))
        {

          //-- if we've locked more than 30 times at --
          //-- this point, let's refresh the pixel.  --
          if (*lockhistory > 30)
          {
            *lockhistory = 0;

            ry1 = (ry1 + ry2) / 2;
            gu1 = (gu1 + gu2) / 2;
            bv1 = (bv1 + bv2) / 2;
          }
          else
          {
            *lockhistory = *lockhistory + 1;

            //-- take locked pixels --
            ry1 = ry2;
            gu1 = gu2;
            bv1 = bv2;
          }
        }
        //-- If the luma is within pixellock, and the chroma is within   --
        //-- blend, lets blend the chroma and lock the luma.
        //-----------------------------------------------------------------
        else if ( (threshRY < fctx->pPixellock) &&
                  (threshGU < fctx->pThreshold2) &&
                  (threshBV < fctx->pThreshold2) )
        {
           *lockhistory = 0;

           ry1 = ry2;
           gu1 = (gu1 + gu2) / 2;
           bv1 = (bv1 + bv2) / 2;
        }
        //-- We are above pixellock in luma and chroma, but     --
        //-- below the blend thresholds in both, so let's blend --
        //--------------------------------------------------------
        else if ( (threshRY < fctx->pThreshold) &&
                  (threshGU < fctx->pThreshold2) &&
                  (threshBV < fctx->pThreshold2) )
        {
           *lockhistory = 0;

           ry1 = (ry1 + ry2) / 2;
           gu1 = (gu1 + gu2) / 2;
           bv1 = (bv1 + bv2) / 2;
        }
        //-- if we are above all thresholds, --
        //-- just leave the output untouched --
        //-------------------------------------
        else
        {
           *lockhistory  = 0;
           update_needed = 0;
           totlocks++;
        }
      }
      //-- nonPARTIAL --
      //----------------
      else
      {
        //-- beneath pixellock so lets keep   --
        //-- the existing pixel (most likely) --
        //--------------------------------------
        if ( (threshRY < fctx->pPixellock) &&
             (threshGU < fctx->pPixellock2) &&
             (threshBV < fctx->pPixellock2) )
        {
          // if we've locked more than 30 times at this point,
          // let's refresh the pixel
          if (*lockhistory > 30)
          {
            *lockhistory = 0;

            ry1 = (ry1 + ry2) / 2;
            gu1 = (gu1 + gu2) / 2;
            bv1 = (bv1 + bv2) / 2;
          }
          else
          {
            *lockhistory = *lockhistory + 1;

            ry1 = ry2;
            gu1 = gu2;
            bv1 = bv2;
          }
        }
        //-- we are above pixellock, but below the --
        //-- blend threshold so we want to blend   --
        //-------------------------------------------
        else if ( (threshRY < fctx->pThreshold) &&
                  (threshGU < fctx->pThreshold2) &&
                  (threshBV < fctx->pThreshold2) )
        {
            *lockhistory = 0;

            ry1 = (ry1 + ry2) / 2;
            gu1 = (gu1 + gu2) / 2;
            bv1 = (bv1 + bv2) / 2;
        }
        //-- it's beyond the thresholds, just leave it alone --
        //-----------------------------------------------------
        else
        {
          *lockhistory  = 0;
          update_needed = 0;
          totlocks++;
        }
      }

      //-- set destination --
      //---------------------
      if (update_needed)
      {
        RY3[rl] = ry1;
        GU3[rc] = gu1;
        BV3[rc] = bv1;
      }

      //-- refresh locked pixels --
      //---------------------------
      if ( *lockhistory == 0 )
      {
        RY2[rl] = ry1;
        GU2[rc] = gu1;
        BV2[rc] = bv1;
      }

      lockhistory++;

      rl += fctx->pitch;
      rc  = (fctx->isYUV) ? (rl>>1) : rl;

    } while(--w);

    //-- next line ... --
    RY1 += fctx->line_size_l;
    RY2 += fctx->line_size_l;
    RY3 += fctx->line_size_l;

    //-- ... in YUV-Mode for chromas, only on even luma-lines --
    if (!fctx->isYUV || !(h&0x01) )
    {
      GU1 += fctx->line_size_c;
      BV1 += fctx->line_size_c;

      GU2 += fctx->line_size_c;
      BV2 += fctx->line_size_c;

      GU3 += fctx->line_size_c;
      BV3 += fctx->line_size_c;
    }

  } while(--h);

  totpixels  = fctx->src_h * fctx->src_w;
  totpixels *= fctx->pScene;
  totpixels /= 100;

  // If more than the specified percent of pixels have exceeded all thresholds
  // then we restore the saved frame.  (this doesn't happen very often
  // hopefully)  We also set the pixellock history to 0 for all frames

  if (totlocks > totpixels)
  {
    T_PIXEL *ptmp = fctx->lastframe;

    fctx->lastframe  = fctx->origframe;
    fctx->undo_data  = fctx->lastframe;
    fctx->origframe  = ptmp;
    fctx->undo       = 1;

    memset(fctx->lockhistory, 0, fctx->hist_size);
  }
  else
  {
    fctx->undo_data = fctx->src_data;
    fctx->undo      = 0;
  }

  return 0;
}

static void dnr_cleanup(T_DNR_FILTER_CTX *fctx)
{
  if (fctx->lastframe) free(fctx->lastframe);
  if (fctx->origframe) free(fctx->origframe);
  if (fctx->lockhistory) free(fctx->lockhistory);

  fctx->lastframe   = NULL;
  fctx->origframe   = NULL;
  fctx->lockhistory = NULL;
}

#define DEFAULT_LT 10
#define DEFAULT_LL  4
#define DEFAULT_CT 16
#define DEFAULT_CL  8
#define DEFAULT_SC 30

static T_DNR_FILTER_CTX *dnr_init(int src_w, int src_h, int isYUV)
{
  double low1, low2;
  double high1, high2;
  int    a, b, dif1, dif2;

  T_DNR_FILTER_CTX *fctx = tc_malloc (sizeof(T_DNR_FILTER_CTX));

  //-- PARAMETERS --
  fctx->pThreshold  = DEFAULT_LT; // threshold to blend luma/red (default 10)
  fctx->pPixellock  = DEFAULT_LL; // threshold to lock luma/red (default 4)
  fctx->pThreshold2 = DEFAULT_CT; // threshold to blend croma/green+blue (default 16)
  fctx->pPixellock2 = DEFAULT_CL; // threshold to lock croma/green+blue (default 8)
  fctx->pScene      = DEFAULT_SC; // percentage of picture difference
                                  // to interpret as a new scene (default 30%)
  fctx->pPartial    = 0;          // operating mode [0,1] (default 0)
  //----------------

  fctx->isYUV = isYUV;
  fctx->is_first_frame = 1;
  fctx->lastframe      = (T_PIXEL *)calloc(src_h * src_w, 3);
  fctx->origframe      = (T_PIXEL *)calloc(src_h * src_w, 3);
  fctx->lockhistory    = (unsigned char *)calloc(src_h * src_w, 1);
  fctx->src_h          = src_h;
  fctx->src_w          = src_w;
  fctx->hist_size      = src_h * src_w;

  if (isYUV)
  {
    fctx->gu_ofs   = fctx->hist_size;
    fctx->bv_ofs   = fctx->gu_ofs + (src_h/2) * (src_w/2);
    fctx->img_size = fctx->bv_ofs + (src_h/2) * (src_w/2);
    fctx->pitch    = 1;

    fctx->line_size_c = (src_w >> 1);
    fctx->line_size_l = src_w;
  }
  else
  {
    fctx->img_size = fctx->hist_size * 3;
    fctx->gu_ofs = 1;
    fctx->bv_ofs = 2;
    fctx->pitch  = 3;

    fctx->line_size_c = src_w * 3;
    fctx->line_size_l = src_w * 3;
  }

  if (!fctx->lastframe || !fctx->origframe || !fctx->lockhistory)
  {
    dnr_cleanup(fctx);
    return NULL;
  }

  // setup a biased thresholding difference matrix
  // this is an expensive operation we only want to to once
  for (a = 0; a < 256; a++)
  {
    for (b = 0; b < 256; b++)
    {
      // instead of scaling linearly
      // we scale according to the following formulas
      // val1 = 256 * (x / 256) ^ .9
      // and
      // val2 = 256 * (x / 256) ^ (1/.9)
      // and we choose the maximum distance between two points
      // based on these two scales
      low1 = a;
      low2 = b;
      low1 = low1 / 256;
      low1 = 256 * pow(low1, .9);
      low2 = low2 / 256;
      low2 = 256 * pow(low2, .9);

      // the low scale should make all values larger
      // and the high scale should make all values smaller
      high1 = a;
      high2 = b;
      high1 = high1 / 256;
      high2 = high2 / 256;
      high1 = 256 * pow(high1, 1.0/.9);
      high2 = 256 * pow(high2, 1.0/.9);
      dif1 = (int) (low1 - low2);
      if (dif1 < 0) dif1 *= -1;
      dif2 = (int) (high1 - high2);
      if (dif2 < 0) dif2 *= -1;
      dif1 = (dif1 > dif2) ? dif1 : dif2;
      fctx->lookup[a][b] = dif1;
    }
  }

  return fctx;
}

// old or new syntax?
static int is_optstr (char *buf) {
    if (strchr(buf, '='))
	return 1;
    if (strchr(buf, 'l'))
	return 1;
    if (strchr(buf, 'c'))
	return 1;
    return 0;
}
/*-------------------------------------------------
 *
 * single function interface
 *
 *-------------------------------------------------*/

int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t           *ptr     = (vframe_list_t *)ptr_;
  static vob_t            *vob     = NULL;
  static T_DNR_FILTER_CTX *my_fctx = NULL;

  //----------------------------------
  //
  // filter init
  //
  //----------------------------------

  if(ptr->tag & TC_FILTER_GET_CONFIG) {
    char buf[32];

    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, "Gerhard Monzel", "VYRO", "1");

    tc_snprintf(buf, 32, "%d", my_fctx->pThreshold);
    optstr_param (options, "lt", "Threshold to blend luma/red", "%d", buf, "1", "128");

    tc_snprintf(buf, 32, "%d", my_fctx->pPixellock);
    optstr_param (options, "ll", "Threshold to lock luma/red", "%d", buf, "1", "128");

    tc_snprintf(buf, 32, "%d", my_fctx->pThreshold2);
    optstr_param (options, "ct", "Threshold to blend croma/green+blue", "%d", buf, "1", "128");

    tc_snprintf(buf, 32, "%d", my_fctx->pPixellock2);
    optstr_param (options, "cl", "Threshold to lock croma/green+blue", "%d", buf, "1", "128");

    tc_snprintf(buf, 32, "%d", my_fctx->pScene);
    optstr_param (options, "sc", "Percentage of picture difference (scene change)", "%d", buf, "1", "90");


      return 0;
  }

  if(ptr->tag & TC_FILTER_INIT)
  {
    if((vob = tc_get_vob())==NULL) return(-1);

    //-- initialization --
    my_fctx = dnr_init( vob->ex_v_width, vob->ex_v_height,
                        (vob->im_v_codec==CODEC_RGB)? 0:1 );
    if (!my_fctx) {
      return (-1);
    }

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);


    if (options)
    {
      if (!is_optstr(options)) {
	char *p1, *p2;
	char hlp_str[128];

	p1 = options;
	p2 = hlp_str;
	do
	{
	  if (*p1 == ':')
	  {
	    *p2 = ' ';
	    p2++;
	  }
	  *p2 = *p1;
	  p1++;
	  p2++;
	} while (*p1);
	*p2 = '\0';

	if(verbose & TC_DEBUG) tc_log_info(MOD_NAME, "options=%s", options);

	if ( (p1 = strtok(hlp_str,":")) != NULL)
	  my_fctx->pThreshold  = atoi(p1);
	if ( (p1 = strtok(NULL, ":")) != NULL )
	  my_fctx->pPixellock  = atoi(p1);
	if ( (p1 = strtok(NULL, ":")) != NULL )
	  my_fctx->pThreshold2 = atoi(p1);
	if ( (p1 = strtok(NULL, ":")) != NULL )
	  my_fctx->pPixellock2 = atoi(p1);
	if ( (p1 = strtok(NULL, ":")) != NULL )
	  my_fctx->pScene = atoi(p1);


      } else { // new options

	optstr_get (options, "lt", "%d", &my_fctx->pThreshold);
	optstr_get (options, "ll", "%d", &my_fctx->pPixellock);
	optstr_get (options, "ct", "%d", &my_fctx->pThreshold2);
	optstr_get (options, "cl", "%d", &my_fctx->pPixellock2);
	optstr_get (options, "sc", "%d", &my_fctx->pScene);

      }

      if (my_fctx->pThreshold > 128 || my_fctx->pThreshold < 1)
	my_fctx->pThreshold = DEFAULT_LT;
      if (my_fctx->pPixellock > 128 || my_fctx->pPixellock < 1)
	my_fctx->pPixellock = DEFAULT_LL;
      if (my_fctx->pThreshold2 > 128 || my_fctx->pThreshold2 < 1)
	my_fctx->pThreshold2 = DEFAULT_CT;
      if (my_fctx->pPixellock2 > 128 || my_fctx->pPixellock2 < 1)
	my_fctx->pPixellock2 = DEFAULT_CL;
      if (my_fctx->pScene > 90 || my_fctx->pScene < 1)
	my_fctx->pScene = DEFAULT_SC;
    }

    return(0);
  }

  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE)
  {
    dnr_cleanup(my_fctx);
    my_fctx = NULL;
    return(0);
  }

  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------


  // tag variable indicates, if we are called before
  // transcodes internal video/audo frame processing routines
  // or after and determines video/audio context

  if((ptr->tag & TC_POST_M_PROCESS) && (ptr->tag & TC_VIDEO) && !(ptr->attributes & TC_FRAME_IS_SKIPPED))
  {
    dnr_run(my_fctx, ptr->video_buf);

    if (my_fctx->undo)
      ac_memcpy(ptr->video_buf, my_fctx->undo_data, my_fctx->img_size);
  }

  return(0);
}

