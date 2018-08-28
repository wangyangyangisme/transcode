/*
 *  export_null.c
 *
 *  Copyright (C) Thomas Oestreich - June 2001
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

#define MOD_NAME    "export_null.so"
#define MOD_VERSION "v0.1.2 (2001-08-17)"
#define MOD_CODEC   "(video) null | (audio) null"

#include "transcode.h"

#include <stdio.h>
#include <stdlib.h>

static int verbose_flag=TC_QUIET;
static int capability_flag=-1; //all codecs welcome

#define MOD_PRE null
#include "export_def.h"

/* ------------------------------------------------------------
 *
 * open codec
 *
 * ------------------------------------------------------------*/

MOD_open
{
  return(0);
}

/* ------------------------------------------------------------
 *
 * init codec
 *
 * ------------------------------------------------------------*/

MOD_init
{
  return(0);
}


/* ------------------------------------------------------------
 *
 * encode and export
 *
 * ------------------------------------------------------------*/

MOD_encode
{
  return(0);
}


/* ------------------------------------------------------------
 *
 * stop codec
 *
 * ------------------------------------------------------------*/

MOD_stop
{
  return(0);
}


/* ------------------------------------------------------------
 *
 * close codec
 *
 * ------------------------------------------------------------*/

MOD_close
{
  return(0);
}


