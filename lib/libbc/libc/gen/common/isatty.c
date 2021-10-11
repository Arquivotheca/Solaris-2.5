#pragma ident	"@(#)isatty.c	1.2	92/07/20 SMI"  /* from S5R3 1.7 */
/*	Copyright (c) 1984 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*LINTLIBRARY*/
/*
 * Returns 1 iff file is a tty
 */
#include <sys/termio.h>

extern int ioctl();
extern int errno;

int
isatty(f)
int	f;
{
	struct termio tty;
	int err ;

	err = errno;
	if(ioctl(f, TCGETA, &tty) < 0)
	{
		errno = err; 
		return(0);
	}
	return(1);
}
