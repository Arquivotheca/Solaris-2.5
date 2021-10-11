/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* 	Portions Copyright(c) 1988, Sun Microsystems Inc.	*/
/*	All Rights Reserved					*/

#ident	"@(#)gettimeofday.c	1.8	94/05/17 SMI"	/* SVr4.0 1.1	*/

#include <sys/time.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/syscall.h>

extern int stime();

/*
 * Get the time of day information.
 * BSD compatibility on top of SVr4 facilities:
 * u_sec always zero, and don't do anything with timezone pointer.
 */
int
gettimeofday(tp, tzp)
	struct timeval *tp;
	void *tzp;
{
	long		rval;
	
#ifdef lint
        tzp = tzp;
#endif
        
        if (tp == NULL) 
                return (0);
	
	return(_gettimeofday(tp));
}

/*
 * Set the time.
 * Don't do anything with the timezone information.
 */
int
settimeofday(tp, tzp)
	struct timeval *tp;
	void *tzp;
{
        time_t t;                       /* time in seconds */
	
#ifdef lint
        tzp = tzp;
#endif
        if (tp == NULL)
                return (0);
	
        t = (time_t) tp->tv_sec;
        if (tp->tv_usec >= 500000)
                /* round up */
                t++;
	
        return(stime(&t));
}
