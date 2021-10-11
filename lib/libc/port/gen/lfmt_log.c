/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)lfmt_log.c	1.4	93/12/01 SMI"

/* lfmt_log() - log info */
#include "synonyms.h"
#include <pfmt.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <fcntl.h>
#include <errno.h>
#include <thread.h>
#include "pfmt_data.h"
#include <time.h>

#define	MAXMSG	1024
#define	LOGNAME		"/dev/conslog"
#define	LOG_CONSOLE	"/dev/console"

__lfmt_log(text, sev, args, flag, ret)
const char *text, *sev;
va_list args;
long flag;
int ret;
{
	static int fd = -1;
	struct strbuf dat, ctl;
	int msg_offset = 0;
	int len;
	char msgbuf[MAXMSG];
	int err;
	int fdd;

	len = ret + sizeof (long) + 3;

	if (len > sizeof (msgbuf)) {
		errno = ERANGE;
		return (-2);
	}

	*(long *)msgbuf = flag;
	msg_offset = sizeof (long);

	rw_rdlock(&_rw_pfmt_label);
	if (*__pfmt_label)
		msg_offset += sprintf(msgbuf + msg_offset, __pfmt_label);
	rw_unlock(&_rw_pfmt_label);

	if (sev)
		msg_offset += sprintf(msgbuf + msg_offset, sev, flag & 0xff);

	msg_offset += 1 + vsprintf(msgbuf + msg_offset, text, args);
	msgbuf[msg_offset++] = '\0';

	if (fd == -1 &&
		((fd = open(LOGNAME, O_WRONLY)) == -1 || fcntl(fd, F_SETFD, 1) == -1))
		return (-2);

	dat.maxlen = MAXMSG;
	dat.len = msg_offset;
	dat.buf = msgbuf;

	if (putmsg(fd, 0, &dat, 0) == -1) {
		close(fd);
		return (-2);
	}

	/*
	 *  Display it to a console
	 */
	if ((flag & MM_CONSOLE) != 0) {
		char *p;
		time_t t;
		char buf[128];
		struct tm *tm;
		err = errno;
		fdd = open(LOG_CONSOLE, O_WRONLY);
		if (fdd != -1) {
			/*
			 * Use C locale for time stamp.
			 */
			(void) time(&t);
			sprintf(buf, ctime(&t));
			p = (char *)strrchr(buf, '\n');
			if (p != NULL)
				*p = ':';
			write(fdd, buf, strlen(buf));
			write(fdd, msgbuf+sizeof (long), msg_offset-sizeof (long));
			write(fdd, "\n", 1);
		} else
			return (-2);
		close(fdd);
		errno = err;
	}
	return (ret);
}
