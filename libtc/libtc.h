/*
 *  libtc.h - include file for utilities library for transcode
 *
 *  Copyright (C) Thomas Oestreich - August 2003
 *  Copyright (C) Transcode Team - 2005-2010
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

#ifndef LIBTC_H
#define LIBTC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <string.h>
#include <pthread.h>

#ifndef OS_BSD
# ifdef HAVE_MALLOC_H
#  include <malloc.h>
# endif
#endif

#include "tccodecs.h"
#include "tcformats.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TC_FALSE,
    TC_TRUE
};

#define TC_NULL_MATCH           -1

#define TC_BUF_MAX            1024
#define TC_BUF_LINE            256
#define TC_BUF_MIN             128

#define TC_MAX(a, b)		(((a) > (b)) ?(a) :(b))
#define TC_MIN(a, b)		(((a) < (b)) ?(a) :(b))
/* clamp x between a and b */
#define TC_CLAMP(x, a, b)	TC_MIN(TC_MAX((a), (x)), (b))

/* colors macros */
#define COL(x)              "\033[" #x ";1m"
#define COL_RED             COL(31)
#define COL_GREEN           COL(32)
#define COL_YELLOW          COL(33)
#define COL_BLUE            COL(34)
#define COL_WHITE           COL(37)
#define COL_GRAY            "\033[0m"

#define TC_LOG_COLOR_ENV_VAR	"TRANSCODE_LOG_NO_COLOR"
#define TC_LOG_COLOR_OPTION     "--log_no_color"

/* 
 * Made to be compatible with 
 *      TC_IMPORT_{OK,ERROR,UNKNOWN}
 *      TC_EXPORT_{OK,ERROR,UNKNOWN}
 * see src/transcode.h
 */
typedef enum {
    TC_ERROR     = -1,
    TC_OK        =  0,
    TC_INTERRUPT =  1,
    TC_UNKNOWN, /* MUST always be the last one */
} TCReturnCode;


typedef enum {
    TC_LOG_ERR = 0, /* critical error condition */
    TC_LOG_WARN,    /* non-critical error condition */
    TC_LOG_INFO,    /* informative highlighted message */
    TC_LOG_MSG,     /* regular message */
    
    TC_LOG_EXTRA,   /* must always be the last */
    /* 
     * on this special log level is guaranteed that message will be logged
     * verbatim: no tag, no colours, anything
     */
} TCLogLevel;


/*
 * libtc_init:
 *     tune up some libtc settings.
 *     You DO NOT always NEED to use this function because libtc has it
 *     (most of time) sane defaults; just use this function to adapt
 *     libtc behaviour to some unusual conditions, like if stderr is a file
 *     and not a terminal.
 *     If you use this function, you MUST call it BEFORE any other libtc call,
 *     or you will experience undefined behaviours.
 *     It's safe to call libtc_setup multiple times BEFORE to call any other
 *     libtc function.
 *
 * Parameters:
 *     flags: flag to tune libtc behaviour (see above)
 * Return Value:
 *     N/A
 * Side effects:
 *     various. See description of flags above.
 * Preconditions:
 *     this function, IF used, MUST be called BEFORE any other libtc function.
 */
void libtc_init(int *argc, char ***argv);

/*
 * tc_log:
 *     main libtc logging function. Log arbitrary messages according
 *     to a printf-like format chosen by the caller.
 *
 * Parameters:
 *     level: priority of message to log; see TCLogLevel definition
 *            above.
 *       tag: header of message, to identify subsystem originating
 *            the message. It's suggested to use __FILE__ as
 *            fallback default tag.
 *       fmt: printf-like format string. You must provide enough
 *            further arguments to fullfill format string, doing
 *            otherwise will cause an undefined behaviour, most
 *            likely a crash.
 * Return Value:
 *      0 if message succesfully logged.
 *     -1 if message was truncated.
 *        (message too large and buffer allocation failed).
 * Side effects:
 *     this function store final message in an intermediate string
 *     before to log it to destination. If such intermediate string
 *     is wider than a given amount (TC_BUF_MIN * 2 at moment
 *     of writing), tc_log needs to dinamically allocate some memory.
 *     This allocation can fail, and as result log message will be
 *     truncated to fit in avalaible static buffer.
 */
int tc_log(TCLogLevel level, const char *tag, const char *fmt, ...)
#ifdef HAVE_GCC_ATTRIBUTES
__attribute__((format(printf,3,4)))
#endif
;

/* 
 * When to use tc_log*() stuff?
 *
 * tc_log() family should be used for non-output status messages, like
 * the ones coming from the various modules and components of transcode.
 * For actual output use printf() (or fprintf(), etc.) as appropriate.
 * (yes, this means that transcode prints a lot of status and a very
 * few output messages).
 */

/* compatibility macros */
#define tc_error(format, args...) do { \
    tc_log(TC_LOG_ERR, PACKAGE, format , ## args); \
    exit(1); \
} while(0)
#define tc_info(format, args...) \
    tc_log(TC_LOG_INFO, PACKAGE, format , ## args)
#define tc_warn(format, args...) \
    tc_log(TC_LOG_WARN, PACKAGE, format , ## args)

/* macro goodies */
#define tc_log_error(tag, format, args...) \
    tc_log(TC_LOG_ERR, tag, format , ## args)
#define tc_log_info(tag, format, args...) \
    tc_log(TC_LOG_INFO, tag, format , ## args)
#define tc_log_warn(tag, format, args...) \
    tc_log(TC_LOG_WARN, tag, format , ## args)
#define tc_log_msg(tag, format, args...) \
    tc_log(TC_LOG_MSG, tag, format , ## args)

#define tc_log_perror(tag, string) do {                            \
    const char *__s = (string);  /* watch out for side effects */  \
    tc_log_error(tag, "%s%s%s", __s ? __s : "",                    \
                 (__s && *__s) ? ": " : "",  strerror(errno));     \
} while (0)

/*************************************************************************/

/*
 * tc_mangle_cmdline:
 *      parse a command line option array looking for a given option.
 *      Given option can be short or long but must be given literally.
 *      So, if you want to mangle "--foobar", give "--foobar" not
 *      "foobar". Same story for short options "-V": use "-V" not "V".
 *      If given option isn't found in string option array, do nothing
 *      and return succesfull (see below). If option is found but
 *      its argument isn't found, don't mangle string options array
 *      but return failure.
 *      If BOTH option and its value is found, store a pointer to
 *      option value into "optval" parameter and remove both option
 *      and value from string options array.
 * Parameters:
 *      argc: pointer to number of values present into option string
 *            array. This parameter must be !NULL and it's updated
 *            by a succesfull call of this function.
 *      argv: pointer to array of option string items. This parameter
 *            must be !NULL and it's updated by a succesfull call of
 *            this function
 *       opt: option to look for.
 *    optval: if !NULL, this function will expect a value for given option;
 *            if such value is found, `optval' will point to it.
 * Return value:
 *      1: no option found
 *      0: succesfull
 *     -1: bad parameter(s) (NULL)
 *     -2: bad usage: expected value for option, but not found,
 * Postconditions:
 *      this function must operate trasparently by always leaving
 *      argc/argv in an usable and consistent state.
 */
int tc_mangle_cmdline(int *argc, char ***argv,
                      const char *opt, const char **optval);

/*
 * tc_test_program:
 *     check if a given program is avalaible in current PATH.
 *     This function of course needs to read (and copy) the PATH
 *     environment variable
 *
 * Parameters:
 *     name: name of program to look for.
 * Return Value:
 *     0 if program was found in PATH.
 *     ENOENT if program was not found in PATH
 *     value of errno if program was found in PATH but it wasn't accessible
 *     for some reason.
 */
int tc_test_program(const char *name);

/*
 * Safer string functions from OpenBSD, because these are not in all
 * libc implementations.
 */

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t size);
#endif

/*
 * tc_strsplit:
 *      split a given string into tokens using given separator character.
 *      Return NULL-terminated array of splitted tokens, and optionally
 *      return (via a out parameter) size of returned array.
 *
 * Parameters:
 *         str: string to split
 *         sep: separator CHARACTER: cut string when sep is found
 *  pieces_num: if not NULL, store here the size of returned array
 * Return value:
 *      NULL-terminated array of splitted pieces.
 *      You must explicitely free this returned array by using tc_strfreev
 *      (see below) in order to avoid memleaks.
 */
char **tc_strsplit(const char *str, char sep, size_t *pieces_num);

/*
 * tc_strfreev:
 *      return an array of strings as returned by tc_strsplit
 *
 * Parameters:
 *      pieces: return value of tc_strsplit to be freed.
 * Return value:
 *      None.
 */
void tc_strfreev(char **pieces);

/*
 * tc_strstrip:
 * 	remove IN PLACE heading and trailing whitespaces from a given
 * 	C-string. This means that given string will be mangled to
 * 	remove such whitespace while moving pointer to first element
 * 	and terminating '\0'.
 * 	It's safe to supply a NULL string.
 * Parameters:
 *      s: string to strip.
 * Return Value:
 *      None
 */
void tc_strstrip(char *s);

/*
 * tc_test_string:
 *	check the return value of snprintf, strlcpy, and strlcat.
 *      If an error is detected, prints reason.
 *
 * Parameters:
 *        file: name of source code file on which this function is called
 *              (this parameter is usually equal to __FILE__).
 *        line: line of source code file on which this function is called
 *              (this parameter is usually equal to __LINE__).
 *       limit: maximum size of char buffer previously used.
 *         ret: return code of one of above function.
 *      errnum: error code (this parameter is usually equal to errno)
 * Return Value:
 * 	< 0 is an internal error.
 *      >= limit means characters were truncated.
 *      0 if not problems.
 *      1 if error.
 */
int tc_test_string(const char *file, int line, int limit,
                   long ret, int errnum);


/*
 * These versions of [v]snprintf() return -1 if the string was truncated,
 * printing a message to stderr in case of truncation (or other error).
 */
#define tc_vsnprintf(buf,limit,format,args...) \
    _tc_vsnprintf(__FILE__, __LINE__, buf, limit, format , ## args)
#define tc_snprintf(buf,limit,format,args...) \
    _tc_snprintf(__FILE__, __LINE__, buf, limit, format , ## args)

int _tc_vsnprintf(const char *file, int line, char *buf, size_t limit,
                  const char *format, va_list args);
int _tc_snprintf(const char *file, int line, char *buf, size_t limit,
                 const char *format, ...);

/*************************************************************************/

/*
 * tc_malloc: just a simple wrapper on libc's malloc(), with emits
 *            an additional warning, specifying calling context,
 *            if allocation fails
 * tc_zalloc: like tc_malloc, but zeroes all acquired memory before
 *             returning to the caller (this is quite common in
 *             transcode codebase)
 * tc_realloc: the same thing for realloc()
 * tc_free: the companion memory releasing wrapper.
 */
#define tc_malloc(size) \
            _tc_malloc(__FILE__, __LINE__, size)
#define tc_zalloc(size) \
            _tc_zalloc(__FILE__, __LINE__, size)
#define tc_realloc(p,size) \
            _tc_realloc(__FILE__, __LINE__, p, size)
#define tc_free(ptr) \
            free(ptr);

/*
 * _tc_malloc:
 *     do the real work behind tc_malloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr  if acquisition fails
 * Preconditions:
 *     file param not null
 */
void *_tc_malloc(const char *file, int line, size_t size);

/*
 * _tc_zalloc:
 *     do the real work behind tc_zalloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr  if acquisition fails
 * Preconditions:
 *     file param not null
 * Postconditions:
 *     if call succeed, acquired memory contains all zeros
 */
void *_tc_zalloc(const char *file, int line, size_t size);

/*
 * _tc_realloc:
 *     do the real work behind tc_realloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *        p: pointer to reallocate
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr if acquisition fails
 * Preconditions:
 *     file param not null
 */
void *_tc_realloc(const char *file, int line, void *p, size_t size);

/*
 * Allocate a buffer aligned to the machine's page size, if known.  The
 * buffer must be freed with buffree() (not free()).
 */

#define tc_bufalloc(size) \
            _tc_bufalloc(__FILE__, __LINE__, size)

/*
 * _tc_malloc:
 *     do the real work behind _tc_bufalloc macro
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs
 *           (above two parameters are intended to be, and usually
 *           are, filled by tc_malloc macro)
 *     size: size of desired chunk of memory
 * Return Value:
 *     a pointer of acquired, aligned, memory, or NULL if acquisition fails
 * Side effects:
 *     a message is printed on stderr (20051017)
 * Preconditions:
 *     file param not null
 */

void *_tc_bufalloc(const char *file, int line, size_t size);

/*
 * tc_buffree:
 *     release a memory buffer acquired using tc_bufalloc
 *
 * Parameters:
 *     ptr: pointer obtained as return value of a succesfull
 *          tc_bufalloc() call
 * Return Value:
 *     none
 * Preconditions:
 *     ptr is acquired via tc_bufalloc(). Really BAD things will happen
 *     if a buffer acquired via tc_bufalloc() is released using anything
 *     but tc_buffree(), or vice versa.
 */
void tc_buffree(void *ptr);

/*************************************************************************/

/*
 * tc_strdup: a macro wrapper on top of _tc_strndup, like tc_malloc, above
 * tc_strndup: like tc_strdup, but copies only N byte of given string
 *
 * This function does the same thing of libc's standard function
 * strdup(3) and the GNU extension strndup(3), but using libtc's
 * tc_malloc features.
 */
#define tc_strdup(s) \
            _tc_strndup(__FILE__, __LINE__, s, strlen(s))
#define tc_strndup(s, n) \
            _tc_strndup(__FILE__, __LINE__, s, n)

/*
 * _tc_strndup:
 *     do the real work behind tc_strdup/tc_strndup macro. This function
 *     adds automatically and implicitely a '\0' terminator at end of
 *     copied string.
 *
 * Parameters:
 *     file: name of the file on which call occurs
 *     line: line of above file on which call occurs (above two parameters
 *           are intended to be, and usually are, filled by tc_malloc macro)
 *        s: null-terminated string to copy
 *        n: copy at most 'n' characters of original string.
 * Return Value:
 *     a pointer to a copy of given string. This pointer must be freed using
 *     tc_free() to avoid memory leaks
 * Side effects:
 *     a message is printed on stderr (20051017)
 * Preconditions:
 *     file param not null
 * Postconditions:
 *     none
 */
char *_tc_strndup(const char *file, int line, const char *s, size_t n);

/*
 * tc_file_check:
 *     verify the type of a given file (path) this function will be
 *     deprecated very soon, replaced by a powered tc_probe_path().
 *
 * Parameters:
 *     file: the file (really: path) to verify.
 * Return Value:
 *     -1 if an internal error occur
 *     0  if given path is really a file
 *     1  if given path is a directory
 * Side effects:
 *     none
 * Preconditions:
 *     none
 * Postconditions:
 *     none
 */
int tc_file_check(const char *file);

/*
 * tc_pread:
 *     read an entire buffer from a file descriptor, restarting
 *     automatically if interrupted. This function is basically a wrapper
 *     around posix read(2); read(2) can be interrupted by a signal,
 *     so doesn't guarantee that all requested bytes are effectively readed
 *     when read(2) returns; this function ensures so, except for critical
 *     errors.
 * Parameters:
 *      fd: read data from this file descriptor
 *     buf: pointer to a buffer which will hold readed data
 *     len: how much data function must read from fd
 * Return Value:
 *     size of effectively readed data
 * Side effects:
 *     errno is readed internally
 * Postconditions:
 *     read exactly the requested bytes, if no *critical*
 *     (tipically I/O related) error occurs.
 */
ssize_t tc_pread(int fd, uint8_t *buf, size_t len);

/*
 * tc_pwrite:
 *     write an entire buffer from a file descriptor, restarting
 *     automatically if interrupted. This function is basically a wrapper
 *     around posix write(2); write(2) can be interrupted by a signal,
 *     so doesn't guarantee that all requested bytes are effectively writed
 *     when write(2) returns; this function ensures so, except for critical
 *     errors.
 * Parameters:
 *      fd: write data on this file descriptor
 *     buf: pointer to a buffer which hold data to be written
 *     len: how much data function must write in fd
 * Return Value:
 *     size of effectively written data
 * Side effects:
 *     errno is readed internally
 * Postconditions:
 *     write exactly the requested bytes, if no *critical* (tipically I/O
 *     related) error occurs.
 */
ssize_t tc_pwrite(int fd, const uint8_t *buf, size_t len);

/*
 * tc_preadwrite:
 *     read all data avalaible from a file descriptor, putting it on the
 *     other one.
 * Parameters:
 *      in: read data from this file descriptor
 *     out: write readed data on this file descriptor
 * Return Value:
 *     -1 if a read error happens
 *     0  if no error happens
 * Postconditions:
 *     move the entire content of 'in' into 'out', if no *critical*
 *     (tipically I/O related) error occurs.
 */
int tc_preadwrite(int in, int out);

enum {
    TC_PROBE_PATH_INVALID = 0,
    TC_PROBE_PATH_ABSPATH,
    TC_PROBE_PATH_RELDIR,
    TC_PROBE_PATH_FILE,
    TC_PROBE_PATH_BKTR,
    TC_PROBE_PATH_SUNAU,
    TC_PROBE_PATH_V4L_VIDEO,
    TC_PROBE_PATH_V4L_AUDIO,
    TC_PROBE_PATH_OSS,
    /* add more elements here */
};

/*
 * tc_probe_path:
 *     verify the type of a given path.
 *
 * Parameters:
 *     path: the path to probe.
 * Return Value:
 *     the probed type of path. Can be TC_PROBE_PATH_INVALID if given path
 *     doesn't exists or an internal error occur.
 * Side effects:
 *     if function fails, one or more debug message can be issued using
 *     tc_log*(). A name resolve request can be issued to system.
 */
int tc_probe_path(const char *name);

/* codec helpers ***********************************************************/

/*
 * tc_translate_codec_id:
 *     translate a CODEC_* value to corresponding TC_CODEC_* one.
 *
 * Parameters:
 *     codec: CODEC_* value to translate.
 * Return value:
 *     corresponding TC_CODEC_* value, or
 *     TC_CODEC_ERROR if given CODEC_XXX hasn't corresponding TC_CODEC_XXX
 *     or if it;s just unknown.
 */
int tc_translate_codec_id(TCCodecID codec);

/*
 * tc_codec_to_comment:
 *     return a short constant descriptive string given the codec identifier.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to represent.
 * Return value:
 *     a constant string describing the given codec (there is no need to
 *     free() it).
 * Postconditions:
 *     Always return something sensible, even if unknown codec id was given.
 */
const char* tc_codec_to_comment(TCCodecID codec);

/*
 * tc_codec_to_string:
 *     return the codec name as a lowercase constant string,
 *     given the codec identifier.
 *
 * Parameters:
 *     codec: the TC_CODEC_* value to represent.
 * Return value:
 *     a constant string representing the given codec (there is no need to
 *     free() it).
 *     NULL if codec is (yet) unknown.
 */
const char* tc_codec_to_string(TCCodecID codec);

/*
 * tc_codec_from_string:
 *     extract codec identifier from its string representation
 *
 * Parameters:
 *     codec: string representation of codec, lowercase (name).
 * Return value:
 *     the correspinding TC_CODEC_* of given string representation,
 *     or TC_CODEC_ERROR if string is unknown or wrong.
 */
TCCodecID tc_codec_from_string(const char *codec);

/*
 * tc_codec_fourcc:
 *     extract the FOURCC code for a given codec, if exists.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to get the FOURCC for.
 * Return value:
 *     a constant string representing the FOURCC for a given codec (there
 *     is no need to free() it NULL of codec's FOURCC is (yet) unknown or
 *     given codec has _not_ FOURCC (es: audio codec identifiers).
 */
const char* tc_codec_fourcc(TCCodecID codec);

/*
 * tc_codec_description:
 *     describe a codec, given its ID.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to get the description for.
 *     buf: buffer provided to caller. Description will be stored here.
 *     bufsize: size of the given buffer.
 * Return value:
 *     -1 if requested codec isn't known.
 *     0  truncation error (given buffer too small).
 *     >0 no errors.
 */
int tc_codec_description(TCCodecID codec, char *buf, size_t bufsize);

/* 
 * tc_codec_is_multipass:
 *     tell if a given codec is multipass capable or not.
 *
 * Parameters:
 *     codec: TC_CODEC_* value to inquiry.
 * Return value:
 *     TC_TRUE: given codec is multipass capable.
 *     TC_FALSE: given codec is NOT multipass capable OR is not known.
 */
int tc_codec_is_multipass(TCCodecID codec);

/*************************************************************************/

/*
 * tc_compute_fast_resize_values:
 *     compute internal values needed for video frame fast resize (-B/-X)
 *     given base resolution (ex_v_{width,height}) and target one
 * 	   (zoom_{width,height}).
 *     WARNING: at moment of writing there are some back compatibility
 *     constraints, nevethless this function interface (notabley I/O
 *     parameters passing) needs a SERIOUS rethink.
 * 
 * Parameters:
 *      _vob: pointer to a structure on which read/store values for
 *            computation.
 *            Should ALWAYS really be a pointer to a vob_t structure,
 *            but vob_t pointer isn't used (yet) in order to avoid
 *            libtc/transcode.h interdependency.
 *            I'm not yet convinced that those informations should go
 *            in TCExportInfo because only transcode core needs them.
 *            Perhaps the cleanest solution is to introduce yet
 *            another structure :\.
 *            If anyone has a better solution just let me know -- FR.
 *            vob_t fields used:
 *                ex_v_{width, height}: base resolution (In)
 *                zoom_{width, height}: target resolution (In)
 *                resize{1,2}_mult, vert_resize{1,2}, hori_resize{1,2}:
 *                                   computed parameters (Out)
 *    strict: if !0, allow only enlarging and shrinking of frame in
 *            both dimensions, and fail otherwise.
 * Return value:
 *      0 succesfull
 *     -1 error, computation failed 
 *        (i.e. width or height not multiple of 8)
 * Side effects:
 *     if succesfull, zoom_{width,height} will be set to 0.
 */
int tc_compute_fast_resize_values(void *_vob, int strict);

/*************************************************************************/

/**
 * tc_find_best_aspect_ratio:
 * 	set sar_num and sar_den to the sample aspect ratio (also called
 * 	pixel aspect ratio) described by vob->ex_par,
 * 	vob->ex_par_width, vob->ex_par_height and vob->ex_asr.
 *
 * This function might return quite high values in sar_num and
 * sar_den. Depending on what codec these parameters are given to,
 * eventually a common factor should be reduced first. In case of x264
 * this is not needed, because it's done in x264's code.
 *
 * Parameters:
 *         vob: constant pointer to vob structure.
 *     sar_num: integer to store SAR-numerator in.
 *     sar_den: integer to store SAR-denominator in.
 *         tag: tag to use in log messages (if any).
 *
 * Returns:
 *     0 on success, nonzero otherwise (this means bad parameters).
 */
int tc_find_best_aspect_ratio(const void *_vob,
                              int *sar_num, int *sar_den,
			      const char *tag);

/*************************************************************************/

/*
 * XXX: add some general notes about quantization matrices stored
 * into files (format etc. etc.)
 *
 * tc_*_matrix GOTCHA:
 * Why _two_ allowed elements wideness? Why this mess?
 * The problem is that XviD and libavcodec wants elements for
 * quantization matrix in two different wideness. Obviously
 * we DON'T want to patch such sources, so we must handle in
 * some way this difference.
 * Of course we are looking for cleaner solutions.
 * -- fromani 20060305
 */

/*
 * Total size (=number of elements) of quantization matrix
 * for following two support functions
 */
#define TC_MATRIX_SIZE     (64)

/*
 * tc_read_matrix:
 *     read a quantization matrix from given file.
 *     Can read 8-bit wide or 16-bit wide matrix elements.
 *     Store readed matrix in a caller-provided buffer.
 *
 *     Caller can select the elements wideness just
 *     providing a not-NULL buffer for corresponding buffer.
 *     For example, if caller wants to read a quantization matrix
 *     from 'matrix.txt', and want 16-bit wide elements, it
 *     will call
 *
 *     uint16_t matrix[TC_MATRIX_SIZE];
 *     tc_read_matrix('matrix.txt', NULL, matrix);
 *
 * Parameters:
 *     filename: read quantization matrix from this file.
 *           m8: buffer for 8-bit wide elements quantization matrix
 *          m16: buffer for 16-bit wide elements quantization matrix
 *
 *     NOTE: if m8 AND m16 BOTH refers to valid buffers, 8-bit
 *     wideness is preferred.
 * Return value:
 *     -1 filename not found, or neither buffers is valid.
 *     +1 read error: matrix incomplete or badly formatted.
 *     0  no errors.
 * Side effects:
 *     a file on disk is open, readed, closed.
 * Preconditions:
 *     buffer provided by caller MUST be large enough to hold
 *     TC_MATRIX_SIZE elements of requested wideness.
 *     At least one given buffer is valid.
 */
int tc_read_matrix(const char *filename, uint8_t *m8, uint16_t *m16);

/*
 * tc_print_matrix:
 *     print (using tc_log*) a quantization matrix.
 *     Can print 8-bit wide or 16-bit wide matrix elements.
 *
 *     Caller must provide a valid pointer correspoinding to
 *     wideness of elements of matrix to be printed.
 *     Example: quantization matrix has 8-bit wide elements:
 *
 *     uint8_t matrix[TC_MATRIX_SIZE];
 *     // already filled with something useful
 *     tc_print_matrix(matrix, NULL);
 *
 * Parameters:
 *     m8: pointer to 8-bit wide elements quantization matrix.
 *     m16: pointer to 16-bit wide elements quantization matrix.
 *
 *     NOTE: if m8 AND m16 BOTH refers to valid buffers, 8-bit
 *     wideness is preferred.
 * Preconditions:
 *     At least one given pointer is valid.
 */
void tc_print_matrix(uint8_t *m8, uint16_t *m16);

#ifdef __cplusplus
}
#endif

#endif  /* _LIBTC_H */
