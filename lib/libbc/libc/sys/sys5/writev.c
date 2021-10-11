/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)writev.c	1.4	95/08/29 SMI"

#include "../common/compat.h"
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/uio.h>

/*
 * If writing to a utmp-like file, map the utmp structure to
 * new format on the fly.
 */
extern int conv2utmpx(char *, char *, int);
extern int conv2utmp(char *, char *, int);

int
writev(int fd, struct iovec *iov, int iovcnt)
{
	return (bc_writev(fd, iov, iovcnt));
}

int
bc_writev(int fd, struct iovec *iov, int iovcnt)
{
	int fds, ret, off;
	int nsize, total = 0;
	char *nbuf;
	int i;
	
	if ((fds = fd_get(fd)) != -1) {
		for (i = 0; i < iovcnt; i++) {
			nsize = getmodsize(iov[i].iov_len, sizeof (struct utmp),
							sizeof (struct utmpx));

			if ((nbuf = (void *)malloc(nsize)) == NULL) {
				fprintf(stderr, "writev: malloc failed\n");
				exit(-1);
			}
		
			(void) memset(nbuf, 0, nsize);

			ret = conv2utmpx(nbuf, iov[i].iov_base, iov[i].iov_len);

			if ((ret = _write(fds, nbuf, ret)) == -1) {
				free(nbuf);
				return (-1);
			}

			(void) memset(nbuf, 0, nsize);

			ret = conv2utmp(nbuf, iov[i].iov_base, iov[i].iov_len);

			if ((ret = _write(fd, nbuf, ret)) == -1) {
				free(nbuf);
				return (-1);
			}

			total += ret;

			free(nbuf);
	
			ret = getmodsize(ret, sizeof (struct compat_utmp), 
						sizeof (struct utmp));
		}
		total = getmodsize(total, sizeof (struct compat_utmp), 
					sizeof (struct utmp));
		return (total);

	}

	return (_writev(fd, iov, iovcnt));
}
