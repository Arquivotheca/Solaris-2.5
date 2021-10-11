/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)tcsendbreak.c	1.9	93/02/26 SMI"	/* SVr4.0 1.3	*/

#ifdef __STDC__
	#pragma weak tcsendbreak = _tcsendbreak
#endif
#include "synonyms.h"
#include <sys/types.h>
#include <sys/termios.h>

/* 
 * send zeros for 0.25 seconds, if duration is 0
 * If duration is not 0, ioctl(fildes, TCSBRK, 0) is used also to
 * make sure that a break is sent. This is for POSIX compliance.
 */

/*ARGSUSED*/
int tcsendbreak (fildes, duration)
int fildes;
int duration;
{
	return(ioctl(fildes,TCSBRK,duration));
}
