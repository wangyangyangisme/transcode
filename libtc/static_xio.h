/*
 * static_xio.h - static linkage helper for (lib)xio.
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef STATIC_XIO_H
#define STATIC_XIO_H

#include "xio.h"
#include <fcntl.h>
#include <unistd.h>

void dummy_xio(void);
void dummy_xio(void)
{
	int i;
	struct stat tmp;

	i = xio_open("", O_RDONLY);
	xio_read(i, NULL, 0);
	xio_write(i, NULL, 0);
	xio_ftruncate(i, 0);
	xio_lseek(i, 0, 0);
	xio_fstat(i, &tmp);
	xio_lstat("", &tmp);
	xio_stat("", &tmp);
	xio_rename("", "");
	xio_close(i);
}

#endif /* STATIC_XIO_H */
