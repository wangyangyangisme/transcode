/*
 *  tccat.c
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

#include "transcode.h"
#include "tcinfo.h"
#include "libtc/libtc.h"
#include "libtc/iodir.h"
#include "libtc/xio.h"
#include "ioaux.h"
#include "tc.h"
#include "dvd_reader.h"

#include <sys/types.h>

#ifdef HAVE_LIBDVDREAD
#ifdef HAVE_LIBDVDREAD_INC
#include <dvdread/dvd_reader.h>
#else
#include <dvd_reader.h>
#endif
#else
#include "dvd_reader.h"
#endif

#define EXE "tccat"

//static char buf[TC_BUF_MAX];

int verbose = TC_INFO;

void import_exit(int code)
{
  if (verbose & TC_DEBUG) {
    tc_log_msg(EXE, "(pid=%d) exit (code %d)", (int) getpid(), code);
  }
  exit(code);
}


/* ------------------------------------------------------------
 *
 * source extract thread
 *
 * ------------------------------------------------------------*/

#define IO_BUF_SIZE 1024
#define DVD_VIDEO_LB_LEN 2048

static void tccat_thread(info_t *ipipe)
{
    const char *name = NULL;
    int found = 0, itype = TC_MAGIC_UNKNOWN, type = TC_MAGIC_UNKNOWN;
    int verbose_flag = ipipe->verbose;
    int vob_offset = ipipe->vob_offset;
    info_t ipipe_avi;
    TCDirList tcdir;

    switch(ipipe->magic) {
      case TC_MAGIC_DVD_PAL: /* fallthrough */
      case TC_MAGIC_DVD_NTSC:
        if (verbose_flag & TC_DEBUG) {
            tc_log_msg(__FILE__, "%s", filetype(ipipe->magic));
        }
        dvd_read(ipipe->dvd_title, ipipe->dvd_chapter, ipipe->dvd_angle);
        break;

      case TC_MAGIC_TS:
        ts_read(ipipe->fd_in, ipipe->fd_out, ipipe->ts_pid);
        break;

      case TC_MAGIC_RAW:
        if (verbose_flag & TC_DEBUG) {
            tc_log_msg(__FILE__, "%s", filetype(ipipe->magic));
        }
        if(vob_offset > 0) {
            /* get filesize in units of packs (2kB) */
            off_t off = lseek(ipipe->fd_in,
                              vob_offset * (off_t) DVD_VIDEO_LB_LEN,
                              SEEK_SET);

            if (off != (vob_offset * (off_t)DVD_VIDEO_LB_LEN)) {
                tc_log_warn(__FILE__, "unable to seek to block %d",
                            vob_offset); /* drop this chunk/file */
                goto vob_skip2;
            }
        }
        tc_preadwrite(ipipe->fd_in, ipipe->fd_out);
vob_skip2:
        break;

      case TC_MAGIC_DIR:
        /* PASS 1: check file type - file order not important */
        if (tc_dirlist_open(&tcdir, ipipe->name, 0) < 0) {
            tc_log_error(__FILE__, "unable to open directory \"%s\"",
                         ipipe->name);
            exit(1);
        } else if (verbose_flag & TC_DEBUG) {
            tc_log_msg(__FILE__, "scanning directory \"%s\"", ipipe->name);
        }

        while ((name = tc_dirlist_scan(&tcdir)) != NULL) {
            if ((ipipe->fd_in = open(name, O_RDONLY)) < 0) {
                tc_log_perror(__FILE__, "file open");
                exit(1);
            }
            /*
             * first valid magic must be the same for all
             * files to follow
             */
            itype = fileinfo(ipipe->fd_in, 0);
            close(ipipe->fd_in);

            if (itype == TC_MAGIC_UNKNOWN || itype == TC_MAGIC_PIPE
             || itype == TC_MAGIC_ERROR) {
                tc_log_error(__FILE__, "this version of transcode"
                                       " supports only");
                tc_log_error(__FILE__, "directories containing files of"
                                       " identical file type.");
                tc_log_error(__FILE__, "Please clean up directory %s and"
                                       " restart.", ipipe->name);

                tc_log_error(__FILE__, "file %s with filetype %s is invalid"
                                       " for directory mode.",
                             name, filetype(itype));

                exit(1);
            } /* bad magic */

            switch(itype) { /* supported file types, global fallthrough */
              case TC_MAGIC_VOB:
              case TC_MAGIC_DV_PAL:
              case TC_MAGIC_DV_NTSC:
              case TC_MAGIC_AC3:
              case TC_MAGIC_YUV4MPEG:
              case TC_MAGIC_AVI:
              case TC_MAGIC_MPEG:
                if (!found) { /* very first time only */
                    type = itype;
                }
                if (itype!=type) {
                    tc_log_error(__FILE__,"multiple filetypes not valid for"
                                          " directory mode.");
                    exit(1);
                }
                found = 1;
                break;
              default:
                tc_log_error(__FILE__, "invalid filetype %s for directory"
                                       " mode.", filetype(type));
                exit(1);
            } /* check itype */
        } /* process files */
        tc_dirlist_close(&tcdir);

        if (!found) {
            tc_log_error(__FILE__, "no valid files found in %s", name);
            exit(1);
        } else if (verbose_flag & TC_DEBUG) {
            tc_log_msg(__FILE__, "%s", filetype(type));
        }

        /* PASS 2: dump files in correct order */
        if (tc_dirlist_open(&tcdir, ipipe->name, 1) < 0) {
            tc_log_error(__FILE__, "unable to sort directory entries\"%s\"",
                         name);
            exit(1);
        }

        while ((name = tc_dirlist_scan(&tcdir)) != NULL) {
            if ((ipipe->fd_in = open(name, O_RDONLY)) < 0) {
                tc_log_perror(__FILE__, "file open");
                exit(1);
            } else if(verbose_flag & TC_STATS) {
                tc_log_msg(__FILE__, "processing %s", name);
            }

            /* type determined in pass 1 */
            switch (type) {
              case TC_MAGIC_VOB:
                if (vob_offset > 0) {
	                /* get filesize in units of packs (2kB) */
                    off_t off;
                    off_t size = lseek(ipipe->fd_in, 0, SEEK_END);
                    lseek(ipipe->fd_in, 0, SEEK_SET);

                    if (size > vob_offset * (off_t)DVD_VIDEO_LB_LEN) {
                        /* offset within current file */
                        off = lseek(ipipe->fd_in,
                                    vob_offset * (off_t)DVD_VIDEO_LB_LEN,
                                    SEEK_SET);
                        vob_offset = 0;
                    } else {
                        vob_offset -= size/DVD_VIDEO_LB_LEN;
                        goto vob_skip;
                    }
                }
                tc_preadwrite(ipipe->fd_in, ipipe->fd_out);
vob_skip:
                break;
              /*
               * all following (fallthrough stream typeas are all handlead in
               * the same way since they can be viewd as concatenation of
               * frames (no header or other things
               */
              case TC_MAGIC_DV_PAL: /* fallthrough */
              case TC_MAGIC_DV_NTSC: /* fallthrough */
              case TC_MAGIC_AC3: /* fallthrough */
              case TC_MAGIC_YUV4MPEG: /* fallthrough */
              case TC_MAGIC_MPEG: /* fallthrough */
                tc_preadwrite(ipipe->fd_in, ipipe->fd_out);
                break;

              case TC_MAGIC_AVI:
                /* extract and concatenate streams. avilib must do it. */
                ac_memcpy(&ipipe_avi, ipipe, sizeof(info_t));
	            ipipe_avi.name = (char *)name; /* real AVI file name */
                ipipe_avi.magic = TC_MAGIC_AVI;
                extract_avi(&ipipe_avi);
                break;

              default:
                tc_log_error(__FILE__, "invalid filetype %s for directory"
                                       " mode.", filetype(type));
                exit(1);
            }
            close(ipipe->fd_in);
        } /* process files */
        tc_dirlist_close(&tcdir);
        break;
    }
}


void version(void)
{
    /* XXX why not plain old printf? */
    tc_log_msg(EXE, "(%s v%s) (C) 2001-2003 Thomas Oestreich,"
                    " 2003-2010 Transcode Team",
               PACKAGE, VERSION);
}


static void usage(int status)
{
    /* XXX why not plain old printf? */
    version();

    fprintf(stderr,"\nUsage: %s [options]\n", EXE);
    fprintf(stderr,"    -i name          input file/directory%s name\n",
#ifdef HAVE_LIBDVDREAD
                   "/device/mountpoint"
#else
                   ""
#endif
           );
    fprintf(stderr,"    -t magic         file type [autodetect]\n");
#ifdef HAVE_LIBDVDREAD
    fprintf(stderr,"    -T t[,c[-d][,a]] DVD title[,chapter(s)[,angle]]"
                   " [1,1,1]\n");
    fprintf(stderr,"    -L               process all following chapters"
                   " [off]\n");
#endif
    fprintf(stderr,"    -S n             seek to VOB stream offset nx2kB"
                   " [0]\n");
    fprintf(stderr,"    -P               stream DVD ( needs -T )\n");
    fprintf(stderr,"    -a               dump AVI-file/socket audio"
                   " stream\n");
    fprintf(stderr,"    -n id            transport stream id [0x10]\n");
    fprintf(stderr,"    -d mode          verbosity mode\n");
    fprintf(stderr,"    -v               print version\n");

    exit(status);
}

typedef enum {
    TCCAT_SOURCE_STDIN = 1,
    TCCAT_SOURCE_FILE,
    TCCAT_SOURCE_DVD,
    TCCAT_SOURCE_DIR,
    TCCAT_SOURCE_TS
} TCCatSources;

/* ------------------------------------------------------------
 * universal extract frontend
 * ------------------------------------------------------------*/

#define VALIDATE_OPTION \
    if (optarg[0] == '-') usage(EXIT_FAILURE)

int main(int argc, char *argv[])
{
    struct stat fbuf;
    info_t ipipe;
    int end_chapter, start_chapter;
    int title = 1, chapter1 = 1, chapter2 = -1, angle = 1;
    int max_chapters, max_angles, max_titles;
    int n = 0, j, stream = 0, audio = 0, user = 0, source = 0;

    int vob_offset = 0;
    int ch, ts_pid = 0x10;
    char *magic="", *name=NULL;

    /* proper initialization */
    memset(&ipipe, 0, sizeof(info_t));

    libtc_init(&argc, &argv);

    while ((ch = getopt(argc, argv, "S:T:d:i:vt:LaP?hn:")) != -1) {
        switch (ch) {
          case 'i':
            VALIDATE_OPTION;
            name = optarg;
            break;

          case 'T':
            VALIDATE_OPTION;
            n = sscanf(optarg,"%d,%d-%d,%d", &title, &chapter1, &chapter2, &angle);
            if (n != 4) {
                n = sscanf(optarg,"%d,%d-%d", &title, &chapter1, &chapter2);
                if (n != 3) {
                    n = sscanf(optarg,"%d,%d,%d", &title, &chapter1, &angle);
                    /* only do one chapter ! */
                    chapter2 = chapter1;

                    if (n < 0 || n > 3) {
                        tc_log_error(EXE, "invalid parameter for option -T");
                        exit(1);
                    }
                }
            }
            source = TCCAT_SOURCE_DVD;

            if (chapter2 != -1) {
                if (chapter2 < chapter1) {
                    tc_log_error(EXE, "invalid parameter for option -T");
                    exit(1);
                }
            }
            break;

          case 'P':
            stream = 1;
            break;

          case 'a':
            audio = 1;
            break;

          case 'd':
            VALIDATE_OPTION;
            verbose = atoi(optarg);
            break;

          case 'n':
            VALIDATE_OPTION;
            ts_pid = strtol(optarg, NULL, 16);
            source = TCCAT_SOURCE_TS;
            break;

          case 'S':
            VALIDATE_OPTION;
            vob_offset = atoi(optarg);
            break;

          case 't':
            VALIDATE_OPTION;
            magic = optarg;
            user = 1;

            if (strcmp(magic, "dvd") == 0) {
                source = TCCAT_SOURCE_DVD;
            }
            break;

          case 'v':
            version();
            exit(0);
            break;

          case 'h':
            usage(EXIT_SUCCESS);
          default:
            usage(EXIT_FAILURE);
        }
    }

    /* DVD debugging information */
    if ((verbose & TC_DEBUG) && source == TCCAT_SOURCE_DVD) {
        tc_log_msg(EXE, "T=%d %d %d %d %d", n, title, chapter1,
                   chapter2, angle);
    }

    /* ------------------------------------------------------------
     * fill out defaults for info structure
     * ------------------------------------------------------------*/

    /* no autodetection yet */
    if (argc == 1) {
        usage(EXIT_FAILURE);
    }

    /* assume defaults */
    if (name == NULL) {
        source = TCCAT_SOURCE_STDIN;
        ipipe.fd_in = STDIN_FILENO;
    }

    /* no stdin for DVD */
    if (name == NULL && source == TCCAT_SOURCE_DVD) {
        tc_log_error(EXE, "invalid directory/path_to_device");
        usage(EXIT_FAILURE);
    }

    /* do not try to mess with the stdin stream */
    if ((source != TCCAT_SOURCE_DVD) && (source != TCCAT_SOURCE_TS)
     && (source != TCCAT_SOURCE_STDIN)) {
        /* file or directory? */
        if (stat(name, &fbuf)) {
            tc_log_error(EXE, "invalid file \"%s\"", name);
            exit(1);
        }

        source = (S_ISDIR(fbuf.st_mode))
                    ?TCCAT_SOURCE_DIR :TCCAT_SOURCE_FILE;
    }

    /* fill out defaults for info structure */
    ipipe.fd_out = STDOUT_FILENO;
    ipipe.verbose = verbose;
    ipipe.dvd_title = title;
    ipipe.dvd_chapter = chapter1;
    ipipe.dvd_angle = angle;
    ipipe.ts_pid = ts_pid;
    ipipe.vob_offset = vob_offset;

    if (name) {
        ipipe.name = tc_strdup(name);
        if (ipipe.name == NULL) {
            tc_log_error(EXE, "could not allocate memory");
            exit(1);
        }
    } else {
        ipipe.name = NULL;
    }

    ipipe.select = audio;

    /* ------------------------------------------------------------
     * source specific section
     * ------------------------------------------------------------*/

    switch(source) {
      case TCCAT_SOURCE_TS:
        ipipe.fd_in = xio_open(name, O_RDONLY);
        if (ipipe.fd_in < 0) {
            tc_log_perror(EXE, "file open");
            exit(1);
        }
        ipipe.magic = TC_MAGIC_TS;
        tccat_thread(&ipipe);
        xio_close(ipipe.fd_in);
        break;

      case TCCAT_SOURCE_DVD:
        if (dvd_init(name, &max_titles, verbose) < 0) {
            tc_log_error(EXE, "(pid=%d) failed to open DVD %s", getpid(), name);
            exit(1);
        }
        ipipe.magic = TC_MAGIC_DVD_PAL; /* FIXME */
        dvd_query(title, &max_chapters, &max_angles);
        /* set chapternumbers now we know how much there are */
        start_chapter = (chapter1!=-1 && chapter1 <=max_chapters) ? chapter1:1;
        end_chapter = (chapter2!=-1 && chapter2 <=max_chapters) ? chapter2:max_chapters;

        for (j = start_chapter; j < end_chapter+1; j++) {
            ipipe.dvd_chapter = j;
            if (verbose & TC_DEBUG) {
                tc_log_msg(EXE, "(pid=%d) processing chapter (%d/%d)",
                           getpid(), j, max_chapters);
            }
            if (stream) {
                dvd_stream(title, j);
            } else {
                tccat_thread(&ipipe);
            }
        }
        dvd_close();
        break;

      case TCCAT_SOURCE_FILE:
        ipipe.fd_in = open(name, O_RDONLY);
        if (ipipe.fd_in < 0) {
            tc_log_perror(EXE, "file open");
            exit(1);
        }

      case TCCAT_SOURCE_STDIN:
        ipipe.magic = TC_MAGIC_RAW;
        tccat_thread(&ipipe);
        if (ipipe.fd_in != STDIN_FILENO) {
            close(ipipe.fd_in);
        }
        break;

      case TCCAT_SOURCE_DIR:
        ipipe.magic = TC_MAGIC_DIR;
        tccat_thread(&ipipe);
        break;
    }
    return 0;
}

#include "libtc/static_xio.h"
