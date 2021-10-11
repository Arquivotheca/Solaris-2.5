/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getpw.c	1.12	93/08/11 SMI"	/* SVr4.0 1.10	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
#ifdef __STDC__
	#pragma weak getpw = _getpw
#endif
#include "synonyms.h"
#include <sys/types.h>
#include <stdio.h>
#include <ctype.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

static FILE *pwf;
#ifdef _REENTRANT
static mutex_t _pwlock = DEFAULTMUTEX;
#endif _REENTRANT
const char *PASSWD = "/etc/passwd";

int
getpw(uid, buf)
uid_t	uid;
char	buf[];
{
	register n, c;
	register char *bp;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT

	if(pwf == 0) {
		_mutex_lock(&_pwlock);
		if (pwf == 0) {
			pwf = fopen(PASSWD, "r");
			if(pwf == NULL) {
				_mutex_unlock(&_pwlock);
				return(1);
			}
		}
		_mutex_unlock(&_pwlock);
	}
	FLOCKFILE(lk, pwf);
	REWIND(pwf);

	for(;;) {
		bp = buf;
		while((c=GETC(pwf)) != '\n') {
			if(c == EOF) {
				FUNLOCKFILE(lk);
				return(1);
			}
			*bp++ = (char)c;
		}
		*bp = '\0';
		bp = buf;
		n = 3;
		while(--n)
			while((c = *bp++) != ':')
				if(c == '\n') {
					FUNLOCKFILE(lk);
					return(1);
				}
		while((c = *bp++) != ':')
			if(isdigit(c))
				n = n*10+c-'0';
			else
				continue;
		if(n == uid) {
			FUNLOCKFILE(lk);
			return(0);
		}
	}
}
