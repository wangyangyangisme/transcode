/*
 *  filter_logoaway.c
 *
 *  Copyright (C) Thomas Wehrspann - 2002/2003
 *
 *  This plugin is based on ideas of Krzysztof Wojdon's
 *  logoaway filter for VirtualDub
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
  * TODO:
  *   -blur                     -
 */

/*
 * ChangeLog:
 * v0.1 (2002-12-04)  Thomas Wehrspann <thomas@wehrspann.de>
 *    -First version
 *
 * v0.1 (2002-12-19) Tilmann Bitterberg
 *    -Support for new filter-API(optstr_param) added
 *
 * v0.2 (2003-01-15) Thomas Wehrspann <thomas@wehrspann.de>
 *    -Fixed RGB-SOLID-mode
 *    -Added alpha channel
 *      now you need ImageMagick
 *    -Documentation added
 *
 * v0.2 (2003-01-21) Tilmann Bitterberg
 *    -More support for new filter-API.
 *
 * v0.2 (2003-04-08) Tilmann Bitterberg
 *    -change include order to avoid warnings from Magick
 *
 * v0.3 (2003-04-24) Thomas Wehrspann <thomas@wehrspann.de>
 *    -Fixed bug with multiple instances
 *    -coordinates in RGB=YUV
 *    -Added SHAPE-mode
 *    -Documentation updated
 *
 * v0.4 (2003-09-03) Tilmann Bitterberg
 *    -add information for shape mode
 *
 * v0.5 (2004-03-07) Thomas Wehrspann <thomas@wehrspann.de>
 *    -Changed filter to PRE process
 *    -Added dump image function (RGB only)
 */

#define MOD_NAME    "filter_logoaway.so"
#define MOD_VERSION "v0.5.1 (2004-03-01)"
#define MOD_CAP     "remove an image from the video"
#define MOD_AUTHOR  "Thomas Wehrspann"

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

#include <stdlib.h>
#include <stdio.h>

static vob_t *vob=NULL;

static char *modes[] = {"NONE", "SOLID", "XY", "SHAPE"};

typedef struct logoaway_data {
  int           id; /* legacy, for dump */
  unsigned int  start, end;
  int           xpos, ypos;
  int           width, height;
  int           mode;
  int           border;
  int           xweight, yweight;
  int           rcolor, gcolor, bcolor;
  int           ycolor, ucolor, vcolor;
  char          file[PATH_MAX];

  int           alpha;

  ExceptionInfo exception_info;
  Image         *image;
  ImageInfo     *image_info;
  PixelPacket   *pixel_packet;

  int           dump;
  char          *dump_buf;
  Image         *dumpimage;
  ImageInfo     *dumpimage_info;
} logoaway_data;

/* FIXME: this uses the filter ID as an index--the ID can grow
 * arbitrarily large, so this needs to be fixed */
static logoaway_data *data[100];


/*********************************************************
 * help text
 * this function prints out a small description
 * of this filter and the commandline options,
 * when the "help" option is given
 * @param   void      nothing
 * @return  void      nothing
 *********************************************************/
static void help_optstr(void)
{
    tc_log_info(MOD_NAME, "(%s) help\n"
"* Overview\n"
"    This filter removes an image in a user specified area from the video.\n"
"    You can choose from different methods.\n"
"\n"
"* Options\n"
"       'range' Frame Range      (0-oo)                        [0-end]\n"
"         'pos' Position         (0-width x 0-height)          [0x0]\n"
"        'size' Size             (0-width x 0-height)          [10x10]\n"
"        'mode' Filter Mode      (0=none,1=solid,2=xy,3=shape) [0]\n"
"      'border' Visible Border\n"
"        'dump' Dump filter area to file\n"
"     'xweight' X-Y Weight       (0%%-100%%)                   [50]\n"
"        'fill' Solid Fill Color (RRGGBB)                      [000000]\n"
"        'file' Image with alpha/shape information             []\n"
"\n"
		, MOD_CAP);
}


/*********************************************************
 * blend two pixel
 * this function blends two pixel with the given
 * weight
 * @param   srcPixel        source pixel value
 *          destPixel       source pixel value
 *          alpha           weight
 * @return  unsigned char   new pixel value
 *********************************************************/
static unsigned char alpha_blending(unsigned char srcPixel, unsigned char destPixel, int alpha)
{
  return ( ( ( alpha * ( srcPixel - destPixel ) ) >> 8 ) + destPixel );
}


/*********************************************************
 * processing of the video frame (RGB codec)
 * processes the actual frame depending on the
 * selected mode
 * @param   buffer      video buffer
 *          width       video width
 *          height      video height
 *          instance    filter instance
 * @return  void        nothing
 *********************************************************/
static void work_with_rgb_frame(logoaway_data *LD, char *buffer, int width, int height)
{
  int row, col, i;
  int xdistance, ydistance, distance_west, distance_north;
  unsigned char hcalc, vcalc;
  int buf_off, pkt_off, buf_off_xpos, buf_off_width, buf_off_ypos, buf_off_height;
  int alpha_hori, alpha_vert;
  uint8_t alpha_px, new_px;

  if(LD->dump) { // DUMP
    for(row=LD->ypos; row<LD->height; ++row) {
      for(col=LD->xpos; col<LD->width; ++col) {

        pkt_off = ((row-LD->ypos)*(LD->width-LD->xpos)+(col-LD->xpos)) * 3;
        buf_off = ((height-row)*width+col) * 3;

        /* R */
        LD->dump_buf[pkt_off +0] = buffer[buf_off +0];

        /* G */
        LD->dump_buf[pkt_off +1] = buffer[buf_off +1];

        /* B */
        LD->dump_buf[pkt_off +2] = buffer[buf_off +2];
      }
    }

    LD->dumpimage = ConstituteImage(LD->width-LD->xpos, LD->height-LD->ypos, "RGB", CharPixel, LD->dump_buf, &LD->exception_info);
    tc_snprintf(LD->dumpimage->filename, MaxTextExtent, "dump[%d].png", LD->id);

    WriteImage(LD->dumpimage_info, LD->dumpimage);
  }

  switch(LD->mode) {

  case 0: // NONE

      break;

  case 1: // SOLID

      for(row=LD->ypos; row<LD->height; ++row) {
        for(col=LD->xpos; col<LD->width; ++col) {

          buf_off = ((height-row)*width+col) * 3;
          pkt_off = (row-LD->ypos) * (LD->width-LD->xpos) + (col-LD->xpos);
          /* R */
          if (!LD->alpha) {
              buffer[buf_off +0] = LD->rcolor;
          } else {
              alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
              buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], LD->rcolor, alpha_px);
          }
          /* G */
          if (!LD->alpha) {
              buffer[buf_off +1] = LD->gcolor;
          } else {
              alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].green);
              buffer[buf_off +1] = alpha_blending(buffer[buf_off +1], LD->gcolor, alpha_px);
          }
          /* B */
          if (!LD->alpha) {
              buffer[buf_off +2] = LD->bcolor;
          } else {
              alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].blue);
              buffer[buf_off +2] = alpha_blending(buffer[buf_off +2], LD->bcolor, alpha_px);
          }
        }
      }

      break;

  case 2: // XY

      xdistance = 256 / (LD->width - LD->xpos);
      ydistance = 256 / (LD->height - LD->ypos);
      for(row=LD->ypos; row<LD->height; ++row) {
        distance_north = LD->height - row;

        alpha_vert = ydistance * distance_north;

        buf_off_xpos = ((height-row)*width+LD->xpos) * 3;
        buf_off_width = ((height-row)*width+LD->width) * 3;

        for(col=LD->xpos; col<LD->width; ++col) {
          distance_west  = LD->width - col;

          alpha_hori = xdistance * distance_west;

          buf_off_ypos = ((height-LD->ypos)*width+col) * 3;
          buf_off_height = ((height-LD->height)*width+col) * 3;
          buf_off = ((height-row)*width+col) * 3;

          pkt_off = (row-LD->ypos) * (LD->width-LD->xpos) + (col-LD->xpos);

          /* R */
          hcalc  = alpha_blending(buffer[buf_off_xpos +0], buffer[buf_off_width  +0], alpha_hori);
          vcalc  = alpha_blending(buffer[buf_off_ypos +0], buffer[buf_off_height +0], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          if (!LD->alpha) {
              buffer[buf_off +0] = new_px;
          } else {
              alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
              buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], new_px, alpha_px);
          }
          /* G */
          hcalc  = alpha_blending(buffer[buf_off_xpos +1], buffer[buf_off_width  +1], alpha_hori);
          vcalc  = alpha_blending(buffer[buf_off_ypos +1], buffer[buf_off_height +1], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          if (!LD->alpha) {
              buffer[buf_off +1] = new_px;
          } else {
              alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].green);
              buffer[buf_off +1] = alpha_blending(buffer[buf_off +1], new_px, alpha_px);
          }
          /* B */
          hcalc  = alpha_blending(buffer[buf_off_xpos +2], buffer[buf_off_width  +2], alpha_hori);
          vcalc  = alpha_blending(buffer[buf_off_ypos +2], buffer[buf_off_height +2], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          if (!LD->alpha) {
              buffer[buf_off +2] = new_px;
          } else {
              alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
              buffer[buf_off +2] = alpha_blending(buffer[buf_off +2], new_px, alpha_px);
          }
        }
      }

    break;

  case 3: // SHAPE

      xdistance = 256 / (LD->width - LD->xpos);
      ydistance = 256 / (LD->height - LD->ypos);
      for(row=LD->ypos; row<LD->height; ++row) {
        distance_north = LD->height - row;

        alpha_vert = ydistance * distance_north;

        for(col=LD->xpos; col<LD->width; ++col) {
          distance_west  = LD->width - col;

          alpha_hori = xdistance * distance_west;

          buf_off = ((height-row)*width+col) * 3;
          pkt_off = (row-LD->ypos) * (LD->width-LD->xpos) + (col-LD->xpos);

          buf_off_xpos   = ((height-row)*width+LD->xpos)   * 3;
          buf_off_width  = ((height-row)*width+LD->width)  * 3;
          buf_off_ypos   = ((height-LD->ypos)*width+col)   * 3;
          buf_off_height = ((height-LD->height)*width+col) * 3;

          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off-i].red);
          while ((alpha_px != 255) && (col-i>LD->xpos))
            i++;
          buf_off_xpos   = ((height-row)*width + col-i) * 3;
          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off+i].red);
          while ((alpha_px != 255) && (col+i<LD->width))
            i++;
          buf_off_width  = ((height-row)*width + col+i) * 3;

          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off-i*(LD->width-LD->xpos)].red);
          while ((alpha_px != 255) && (row-i>LD->ypos))
            i++;
          buf_off_ypos   = (height*width*3)-((row-i)*width - col) * 3;
          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off+i*(LD->width-LD->xpos)].red);
          while ((alpha_px != 255) && (row+i<LD->height))
            i++;
          buf_off_height = (height*width*3)-((row+i)*width - col) * 3;

          alpha_px     = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
          /* R */
          hcalc  = alpha_blending(buffer[buf_off_xpos +0], buffer[buf_off_width  +0], alpha_hori);
          vcalc  = alpha_blending(buffer[buf_off_ypos +0], buffer[buf_off_height +0], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          buffer[buf_off +0] = alpha_blending(buffer[buf_off +0], new_px, alpha_px);
          /* G */
          hcalc = alpha_blending(buffer[buf_off_xpos +1], buffer[buf_off_width  +1], alpha_hori);
          vcalc = alpha_blending(buffer[buf_off_ypos +1], buffer[buf_off_height +1], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          /* reuse red alpha_px. sic. */
          buffer[buf_off +1] = alpha_blending(buffer[buf_off +1], new_px, alpha_px);
          /* B */
          hcalc = alpha_blending(buffer[buf_off_xpos +2], buffer[buf_off_width  +2], alpha_hori);
          vcalc = alpha_blending(buffer[buf_off_ypos +2], buffer[buf_off_height +2], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          /* reuse red alpha_px. sic. */
          buffer[buf_off +2] = alpha_blending(buffer[buf_off +2], new_px, alpha_px);
        }
      }

      break;

  }

  if(LD->border) { 
    for(row=LD->ypos; row<LD->height; ++row) {
      if((row == LD->ypos) || (row==LD->height-1)) {
        for(col=LD->xpos*3; col<LD->width*3; ++col) if(col&1) buffer[((height-row)*width*3+col)] = 255 & 0xff;
      }
      if(row&1) {
        buf_off = ((height-row)*width+LD->xpos)*3;
        buffer[buf_off +0] = 255;
        buffer[buf_off +1] = 255;
        buffer[buf_off +2] = 255;
      }
      if(row&1) {
        buf_off = ((height-row)*width+LD->width)*3;
        buffer[buf_off +0] = 255;
        buffer[buf_off +1] = 255;
        buffer[buf_off +2] = 255;
      }
    }
  }
}


/*********************************************************
 * processing of the video frame (YUV codec)
 * processes the actual frame depend on the
 * selected mode
 * @param   buffer      video buffer
 *          width       video width
 *          height      video height
 *          instance    filter instance
 * @return  void        nothing
 *********************************************************/
static void work_with_yuv_frame(logoaway_data *LD, char *buffer, int width, int height)
{
  int row, col, i;
  int craddr, cbaddr;
  int xdistance, ydistance, distance_west, distance_north;
  unsigned char hcalc, vcalc;
  int buf_off, pkt_off=0, buf_off_xpos, buf_off_width, buf_off_ypos, buf_off_height;
  int alpha_hori, alpha_vert;
  uint8_t alpha_px, new_px;

  craddr = (width * height);
  cbaddr = (width * height) * 5 / 4;

  switch(LD->mode) {

  case 0: // NONE

      break;

  case 1: // SOLID 
      /* Y */
      for(row=LD->ypos; row<LD->height; ++row) {
        for(col=LD->xpos; col<LD->width; ++col) {

          buf_off = row*width+col;
          pkt_off = (row-LD->ypos) * (LD->width-LD->xpos) + (col-LD->xpos);
          if (!LD->alpha) {
            buffer[buf_off] = LD->ycolor;
          } else {
            alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
            buffer[buf_off] = alpha_blending(buffer[buf_off], LD->ycolor, alpha_px);
          }
        }
      }

      /* Cb, Cr */
      for(row=LD->ypos/2+1; row<LD->height/2; ++row) {
        for(col=LD->xpos/2+1; col<LD->width/2; ++col) {

          buf_off = row*width/2+col;
          pkt_off = (row*2-LD->ypos) * (LD->width-LD->xpos) + (col*2-LD->xpos);
          /* sic */
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
          if (!LD->alpha) {
            buffer[craddr + buf_off] = LD->ucolor;
            buffer[cbaddr + buf_off] = LD->vcolor;
          } else {
            buffer[craddr + buf_off] = alpha_blending(buffer[craddr + buf_off], LD->ucolor, alpha_px);
            buffer[cbaddr + buf_off] = alpha_blending(buffer[cbaddr + buf_off], LD->vcolor, alpha_px);
          }
        }
      }

      break;

  case 2: // XY

      /* Y' */
      xdistance = 256 / (LD->width - LD->xpos);
      ydistance = 256 / (LD->height - LD->ypos);
      for(row=LD->ypos; row<LD->height; ++row) {
        distance_north = LD->height - row;

        alpha_vert = ydistance * distance_north;

        buf_off_xpos = row*width+LD->xpos;
        buf_off_width = row*width+LD->width;

        for(col=LD->xpos; col<LD->width; ++col) {
          distance_west  = LD->width - col;

          alpha_hori = xdistance * distance_west;

          buf_off = row*width+col;
          buf_off_ypos = LD->ypos*width+col;
          buf_off_height = LD->height*width+col;

          pkt_off = (row-LD->ypos) * (LD->width-LD->xpos) + (col-LD->xpos);

          hcalc = alpha_blending(buffer[buf_off_xpos], buffer[buf_off_width],  alpha_hori);
          vcalc = alpha_blending(buffer[buf_off_ypos], buffer[buf_off_height], alpha_vert);
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          if (!LD->alpha) {
            buffer[buf_off] = new_px;
          } else {
            buffer[buf_off] = alpha_blending(buffer[buf_off], new_px, alpha_px);
          }
        }
      }

      /* Cb, Cr */
      xdistance = 512 / (LD->width - LD->xpos);
      ydistance = 512 / (LD->height - LD->ypos);
      for (row=LD->ypos/2+1; row<LD->height/2; ++row) {
        distance_north = LD->height/2 - row;

        alpha_vert = ydistance * distance_north;

        buf_off_xpos = row*width/2+LD->xpos/2;
        buf_off_width = row*width/2+LD->width/2;

        for (col=LD->xpos/2+1; col<LD->width/2; ++col) {
          distance_west  = LD->width/2 - col;

          alpha_hori = xdistance * distance_west;

          buf_off = row*width/2+col;
          buf_off_ypos = LD->ypos/2*width/2+col;
          buf_off_height = LD->height/2*width/2+col;

          pkt_off = (row*2-LD->ypos) * (LD->width-LD->xpos) + (col*2-LD->xpos);
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red); 
          /* sic, reuse red alpha_px */

          hcalc  = alpha_blending(buffer[craddr + buf_off_xpos], buffer[craddr + buf_off_width],  alpha_hori);
          vcalc  = alpha_blending(buffer[craddr + buf_off_ypos], buffer[craddr + buf_off_height], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          if (!LD->alpha) {
            buffer[craddr + buf_off] = new_px;
          } else {
            buffer[craddr + buf_off] = alpha_blending(buffer[craddr + buf_off], new_px, alpha_px);
          }

          hcalc  = alpha_blending(buffer[cbaddr + buf_off_xpos], buffer[cbaddr + buf_off_width],  alpha_hori);
          vcalc  = alpha_blending(buffer[cbaddr + buf_off_ypos], buffer[cbaddr + buf_off_height], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          if (!LD->alpha) {
            buffer[cbaddr + buf_off] = new_px;
          } else {
            buffer[cbaddr + buf_off] = alpha_blending(buffer[cbaddr + buf_off], new_px, alpha_px);
          }
        }
      }

      break;

  case 3: // SHAPE

      xdistance = 256 / (LD->width - LD->xpos);
      ydistance = 256 / (LD->height - LD->ypos);
      for(row=LD->ypos; row<LD->height; ++row) {
        distance_north = LD->height - row;

        alpha_vert = ydistance * distance_north;

        for(col=LD->xpos; col<LD->width; ++col) {
          distance_west  = LD->width - col;

          alpha_hori = xdistance * distance_west;

          buf_off = (row*width+col);
          pkt_off = (row-LD->ypos) * (LD->width-LD->xpos) + (col-LD->xpos);

          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off-i].red);
          while ((alpha_px != 255) && (col-i>LD->xpos))
            i++;
          buf_off_xpos   = (row*width + col-i);
          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off+i].red);
          while ((alpha_px != 255) && (col+i<LD->width))
            i++;
          buf_off_width  = (row*width + col+i);

          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off-i*(LD->width-LD->xpos)].red);
          while ((alpha_px != 255) && (row-i>LD->ypos))
            i++;
          buf_off_ypos   = ((row-i)*width + col);
          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off+i*(LD->width-LD->xpos)].red);
          while ((alpha_px != 255) && (row+i<LD->height))
            i++;
          buf_off_height = ((row+i)*width + col);

          hcalc  = alpha_blending( buffer[buf_off_xpos], buffer[buf_off_width],  alpha_hori );
          vcalc  = alpha_blending( buffer[buf_off_ypos], buffer[buf_off_height], alpha_vert );
          alpha_px     = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          buffer[buf_off] = alpha_blending(buffer[buf_off], new_px, alpha_px);
        }
      }

      /* Cb, Cr */
      xdistance = 512 / (LD->width - LD->xpos);
      ydistance = 512 / (LD->height - LD->ypos);
      for (row=LD->ypos/2+1; row<LD->height/2; ++row) {
        distance_north = LD->height/2 - row;

        alpha_vert = ydistance * distance_north;

        for (col=LD->xpos/2+1; col<LD->width/2; ++col) {
          distance_west  = LD->width/2 - col;

          alpha_hori = xdistance * distance_west;

          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off-i].red);
          while ((alpha_px != 255) && (col-i>LD->xpos))
            i++;
          buf_off_xpos   = (row*width/2 + col-i);
          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off+i].red);
          while ((alpha_px != 255) && (col+i<LD->width))
            i++;
          buf_off_width  = (row*width/2 + col+i);

          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off-i*(LD->width-LD->xpos)].red);
          while ((alpha_px != 255) && (row-i>LD->ypos))
            i++;
          buf_off_ypos   = ((row-i)*width/2 + col);
          i = 0;
          alpha_px = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off+i*(LD->width-LD->xpos)].red);
          while ((alpha_px != 255) && (row+i<LD->height))
            i++;
          buf_off_height = ((row+i)*width/2 + col);

          buf_off = row*width/2+col;
          buf_off_ypos = LD->ypos/2*width/2+col;
          buf_off_height = LD->height/2*width/2+col;

          pkt_off = (row*2-LD->ypos) * (LD->width-LD->xpos) + (col*2-LD->xpos);

          alpha_px    = (uint8_t)ScaleQuantumToChar(LD->pixel_packet[pkt_off].red);
          /* sic: reuse the red component */
          hcalc  = alpha_blending(buffer[craddr + buf_off_xpos], buffer[craddr + buf_off_width],  alpha_hori);
          vcalc  = alpha_blending(buffer[craddr + buf_off_ypos], buffer[craddr + buf_off_height], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          buffer[craddr + buf_off] = alpha_blending(buffer[craddr + buf_off], new_px, alpha_px);

          hcalc  = alpha_blending(buffer[cbaddr + buf_off_xpos], buffer[cbaddr + buf_off_width],  alpha_hori);
          vcalc  = alpha_blending(buffer[cbaddr + buf_off_ypos], buffer[cbaddr + buf_off_height], alpha_vert);
          new_px = (hcalc*LD->xweight + vcalc*LD->yweight)/100;
          buffer[cbaddr + buf_off] = alpha_blending(buffer[cbaddr + buf_off], new_px, alpha_px);
        }
      }

      break;
  }

  if(LD->border) {
    for(row=LD->ypos; row<LD->height; ++row) {
      if((row == LD->ypos) || (row==LD->height-1)) {
        for(col=LD->xpos; col<LD->width; ++col) if(col&1) buffer[row*width+col] = 255 & 0xff;
      }
      if(row&1) buffer[row*width+LD->xpos]  = 255 & 0xff;
      if(row&1) buffer[row*width+LD->width] = 255 & 0xff;
    }
  }

}


/*-------------------------------------------------
 *
 * main
 *
 *-------------------------------------------------*/


int tc_filter(frame_list_t *ptr_, char *options)
{
  vframe_list_t *ptr = (vframe_list_t *)ptr_;
  int instance = ptr->filter_id;


  //----------------------------------
  //
  // filter get config
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_GET_CONFIG && options) {

    char buf[255];
    optstr_filter_desc (options, MOD_NAME, MOD_CAP, MOD_VERSION, MOD_AUTHOR, "VRYOM", "1");

    tc_snprintf (buf, sizeof(buf), "%u-%u", data[instance]->start, data[instance]->end);
    optstr_param (options, "range",  "Frame Range",                         "%d-%d",     buf, "0", "oo",    "0", "oo");

    tc_snprintf (buf, sizeof(buf), "%dx%d", data[instance]->xpos, data[instance]->ypos);
    optstr_param (options, "pos",    "Position of logo",                    "%dx%d",     buf, "0", "width", "0", "height");

    tc_snprintf (buf, sizeof(buf), "%dx%d", data[instance]->width, data[instance]->height);
    optstr_param (options, "size",   "Size of logo",                        "%dx%d",     buf, "0", "width", "0", "height");

    tc_snprintf (buf, sizeof(buf), "%d", data[instance]->mode);
    optstr_param (options, "mode",   "Filter Mode (0=none,1=solid,2=xy,3=shape)", "%d",  buf, "0", "3");

    tc_snprintf (buf, sizeof(buf), "%d",  data[instance]->border);
    optstr_param (options, "border", "Visible Border",                      "",          buf);

    tc_snprintf (buf, sizeof(buf), "%d",  data[instance]->dump);
    optstr_param (options, "dump", "Dump filterarea to file",               "",          buf);

    tc_snprintf (buf, sizeof(buf), "%d", data[instance]->xweight);
    optstr_param (options, "xweight","X-Y Weight(0%-100%)",                 "%d",        buf, "0", "100");

    tc_snprintf (buf, sizeof(buf), "%x%x%x", data[instance]->rcolor, data[instance]->gcolor, data[instance]->bcolor);
    optstr_param (options, "fill",   "Solid Fill Color(RGB)",               "%2x%2x%2x", buf, "00", "FF",   "00", "FF", "00", "FF");

    tc_snprintf (buf, sizeof(buf), "%s",  data[instance]->file);
    optstr_param (options, "file",   "Image with alpha/shape information",  "%s",        buf);

    return TC_OK;
  }


  //----------------------------------
  //
  // filter init
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_INIT) {

    if((vob = tc_get_vob())==NULL) return TC_ERROR;

    if((data[instance] = tc_malloc (sizeof(logoaway_data))) == NULL) {
      tc_log_error(MOD_NAME, "can't allocate filter data");
      return TC_ERROR;
    }

    data[instance]->start    = 0;
    data[instance]->end      = (unsigned int)-1;
    data[instance]->xpos     = -1;
    data[instance]->ypos     = -1;
    data[instance]->width    = -1;
    data[instance]->height   = -1;
    data[instance]->mode     = 0;
    data[instance]->border   = 0;
    data[instance]->xweight  = 50;
    data[instance]->yweight  = 50;
    data[instance]->rcolor   = 0;
    data[instance]->gcolor   = 0;
    data[instance]->bcolor   = 0;
    data[instance]->ycolor   = 16;
    data[instance]->ucolor   = 128;
    data[instance]->vcolor   = 128;
    data[instance]->alpha    = 0;
    data[instance]->dump     = 0;
    data[instance]->id       = instance;

    // filter init ok.

    if(verbose) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CAP);

    if(options!=NULL) {
      optstr_get     (options,  "range",   "%d-%d",     &data[instance]->start,  &data[instance]->end);
      optstr_get     (options,  "pos",     "%dx%d",     &data[instance]->xpos,   &data[instance]->ypos);
      optstr_get     (options,  "size",    "%dx%d",     &data[instance]->width,  &data[instance]->height);
        data[instance]->width += data[instance]->xpos; data[instance]->height += data[instance]->ypos;
      optstr_get     (options,  "mode",    "%d",        &data[instance]->mode);
      if (optstr_lookup (options,  "border") != NULL)
        data[instance]->border = 1;
      if (optstr_lookup (options,  "help") != NULL)
        help_optstr();
      optstr_get     (options,  "xweight", "%d",        &data[instance]->xweight);
        data[instance]->yweight = 100 - data[instance]->xweight;
      optstr_get     (options,  "fill",    "%2x%2x%2x", &data[instance]->rcolor, &data[instance]->gcolor, &data[instance]->bcolor);
        data[instance]->ycolor =  (0.257 * data[instance]->rcolor) + (0.504 * data[instance]->gcolor) + (0.098 * data[instance]->bcolor) + 16;
        data[instance]->ucolor =  (0.439 * data[instance]->rcolor) - (0.368 * data[instance]->gcolor) - (0.071 * data[instance]->bcolor) + 128;
        data[instance]->vcolor = -(0.148 * data[instance]->rcolor) - (0.291 * data[instance]->gcolor) + (0.439 * data[instance]->bcolor) + 128;
      if (optstr_get (options,  "file",    "%[^:]",     data[instance]->file) >= 0)
        data[instance]->alpha = 1;
      if (optstr_lookup (options,  "dump") != NULL)
        data[instance]->dump = 1;
    }

    if(verbose) tc_log_info(MOD_NAME, "instance(%d) options=%s", instance, options);
    if(verbose > 1) {
      tc_log_info (MOD_NAME, " LogoAway Filter Settings:");
      tc_log_info (MOD_NAME, "            pos = %dx%d", data[instance]->xpos, data[instance]->ypos);
      tc_log_info (MOD_NAME, "           size = %dx%d", data[instance]->width-data[instance]->xpos, data[instance]->height-data[instance]->ypos);
      tc_log_info (MOD_NAME, "           mode = %d(%s)", data[instance]->mode, modes[data[instance]->mode]);
      tc_log_info (MOD_NAME, "         border = %d", data[instance]->border);
      tc_log_info (MOD_NAME, "     x-y weight = %d:%d", data[instance]->xweight, data[instance]->yweight);
      tc_log_info (MOD_NAME, "     fill color = %2X%2X%2X", data[instance]->rcolor, data[instance]->gcolor, data[instance]->bcolor);
      if(data[instance]->alpha)
        tc_log_info (MOD_NAME, "           file = %s", data[instance]->file);
      if(data[instance]->dump)
        tc_log_info (MOD_NAME, "           dump = %d", data[instance]->dump);
    }

    if( (data[instance]->xpos > vob->im_v_width) || (data[instance]->ypos > vob->im_v_height) || (data[instance]->xpos < 0) || (data[instance]->ypos < 0) )  {
      tc_log_error(MOD_NAME, "invalid position");
      return TC_ERROR;
    }
    if( (data[instance]->width > vob->im_v_width) || (data[instance]->height > vob->im_v_height) || (data[instance]->width-data[instance]->xpos < 0) || (data[instance]->height-data[instance]->ypos < 0) ) {
      tc_log_error(MOD_NAME, "invalid size");
      return TC_ERROR;
    }
    if( (data[instance]->xweight > 100) || (data[instance]->xweight < 0) ) {
      tc_log_error(MOD_NAME, "invalid x weight");
      return TC_ERROR;
    }
    if( (data[instance]->mode < 0) || (data[instance]->mode > 3) ) {
      tc_log_error(MOD_NAME, "invalid mode");
      return TC_ERROR;
    }
    if( (data[instance]->mode == 3) && (data[instance]->alpha == 0) ) {
      tc_log_error(MOD_NAME, "alpha/shape file needed for SHAPE-mode");
      return TC_ERROR;
    }

    if((data[instance]->alpha) || (data[instance]->dump)) {
      InitializeMagick("");
      GetExceptionInfo(&data[instance]->exception_info);

      if(data[instance]->alpha) {
        data[instance]->image_info = CloneImageInfo((ImageInfo *) NULL);

        strlcpy(data[instance]->image_info->filename, data[instance]->file, MaxTextExtent);
        data[instance]->image = ReadImage(data[instance]->image_info, &data[instance]->exception_info);
        if (data[instance]->image == (Image *) NULL) {
          tc_log_error(MOD_NAME, "\n");
          MagickWarning (data[instance]->exception_info.severity, data[instance]->exception_info.reason, data[instance]->exception_info.description);
          return TC_ERROR;
        }

        if ((data[instance]->image->columns != (data[instance]->width-data[instance]->xpos)) || (data[instance]->image->rows != (data[instance]->height-data[instance]->ypos))) {
          tc_log_error(MOD_NAME, "\"%s\" has incorrect size", data[instance]->file);

          return TC_ERROR;
        }

        data[instance]->pixel_packet = GetImagePixels(data[instance]->image, 0, 0, data[instance]->image->columns, data[instance]->image->rows);
      }
      if(data[instance]->dump) {
        if((data[instance]->dump_buf = tc_malloc ((data[instance]->width-data[instance]->xpos)*(data[instance]->height-data[instance]->ypos)*3)) == NULL)
          tc_log_error(MOD_NAME, "out of memory");

        data[instance]->dumpimage_info = CloneImageInfo((ImageInfo *) NULL);
      }
    }

    return TC_OK;
  }


  //----------------------------------
  //
  // filter close
  //
  //----------------------------------


  if(ptr->tag & TC_FILTER_CLOSE) {

    if (data[instance]->image != (Image *)NULL) {
      DestroyImage(data[instance]->image);
      DestroyImageInfo(data[instance]->image_info);
    }
    if (data[instance]->dumpimage != (Image *)NULL) {
      DestroyImage(data[instance]->dumpimage);
      DestroyImageInfo(data[instance]->dumpimage_info);
      ConstituteComponentTerminus();
    }
    DestroyExceptionInfo(&data[instance]->exception_info);
    DestroyMagick();

    if(data[instance]->dump_buf) free(data[instance]->dump_buf);
    if(data[instance]) free(data[instance]);
    data[instance] = NULL;

    return TC_OK;
  }


  //----------------------------------
  //
  // filter frame routine
  //
  //----------------------------------


  if(ptr->tag & TC_PRE_M_PROCESS && ptr->tag & TC_VIDEO && !(ptr->attributes & TC_FRAME_IS_SKIPPED)) {

    if (ptr->id >= data[instance]->start && ptr->id <= data[instance]->end) {
      if(vob->im_v_codec==CODEC_RGB) {
        work_with_rgb_frame(data[instance], ptr->video_buf, vob->im_v_width, vob->im_v_height);
      } else {
        work_with_yuv_frame(data[instance], ptr->video_buf, vob->im_v_width, vob->im_v_height);
      }
    }
  }
  return TC_OK;
}

