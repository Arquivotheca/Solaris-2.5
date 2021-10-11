/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ident	"@(#)confstr.c	1.3	95/08/16 SMI"

#include <unistd.h>
#include <errno.h>
#include <string.h>

/* When changing CSPATH, also update the default XPG4 paths in execvp.c. */
#define	CSPATH	"/usr/xpg4/bin:/usr/ccs/bin:/usr/bin\0"

size_t
confstr(int name, char *buf, size_t len)
{
	int	conflen;

	switch (name) {
		case _CS_PATH:
			conflen = strlen(CSPATH) + 1;
			if (len != 0) {
				if (conflen <= len)
					strcpy(buf, CSPATH);
				else {
					strncpy(buf, CSPATH, (len - 1));
					buf[len-1] = '\0';
				}
			}
			return (conflen);
		default:
			errno = EINVAL;
			return (0);
	}
}
