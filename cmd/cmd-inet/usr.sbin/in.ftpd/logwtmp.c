/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)logwtmp.c	1.8	93/09/28 SMI"	/* SVr4.0 1.2	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 * 
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 * 
 * 
 * 
 * 		Copyright Notice 
 * 
 * Notice of copyright on this source code product does not indicate 
 * publication.
 * 
 * 	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 	          All rights reserved.
 *  
 */


#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <utmp.h>
#ifdef SYSV
#include <utmpx.h>
#include <sac.h>        /* for SC_WILDC */
#endif /* SYSV */
#include <fcntl.h>

#include <syslog.h>
#include <security/ia_switch.h>

#include <security/ia_appl.h>
extern  void    *iad;

static int fd;

#ifdef SYSV

unsigned char ut_id[4] = { 'f', 't', 'p', (unsigned char) SC_WILDC };
 
/*
 * Since logwtmp is only called in two places, on log in and on logout
 * and since on logout the 2nd parameter is NULL we check for that to
 * to determine if we are starting or ending a session. Rather gross,
 * but it allows us to avoid changing a bunch of other code.
 */
 
 
void
logwtmp(line, name, host)
        char *line, *name, *host;
{
        struct ia_status        out;
        int uid;
 
        if (name == NULL)
                return;
 
        uid = geteuid();
        seteuid(0);
 
        if (name[0] == '\0')
		(void)ia_close_session(iad, IS_NOLOG, getpid(), 0,
				(char *)ut_id, &out);
        else
		if (ia_open_session(iad, IS_NOLOG, USER_PROCESS,
				(char *)ut_id, &out) != IA_SUCCESS)
			exit(1);
 
        seteuid(uid);
 
}
#else
logwtmp(line, name, host)
	char *line, *name, *host;
{
	struct utmp ut;
	struct stat buf;
	time_t time();
	char *strncpy();

	if (!fd && (fd = open(WTMP_FILE, O_WRONLY|O_APPEND, 0)) < 0)
		return;
	if (!fstat(fd, &buf)) {
		(void)strncpy(ut.ut_line, line, sizeof(ut.ut_line));
		(void)strncpy(ut.ut_name, name, sizeof(ut.ut_name));
		(void)strncpy(ut.ut_host, host, sizeof(ut.ut_host));
		(void)time(&ut.ut_time);
		if (write(fd, (char *)&ut, sizeof(struct utmp)) !=
		    sizeof(struct utmp))
			(void)ftruncate(fd, buf.st_size);
	}
}
#endif /* SYSV */
