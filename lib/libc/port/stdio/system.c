/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)system.c	1.14	95/04/06 SMI"	/* SVr4.0 1.21	*/

/*	3.0 SID #	1.4	*/
/*LINTLIBRARY*/
#ifdef FOO
#include "synonyms.h"
#endif FOO
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread.h>
#include <errno.h>
#include <synch.h>
#include "mtlib.h"

extern  int __xpg4;     /* defined in _xpg4.c; 0 if not xpg4-compiled program */

static struct sigaction ignore={
	0,
	SIG_IGN,
	0
};

static struct sigaction defalt={
	0,
	SIG_DFL,
	0
};


int
system(s)
const char *s;
{
	int	status, pid, w;
	struct sigaction ibuf, qbuf, cbuf;
	sigset_t savemask;
	struct stat buf;
	char *shpath, *shell;

	if (__xpg4 == 0) {	/* not XPG4 */
		shpath = "/bin/sh";
		shell = "sh";
	} else {
		/* XPG4 */
		shpath = "/bin/ksh";
		shell = "ksh";
	}
	if (s == NULL) {
		if (stat(shpath, &buf) != 0) {
			return(0);
		} else if (getuid() == buf.st_uid) {
			if ((buf.st_mode & 0100) == 0)
				return(0);
		} else if (getgid() == buf.st_gid) {
			if ((buf.st_mode & 0010) == 0)
				return(0);
		} else if ((buf.st_mode & 0001) == 0) {
			return(0);
		}
		return(1);
	}
		
	if((pid = vfork()) == 0) {
		(void) execl(shpath, shell, (const char *)"-c", s, (char *)0);
		_exit(127);
	}

	(void) sigaction(SIGINT, &ignore, &ibuf);
 	(void) sigaction(SIGQUIT, &ignore, &qbuf);

        sigaddset(&ignore.sa_mask,SIGCHLD);
        sigprocmask(SIG_BLOCK,&ignore.sa_mask,&savemask);
	
 	(void) sigaction(SIGCLD, &defalt, &cbuf);

	do {
		w = waitpid(pid, &status, 0);
	} while (w == -1 && errno == EINTR);

 	(void) sigaction(SIGINT, &ibuf, NULL);
 	(void) sigaction(SIGQUIT, &qbuf, NULL);
 	(void) sigaction(SIGCLD, &cbuf, NULL);
	sigprocmask(SIG_SETMASK,&savemask,NULL);

	return((w == -1)? w: status);
}
