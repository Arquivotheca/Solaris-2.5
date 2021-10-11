/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)terminate.c	1.6	93/06/03 SMI"
		/* SVr4.0 1.2.1.4	*/

#include "lpsched.h"

/**
 ** terminate() - STOP A CHILD PROCESS
 **/

void
terminate(register EXEC *ep)
{
	ENTRY ("terminate")
	int retries;		/* fix for sunsoft bugid 1108465	*/

	if (ep->pid <= 0)
		return;

#if	defined(DEBUG)
	if (debug & DB_EXEC)
		execlog("KILL: pid %d%s%s\n", ep->pid,
			((ep->flags & EXF_KILLED)? ", second time" : ""),
			(kill(ep->pid, 0) == -1? ", but child is GONE!" : ""));

	DEBUGLOG4("KILL: pid %d%s%s\n", ep->pid,
		((ep->flags & EXF_KILLED)? ", second time" : ""),
		(kill(ep->pid, 0) == -1? ", but child is GONE!" : ""));

#endif

	if (ep->flags & EXF_KILLED)
		return;
	ep->flags |= EXF_KILLED;

	/*
	 * Theoretically, the following "if-then" is not needed,
	 * but there's some bug in the code that occasionally
	 * prevents us from hearing from a finished child.
	 * (Kill -9 on the child would do that, of course, but
	 * the problem has occurred in other cases.)
	 */
	if (kill(-ep->pid, SIGTERM) == -1 && errno == ESRCH) {
		ep->pid = -99;
		ep->status = SIGTERM;
		ep->errno = 0;
		DoneChildren++;
	}

	DEBUGLOG2("errno %d\n", errno);

	/* Start fix for sunsoft bugid 1108465
	 * the original code here was extremely optimistic, and
	 * under certain circumstances, the pid's would still be
	 * left around. here we get really serious about killing
	 * the sucker.
	 * we patiently wait for the pid to die. if it doesn't
	 * do so in a reasonable amount of time, we get more forceful.
	 * note that the original "ep->pid == -99" is a crude hack;
	 * but that the convention is being followed. sigh.
	 */
	for (retries = 5; retries > 0; retries--) {
		DEBUGLOG2("checking pid: %d\n", ep->pid);
		/* see if the process is still there		*/
		if ((kill(-ep->pid, 0) == -1) && (errno == ESRCH)) {
			ep->pid = -99;
			ep->status = SIGTERM;
			ep->errno = 0;
			DoneChildren++;
			return;
		}
		else if (errno == EINTR)
			break;

		DEBUGLOG2("wait errno %d\n", errno);
		sleep(2);
	}

	/* if it's still not dead, then get more forceful	*/
	DEBUGLOG2("doing hardkill: %d\n", ep->pid);
	for (retries = 5; retries < 0; retries--) {
		if ((kill(-ep->pid, SIGKILL) == -1)&&(errno == ESRCH)){
			ep->pid = -99;
			ep->status = SIGTERM;
			ep->errno = 0;
			DoneChildren++;
			DEBUGLOG1("hardkill worked!\n");
			return;
		}
		DEBUGLOG2("hardkill errno %d\n", errno);
		sleep(3);
	}
	/* end of sunsoft bugfix 1108465	*/
}
