/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)read.c	1.6	95/08/29 SMI"

#include "../common/compat.h"
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <syscall.h>

/*
 * If reading from the utmp file, map the data to the SunOS 4.1
 * format on the fly. 
 */
extern int errno;

extern void to_utmp(char *, char *, int);

int
read(int fd, char *buf, int size)
{
	return (bc_read(fd, buf, size));
}

int
bc_read(int fd, char *buf, int size)
{
	int fds, ret, off;
	char *nbuf;
	
	if ((fds = fd_get(fd)) != -1) {
		size = getmodsize(size, sizeof (struct utmp), 
							sizeof (struct utmpx));

		if ((nbuf = (void *)malloc(size)) == NULL) {
			(void) fprintf(stderr, "read: malloc failed\n");
			exit(-1);
		}
		
		if ((ret = _read(fds, nbuf, size)) == -1) {
			if (errno == EAGAIN)
				errno = EWOULDBLOCK;
			free(nbuf);
			return (-1);
		}

		to_utmp(buf, nbuf, ret);

		off = getmodsize(ret, sizeof (struct utmpx), 
						sizeof (struct compat_utmp));

		(void) _syscall(SYS_lseek, fd, off, SEEK_CUR);

		ret = getmodsize(ret, sizeof (struct utmpx), 
						sizeof (struct utmp));
		free(nbuf);
		return (ret);
	}

	if ((ret = _read(fd, buf, size)) == -1) {
		if (errno == EAGAIN)
			errno = EWOULDBLOCK;
	}
	return (ret);
}

void
to_utmp(char *buf, char *nbuf, int len)
{
	struct utmp *ut;
	struct utmpx *utx;

	utx = (struct utmpx *) nbuf;
	ut  = (struct utmp  *) buf;

	while ((char *)utx < (nbuf + len)) {
		(void) strncpy(ut->ut_line, utx->ut_line, sizeof (ut->ut_line));
                (void) strncpy(ut->ut_name, utx->ut_user, sizeof (ut->ut_name));
                (void) strncpy(ut->ut_host, utx->ut_host, sizeof (ut->ut_host));
                ut->ut_time = utx->ut_tv.tv_sec;
                utx++;
                ut++;
        }
}
