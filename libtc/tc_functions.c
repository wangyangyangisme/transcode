/*
 *  tc_functions.c - various common functions for transcode
 *  Written by Thomas Oestreich, Francesco Romani, Andrew Church, and others
 *
 *  This file is part of transcode, a video stream processing tool.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>

#include "xio.h"
#include "libtc.h"
#include "tc_defaults.h"

#include "transcode.h"

#include "ratiocodes.h"

/*************************************************************************/
#ifdef HAVE_FFMPEG

pthread_mutex_t tc_libavcodec_mutex = PTHREAD_MUTEX_INITIALIZER;

#endif
/*************************************************************************/

#define TC_MSG_BUF_SIZE     (TC_BUF_MIN * 2)

/* WARNING: we MUST keep in sync preambles order with TC_LOG* macros */
static const char *tc_log_preambles[] = {
    /* TC_LOG_ERR */
    "["COL_RED"%s"COL_GRAY"]"COL_RED" critical"COL_GRAY": %s\n",
    /* TC_LOG_WARN */
    "["COL_RED"%s"COL_GRAY"]"COL_YELLOW" warning"COL_GRAY": %s\n",
    /* TC_LOG_INFO */
    "["COL_BLUE"%s"COL_GRAY"] %s\n",
    /* TC_LOG_MSG */
    "[%s] %s\n",
    /* TC_LOG_EXTRA */
    "%s%s" /* tag placeholder must be present but tag will be ignored */
};

static int tc_log_use_colors = TC_TRUE;
/* 
 * I'm not so proud of doing so. If someone has a better (cleaner)
 * solution, just speak up. -- fromani 20060919
 */


void libtc_init(int *argc, char ***argv)
{
    int ret = tc_mangle_cmdline(argc, argv, TC_LOG_COLOR_OPTION, NULL);

    if (ret == 0) {
        tc_log_use_colors = TC_FALSE;
    } else {
        const char *envvar = getenv(TC_LOG_COLOR_ENV_VAR);
        if (envvar != NULL) {
                tc_log_use_colors = TC_FALSE;
        }
    }
}



int tc_log(TCLogLevel level, const char *tag, const char *fmt, ...)
{
    char buf[TC_MSG_BUF_SIZE];
    char *msg = buf;
    size_t size = sizeof(buf);
    int dynbuf = TC_FALSE, truncated = TC_FALSE;
    /* flag: we must use a dynamic (larger than static) buffer? */
    va_list ap;

    /* sanity check, avoid {under,over}flow; */
    level = (level < TC_LOG_ERR) ?TC_LOG_ERR :level;
    level = (level > TC_LOG_EXTRA) ?TC_LOG_EXTRA :level;
    /* sanity check, avoid dealing with NULL as much as we can */
    if (!tc_log_use_colors && level != TC_LOG_EXTRA) {
        /* TC_LOG_EXTRA and TC_LOG_MSG must not use colors */
        level = TC_LOG_MSG;
    }

    tag = (tag != NULL) ?tag :"";
    fmt = (fmt != NULL) ?fmt :"";
    /* TC_LOG_EXTRA special handling: force always empty tag */
    tag = (level == TC_LOG_EXTRA) ?"" :tag; 
    
    size = strlen(tc_log_preambles[level])
           + strlen(tag) + strlen(fmt) + 1;

    if (size > sizeof(buf)) {
        /* 
         * we use malloc/fprintf instead of tc_malloc because
         * we want custom error messages
         */
        msg = malloc(size);
        if (msg != NULL) {
            dynbuf = TC_TRUE;
        } else {
            fprintf(stderr, "(%s) CRITICAL: can't get memory in "
                    "tc_log() output will be truncated.\n",
                    __FILE__);
            /* force reset to default values */
            msg = buf;
            size = sizeof(buf) - 1;
            truncated = TC_TRUE;
        }
    } else {
        size = sizeof(buf) - 1;
    }

    /* construct real format string */
    tc_snprintf(msg, size, tc_log_preambles[level], tag, fmt);
//    msg[size] = '\0';
//    /* `size' value was already scaled for the final '\0' */
//    trusting valgrind, this assignement don't help us and can
//    lead to troubles. So it's temporary disable until I gain
//    some time to experiment -- fromani 20060919

    va_start(ap, fmt);
    vfprintf(stderr, msg, ap);
    va_end(ap);

    if (dynbuf == 1) {
        free(msg);
    }

    /* ensure that all *other* messages are written */
    fflush(stderr);
    return (truncated) ?-1 :0;
}

/*************************************************************************/

int tc_mangle_cmdline(int *argc, char ***argv,
                      const char *opt, const char **optval)
{
    int i = 0, skew = (optval == NULL) ?1 :2, err = -1;

    if (argc == NULL || argv == NULL || opt == NULL) {
        return err;
    }

    err = 1;
    /* first we looking for our option (and it's value) */
    for (i = 1; i < *argc; i++) {
        if ((*argv)[i] && strcmp((*argv)[i], opt) == 0) {
            if (optval == NULL) {
                err = 0; /* we're set */
            } else {
                /* don't peek after the end... */
                if (i + 1 >= *argc || (*argv)[i + 1][0] == '-') {
                    tc_log_warn(__FILE__, "wrong usage for option '%s'", opt);
                    err = 1; /* no option and/or value found */
                } else {
                    *optval = (*argv)[i + 1];
                    err = 0;
                }
            }
            break;
        }
    }

    /*
     * if we've found our option, now we must shift back all
     * the other options after the ours and we must also update argc.
     */
    if (!err) {
        for (; i < (*argc - skew); i++) {
            (*argv)[i] = (*argv)[i + skew];
        }
        (*argc) -= skew;
    }

    return err;
}


int tc_test_program(const char *name)
{
#ifndef NON_POSIX_PATH
    const char *path = getenv("PATH");
    char *tok_path = NULL;
    char *compl_path = NULL;
    char *tmp_path;
    char **strtokbuf;
    char done;
    size_t pathlen;
    long sret;
    int error = 0;

    if (name == NULL) {
        tc_warn("ERROR: Searching for a NULL program!\n");
        return ENOENT;
    }

    if (path == NULL) {
        tc_warn("The '%s' program could not be found. \n", name);
        tc_warn("Because your PATH environment variable is not set.\n");
        return ENOENT;
    }

    pathlen = strlen(path) + 1;
    tmp_path = tc_malloc(pathlen);
    strtokbuf = tc_malloc(pathlen);

    sret = strlcpy(tmp_path, path, pathlen);
    tc_test_string(__FILE__, __LINE__, pathlen, sret, errno);

    /* iterate through PATH tokens */
    for (done = 0, tok_path = strtok_r(tmp_path, ":", strtokbuf);
            !done && tok_path;
            tok_path = strtok_r((char *)0, ":", strtokbuf)) {
        pathlen = strlen(tok_path) + strlen(name) + 2;
        compl_path = tc_malloc(pathlen * sizeof(char));
        sret = tc_snprintf(compl_path, pathlen, "%s/%s", tok_path, name);

        if (access(compl_path, X_OK) == 0) {
            error   = 0;
            done    = 1;
        } else { /* access != 0 */
            if (errno != ENOENT) {
                done    = 1;
                error   = errno;
            }
        }

        tc_free(compl_path);
    }

    tc_free(tmp_path);
    tc_free(strtokbuf);

    if (!done) {
        tc_warn("The '%s' program could not be found. \n", name);
        tc_warn("Please check your installation.\n");
        return ENOENT;
    }

    if (error != 0) {
        /* access returned an unhandled error */
        tc_warn("The '%s' program was found, but is not accessible.\n", name);
        tc_warn("%s\n", strerror(errno));
        tc_warn("Please check your installation.\n");
        return error;
    }
#endif

    return 0;
}


int tc_test_string(const char *file, int line, int limit, long ret, int errnum)
{
    if (ret < 0) {
        fprintf(stderr, "[%s:%d] string error: %s\n",
                        file, line, strerror(errnum));
        return 1;
    }
    if (ret >= limit) {
        fprintf(stderr, "[%s:%d] truncated %ld characters\n",
                        file, line, (ret - limit) + 1);
        return 1;
    }
    return 0;
}

/*************************************************************************/

/*
 * These versions of [v]snprintf() return -1 if the string was truncated,
 * printing a message to stderr in case of truncation (or other error).
 */

int _tc_vsnprintf(const char *file, int line, char *buf, size_t limit,
                  const char *format, va_list args)
{
    int res = vsnprintf(buf, limit, format, args);
    return tc_test_string(file, line, limit, res, errno) ? -1 : res;
}


int _tc_snprintf(const char *file, int line, char *buf, size_t limit,
                 const char *format, ...)
{
    va_list args;
    int res;

    va_start(args, format);
    res = _tc_vsnprintf(file, line, buf, limit, format, args);
    va_end(args);
    return res;
}

/*************************************************************************/

/* simple malloc wrapper with failure guard. */

void *_tc_malloc(const char *file, int line, size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        fprintf(stderr, "[%s:%d] tc_malloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    }
    return p;
}

/* allocate a chunk of memory (like tc_malloc), but zeroes memory before
 * returning. */

void *_tc_zalloc(const char *file, int line, size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        fprintf(stderr, "[%s:%d] tc_zalloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    } else {
        memset(p, 0, size);
    }
    return p;
}

/* realloc() wrapper. */

void *_tc_realloc(const char *file, int line, void *p, size_t size)
{
    p = realloc(p, size);
    if (p == NULL && size > 0) {
        fprintf(stderr, "[%s:%d] tc_realloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    }
    return p;
}


/*** FIXME ***: find a clean way to refactorize above functions */

/* Allocate a buffer aligned to the machine's page size, if known.  The
 * buffer must be freed with buffree() (not free()). */

void *_tc_bufalloc(const char *file, int line, size_t size)
{
#ifdef HAVE_GETPAGESIZE
    unsigned long pagesize = getpagesize();
    int8_t *base = malloc(size + sizeof(void *) + pagesize);
    int8_t *ptr = NULL;
    unsigned long offset = 0;

    if (base == NULL) {
        fprintf(stderr, "[%s:%d] tc_bufalloc(): can't allocate %lu bytes\n",
                        file, line, (unsigned long)size);
    } else {
        ptr = base + sizeof(void *);
        offset = (unsigned long)ptr % pagesize;

        if (offset)
            ptr += (pagesize - offset);
        ((void **)ptr)[-1] = base;  /* save the base pointer for freeing */
    }
    return ptr;
#else  /* !HAVE_GETPAGESIZE */
    return malloc(size);
#endif
}

char *_tc_strndup(const char *file, int line, const char *s, size_t n)
{
    char *pc = NULL;

    if (s != NULL) {
        pc = _tc_malloc(file, line, n + 1);
        if (pc != NULL) {
            memcpy(pc, s, n);
            pc[n] = '\0';
        }
    }
    return pc;
}

/* Free a buffer allocated with tc_bufalloc(). */
void tc_buffree(void *ptr)
{
#ifdef HAVE_GETPAGESIZE
    if (ptr)
	free(((void **)ptr)[-1]);
#else
    free(ptr);
#endif
}

/*************************************************************************/

ssize_t tc_pread(int fd, uint8_t *buf, size_t len)
{
    ssize_t n = 0;
    ssize_t r = 0;

    while (r < len) {
        n = xio_read(fd, buf + r, len - r);

        if (n == 0) {  /* EOF */
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        r += n;
    }
    return r;
}


ssize_t tc_pwrite(int fd, const uint8_t *buf, size_t len)
{
    ssize_t n = 0;
    ssize_t r = 0;

    while (r < len) {
        n = xio_write(fd, buf + r, len - r);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        r += n;
    }
    return r;
}

#ifdef PIPE_BUF
# define BLOCKSIZE PIPE_BUF /* 4096 on linux-x86 */
#else
# define BLOCKSIZE 4096
#endif

int tc_preadwrite(int fd_in, int fd_out)
{
    uint8_t buffer[BLOCKSIZE];
    ssize_t bytes;
    int error = 0;

    do {
        bytes = tc_pread(fd_in, buffer, BLOCKSIZE);

        /* error on read? */
        if (bytes < 0) {
            return -1;
        }

        /* read stream end? */
        if (bytes != BLOCKSIZE) {
            error = 1;
        }

        if (bytes) {
            /* write stream problems? */
            if (tc_pwrite(fd_out, buffer, bytes) != bytes) {
                error = 1;
            }
        }
    } while (!error);

    return 0;
}

int tc_file_check(const char *name)
{
    struct stat fbuf;

    if(xio_stat(name, &fbuf)) {
        tc_log_warn(__FILE__, "invalid file \"%s\"", name);
        return -1;
    }

    /* file or directory? */
    if(S_ISDIR(fbuf.st_mode)) {
        return 1;
    }
    return 0;
}

#ifndef major
# define major(dev)  (((dev) >> 8) & 0xff)
#endif

int tc_probe_path(const char *name)
{
    struct stat fbuf;

    if(name == NULL) {
        tc_log_warn(__FILE__, "invalid file \"%s\"", name);
        return TC_PROBE_PATH_INVALID;
    }

    if(xio_stat(name, &fbuf) == 0) {
        /* inode exists */

        /* treat DVD device as absolute directory path */
        if (S_ISBLK(fbuf.st_mode)) {
            return TC_PROBE_PATH_ABSPATH;
        }

        /* char device could be several things, depending on system */
        /* *BSD DVD device? v4l? bktr? sunau? */
        if(S_ISCHR(fbuf.st_mode)) {
            switch (major(fbuf.st_rdev)) {
#ifdef OS_BSD
# ifdef __OpenBSD__
                case 15: /* rcd */
                    return TC_PROBE_PATH_ABSPATH;
                case 42: /* sunau */
                    return TC_PROBE_PATH_SUNAU;
                case 49: /* bktr */
                    return TC_PROBE_PATH_BKTR;
# endif
# ifdef __FreeBSD__
                case 4: /* acd */
                    return TC_PROBE_PATH_ABSPATH;
                case 229: /* bktr */
                    return TC_PROBE_PATH_BKTR;
                case 0: /* OSS */
                    return TC_PROBE_PATH_OSS;
# endif
                default: /* libdvdread uses "raw" disk devices here */
                    return TC_PROBE_PATH_ABSPATH;
#else
                case 81: /* v4l (Linux) */
                    return TC_PROBE_PATH_V4L_VIDEO;
                case 14: /* OSS */
                    return TC_PROBE_PATH_OSS;
                default:
                    break;
#endif
            }
        }

        /* file or directory? */
        if (!S_ISDIR(fbuf.st_mode)) {
            return TC_PROBE_PATH_FILE;
        }

        /* directory, check for absolute path */
        if(name[0] == '/') {
            return TC_PROBE_PATH_ABSPATH;
        }

        /* directory mode */
        return TC_PROBE_PATH_RELDIR;
    } else {
        tc_log_warn(__FILE__, "invalid filename \"%s\"", name);
        return TC_PROBE_PATH_INVALID;
    }

    return TC_PROBE_PATH_INVALID;
}

/*************************************************************************/

#define RESIZE_DIV      8
#define DIM_IS_OK(dim)  ((dim) % RESIZE_DIV == 0)

int tc_compute_fast_resize_values(void *_vob, int strict)
{
    int ret = -1;
    int dw = 0, dh = 0; /* delta width, height */
    vob_t *vob = _vob; /* adjust pointer */

    /* sanity checks first */
    if (vob == NULL) {
        return -1;
    }
    if (!DIM_IS_OK(vob->ex_v_width) || !DIM_IS_OK(vob->ex_v_width)) {
        return -1;
    }
    if (!DIM_IS_OK(vob->zoom_width) || !DIM_IS_OK(vob->zoom_width)) {
        return -1;
    }
    
    dw = vob->ex_v_width - vob->zoom_width;
    dh = vob->ex_v_height - vob->zoom_height;
    /* MORE sanity checks */
    if (!DIM_IS_OK(dw) || !DIM_IS_OK(dh)) {
        return -1;
    }
    if (dw == 0 && dh == 0) {
        /* we're already fine */
        ret = 0;
    } else  if (dw > 0 && dh > 0) {
        /* smaller destination frame -> -B */
        vob->resize1_mult = RESIZE_DIV;
        vob->hori_resize1 = dw / RESIZE_DIV;
        vob->vert_resize1 = dh / RESIZE_DIV;
        ret = 0;
    } else if (dw < 0 && dh < 0) {
        /* bigger destination frame -> -X */
        vob->resize2_mult = RESIZE_DIV;
        vob->hori_resize2 = -dw / RESIZE_DIV;
        vob->vert_resize2 = -dh / RESIZE_DIV;
        ret = 0;
    } else if (strict == 0) {
        /* always needed in following cases */
        vob->resize1_mult = RESIZE_DIV;
        vob->resize2_mult = RESIZE_DIV;
        ret = 0;
        if (dw <= 0 && dh >= 0) {
            vob->hori_resize2 = -dw / RESIZE_DIV;
            vob->vert_resize1 = dh / RESIZE_DIV;
        } else if (dw >= 0 && dh <= 0) {
            vob->hori_resize1 = dw / RESIZE_DIV;
            vob->vert_resize2 = -dh / RESIZE_DIV;
        }
    }

    if (ret == 0) {
        vob->zoom_width = 0;
        vob->zoom_height = 0;
    }
    return ret;
}

#undef RESIZE_DIV
#undef DIM_IS_OK

/*************************************************************************/


int tc_find_best_aspect_ratio(const void *_vob,
                              int *sar_num, int *sar_den,
                              const char *tag)
{
    const vob_t *vob = _vob; /* adjust pointer */
    int num, den;

    if (!vob || !sar_num || !sar_den) {
        return TC_ERROR;
    }

    /* Aspect Ratio Calculations (modified code from export_ffmpeg.c) */
    if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_PAR) {
        if (vob->ex_par > 0) {
            /* 
             * vob->ex_par MUST be guarantee to be in a sane range
             * by core (transcode/tcexport 
             */
            tc_par_code_to_ratio(vob->ex_par, &num, &den);
        } else {
            /* same as above */
            num = vob->ex_par_width;
            den = vob->ex_par_height;
        }
        tc_log_info(tag, "DAR value ratio calculated as %f = %d/%d",
                    (double)num/(double)den, num, den);
    } else {
        if (vob->export_attributes & TC_EXPORT_ATTRIBUTE_ASR) {
            /* same as above for PAR stuff */
            tc_asr_code_to_ratio(vob->ex_asr, &num, &den);
            tc_log_info(tag, "display aspect ratio calculated as %f = %d/%d",
                        (double)num/(double)den, num, den);

            /* ffmpeg FIXME:
             * This original code might lead to rounding/truncating errors
             * and maybe produces too high values for "den" and
             * "num" for -y ffmpeg -F mpeg4
             *
             * sar = dar * ((double)vob->ex_v_height / (double)vob->ex_v_width);
             * lavc_venc_context->sample_aspect_ratio.num = (int)(sar * 1000);
             * lavc_venc_context->sample_aspect_ratio.den = 1000;
             */

             num *= vob->ex_v_height;
             den *= vob->ex_v_width;
             /* I don't need to reduce since x264 does it itself :-) */
             tc_log_info(tag, "sample aspect ratio calculated as"
                              " %f = %d/%d",
                              (double)num/(double)den, num, den);

        } else { /* user did not specify asr at all, assume no change */
            tc_log_info(tag, "set display aspect ratio to input");
            num = 1;
            den = 1;
        }
    }

    *sar_num = num;
    *sar_den = den;
    return TC_OK;
}

/*************************************************************************/

void tc_strstrip(char *s) 
{
    char *start;

    if (s == NULL) {
        return;
    }
    
    start = s;
    while ((*start != 0) && isspace(*start)) {
        start++;
    }
    
    memmove(s, start, strlen(start) + 1);
    if (strlen(s) == 0) {
        return;
    }
    
    start = &s[strlen(s) - 1];
    while ((start != s) && isspace(*start)) {
        *start = 0;
        start--;
    }
}

char **tc_strsplit(const char *str, char sep, size_t *pieces_num)
{
    const char *begin = str, *end = NULL;
    char **pieces = NULL, *pc = NULL;
    size_t i = 0, n = 2;
    int failed = TC_FALSE;

    if (!str || !strlen(str)) {
        return NULL;
    }

    while (begin != NULL) {
        begin = strchr(begin, sep);
        if (begin != NULL) {
            begin++;
            n++;
        }
    }

    pieces = tc_malloc(n * sizeof(char*));
    if (!pieces) {
        return NULL;
    }

    begin = str;
    while (begin != NULL) {
        size_t len;

        end = strchr(begin, sep);
        if (end != NULL) {
            len = (end - begin);
        } else {
            len = strlen(begin);
        }
        if (len > 0) {
            pc = tc_strndup(begin, len);
            if (pc == NULL) {
                failed = TC_TRUE;
                break;
            } else {
                pieces[i] = pc;
                i++;
            }
        }
        if (end != NULL) {
            begin = end + 1;
        } else {
            break;
        }
    }

    if (failed) {
        /* one or more copy of pieces failed */
        tc_free(pieces);
        pieces = NULL;
    } else { /* i == n - 1 -> all pieces copied */
        pieces[n - 1] = NULL; /* end marker */
        if (pieces_num != NULL) {
            *pieces_num = i;
        }
    }
    return pieces;
}

void tc_strfreev(char **pieces)
{
    if (pieces != NULL) {
        int i = 0;
        for (i = 0; pieces[i] != NULL; i++) {
            tc_free(pieces[i]);
        }
        tc_free(pieces);
    }
}
 
/*************************************************************************/

/* 
 * clamp an unsigned value so it can be safely (without any loss) in
 * an another unsigned integer of <butsize> bits.
 */
static int32_t clamp(int32_t value, uint8_t bitsize)
{
    value = (value < 1) ?1 :value;
    value = (value > (1 << bitsize)) ?(1 << bitsize) :value;
    return value;
}

int tc_read_matrix(const char *filename, uint8_t *m8, uint16_t *m16)
{
    int i = 0;
    FILE *input = NULL;

    /* Open the matrix file */
    input = fopen(filename, "rb");
    if (!input) {
        tc_log_warn("read_matrix",
            "Error opening the matrix file %s",
            filename);
        return -1;
    }
    if (!m8 && !m16) {
        tc_log_warn("read_matrix", "bad matrix reference");
        return -1;
    }

    /* Read the matrix */
    for(i = 0; i < TC_MATRIX_SIZE; i++) {
        int value;

        /* If fscanf fails then get out of the loop */
        if(fscanf(input, "%d", &value) != 1) {
            tc_log_warn("read_matrix",
                "Error reading the matrix file %s",
                filename);
            fclose(input);
            return 1;
        }

        if (m8 != NULL) {
            m8[i] = clamp(value, 8);
        } else {
            m16[i] = clamp(value, 16);
        }
    }

    /* We're done */
    fclose(input);

    return 0;
}

void tc_print_matrix(uint8_t *m8, uint16_t *m16)
{
    int i;

    if (!m8 && !m16) {
        tc_log_warn("print_matrix", "bad matrix reference");
        return;
    }
   
    // XXX: magic number
    for(i = 0; i < TC_MATRIX_SIZE; i += 8) {
        if (m8 != NULL) {
            tc_log_info("print_matrix",
                        "%3d %3d %3d %3d "
                        "%3d %3d %3d %3d",
                        (int)m8[i  ], (int)m8[i+1],
                        (int)m8[i+2], (int)m8[i+3],
                        (int)m8[i+4], (int)m8[i+5],
                        (int)m8[i+6], (int)m8[i+7]);
        } else {
            tc_log_info("print_matrix",
                        "%3d %3d %3d %3d "
                        "%3d %3d %3d %3d",
                        (int)m16[i  ], (int)m16[i+1],
                        (int)m16[i+2], (int)m16[i+3],
                        (int)m16[i+4], (int)m16[i+5],
                        (int)m16[i+6], (int)m16[i+7]);
        }
    }
    return;
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
