/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)time_gdata.c	1.6	93/05/27 SMI"	/* SVr4.0 1.4	*/

#ifdef __STDC__
	#pragma weak altzone = _altzone
	#pragma weak daylight = _daylight
	#pragma weak timezone = _timezone
	#pragma weak tzname = _tzname
#endif
#include	"synonyms.h"
#include	<sys/types.h>
#include 	<time.h>
#include	<synch.h>
#include 	<mtlib.h>

time_t	timezone = 0;
time_t	altzone = 0;
int 	daylight = 0;
char 	*tzname[] = {"GMT", "   "};
#ifdef _REENTRANT
mutex_t _time_lock = DEFAULTMUTEX;
#endif _REENTRANT
