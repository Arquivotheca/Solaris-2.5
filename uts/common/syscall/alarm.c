/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)alarm.c	1.4	95/09/23 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

/*
 * Introduce a per-thread and a per-process interface for alarm(2).
 * One (per-process) is called for POSIX threads, and the other is
 * kept for compatibility. The per-thread version will disappear (EOL)
 * in a future (post-2.5) release. This has been documented in the
 * man pages in 2.5.
 */
static void
sigalarm2thread(kthread_t *t)
{
        proc_t *p = ttoproc(t);
 
        mutex_enter(&p->p_lock);
        t->t_alarmid = 0;
        sigtoproc(p, t, SIGALRM, 0);
        mutex_exit(&p->p_lock);
}

static void
sigalarm2proc(proc_t *p)
{
	mutex_enter(&p->p_lock);
	p->p_alarmid = 0;
	sigtoproc(p, NULL, SIGALRM, 0);
	mutex_exit(&p->p_lock);
}

/*
 * lwp_alarm() is a new system trap entered via the alarm() entry point
 * in libthread to provide per-lwp SIGALRM semantics. Is introduced
 * only to provide compatibility for existing Solaris thread applications -
 * will be EOL'ed in the future - the move will occur to the per-process
 * semantic provided by alarm() defined below, in the post-2.5 release.
 */
int
lwp_alarm(int deltat)
{
	int	del;
	int	ret;

	/*
	 * We don't need to lock this operation because only
	 * the current thread can set its lwp alarm and the
	 * sigalarm2thread() function only cancels the lwp alarm.
	 */
	del = untimeout(curthread->t_alarmid);
	curthread->t_alarmid = 0;
 
	if (del < 0)
		ret = 0;
	else
		ret = (del + HZ - 1) / HZ;      /* convert to seconds */
	if (deltat)
		curthread->t_alarmid = timeout(sigalarm2thread,
		    (caddr_t)curthread, deltat * HZ);
	return (ret);
}

/*
 * This system trap provides the per-process SIGALRM semantic. This trap
 * is entered via the alarm() entry point in posix threads (libpthread)
 * so POSIX threads have the per-process SIGALRM model. This will also be
 * called by non-threaded applications which call alarm(2).
 */
int
alarm(int deltat)
{
	proc_t	*p = ttoproc(curthread);
	int	del = 0;
	int	ret;
	int	tmp_id;

	/*
	 * We must single-thread this code relative to other
	 * lwps in the same process also performing an alarm().
	 * The mutex dance in the while loop is necessary because
	 * we cannot call untimeout() while holding a lock that
	 * is grabbed by the timeout function, sigalarm2proc().
	 * We can, however, hold p->p_lock across timeout().
	 */
	mutex_enter(&p->p_lock);
	while ((tmp_id = p->p_alarmid) != 0) {
		p->p_alarmid = 0;
		mutex_exit(&p->p_lock);
		del = untimeout(tmp_id);
		mutex_enter(&p->p_lock);
	}

	if (del < 0)
		ret = 0;
	else
		ret = (del + HZ - 1) / HZ;	/* convert to seconds */
	if (deltat)
		p->p_alarmid = timeout(sigalarm2proc, (caddr_t)p, deltat * HZ);
	mutex_exit(&p->p_lock);
	return (ret);
}
