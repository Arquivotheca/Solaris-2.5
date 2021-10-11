/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)settimeofday.c	1.11	94/05/16 SMI"	/* SVr4.0 1.3	*/

#pragma weak settimeofday = _settimeofday

#include "synonyms.h"
#include <sys/time.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>

extern int stime();

/*
 * Set the time.
 * Don't do anything with the timezone information.
 */
int
settimeofday(struct timeval *tp, void *tzp)
{
        time_t t;                       /* time in seconds */

        if (tp == NULL)
                return (0);

	if (tp->tv_sec < 0 || tp->tv_usec < 0 ||
		tp->tv_usec >= MICROSEC) {
		errno = EINVAL;
		return(-1);
	}

        t = (time_t) tp->tv_sec;
        if (tp->tv_usec >= (MICROSEC/2))
                /* round up */
                t++;

        return(stime(&t));
}
