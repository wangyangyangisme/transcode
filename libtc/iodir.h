/*
 *  iodir.h
 *
 *  Copyright (C) Thomas Oestreich - June 2001
 *  Updates:
 *  Copyright (C) Francesco Romani - November 2005
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

#include "config.h"

#ifndef IODIR_H
#define IODIR_H

#include <dirent.h>

typedef struct tcdirlist_ TCDirList;
struct tcdirlist_ {
    DIR *dir;	/* for internal use */

    const char *dir_name; /* saved base path */
    const char *path_sep; /* optional *nix path separator */

    char filename[PATH_MAX + 2];
    /*
     * full path of file actually under focus + optional separator +
     * final string terminator
     */
    char **entries;
    /* array of full PATHs of files in scanned dirlist */

    int nfiles; /* (current) number of files in dirlist */
    int findex; /* index of file under focus */
    int buffered;
    /* boolean flag: above array of file in directory is valid? */
};

/*
 * tc_dirlist_open:
 *     initialize a TCDirList descriptor.
 *     every TCDirList descriptor refers to a specific directory in
 *     filesystem, and more descriptors can refer to the same directory.
 *     BIG FAT WARNING:
 *     all iodir code relies on assumption that target directory
 *     *WILL NOT CHANGE* when referring descriptor is active.
 *
 * Parameters:
 *     tcdir: TCDirList structure (descriptor) to initialize.
 *     dirname: full path of target directory.
 *     sort: boolean flag. If is !0, use buffered mode.
 *           further calls to tc_dirlist_scan will return
 *           directory entries in lexicographical order.
 *           otherwise use unbuffered mode.
 * Return Value:
 *     -1 if some parameter is wrong or if target
 *        directory can't be opened.
 *     0  succesfull.
 * Side effects:
 *     none
 * Preconditions:
 *     referred directory *MUST NOT CHANGE* until descriptor
 *     will be closed via tc_dirlist_close().
 * Postconditions:
 *     none
 */
int tc_dirlist_open(TCDirList *tcdir, const char *dirname, int sort);

/*
 * tc_dirlist_scan:
 *     give full path of next entry in target directory. This function
 *     can operate in two modes, returning the same values to caller
 *     (if preconditions holds) but in different order.
 *     The first, standard mode is the so called 'unbuffered' mode.
 *     In this mode, this function simply scan the target directory, build
 *     the full path for each entry and return to the caller in filesystem
 *     order. The other operating mode is the 'buffered' mode, and
 *     it's triggered using a non-zero value for parameter 'sort' in
 *     tc_dirlist_open (see above). When in buffered mode, this function
 *     will return the full path of each entry in target directory in
 *     lexicogrpaphical order. Otherwise full path is given using
 *     filesystem order.
 *
 * Parameters:
 *     tcdir: TCDirList structure (descriptor) to use.
 * Return Value:
 *     a constant pointer to full path of next entry NULL there are no
 *     more entries, or if an internal error occurs.
 * Side effects:
 *     in unbuffered mode, target directory will be scanned one time.
 * Preconditions:
 *     referred directory *MUST NOT CHANGE* until descriptor
 *     will be closed via tc_dirlist_close().
 *     'tcdir' was initialized calling tc_dirlist_open().
 * Postconditions:
 *     none
 */
const char *tc_dirlist_scan(TCDirList *tcdir);

/*
 * tc_dirlist_close:
 *     finalize a TCDirList structure (descriptor), freeing all
 *     acquired resources.
 *
 * Parameters:
 *     tcdir: TCDirList structure (descriptor) to close.
 * Return Value:
 *     none
 * Side effects:
 *     none
 * Preconditions:
 *     referred directory *MUST NOT BE CHANGED* until now.
 *     'tcdir' was initialized calling tc_dirlist_open()
 * Postconditions:
 *     none
 */
void tc_dirlist_close(TCDirList *tcdir);

/*
 * tc_dirlist_file_count:
 *     return the actual count of files in target directory.
 *
 * Parameters:
 *     tcdir: TCDirList structure (descriptor) to use.
 * Return Value:
 *     actual count of files in target directory
 *     -1 if 'tcdir' is an invalid descriptor
 * Side effects:
 *     none
 * Preconditions:
 *     referred directory *MUST NOT CHANGE* until descriptor will be closed
 *     via tc_dirlist_close().
 *     'tcdir' was initialized calling tc_dirlist_open().
 * Postconditions:
 *     none
 */
int tc_dirlist_file_count(TCDirList *tcdir);

#endif /* IODIR_H */
