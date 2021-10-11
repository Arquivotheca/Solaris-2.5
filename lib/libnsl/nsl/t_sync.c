/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_sync.c	1.20	94/03/02 SMI"	/* SVr4.0 1.4.4.1	*/

#include "timt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <errno.h>
#include <rpc/trace.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <tiuser.h>
#include "tli.h"


t_sync(fd)
int fd;
{
	register struct _ti_user *tiptr;

	trace2(TR_t_sync, 0, fd);
	/*
	 * In case of fork/exec'd servers, _t_checkfd() has all
	 * the code to synchronize the tli data structures.
	 *
	 */

	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_sync, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	trace2(TR_t_sync, 1, fd);
	return (tiptr->ti_state);
}
