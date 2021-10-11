#ident	"@(#)util.c	1.2	95/09/07 SMI"
/*
 *	Copyright (c) 1995, Sun Microsystems, Inc.
 *	All Rights Reserved.
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1993
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * OSF/1 1.2
 *
 * Copyright 1986, Larry Wall
 *
 * This program may be copied as long as you don't try to make any
 * money off of it, or pretend that you wrote it.
 */

/*
 * File: util.c
 * Date: Sun Feb 12 18:48:18 PST 1995
 *
 * Description:
 *
 *	Common utility routines for patch.
 *
 * Modifications:
 * 	$Log$
 */

/*
 * Include files:
 */
#include "common.h"
#include <stdarg.h>


/*
 * Function: void fatal(char *, ...)
 *
 * Description:
 *
 *	Terminal output, pun intended.
 *	Printf message to stderr and die.
 *
 * Inputs:
 *	pat	-> Printf pattern string.
 *	...	-> other printf arguments
 *
 * Returns:
 *	It does not...
 */

void
fatal(char *pat, ...)
{
	va_list	ap;

	(void) fprintf(stderr, "patch: ");
	va_start(ap, pat);
	(void) vfprintf(stderr, pat, ap);
	va_end(ap);
	dont_sync = TRUE;
	cleanup();
	exit(ABORT_EXIT_VALUE);
}


/*
 * Function: void pfatal(char *, ...)
 *
 * Description:
 *
 *	Terminal output, pun intended.
 *	Printf message to stderr and call perror, then die.
 *
 * Inputs:
 *	pat	-> Printf pattern string.
 *	...	-> other printf arguments
 *
 * Returns:
 *	It does not...
 */

void
pfatal(char *pat, ...)
{
	va_list	ap;

	va_start(ap, pat);
	(void) vsprintf(buf, pat, ap);
	va_end(ap);

	(void) fprintf(stderr, "patch: ");
	perror(buf);
	dont_sync = TRUE;
	cleanup();
	exit(ABORT_EXIT_VALUE);
}


/*
 * Function: char *savestr(char_t *)
 *
 * Description:
 *
 *	Allocate a unique area for a string and report error if
 * 	memory can't be allocated...
 *
 * Inputs:
 *	s	-> A pointer to string to save.
 *
 * Returns:
 *	Address of saved string
 */

char *
savestr(char *s)
{
	char *rv;

	rv = strdup(s);
	if (rv == NULL) {
		pfatal(gettext("Memory allocation error"));
		/* NOTREACHED */
	}
	return (rv);
}


/*
 * Function: char *wsavestr(char_t *)
 *
 * Description:
 *
 *	Allocate a unique area for a wide string and report error if
 * 	memory can't be allocated...
 *
 * Inputs:
 *	s	-> A pointer to wide string to save.
 *
 * Returns:
 *	Address of saved wide string
 */

wchar_t *
wsavestr(wchar_t *s)
{
	wchar_t *rv;

	rv = wsdup(s);
	if (rv == NULL) {
		pfatal(gettext("Memory allocation error"));
		/* NOTREACHED */
	}
	return (rv);
}


/*
 * Function: void *allocate(size_t)
 *
 * Description:
 *
 *	Functionally equal to calloc but prints error message and dies
 *	if memory can't be allocated.
 *
 * Inputs:
 *	size	-> #of bytes to allocate
 *
 * Returns:
 *	Address of allocated memory
 */

void *
allocate(size_t size)
{
	void	*address = calloc(1, size);

	if (address == NULL) {
		pfatal(gettext("Memory allocation failure"));
		/* NOTREACHED */
	}
	return (address);
}


/*
 * Function: void *reallocate(size_t)
 *
 * Description:
 *
 *	Functionally equal to realloc but prints error message and dies
 *	if memory can't be reallocated.
 *
 * Inputs:
 *	address	-> Address of memory to reallocate
 *	size	-> #of bytes to reallocate
 *
 * Returns:
 *	Address of allocated memory
 */

void *
reallocate(void *address, size_t size)
{
	address = realloc(address, size);
	if (address == NULL) {
		pfatal(gettext("Memory reallocation failure"));
		/* NOTREACHED */
	}
	return (address);
}


/*
 * Function: void say(char *, ...)
 *
 * Description:
 *
 *	Vanilla terminal output (buffered).
 *	Shorthand for fprintf(stderr, pat, ...);
 *
 * Inputs:
 *	pat	-> Printf pattern string.
 *	...	-> other printf arguments
 */

void
say(char *pat, ...)
{
	va_list	ap;

	va_start(ap, pat);
	(void) vfprintf(stderr, pat, ap);
	va_end(ap);
}


/*
 * Function: char *ask(char *)
 *
 * Description:
 *
 *	Print a question and get a response from the user into buf,
 *	somehow or other.
 *
 * Inputs:
 *	pat	-> Printf pattern string.
 *	...	-> other printf arguments
 *
 * Returns:
 *	Answer to question in buf[].
 */


void
ask(char *pat, ...)
{
	va_list	ap;
	int ttyfd;
	int r;
	bool tty2 = isatty(2);

	va_start(ap, pat);
	(void) vsprintf(buf, pat, ap);
	va_end(ap);

	(void) fflush(stderr);
	(void) write(2, buf, strlen(buf));
	if (tty2) {			/* might be redirected to a file */
		r = read(2, buf, sizeof (buf));
	} else if (isatty(1)) {		/* this may be new file output */
		(void) fflush(stdout);
		(void) write(1, buf, strlen(buf));
		r = read(1, buf, sizeof (buf));
	} else if ((ttyfd = open("/dev/tty", 2)) >= 0 && isatty(ttyfd)) {
		/* might be deleted or unwriteable */
		(void) write(ttyfd, buf, strlen(buf));
		r = read(ttyfd, buf, sizeof (buf));
		(void) close(ttyfd);
	} else if (isatty(0)) {		/* this is probably patch input */
		(void) fflush(stdin);
		(void) write(0, buf, strlen(buf));
		r = read(0, buf, sizeof (buf));
	} else {			/* no terminal at all--default it */
		buf[0] = '\n';
		r = 1;
	}
	if (r <= 0)
		buf[0] = 0;
	else
		buf[r] = '\0';
	if (!tty2)
		say(buf);
}


/*
 * Function: void set_signals(int)
 *
 * Description:
 *
 * How to handle certain events.
 *
 * Inputs:
 *	None
 *
 * Returns:
 *	Nothing
 */

void
sig_handler(int sig)
{
	exit(3);
}

void
set_signals(void)
{
	(void) signal(SIGABRT, sig_handler);
	(void) signal(SIGALRM, sig_handler);
	(void) signal(SIGFPE, sig_handler);
	(void) signal(SIGHUP, sig_handler);
	(void) signal(SIGILL, sig_handler);
	(void) signal(SIGINT, sig_handler);
	(void) signal(SIGPIPE, sig_handler);
	(void) signal(SIGQUIT, sig_handler);
	(void) signal(SIGSEGV, sig_handler);
	(void) signal(SIGTERM, sig_handler);
	(void) signal(SIGUSR1, sig_handler);
	(void) signal(SIGUSR1, sig_handler);
}


/*
 * Function: void ignore_signals(void)
 *
 * Description:
 *
 *	How to handle certain events when in a critical region. (ignore them)
 */

void
ignore_signals(void)
{
	(void) signal(SIGABRT, SIG_IGN);
	(void) signal(SIGALRM, SIG_IGN);
	(void) signal(SIGFPE, SIG_IGN);
	(void) signal(SIGHUP, SIG_IGN);
	(void) signal(SIGILL, SIG_IGN);
	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGPIPE, SIG_IGN);
	(void) signal(SIGQUIT, SIG_IGN);
	(void) signal(SIGSEGV, SIG_IGN);
	(void) signal(SIGTERM, SIG_IGN);
	(void) signal(SIGUSR1, SIG_IGN);
	(void) signal(SIGUSR1, SIG_IGN);
}


/*
 * Function: char *fetchname(wchar_t, int, int)
 *
 * Description:
 *
 *	Process file name we get from patch file or user.
 *	Strip path components, check out from sccs or rcs if nescessary.
 *
 * Inputs:
 *	at		-> pointer to intial name.
 *	strip_leading	-> strip leading pathname components?
 *	assume_exists	-> check out from rcs/sccs if needed?
 *
 * Returns:
 *	Pointer to char * (ready for use by open)
 */

char *
fetchname(wchar_t *at, int strip_leading, int assume_exists)
{
	wchar_t	*s, *t, *n;
	char	*name;
	char	tmpbuf[200];

	if (!at)
		return (NULL);

	s = wsavestr(at);
	for (t = s; iswspace(*t); t++)
		;

	n = t;
	/* so files can be created by diffing */
	if (!wcsncmp(t, L"/dev/null", 9))
		return (NULL);			/*   against /dev/null. */
	for (; *t && !iswspace(*t); t++) {
		if (*t == '/') {
			if (--strip_leading >= 0)
				n = t+1;
		}
	}

	*t = '\0';
	if (n != s && *s != '/') {
		n[-1] = '\0';

		(void) wcstombs(tmpbuf, s, 199);
		tmpbuf[199] = 0;
		if (lstat(tmpbuf, &filestat) && filestat.st_mode & S_IFDIR) {
			n[-1] = '/';
			n = s;
		}
	}

	(void) wcstombs(tmpbuf, n, 199);
	name = savestr(tmpbuf);
	free(s);
	if (!assume_exists && lstat(name, &filestat) < 0) {
		(void) sprintf(tmpbuf, "RCS/%s%s", name, RCSSUFFIX);
		if (lstat(tmpbuf, &filestat) >= 0 ||
		    lstat(tmpbuf+4, &filestat) >= 0) {
			(void) sprintf(buf, CHECKOUT, name);
			if (verbose)
				say(gettext("Can't find %s--attempting to "
				    "check it out from RCS.\n"), name);
			if (system(buf) || lstat(name, &filestat) < 0) {
				say(gettext("Can't check out %s.\n"), name);
				return (NULL);
			}
			return (name);
		}
		(void) sprintf(tmpbuf, "SCCS/%s%s", SCCSPREFIX, name);
		if (lstat(tmpbuf, &filestat) >= 0 ||
		    lstat(tmpbuf+5, &filestat) >= 0) {
			(void) sprintf(buf, GET, name);
			if (verbose)
				say(gettext("Can't find %s--attempting"
				    " to get it from SCCS.\n"), name);
			if (system(buf) || lstat(name, &filestat) < 0) {
				say(gettext("Can't get %s.\n"), name);
				return (NULL);
			}
			return (name);
		}
		return (NULL);
	}
	return (name);
}


/*
 * Function: int rpmatch(char *)
 *
 * Description:
 *
 *	Internationalized get yes / no answer.
 *
 * Inputs:
 *	s	-> Pointer to answer to compare against.
 *
 * Returns:
 *	TRUE	-> Answer was affirmative
 *	FALSE	-> Answer was negative
 */

int
rpmatch(char *s)
{
	static char	*default_yesexpr = "^[Yy].*";
	static char	*compiled_yesexpr = (char *)NULL;

	/* Execute once to initialize */
	if (compiled_yesexpr == (char *)NULL) {
		char	*yesexpr;

		/* get yes expression according to current locale */
		yesexpr = nl_langinfo(YESEXPR);
		/*
		 * If the was no expression or if there is a compile error
		 * use default yes expression.  Anchor
		 */
		if ((yesexpr == (char *)NULL) || (*yesexpr == (char)NULL) ||
		    ((compiled_yesexpr =
		    regcmp(yesexpr, 0)) == NULL))
			compiled_yesexpr = regcmp(default_yesexpr, 0);
	}

	/* match yesexpr */
	if (regex(compiled_yesexpr, s) == NULL) {
		return (FALSE);
	}
	return (TRUE);
}
