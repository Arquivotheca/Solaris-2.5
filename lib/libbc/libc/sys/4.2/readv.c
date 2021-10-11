/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)readv.c	1.4	95/08/29 SMI"

#include "../common/compat.h"
#include <stdio.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/uio.h>

extern int errno;

/*
 * If reading from the utmp file, map the data to the SunOS 4.1
 * format on the fly. 
 */
extern void to_utmp(char *, char *, int);

int
readv(int fd, struct iovec *iov, int iovcnt)
{
	return (bc_readv(fd, iov, iovcnt));
}

int
bc_readv(int fd, struct iovec *iov, int iovcnt)
{
	int fds, ret, off;
	int i, size, total = 0;
	char *nbuf;
	
	if ((fds = fd_get(fd)) != -1) {
		for (i = 0; i < iovcnt; i++) {
			size = getmodsize(iov[i].iov_len, sizeof (struct utmp),
							sizeof (struct utmpx));

			if ((nbuf = (void *)malloc(size)) == NULL) {
				fprintf(stderr, "readv: malloc failed\n");
				exit(-1);
			}
		
			if ((ret = _read(fds, nbuf, size)) == -1) {
				if (errno == EAGAIN)
					errno = EWOULDBLOCK;	
				free(nbuf);
				return (-1);
			}

			total += ret;

			to_utmp(iov[i].iov_base, nbuf, ret);

			off = getmodsize(ret, sizeof (struct utmpx), 
						sizeof (struct compat_utmp));

			(void) _syscall(SYS_lseek, fd, off, SEEK_CUR);

			ret = getmodsize(ret, sizeof (struct utmpx), 
						sizeof (struct utmp));
			free(nbuf);
		}

		return (total);
	}

	if ((ret = _readv(fd, iov, iovcnt)) == -1) {
		if (errno == EAGAIN)
			errno = EWOULDBLOCK;
	}
	return (ret);
}
