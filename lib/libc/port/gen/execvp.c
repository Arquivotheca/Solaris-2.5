/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)execvp.c	1.16	95/10/02 SMI"	/* SVr4.0 1.20	*/

/*LINTLIBRARY*/
/*
 *	execlp(name, arg,...,0)	(like execl, but does path search)
 *	execvp(name, argv)	(like execv, but does path search)
 */
#ifdef __STDC__
	#pragma weak execlp = _execlp
	#pragma weak execvp = _execvp
#endif
#include "synonyms.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#define	NULL	0

extern  int __xpg4;	/* defined in xpg4.c; 0 if not xpg4-compiled program */

static const char *execat();
extern char *getenv(), *strchr();
extern unsigned sleep();
extern int execv(), execvp();

/*VARARGS1*/
int
#ifdef __STDC__
execlp(char *name, ...)
#else
execlp(name, va_alist) char *name; va_dcl
#endif
{
	va_list args;

#ifdef __STDC__
	va_start(args,);
#else
	va_start(args);
#endif
	return(execvp(name, (char **)args));
}

int
execvp(name, argv)
char	*name, *const *argv;
{
	const char	*pathstr;
	char	fname[PATH_MAX+2];
	const char	*newargs[256];
	int	i;
	register const char	*cp;
	register unsigned etxtbsy=1;
	register int eacces=0;
	char *shpath, *shell;

	if (*name == '\0') {
		errno = ENOENT;
		return(-1);
	}
	if((pathstr = getenv("PATH")) == NULL) {
		/*
		 * XPG4:  pathstr is equivalent to CSPATH, except that
		 * :/usr/sbin is appended when root, and pathstr must end
		 * with a colon when not root.  Keep these paths in sync
		 * with CSPATH in confstr.c.  Note that pathstr must end
		 * with a colon when not root so that when name doesn't
		 * contain '/', the last call to execat() will result in an
		 * attempt to execv name from the current directory.
		 */
		if (geteuid() == 0 || getuid() == 0) {
			if (__xpg4 == 0) {	/* not XPG4 */
				pathstr = "/usr/sbin:/usr/ccs/bin:/usr/bin";
			} else {		/* XPG4 (CSPATH + /usr/sbin) */
		pathstr = "/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:/usr/sbin";
			}
		} else {
			if (__xpg4 == 0) {	/* not XPG4 */
				pathstr = "/usr/ccs/bin:/usr/bin:";
			} else {		/* XPG4 (CSPATH) */
				pathstr = "/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:";
			}
		}
	}
	cp = strchr(name, '/')? (const char *)"": pathstr;

	do {
		cp = execat(cp, name, fname);
	retry:
		(void) execv(fname, argv);
		switch(errno) {
		case ENOEXEC:
			if (__xpg4 == 0) {	/* not XPG4 */
				shpath = "/bin/sh";
				shell = "sh";
			} else {
				/* XPG4 */
				shpath = "/bin/ksh";
				shell = "ksh";
			}
			newargs[0] = shell;
			newargs[1] = fname;
			for(i=1; newargs[i+1]=argv[i]; ++i) {
				if(i >= 254) {
					errno = E2BIG;
					return(-1);
				}
			}
			(void) execv((const char *)shpath, newargs);
			return(-1);
		case ETXTBSY:
			if(++etxtbsy > 5)
				return(-1);
			(void) sleep(etxtbsy);
			goto retry;
		case EACCES:
			++eacces;
			break;
		case ENOMEM:
		case E2BIG:
		case EFAULT:
			return(-1);
		}
	} while(cp);
	if(eacces)
		errno = EACCES;
	return(-1);
}

static const char *
execat(s1, s2, si)
register const char *s1, *s2;
char	*si;
{
	register char	*s;
	register int cnt = PATH_MAX + 1; /* number of characters in s2 */

	s = si;
	while(*s1 && *s1 != ':') {
		if (cnt > 0) {
			*s++ = *s1++;
			cnt--;
		} else
			s1++;
	}
	if(si != s && cnt > 0) {
		*s++ = '/';
		cnt--;
	}
	while(*s2 && cnt > 0) {
		*s++ = *s2++;
		cnt--;
	}
	*s = '\0';
	return(*s1? ++s1: 0);
}
