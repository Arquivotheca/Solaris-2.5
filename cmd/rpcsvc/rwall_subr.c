#pragma ident	"@(#)rwall_subr.c	1.7	93/05/14 SMI"

/*
 * rwall_subr.c
 *	The server procedure for rwalld
 *
 * Copyright (c) 1984 by Sun Microsystems, Inc.
 */

#include <rpc/rpc.h>
#include <stdio.h>
#include <rpcsvc/rwall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>

#define	WALL_PROG	"/usr/sbin/wall"

static char *oldmsg;
extern char *strdup();

void *
wallproc_wall_1(argp, clnt)
	wrapstring *argp;
	CLIENT *clnt;
{
	static char res;
	char *msg;
	FILE *fp;
	int rval;
	struct stat wall;

	msg = *argp;
	if ((oldmsg == (char *) 0) || (strcmp (msg, oldmsg))) {
		rval = stat(WALL_PROG, &wall);
		if (rval != -1 && (wall.st_mode & S_IXUSR)) {
			fp = popen(WALL_PROG, "w");
			if (fp != NULL) {
				fprintf(fp, "%s", msg);
				(void) pclose(fp);
				goto cleanup;
			}
		}
#ifdef	DEBUG
fprintf(stderr, "rwall message received but could not execute %s", WALL_PROG);
fprintf(stderr, msg);
#endif
		syslog(LOG_NOTICE,
	"rwall message received but could not execute %s", WALL_PROG);
		syslog(LOG_NOTICE, msg);
cleanup:
		if (oldmsg)
			free(oldmsg);
		oldmsg = strdup(msg);
	}
	return ((void *)&res);
}
