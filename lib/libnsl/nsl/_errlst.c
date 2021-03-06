/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_errlst.c	1.15	94/01/27 SMI"	/* SVr4.0 1.3	*/

#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include "timt.h"
#undef t_errno

/*
 * transport errno
 */

int t_errno = 0;

int t_nerr = 19;

/*
 * transport interface error list
 */

const char *t_errlist[] = {
	"No Error",					/*  0 */
	"Incorrect address format",			/*  1 */
	"Incorrect options format",			/*  2 */
	"Illegal permmissions",				/*  3 */
	"Illegal file descriptor",			/*  4 */
	"Couldn't allocate address",			/*  5 */
	"Routine will place interface out of state",    /*  6 */
	"Illegal called/calling sequence number",	/*  7 */
	"System error",					/*  8 */
	"An event requires attention",			/*  9 */
	"Illegal amount of data",			/* 10 */
	"Buffer not large enough",			/* 11 */
	"Can't send message - (blocked)",		/* 12 */
	"No message currently available",		/* 13 */
	"Disconnect message not found",			/* 14 */
	"Unitdata error message not found",		/* 15 */
	"Incorrect flags specified",			/* 16 */
	"Orderly release message not found",		/* 17 */
	"Primitive not supported by provider",		/* 18 */
	"State is in process of changing",		/* 19 */
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	""
	/*
	 *	N.B.:  t_errlist must not expand beyond this point or binary
	 *	compatibility will be broken.  When necessary to accomodate
	 *	more error strings, they may only be added to the list printed
	 *	by t_strerror(), q.v..  Currently, t_strerror() conserves space
	 *	by pointing into t_errlist[].  To expand beyond 57 errors, it
	 *	will be necessary to change t_strerror() to use a different
	 *	array.
	 */
};


int *
__t_errno()
{
	static thread_key_t t_errno_key = 0;
	register int *ret;

	if (_thr_main())
		return (&t_errno);
	ret = (int *)_t_tsdalloc(&t_errno_key, sizeof (int));
	/* if _t_tsdalloc fails we return the address of t_errno */
	return (ret ? ret : &t_errno);
}
