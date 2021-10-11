/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_error.c	1.11	94/01/27 SMI"	/* SVr4.0 1.2	*/
#include <sys/stropts.h>
#include <tiuser.h>
#include <errno.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include "timt.h"

void
t_error(s)
char	*s;
{
	register char *c;
	register int n;

	trace1(TR_t_error, 0);
	c = t_strerror(t_errno);
	n = strlen(s);
	if (n) {
		(void) write(2, s, (unsigned)n);
		(void) write(2, ": ", 2);
	}
	(void) write(2, c, (unsigned)strlen(c));
	if (t_errno == TSYSERR) {
		(void) write(2, ": ", 2);
		perror("");
	} else
		(void) write(2, "\n", 1);
	trace1(TR_t_error, 1);
}
