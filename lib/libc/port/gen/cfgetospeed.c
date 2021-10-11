/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)cfgetospeed.c	1.8	95/01/09 SMI"	/* SVr4.0 1.1	*/

#ifdef __STDC__
	#pragma weak cfgetospeed = _cfgetospeed
#endif
#include "synonyms.h"
#include <sys/termios.h>

/*
 * returns output baud rate stored in c_cflag pointed by termios_p
 */

speed_t cfgetospeed(termios_p)
const struct termios *termios_p;
{
	return (termios_p->c_cflag & CBAUDEXT ?
			(termios_p->c_cflag & CBAUD) + CBAUD + 1 :
			termios_p->c_cflag & CBAUD);
}
