/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)gid.c	1.6	95/07/14 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/debug.h>

int
setgid(gid_t gid)
{
	register proc_t *p;
	int error = 0;
	cred_t	*cr;

	if (gid < 0 || gid > MAXUID)
		return (set_errno(EINVAL));

	p = ttoproc(curthread);
	mutex_enter(&p->p_crlock);
	cr = p->p_cred;
	if (cr->cr_uid && (gid == cr->cr_rgid || gid == cr->cr_sgid)) {
		cr = crcopy(cr);
		p->p_cred = cr;
		cr->cr_gid = gid;
	} else if (suser(cr)) {
		cr = crcopy(cr);
		p->p_cred = cr;
		cr->cr_gid = gid;
		cr->cr_rgid = gid;
		cr->cr_sgid = gid;
	} else
		error = EPERM;
	mutex_exit(&p->p_crlock);

	if (error == 0) {
		crset(p, cr);		/* broadcast to process threads */
		return (0);
	}
	return (set_errno(error));
}

longlong_t
getgid(void)
{
	rval_t	r;
	cred_t	*cr;

	cr = curthread->t_cred;
	r.r_val1 = cr->cr_rgid;
	r.r_val2 = cr->cr_gid;
	return (r.r_vals);
}

int
setegid(gid_t gid)
{
	register proc_t *p;
	register cred_t	*cr;
	int error;

	if (gid < 0 || gid > MAXUID)
		return (set_errno(EINVAL));

	p = ttoproc(curthread);
	mutex_enter(&p->p_crlock);
	cr = p->p_cred;
	if (gid == cr->cr_rgid || gid == cr->cr_gid || gid == cr->cr_sgid ||
	    suser(cr)) {
		cr = crcopy(cr);
		p->p_cred = cr;
		cr->cr_gid = gid;
		error = 0;
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
 * Like setgid() and setegid() combined -except- that non-root users
 * can change cr_rgid to cr_gid, and the semantics of cr_sgid are
 * subtly different.
 */
int
setregid(gid_t rgid, gid_t egid)
{
	proc_t *p;
	int error = 0;
	cred_t *cr;

	if ((rgid != -1 && (rgid < 0 || rgid > MAXUID)) ||
	    (egid != -1 && (egid < 0 || egid > MAXUID)))
		return (set_errno(EINVAL));

	p = ttoproc(curthread);
	mutex_enter(&p->p_crlock);
	cr = p->p_cred;

	if (rgid != -1 &&
	    rgid != cr->cr_rgid && rgid != cr->cr_gid && !suser(cr)) {
		error = EPERM;
		goto end;
		/*NOTREACHED*/
	}
	if (egid != -1 &&
	    egid != cr->cr_rgid && egid != cr->cr_gid &&
	    egid != cr->cr_sgid && !suser(cr)) {
		error = EPERM;
		goto end;
		/*NOTREACHED*/
	}
	cr = crcopy(cr);
	p->p_cred = cr;

	if (egid != -1)
		cr->cr_gid = egid;
	if (rgid != -1)
		cr->cr_rgid = rgid;
	/*
	 * "If the real gid is being changed, or the effective gid is
	 * being changed to a value not equal to the real gid, the
	 * saved gid is set to the new effective gid."
	 */
	if (rgid != -1 || (egid != -1 && cr->cr_gid != cr->cr_rgid))
		cr->cr_sgid = cr->cr_gid;
end:
	mutex_exit(&p->p_crlock);

	if (error == 0) {
		crset(p, cr);		/* broadcast to process threads */
		return (0);
	}
	return (set_errno(error));
}
