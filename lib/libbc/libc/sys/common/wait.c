/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)wait.c	1.13	95/02/28 SMI"

/*******************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice 

Notice of copyright on this source code product does not indicate 
publication.

	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
	          All rights reserved.
********************************************************************/ 

/*
 *      Compatibility lib for BSD's wait3() and wait4().
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/resource.h>
#include "signalmap.h"

/*
 * Since sysV does not support rusage as in BSD, an approximate approach
 * is:
 *      ...
 *      call times
 *      call waitid
 *      if ( a child is found )
 *              call times again
 *              rusage ~= diff in the 2 times call
 *      ...
 * 
 */

extern int errno;

/*
 * arguments to wait functions from SVR4
 */

#define N_WEXITED         0001    /* wait for processes that have exite   */
#define N_WTRAPPED        0002    /* wait for processes stopped while tracing */
#define N_WSTOPPED        0004    /* wait for processes stopped by signals */
#define N_WCONTINUED      0010    /* wait for processes continued */
 
#define N_WUNTRACED       N_WSTOPPED /* for POSIX */
 
#define N_WNOHANG         0100    /* non blocking form of wait    */
#define N_WNOWAIT         0200    /* non destructive form of wait */

#define WCOREFLG	  0200

/*
 * SIGCLD signal codes from SVr4
 */

#define CLD_EXITED      1       /* child has exited */
#define CLD_KILLED      2       /* child was killed */
#define CLD_DUMPED      3       /* child has coredumped */
#define CLD_TRAPPED     4       /* traced child has stopped */
#define CLD_STOPPED     5       /* child has stopped on signal */
#define CLD_CONTINUED   6       /* stopped child has continued */
#define NSIGCLD         6

/* 
 * id type from SVR4 procset.h
 */
typedef enum idtype {
        P_PID,          /* A process identifier.                */
        P_PPID,         /* A parent process identifier.         */
        P_PGID,         /* A process group (job control group)  */
                        /* identifier.                          */
        P_SID,          /* A session identifier.                */
        P_CID,          /* A scheduling class identifier.       */
        P_UID,          /* A user identifier.                   */
        P_GID,          /* A group identifier.                  */
        P_ALL           /* All processes.                       */
} idtype_t;

static void mapstatus(int *, int);

int
wait(int *status)
{
	int ret, nstatus;
	
	if ((int)status == -1) {
		errno = EFAULT;
		return (-1);
	}

	ret = _wait(&nstatus);
	if (status) 
		mapstatus(status, nstatus);
	return (ret);
}

int
waitpid(int pid, int *status, int options)
{
	int noptions, ret;
	int nstatus;

	if ((int)status == -1) {
		errno = EFAULT;
		return (-1);
	}

	/*
	 * BSD's wait* routines only support WNOHANG & WUNTRACED
	 */
	if (options & ~(WNOHANG|WUNTRACED))
		return (EINVAL);
	noptions = (N_WEXITED|N_WTRAPPED);
	if (options & WNOHANG)
		noptions |= N_WNOHANG;
	if (options & WUNTRACED)
		noptions |= N_WUNTRACED;	/* == N_WSTOPPED */
	
	ret = _waitpid(pid, &nstatus, noptions);
	
	if (status)
		mapstatus(status, nstatus);

	return (ret);
}

/*
 * It would be -so- nice just to call _wait3 and mapstatus here.
 */
int
wait3(int *status, int options, struct rusage *rp)
{
	return (wait4(0, status, options, rp));
}

static int wstat(int, int);

/*
 * It would be -so- nice just to call _wait4 and mapstatus here.
 */
int
wait4(int pid, int *status, int options, struct rusage *rp)
{
        struct  tms     before_tms;
        struct  tms     after_tms;
        siginfo_t       info;
        int             error;
        int             noptions;
	idtype_t	idtype;
 
        if ((int)status == -1 || (int)rp == -1) {
                errno = EFAULT;
                return(-1);
        }
 
        if (rp)
                memset(rp, 0, sizeof(struct rusage));
	memset(&info, 0, sizeof (siginfo_t));
        if (times(&before_tms) < 0)
                return (-1);            /* errno is set by times() */

	/*
	 * BSD's wait* routines only support WNOHANG & WUNTRACED
	 */
	if (options & ~(WNOHANG|WUNTRACED))
		return (EINVAL);
	noptions = N_WEXITED | N_WTRAPPED;
	if (options & WNOHANG)
		noptions |= N_WNOHANG;
	if (options & WUNTRACED)
		noptions |= N_WUNTRACED;	/* == N_WSTOPPED */

	/*
	 * Emulate undocumented 4.x semantics for 1186845
	 */
	if (pid < 0) {
		pid = -pid;
		idtype = P_PGID;
	} else if (pid == 0)
		idtype = P_ALL;
	else
		idtype = P_PID;

        error = _waitid(idtype, pid, &info, noptions);
        if (error == 0) {
                long diffu;  /* difference in usertime (ticks) */
                long diffs;  /* difference in systemtime (ticks) */

                if ((options & WNOHANG) && (info.si_pid == 0))
                        return (0);     /* no child found */

		if (rp) {
			if (times(&after_tms) < 0)
				return (-1);    /* errno already set by times() */
			/*
			 * The system/user time is an approximation only !!!
			 */
			diffu = after_tms.tms_cutime - before_tms.tms_cutime;
			diffs = after_tms.tms_cstime - before_tms.tms_cstime;
                	rp->ru_utime.tv_sec = diffu / HZ;
                	rp->ru_utime.tv_usec = (diffu % HZ) * (1000000 / HZ);
                	rp->ru_stime.tv_sec = diffs / HZ;
                	rp->ru_stime.tv_usec = (diffs % HZ) * (1000000 / HZ);
		}
                if (status)
                        *status = wstat(info.si_code, info.si_status);
                return (info.si_pid);
         } else {
                return (-1);            /* error number is set by waitid() */
        }
}


/*
 * Convert the status code to old style wait status
 */
static int
wstat(int code, int status)
{
        register stat = (status & 0377);

        switch (code) {
	case CLD_EXITED:
		stat <<= 8;
		break;
	case CLD_KILLED:
		stat = maptooldsig(stat);
		if (code == CLD_DUMPED)
			stat |= WCOREFLG;
		break;
	case CLD_DUMPED:
		stat |= WCOREFLG;
		break;
	case CLD_TRAPPED:
	case CLD_STOPPED:
		stat = maptooldsig(stat);
		stat <<= 8;
		stat |= _WSTOPPED;
		break;
        }
        return (stat);
}

static void
mapstatus(int *new, int old)
{
	int stat = old & 0xFF;

	switch(stat) {
	case _WSTOPPED:
		*new = maptooldsig(stat >> 8);
		*new = (stat << 8) | _WSTOPPED;
		break;
	case 0:
		*new = old;
		break;
	default:
		*new = maptooldsig(old & 0x7F);
		if (old & 0x80)
			*new |= 0x80;		/* set WCOREFLG */
	}
}
