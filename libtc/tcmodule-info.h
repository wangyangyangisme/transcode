/*
 * tcmodule-info.h -- module data (capabilities) and helper functions.
 * (C) 2005-2010 - Francesco Romani <fromani -at- gmail -dot- com>
 *
 * This file is part of transcode, a video stream processing tool.
 *
 * transcode is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transcode is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef TCMODULEINFO_H
#define TCMODULEINFO_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>

#include "tccodecs.h"
#include "tcformats.h"

/* FIXME: move to a enum? */
/* actions */
#define TC_MODULE_FEATURE_NONE          0x00000000

#define TC_MODULE_FEATURE_FILTER        0x00000001
#define TC_MODULE_FEATURE_DECODE        0x00000002
#define TC_MODULE_FEATURE_ENCODE        0x00000004
#define TC_MODULE_FEATURE_DEMULTIPLEX   0x00000020
#define TC_MODULE_FEATURE_MULTIPLEX     0x00000040
/* targets */
#define TC_MODULE_FEATURE_VIDEO         0x00010000
#define TC_MODULE_FEATURE_AUDIO         0x00020000
#define TC_MODULE_FEATURE_EXTRA         0x00040000

#define TC_MODULE_FLAG_NONE             0x00000000
#define TC_MODULE_FLAG_RECONFIGURABLE   0x00000001
/* module can be reconfigured multiple times */
#define TC_MODULE_FLAG_DELAY            0x00000002
/* module require more than one input frame to work */
#define TC_MODULE_FLAG_BUFFERING        0x00000004
/* module require extra internal buffering 
 * (XXX: this flag will hopefully vanish soon) */
#define TC_MODULE_FLAG_CONVERSION       0x00000010
/* module requires an unavoidable csp conversion)
 * (XXX: this flag will hopefully vanish soon) */

/*
 * this structure will hold all the interesting informations
 * both for user and for transcode itself of a given module.
 */
typedef struct tcmoduleinfo_ TCModuleInfo;
struct tcmoduleinfo_ {
    uint32_t features; /* what this module can do? */
    uint32_t flags; /* quirks */

    const char *name;
    const char *version;
    const char *description;

    /*
     * the following two MUST point to an array of TC_CODEC_*
     * terminated by a TC_CODEC_ERROR value
     */
    const TCCodecID *codecs_in;
    const TCCodecID *codecs_out;

    /*
     * the following two MUST point to an array of TC_FORMAT_*
     * terminated by a TC_FORMAT_ERROR value
     */
    const TCFormatID *formats_in;
    const TCFormatID *formats_out;
};

/*
 * tc_module_info_match:
 *     scan the given informations about two modules, and tell if the
 *     two modules can be chained together using the given codec.
 *
 * Parameters:
 *     tc_codec:
 *         codec_id to be used.
 *     head:
 *         the first given module information structure;
 *         'head' output is supposed to fit in 'tail' input.
 *     tail:
 *         the second given module information structure;
 *         tail' input is supposed to be given by 'head' output.
 *
 * Return value:
 *     1 if 'head' can feed 'tail' safely,
 *     0 otherwise
 *
 * Side effects:
 *     none
 *
 * Preconditions:
 *     none
 *
 * Postconditions:
 *     none
 */
int tc_module_info_match(int tc_codec,
                         const TCModuleInfo *head,
                         const TCModuleInfo *tail);

/*
 * tc_module_info_log:
 *     pretty-print the content of a given module information structure
 *     using functions of tc_log_*() family.
 *
 * Parameters:
 *     info:
 *         module information structure to dump
 *     verbose:
 *         level of detail of description, ranging to TC_QUIET to TC_STATS.
 *         Other values will be ignored
 *
 * Return value:
 *     none
 *
 * Side effects:
 *     some informations are printed using tc_log_*()
 *
 * Preconditions:
 *     'verbose' must be in range TC_QUIET ... TC_STATS
 *
 * Postconditions:
 *     none
 */
void tc_module_info_log(const TCModuleInfo *info, int verbose);

/*
 * tc_module_info_copy:
 *     create an exact copy of 'src' in 'dst', allocating new fields.
 *     PLEASE NOTE: this is an 'hard' (real) copy, which makes two
 *     TCModuleInfo identical but indipendent.
 *     For a 'soft' (reference) copy, just memcpy() the two structures.
 *
 * Parameters:
 *     src: TCModuleInfo structure to be copied
 *     dst: pointer to structure that will hold the copy.
 *
 * Return value:
 *     0  successfull
 *     -1 given (at least) a bad TCModuleInfo reference
 *     1  not enough memory to perform a full copy
 *
 * Side effects:
 *     memory in 'dst' is allocated, usinc tc_* helpers, to hold data
 *     provided by 'src'.
 *
 * Preconditions:
 *     'src' and 'dst' must point to valid TCModuleInfo structures.
 *
 * Postconditions:
 *     'dst' is an exact copy of 'src'.
 */
int tc_module_info_copy(const TCModuleInfo *src, TCModuleInfo *dst);

/*
 * tc_module_info_free:
 *     free all resources (memory) acquired when copying a TCModuleInfo
 *     structure into another one.
 *
 * Parameters:
 *     info: structure copied to be freed
 *
 * Return value:
 *     none
 *
 * Side effects:
 *     none
 *
 * Preconditions:
 *     'info' must be obtained as result of 'tc_module_info_copy' function,
 *     as 'dst' parameter. Applying this function to a structure obtained
 *     in a different way will cause an undefined behaviour, most likely
 *     a memory corruption or a crash
 *
 * Postconditions:
 *     all resources (memory) acquired by 'info' are released.
 *
 */
void tc_module_info_free(TCModuleInfo *info);

/*************************************************************************/

#endif  /* TCMODULEINFO_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
