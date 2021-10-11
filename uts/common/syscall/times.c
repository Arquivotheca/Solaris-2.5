/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)times.c	1.2	94/09/13 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/tuneable.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/debug.h>


/*
 * Return system and user times.
 */

int
times(struct tms *tp)
{
	proc_t *p = ttoproc(curthread);
	struct tms	p_time;

	mutex_enter(&p->p_lock);
	p_time.tms_utime = p->p_utime;
	p_time.tms_stime = p->p_stime;
	p_time.tms_cutime = p->p_cutime;
	p_time.tms_cstime = p->p_cstime;
	mutex_exit(&p->p_lock);

	if (copyout((caddr_t)&p_time, (caddr_t)tp, sizeof (struct tms))) {
		return (set_errno(EFAULT));
	}

	return (lbolt);
}
