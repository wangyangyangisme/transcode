/*
 * test-pvmparser -- testsuite for PVM3 configuration parser; 
 *                   everyone feel free to add more tests and improve
 *                   existing ones.
 * (C) 2007-2010 - Francesco Romani <fromani -at- gmail -dot- com>
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "config.h"
#include "libtc/libtc.h"
#include "libtc/cfgfile.h"

#include "pvm3/pvm_parser.h"


int main(int argc, char *argv[])
{
    int full = TC_FALSE;
    pvm_config_env *env = NULL;
    
    if (argc != 2) {
        fprintf(stderr, "(%s) usage: %s pvm.cfg\n", __FILE__, argv[0]);
        exit(1);
    }
    if (argc == 3 && !strcmp(argv[2], "full")) {
        full = TC_TRUE;
    }
    env = pvm_parser_open(argv[1], TC_TRUE, full);

    pvm_parser_close();

    return 0;
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
