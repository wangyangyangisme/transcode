/*
 * export_profile.h -- transcode export profile support code - interface
 * (C) 2006-2010 - Francesco Romani <fromani at gmail dot com>
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


#ifndef EXPORT_PROFILE_H
#define EXPORT_PROFILE_H

#include "transcode.h"
#include "tcinfo.h"

/*
 * GENERAL WARNING: none of those functions
 * are intended to be thread-safe
 */

/*
 * tc_setup_export_profile:
 *     determine the export profile(s) to load later, by extracting
 *     informations by command line options (argc\argv).
 *     In more detail, this function handles '--export_prof PROFILE'
 *     option.
 *     Also removes used option by options array, so later processing
 *     of used option is easier.
 *
 * Parameters:
 *     argc: POINTER to integer representing the number of items in argv
 *           array.
 *     argv: POINTER to array of C-string representing option keys
 *           and values.
 * Return value:
 *     -2: internal error
 *     -1: bad parameters (== NULL)
 *      0: bad option value
 *     >0: succesfull, and return value is number of profile parsed.
 * Side effects:
 *     if operation is succesfull AND if user provided (valid) --export_prof
 *     option, both option and it's argument are removed from argv vector,
 *     so *TWO* items of argv vector will be NULL-ified, and argc is
 *     decreased by two.
 *     This function also trasparently set some internal variables.
 * Preconditions:
 *     argc != NULL
 *     argv != NULL
 */
int tc_setup_export_profile(int *argc, char ***argv);

/*
 * tc_setup_export_profile:
 *      release all resources acquired by tc_setup_export_profile.
 *
 * Parameters:
 *      None.
 * Return vaule:
 *      None
 */
void tc_cleanup_export_profile(void);

/*
 * tc_load_export_profile:
 *      sequentially load all profiles recognized using
 *      tc_setup_export_profile, so if two or more profile specifies
 *      a value for an option, the later will prevail.
 *
 * Parameters:
 *      None
 * Return value:
 *      if succesfull, return a pointer to a TCEXportInfo structure
 *      intialized with sensible defaults and containing the values
 *      set by loaded profile(s). There is no need to free() returned
 *      structure, it's handled internally.
 *      If an error happens, return NULL, and tc_log*() reason
 *      (see side effects below).
 * Side effects:
 *      if verbose value is >= TC_DEBUG *AND* a profile can
 *      be loaded, tc_log'd out the unavalaible profile.
 *      if verbose value is >= TC_INFO, tc_log out every loaded
 *      profile.
 */
const TCExportInfo *tc_load_export_profile(void);

/*
 * tc_export_profile_to_vob:
 *      translate values stored in a TCExportInfo structure into
 *      a vob_t structure, doing the needed adaptations.
 *      This function ignore bad (or unreproducible, even if it's
 *      very unlikely) values/combination sotre in TCExportInfo
 *      structures reporting errors using tc_log*.
 *
 * Parameters:
 *      info: pointer to TCExportInfo to translate
 *       vob: pointer to vob_t storing translated values.
 * Return value:
 *      None
 * Side effects:
 *      tc_log*() is used internally.
 */
void tc_export_profile_to_vob(const TCExportInfo *info, vob_t *vob);

#endif /* EXPORT_PROFILE_H */
