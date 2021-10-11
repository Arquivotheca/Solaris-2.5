/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mktemp.c	1.9	92/09/17 SMI"	/* SVr4.0 1.16	*/

/*LINTLIBRARY*/
/****************************************************************
 *	Routine expects a string of length at least 6, with
 *	six trailing 'X's.  These will be overlaid with a
 *	letter and the last (5) symbols of the proccess ID.
 *	If every letter (a thru z) thus inserted leads to
 *	an existing file name, your string is shortened to
 *	length zero upon return (first character set to '\0').
 ***************************************************************/
#define XCNT  6
#ifdef __STDC__
	#pragma weak mktemp = _mktemp
#endif
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include "synonyms.h"
#include "shlib.h"
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

/*
 * mktemp() needs to be changed for stdio since tmpnam() and tempnam()
 * which are part of the stdio interface, call mktemp(). In *any case*,
 * mktemp() needs to be modified for use in a multi-threaded program.
 * Also, note that just as in the single-threaded case, it is bad
 * practice for the same thread to make multiple calls to mktemp() with 
 * the same root template (if this happens, mktemp will create upto 26
 * unique file names per thread for each unique template). 
 * This point should be documented in the man page. Note, however, that 
 * the mod to mktemp() below allows different threads to call mktemp() with 
 * the same root template (with X's) and still get different names.
 */
char *
mktemp(as)
char *as;
{
	register char *s=as;
	register pid_t pid;
	register unsigned mod;
	register unsigned xcnt=0; /* keeps track of number of X's seen */

#ifdef _REENTRANT
	pid = (pid_t)(getpid() + _thr_self() - 1);
#else
	pid = (pid_t)getpid();
#endif
	s += strlen(as);	/* point at the terminal null */
	while(*--s == 'X' && ++xcnt <= XCNT) {
		mod = pid & 077;	/* use radix-64 arithmetic */
		if (mod > 35)
			*s = mod + '_' - 36;
		else if(mod > 9)
			*s = mod + 'A' - 10;
		else	*s = mod + '0';
		if (*s == '\140') *s = '.';
		pid >>= 6;
	}
	if(*++s) {		/* maybe there were no 'X's */
		*s = 'a';
		while(access(as, 0) == 0) {
			if(++*s > 'z') {
				*as = '\0';
				break;
			}
		}
	} else
		if(access(as, 0) == 0)
			*as = '\0';
	return(as);
}
