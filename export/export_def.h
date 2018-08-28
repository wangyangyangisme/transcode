/*
 *  export_def.h
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

#ifndef _EXPORT_DEF_H
#define _EXPORT_DEF_H


#ifndef MOD_PRE
#error MOD_PRE not defined!
#endif

#define r2(i, a, b) i ## a ## b
#define r1(a, b) r2(export_, a, b)
#define RENAME(a, b) r1(a, b)

#define MOD_name   static int RENAME(MOD_PRE, _name) (transfer_t *param)
#define MOD_open   static int RENAME(MOD_PRE, _open) (transfer_t *param, vob_t *vob)
#define MOD_init   static int RENAME(MOD_PRE, _init) (transfer_t *param, vob_t *vob)
#define MOD_encode static int RENAME(MOD_PRE, _encode) (transfer_t *param)
#define MOD_stop   static int RENAME(MOD_PRE, _stop) (transfer_t *param)
#define MOD_close  static int RENAME(MOD_PRE, _close) (transfer_t *param)


//extern int verbose_flag;
//extern int capability_flag;

/* ------------------------------------------------------------
 *
 * codec id string
 *
 * ------------------------------------------------------------*/

MOD_name
{
    static int display=0;

    verbose_flag = param->flag;

    // print module version only once
    if(verbose_flag && (display++)==0) tc_log_info(MOD_NAME, "%s %s", MOD_VERSION, MOD_CODEC);

    // return module capability flag
    param->flag = capability_flag;

    return(0);
}


MOD_open;
MOD_init;
MOD_encode;
MOD_stop;
MOD_close;

/* ------------------------------------------------------------
 *
 * interface
 *
 * ------------------------------------------------------------*/

int tc_export(int opt, void *para1, void *para2)
{

  switch(opt)
  {

  case TC_EXPORT_NAME:

      return(RENAME(MOD_PRE, _name)((transfer_t *) para1));

  case TC_EXPORT_OPEN:

      return(RENAME(MOD_PRE, _open)((transfer_t *) para1, (vob_t *) para2));

  case TC_EXPORT_INIT:

      return(RENAME(MOD_PRE, _init)((transfer_t *) para1, (vob_t *) para2));

  case TC_EXPORT_ENCODE:

      return(RENAME(MOD_PRE, _encode)((transfer_t *) para1));

  case TC_EXPORT_STOP:

      return(RENAME(MOD_PRE, _stop)((transfer_t *) para1));

  case TC_EXPORT_CLOSE:

      return(RENAME(MOD_PRE, _close)((transfer_t *) para1));

  default:
      return(TC_EXPORT_UNKNOWN);
  }
}

#endif
