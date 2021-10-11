/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)popen.c	1.19	95/04/06 SMI"	/* SVr4.0 1.29	*/

/*LINTLIBRARY*/
#pragma weak pclose = _pclose
#pragma weak popen = _popen

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#define	tst(a, b) (*mode == 'r'? (b) : (a))
#define	RDR	0
#define	WTR	1

#define	BIN_SH "/bin/sh"
#define	BIN_KSH "/bin/ksh"
#define	SH "sh"
#define	KSH "ksh"
#define	SHFLG "-c"

extern	int __xpg4;	/* defined in _xpg4.c; 0 if not xpg4-compiled program */

static int *popen_pid;
#ifdef _REENTRANT
static mutex_t popen_lock = DEFAULTMUTEX;
#endif _REENTRANT

FILE *
popen(cmd, mode)
const char	*cmd, *mode;
{
	int	p[2];
	register int *poptr;
	register int myside, yourside, pid;

	if (popen_pid == NULL) {
		_mutex_lock(&popen_lock);
		if (popen_pid == NULL) {
			if ((popen_pid = (int *)calloc(256, sizeof (int)))
					== NULL) {
				_mutex_unlock(&popen_lock);
				return (NULL);
			}
		}
		_mutex_unlock(&popen_lock);
	}

	if (pipe(p) < 0)
		return (NULL);
	myside = tst(p[WTR], p[RDR]);
	yourside = tst(p[RDR], p[WTR]);
	if ((pid = vfork()) == 0) {
		/* myside and yourside reverse roles in child */
		int	stdio;

		/* close all pipes from other popen's */
		for (poptr = popen_pid; poptr < popen_pid+256; poptr++) {
			if (*poptr)
				close(poptr - popen_pid);
		}
		stdio = tst(0, 1);
		(void) close(myside);
		if (yourside != stdio) {
			(void) close(stdio);
			(void) fcntl(yourside, F_DUPFD, stdio);
			(void) close(yourside);
		}

		if (__xpg4 == 0) {	/* not XPG4 */
			if (access(BIN_SH, X_OK))
				_exit(127);
			(void) execl(BIN_SH, SH, SHFLG, cmd, (char *)0);
		} else {
			if (access(BIN_KSH, X_OK))	/* XPG4 Requirement */
				_exit(127);
			(void) execl(BIN_KSH, KSH, SHFLG, cmd, (char *)0);
		}
		_exit(1);
	}
	if (pid == -1)
		return (NULL);
	popen_pid[myside] = pid;
	(void) close(yourside);
	return (fdopen(myside, mode));
}

int
pclose(ptr)
FILE	*ptr;
{
	register int f;
	int status;

	if (!popen_pid)
		return (-1);

	f = fileno(ptr);
	(void) fclose(ptr);
	while (waitpid(popen_pid[f], &status, 0) < 0) {
		/* If waitpid fails with EINTR, restart the waitpid call */
		if (errno != EINTR) {
			status = -1;
			break;
		}
	}
	/* mark this pipe closed */
	popen_pid[f] = 0;
	return (status);
}
