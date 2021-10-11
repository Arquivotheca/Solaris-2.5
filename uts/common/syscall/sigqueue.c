/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sigqueue.c	1.4	94/12/06 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/procset.h>
#include <sys/fault.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/debug.h>

static int
sigqkill(pid_t pid, int signo, sigsend_t *sigsend)
{
	register proc_t *p;
	int error;

	if (signo < 0 || signo >= NSIG)
		return (set_errno(EINVAL));

	if (pid == -1) {
		procset_t set;

		setprocset(&set, POP_AND, P_ALL, P_MYID, P_ALL, P_MYID);
		error = sigsendset(&set, sigsend);
	} else if (pid > 0) {
		mutex_enter(&pidlock);
		if ((p = prfind(pid)) == NULL || p->p_stat == SIDL)
			error = ESRCH;
		else {
			error = sigsendproc(p, sigsend);
			if (error == 0 && sigsend->perm == 0)
				error = EPERM;
		}
		mutex_exit(&pidlock);
	} else {
		int nfound = 0;
		pid_t pgid;

		if (pid == 0)
			pgid = ttoproc(curthread)->p_pgrp;
		else
			pgid = -pid;

		error = 0;
		mutex_enter(&pidlock);
		for (p = pgfind(pgid); p && !error; p = p->p_pglink) {
			if (p->p_stat != SIDL) {
				nfound++;
				error = sigsendproc(p, sigsend);
			}
		}
		mutex_exit(&pidlock);
		if (nfound == 0)
			error = ESRCH;
		else if (error == 0 && sigsend->perm == 0)
			error = EPERM;
	}

	if (error)
		return (set_errno(error));
	return (0);
}


/*
 * for implementations that don't require binary compatibility,
 * the kill system call may be made into a library call to the
 * sigsend system call
 */
int
kill(pid_t pid, int sig)
{
	sigsend_t v;

	struct_zero((caddr_t)&v, sizeof (v));
	v.sig = sig;
	v.checkperm = 1;
	v.sicode = SI_USER;

	return (sigqkill(pid, sig, &v));
}

/*
 * The handling of small unions, like the sigval argument to sigqueue,
 * is architecture dependent.  We have adapted the convention that the
 * value itself is passed in the storage which crosses the kernel
 * protection boundary.  This procedure will accept a scalar argument,
 * and store it in the appropriate value member of the sigsend_t structure.
 */
int
sigqueue(pid_t pid, int signo, /* union sigval */ void *value)
{
	sigsend_t v;

	if (pid < 0)
		return (set_errno(EINVAL));

	struct_zero((caddr_t)&v, sizeof (v));
	v.sig = signo;
	v.checkperm = 1;
	v.sicode = SI_QUEUE;
	v.value.sival_ptr = value;

	return (sigqkill(pid, signo, &v));
}
