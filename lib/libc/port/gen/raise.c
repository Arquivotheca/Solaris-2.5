/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)raise.c	1.8	95/08/22 SMI"	/* SVr4.0 1.4	*/

/*LINTLIBRARY*/
#include <sys/types.h>
#include "synonyms.h"
#include <signal.h>
#include <unistd.h>
#include <thread.h>

extern int _thr_main(void);
extern int _thr_kill(thread_t, int);
extern thread_t _thr_self(void);

int
raise(int sig)
{
	if (_thr_main() == -1)
		return( kill(getpid(), sig));
	else
		return( _thr_kill(_thr_self(), sig));
}
