/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)uid.c	1.8	95/07/14 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/tuneable.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/debug.h>

int
setuid(uid_t uid)
{
	register proc_t *p;
	int error = 0;
	cred_t	*cr;
	uid_t oldruid = uid;

	if (uid < 0 || uid > MAXUID)
		return (set_errno(EINVAL));

	p = ttoproc(curthread);
	mutex_enter(&p->p_crlock);
	cr = p->p_cred;
	if (cr->cr_uid && (uid == cr->cr_ruid || uid == cr->cr_suid)) {
		cr = crcopy(cr);
		p->p_cred = cr;
		cr->cr_uid = uid;
	} else if (suser(cr)) {
		oldruid = cr->cr_ruid;
		cr = crcopy(cr);
		p->p_cred = cr;
		cr->cr_ruid = uid;
		cr->cr_suid = uid;
		cr->cr_uid = uid;
	} else
		error = EPERM;
	mutex_exit(&p->p_crlock);

	if (oldruid != uid) {
		mutex_enter(&pidlock);
		upcount_dec(oldruid);
		upcount_inc(uid);
		mutex_exit(&pidlock);
	}

	if (error == 0) {
		crset(p, cr);		/* broadcast to process threads */
		return (0);
	}
	return (set_errno(error));
}

longlong_t
getuid(void)
{
	rval_t	r;
	cred_t *cr;

	cr = curthread->t_cred;
	r.r_val1 = cr->cr_ruid;
	r.r_val2 = cr->cr_uid;
	return (r.r_vals);
}

int
seteuid(uid_t uid)
{
	register proc_t *p;
	int error = 0;
	cred_t	*cr;

	if (uid < 0 || uid > MAXUID)
		return (set_errno(EINVAL));

	p = ttoproc(curthread);
	mutex_enter(&p->p_crlock);
	cr = p->p_cred;
	if (uid == cr->cr_ruid || uid == cr->cr_uid || uid == cr->cr_suid ||
	    suser(cr)) {
		cr = crcopy(cr);
		p->p_cred = cr;
		cr->cr_uid = uid;
	} else
		error = EPERM;
	mutex_exit(&p->p_crlock);

	if (error == 0) {
		crset(p, cr);		/* broadcast to process threads */
		return (0);
	}
	return (set_errno(error));
}

/*
 * Buy-back from SunOS 4.x
 *
 * Like setuid() and seteuid() combined -except- that non-root users
 * can change cr_ruid to cr_uid, and the semantics of cr_suid are
 * subtly different.
 */
int
setreuid(uid_t ruid, uid_t euid)
{
	proc_t *p;
	int error = 0;
	uid_t oldruid = ruid;
	cred_t *cr;

	if ((ruid != -1 && (ruid < 0 || ruid > MAXUID)) ||
	    (euid != -1 && (euid < 0 || euid > MAXUID)))
		return (set_errno(EINVAL));

	p = ttoproc(curthread);
	mutex_enter(&p->p_crlock);
	cr = p->p_cred;

	if (ruid != -1 &&
	    ruid != cr->cr_ruid && ruid != cr->cr_uid && !suser(cr)) {
		error = EPERM;
		goto end;
		/*NOTREACHED*/
	}
	if (euid != -1 &&
	    euid != cr->cr_ruid && euid != cr->cr_uid &&
	    euid != cr->cr_suid && !suser(cr)) {
		error = EPERM;
		goto end;
		/*NOTREACHED*/
	}
	cr = crcopy(cr);
	p->p_cred = cr;

	if (euid != -1)
		cr->cr_uid = euid;
	if (ruid != -1) {
		oldruid = cr->cr_ruid;
		cr->cr_ruid = ruid;
	}
	/*
	 * "If the real uid is being changed, or the effective uid is
	 * being changed to a value not equal to the real uid, the
	 * saved uid is set to the new effective uid."
	 */
	if (ruid != -1 || (euid != -1 && cr->cr_uid != cr->cr_ruid))
		cr->cr_suid = cr->cr_uid;
end:
	mutex_exit(&p->p_crlock);

	if (oldruid != ruid) {
		ASSERT(oldruid != -1 && ruid != -1);
		mutex_enter(&pidlock);
		upcount_dec(oldruid);
		upcount_inc(ruid);
		mutex_exit(&pidlock);
	}

	if (error == 0) {
		crset(p, cr);		/* broadcast to process threads */
		return (0);
	}
	return (set_errno(error));
}
